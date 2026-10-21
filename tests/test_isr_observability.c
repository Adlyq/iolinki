/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Two-pump ISR transport observability (Phase 1):
 *  - a frame arriving while the single mailbox slot is busy is counted;
 *  - an eager frame that fails checksum/type validation is counted;
 *  - a valid eager frame still produces a reply.
 *
 * Only built in the IOLINK_ISR_PUMP configuration.
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"

static uint64_t now_us;

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (now_us / 1000U);
}

uint64_t iolink_time_get_us(void)
{
    return now_us;
}

#define RX_MAX 16U
#define TX_MAX 32U

static uint8_t rx_q[RX_MAX];
static size_t rx_n;
static size_t rx_i;
static uint8_t tx_q[TX_MAX];
static size_t tx_n;
static uint8_t chk_pending;
static unsigned timer_a_starts;
static unsigned timer_a_stops;
static unsigned timer_b_starts;

static int rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (rx_i < rx_n) {
        *data = rx_q[rx_i++];
        return 1;
    }
    if (chk_pending != 0U) {
        chk_pending = 0U;
        *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}

static int tx_octet(void* user, uint8_t data)
{
    (void) user;
    if (tx_n < sizeof(tx_q)) {
        tx_q[tx_n++] = data;
    }
    return 0;
}

static void tx_done(void* user)
{
    (void) user;
}

static void timer_a_start(void* user)
{
    (void) user;
    timer_a_starts++;
}

static void timer_a_stop(void* user)
{
    (void) user;
    timer_a_stops++;
}

static void timer_b_start(void* user)
{
    (void) user;
    timer_b_starts++;
}

static uint8_t int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((rx_i < rx_n) || (chk_pending != 0U) || (ctx->isr_tx_active != 0U)) ? 1U : 0U;
}

static void feed(const uint8_t* frame, size_t n)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    chk_pending = 0U;
}

static void feed_chk(const uint8_t* frame, size_t n)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    chk_pending = 1U;
}

static void make_type2_process_read(uint8_t* frame, size_t len)
{
    frame[0] = IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK; /* READ */
    frame[1] = IOLINK_MSEQ_TYPE_2;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static iolink_phy_api_t g_phy;

static void init_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.user = dll;
    g_phy.rx_octet = rx_octet;
    g_phy.tx_octet = tx_octet;
    g_phy.tx_done = tx_done;
    g_phy.int_active = int_active;
    g_phy.response_timer_start = timer_a_start;
    g_phy.response_timer_stop = timer_a_stop;
    g_phy.stack_timer_start = timer_b_start;
    iolink_dll_init(dll, &g_phy);
    dll->state = IOLINK_DLL_STATE_OPERATE;
    dll->m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll->od_width = 1U;
    dll->pd_out_len_current = 0U;
    dll->pd_in_len_current = 0U;
    dll->cyclic_reply_precached = true;
    dll->pd_valid = true;
    iolink_dll_prepare_cyclic_reply(dll);
    now_us = 1000000U;
    rx_n = 0U;
    rx_i = 0U;
    tx_n = 0U;
}

static void exchange(iolink_dll_ctx_t* dll, const uint8_t* frame, size_t len)
{
    feed(frame, len);
    iolink_dll_process_isr(dll);
    while (dll->isr_tx_active != 0U) {
        iolink_dll_process_isr(dll);
    }
    iolink_dll_process(dll);
}

/* Type-2_V ISDU WRITE M-sequence: MC + CKT + one OD octet. */
static void make_isdu_write(const uint8_t mc, uint8_t od, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_2;
    frame[2] = od;
    for (size_t i = 3U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void make_type0(uint8_t mc, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void init_type0_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.user = dll;
    g_phy.rx_octet = rx_octet;
    g_phy.tx_octet = tx_octet;
    g_phy.tx_done = tx_done;
    g_phy.int_active = int_active;
    g_phy.response_timer_start = timer_a_start;
    g_phy.response_timer_stop = timer_a_stop;
    g_phy.stack_timer_start = timer_b_start;
    iolink_dll_init(dll, &g_phy);
    dll->state = IOLINK_DLL_STATE_PREOPERATE;
    dll->m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll->od_width = 1U;
    dll->pd_out_len_current = 0U;
    dll->pd_in_len_current = 0U;
    dll->cyclic_reply_precached = false;
    dll->pd_valid = true;
    dll->isdu.state = ISDU_STATE_RESPONSE_READY;
    dll->isdu.resp_buf[0] = 0x11U;
    dll->isdu.resp_buf[1] = 0x22U;
    dll->isdu.resp_total = 2U;
    now_us = 1000000U;
    rx_n = 0U;
    rx_i = 0U;
    chk_pending = 0U;
    tx_n = 0U;
    iolink_dll_process(dll); /* sync the ISDU snapshot */
}

/* Type-0 ISDU read in PREOPERATE is answered directly by the ISR. */
static int test_type0_isdu_read_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    if (dll.isr_isdu_len != 2U) {
        return 1;
    }

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | IOLINK_FLOWCTRL_START),
               frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }

    if ((tx_n != 2U) || (tx_q[0] != 0x11U)) {
        return 2;
    }
    if (dll.isr_frame_ready != 0U) {
        return 3; /* fast path must not publish */
    }
    return 0;
}

/* Type-0 page read in PREOPERATE is answered directly by the ISR. */
static int test_type0_page_read_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK), frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }

    const uint8_t expected = iolink_isdu_direct_param_page1_octet(&dll.isdu, 0U);
    if ((tx_n != 2U) || (tx_q[0] != expected)) {
        return 1;
    }
    return 0;
}

/* Type-0 ISDU write in PREOPERATE: ISR buffers it and replies CKS only. */
static int test_type0_isdu_write_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t frame[3];
    frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START);
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = 0x14U; /* ISDU service octet */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }

    if (tx_n != 1U) {
        return 1; /* CKS only */
    }
    /* 14d63cb: the ISR only stages the write; ctx->isdu stays main-owned. */
    if (dll.isdu.req_idx != 0U) {
        return 2;
    }
    if ((dll.isr_od_staged != 1U) || (dll.isr_od_stage_len != 1U) ||
        (dll.isr_od_stage[0] != 0x14U) || (dll.isr_od_stage_flowctrl != IOLINK_FLOWCTRL_START)) {
        return 3;
    }
    if (dll.isdu_isr_consumed != 0U) {
        return 4; /* already ACKed by the Type-0 fast path */
    }

    /* Main window drains the staged octet into the ISDU transport. */
    iolink_dll_process(&dll);
    if ((dll.isr_od_staged != 0U) || (dll.isdu.req_idx != 1U) || (dll.isdu.req_buf[0] != 0x14U)) {
        return 5;
    }
    return 0;
}

/* Mailbox already holds a frame: a new eager frame must be counted as dropped. */
static int test_mailbox_drop(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));

    dll.isr_frame_ready = 1U; /* previous frame not consumed yet */
    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }

    if (dll.isr_mailbox_drops != 1U) {
        return 1;
    }
    /* The eager reply no longer depends on the mailbox: a full mailbox only
     * drops the main-pump copy, it must not suppress the reply. */
    if (tx_n != 2U) {
        return 2;
    }
    return 0;
}

/* Bad checksum on an eager frame must be counted, not silently dropped. */
static int test_eager_reject(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));
    frame[1] ^= 0x01U; /* corrupt the checksum */

    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);

    if (dll.isr_eager_rejects != 1U) {
        return 1;
    }
    if (tx_n != 0U) {
        return 2; /* no eager reply for a corrupt frame */
    }
    if (dll.isr_frame_ready != 1U) {
        return 3; /* published so the main pump can validate/fall back */
    }
    return 0;
}

/* A valid eager frame still replies and publishes. */
static int test_valid_eager(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));

    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }

    if ((dll.isr_mailbox_drops != 0U) || (dll.isr_eager_rejects != 0U)) {
        return 1;
    }
    if ((dll.isr_frame_ready != 1U) || (dll.isr_eager_replied != 1U)) {
        return 2;
    }
    /* process READ reply: OD placeholder + CKS. */
    if (tx_n != 2U) {
        return 3;
    }
    return 0;
}

/* A corrupt ISDU write must not reach the transport. */
static int test_isdu_write_bad_crc_not_consumed(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[3];
    make_isdu_write((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START), 0x14U, frame,
                    sizeof(frame));
    frame[1] ^= 0x01U; /* corrupt the CKT checksum */

    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);

    if (dll.isdu.req_idx != 0U) {
        return 1;
    }
    if ((dll.isr_eager_rejects != 1U) || (dll.isr_frame_ready != 0U)) {
        return 2;
    }
    return 0;
}

/* Phase 2: an ISDU write split over four M-sequences is buffered by the ISR
 * and executed by the main pump, without being appended twice. */
static int test_isdu_write_consumed_by_isr(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    /* Write index 0x10 (VendorName, read-only) with one data octet:
     * I-Service 0x14, index 0x10, data 0x00, CHKPDU 0x04. */
    const uint8_t isdu_octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};
    uint8_t frame[3];
    const uint8_t flowctrl[4] = {IOLINK_FLOWCTRL_START, 1U, 2U, 3U};

    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t mc = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | flowctrl[i]);
        make_isdu_write(mc, isdu_octets[i], frame, sizeof(frame));
        exchange(&dll, frame, sizeof(frame));
    }

    /* The ISR buffered each octet exactly once (no double append in main). */
    if (dll.isdu.req_idx != 4U) {
        return 1;
    }
    if (dll.isdu.req_total != 4U) {
        return 2;
    }
    /* A write to a read-only index produces a framed negative response. */
    if ((dll.isdu.state != ISDU_STATE_RESPONSE_READY) || (dll.isr_isdu_len == 0U)) {
        return 3;
    }
    if (dll.isdu_isr_consumed != 0U) {
        return 4;
    }
    return 0;
}

/* Phase 3b: Type-0 state-changing commands are handled in the ISR. */
static int test_type0_commands_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    uint8_t frame[3];

    /* Transition command (PREOP -> ESTAB_COM), no reply. */
    make_type0(IOLINK_MC_TRANSITION_COMMAND, frame, 2U);
    tx_n = 0U;
    feed_chk(frame, 2U);
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }
    if ((dll.state != IOLINK_DLL_STATE_ESTAB_COM) || (tx_n != 0U)) {
        return 1;
    }

    /* DeviceOperate (0x99, page write addr 0): ESTAB_COM + CKS-only. */
    dll.state = IOLINK_DLL_STATE_PREOPERATE;
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_DEVICE_OPERATE;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3U));
    tx_n = 0U;
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }
    if ((dll.state != IOLINK_DLL_STATE_ESTAB_COM) || (tx_n != 1U)) {
        return 2;
    }

    /* Fallback (0x5A): FALLBACK + CKS-only + deadline. */
    dll.state = IOLINK_DLL_STATE_PREOPERATE;
    frame[2] = IOLINK_CMD_FALLBACK;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3U));
    tx_n = 0U;
    dll.fallback_deadline_ms = 0U;
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }
    if ((dll.state != IOLINK_DLL_STATE_FALLBACK) || (tx_n != 1U) ||
        (dll.fallback_deadline_ms == 0U)) {
        return 3;
    }

    /* Startup probe (page READ in STARTUP): PREOP + OD/CKS. */
    dll.state = IOLINK_DLL_STATE_STARTUP;
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), frame, 2U);
    tx_n = 0U;
    feed_chk(frame, 2U);
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }
    if ((dll.state != IOLINK_DLL_STATE_PREOPERATE) || (tx_n != 2U)) {
        return 4;
    }
    return 0;
}

/* Phase 4a: Timer A starts on the first octet and stops before the reply;
 * Timer B starts when the reply completes. */
static int test_timer_interlock(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    timer_a_starts = 0U;
    timer_a_stops = 0U;
    timer_b_starts = 0U;

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | IOLINK_FLOWCTRL_START),
               frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll);
    while (dll.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll);
    }

    if ((timer_a_starts == 0U) || (timer_a_stops == 0U) || (timer_b_starts == 0U)) {
        return 1;
    }
    return 0;
}

/* Phase 4a: Timer A timeout aborts the partial frame and counts a timeout. */
static int test_rx_timeout(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    timer_a_starts = 0U;
    timer_a_stops = 0U;

    const uint8_t partial[1] = {0x70U};
    feed(partial, sizeof(partial));
    iolink_dll_process_isr(&dll);
    if ((timer_a_starts == 0U) || (dll.isr_rx_cnt == 0U)) {
        return 1;
    }

    iolink_dll_on_rx_timeout(&dll);
    if ((dll.timeout_errors == 0U) || (dll.isr_rx_cnt != 0U) || (timer_a_stops == 0U)) {
        return 2;
    }
    return 0;
}

/* Phase 4a: Timer B tick authorises exactly one main-context window. */
static int test_stack_tick_window(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    dll.stack_tick_pending = 0U;
    if (iolink_dll_can_run_user_code(&dll) != 0U) {
        return 1; /* no tick yet (hardware hook installed) */
    }
    iolink_dll_on_stack_tick(&dll);
    if (iolink_dll_can_run_user_code(&dll) != 1U) {
        return 2;
    }
    if (iolink_dll_can_run_user_code(&dll) != 0U) {
        return 3; /* consumed exactly once */
    }
    return 0;
}

int main(void)
{
    int rc = test_mailbox_drop();
    if (rc != 0) {
        return 10 + rc;
    }
    rc = test_eager_reject();
    if (rc != 0) {
        return 20 + rc;
    }
    rc = test_valid_eager();
    if (rc != 0) {
        return 30 + rc;
    }
    rc = test_isdu_write_consumed_by_isr();
    if (rc != 0) {
        return 40 + rc;
    }
    rc = test_isdu_write_bad_crc_not_consumed();
    if (rc != 0) {
        return 50 + rc;
    }
    rc = test_type0_isdu_read_fast_path();
    if (rc != 0) {
        return 60 + rc;
    }
    rc = test_type0_page_read_fast_path();
    if (rc != 0) {
        return 70 + rc;
    }
    rc = test_type0_isdu_write_fast_path();
    if (rc != 0) {
        return 80 + rc;
    }
    rc = test_type0_commands_fast_path();
    if (rc != 0) {
        return 85 + rc;
    }
    rc = test_timer_interlock();
    if (rc != 0) {
        return 90 + rc;
    }
    rc = test_rx_timeout();
    if (rc != 0) {
        return 100 + rc;
    }
    rc = test_stack_tick_window();
    if (rc != 0) {
        return 110 + rc;
    }
    return 0;
}
