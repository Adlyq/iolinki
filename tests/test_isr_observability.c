/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Two-pump ISR transport observability (Phase 1):
 *  - a frame arriving while the single mailbox slot is busy is counted;
 *  - an eager frame that fails checksum/type validation is counted;
 *  - a valid eager frame still produces a reply.
 *
 * ISR two-pump transport observability.
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "conformance_test.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
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
static unsigned tx_done_calls;

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
    tx_done_calls++;
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

/* The DLL only arms the FALLBACK transition when the PHY can report a wake-up
 * (dll_phy_has_wakeup()); provide the hook so the fast-path Fallback command
 * exercises the full transition + deadline path. */
static int mock_detect_wakeup(void* user)
{
    (void) user;
    return 0;
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
    g_phy.detect_wakeup = mock_detect_wakeup;
    iolink_dll_init(dll, &g_phy);
    dll->time_us = test_time_us;
    dll->phy_mode = IOLINK_PHY_MODE_SDCI;
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
    g_phy.detect_wakeup = mock_detect_wakeup;
    iolink_dll_init(dll, &g_phy);
    dll->time_us = test_time_us;
    dll->phy_mode = IOLINK_PHY_MODE_SDCI;
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
    if (IOLINK_ISDU_PUB_LEN(dll.isr_isdu_pub) != 2U) {
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
    if (!iolink_ring_is_empty(&dll.isr_frame_ring)) {
        return 3; /* fast path must not publish */
    }
    return 0;
}

/* SDCI_TC_0053: FlowCTRL 0x11..0x1F must answer "No Service" (0x00). */
static int test_0053_reserved_flowctrl_no_service(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0053");
    for (uint8_t fc = 0x11U; fc <= 0x1FU; fc++) {
        iolink_dll_ctx_t dll;
        init_type0_ctx(&dll);
        uint8_t frame[2];
        make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | fc), frame,
                   sizeof(frame));
        tx_n = 0U;
        feed_chk(frame, sizeof(frame));
        iolink_dll_process_isr(&dll);
        while (dll.isr_tx_active != 0U) {
            iolink_dll_process_isr(&dll);
        }
        /* Invalid FlowCTRL is deliberately left to the main pump. */
        iolink_dll_process(&dll);
        while (dll.isr_tx_active != 0U) {
            iolink_dll_process_isr(&dll);
        }
        if ((tx_n != 2U) || (tx_q[0] != 0x00U)) {
            return (int) (fc - 0x10U);
        }
    }
    return 0;
}

/* SDCI_TC_0309: a repeated FlowCTRL must repeat the previous segment and the
 * next new FlowCTRL must continue where the response left off. */
static uint8_t type0_isdu_exchange(iolink_dll_ctx_t* dll, uint8_t fc)
{
    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | fc), frame, sizeof(frame));
    tx_n = 0U;
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(dll);
    while (dll->isr_tx_active != 0U) {
        iolink_dll_process_isr(dll);
    }
    /* IDLE/ABORT are left to the main pump; valid COUNT/START are already
       answered by the ISR and do not publish a frame. */
    iolink_dll_process(dll);
    while (dll->isr_tx_active != 0U) {
        iolink_dll_process_isr(dll);
    }
    return tx_q[0];
}

static int test_0309_isdu_segment_repeat(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0309");
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t response[40];
    response[0] = 0xD1U;
    response[1] = (uint8_t) sizeof(response);
    for (uint8_t i = 2U; i < sizeof(response); i++) {
        response[i] = (uint8_t) (0x30U + i);
    }
    /* Publish a 40-octet framed response through the normal main->ISR snapshot
       handoff, then exercise the ISR segment repeat logic. */
    dll.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.isdu.resp_total = (uint8_t) sizeof(response);
    (void) memcpy(dll.isdu.resp_buf, response, sizeof(response));
    iolink_dll_process(&dll);
    if (IOLINK_ISDU_PUB_LEN(dll.isr_isdu_pub) != (uint8_t) sizeof(response)) {
        return 6;
    }

    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_START) != response[0]) {
        return 1;
    }
    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_START) != response[0]) {
        return 2;
    }

    for (uint8_t i = 1U; i < sizeof(response); i++) {
        const uint8_t fc = (uint8_t) (i & IOLINK_FLOWCTRL_COUNT_MASK);
        if (type0_isdu_exchange(&dll, fc) != response[i]) {
            return 3;
        }
        if ((i == 1U) || (i == 20U) || (i == (sizeof(response) - 1U))) {
            if (type0_isdu_exchange(&dll, fc) != response[i]) {
                return 4;
            }
        }
    }

    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_IDLE) != 0x00U) {
        return 5;
    }
    return 0;
}

/* SDCI_TC_0068: aborting a partially received Read response returns No Service. */
static int test_0068_aborted_read_response(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0068");
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t response[16];
    response[0] = 0xD1U;
    response[1] = (uint8_t) sizeof(response);
    for (uint8_t i = 2U; i < sizeof(response); i++) {
        response[i] = (uint8_t) (0x40U + i);
    }
    dll.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.isdu.resp_total = (uint8_t) sizeof(response);
    (void) memcpy(dll.isdu.resp_buf, response, sizeof(response));
    iolink_dll_process(&dll);

    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_START) != response[0]) {
        return 1;
    }
    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_ABORT) != 0x00U) {
        return 2;
    }
    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_IDLE) != 0x00U) {
        return 3;
    }
    return 0;
}

/* Type-0 page read in PREOPERATE is answered directly by the ISR. */
static int test_type0_page_read_fast_path(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0052");
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
    const iolink_od_rec_t* staged = (const iolink_od_rec_t*) iolink_ring_peek(&dll.isr_od_ring);
    if ((staged == NULL) || (staged->len != 1U) || (staged->data[0] != 0x14U) ||
        (staged->flowctrl != IOLINK_FLOWCTRL_START)) {
        return 3;
    }

    /* Main window drains the staged octet into the ISDU transport. */
    iolink_dll_process(&dll);
    if (!iolink_ring_is_empty(&dll.isr_od_ring) || (dll.isdu.req_idx != 1U) ||
        (dll.isdu.req_buf[0] != 0x14U)) {
        return 5;
    }
    return 0;
}

/** @brief Publish a frame into the mailbox ring (what the data ISR does). */
static void push_mailbox(iolink_dll_ctx_t* dll, const uint8_t* frame, uint8_t len)
{
    iolink_frame_rec_t* rec = (iolink_frame_rec_t*) iolink_ring_claim(&dll->isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    rec->len = len;
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, len);
    iolink_ring_publish(&dll->isr_frame_ring);
}

/* Mailbox already holds a frame: a new eager frame must be counted as dropped. */
static int test_mailbox_drop(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));

    /* Fill both slots: the previous frames are not consumed yet. */
    push_mailbox(&dll, frame, (uint8_t) sizeof(frame));
    push_mailbox(&dll, frame, (uint8_t) sizeof(frame));
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
    if (iolink_ring_count(&dll.isr_frame_ring) != 1U) {
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
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&dll.isr_frame_ring);
    if ((iolink_ring_count(&dll.isr_frame_ring) != 1U) || (rec == NULL) || (rec->eager != 1U)) {
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
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0065");
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
    if ((dll.isr_eager_rejects != 1U) || !iolink_ring_is_empty(&dll.isr_frame_ring)) {
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
    if ((dll.isdu.state != ISDU_STATE_RESPONSE_READY) ||
        (IOLINK_ISDU_PUB_LEN(dll.isr_isdu_pub) == 0U)) {
        return 3;
    }
    if (!iolink_ring_is_empty(&dll.isr_od_ring)) {
        return 4; /* the main pump drained every staged segment */
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

    /* Fallback (0x5A): FALLBACK + CKS-only. The ISR only flags the request;
     * the 64-bit T_FBD deadline is armed by the main pump from MinCycleTime. */
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
        (dll.isr_fallback_pending == 0U) || (dll.fallback_deadline_ms != 0U)) {
        return 3;
    }
    iolink_dll_process(&dll);
    if ((dll.isr_fallback_pending != 0U) || (dll.fallback_deadline_ms == 0U)) {
        return 5;
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

/* Characterization of the RX BYTE CHK/gap frame end. Only a short or an
 * incomplete M-sequence can end on the gap: a length-complete frame is consumed
 * by the byte engine before the CHK indication. The gap must discard the frame,
 * count only a real truncation, release the transceiver and never publish or
 * answer. */
static int test_byte_gap_incomplete(void)
{
    /* Short M-sequence (fewer than the two header octets). */
    {
        iolink_dll_ctx_t dll;
        init_ctx(&dll);
        tx_done_calls = 0U;
        const uint8_t one[1] = {IOLINK_MC_CHANNEL_PROCESS};
        feed_chk(one, sizeof(one));
        iolink_dll_process_isr(&dll);
        if (dll.framing_errors != 0U) {
            return 1; /* a short frame is not a framing error */
        }
        if ((dll.isr_rx_cnt != 0U) || (dll.isr_rx_expect != 0U)) {
            return 2;
        }
        if (!iolink_ring_is_empty(&dll.isr_frame_ring)) {
            return 3;
        }
        if (tx_done_calls != 1U) {
            return 4;
        }
    }

    /* Truncated M-sequence: header complete, known length not reached. */
    {
        iolink_dll_ctx_t dll;
        init_ctx(&dll);
        tx_done_calls = 0U;
        uint8_t frame[3];
        make_isdu_write((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START), 0x14U, frame,
                        sizeof(frame));
        feed_chk(frame, 2U); /* deliver the header, then the CHK gap */
        iolink_dll_process_isr(&dll);
        if (dll.framing_errors != 1U) {
            return 5;
        }
        if ((dll.isr_rx_cnt != 0U) || (dll.isr_rx_expect != 0U)) {
            return 6;
        }
        if (!iolink_ring_is_empty(&dll.isr_frame_ring)) {
            return 7; /* never published for the main pump */
        }
        if (dll.isr_eager_rejects != 0U) {
            return 8; /* the gap path does not run the eager validation */
        }
        if (tx_done_calls != 1U) {
            return 9;
        }
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
    rc = test_0053_reserved_flowctrl_no_service();
    if (rc != 0) {
        return 62 + rc;
    }
    rc = test_0309_isdu_segment_repeat();
    if (rc != 0) {
        return 63 + rc;
    }
    rc = test_0068_aborted_read_response();
    if (rc != 0) {
        return 65 + rc;
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
    rc = test_byte_gap_incomplete();
    if (rc != 0) {
        return 120 + rc;
    }
    return 0;
}
