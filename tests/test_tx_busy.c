/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * TX FRAME line-state hook (tx_busy):
 *  - an eager ISR reply is deferred to the main pump while the PHY reports the
 *    line busy (the previous frame is still on the wire);
 *  - the PD-in reply buffer lease is released as soon as the PHY reports the
 *    line free, so two buffers are enough;
 *  - a TX_FRAME build with the hook uses two PD-in buffers.
 *
 * Built against a FRAME/FRAME copy of the library with
 * IOLINK_PHY_FEATURE_TX_BUSY=1 (see tests/CMakeLists.txt).
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/port.h"
#include "iolinki/protocol.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

static uint8_t g_frame[8];
static uint8_t g_frame_len;
static uint8_t g_frame_ready;
static unsigned g_tx_calls;
static uint8_t g_line_busy;

static const uint8_t* mock_rx_frame(void* user, uint8_t* status, uint8_t* len)
{
    (void) user;
    if (g_frame_ready == 0U) {
        *status = 0U;
        *len = 0U;
        return NULL;
    }
    g_frame_ready = 0U;
    *status = IOLINK_PHY_STATUS_DATA_READY;
    *len = g_frame_len;
    return g_frame;
}

static int mock_tx_frame(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    g_tx_calls++;
    return 0;
}

static uint8_t mock_tx_busy(void* user)
{
    (void) user;
    return g_line_busy;
}

static uint8_t mock_int_active(void* user)
{
    (void) user;
    return g_frame_ready;
}

static void mock_timer_start(void* user)
{
    (void) user;
}

static void mock_timer_stop(void* user)
{
    (void) user;
}

static void mock_request_service(void* user)
{
    (void) user;
}

static iolink_phy_api_t g_phy = {
    .user = NULL,
    .rx_frame = mock_rx_frame,
    .tx_frame = mock_tx_frame,
    .tx_busy = mock_tx_busy,
    .int_active = mock_int_active,
    .response_timer_start = mock_timer_start,
    .response_timer_stop = mock_timer_stop,
    .request_service = mock_request_service,
};

/* Type-2_V process READ in OPERATE: MC + CKT (header only, PD-out = 0). */
static void feed_process_read(void)
{
    g_frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK);
    g_frame[1] = IOLINK_MSEQ_TYPE_2;
    g_frame[1] = (uint8_t) (g_frame[1] | iolink_checksum6(g_frame, 2U));
    g_frame_len = 2U;
    g_frame_ready = 1U;
}

static void init_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.user = dll;
    iolink_dll_init(dll, &g_phy);
    dll->time_us = test_time_us;
    dll->phy_mode = IOLINK_PHY_MODE_SDCI;
    dll->state = IOLINK_DLL_STATE_OPERATE;
    dll->m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll->od_width = 1U;
    dll->pd_out_len_current = 0U;
    dll->pd_in_len_current = 0U;
    dll->pd_valid = true;
    g_frame_ready = 0U;
    g_tx_calls = 0U;
    g_line_busy = 0U;
    now_us = 1000000U;
}

/* A busy line must not be overwritten: the ISR declines and the main pump owns
 * the frame, which is then transmitted once the line is free. */
static void test_busy_defers_eager_reply(void)
{
    iolink_dll_ctx_t dll;

    init_ctx(&dll);
    g_line_busy = 1U;
    feed_process_read();

    iolink_dll_process_isr(&dll);
    assert(g_tx_calls == 0U);         /* eager reply declined */
    assert(dll.isr_tx_pending == 0U); /* nothing staged from the ISR */
    assert(dll.pd_in_captured == 0xFFU);

    iolink_dll_process(&dll);         /* main pump answers the frame */
    assert(dll.isr_tx_pending == 1U); /* staged, waiting for the ISR */

    g_line_busy = 0U;
    iolink_dll_process_isr(&dll); /* drain the staged reply */
    assert(g_tx_calls == 1U);
    assert(dll.isr_tx_pending == 0U);
}

/* The reply buffer stays leased while the transfer is on the wire and is
 * released by the main pump once tx_busy() reports the line free. */
static void test_lease_released_when_line_free(void)
{
    iolink_dll_ctx_t dll;

    init_ctx(&dll);
    feed_process_read();

    iolink_dll_process_isr(&dll); /* eager reply from the ISR */
    assert(g_tx_calls == 1U);
    assert(dll.pd_in_captured != 0xFFU);

    g_line_busy = 1U; /* still on the wire */
    iolink_dll_process(&dll);
    assert(dll.pd_in_captured != 0xFFU);

    g_line_busy = 0U; /* completion seen by the PHY */
    iolink_dll_process(&dll);
    assert(dll.pd_in_captured == 0xFFU);
}

/* Without the hook the library cannot observe the end of a TX FRAME transfer,
 * so it keeps a third buffer. */
IOLINK_STATIC_ASSERT(IOLINK_PD_IN_BUFFERS == 2U, "tx_busy build must use two PD-in buffers");

int main(void)
{
    test_busy_defers_eager_reply();
    test_lease_released_when_line_free();
    return 0;
}
