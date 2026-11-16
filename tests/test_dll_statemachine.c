/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll_statemachine.c
 * @brief 状态机表：相位性质、计划状态戳与迁移后的失效/重发布。
 *
 * 性质："任何状态下都不会对错误 M-sequence 相位的帧发字节"。
 * 做法：对每个 DL 状态 × {Type-0/1/2} × {过程/页通道} 的合法长度帧，
 * 走完 ISR 泵 + 主泵，比较实际发送字节与"该状态允许的类型"。
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
#include "iolinki/internal/dll_sm.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "test_case.h"

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
/* 交付给用例的应答数与用例已经查看过的应答数：group teardown 要求相等。 */
static uint64_t s_tx_delivered;
static uint64_t s_tx_asserted;
static uint64_t s_tx_leaked; /* 开始新捕获时仍未查看的交付 */
static uint8_t s_tx_active;

static uint64_t sm_time_us(iolink_app_t* self)
{
    (void) self;
    return s_now_us;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME：feed() 送入的字节即一整个 PHY 帧。 */
static const uint8_t* sm_rx_frame(iolink_app_t* self, uint8_t* status, uint8_t* len)
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
static int sm_rx_octet(iolink_app_t* self, uint8_t* data, uint8_t* status)
{
    (void) self;
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
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* TX FRAME：帧尾 CKS 由库在 tx_frame() 之后写入，交付推迟到观察点。 */
static const uint8_t* s_tx_pending;
static size_t s_tx_pending_n;

static void sm_tx_frame_sync(void)
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

static int sm_tx_frame(iolink_app_t* self, const uint8_t* data, size_t len)
{
    (void) self;
    sm_tx_frame_sync();
    s_tx_pending = data;
    s_tx_pending_n = len;
    return 0;
}
#else
static int sm_tx_octet(iolink_app_t* self, uint8_t data)
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

static void sm_tx_done(iolink_app_t* self)
{
    (void) self;
    if (s_tx_active != 0U) {
        s_tx_active = 0U;
        s_tx_delivered++;
    }
}
#endif

/* 开始新一次捕获。上一帧若还没被查看，说明用例漏了断言：记入泄漏，
 * group teardown 失败。 */
static void s_tx_reset(void)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    sm_tx_frame_sync();
#endif
    if (s_tx_asserted != s_tx_delivered) {
        s_tx_leaked += s_tx_delivered - s_tx_asserted;
        s_tx_asserted = s_tx_delivered;
    }
    s_tx_n = 0U;
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

static uint8_t sm_int_active(iolink_app_t* self)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) self;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    (void) ctx;
    return ((s_rx_n != 0U) && (s_rx_i < s_rx_n)) ? 1U : 0U;
#else
    return ((s_rx_i < s_rx_n) || (s_chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U))
               ? 1U
               : 0U;
#endif
}

static const iolink_phy_api_t s_phy = {
    .self = (iolink_app_t*) &s_dll,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    .rx_frame = sm_rx_frame,
#else
    .rx_octet = sm_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    .tx_frame = sm_tx_frame,
#else
    .tx_octet = sm_tx_octet,
    .tx_done = sm_tx_done,
#endif
    .int_active = sm_int_active,
};

static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

static void feed(const uint8_t* frame, size_t len)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    s_chk_pending = 1U;
    s_tx_reset();
    s_tx_active = 0U;
}

static void drain_tx(void)
{
    while (s_dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_dll.main);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    sm_tx_frame_sync();
#endif
}

/** @brief 直接构造一个已确认状态；不经过状态机（性质测试需要每个相位）。 */
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

static void init_state(iolink_dll_state_t state)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    bind_test_identity(&s_dll.main);
    s_dll.main.shared.clock.time_us = sm_time_us;
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
            (state == IOLINK_DLL_STATE_PREOPERATE) || (state == IOLINK_DLL_STATE_ESTAB_COM)
#if IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
            || (state == IOLINK_DLL_STATE_OPERATE)
#endif
            ;
        if (page_reply) {
            if (tx_observed_len() == 0U) {
                return 10 + (int) state;
            }
        }
        else if (tx_observed_len() != 0U) {
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
        /* 无论该相位是否合法都消费这次捕获：不合法时必须没有应答。 */
        const size_t type0_len = tx_observed_len();
        if (!type0_process_allowed(state) && (type0_len != 0U)) {
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
            if (tx_observed_len() == 0U) {
                return 40 + (int) state;
            }
        }
        else if (tx_observed_len() != 0U) {
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
        if (tx_observed_len() != 0U) {
            return 60 + (int) state;
        }
    }
    return 0;
}

/* 计划表按状态切片：状态迁移后 ISR 直接使用新状态切片，不需要主泵
 * 重发布；ESTAB_COM 的首帧应答后仍交主泵完成迁移。 */
static int test_plan_state_stamp_and_republish(void)
{
    init_state(IOLINK_DLL_STATE_PREOPERATE);

    /* PREOPERATE 切片非空，且切片内的计划都标记为该状态。 */
    const uint8_t off = s_dll.main.shared.reply_plan_off[IOLINK_PLAN_SLOT_PREOPERATE];
    const uint8_t count = s_dll.main.shared.reply_plan_cnt[IOLINK_PLAN_SLOT_PREOPERATE];
    if (count == 0U) {
        return 1;
    }
    for (uint8_t i = 0U; i < count; i++) {
        if (s_dll.main.shared.reply_plan[off + i].state != (uint8_t) IOLINK_DLL_STATE_PREOPERATE) {
            return 2;
        }
    }
    const IOLINK_ATOMIC_UINT_T seq_before = s_dll.main.shared.reply_plan_seq;

    /* 过渡命令由 ISR 计划消费并把状态推进到 ESTAB_COM（不应答）。 */
    uint8_t frame[2];
    make_frame(IOLINK_MC_TRANSITION_COMMAND, IOLINK_MSEQ_TYPE_0, frame, 2U);
    feed(frame, 2U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_dll.main.shared.state != IOLINK_DLL_STATE_ESTAB_COM) {
        return 3;
    }
    if (tx_observed_len() != 0U) {
        return 4;
    }

    /* ESTAB_COM 有独立切片：Type-0 页读由 ISR 即时应答（页字节 + CKS），
     * 同时帧仍进邮箱交给主泵完成到 OPERATE 的迁移。 */
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, 2U);
    feed(frame, 2U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 2U) {
        return 5; /* 页字节 + CKS */
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 1U)) {
        return 6; /* 已应答但必须交给主泵 */
    }

    /* 主泵消费该帧：ESTAB -> OPERATE。计划表不重发布（键与状态无关）。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_dll.main.shared.state != IOLINK_DLL_STATE_OPERATE) {
        return 7;
    }
    if (s_dll.main.shared.reply_plan_seq != seq_before) {
        return 8; /* 状态迁移不触发重发布 */
    }

    /* OPERATE 切片：配置类型的过程读由 ISR 即时应答。 */
    const uint32_t misses_after = s_dll.main.shared.isr_plan_misses;
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_1, frame,
               2U);
    feed(frame, 2U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() == 0U) {
        return 9;
    }
    if (s_dll.main.shared.isr_plan_misses != misses_after) {
        return 10;
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

IOLINK_TEST_RC(test_table_accessors)
IOLINK_TEST_RC(test_phase_property)
IOLINK_TEST_RC(test_plan_state_stamp_and_republish)

static int group_teardown(void** state)
{
    (void) state;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    sm_tx_frame_sync();
#endif
    if ((s_tx_asserted != s_tx_delivered) || (s_tx_leaked != 0U)) {
        print_message(
            "test_dll_statemachine: %llu replies delivered, %llu asserted, %llu discarded\n",
            (unsigned long long) s_tx_delivered, (unsigned long long) s_tx_asserted,
            (unsigned long long) s_tx_leaked);
        exit(EXIT_FAILURE); /* cmocka keeps exit 0 after a failed group teardown */
    }
    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_test_table_accessors),
        cmocka_unit_test(iolink_case_test_phase_property),
        cmocka_unit_test(iolink_case_test_plan_state_stamp_and_republish),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
