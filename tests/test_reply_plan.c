/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_reply_plan.c
 * @brief 应答计划：初始化发布、ISR 执行与丢弃语义。
 *
 * 这些用例把"初始化时发布计划"和"ISR 执行计划"分开驱动，覆盖：
 *  - 命中计划的即时应答（t_A 路径）；
 *  - 合法帧计划未命中：不应答、计数并丢弃，邮箱不接收该帧；
 *  - 非法帧仍进入邮箱，由主泵完成错误/状态处理且不计计划未命中；
 *  - Type-0 IDLE/ABORT 读的"无服务"应答与传输层复位握手；
 *  - ISDU 写重发风暴按 FlowCTRL 位置去重，不消耗新位置；
 *  - 未接管的 COMPLETE 请求被新 START 覆盖时计数，且不得被主泵当作有效请求执行；
 *  - t_PD 保护窗口内丢弃的邮箱帧可观测；
 *  - ISDU 写暂存后的读视图失效（防"写后立刻读"读到旧值）；
 *  - 计划表未完整发布（初始化窗口）：数据 ISR 完全不参与，不计数不投邮箱。
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "crc_oracle.h"
#include "test_null.h"
#include "dll_access.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "test_case.h"

#define RX_MAX 16U
#define TX_MAX 32U

static iolink_dll_ctx_t s_dll;
static uint64_t s_now_us;

static uint8_t s_rx_q[RX_MAX];
static size_t s_rx_n;
static size_t s_rx_i;

static uint8_t s_tx_q[TX_MAX];
static size_t s_tx_n;
/* 逐字节状态分别属于 RX 与 TX 传输：FRAME 传输没有这些钩子，写点用下面的
 * 宏置空，否则 clang 会把它们当成"只写不读"的全局量。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static uint8_t s_chk_pending;
#define TEST_CHK_PENDING(chk_) (s_chk_pending = (chk_))
#else
#define TEST_CHK_PENDING(chk_) ((void) (chk_))
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static uint8_t s_tx_active;
#define TEST_TX_IDLE() (s_tx_active = 0U)
#else
#define TEST_TX_IDLE() ((void) 0)
#endif
/* 交付给用例的应答数与用例已经查看过的应答数：group teardown 要求相等。 */
static uint64_t s_tx_delivered;
static uint64_t s_tx_asserted;
static uint64_t s_tx_leaked; /* 开始新捕获时仍未查看的交付（一律算失败） */

static uint64_t plan_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return s_now_us;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME：feed() 送入的字节即一整个 PHY 帧；没有被送入的帧就交付 NULL。 */
static const uint8_t* plan_rx_frame(iolink_app_t* IOLINK_NULLABLE self, uint8_t* status,
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
static int plan_rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
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
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* TX FRAME：库在 tx_frame() 返回之后才把帧尾 CKS 写进同一缓冲区
 * （长应答的 DMA 时序），因此交付推迟到观察点 drain_tx()。 */
static const uint8_t* s_tx_pending;
static size_t s_tx_pending_n;

static void plan_tx_frame_sync(void)
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

static int plan_tx_frame(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    (void) self;
    plan_tx_frame_sync();
    s_tx_pending = data;
    s_tx_pending_n = len;
    return 0;
}
#else
static int plan_tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
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

static void plan_tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (s_tx_active != 0U) {
        TEST_TX_IDLE();
        s_tx_delivered++;
    }
}
#endif

/* 开始新一次捕获。上一帧若还没被查看，说明用例漏了断言：记入泄漏，
 * group teardown 失败。 */
static void tx_reset(void)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    plan_tx_frame_sync();
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

static void plan_request_service(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    /* 宿主机没有中断源：应答由 drain_tx() 排空。 */
}

static uint8_t plan_int_active(iolink_app_t* IOLINK_NULLABLE self)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) self;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    (void) ctx;
    /* 只有完整帧才是 PHY 可交付的事件。 */
    return ((s_rx_n != 0U) && (s_rx_i < s_rx_n)) ? 1U : 0U;
#else
    return ((s_rx_i < s_rx_n) || (s_chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U))
               ? 1U
               : 0U;
#endif
}

static const iolink_phy_api_t s_phy = {
    .self = (iolink_app_t * IOLINK_NULLABLE) & s_dll,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    .rx_frame = plan_rx_frame,
#else
    .rx_octet = plan_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    .tx_frame = plan_tx_frame,
#else
    .tx_octet = plan_tx_octet,
    .tx_done = plan_tx_done,
#endif
    .int_active = plan_int_active,
    .request_service = plan_request_service,
};

/** @brief 只收不发的 PHY（用于"计划命中但无法执行"的兜底路径）。 */
static const iolink_phy_api_t s_phy_rx_only = {
    .self = (iolink_app_t * IOLINK_NULLABLE) & s_dll,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    .rx_frame = plan_rx_frame,
#else
    .rx_octet = plan_rx_octet,
#endif
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
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

static void feed(const uint8_t* frame, size_t len, uint8_t chk)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    TEST_CHK_PENDING(chk);
}

static void drain_tx(void)
{
    while ((s_dll.main.shared.isr_tx_pending != 0U) || (s_dll.main.shared.isr_tx_active != 0U)) {
        iolink_dll_process_isr(&s_dll.main);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    plan_tx_frame_sync();
#endif
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

/** @brief OPERATE / TYPE_2_V：PD-in 1、PD-out 0、OD 1。 */
static void init_operate(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    bind_test_identity(&s_dll.main);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    s_dll.main.shared.pd_in[0][1] = 0x5AU; /* PD-in 槽位（OD 头之后） */
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief PREOPERATE / TYPE_0：无 PD、OD 1。 */
static void init_preoperate(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    bind_test_identity(&s_dll.main);

    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief PREOPERATE / TYPE_0，但 PHY 只有接收能力。 */
static void init_preoperate_rx_only(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy_rx_only);
    bind_test_identity(&s_dll.main);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();
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

/* OPERATE 的 M-sequence 页读：有身份时返回绑定的 DPP1 字节。
 * 无身份时该 OD 槽位按 A.1.2 报 0（见 dll_dispatch_od 的注释）；那条分支
 * 依赖主泵派发，本夹具的 ISR 计划会先接管这条帧，故只钉正向路径。 */
static int test_operate_page_read_uses_identity(void)
{
    init_operate();
    /* 主泵先跑一轮：DPP1 映像只由主泵构建，ISR 只读。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();

    uint8_t rd[2];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x04U), IOLINK_MSEQ_TYPE_2,
               rd, sizeof(rd));
    tx_reset();
    feed(rd, sizeof(rd), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 3U) {
        return 1; /* OD 1 + PD-in 1 + CKS */
    }
    if (s_tx_q[0] != IOLINK_REVISION_ID_1_1) {
        return 2;
    }
    return 0;
}

/* 命中计划：ISR 直接应答，帧仍带 eager=1 发布给主泵。 */
static int test_plan_hit_executes_reply(void)
{
    init_operate();
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (tx_observed_len() != 3U) {
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

/* 合法帧计划未命中：ISR 不应答、计入一次未命中并丢弃（邮箱保持为空）。
 * 主泵不再接手合法帧，因此后续处理不会补发应答。 */
static int test_valid_miss_is_dropped(void)
{
    init_operate();
    /* 撤回全部计划：每个状态切片长度清零（计划表本身仍在）。 */
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_PLAN_SLOT_COUNT; i++) {
        s_dll.main.shared.reply_plan_cnt[i] = 0U;
    }

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 1; /* ISR 绝不应答计划未命中帧 */
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 2;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 3; /* 合法帧不进邮箱 */
    }

    /* 主泵一轮：不得补发应答，也不得凭空产生邮箱帧。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 4;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 5;
    }

    /* 另一种确实无计划的形态：FALLBACK 状态不发布任何计划切片
     * （T_FBD 由主泵计时），合法 Type-0 帧只能按未命中丢弃。 */
    init_preoperate();
    s_dll.main.shared.state = IOLINK_DLL_STATE_FALLBACK;
    uint8_t write[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               write, sizeof(write));
    tx_reset();
    feed(write, sizeof(write), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 6;
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 7;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 8;
    }
    return 0;
}

/* 现场回归：PREOPERATE 的 DPP 页写（地址 1 = MasterCycleTime）必须由
 * ISR 以 CKS 确认，帧仍交给主泵应用写入，且主泵不得重发第二次应答。
 * 诊断写走同一机制。 */
static int test_type0_page_write_is_answered(void)
{
    init_preoperate();

    /* 与现场一致：MC=0x21（页写地址 1），CKT=0x00（Type-0，CKS=0），OD=0x4F。 */
    const uint8_t wr[3] = {0x21U, 0x00U, 0x4FU};
    tx_reset();
    feed(wr, sizeof(wr), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 1U) {
        return 1; /* 仅 CKS */
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 1U)) {
        return 3; /* 帧仍进邮箱，由主泵应用写入 */
    }

    /* 主泵应用写入，不重发应答。 */
    tx_reset();
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_dll.main.master_cycle_time != 0x4FU) {
        return 4;
    }
    if (tx_observed_len() != 0U) {
        return 5; /* frame_replied_by_isr 必须抑制主泵的第二次应答 */
    }

    /* 页写使 DPP1 映像失效：主泵再跑一轮重建缓存。 */
    iolink_dll_process(&s_dll.main);

    /* 回读：ISR 页读拿到主泵刚写入的值。 */
    uint8_t rd[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               rd, sizeof(rd));
    tx_reset();
    feed(rd, sizeof(rd), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 2U) || (s_tx_q[0] != 0x4FU)) {
        return 6;
    }

    /* 诊断写（事件确认）同样由 ISR 确认，帧仍进邮箱由主泵应用。 */
    uint8_t dw[3];
    make_frame(IOLINK_MC_CHANNEL_DIAGNOSIS, IOLINK_MSEQ_TYPE_0, dw, sizeof(dw));
    dw[2] = 0x00U;
    dw[1] = IOLINK_MSEQ_TYPE_0; /* 重算 CKS 前清掉 make_frame 写入的校验位 */
    dw[1] = (uint8_t) (dw[1] | test_expected_checksum6(dw, sizeof(dw)));
    tx_reset();
    feed(dw, sizeof(dw), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 1U) {
        return 7;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 8;
    }
    return 0;
}

/* 没有绑定设备身份时，DPP1 页读必须保持沉默：既不能发全零页（ISR 快路），
 * 也不能由主泵的启动探测补一个臆造页。 */
static int test_page_read_without_identity_is_dropped(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();
    iolink_dll_publish_reply_plans(&s_dll.main);

    uint8_t rd[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               rd, sizeof(rd));

    /* ISR 快路：计划源没有身份可读，本帧不应答。 */
    feed(rd, sizeof(rd), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 1;
    }

    /* 主泵窗口：PREOPERATE/ESTAB_COM 的 Type-0 页读同样不能补发。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 2;
    }
    return 0;
}

/* 主站重发风暴：同一 FlowCTRL 的重复段不消耗新位置、不丢新段，
 * 且请求保持完整。 */
static int test_isdu_write_retry_storm_is_deduped(void)
{
    init_operate();
    /* 写索引 0x10（只读）0 字节：I-Service 0x14 / index / len / CHKPDU。 */
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};

    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t fc = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) i;
        uint8_t frame[3];
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | fc), IOLINK_MSEQ_TYPE_2, frame,
                   sizeof(frame));
        frame[2] = octets[i];
        frame[1] = IOLINK_MSEQ_TYPE_2;
        frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, sizeof(frame)));

        tx_reset();
        feed(frame, sizeof(frame), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        if (tx_observed_len() == 0U) {
            return 1; /* 每个有效写段都必须由 ISR 立即 ACK */
        }
        if (i == 1U) {
            /* 在 COUNT1 之后连发 5 次重发（伪造载荷）：全部忽略。 */
            for (uint8_t r = 0U; r < 5U; r++) {
                uint8_t retry[3];
                make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | 1U), IOLINK_MSEQ_TYPE_2, retry,
                           sizeof(retry));
                retry[2] = 0xAAU;
                retry[1] = IOLINK_MSEQ_TYPE_2;
                retry[1] = (uint8_t) (retry[1] | test_expected_checksum6(retry, sizeof(retry)));
                tx_reset();
                feed(retry, sizeof(retry), 0U);
                iolink_dll_process_isr(&s_dll.main);
                drain_tx();
                if (tx_observed_len() == 0U) {
                    return 2; /* 重发同样要被 ACK */
                }
            }
        }
    }

    if ((s_dll.main.shared.isdu.req_idx != 4U) || (s_dll.main.shared.isdu.req_buf[1] != 0x10U) ||
        (s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE)) {
        return 3; /* 重发没有推进位置，也没有覆盖已接受的 COUNT1 */
    }

    /* 主泵接管并执行：请求被 ack，应答就绪。 */
    iolink_dll_process(&s_dll.main);
    if ((s_dll.main.shared.isdu.req_ack_gen !=
         IOLINK_ISDU_REQ_PUB_GEN(s_dll.main.shared.isdu.req_pub)) ||
        (s_dll.main.shared.isdu.state != ISDU_STATE_RESPONSE_READY)) {
        return 4;
    }
    return 0;
}

/* 上一条请求已收齐但主泵尚未接管时又收到 START：前一条被覆盖并计数。 */
static int test_new_start_overwrites_undrained_request(void)
{
    init_operate();
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};

    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t fc = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) i;
        uint8_t frame[3];
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | fc), IOLINK_MSEQ_TYPE_2, frame,
                   sizeof(frame));
        frame[2] = octets[i];
        frame[1] = IOLINK_MSEQ_TYPE_2;
        frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, sizeof(frame)));
        tx_reset();
        feed(frame, sizeof(frame), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        /* 每个写段都必须被 ISR 立即确认（捕获长度非零）。 */
        if (tx_observed_len() == 0U) {
            return 8;
        }
    }
    if (s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE) {
        return 1;
    }
    if (s_dll.main.shared.isdu.req_overruns != 0U) {
        return 2;
    }

    /* 主泵未接管前又发 START：覆盖前一条并计数。 */
    uint8_t next[3];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START), IOLINK_MSEQ_TYPE_2, next,
               sizeof(next));
    next[2] = 0x93U;
    next[1] = IOLINK_MSEQ_TYPE_2;
    next[1] = (uint8_t) (next[1] | test_expected_checksum6(next, sizeof(next)));
    tx_reset();
    feed(next, sizeof(next), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() == 0U) {
        return 3;
    }
    if (s_dll.main.shared.isdu.req_overruns != 1U) {
        return 4;
    }
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) ||
        (s_dll.main.shared.isdu.req_buf[0] != 0x93U)) {
        return 5;
    }

    /* 相位契约：主泵只能在 COMPLETE 时接管。新请求处于 RECEPTION，
     * iolink_isdu_process() 不得把被覆盖的旧请求当作有效请求执行。 */
    iolink_dll_process(&s_dll.main);
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) ||
        (s_dll.main.shared.isdu.state != ISDU_STATE_IDLE) ||
        (s_dll.main.shared.isdu.req_buf[0] != 0x93U)) {
        return 6;
    }

    /* 公共统计映射：内部计数必须能通过 iolink_dll_get_stats() 读到。 */
    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&s_dll.main, &stats);
    if (stats.isr_od_overruns != 1U) {
        return 7;
    }
    return 0;
}

/* 非法帧（校验和损坏）：仍以 eager=0 发布到邮箱供主泵处理，
 * 不计计划未命中，也不产生应答。 */
static int test_invalid_frame_is_published(void)
{
    init_operate();

    feed_process_read();
    s_rx_q[1] ^= 0x01U; /* 破坏 CKT 中的 A.1.6 校验和 */
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (tx_observed_len() != 0U) {
        return 1; /* 非法帧绝不应答 */
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2; /* 非法帧不计计划未命中 */
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 0U)) {
        return 3; /* 主泵必须能看到它 */
    }
    return 0;
}

/* 计划命中但 PHY 无法发送：不是覆盖缺口，帧以 eager=0 交主泵兜底，
 * 且不计入 isr_plan_misses。 */
static int test_plan_hit_without_tx_is_deferred(void)
{
    /* 公共入口对 NULL 上下文是空操作。 */
    iolink_dll_publish_reply_plans(TEST_NULL(iolink_dll_main_t*));

    init_preoperate_rx_only();
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    tx_reset();
    feed(frame, sizeof(frame), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 1; /* 没有 TX 能力：不得应答 */
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2; /* 计划命中，只是无法执行 */
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 0U)) {
        return 3; /* 帧必须交给主泵，且主泵可应答 */
    }
    return 0;
}

/* t_PD 保护窗口内丢弃的邮箱帧必须计数（此前完全静默）。 */
static int test_t_pd_discards_are_counted(void)
{
    init_operate();
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_PLAN_SLOT_COUNT; i++) {
        s_dll.main.shared.reply_plan_cnt[i] = 0U;
    }

    /* 非法帧仍进邮箱（合法帧计划未命中会被丢弃，无法触发该路径）。 */
    feed_process_read();
    s_rx_q[1] ^= 0x01U;
    iolink_dll_process_isr(&s_dll.main);
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

/* 计划表未完整发布时数据 ISR 完全不参与：不应答、不计数、不投邮箱。 */
static int test_unpublished_plans_block_the_isr(void)
{
    init_operate();
    iolink_atomic_store(&s_dll.main.shared.reply_plans_ready, 0U);

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 0U) {
        return 1;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2; /* 未初始化不是"计划未命中"，不计任何统计 */
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 3; /* 传输泵未运行：帧连邮箱都不进 */
    }

    iolink_atomic_store(&s_dll.main.shared.reply_plans_ready, 1U);
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 3U) {
        return 4;
    }
    return 0;
}

/* 三字节 Type-2 ISDU 写段：MC + CKT/CKS + 一个 OD 八位组。 */
static void make_isdu_write_segment(uint8_t* frame, uint8_t flowctrl, uint8_t octet)
{
    frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | flowctrl);
    frame[1] = IOLINK_MSEQ_TYPE_2;
    frame[2] = octet;
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 3U));
}

/* ISDU 写暂存后，读视图必须失效：写被应用前只能读到"无服务"。
 * 序号协议：ISR 每次接受写 START/ABORT 就自增 isr_isdu_write_gen，
 * 主泵在应用（或中止）并发布新视图之后才把 isdu_write_gen_applied 追平。 */
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
    if (s_dll.main.shared.isr_isdu_write_gen != s_dll.main.shared.isdu_write_gen_applied) {
        return 2; /* 没有待应用的写：两值必须一致 */
    }
    const uint8_t gen_before = IOLINK_ISDU_PUB_GEN(s_dll.main.shared.isr_isdu_pub);
    const uint32_t write_gen_before = s_dll.main.shared.isr_isdu_write_gen;

    /* 先读一次 START：拿到旧应答的首字节（od_width = 1）。 */
    uint8_t frame[3];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    tx_reset();
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    /* OPERATE 长应答：[OD][PD-in]CKS。 */
    if ((tx_observed_len() != 3U) || (s_tx_q[0] != 0xD4U) || (s_tx_q[1] != 0x5AU)) {
        return 3;
    }

    /* 完整的 ISDU 写请求（0x14 0x10 0x00 0x04），先只发 START：
     * 写入口停在 RECEPTION，主泵因此不能应用它。 */
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};
    uint8_t wr[3];
    make_isdu_write_segment(wr, (uint8_t) IOLINK_FLOWCTRL_START, octets[0]);
    feed(wr, sizeof(wr), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 2U) {
        return 4; /* 写段必须被 ISR 立即确认 */
    }
    if (s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) {
        return 5;
    }
    if (s_dll.main.shared.isr_isdu_write_gen != (write_gen_before + 1U)) {
        return 6; /* 每个被接受的写 START/ABORT 恰好自增一次 */
    }
    if (s_dll.main.shared.isdu_write_gen_applied != write_gen_before) {
        return 7; /* 主泵尚未应用：不得提前追平 */
    }

    /* 写未应用：主泵多轮 sync 都必须发布"无服务"，绝不重发旧应答。 */
    for (uint8_t i = 0U; i < 3U; i++) {
        iolink_dll_process(&s_dll.main);
        if (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) != 0U) {
            return 8;
        }
        if (s_dll.main.shared.isdu_write_gen_applied != write_gen_before) {
            return 9;
        }
    }

    /* 主泵尚未应用该写：START 只能读到"无服务"（0x00）。 */
    tx_reset();
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 3U) || (s_tx_q[0] != 0x00U)) {
        return 10; /* 不得再返回 0xD4（旧应答） */
    }

    /* 补齐剩余的写段：写入口进入 COMPLETE；只有 START/ABORT 自增序号，
     * COUNT 段不改变它。 */
    for (uint8_t i = 1U; i < 4U; i++) {
        make_isdu_write_segment(wr, i, octets[i]);
        const uint64_t delivered_before = s_tx_delivered;
        feed(wr, sizeof(wr), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        if (tx_observed_len() != 2U) {
            return 11;
        }
        if (s_tx_delivered != (delivered_before + 1U)) {
            return 11;
        }
    }
    if (s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE) {
        return 12;
    }
    if (s_dll.main.shared.isr_isdu_write_gen != (write_gen_before + 1U)) {
        return 12;
    }

    /* 写已收齐但尚未应用：读仍然是"无服务"。 */
    tx_reset();
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 3U) || (s_tx_q[0] != 0x00U)) {
        return 13;
    }

    /* 主泵应用写并产出新应答：序号追平，读视图恢复。 */
    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isdu_write_gen_applied != s_dll.main.shared.isr_isdu_write_gen) {
        return 14;
    }
    if (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) == 0U) {
        return 15; /* 写已确认：新应答必须已发布 */
    }
    const uint8_t gen_after = IOLINK_ISDU_PUB_GEN(s_dll.main.shared.isr_isdu_pub);
    if ((gen_after == gen_before) && (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) == 4U)) {
        return 16; /* 旧视图被重新发布：代数必须能区分两份应答 */
    }

    tx_reset();
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    const uint8_t expected_first =
        s_dll.main.shared.isdu.resp_store[IOLINK_ISDU_PUB_SEL(s_dll.main.shared.isr_isdu_pub)][0];
    if ((tx_observed_len() != 3U) || (s_tx_q[0] != expected_first) || (s_tx_q[0] == 0x00U)) {
        return 17; /* 闸门必须已解除：读到的是应用该写之后的应答 */
    }

    /* ABORT 也是一个写实例：序号自增、视图立即失效，主泵处理后恢复。 */
    const uint32_t gen_after_apply = s_dll.main.shared.isr_isdu_write_gen;
    make_isdu_write_segment(wr, (uint8_t) IOLINK_FLOWCTRL_ABORT, 0x00U);
    feed(wr, sizeof(wr), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 2U) {
        return 18;
    }
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_IDLE) ||
        (s_dll.main.shared.isr_isdu_write_gen != (gen_after_apply + 1U)) ||
        (s_dll.main.shared.isdu_write_gen_applied != gen_after_apply)) {
        return 19;
    }

    tx_reset();
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 3U) || (s_tx_q[0] != 0x00U)) {
        return 20;
    }
    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isdu_write_gen_applied != s_dll.main.shared.isr_isdu_write_gen) {
        return 21;
    }
    return 0;
}

/* 启动探测计划：STARTUP 中的页读同时完成 T1 迁移。 */
static int test_startup_probe_plan(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    bind_test_identity(&s_dll.main);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 2U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();
    /* 计划表只在初始化时编译；本用例在 init 之后改了类型/宽度，因此显式
     * 重发一次（ISR 尚未运行）。process() 只负责同步 ISDU 快照。 */
    iolink_dll_publish_reply_plans(&s_dll.main);
    iolink_dll_process(&s_dll.main);

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
    if (tx_observed_len() != 2U) {
        return 3; /* 页字节 + CKS */
    }
    return 0;
}

/* Type-0 IDLE 读：ISR 回 od_width 个 0x00 + CKS，帧不进邮箱，并置
 * shared.isr_isdu_read_reset；主泵消费后清 0 并复位读传输层。 */
static int test_type0_idle_read_resets_transport(void)
{
    init_preoperate();

    /* 先发布一个 4 字节应答，使"无服务"与旧应答可区分。 */
    s_dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    s_dll.main.shared.isdu.resp_buf[0] = 0xD4U;
    s_dll.main.shared.isdu.resp_buf[1] = 0x12U;
    s_dll.main.shared.isdu.resp_buf[2] = 0x34U;
    s_dll.main.shared.isdu.resp_buf[3] = 0xF2U;
    s_dll.main.shared.isdu.resp_total = 4U;
    iolink_dll_process(&s_dll.main); /* 发布读视图 */
    if (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) != 4U) {
        return 1;
    }

    /* 先走 START + COUNT1，让 ISR 真正接管 4 字节应答（否则 COUNT 的
     * "无服务"断言在修复前后都成立，无法捕获重放旧段的回归）。 */
    uint8_t start[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_0, start, sizeof(start));
    tx_reset();
    feed(start, sizeof(start), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 2U) || (s_tx_q[0] != 0xD4U)) {
        return 2; /* START 采纳已发布应答的第一段 */
    }

    uint8_t count1[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | 0x01U), IOLINK_MSEQ_TYPE_0,
               count1, sizeof(count1));
    tx_reset();
    feed(count1, sizeof(count1), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 2U) || (s_tx_q[0] != 0x12U)) {
        return 3; /* COUNT1 推进到第二段 */
    }

    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE),
               IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    tx_reset();
    feed(frame, sizeof(frame), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    /* OD（od_width = 1，全 0）+ CKS。 */
    if ((tx_observed_len() != 2U) || (s_tx_q[0] != 0x00U)) {
        return 4;
    }
    if (iolink_ring_count(&s_dll.main.shared.isr_frame_ring) != 0U) {
        return 5; /* CONSUME：帧不进邮箱 */
    }
    if (s_dll.main.shared.isr_isdu_read_reset == 0U) {
        return 6;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 7;
    }

    /* IDLE 之后紧跟 COUNT2：不得重放旧段（0x34），只能回"无服务"。 */
    uint8_t count2[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | 0x02U), IOLINK_MSEQ_TYPE_0,
               count2, sizeof(count2));
    tx_reset();
    feed(count2, sizeof(count2), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 2U) || (s_tx_q[0] != 0x00U)) {
        return 8; /* 旧段索引必须已被 IDLE 清零 */
    }

    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isr_isdu_read_reset != 0U) {
        return 9;
    }
    if (!iolink_isdu_is_idle(&s_dll.main.shared.isdu)) {
        return 10;
    }
    return 0;
}

/** @brief 读复位立即应用，且不扰动数据 ISR 的写入口（无锁接管的契约）。
 *
 * 主站"读 ABORT 后立刻写 START"：ISR 已用新 START 建立写请求，主泵同一
 * 轮里应用读复位。写入口归 ISR，主泵的复位只清读方向字段，因此请求不会
 * 被整段丢弃——这个竞争在所有权层面消失，不需要相位守卫或临界区。
 */
static int test_read_reset_keeps_write_entry(void)
{
    init_preoperate();

    /* 1. Type-0 ISDU 读 IDLE：ISR 回 0x00 并置读传输层复位请求。 */
    uint8_t idle_rd[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE),
               IOLINK_MSEQ_TYPE_0, idle_rd, sizeof(idle_rd));
    tx_reset();
    feed(idle_rd, sizeof(idle_rd), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((tx_observed_len() != 2U) || (s_tx_q[0] != 0x00U)) {
        return 1;
    }
    if (s_dll.main.shared.isr_isdu_read_reset == 0U) {
        return 2;
    }

    /* 2. 紧接一个写 START（Type-0 写：MC + CKT + 一个 OD 字节），
     *    ISR 借此建立写入口（stage_state = RECEPTION，req_total = 4）。 */
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};
    uint8_t start[3] = {(uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
                        IOLINK_MSEQ_TYPE_0, octets[0]};
    start[1] = (uint8_t) (start[1] | test_expected_checksum6(start, sizeof(start)));
    tx_reset();
    feed(start, sizeof(start), 1U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() == 0U) {
        return 3; /* 写段必须被 ISR ACK */
    }
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) ||
        (s_dll.main.shared.isdu.req_total != 4U)) {
        return 4;
    }

    /* 3. 主泵一轮：复位在同一轮生效，写入口字段原样不动。 */
    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isr_isdu_read_reset != 0U) {
        return 5;
    }
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) ||
        (s_dll.main.shared.isdu.req_idx != 1U) || (s_dll.main.shared.isdu.req_total != 4U)) {
        return 6;
    }

    /* 4. 其余三段 COUNT：位置推进正常，请求得以收齐。 */
    for (uint8_t i = 1U; i < 4U; i++) {
        uint8_t count[3] = {(uint8_t) (IOLINK_MC_CHANNEL_ISDU | i), IOLINK_MSEQ_TYPE_0, octets[i]};
        count[1] = (uint8_t) (count[1] | test_expected_checksum6(count, sizeof(count)));
        tx_reset();
        feed(count, sizeof(count), 1U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        if (tx_observed_len() == 0U) {
            return 7;
        }
    }
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE) ||
        (s_dll.main.shared.isdu.req_idx != 4U)) {
        return 8; /* 请求被整段丢弃：读复位动了写入口 */
    }

    /* 5. 主泵接管：写被 ack，否定应答就绪（等待主站读出后才回到空闲）。 */
    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isdu.req_ack_gen !=
        IOLINK_ISDU_REQ_PUB_GEN(s_dll.main.shared.isdu.req_pub)) {
        return 9;
    }
    if (s_dll.main.shared.isdu.state != ISDU_STATE_RESPONSE_READY) {
        return 10;
    }
    return 0;
}

/** @brief OPERATE 切片中 Type-0 页读槽位的动作；未找到返回 0xFF。 */
static uint8_t type0_page_read_action(void)
{
    const uint8_t off = s_dll.main.shared.reply_plan_off[IOLINK_PLAN_SLOT_OPERATE];
    const uint8_t cnt = s_dll.main.shared.reply_plan_cnt[IOLINK_PLAN_SLOT_OPERATE];
    for (uint8_t i = 0U; i < cnt; i++) {
        const iolink_reply_plan_t* plan = &s_dll.main.shared.reply_plan[off + i];
        if ((plan->match_mask == (IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK)) &&
            (plan->match_value == (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE)) &&
            (plan->ckt_mask == IOLINK_MSEQ_TYPE_MASK) && (plan->ckt_value == IOLINK_MSEQ_TYPE_0)) {
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

/* 计划表是能力位的纯函数：能力位在初始化时定义，发布一次即固化。
 * 本用例钉住"改能力位 + 重发布 = 表随能力位变化"这条构建规则。
 * 第一步只在构建档位默认打开 `IOLINK_CAP_COMPAT_T0_READ` 时才改变状态；
 * 真正的证明是第二步——同一张表按新能力位重建后行为随之改变。 */
static int test_publish_reflects_capability_bits(void)
{
    init_operate();
    /* 非兼容模式：OPERATE 中的 Type-0 页读是复位请求（不应答）。 */
    s_dll.main.shared.caps &= (iolink_caps_t) ~IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 1;
    }

    /* 兼容模式：同一张表按新能力位重建，页读改为普通应答。 */
    s_dll.main.shared.caps |= IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_NONE) {
        return 2;
    }
    tx_reset();
    feed_type0_page_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 2U) {
        return 3; /* 页字节 + CKS */
    }
    return 0;
}

/* 直接使用 DLL：iolink_dll_init() 自身必须发布默认表，数据 ISR 无需再被
 * 显式 publish 一次即可命中（设备层会在这之后按完整配置重发布）。 */
static int test_init_publishes_default_plan(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.pd_valid = true;
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();
    if (iolink_atomic_load(&s_dll.main.shared.reply_plans_ready) == 0U) {
        return 1; /* init 必须发布一次 */
    }
    if (s_dll.main.shared.state != IOLINK_DLL_STATE_STARTUP) {
        return 2;
    }

    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_0, frame,
               sizeof(frame));
    feed(frame, sizeof(frame), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 2U) {
        return 3; /* 默认 Type-0 表：OD + CKS */
    }
    return 0;
}

/* iolink_dll_process() 不再重发布：运行期改能力位不会悄悄换掉 ISR 的表。 */
static int test_process_does_not_republish(void)
{
    init_operate();
    s_dll.main.shared.caps &= (iolink_caps_t) ~IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 1;
    }

    s_dll.main.shared.caps |= IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_process(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 2; /* 主泵不重编译：表保持发布时的能力位语义 */
    }
    return 0;
}

/* tx_done 只是可选的帧结束通知：只有 tx_octet 的 PHY 仍应置位 CAP_TX 并能发送。 */
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static int test_tx_without_done_hook(void)
{
    iolink_phy_api_t phy = s_phy;
    phy.tx_done = NULL; /* 变体：去掉可选完成回调 */

    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &phy);
    bind_test_identity(&s_dll.main);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    s_dll.main.shared.pd_in[0][1] = 0x5AU;
    s_now_us = 1000000U;
    tx_reset();
    TEST_TX_IDLE();

    if (!iolink_caps_has(s_dll.main.shared.caps, IOLINK_CAP_TX)) {
        return 1;
    }
    iolink_dll_publish_reply_plans(&s_dll.main);
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (tx_observed_len() != 3U) {
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
#endif /* IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE */

IOLINK_TEST_RC(test_plan_hit_executes_reply)
IOLINK_TEST_RC(test_valid_miss_is_dropped)
IOLINK_TEST_RC(test_isdu_write_retry_storm_is_deduped)
IOLINK_TEST_RC(test_new_start_overwrites_undrained_request)
IOLINK_TEST_RC(test_invalid_frame_is_published)
IOLINK_TEST_RC(test_t_pd_discards_are_counted)
IOLINK_TEST_RC(test_unpublished_plans_block_the_isr)
IOLINK_TEST_RC(test_isdu_write_invalidates_read_view)
IOLINK_TEST_RC(test_startup_probe_plan)
IOLINK_TEST_RC(test_type0_idle_read_resets_transport)
IOLINK_TEST_RC(test_read_reset_keeps_write_entry)
IOLINK_TEST_RC(test_publish_reflects_capability_bits)
IOLINK_TEST_RC(test_init_publishes_default_plan)
IOLINK_TEST_RC(test_process_does_not_republish)
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
IOLINK_TEST_RC(test_tx_without_done_hook)
#endif
IOLINK_TEST_RC(test_type0_page_write_is_answered)
IOLINK_TEST_RC(test_page_read_without_identity_is_dropped)
IOLINK_TEST_RC(test_operate_page_read_uses_identity)
IOLINK_TEST_RC(test_plan_hit_without_tx_is_deferred)

static int group_teardown(void** state)
{
    (void) state;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    plan_tx_frame_sync();
#endif
    if ((s_tx_asserted != s_tx_delivered) || (s_tx_leaked != 0U)) {
        print_message("test_reply_plan: %llu replies delivered, %llu asserted, %llu discarded\n",
                      (unsigned long long) s_tx_delivered, (unsigned long long) s_tx_asserted,
                      (unsigned long long) s_tx_leaked);
        exit(EXIT_FAILURE); /* cmocka keeps exit 0 after a failed group teardown */
    }
    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_test_plan_hit_executes_reply),
        cmocka_unit_test(iolink_case_test_valid_miss_is_dropped),
        cmocka_unit_test(iolink_case_test_isdu_write_retry_storm_is_deduped),
        cmocka_unit_test(iolink_case_test_new_start_overwrites_undrained_request),
        cmocka_unit_test(iolink_case_test_invalid_frame_is_published),
        cmocka_unit_test(iolink_case_test_t_pd_discards_are_counted),
        cmocka_unit_test(iolink_case_test_unpublished_plans_block_the_isr),
        cmocka_unit_test(iolink_case_test_isdu_write_invalidates_read_view),
        cmocka_unit_test(iolink_case_test_startup_probe_plan),
        cmocka_unit_test(iolink_case_test_type0_idle_read_resets_transport),
        cmocka_unit_test(iolink_case_test_read_reset_keeps_write_entry),
        cmocka_unit_test(iolink_case_test_publish_reflects_capability_bits),
        cmocka_unit_test(iolink_case_test_init_publishes_default_plan),
        cmocka_unit_test(iolink_case_test_process_does_not_republish),
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        cmocka_unit_test(iolink_case_test_tx_without_done_hook),
#endif
        cmocka_unit_test(iolink_case_test_type0_page_write_is_answered),
        cmocka_unit_test(iolink_case_test_page_read_without_identity_is_dropped),
        cmocka_unit_test(iolink_case_test_operate_page_read_uses_identity),
        cmocka_unit_test(iolink_case_test_plan_hit_without_tx_is_deferred),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
