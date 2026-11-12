/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_reply_plan.c
 * @brief P5 应答计划：主泵发布、ISR 执行、失效与回退语义。
 *
 * 这些用例把"主泵发布计划"和"ISR 执行计划"分开驱动，覆盖：
 *  - 命中计划的即时应答（t_A 路径）；
 *  - 计划外请求的显式延迟路径（主泵仍给出完全相同的字节）；
 *  - ISDU 写暂存后的读视图失效（防"写后立刻读"读到旧值）；
 *  - seqlock 发布窗口：主泵正在覆盖计划表时 ISR 不得执行半更新计划；
 *  - 计划携带的 t_A 预算元数据。
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"

#define RX_MAX 16U
#define TX_MAX 32U

static iolink_dll_ctx_t s_dll;
static uint64_t s_now_us;

static uint8_t s_rx_q[RX_MAX];
static size_t s_rx_n;
static size_t s_rx_i;
static uint8_t s_chk_pending;

static uint8_t s_tx_q[TX_MAX];
static size_t s_tx_n;
static uint8_t s_tx_active;

static uint64_t plan_time_us(void* user)
{
    (void) user;
    return s_now_us;
}

static int plan_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (s_rx_i < s_rx_n) {
        *data = s_rx_q[s_rx_i++];
        return 1;
    }
    if (s_chk_pending != 0U) {
        /* Type-0 帧由收发器的帧结束（CHK）定界。 */
        s_chk_pending = 0U;
        *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}

static int plan_tx_octet(void* user, uint8_t data)
{
    (void) user;
    if (!s_tx_active) {
        s_tx_n = 0U;
        s_tx_active = 1U;
    }
    if (s_tx_n < sizeof(s_tx_q)) {
        s_tx_q[s_tx_n++] = data;
    }
    return 0;
}

static void plan_tx_done(void* user)
{
    (void) user;
    s_tx_active = 0U;
}

static uint8_t plan_int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((s_rx_i < s_rx_n) || (s_chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U))
               ? 1U
               : 0U;
}

static const iolink_phy_api_t s_phy = {
    .user = &s_dll,
    .rx_octet = plan_rx_octet,
    .tx_octet = plan_tx_octet,
    .tx_done = plan_tx_done,
    .int_active = plan_int_active,
};

/** @brief 补全一条 M-sequence：CKT 承载类型位 + A.1.6 CKS。 */
static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void feed(const uint8_t* frame, size_t len, uint8_t chk)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    s_chk_pending = chk;
}

static void drain_tx(void)
{
    while (s_dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_dll.main);
    }
}

/** @brief OPERATE / TYPE_2_V：PD-in 1、PD-out 0、OD 1。 */
static void init_operate(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 1U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    s_dll.main.shared.pd_in[0][1] = 0x5AU; /* PD-in 槽位（OD 头之后） */
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief Type-2 过程通道读（无 OD、无 PD-out）。 */
static void feed_process_read(void)
{
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_2, frame,
               sizeof(frame));
    feed(frame, sizeof(frame), 0U);
}

/* 命中计划：ISR 直接应答，帧仍带 eager=1 发布给主泵。 */
static int test_plan_hit_executes_reply(void)
{
    init_operate();
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (s_tx_n != 3U) {
        return 1; /* OD 占位 + PD-in 1 + CKS */
    }
    if ((s_tx_q[0] != 0x00U) || (s_tx_q[1] != 0x5AU)) {
        return 2;
    }
    if (s_dll.isr.isr_plan_misses != 0U) {
        return 3;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 1U)) {
        return 4;
    }
    return 0;
}

/* 计划外请求：ISR 不应答，走显式延迟路径；主泵随后给出相同字节。 */
static int test_missing_plan_defers(void)
{
    init_operate();
    /* 撤回全部计划：发布字为 0 表示"没有计划表"。 */
    s_dll.main.shared.reply_plan_seq = 0U;
    s_dll.main.shared.reply_plan_count = 0U;

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 1; /* ISR 绝不应答计划外请求 */
    }
    if (s_dll.isr.isr_plan_misses != 1U) {
        return 2;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 0U)) {
        return 3;
    }

    /* 主泵按既有分发路径应答：与命中计划时逐字节一致。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_tx_n != 3U) {
        return 4;
    }
    if ((s_tx_q[0] != 0x00U) || (s_tx_q[1] != 0x5AU)) {
        return 5;
    }
    return 0;
}

/* 主泵正在覆盖计划表（发布字为奇数）：不得执行半更新计划。 */
static int test_seqlock_window_is_a_miss(void)
{
    init_operate();
    const IOLINK_ATOMIC_UINT_T stable = s_dll.main.shared.reply_plan_seq;
    s_dll.main.shared.reply_plan_seq = (IOLINK_ATOMIC_UINT_T) (stable + 1U); /* 写入中 */

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 1;
    }
    if (s_dll.isr.isr_plan_misses != 1U) {
        return 2;
    }

    s_dll.main.shared.reply_plan_seq = stable;
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 3U) {
        return 3;
    }
    return 0;
}

/* ISDU 写暂存后，读视图必须失效：下一次 START 只能返回"无服务"。 */
static int test_isdu_write_invalidates_read_view(void)
{
    init_operate();
    /* 已完成的应答（4 字节）作为当前读视图。 */
    s_dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    s_dll.main.shared.isdu.resp_buf[0] = 0xD4U;
    s_dll.main.shared.isdu.resp_buf[1] = 0x12U;
    s_dll.main.shared.isdu.resp_buf[2] = 0x34U;
    s_dll.main.shared.isdu.resp_buf[3] = 0xF2U;
    s_dll.main.shared.isdu.resp_total = 4U;
    iolink_dll_process(&s_dll.main); /* 发布快照（代数 +1） */
    if (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) != 4U) {
        return 1;
    }
    const uint8_t gen_before = IOLINK_ISDU_PUB_GEN(s_dll.main.shared.isr_isdu_pub);

    /* 先读一次 START：拿到旧应答的首字节（od_width = 1）。 */
    uint8_t frame[3];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    s_tx_n = 0U;
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    /* OPERATE 长应答：[OD][PD-in]CKS。 */
    if ((s_tx_n != 3U) || (s_tx_q[0] != 0xD4U) || (s_tx_q[1] != 0x5AU)) {
        return 2;
    }

    /* ISDU 写（Type-2_V：PD-out 0，offset 2 处一个 OD 八位组）。 */
    uint8_t wr[3];
    make_frame(IOLINK_MC_CHANNEL_ISDU, IOLINK_MSEQ_TYPE_2, wr, sizeof(wr));
    wr[2] = 0x01U;
    wr[1] = IOLINK_MSEQ_TYPE_2;
    wr[1] = (uint8_t) (wr[1] | iolink_checksum6(wr, sizeof(wr)));
    feed(wr, sizeof(wr), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_dll.main.shared.isr_isdu_invalidated != 1U) {
        return 3; /* 写已暂存：主泵必须重建读视图 */
    }

    /* 主泵尚未应用该写：START 只能读到"无服务"（0x00）。 */
    s_tx_n = 0U;
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((s_tx_n != 3U) || (s_tx_q[0] != 0x00U)) {
        return 4; /* 不得再返回 0xD4（旧应答） */
    }

    /* 主泵应用写并产出新应答：失效旗标清除，代数翻转。 */
    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isr_isdu_invalidated != 0U) {
        return 5;
    }
    const uint8_t gen_after = IOLINK_ISDU_PUB_GEN(s_dll.main.shared.isr_isdu_pub);
    if ((gen_after == gen_before) && (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) == 4U)) {
        return 6; /* 旧视图被重新发布：代数必须能区分两份应答 */
    }
    return 0;
}

/* 启动探测计划：STARTUP 中的页读同时完成 T1 迁移。 */
static int test_startup_probe_plan(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 1U;
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;

    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    feed(frame, sizeof(frame), 1U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (s_dll.main.shared.state != IOLINK_DLL_STATE_PREOPERATE) {
        return 1;
    }
    if (s_dll.main.shared.isr_dsio_clear == 0U) {
        return 2;
    }
    if (s_tx_n != 2U) {
        return 3; /* 页字节 + CKS */
    }
    return 0;
}

/* 计划携带 t_A 预算：COM2 下为 10 * 26 us = 260 us。 */
static int test_plan_deadline_metadata(void)
{
    init_operate();
    if (s_dll.main.shared.reply_plan_count == 0U) {
        return 1;
    }
    for (uint8_t i = 0U; i < s_dll.main.shared.reply_plan_count; i++) {
        if (s_dll.main.shared.reply_plan[i].deadline_us != 260U) {
            return 2;
        }
    }
    return 0;
}

int main(void)
{
    int rc = test_plan_hit_executes_reply();
    if (rc != 0) {
        return 10 + rc;
    }
    rc = test_missing_plan_defers();
    if (rc != 0) {
        return 20 + rc;
    }
    rc = test_seqlock_window_is_a_miss();
    if (rc != 0) {
        return 30 + rc;
    }
    rc = test_isdu_write_invalidates_read_view();
    if (rc != 0) {
        return 40 + rc;
    }
    rc = test_startup_probe_plan();
    if (rc != 0) {
        return 50 + rc;
    }
    return test_plan_deadline_metadata();
}
