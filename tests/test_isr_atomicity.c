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

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/platform.h"
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

static const uint8_t* phy_rx_frame(void* user, uint8_t* status, uint8_t* len)
{
    (void) user;
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

static int phy_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
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

static uint8_t phy_int_active(void* user)
{
    (void) user;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return s_frame_pending ? 1U : 0U;
#else
    return ((s_rxq_pos < s_rxq_len) || s_chk_pending) ? 1U : 0U;
#endif
}

static int s_tx_calls;
static int s_tx_rc;
static uint32_t s_service_calls;
static bool s_service_in_critical;
static void (*s_timer_start_hook)(void);

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
static int phy_tx_frame(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    s_tx_calls++;
    return s_tx_rc;
}
#else
static int phy_tx_octet(void* user, uint8_t data)
{
    (void) user;
    (void) data;
    s_tx_calls++;
    return 0;
}

static void phy_tx_done(void* user)
{
    (void) user;
}
#endif

static void phy_timer_start(void* user)
{
    (void) user;
    if (s_timer_start_hook != NULL) {
        s_timer_start_hook();
    }
}

static void phy_timer_stop(void* user)
{
    (void) user;
}

static void phy_request_service(void* user)
{
    (void) user;
    s_service_calls++;
    s_service_in_critical = s_in_critical;
}

static int phy_init(void* user)
{
    (void) user;
    return 0;
}

static void phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static int phy_detect_wakeup(void* user)
{
    (void) user;
    return 0;
}

static const iolink_phy_api_t s_phy = {
    .user = NULL,
    .init = phy_init,
    .set_mode = phy_set_mode,
    .set_baudrate = phy_set_baudrate,
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
    s_tx_calls = 0;
    s_tx_rc = 0;
    s_service_calls = 0U;
    s_service_in_critical = false;
    s_timer_start_hook = NULL;
    s_crit_enters = 0U;
    s_crit_exits = 0U;
    s_in_critical = false;

    iolink_dll_init(&s_ctx.main, &s_ctx.isr, &s_phy);
    /* 直接驱动共享的帧处理器：OPERATE、SDCI、不强制时序。 */
    s_ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    s_ctx.main.enforce_timing = false;
    s_ctx.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_ctx.main.shared.od_width = 1U;
    s_ctx.main.od_len = 1U;
    s_ctx.main.shared.pd_in_len = 0U;
    s_ctx.main.pd_out_len = 0U;
    s_ctx.main.pd_in_len_current = 0U;
    s_ctx.main.shared.pd_out_len_current = 0U;
    s_ctx.main.pd_valid = true;
}

/* 补全一个 M 序列：CKT 八位组承载其类型位 + A.1.6 CKS。 */
static void make_frame(uint8_t* frame, uint8_t len)
{
    frame[1] = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
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
    /* 带 FlowCTRL IDLE 的 Type-0 ISDU 读：ISR 快速路径把它留给
     * 主泵，由主泵通过 dll_send() 应答。 */
    uint8_t frame[2] = {
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE), 0x00U};

    reset_ctx();
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));

    iolink_dll_process_isr(&s_ctx.main);

    /* dll_send() 中的暂存路径（槽位选择、载荷复制、发布）归主上下文
     * 所有，不得被平台临界区包裹：互斥属于直接发送的回退路径，
     * 而不属于这里的一次发布。通过在应答该 ISDU 读的那一趟
     * 前后统计进入次数来固定这一点；这样即使 request_service() 仍在
     * 该段已释放的情况下运行，重新在复制前后加入
     * IOLINK_CRITICAL_ENTER/EXIT 的回归也会暴露出来。
     * 这一趟没有其它临界区：mock PHY 没有诊断钩子，
     * ISDU FlowCTRL IDLE 读只复位传输，时序强制关闭
     * （不产生 t_CYCLE 事件），DLL 状态不变，且
     * 邮箱消费不取锁。本套件在 BYTE/BYTE 配置下运行该测试；
     * FRAME 粒度仅做编译验证。 */
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
    s_ctx.main.od_len = 1U;
    s_ctx.main.pd_out_len = 1U;
    s_ctx.main.shared.pd_out_len_current = 1U;
    s_ctx.main.shared.pd_in_len = 1U;
    s_ctx.main.pd_in_len_current = 1U;
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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reply_staging_pokes_phy_outside_critical),
        cmocka_unit_test(test_nested_pump_is_deferred),
        cmocka_unit_test(test_state_notification_is_level_based),
        cmocka_unit_test(test_isr_startup_notification_is_not_coalesced),
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        cmocka_unit_test(test_busy_tx_frame_keeps_frame_for_main),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
