/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Two-pump ISR transport: ISDU response snapshot + request_service staging.
 *
 * - The main pump snapshots the framed ISDU response; the ISR answers
 *   START/COUNT reads from it (Busy / No Service / data), so no read
 *   round-trips through the main loop.
 * - A main-context reply is staged and reported through request_service();
 *   the TX hooks are only invoked from the ISR.
 *
 * Only built in the IOLINK_ISR_PUMP configuration.
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
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
static unsigned request_calls;
static uint8_t chk_pending;

static int rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (rx_i < rx_n) {
        *data = rx_q[rx_i++];
        return 1;
    }
    if (chk_pending != 0U) {
        /* Type-0 frames are delimited by the transceiver frame-end (CHK). */
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

static uint8_t int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((rx_i < rx_n) || (chk_pending != 0U) || (ctx->isr_tx_active != 0U)) ? 1U : 0U;
}

static void request_service(void* user)
{
    request_calls++;
    iolink_dll_process_isr((iolink_dll_ctx_t*) user);
}

static void feed(const uint8_t* frame, size_t n, uint8_t chk)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    chk_pending = chk;
}

static void drain_tx(iolink_dll_ctx_t* ctx)
{
    while (ctx->isr_tx_active != 0U) {
        iolink_dll_process_isr(ctx);
    }
}

/* MC + CKT + PD-out, with the A.1.6 checksum in the CKT low bits. */
static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

/* Drive one request through the ISR, drain the reply, then let the main pump
 * consume the mailbox so the next frame can be published. */
static void exchange(iolink_dll_ctx_t* ctx, const uint8_t* frame, size_t len, uint8_t chk)
{
    feed(frame, len, chk);
    iolink_dll_process_isr(ctx);
    drain_tx(ctx);
    iolink_dll_process(ctx);
}

/* START/COUNT reads are served from the snapshot; Busy and No Service map to
 * 0x01/0x00. The OD width is 2 (TYPE_2_V). */
static int test_eager_isdu_snapshot(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .user = &dll,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
    };

    iolink_dll_init(&dll, &phy);
    dll.state = IOLINK_DLL_STATE_OPERATE;
    dll.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll.od_width = 2U;
    dll.pd_out_len_current = 2U;
    dll.pd_in_len_current = 0U;
    dll.cyclic_reply_precached = true;

    dll.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.isdu.resp_buf[0] = 0xD4U;
    dll.isdu.resp_buf[1] = 0x12U;
    dll.isdu.resp_buf[2] = 0x34U;
    dll.isdu.resp_buf[3] = 0xF2U;
    dll.isdu.resp_total = 4U;

    iolink_dll_prepare_cyclic_reply(&dll);
    now_us = 1000000U;
    dll.last_activity_ms = iolink_time_get_ms();
    iolink_dll_process(&dll); /* refresh the ISR snapshot */
    if ((dll.isr_isdu_len != 4U) || (dll.isr_isdu_busy != 0U)) {
        return 1;
    }

    /* START read: MC = RW | ISDU | FlowCTRL START = 0xF0. */
    uint8_t frame[4];
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if (tx_n != 3U) {
        return 2; /* two OD octets + CKS */
    }
    if ((tx_q[0] != 0xD4U) || (tx_q[1] != 0x12U)) {
        return 3;
    }

    /* COUNT 1 read: MC = RW | ISDU | COUNT 1 = 0xE1 -> next two octets. */
    make_frame(0xE1U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if (tx_n != 3U) {
        return 4;
    }
    if ((tx_q[0] != 0x34U) || (tx_q[1] != 0xF2U)) {
        return 5;
    }

    /* A repeated COUNT repeats the previous segment (same two octets). */
    make_frame(0xE1U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n != 3U) || (tx_q[0] != 0x34U) || (tx_q[1] != 0xF2U)) {
        return 8;
    }

    /* An out-of-order COUNT is a segmentation error -> No Service (0x00). */
    make_frame(0xE3U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n < 1U) || (tx_q[0] != 0x00U)) {
        return 9;
    }

    /* Busy START. */
    dll.isr_isdu_busy = 1U;
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n < 1U) || (tx_q[0] != 0x01U)) {
        return 6;
    }

    /* No Service START (no response staged). */
    dll.isr_isdu_busy = 0U;
    dll.isr_isdu_len = 0U;
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n < 1U) || (tx_q[0] != 0x00U)) {
        return 7;
    }

    return 0;
}

/* A main-context Type-0 reply must be staged and reported via request_service;
 * the ISR is the only caller of the TX hooks. */
static int test_request_service_staging(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .user = &dll,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
        .request_service = request_service,
    };

    iolink_dll_init(&dll, &phy);
    dll.state = IOLINK_DLL_STATE_PREOPERATE;
    dll.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll.pd_out_len_current = 0U;
    dll.pd_in_len_current = 0U;
    now_us = 1000000U;
    dll.last_activity_ms = iolink_time_get_ms();

    uint8_t frame[2];
    /* A Type-0 process WRITE is not served by the ISR fast path, so it is
     * staged in main context and reported through request_service. */
    make_frame(0x00U, IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    tx_n = 0U;
    request_calls = 0U;
    feed(frame, sizeof(frame), 1U);
    iolink_dll_process_isr(&dll); /* RX -> mailbox (not eager for Type 0) */
    iolink_dll_process(&dll);     /* main -> dll_send -> request_service */
    if (request_calls != 1U) {
        return 10;
    }
    drain_tx(&dll);
    if (tx_n < 1U) {
        return 11; /* Type-0 WRITE reply: CKS only */
    }

    return 0;
}

/* The diagnosis READ is served from the ISR fast path (event memory), so the
 * reply must not go through request_service / the main pump. */
static int test_type0_diagnosis_read_isr(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .user = &dll,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
        .request_service = request_service,
    };

    iolink_dll_init(&dll, &phy);
    dll.state = IOLINK_DLL_STATE_PREOPERATE;
    dll.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll.pd_out_len_current = 0U;
    dll.pd_in_len_current = 0U;
    now_us = 1000000U;
    dll.last_activity_ms = iolink_time_get_ms();

    uint8_t frame[2];
    make_frame(0xC0U, IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    tx_n = 0U;
    request_calls = 0U;
    exchange(&dll, frame, sizeof(frame), 1U);
    if (request_calls != 0U) {
        return 12;
    }
    if (tx_n != 2U) {
        return 13; /* Type-0 READ reply: OD + CKS */
    }
    return 0;
}

int main(void)
{
    int rc = test_eager_isdu_snapshot();
    if (rc != 0) {
        return rc;
    }
    rc = test_type0_diagnosis_read_isr();
    if (rc != 0) {
        return rc;
    }
    return test_request_service_staging();
}
