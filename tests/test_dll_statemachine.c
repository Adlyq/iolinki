/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll_statemachine.c
 * @brief P9 状态机表：相位性质、计划状态戳与迁移后的失效/重发布。
 *
 * 性质："任何状态下都不会对错误 M-sequence 相位的帧发字节"。
 * 做法：对每个 DL 状态 × {Type-0/1/2} × {过程/页通道} 的合法长度帧，
 * 走完 ISR 泵 + 主泵，比较实际发送字节与"该状态允许的类型"。
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/dll_sm.h"
#include "iolinki/iolink.h"
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

static uint64_t sm_time_us(void* user)
{
    (void) user;
    return s_now_us;
}

static int sm_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (s_rx_i < s_rx_n) {
        *data = s_rx_q[s_rx_i++];
        return 1;
    }
    if (s_chk_pending != 0U) {
        s_chk_pending = 0U;
        *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}

static int sm_tx_octet(void* user, uint8_t data)
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

static void sm_tx_done(void* user)
{
    (void) user;
    s_tx_active = 0U;
}

static uint8_t sm_int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((s_rx_i < s_rx_n) || (s_chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U))
               ? 1U
               : 0U;
}

static const iolink_phy_api_t s_phy = {
    .user = &s_dll,
    .rx_octet = sm_rx_octet,
    .tx_octet = sm_tx_octet,
    .tx_done = sm_tx_done,
    .int_active = sm_int_active,
};

static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void feed(const uint8_t* frame, size_t len)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    s_chk_pending = 1U;
    s_tx_n = 0U;
    s_tx_active = 0U;
}

static void drain_tx(void)
{
    while (s_dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_dll.main);
    }
}

/** @brief 直接构造一个已确认状态；不经过状态机（性质测试需要每个相位）。 */
static void init_state(iolink_dll_state_t state)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = sm_time_us;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_1_2;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.state = state;
    s_now_us = 1000000U;
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/* 过程通道 Type-0 只在 Type-0 配置或启动相位合法。 */
static int type0_process_allowed(iolink_dll_state_t state)
{
    return (state == IOLINK_DLL_STATE_STARTUP) || (state == IOLINK_DLL_STATE_AWAITING_COMM) ||
           (state == IOLINK_DLL_STATE_PREOPERATE) || (state == IOLINK_DLL_STATE_FALLBACK);
}

/* 配置为 TYPE_1_2：配置类型的交换只在 ESTAB_COM / OPERATE 合法。 */
static int type1_allowed(iolink_dll_state_t state)
{
    return (state == IOLINK_DLL_STATE_ESTAB_COM) || (state == IOLINK_DLL_STATE_OPERATE);
}

/* 每一相位只允许其合法类型的帧产生字节；COMM_DISABLED 永不应答。 */
static int test_phase_property(void)
{
    static const iolink_dll_state_t states[] = {
        IOLINK_DLL_STATE_STARTUP,       IOLINK_DLL_STATE_AWAITING_COMM, IOLINK_DLL_STATE_PREOPERATE,
        IOLINK_DLL_STATE_ESTAB_COM,     IOLINK_DLL_STATE_OPERATE,       IOLINK_DLL_STATE_FALLBACK,
        IOLINK_DLL_STATE_COMM_DISABLED,
    };
    uint8_t frame[2];

    for (size_t s = 0U; s < sizeof(states) / sizeof(states[0]); s++) {
        const iolink_dll_state_t state = states[s];

        /* Type-0 页读：启动相位应答；ESTAB/OPERATE 在非兼容配置下是复位
         * 请求（不应答、回 STARTUP）；FALLBACK/COMM_DISABLED 无服务。 */
        init_state(state);
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_0,
                   frame, 2U);
        feed(frame, 2U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        iolink_dll_process(&s_dll.main);
        drain_tx();
        const int page_reply =
            (state == IOLINK_DLL_STATE_STARTUP) || (state == IOLINK_DLL_STATE_AWAITING_COMM) ||
            (state == IOLINK_DLL_STATE_PREOPERATE)
#if IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
            || (state == IOLINK_DLL_STATE_ESTAB_COM) || (state == IOLINK_DLL_STATE_OPERATE)
#endif
            ;
        if (page_reply) {
            if (s_tx_n == 0U) {
                return 10 + (int) state;
            }
        }
        else if (s_tx_n != 0U) {
            return 20 + (int) state;
        }

        /* Type-0 过程通道读：只在启动相位（以及 Type-0 配置）合法。 */
        init_state(state);
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_0,
                   frame, 2U);
        feed(frame, 2U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        iolink_dll_process(&s_dll.main);
        drain_tx();
        if (!type0_process_allowed(state) && (s_tx_n != 0U)) {
            return 30 + (int) state;
        }

        /* 配置类型（TYPE_1）的过程通道读：只有 ESTAB/OPERATE 合法。 */
        init_state(state);
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_1,
                   frame, 2U);
        feed(frame, 2U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        iolink_dll_process(&s_dll.main);
        drain_tx();
        if (type1_allowed(state)) {
            if (s_tx_n == 0U) {
                return 40 + (int) state;
            }
        }
        else if (s_tx_n != 0U) {
            return 50 + (int) state;
        }

        /* 非配置类型（TYPE_2）在任何相位都不得产生字节。 */
        init_state(state);
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_2,
                   frame, 2U);
        feed(frame, 2U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        iolink_dll_process(&s_dll.main);
        drain_tx();
        if (s_tx_n != 0U) {
            return 60 + (int) state;
        }
    }
    return 0;
}

/* 计划表携带发布状态；状态迁移后旧计划自动失效，重发布后恢复即时应答。 */
static int test_plan_state_stamp_and_republish(void)
{
    init_state(IOLINK_DLL_STATE_PREOPERATE);

    /* 已发布计划必须全部属于当前状态。 */
    const uint8_t count = s_dll.main.shared.reply_plan_count;
    if (count == 0U) {
        return 1;
    }
    for (uint8_t i = 0U; i < count; i++) {
        if (s_dll.main.shared.reply_plan[i].state != (uint8_t) IOLINK_DLL_STATE_PREOPERATE) {
            return 2;
        }
    }

    /* 过渡命令由 ISR 计划消费并把状态推进到 ESTAB_COM（不应答）。 */
    uint8_t frame[2];
    make_frame(IOLINK_MC_TRANSITION_COMMAND, IOLINK_MSEQ_TYPE_0, frame, 2U);
    feed(frame, 2U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_dll.main.shared.state != IOLINK_DLL_STATE_ESTAB_COM) {
        return 3;
    }
    if (s_tx_n != 0U) {
        return 4;
    }

    /* 计划表仍是 PREOPERATE 版本：ISR 不得执行，帧必须交给主泵。
     * Type-0 页读在 PREOPERATE 计划里是"页字节+CKS"，在 ESTAB_COM 里
     * 语义已变；只有状态戳能让 ISR 拒绝旧计划。 */
    const uint32_t misses_before = s_dll.isr.isr_plan_misses;
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, 2U);
    feed(frame, 2U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 5;
    }
    if (s_dll.isr.isr_plan_misses != (misses_before + 1U)) {
        return 6;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 0U)) {
        return 7;
    }

    /* 主泵按状态机表应答并重发布计划（ESTAB -> OPERATE）。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_dll.main.shared.state != IOLINK_DLL_STATE_OPERATE) {
        return 8;
    }
    if (s_tx_n == 0U) {
        return 9;
    }
    if (s_dll.main.shared.reply_plan_count == 0U) {
        return 10;
    }
    for (uint8_t i = 0U; i < s_dll.main.shared.reply_plan_count; i++) {
        if (s_dll.main.shared.reply_plan[i].state != (uint8_t) IOLINK_DLL_STATE_OPERATE) {
            return 11;
        }
    }

    /* 重发布后，配置类型的过程读恢复 ISR 即时应答。 */
    const uint32_t misses_after = s_dll.isr.isr_plan_misses;
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_1, frame,
               2U);
    feed(frame, 2U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n == 0U) {
        return 12;
    }
    if (s_dll.isr.isr_plan_misses != misses_after) {
        return 13;
    }
    return 0;
}

/* 表项越界安全；每格都有动作。 */
static int test_table_accessors(void)
{
    for (uint8_t state = 0U; state < (uint8_t) IOLINK_DLL_STATE_COUNT; state++) {
        for (uint8_t event = 0U; event < (uint8_t) DLL_EV_COUNT; event++) {
            const iolink_dll_sm_cell_t* cell =
                iolink_dll_sm_cell((iolink_dll_state_t) state, (iolink_dll_event_t) event);
            if ((cell == NULL) || (cell->action == NULL)) {
                return 1 + state;
            }
            if ((cell->enter_state != DLL_SM_STATE_KEEP) &&
                (cell->enter_state >= (uint8_t) IOLINK_DLL_STATE_COUNT)) {
                return 10 + state;
            }
        }
    }
    if (iolink_dll_sm_cell(IOLINK_DLL_STATE_COUNT, DLL_EV_TRANSITION) != NULL) {
        return 20;
    }
    if (iolink_dll_sm_cell(IOLINK_DLL_STATE_STARTUP, DLL_EV_COUNT) != NULL) {
        return 21;
    }
    return 0;
}

int main(void)
{
    int rc = test_table_accessors();
    if (rc != 0) {
        return rc;
    }
    rc = test_phase_property();
    if (rc != 0) {
        return rc;
    }
    return test_plan_state_stamp_and_republish();
}
