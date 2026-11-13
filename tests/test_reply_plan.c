/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_reply_plan.c
 * @brief 应答计划：主泵发布、ISR 执行、失效与回退语义。
 *
 * 这些用例把"主泵发布计划"和"ISR 执行计划"分开驱动，覆盖：
 *  - 命中计划的即时应答（t_A 路径）；
 *  - 计划外请求的显式延迟路径（窗口仍开时主泵给出相同字节）；
 *  - 延迟到 t_A 之后才处理的帧不得发送应答，只计入 late_reply_dropped；
 *  - 连续 ISDU 写段耗尽暂存深度：拒绝新段、计数、不应答；
 *  - t_PD 保护窗口内丢弃的邮箱帧可观测；
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

#if IOLINK_OD_SLOTS > 15
#error "test_reply_plan burst fixture needs FlowCTRL COUNT values (IOLINK_OD_SLOTS <= 15)"
#endif

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
    if (s_dll.main.shared.isr_plan_misses != 0U) {
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
    if (s_dll.main.shared.isr_plan_misses != 1U) {
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

/* 连续 ISDU 写段、主泵完全没有排空机会：暂存深度用尽后必须
 * 拒绝新段、计数，且不对该帧应答（主站超时重发）。 */
static int test_isdu_write_burst_overruns_without_drain(void)
{
    /* FlowCTRL 的 COUNT 只有 4 位，本用例用段号直接当 COUNT。 */

    init_operate();
    /* I-Service 0x1F：写 + 8 位索引，低半字节声明整条请求 15 字节，
     * 因此 8 个段内不会完成传输。 */
    static const uint8_t prefix[3] = {0x1FU, 0x10U, 0x00U};

    for (uint8_t i = 0U; i <= IOLINK_OD_SLOTS; i++) {
        const uint8_t flowctrl = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) (i % 16U);
        uint8_t frame[3];
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | flowctrl), IOLINK_MSEQ_TYPE_2, frame,
                   sizeof(frame));
        frame[2] = (i < sizeof(prefix)) ? prefix[i] : 0x00U;
        frame[1] = IOLINK_MSEQ_TYPE_2;
        frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));

        s_tx_n = 0U;
        feed(frame, sizeof(frame), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        if (i < IOLINK_OD_SLOTS) {
            if (s_tx_n == 0U) {
                return 1; /* 已暂存：ISR 必须立即确认（PD-in + CKS） */
            }
        }
        else if (s_tx_n != 0U) {
            return 2; /* 暂存失败：不得应答 */
        }
    }

    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&s_dll.main, &stats);
    if (stats.isr_od_overruns != 1U) {
        return 3;
    }
    if (stats.isr_plan_misses != 1U) {
        return 4;
    }
    if (iolink_ring_count(&s_dll.main.shared.isr_od_ring) != (uint32_t) IOLINK_OD_SLOTS) {
        return 5;
    }

    /* 主泵恢复后一次性排空所有暂存段。 */
    iolink_dll_process(&s_dll.main);
    if (!iolink_ring_is_empty(&s_dll.main.shared.isr_od_ring)) {
        return 6;
    }
    if (s_dll.main.shared.isdu.req_idx != (uint8_t) IOLINK_OD_SLOTS) {
        return 7;
    }
    return 0;
}

/* 写段未暂存时，即使主泵仍在 t_A 窗口内也不得确认该段。 */
static int test_unstaged_write_is_not_acknowledged(void)
{
    init_operate();

    /* 手工填满暂存环（等价于主泵长时间未排空）。 */
    for (uint8_t i = 0U; i < IOLINK_OD_SLOTS; i++) {
        iolink_od_rec_t* rec = (iolink_od_rec_t*) iolink_ring_claim(&s_dll.main.shared.isr_od_ring);
        if (rec == NULL) {
            return 1;
        }
        rec->flowctrl = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) i;
        rec->len = 1U;
        rec->data[0] = 0x00U;
        iolink_ring_publish(&s_dll.main.shared.isr_od_ring);
    }

    uint8_t frame[3];
    make_frame(IOLINK_MC_CHANNEL_ISDU, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    s_tx_n = 0U;
    feed(frame, sizeof(frame), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 2; /* ISR 不能确认未暂存的段 */
    }
    if (s_dll.main.shared.isr_od_overruns != 1U) {
        return 3;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || ((rec->flags & IOLINK_FRAME_FLAG_WRITE_NOT_STAGED) == 0U)) {
        return 4;
    }

    /* 主泵在 t_A 窗口内处理该帧：仍不得确认。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 5;
    }
    if (s_dll.main.late_reply_dropped != 0U) {
        return 6; /* 这是“未暂存”，不是“窗口已关” */
    }
    return 0;
}

/* t_A 已经过期：主泵不得发送迟到应答（否则抢总线），只计数。 */
static int test_late_reply_is_suppressed(void)
{
    init_operate();
    /* 撤回全部计划：ISR 无法即时应答。 */
    s_dll.main.shared.reply_plan_seq = 0U;
    s_dll.main.shared.reply_plan_count = 0U;

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 1;
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 2;
    }

    /* 主站请求结束后超过 10 个位时间（COM2: 260 us）才轮到主泵。 */
    s_now_us += 1000U;
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 3;
    }
    if (s_dll.main.late_reply_dropped != 1U) {
        return 4;
    }

    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&s_dll.main, &stats);
    if (stats.late_reply_dropped != 1U) {
        return 5;
    }
    return 0;
}

/* t_PD 保护窗口内丢弃的邮箱帧必须计数（此前完全静默）。 */
static int test_t_pd_discards_are_counted(void)
{
    init_operate();
    s_dll.main.shared.reply_plan_seq = 0U;
    s_dll.main.shared.reply_plan_count = 0U;

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main); /* 计划外：进邮箱 */
    if (iolink_ring_count(&s_dll.main.shared.isr_frame_ring) != 1U) {
        return 1;
    }

    dll_timer_arm(&s_dll.main, IOLINK_TIMER_T_PD, s_now_us + 10000U);
    iolink_dll_process(&s_dll.main); /* t_PD 有效：丢弃窗口内的帧 */
    if (!iolink_ring_is_empty(&s_dll.main.shared.isr_frame_ring)) {
        return 2;
    }
    if (s_dll.main.t_pd_discards != 1U) {
        return 3;
    }

    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&s_dll.main, &stats);
    if (stats.t_pd_discards != 1U) {
        return 4;
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
    if (s_dll.main.shared.isr_plan_misses != 1U) {
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

/** @brief 计划表中 Type-0 页读槽位的动作；未找到返回 0xFF。 */
static uint8_t type0_page_read_action(void)
{
    for (uint8_t i = 0U; i < s_dll.main.shared.reply_plan_count; i++) {
        const iolink_reply_plan_t* plan = &s_dll.main.shared.reply_plan[i];
        if ((plan->ckt_mask == IOLINK_MSEQ_TYPE_MASK) && (plan->ckt_value == IOLINK_MSEQ_TYPE_0)) {
            return plan->action;
        }
    }
    return 0xFFU;
}

/** @brief OPERATE Type-0 页读请求（2 字节，无 OD）。 */
static void feed_type0_page_read(void)
{
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    feed(frame, sizeof(frame), 0U);
}

/* 运行期能力位变化必须使计划失效：键不含能力位，不失效就会继续用旧计划。 */
static int test_capability_change_invalidates_plan(void)
{
    init_operate();
    /* 非兼容模式：OPERATE 中的 Type-0 页读是复位请求（不应答）。 */
    s_dll.main.shared.caps &= (iolink_caps_t) ~IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_invalidate_reply_plans(&s_dll.main);
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 1;
    }
    const IOLINK_ATOMIC_UINT_T seq_before = s_dll.main.shared.reply_plan_seq;
    if ((seq_before == 0U) || ((seq_before & 1U) != 0U)) {
        return 2;
    }

    /* 模拟 iolink_device_set_capabilities()：只改 caps。键不变，
     * 未发失效请求时发布必须提前返回，表内容保持旧语义。 */
    s_dll.main.shared.caps |= IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 3;
    }
    if (s_dll.main.shared.reply_plan_seq != seq_before) {
        return 4;
    }

    /* 失效：发布字清 0，ISR 把该请求当"计划外"交给主泵。 */
    iolink_dll_invalidate_reply_plans(&s_dll.main);
    if (s_dll.main.shared.reply_plan_seq != 0U) {
        return 5;
    }
    if (s_dll.main.reply_plan_key != 0U) {
        return 6;
    }
    s_tx_n = 0U;
    feed_type0_page_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 7; /* 失效期间没有即时应答 */
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 8;
    }

    /* 重发布：计划表随新能力位改变，ISR 恢复即时应答。 */
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_NONE) {
        return 9; /* 兼容模式：普通页读应答 */
    }
    const IOLINK_ATOMIC_UINT_T seq_after = s_dll.main.shared.reply_plan_seq;
    if ((seq_after == 0U) || ((seq_after & 1U) != 0U)) {
        return 10;
    }
    s_tx_n = 0U;
    feed_type0_page_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 2U) {
        return 11; /* 页字节 + CKS */
    }
    return 0;
}

/* tx_done 只是可选的帧结束通知：只有 tx_octet 的 PHY 仍应置位 CAP_TX 并能发送。 */
static int test_tx_without_done_hook(void)
{
    iolink_phy_api_t phy = s_phy;
    phy.tx_done = NULL; /* 变体：去掉可选完成回调 */

    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 1U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    s_dll.main.shared.pd_in[0][1] = 0x5AU;
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;

    if (!iolink_caps_has(s_dll.main.shared.caps, IOLINK_CAP_TX)) {
        return 1;
    }
    iolink_dll_publish_reply_plans(&s_dll.main);
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 3U) {
        return 2; /* OD 占位 + PD-in 1 + CKS */
    }
    if ((s_tx_q[0] != 0x00U) || (s_tx_q[1] != 0x5AU)) {
        return 3;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 4;
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
    rc = test_isdu_write_burst_overruns_without_drain();
    if (rc != 0) {
        return 25 + rc;
    }
    rc = test_unstaged_write_is_not_acknowledged();
    if (rc != 0) {
        return 22 + rc;
    }
    rc = test_late_reply_is_suppressed();
    if (rc != 0) {
        return 27 + rc;
    }
    rc = test_t_pd_discards_are_counted();
    if (rc != 0) {
        return 29 + rc;
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
    rc = test_plan_deadline_metadata();
    if (rc != 0) {
        return 60 + rc;
    }
    rc = test_capability_change_invalidates_plan();
    if (rc != 0) {
        return 70 + rc;
    }
    rc = test_tx_without_done_hook();
    if (rc != 0) {
        return 80 + rc;
    }
    return 0;
}
