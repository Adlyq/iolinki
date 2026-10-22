/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Concurrency regressions for the opt-in ISR pump:
 *   - main-context reply staging is serialised against the data ISR even when
 *     the application provides no lock (platform critical-section fallback);
 *   - a pump invocation that is preempted by a higher-priority vector is
 *     deferred instead of re-entering the single-writer state;
 *   - a PHY that reports its line busy (tx_frame() < 0) does not lose the
 *     frame: the main pump keeps ownership and stages the reply itself.
 *
 * The test drives the DLL directly with a minimal PHY so it exercises the
 * public DLL contract rather than a board driver.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/platform.h"
#include "iolinki/protocol.h"

#if IOLINK_PHY_FEATURE_ISR_PUMP

static iolink_dll_ctx_t s_ctx;

/* Platform critical-section observation (overrides the weak no-op default). */
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

    iolink_dll_init(&s_ctx, &s_phy);
    /* Drive the shared frame handlers directly: OPERATE, SDCI, no timing. */
    s_ctx.state = IOLINK_DLL_STATE_OPERATE;
    s_ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    s_ctx.enforce_timing = false;
    s_ctx.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_ctx.od_width = 1U;
    s_ctx.od_len = 1U;
    s_ctx.pd_in_len = 0U;
    s_ctx.pd_out_len = 0U;
    s_ctx.pd_in_len_current = 0U;
    s_ctx.pd_out_len_current = 0U;
    s_ctx.pd_valid = true;
}

/* Complete an M-sequence: the CKT octet carries its type bits + A.1.6 CKS. */
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

/* A no-lock device must still stage main-context replies atomically: the
 * platform critical section is held while request_service() is poked. */
static void test_reply_staging_uses_platform_critical(void** state)
{
    (void) state;
    /* Type-0 ISDU read with FlowCTRL IDLE: the ISR fast path leaves it to the
     * main pump, which answers it through dll_send(). */
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU |
                                   IOLINK_FLOWCTRL_IDLE),
                        0x00U};

    reset_ctx();
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));

    iolink_dll_process_isr(&s_ctx);
    iolink_dll_process(&s_ctx);

    assert_int_equal(s_service_calls, 1U);
    assert_true(s_service_in_critical);
    assert_int_equal(s_crit_enters, s_crit_exits);
    assert_true(s_ctx.isr_tx_pending != 0U);
}

static uint32_t s_nested_calls;

static void nested_pump_hook(void)
{
    s_nested_calls++;
    /* Model a higher-priority vector invoking the pump mid-frame. */
    iolink_dll_process_isr(&s_ctx);
}

/* A preempting pump invocation must defer to the outer one instead of
 * re-entering the single-writer RX/TX staging state. */
static void test_nested_pump_is_deferred(void** state)
{
    (void) state;
    /* Type-0 page READ in OPERATE: answered eagerly from the ISR. */
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), 0x00U};

    reset_ctx();
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));
    s_nested_calls = 0U;
    s_timer_start_hook = nested_pump_hook;

    iolink_dll_process_isr(&s_ctx);

    assert_int_equal(s_nested_calls, 1U);
    assert_int_equal(s_ctx.isr_reentry_pending, 0U);
    assert_int_equal(s_ctx.in_isr, 0U);
    /* Only the outer invocation consumed the request. */
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
    s_cb_in_isr = (s_ctx.in_isr != 0U);
    if (s_state_count < (uint8_t) (sizeof(s_states) / sizeof(s_states[0]))) {
        s_states[s_state_count++] = state;
    }
}

/* Two ISR-side transitions before the main window must both be delivered, in
 * order: a single deferred slot would overwrite the earlier one. */
static void test_deferred_states_are_not_overwritten(void** state)
{
    (void) state;
    uint8_t probe[2] = {(uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), 0x00U};
    uint8_t trans[2] = {IOLINK_MC_TRANSITION_COMMAND, 0x00U};

    reset_ctx();
    s_ctx.state_cb = state_cb;
    s_ctx.state_cb_user = NULL;
    s_ctx.state = IOLINK_DLL_STATE_STARTUP;
    s_state_count = 0U;
    s_cb_in_isr = false;

    /* Startup probe (T1): STARTUP -> PREOPERATE, answered by the ISR. */
    make_frame(probe, sizeof(probe));
    feed_frame(probe, sizeof(probe));
    iolink_dll_process_isr(&s_ctx);
    while (s_ctx.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_ctx);
    }
    assert_int_equal(s_ctx.state, IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(s_state_count, 0U); /* deferred to the main window */

    /* Transition command: PREOPERATE -> ESTAB_COM, before main runs. */
    make_frame(trans, sizeof(trans));
    feed_frame(trans, sizeof(trans));
    iolink_dll_process_isr(&s_ctx);
    while (s_ctx.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_ctx);
    }
    assert_int_equal(s_ctx.state, IOLINK_DLL_STATE_ESTAB_COM);
    assert_int_equal(s_state_count, 0U);

    iolink_dll_flush_state_callbacks(&s_ctx);

    assert_int_equal(s_state_count, 2U);
    assert_int_equal(s_states[0], IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(s_states[1], IOLINK_DLL_STATE_ESTAB_COM);
    assert_false(s_cb_in_isr);
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* A busy PHY (previous reply still on the wire) must not swallow the frame: the
 * eager reply is abandoned and the main pump stages it on the next window. */
static void test_busy_tx_frame_keeps_frame_for_main(void** state)
{
    (void) state;
    /* Type-2_1 process WRITE: MC + CKT + PD-out + OD. */
    uint8_t frame[4] = {IOLINK_MC_CHANNEL_PROCESS, (uint8_t) IOLINK_MSEQ_TYPE_2, 0xAAU, 0x55U};

    reset_ctx();
    s_ctx.m_seq_type = IOLINK_M_SEQ_TYPE_2_1;
    s_ctx.od_width = 1U;
    s_ctx.od_len = 1U;
    s_ctx.pd_out_len = 1U;
    s_ctx.pd_out_len_current = 1U;
    s_ctx.pd_in_len = 1U;
    s_ctx.pd_in_len_current = 1U;
    make_frame(frame, sizeof(frame));
    feed_frame(frame, sizeof(frame));

    s_tx_rc = -1; /* line busy */
    iolink_dll_process_isr(&s_ctx);

    assert_int_equal(s_tx_calls, 1);
    assert_int_equal(s_ctx.isr_eager_replied, 0U);
    assert_int_equal(s_ctx.isr_frame_ready, 1U); /* main pump owns the frame */

    s_tx_rc = 0;
    iolink_dll_process(&s_ctx);
    assert_true(s_service_calls > 0U);
    assert_true(s_ctx.isr_tx_pending != 0U);

    /* The next ISR pass transmits the staged reply. */
    iolink_dll_process_isr(&s_ctx);
    assert_int_equal(s_tx_calls, 2);
}
#endif

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reply_staging_uses_platform_critical),
        cmocka_unit_test(test_nested_pump_is_deferred),
        cmocka_unit_test(test_deferred_states_are_not_overwritten),
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        cmocka_unit_test(test_busy_tx_frame_keeps_frame_for_main),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}

#else /* !IOLINK_PHY_FEATURE_ISR_PUMP */

int main(void)
{
    return 0;
}

#endif /* IOLINK_PHY_FEATURE_ISR_PUMP */
