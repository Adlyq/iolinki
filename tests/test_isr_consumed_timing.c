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
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "crc_oracle.h"
#include "dll_access.h"
#include "iolinki/protocol.h"
#include "test_case.h"

#define RX_MAX 8U
#define TX_MAX 32U

static iolink_dll_ctx_t s_dll;
static uint64_t s_now_us;

/* 交接发布顺序的观察点：步进时钟每被调用一次返回值 +1，并记录该次调用
 * 看到的 isr_consumed_seq。成为 isr_consumed_us 的那次读取必须看到旧
 * 序号，否则说明序号自增（发布边沿）排到了时间戳之前。 */
#define CLOCK_OBS_MAX 8U
static uint64_t s_clock_step;
static struct
{
    uint32_t value;
    uint8_t seq;
} s_clock_obs[CLOCK_OBS_MAX];
static size_t s_clock_obs_n;

static uint8_t s_rx_q[RX_MAX];
static size_t s_rx_n;
static size_t s_rx_i;

static uint8_t s_tx_q[TX_MAX];
static size_t s_tx_n;
/* BYTE 传输的发送忙标志：FRAME 传输没有逐字节钩子，复位点用宏置空。 */
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static uint8_t s_tx_active;
#define TEST_TX_IDLE() (s_tx_active = 0U)
#else
#define TEST_TX_IDLE() ((void) 0)
#endif
/* 交付给用例的应答数与用例已经查看过的应答数：group teardown 要求相等。 */
static uint64_t s_tx_delivered;
static uint64_t s_tx_asserted;
static uint64_t s_tx_leaked; /* 开始新捕获时仍未查看的交付 */

static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    const uint64_t now = s_now_us + s_clock_step;
    if (s_clock_step != 0U) {
        s_clock_step++;
        if (s_clock_obs_n < CLOCK_OBS_MAX) {
            s_clock_obs[s_clock_obs_n].value = (uint32_t) now;
            s_clock_obs[s_clock_obs_n].seq = s_dll.main.shared.isr_consumed_seq;
            s_clock_obs_n++;
        }
    }
    return now;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME：feed() 送入的字节即一整个 PHY 帧。 */
static const uint8_t* test_rx_frame(iolink_app_t* IOLINK_NULLABLE self, uint8_t* status,
                                    uint8_t* len)
{
    (void) self;
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
static int test_rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
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
    s_tx_delivered++;
}

static int test_tx_frame(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    (void) self;
    test_tx_frame_sync();
    s_tx_pending = data;
    s_tx_pending_n = len;
    return 0;
}
#else
static int test_tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
{
    (void) self;
    if (!s_tx_active) {
        s_tx_n = 0U;
        s_tx_active = 1U;
    }
    if (s_tx_n < sizeof(s_tx_q)) {
        s_tx_q[s_tx_n++] = data;
    }
    return 0;
}

static void test_tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (s_tx_active) {
        s_tx_delivered++;
    }
    s_tx_active = 0U;
}
#endif

static uint8_t test_int_active(iolink_app_t* IOLINK_NULLABLE self)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) self;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    (void) ctx;
    return ((s_rx_n != 0U) && (s_rx_i < s_rx_n)) ? 1U : 0U;
#else
    return ((s_rx_i < s_rx_n) || (ctx->main.shared.isr_tx_active != 0U)) ? 1U : 0U;
#endif
}

static void test_request_service(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    /* 宿主机没有中断源：应答由 drain_tx() 排空。 */
}

static const iolink_phy_api_t s_phy = {
    .self = (iolink_app_t * IOLINK_NULLABLE) & s_dll,
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
    .request_service = test_request_service,
};

/** @brief 补全一条 M-sequence：CKT 承载类型位 + A.1.6 CKS。 */
static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

/* 开始新一次捕获。上一帧若还没被查看，说明用例漏了断言：记入泄漏，
 * group teardown 失败。 */
static void s_tx_reset(void)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    test_tx_frame_sync();
#endif
    if (s_tx_asserted != s_tx_delivered) {
        s_tx_leaked += s_tx_delivered - s_tx_asserted;
        s_tx_asserted = s_tx_delivered;
    }
    s_tx_n = 0U;
}

static void feed(const uint8_t* frame, size_t len)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    s_tx_reset();
    TEST_TX_IDLE();
}

static void drain_tx(void)
{
    while ((s_dll.main.shared.isr_tx_pending != 0U) || (s_dll.main.shared.isr_tx_active != 0U)) {
        iolink_dll_process_isr(&s_dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    test_tx_frame_sync();
#endif
}

/* 读取捕获长度即算一次查看：每次读取消费一次尚未查看的交付，
 * group teardown 核对"交付 == 查看"。 */
static size_t tx_observed_len(void)
{
    if (s_tx_asserted < s_tx_delivered) {
        s_tx_asserted++;
    }
    return s_tx_n;
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
    iolink_dll_process_isr(&s_dll.main.shared);
    drain_tx();
    /* 读取捕获长度即算一次查看：teardown 要求每次交付都被查看过。 */
    const size_t tx_len = tx_observed_len();
    return (tx_len == 2U) && (s_dll.main.shared.isr_consumed_seq == (uint8_t) (seq_before + 1U)) &&
           iolink_ring_is_empty(&s_dll.main.shared.isr_frame_ring);
}

/* DPP1 需要绑定的设备身份：DLL 层没有默认身份，页读依赖它。 */
static iolink_device_info_ctx_t s_identity_ctx;

static void bind_test_identity(iolink_dll_main_t* main)
{
    static const iolink_device_info_t identity = {
        .vendor_id = 1234U,
        .device_id = 5678U,
        .min_cycle_time = 10U,
        .revision_id = IOLINK_REVISION_ID_1_1,
    };
    assert_int_equal(iolink_device_info_ctx_init(&s_identity_ctx, &identity), 0);
    main->shared.isdu.device_info = &s_identity_ctx;
}

static void init_operate_type0(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    bind_test_identity(&s_dll.main);
    s_dll.main.shared.clock.time_us = test_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    test_pd_set_valid(&s_dll.main, true);
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
    if (dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_CYCLE) != (t0 + 1000U)) {
        return 5;
    }
    if (dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_REN) != (t0 + (uint64_t) IOLINK_T_REN_US)) {
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
    if (dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_CYCLE) != (t1 + 1000U)) {
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
    if (dll_timer_deadline(&s_dll.main, IOLINK_TIMER_T_CYCLE) != (t2 + 1000U)) {
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
    if (dll_timer_armed(&s_dll.main, IOLINK_TIMER_T_CYCLE)) {
        return 19;
    }
    if (s_dll.main.last_response_us != (t2 + 100U)) {
        return 20;
    }
    return 0;
}

IOLINK_TEST_RC(run_all)

/* 在观察记录里找到返回值等于 @p value 的那次时钟读取。 */
static size_t clock_obs_find(uint32_t value)
{
    for (size_t i = 0U; i < s_clock_obs_n; i++) {
        if (s_clock_obs[i].value == value) {
            return i;
        }
    }
    return CLOCK_OBS_MAX;
}

/**
 * @brief 固定 CONSUME 帧时间戳的发布顺序与双读采纳/拒绝路径。
 *
 * 发布侧：成为 `isr_consumed_us` 的那次时钟读取必须看到旧序号，序号在
 * 时间戳之后才自增，因此序号是 release 边沿。读取侧：新序号被采纳一次，
 * 同一序号不会重复采纳，一轮主泵只采纳最新交接，8 位自由计数回绕不
 * 影响采纳。
 */
static int run_handoff_order(void)
{
    init_operate_type0();
    s_dll.main.enforce_timing = false;

    /* 1. 发布顺序：时间戳先于序号自增。 */
    s_now_us += 1000U;
    s_clock_obs_n = 0U;
    s_clock_step = 1U;
    const uint8_t seq_before = s_dll.main.shared.isr_consumed_seq;
    if (!run_consumed_page_read()) {
        return 1;
    }
    s_clock_step = 0U;
    if (s_dll.main.shared.isr_consumed_seq != (uint8_t) (seq_before + 1U)) {
        return 2;
    }
    const uint32_t stamp = s_dll.main.shared.isr_consumed_us;
    const size_t obs = clock_obs_find(stamp);
    if (obs == CLOCK_OBS_MAX) {
        return 3; /* 时间戳必须来自一次时钟读取 */
    }
    if (s_clock_obs[obs].seq != seq_before) {
        return 4; /* 序号先自增：旧时间戳会配到新序号上 */
    }

    /* 2. 采纳路径：主泵用交接时间戳补记 last_response_us。 */
    s_now_us = (uint64_t) stamp + 150U;
    run_main_pass();
    if (s_dll.main.isr_consumed_seq_seen != (uint8_t) (seq_before + 1U)) {
        return 5;
    }
    if (s_dll.main.last_response_us != (uint64_t) stamp) {
        return 6;
    }

    /* 3. 拒绝路径：没有新序号时不重复采纳同一交接。 */
    s_now_us += 200U;
    run_main_pass();
    if (s_dll.main.last_response_us != (uint64_t) stamp) {
        return 7;
    }

    /* 4. 两次发布之间只跑一轮主泵：只采纳最新交接。 */
    s_now_us = (uint64_t) stamp + 1000U;
    if (!run_consumed_page_read()) {
        return 8;
    }
    const uint32_t stamp2 = s_dll.main.shared.isr_consumed_us;
    s_now_us = (uint64_t) stamp2 + 200U;
    if (!run_consumed_page_read()) {
        return 9;
    }
    const uint32_t stamp3 = s_dll.main.shared.isr_consumed_us;
    if (stamp3 == stamp2) {
        return 10;
    }
    s_now_us = (uint64_t) stamp3 + 150U;
    run_main_pass();
    if (s_dll.main.isr_consumed_seq_seen != s_dll.main.shared.isr_consumed_seq) {
        return 11;
    }
    if (s_dll.main.last_response_us != (uint64_t) stamp3) {
        return 12;
    }

    /* 5. 8 位自由计数回绕：回绕后的序号仍被采纳。 */
    s_dll.main.shared.isr_consumed_seq = 0xFEU;
    s_dll.main.isr_consumed_seq_seen = 0xFEU;
    s_now_us = (uint64_t) stamp3 + 1000U;
    if (!run_consumed_page_read()) {
        return 13;
    }
    if (s_dll.main.shared.isr_consumed_seq != 0xFFU) {
        return 14;
    }
    s_now_us = (uint64_t) stamp3 + 1150U;
    run_main_pass();
    if (s_dll.main.isr_consumed_seq_seen != 0xFFU) {
        return 15;
    }
    s_now_us = (uint64_t) stamp3 + 2000U;
    if (!run_consumed_page_read()) {
        return 16;
    }
    if (s_dll.main.shared.isr_consumed_seq != 0x00U) {
        return 17;
    }
    const uint32_t stamp_wrap = s_dll.main.shared.isr_consumed_us;
    s_now_us = (uint64_t) stamp_wrap + 150U;
    run_main_pass();
    if (s_dll.main.isr_consumed_seq_seen != 0x00U) {
        return 18;
    }
    if (s_dll.main.last_response_us != (uint64_t) stamp_wrap) {
        return 19;
    }
    return 0;
}

IOLINK_TEST_RC(run_handoff_order)

static int group_teardown(void** state)
{
    (void) state;
    if ((s_tx_asserted != s_tx_delivered) || (s_tx_leaked != 0U)) {
        print_message(
            "test_isr_consumed_timing: %llu replies delivered, %llu asserted, %llu discarded\n",
            (unsigned long long) s_tx_delivered, (unsigned long long) s_tx_asserted,
            (unsigned long long) s_tx_leaked);
        exit(EXIT_FAILURE); /* cmocka keeps exit 0 after a failed group teardown */
    }
    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_run_all),
        cmocka_unit_test(iolink_case_run_handoff_order),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
