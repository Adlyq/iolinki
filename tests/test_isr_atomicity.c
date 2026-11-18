/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 可选 ISR 泵的并发回归：
 *   - 即使应用不提供任何锁，主上下文的应答暂存也与数据 ISR
 *     串行化（平台临界区回退）；
 *   - 被更高优先级向量抢占的泵调用会被推迟，
 *     而不是重入单写者的状态；
 *   - 报告线路繁忙（tx_frame() < 0）的 PHY 不会丢失
 *     帧：主泵保持所有权并自行暂存应答。
 *
 * 该测试用最小 PHY 直接驱动 DLL，因此检验的是
 * 公开的 DLL 约定，而不是板级驱动。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "crc_oracle.h"
#include "dll_access.h"
#include "iolinki/iolink.h"
#include "iolinki/port.h"
#include "iolinki/protocol.h"

static iolink_dll_ctx_t s_ctx;

/* 平台临界区观测（覆盖默认的弱空实现）。 */
static bool s_in_critical;
static uint32_t s_crit_enters;
static uint32_t s_crit_exits;

void iolink_critical_enter(void)
{
    s_crit_enters++;
    s_in_critical = true;
}

void iolink_critical_exit(void)
{
    s_crit_exits++;
    s_in_critical = false;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
static uint8_t s_frame[IOLINK_M_SEQ_MAX_LEN];
static uint8_t s_frame_len;
static bool s_frame_pending;
static uint32_t s_rx_calls;

static const uint8_t* phy_rx_frame(iolink_app_t* IOLINK_NULLABLE self, uint8_t* status,
                                   uint8_t* len)
{
    (void) self;
    s_rx_calls++;
    if (!s_frame_pending) {
        return NULL;
    }
    s_frame_pending = false;
    if (status != NULL) {
        *status = IOLINK_PHY_STATUS_DATA_READY;
    }
    if (len != NULL) {
        *len = s_frame_len;
    }
    return s_frame;
}
#else
static uint8_t s_rxq[IOLINK_M_SEQ_MAX_LEN];
static uint8_t s_rxq_len;
static uint8_t s_rxq_pos;
static bool s_chk_pending;
static uint32_t s_rx_calls;

static int phy_rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
    s_rx_calls++;
    if (s_rxq_pos < s_rxq_len) {
        *data = s_rxq[s_rxq_pos++];
        *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if (s_chk_pending) {
        s_chk_pending = false;
        *status = IOLINK_PHY_STATUS_CHK;
        return 0;
    }
    *status = 0U;
    return 0;
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
/* 在途应答标志：TX BYTE 每步请求一次服务，int_active 据此复用同一条
 * 中断入口。定义在 int_active 之前，记录缓冲在发送钩子旁。 */
static bool s_streaming;
#endif

static uint8_t phy_int_active(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return s_frame_pending ? 1U : 0U;
#else
    /* 在途应答与接收一样会让收发器请求服务（TX BYTE 每步一个八位组）。 */
    return ((s_rxq_pos < s_rxq_len) || s_chk_pending || s_streaming) ? 1U : 0U;
#endif
}

static uint32_t s_service_calls;
static bool s_service_in_critical;
static void (*s_timer_start_hook)(void);

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* 只由 FRAME 传输的"线路繁忙"用例读取：BYTE 传输没有
 * tx_frame 返回值语义，计数器在那里无人读。 */
static int s_tx_calls;
static int s_tx_rc;
static int phy_tx_frame(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    (void) self;
    (void) data;
    (void) len;
    s_tx_calls++;
    return s_tx_rc;
}
#else
/* TX BYTE 观测：库逐字节喂送，帧结束由 tx_done 标记。
 * 记录八位组使长度/CKS 成为可断言的对象。 */
static uint8_t s_octets[IOLINK_M_SEQ_MAX_LEN];
static uint8_t s_octet_n;
static uint32_t s_frames_done;

static int phy_tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
{
    (void) self;
    s_streaming = true;
    if (s_octet_n < (uint8_t) sizeof(s_octets)) {
        s_octets[s_octet_n++] = data;
    }
    return 0;
}

static void phy_tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (s_streaming) {
        s_streaming = false;
        s_frames_done++;
    }
}
#endif

static void phy_timer_start(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (s_timer_start_hook != NULL) {
        s_timer_start_hook();
    }
}

static void phy_timer_stop(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
}

static void phy_request_service(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    s_service_calls++;
    s_service_in_critical = s_in_critical;
}

static int phy_init(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
}

static void phy_set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}

static int phy_detect_wakeup(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
}

static const iolink_phy_api_t s_phy = {
    .self = NULL,
    .init = phy_init,
    .set_mode = phy_set_mode,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    .rx_frame = phy_rx_frame,
#else
    .rx_octet = phy_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    .tx_frame = phy_tx_frame,
#else
    .tx_octet = phy_tx_octet,
    .tx_done = phy_tx_done,
#endif
    .int_active = phy_int_active,
    .response_timer_start = phy_timer_start,
    .response_timer_stop = phy_timer_stop,
    .request_service = phy_request_service,
    .detect_wakeup = phy_detect_wakeup,
};

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

static void reset_ctx(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    memset(s_frame, 0, sizeof(s_frame));
    s_frame_len = 0U;
    s_frame_pending = false;
#else
    memset(s_rxq, 0, sizeof(s_rxq));
    s_rxq_len = 0U;
    s_rxq_pos = 0U;
    s_chk_pending = false;
#endif
    s_rx_calls = 0U;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    s_tx_calls = 0;
    s_tx_rc = 0;
#else
    memset(s_octets, 0, sizeof(s_octets));
    s_octet_n = 0U;
    s_frames_done = 0U;
    s_streaming = false;
#endif
    s_service_calls = 0U;
    s_service_in_critical = false;
    s_timer_start_hook = NULL;
    s_crit_enters = 0U;
    s_crit_exits = 0U;
    s_in_critical = false;

    iolink_dll_init(&s_ctx.main, &s_ctx.isr, &s_phy);
    bind_test_identity(&s_ctx.main);
    /* 直接驱动共享的帧处理器：OPERATE、SDCI、不强制时序。 */
    s_ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    s_ctx.main.enforce_timing = false;
    s_ctx.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_ctx.main.shared.od_width = 1U;
    s_ctx.main.shared.pd_in_len = 0U;
    s_ctx.main.shared.pd_out_len_current = 0U;
    s_ctx.main.pd_valid = true;
    /* 应答计划只在初始化时编译；本用例在 init 之后改了类型/宽度，因此
     * 在这里显式重发一次（ISR 尚未运行）。 */
    iolink_dll_publish_reply_plans(&s_ctx.main);
}

/* 补全一个 M 序列：CKT 八位组承载其类型位 + A.1.6 CKS。 */
static void make_frame(uint8_t* frame, uint8_t len)
{
    frame[1] = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

static void feed_frame(const uint8_t* frame, uint8_t len)
{
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    memcpy(s_frame, frame, len);
    s_frame_len = len;
    s_frame_pending = true;
#else
    memcpy(s_rxq, frame, len);
    s_rxq_len = len;
    s_rxq_pos = 0U;
    s_chk_pending = true;
#endif
}

/* 无锁设备仍须原子地暂存主上下文应答，但
 * PHY 服务钩子在有中断使能的情况下运行：它属于端口代码，可能
 * 很慢，而此时已暂存的应答已经发布。 */
static void test_reply_staging_pokes_phy_outside_critical(void** state)
{
    (void) state;
    /* STARTUP 的 MasterCommand 页写带 DEFER 计划：ISR 不解析命令字节，
     * 帧交主泵判定，由主泵通过 dll_send() 应答。 */
    uint8_t frame[3] = {(uint8_t) IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0,
                        IOLINK_CMD_DEVICE_PREOPERATE};

    reset_ctx();
    s_ctx.main.shared.state = IOLINK_DLL_STATE_STARTUP;
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));

    iolink_dll_process_isr(&s_ctx.main);

    /* dll_send() 中的暂存路径（槽位选择、载荷复制、发布）归主上下文
     * 所有，不得被平台临界区包裹：互斥属于直接发送的回退路径，
     * 而不属于这里的一次发布。通过在应答该 DEFER 帧（STARTUP 的
     * MasterCommand 页写）前后统计进入次数来固定这一点；这样即使
     * request_service() 仍在该段已释放的情况下运行，重新在复制前后
     * 加入 IOLINK_CRITICAL_ENTER/EXIT 的回归也会暴露出来。
     * 这一趟没有其它临界区：mock PHY 没有诊断钩子，DEFER 帧由主泵
     * 应用命令，时序强制关闭（不产生 t_CYCLE 事件），且邮箱消费不取锁。
     * 本套件在 BYTE/BYTE 配置下运行该测试；FRAME 粒度仅做编译验证。 */
#if IOLINK_NEED_CRITICAL
    const uint32_t crit_before = s_crit_enters;
#endif
    iolink_dll_process(&s_ctx.main);

    assert_int_equal(s_service_calls, 1U);
#if IOLINK_NEED_CRITICAL
    assert_int_equal(s_crit_enters, crit_before);
    assert_false(s_service_in_critical);
#endif
    assert_int_equal(s_crit_enters, s_crit_exits);
    assert_true(s_ctx.main.shared.isr_tx_pending != 0U);
}

static uint32_t s_nested_calls;

static void nested_pump_hook(void)
{
    s_nested_calls++;
    /* 模拟更高优先级的向量在帧中途调用泵。 */
    iolink_dll_process_isr(&s_ctx.main);
}

/* 抢占式的泵调用必须让位于外层调用，而不是
 * 重入单写者的 RX/TX 暂存状态。 */
static void test_nested_pump_is_deferred(void** state)
{
    (void) state;
    /* OPERATE 下的 Type-0 page 读：由 ISR 即时应答。 */
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), 0x00U};

    reset_ctx();
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));
    s_nested_calls = 0U;
    s_timer_start_hook = nested_pump_hook;

    iolink_dll_process_isr(&s_ctx.main);

    assert_int_equal(s_nested_calls, 1U);
    assert_int_equal(s_ctx.isr.isr_reentry_pending, 0U);
    assert_int_equal(s_ctx.main.shared.in_isr, 0U);
    /* 只有外层调用消费了该请求。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    assert_int_equal(s_rx_calls, 1U);
#else
    assert_int_equal(s_rx_calls, 2U);
#endif
}

static iolink_dll_state_t s_states[8];
static uint8_t s_state_count;
static bool s_cb_in_isr;

static void state_cb(void* user, iolink_dll_state_t state)
{
    (void) user;
    s_cb_in_isr = (s_ctx.main.shared.in_isr != 0U);
    if (s_state_count < (uint8_t) (sizeof(s_states) / sizeof(s_states[0]))) {
        s_states[s_state_count++] = state;
    }
}

/* 电平式通知：主窗口之前发生的两次 ISR 侧状态迁移
 * 会合并为当前状态，且回调绝不在
 * ISR 中运行。状态未变的第二次 flush 不会投递任何内容。 */
static void test_state_notification_is_level_based(void** state)
{
    (void) state;
    uint8_t probe[2] = {(uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), 0x00U};
    uint8_t trans[2] = {IOLINK_MC_TRANSITION_COMMAND, 0x00U};

    reset_ctx();
    s_ctx.main.state_cb = state_cb;
    s_ctx.main.state_cb_user = NULL;
    s_ctx.main.shared.state = IOLINK_DLL_STATE_STARTUP;
    s_state_count = 0U;
    s_cb_in_isr = false;

    /* 启动探测（T1）：STARTUP -> PREOPERATE，由 ISR 应答。 */
    make_frame(probe, sizeof(probe));
    feed_frame(probe, sizeof(probe));
    iolink_dll_process_isr(&s_ctx.main);
    while (s_ctx.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_ctx.main);
    }
    assert_int_equal(s_ctx.main.shared.state, IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(s_state_count, 0U); /* 推迟到主窗口 */

    /* 迁移命令：PREOPERATE -> ESTAB_COM，发生在主泵运行之前。 */
    make_frame(trans, sizeof(trans));
    feed_frame(trans, sizeof(trans));
    iolink_dll_process_isr(&s_ctx.main);
    while (s_ctx.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_ctx.main);
    }
    assert_int_equal(s_ctx.main.shared.state, IOLINK_DLL_STATE_ESTAB_COM);
    assert_int_equal(s_state_count, 0U);

    iolink_dll_flush_state_callbacks(&s_ctx.main);

    assert_int_equal(s_state_count, 1U);
    assert_int_equal(s_states[0], IOLINK_DLL_STATE_ESTAB_COM);
    assert_false(s_cb_in_isr);

    /* 自上次投递以来没有状态变化：不会重复报告。 */
    iolink_dll_flush_state_callbacks(&s_ctx.main);
    assert_int_equal(s_state_count, 1U);
}

/* 即使下一次启动探测在主泵 flush 之前就把链路推进到 PREOPERATE，
 * ISR 侧的 OPERATE -> STARTUP 复位也必须仍然可被观察到。 */
static void test_isr_startup_notification_is_not_coalesced(void** state)
{
    (void) state;
    reset_ctx();
    s_ctx.main.state_cb = state_cb;
    s_ctx.main.state_cb_user = NULL;
    s_ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_ctx.main.last_notified_state = IOLINK_DLL_STATE_OPERATE;
    s_state_count = 0U;
    s_cb_in_isr = false;

    s_ctx.main.shared.in_isr = 1U;
    /* 模拟 dll_set_state() 在数据 ISR 拥有上下文时执行的那两次写操作。
     * 挂起位即 ISR 到主泵的边沿通知。 */
    s_ctx.main.shared.state = IOLINK_DLL_STATE_STARTUP;
    s_ctx.main.startup_notification_pending = 1U;
    s_ctx.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    s_ctx.main.shared.in_isr = 0U;

    iolink_dll_flush_state_callbacks(&s_ctx.main);

    assert_int_equal(s_state_count, 2U);
    assert_int_equal(s_states[0], IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(s_states[1], IOLINK_DLL_STATE_PREOPERATE);
    assert_false(s_cb_in_isr);
    assert_int_equal(s_ctx.main.startup_notification_pending, 0U);
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* 繁忙的 PHY（前一应答仍在线路上）不得吞掉该帧：
 * 即时应答被放弃，由主泵在下一个窗口暂存它。 */
static void test_busy_tx_frame_keeps_frame_for_main(void** state)
{
    (void) state;
    /* Type-2_1 过程数据写：MC + CKT + PD-out + OD。 */
    uint8_t frame[4] = {IOLINK_MC_CHANNEL_PROCESS, (uint8_t) IOLINK_MSEQ_TYPE_2, 0xAAU, 0x55U};

    reset_ctx();
    s_ctx.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_1;
    s_ctx.main.shared.od_width = 1U;
    s_ctx.main.shared.pd_out_len_current = 1U;
    s_ctx.main.shared.pd_in_len = 1U;
    /* 计划表按配置发布：改了 PD/OD 宽度后必须重发，否则请求长度
     * 与计划里的 req_len 不一致，帧会按"计划未命中"丢弃。 */
    iolink_dll_publish_reply_plans(&s_ctx.main);
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));

    s_tx_rc = -1; /* 线路繁忙 */
    iolink_dll_process_isr(&s_ctx.main);

    assert_int_equal(s_tx_calls, 1);
    /* 应答失败（线路繁忙），因此该记录携带 eager = 0，
     * 由主泵拥有该帧。 */
    assert_int_equal(iolink_ring_count(&s_ctx.main.shared.isr_frame_ring), 1U);
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_ctx.main.shared.isr_frame_ring);
    assert_non_null(rec);
    assert_int_equal(rec->eager, 0U);

    s_tx_rc = 0;
    iolink_dll_process(&s_ctx.main);
    assert_true(s_service_calls > 0U);
    assert_true(s_ctx.main.shared.isr_tx_pending != 0U);

    /* 下一次 ISR pass 会发送已暂存的应答。 */
    iolink_dll_process_isr(&s_ctx.main);
    assert_int_equal(s_tx_calls, 2);
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
/* 排空 ISR 正在流式发送的应答；测试 PHY 在流式期间保持 int_active。 */
static void drain_isr_stream(void)
{
    for (uint8_t i = 0U; (i < 16U) && (s_ctx.main.shared.isr_tx_active != 0U); i++) {
        iolink_dll_process_isr(&s_ctx.main);
    }
    assert_int_equal(s_ctx.main.shared.isr_tx_active, 0U);
}

/* 主泵暂存的 {槽位, 长度, CKS 标志} 只在 ISR 取走时一次性搬进流式状态：
 * 取走之后主泵改写暂存元数据（例如为下一帧暂存）不得改变在途帧的
 * 长度，也不得凭空追加或跳过 CKS。 */
static void test_staged_reply_keeps_own_length_and_cks(void** state)
{
    (void) state;
    /* STARTUP 的 MasterCommand 页写按 DEFER 计划交给主泵应答。 */
    uint8_t frame[3] = {(uint8_t) IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0,
                        IOLINK_CMD_DEVICE_PREOPERATE};

    reset_ctx();
    s_ctx.main.shared.state = IOLINK_DLL_STATE_STARTUP;
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));

    iolink_dll_process_isr(&s_ctx.main);
    assert_int_equal(s_ctx.main.shared.isr_tx_pending, 0U);

    /* 主泵生成应答（载荷自带 CKS）并暂存。 */
    iolink_dll_process(&s_ctx.main);
    assert_int_equal(s_ctx.main.shared.isr_tx_pending, 1U);
    assert_int_equal(s_ctx.main.shared.stage_len, 1U);
    assert_int_equal(s_ctx.main.shared.stage_has_cks, 0U);
    const uint8_t staged =
        s_ctx.main.shared.isr_tx_payload[s_ctx.main.shared.isr_tx_pending_slot][0];

    /* ISR 取走暂存并发出第一个八位组。 */
    iolink_dll_process_isr(&s_ctx.main);
    assert_int_equal(s_ctx.main.shared.isr_tx_pending, 0U);
    assert_int_equal(s_ctx.main.shared.isr_tx_active, 1U);
    assert_int_equal(s_octet_n, 1U);
    assert_int_equal(s_octets[0], staged);

    /* 模拟主泵为下一帧改写暂存元数据：在途帧只读 ISR 自己的副本。 */
    s_ctx.main.shared.stage_len = 3U;
    s_ctx.main.shared.stage_has_cks = 1U;
    s_ctx.main.shared.isr_tx_pending_slot = IOLINK_TX_ISR_SLOT;

    drain_isr_stream();

    assert_int_equal(s_octet_n, 1U);
    assert_int_equal(s_octets[0], staged);
    assert_int_equal(s_frames_done, 1U);
}

/* isr_tx_active 期间 dll_send() 必须拒绝暂存并计数：接受它会改写
 * 流式长度/CKS，使在途帧提前 tx_done（丢掉 CKS、提前释放 PD-in 租约）。 */
static void test_staging_rejected_while_reply_streaming(void** state)
{
    (void) state;
    uint8_t deferred[3] = {(uint8_t) IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0,
                           IOLINK_CMD_DEVICE_PREOPERATE};
    /* OPERATE/STARTUP 的 Type-0 页读：ISR 应答 OD + CKS 两个八位组。 */
    uint8_t probe[2] = {(uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), 0x00U};

    reset_ctx();
    s_ctx.main.shared.state = IOLINK_DLL_STATE_STARTUP;

    /* 只有主泵处理的帧先入邮箱。 */
    make_frame(deferred, sizeof(deferred));
    feed_frame(deferred, sizeof(deferred));
    iolink_dll_process_isr(&s_ctx.main);
    assert_int_equal(iolink_ring_count(&s_ctx.main.shared.isr_frame_ring), 1U);

    /* ISR 自己的计划应答开始流式发送（第一个八位组已发出）。 */
    make_frame(probe, sizeof(probe));
    feed_frame(probe, sizeof(probe));
    iolink_dll_process_isr(&s_ctx.main);
    assert_int_equal(s_ctx.main.shared.isr_tx_active, 1U);
    assert_int_equal(s_octet_n, 1U);

    /* 主泵此刻消费页写并调用 dll_send()：暂存被拒绝并计数。 */
    const uint32_t dropped_before = s_ctx.main.shared.isr_tx_dropped;
    iolink_dll_process(&s_ctx.main);
    assert_int_equal(s_ctx.main.shared.isr_tx_dropped, dropped_before + 1U);
    assert_int_equal(s_ctx.main.shared.isr_tx_pending, 0U);

    /* 在途帧仍按自己的长度和 CKS 完成，没有被截断成新暂存应答。 */
    drain_isr_stream();
    assert_int_equal(s_octet_n, 2U);
    assert_int_equal(s_frames_done, 1U);
    assert_int_equal(s_ctx.main.shared.isr_tx_pending, 0U);
}
#endif /* IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/* Timer A 入口复位 RX 组装状态：两个字段必须作为一个相位清掉，
 * 不能被数据中断在中间重新播种半帧。 */
static void test_rx_timeout_resets_assembly_in_one_phase(void** state)
{
    (void) state;
    reset_ctx();
    /* 半帧：两个头部字节已累积，期望长度已知。 */
    s_ctx.isr.isr_rx_cnt = 2U;
    s_ctx.isr.isr_rx_expect = 3U;
#if IOLINK_NEED_CRITICAL
    const uint32_t crit_before = s_crit_enters;
#endif

    iolink_dll_on_rx_timeout(&s_ctx.main);

    assert_int_equal(s_ctx.isr.isr_rx_cnt, 0U);
    assert_int_equal(s_ctx.isr.isr_rx_expect, 0U);
#if IOLINK_NEED_CRITICAL
    assert_int_equal(s_crit_enters, crit_before + 1U);
#endif
    assert_int_equal(s_crit_enters, s_crit_exits);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reply_staging_pokes_phy_outside_critical),
        cmocka_unit_test(test_nested_pump_is_deferred),
        cmocka_unit_test(test_state_notification_is_level_based),
        cmocka_unit_test(test_isr_startup_notification_is_not_coalesced),
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        cmocka_unit_test(test_busy_tx_frame_keeps_frame_for_main),
#else
        cmocka_unit_test(test_staged_reply_keeps_own_length_and_cks),
        cmocka_unit_test(test_staging_rejected_while_reply_streaming),
#endif
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
        cmocka_unit_test(test_rx_timeout_resets_assembly_in_one_phase),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
