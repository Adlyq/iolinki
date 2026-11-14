/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isr_consumed_timing.c
 * @brief CONSUME 计划帧（ISR 完全应答、不进邮箱）的 T_CYCLE/T_REN 记账。
 *
 * ISR 立即应答的帧不经过邮箱，主泵因此看不到它们。主泵必须通过共享区
 * 的一次性时间戳交接补做 MinCycleTime 检查、T_REN/T_CYCLE 装载以及
 * last_response_us 更新，且参考点是帧结束而不是主泵轮询时刻。
 */

#include <stdint.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/protocol.h"
#include "test_case.h"

#define RX_MAX 8U
#define TX_MAX 32U

static iolink_dll_ctx_t s_dll;
static uint64_t s_now_us;

static uint8_t s_rx_q[RX_MAX];
static size_t s_rx_n;
static size_t s_rx_i;

static uint8_t s_tx_q[TX_MAX];
static size_t s_tx_n;
static uint8_t s_tx_active;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return s_now_us;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME：feed() 送入的字节即一整个 PHY 帧。 */
static const uint8_t* test_rx_frame(void* user, uint8_t* status, uint8_t* len)
{
    (void) user;
    *len = 0U;
    *status = 0U;
    if ((s_rx_n == 0U) || (s_rx_i >= s_rx_n)) {
        return NULL;
    }
    const uint8_t* frame = &s_rx_q[s_rx_i];
    *len = (uint8_t) (s_rx_n - s_rx_i);
    *status = (uint8_t) (IOLINK_PHY_STATUS_DATA_READY | IOLINK_PHY_STATUS_CHK);
    s_rx_i = s_rx_n;
    return frame;
}
#else
static int test_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (s_rx_i < s_rx_n) {
        *data = s_rx_q[s_rx_i++];
        return 1;
    }
    return 0;
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* TX FRAME：帧尾 CKS 由库在 tx_frame() 之后写入，交付推迟到观察点。 */
static const uint8_t* s_tx_pending;
static size_t s_tx_pending_n;

static void test_tx_frame_sync(void)
{
    if (s_tx_pending == NULL) {
        return;
    }
    s_tx_n = (s_tx_pending_n <= sizeof(s_tx_q)) ? s_tx_pending_n : sizeof(s_tx_q);
    (void) memcpy(s_tx_q, s_tx_pending, s_tx_n);
    s_tx_pending = NULL;
    s_tx_pending_n = 0U;
}

static int test_tx_frame(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    test_tx_frame_sync();
    s_tx_pending = data;
    s_tx_pending_n = len;
    return 0;
}
#else
static int test_tx_octet(void* user, uint8_t data)
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

static void test_tx_done(void* user)
{
    (void) user;
    s_tx_active = 0U;
}
#endif

static uint8_t test_int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    (void) ctx;
    return ((s_rx_n != 0U) && (s_rx_i < s_rx_n)) ? 1U : 0U;
#else
    return ((s_rx_i < s_rx_n) || (ctx->main.shared.isr_tx_active != 0U)) ? 1U : 0U;
#endif
}

static const iolink_phy_api_t s_phy = {
    .user = &s_dll,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    .rx_frame = test_rx_frame,
#else
    .rx_octet = test_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    .tx_frame = test_tx_frame,
#else
    .tx_octet = test_tx_octet,
    .tx_done = test_tx_done,
#endif
    .int_active = test_int_active,
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

static void feed(const uint8_t* frame, size_t len)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    s_tx_n = 0U;
    s_tx_active = 0U;
}

static void drain_tx(void)
{
    while (s_dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_dll.main);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    test_tx_frame_sync();
#endif
}

/**
 * @brief OPERATE / Type-0 读帧：命中 CONSUME 计划，由 ISR 完全消费。
 *
 * @return true 表示 ISR 已应答且帧未进入邮箱。
 */
static bool run_consumed_page_read(void)
{
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    feed(frame, sizeof(frame));
    const uint8_t seq_before = s_dll.main.shared.isr_consumed_seq;
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    return (s_tx_n == 2U) && (s_dll.main.shared.isr_consumed_seq == (uint8_t) (seq_before + 1U)) &&
           iolink_ring_is_empty(&s_dll.main.shared.isr_frame_ring);
}

static void init_operate_type0(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = test_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_dll.main.enforce_timing = true;
    s_dll.main.min_cycle_time_us = 1000U;
    /* 低 32 位与高 32 位都非零：交接必须经无符号差值还原完整的 64 位时间。 */
    s_now_us = ((uint64_t) 0x1ULL << 32) + 5000000ULL;
    s_dll.main.shared.last_activity_ms = (uint32_t) (s_now_us / 1000U);
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief 主泵一轮：消费交接时间戳并完成 T_CYCLE/T_REN 记账。 */
static void run_main_pass(void)
{
    iolink_dll_process(&s_dll.main);
}

static int run_all(void)
{
    init_operate_type0();
    const uint64_t t0 = s_now_us;

    /* 第一帧：没有 T_CYCLE 参考点，只装载，不计违规。 */
    if (!run_consumed_page_read()) {
        return 1;
    }
    if (s_dll.main.shared.isr_consumed_us != (uint32_t) t0) {
        return 2;
    }
    run_main_pass();
    if (s_dll.main.t_cycle_violations != 0U) {
        return 3;
    }
    if (s_dll.main.last_response_us != t0) {
        return 4; /* 32 位截断会破坏高 32 位 */
    }
    if (iolink_dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_CYCLE) != (t0 + 1000U)) {
        return 5;
    }
    if (iolink_dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_REN) !=
        (t0 + (uint64_t) s_dll.main.t_ren_limit_us)) {
        return 6;
    }
    if (s_dll.main.isr_consumed_seq_seen != s_dll.main.shared.isr_consumed_seq) {
        return 7;
    }

    /* 第二帧：间隔 400 us < 1 ms；主泵晚 150 us 才运行，
     * 截止时间必须按帧时间（t0 + 1000）判定并推进到 t1 + 1000。 */
    const uint64_t t1 = t0 + 400U;
    s_now_us = t1;
    if (!run_consumed_page_read()) {
        return 8;
    }
    s_now_us = t1 + 150U;
    run_main_pass();
    if (s_dll.main.t_cycle_violations != 1U) {
        return 9;
    }
    if (s_dll.main.timing_errors != 1U) {
        return 10;
    }
    if (s_dll.main.last_response_us != t1) {
        return 11;
    }
    if (iolink_dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_CYCLE) != (t1 + 1000U)) {
        return 12;
    }

    /* 第三帧：间隔恰好等于 MinCycleTime（t1 -> t2），不得再计违规。 */
    const uint64_t t2 = t1 + 1000U;
    s_now_us = t2;
    if (!run_consumed_page_read()) {
        return 13;
    }
    s_now_us = t2 + 50U;
    run_main_pass();
    if (s_dll.main.t_cycle_violations != 1U) {
        return 14;
    }
    if (s_dll.main.last_response_us != t2) {
        return 15;
    }
    if (iolink_dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_CYCLE) != (t2 + 1000U)) {
        return 16;
    }

    /* 未启用强制检查时只测量：不装载 T_CYCLE、不计违规。 */
    s_dll.main.enforce_timing = false;
    s_dll.main.t_cycle_violations = 0U;
    s_now_us = t2 + 100U;
    if (!run_consumed_page_read()) {
        return 17;
    }
    s_now_us = t2 + 120U;
    run_main_pass();
    if (s_dll.main.t_cycle_violations != 0U) {
        return 18;
    }
    if (iolink_dll_timer_armed(&s_dll.main, IOLINK_TIMER_T_CYCLE)) {
        return 19;
    }
    if (s_dll.main.last_response_us != (t2 + 100U)) {
        return 20;
    }
    return 0;
}

IOLINK_TEST_RC(run_all)

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_run_all),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
