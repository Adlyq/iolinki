/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_helpers.c
 * @brief Shared test utilities and mock implementations
 */

#include "test_helpers.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "iolinki/platform.h"

/* Test buffers */
uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

/* Mock implementations */

static int g_mock_wakeup = 0;
static uint8_t g_mock_cq_state = 0U;
static uint32_t g_mock_send_delay_us = 0U;

/* Transport warp: both transports read from g_rxq and converge on mock_phy_send. */
#define MOCK_RXQ_SIZE 8192U
static uint8_t g_rxq[MOCK_RXQ_SIZE];
static size_t g_rxq_head;
static size_t g_rxq_tail;
static size_t g_chk_pending;
static int g_rx_err_pending;
static uint8_t g_txq[1024];
static size_t g_txq_len;
static uint8_t g_tx_active;
/* Strong deterministic test storage. The library's weak unsupported defaults
 * can otherwise satisfy archive references before the Linux backend is loaded. */
static uint8_t g_nvm_mock[4096];
int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len)
{
    if (offset > sizeof(g_nvm_mock) || len > sizeof(g_nvm_mock) - offset ||
        (len != 0U && data == NULL))
        return -1;
    if (len != 0U) memcpy(data, g_nvm_mock + offset, len);
    return 0;
}
int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len)
{
    if (offset > sizeof(g_nvm_mock) || len > sizeof(g_nvm_mock) - offset ||
        (len != 0U && data == NULL))
        return -1;
    if (len != 0U) memcpy(g_nvm_mock + offset, data, len);
    return 0;
}

int mock_phy_init(void* user)
{
    (void) user;
    return (int) mock();
}

void mock_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    check_expected(mode);
}

void mock_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    check_expected(baudrate);
}

int mock_phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    check_expected_ptr(data);
    check_expected(len);
    if (g_mock_send_delay_us > 0U) {
        usleep(g_mock_send_delay_us);
    }
    return (int) mock();
}

int mock_phy_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    /* Deliver queued octets first, then a pending transport error. */
    if (g_rxq_tail != g_rxq_head) {
        *byte = g_rxq[g_rxq_tail++];
        return 1;
    }
    if (g_rx_err_pending) {
        g_rx_err_pending = 0;
        return -1;
    }
    return 0;
}

int mock_phy_detect_wakeup(void* user)
{
    (void) user;
    int ret = g_mock_wakeup;
    g_mock_wakeup = 0;
    return ret;
}

void mock_phy_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    g_mock_cq_state = state;
}

/* ---- Raw-octet ISR transport (RX BYTE / TX BYTE) ----------------------- */
void mock_phy_feed_byte(uint8_t b)
{
    if (g_rxq_head < MOCK_RXQ_SIZE) {
        g_rxq[g_rxq_head++] = b;
    }
}
void mock_phy_feed_chk(void)
{
    g_chk_pending++;
}
void mock_phy_feed_error(void)
{
    g_rx_err_pending = 1;
}
void mock_phy_clear_rx(void)
{
    g_rxq_head = 0U;
    g_rxq_tail = 0U;
    g_chk_pending = 0U;
    g_rx_err_pending = 0;
}
void mock_phy_clear_tx(void)
{
    g_txq_len = 0U;
}
const uint8_t* mock_phy_tx(size_t* len)
{
    if (len != NULL) {
        *len = g_txq_len;
    }
    return g_txq;
}

static int mock_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    if (g_rxq_tail != g_rxq_head) {
        *data = g_rxq[g_rxq_tail++];
        *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if (g_rx_err_pending) {
        g_rx_err_pending = 0;
        *status = 0U;
        return -1;
    }
    if (g_chk_pending != 0U) {
        g_chk_pending--;
        *status = IOLINK_PHY_STATUS_CHK;
        return 0;
    }
    *status = 0U;
    return 0;
}

static int mock_tx_octet(void* user, uint8_t b)
{
    (void) user;
    if (!g_tx_active) {
        g_txq_len = 0U; /* start of a new reply */
        g_tx_active = 1U;
    }
    if (g_txq_len < sizeof(g_txq)) {
        g_txq[g_txq_len++] = b;
    }
    return 0;
}

static void mock_tx_done(void* user)
{
    if (!g_tx_active) {
        /* CHK-path reset, not a reply: nothing to deliver. */
        return;
    }
    g_tx_active = 0U;
    /* Hand the collected reply to the same expectation streaming uses. */
    (void) mock_phy_send(user, g_txq, g_txq_len);
}

static uint8_t mock_int_active(void* user)
{
    (void) user;
    if (g_tx_active) {
        return 1U;
    }
    if (g_rxq_tail != g_rxq_head) {
        return 1U;
    }
    if (g_rx_err_pending) {
        return 1U;
    }
    if (g_chk_pending != 0U) {
        return 1U;
    }
    return 0U;
}

const iolink_phy_api_t g_phy_mock = {.user = NULL,
                                     .init = mock_phy_init,
                                     .set_mode = mock_phy_set_mode,
                                     .set_baudrate = mock_phy_set_baudrate,
                                     .send = mock_phy_send,
                                     .recv_byte = mock_phy_recv_byte,
                                     .detect_wakeup = mock_phy_detect_wakeup,
                                     .set_cq_line = mock_phy_set_cq_line,
                                     .get_voltage_mv = NULL,
                                     .is_short_circuit = NULL,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
                                     .rx_frame = NULL,
#else
                                     .rx_octet = mock_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
                                     .tx_frame = NULL,
#else
                                     .tx_octet = mock_tx_octet,
#endif
                                     .tx_done = mock_tx_done,
                                     .int_active = mock_int_active};

void setup_mock_phy(void)
{
    /* Use -1 for infinite expectations to avoid errors on earlier test exit. */
    expect_any_count(mock_phy_set_mode, mode, -1);
    expect_any_count(mock_phy_set_baudrate, baudrate, -1);

    /* NO default will_return here. Tests must provide them. */
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
    g_txq_len = 0U;
    g_tx_active = 0U;
    mock_phy_clear_rx();
}

/* Drive both pumps for one request: the ISR pump frames the queued octets (and
 * drains the reply), then the main pump consumes the mailbox and runs the
 * protocol (Type-0 replies are sent by the main pump). In the streaming build,
 * one main-pump pass drains g_rxq directly. */
void test_io_cycle(iolink_device_ctx_t* ctx)
{
#if IOLINK_PHY_FEATURE_ISR_PUMP
    iolink_device_process_isr(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    /* Drop a leftover frame-end marker (Type-1/2 frames complete by length). */
    mock_phy_clear_rx();
    iolink_device_process(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
#else
    iolink_device_process(ctx);
#endif
}

int iolink_test_device_init(iolink_test_device_t* dev, const iolink_config_t* stack,
                            const iolink_app_callbacks_t* callbacks)
{
    if (dev == NULL) {
        return -1;
    }

    (void) memset(dev, 0, sizeof(*dev));
    dev->cfg.phy = g_phy_mock;
    if (stack != NULL) {
        dev->cfg.stack = *stack;
    }
    else {
        dev->cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
        dev->cfg.stack.min_cycle_time = 0U;
    }
    dev->cfg.app_callbacks = callbacks;

    return iolink_device_init(&dev->ctx, &dev->cfg);
}

void move_to_operate_ctx(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* STARTUP -> AWAITING_COMM (wake-up via the PHY). Timing enforcement is off,
     * so no wall-clock T_DWU wait is needed: the pump is event driven. */
    g_mock_wakeup = 1;
    test_io_cycle(ctx);

    /* AWAITING_COMM -> PREOPERATE (transition command MC=0x0F). */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();
    test_io_cycle(ctx);

    /* PREOPERATE/ESTAB_COM -> OPERATE (first valid frame for the type). */
    iolink_m_seq_type_t type = iolink_device_get_m_seq_type(ctx);
    uint8_t pd_out_len = iolink_device_get_pd_out_len(ctx);
    uint8_t pd_in_len = iolink_device_get_pd_in_len(ctx);
#if IOLINK_PHY_FEATURE_ISR_PUMP
    uint8_t od_w = ctx->dll.od_width; /* actual OPERATE OD width (1/2/8/32) */
#else
    /* Streaming keeps upstream's 1/2-octet OD width. */
    uint8_t od_w = (type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;
#endif

    if (type == IOLINK_M_SEQ_TYPE_0) {
        uint8_t idle_mc = 0x00;
        uint8_t idle_ck = test_frame_checksum(idle_mc);
        mock_phy_feed_byte(idle_mc);
        mock_phy_feed_byte(idle_ck);
        mock_phy_feed_chk();

        expect_any(mock_phy_send, data);
        expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
        will_return(mock_phy_send, 0);
        test_io_cycle(ctx);
        return;
    }

    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    /* Figure A.2: master message is MC, CKT, data... with the A.1.6 checksum in
       the CKT octet (byte 1); there is no trailing checksum octet.
       Use a WRITE M-sequence: its request layout (MC, CKT, PD-out, OD) and
       reply (PD-in, CKS) are identical on both transports, unlike a READ
       request whose OD presence differs. */
    uint8_t frame_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + pd_out_len + od_w);
    frame[0] = 0x00;
    switch (type) {
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            frame[1] = IOLINK_MSEQ_TYPE_1;
            break;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_3:
        case IOLINK_M_SEQ_TYPE_2_4:
        case IOLINK_M_SEQ_TYPE_2_5:
        case IOLINK_M_SEQ_TYPE_2_V:
            frame[1] = IOLINK_MSEQ_TYPE_2;
            break;
        default:
            frame[1] = IOLINK_MSEQ_TYPE_0;
            break;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, frame_len));

    for (uint8_t i = 0U; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* A WRITE reply carries only the PD-in plus CKS. */
    uint8_t resp_len = (uint8_t) (pd_in_len + 1U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, resp_len);
    will_return(mock_phy_send, 0);

    test_io_cycle(ctx);
}

void iolink_phy_mock_reset(void)
{
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
}

void iolink_phy_mock_set_wakeup(int detected)
{
    g_mock_wakeup = detected;
}

uint8_t iolink_phy_mock_get_cq_state(void)
{
    return g_mock_cq_state;
}

void iolink_phy_mock_set_send_delay_us(uint32_t delay_us)
{
    g_mock_send_delay_us = delay_us;
}

/* Mock Storage for Data Storage (DS) testing */
#include "iolinki/data_storage.h"
#define DS_MOCK_SIZE ((size_t) IOLINK_STORAGE_TOTAL_SIZE)
static uint8_t g_ds_mock_buf[DS_MOCK_SIZE];

int ds_mock_read(void* user, uint32_t addr, uint8_t* buf, size_t len)
{
    (void) user;
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(buf, &g_ds_mock_buf[addr], len);
    return 0;
}

int ds_mock_write(void* user, uint32_t addr, const uint8_t* buf, size_t len)
{
    (void) user;
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(&g_ds_mock_buf[addr], buf, len);
    return 0;
}

const iolink_storage_api_t g_ds_storage_mock = {
    .user = NULL, .read = ds_mock_read, .write = ds_mock_write, .erase = NULL};

void iolink_ds_mock_reset(void)
{
    memset(g_ds_mock_buf, 0, DS_MOCK_SIZE);
}

uint8_t* iolink_ds_mock_get_buf(void)
{
    return g_ds_mock_buf;
}

void iolink_nvm_mock_cleanup(void)
{
    memset(g_nvm_mock, 0, sizeof(g_nvm_mock));
    remove("iolink_nvm.bin");
}

/* ISDU V1.1.5 Interleaved Format Helpers */

static uint8_t helper_chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

/** @brief Feed one complete ISDU request over the spec transport (C3). */
static void helper_send_isdu(iolink_isdu_ctx_t* ctx, uint8_t* buf, size_t total)
{
    buf[total - 1U] = 0x00U;
    buf[total - 1U] = helper_chkpdu(buf, total);
    iolink_isdu_od_write(ctx, IOLINK_FLOWCTRL_START, buf, (uint8_t) total);
}

int isdu_send_read_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    uint8_t buf[8];
    size_t n = 0U;

    if (index > 0xFFU) {
        /* 16-bit Index and 8-bit Subindex (I-Service 0xB, Length 0x5). */
        buf[n++] = 0xB5U;
        buf[n++] = (uint8_t) (index >> 8);
        buf[n++] = (uint8_t) (index & 0xFFU);
        buf[n++] = subindex;
    }
    else if (subindex != 0U) {
        /* 8-bit Index and 8-bit Subindex (I-Service 0xA, Length 0x4). */
        buf[n++] = 0xA4U;
        buf[n++] = (uint8_t) index;
        buf[n++] = subindex;
    }
    else {
        /* 8-bit Index (I-Service 0x9, Length 0x3). */
        buf[n++] = 0x93U;
        buf[n++] = (uint8_t) index;
    }

    helper_send_isdu(ctx, buf, n + 1U);
    return 1;
}

int isdu_send_write_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                            const uint8_t* data, uint8_t data_len)
{
    uint8_t buf[IOLINK_ISDU_BUFFER_SIZE];
    size_t n = 0U;

    /* Octet count without the ExtLength octet. */
    size_t base_total;
    if (index > 0xFFU) {
        base_total = (size_t) data_len + 1U /* I-Service */ + 2U /* index */ + 1U /* sub */ + 1U;
    }
    else if (subindex != 0U) {
        base_total = (size_t) data_len + 1U + 1U /* index */ + 1U /* sub */ + 1U;
    }
    else {
        base_total = (size_t) data_len + 1U + 1U /* index */ + 1U;
    }

    const bool ext = (base_total > 15U);
    const size_t total = ext ? (base_total + 1U) : base_total;
    const uint8_t base = (index > 0xFFU) ? 0x30U : ((subindex != 0U) ? 0x20U : 0x10U);
    buf[n++] = (uint8_t) (base | (ext ? 0x01U : (uint8_t) total));
    if (ext) {
        buf[n++] = (uint8_t) total;
    }
    if (index > 0xFFU) {
        buf[n++] = (uint8_t) (index >> 8);
        buf[n++] = (uint8_t) (index & 0xFFU);
        buf[n++] = subindex;
    }
    else {
        buf[n++] = (uint8_t) index;
        if (subindex != 0U) {
            buf[n++] = subindex;
        }
    }
    for (uint8_t i = 0U; i < data_len; i++) {
        buf[n++] = data[i];
    }

    helper_send_isdu(ctx, buf, total);
    return 1;
}

int isdu_collect_response(iolink_isdu_ctx_t* ctx, uint8_t* buffer, size_t buffer_size)
{
    size_t idx = 0;
    uint8_t byte;

    if (ctx == NULL || ctx->state != ISDU_STATE_RESPONSE_READY) return -1;

    while (idx < buffer_size && iolink_isdu_get_response_byte(ctx, &byte) > 0) {
        buffer[idx++] = byte;
    }

    return (int) idx;
}
