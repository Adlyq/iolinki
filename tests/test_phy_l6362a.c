/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/phy_l6362a.h"
#ifdef NDEBUG
#undef NDEBUG /* Keep behavioral checks active in Release CTest builds. */
#endif
#include <assert.h>
#include <stdio.h>

static bool en, in2, ol = true, diag = true;
static int config_result;
static int tx_fail;
static uint32_t baud;
static int rx_byte_avail = 1;
static bool rx_idle_flag;

static int start(void* u)
{
    (void) u;
    en = false;
    return 0;
}
static void enable(void* u, bool b)
{
    (void) u;
    en = b;
}
static void level(void* u, bool b)
{
    (void) u;
    in2 = b;
}
static int config(void* u, uint32_t b)
{
    (void) u;
    assert(!en);
    baud = b;
    return config_result;
}
static int send_bytes(void* u, const uint8_t* b, size_t n)
{
    (void) u;
    (void) b;
    assert(en);
    return tx_fail ? -1 : (int) n;
}
static int rx_byte(void* u, uint8_t* b)
{
    (void) u;
    if (rx_byte_avail) {
        rx_byte_avail = 0;
        *b = 0xa5;
        return 1;
    }
    return 0;
}
static bool rx_idle(void* u)
{
    (void) u;
    return rx_idle_flag;
}
static bool rx_pending(void* u)
{
    (void) u;
    return rx_byte_avail || rx_idle_flag;
}
static int recv_frame(void* u, const uint8_t** frame, uint8_t* len, uint8_t* status)
{
    (void) u;
    (void) frame;
    (void) len;
    (void) status;
    return 0;
}
static int wake(void* u)
{
    (void) u;
    return 1;
}
static bool read_ol(void* u)
{
    (void) u;
    return ol;
}
static bool read_diag(void* u)
{
    (void) u;
    return diag;
}

int main(void)
{
    iolink_l6362a_ctx_t d;
    iolink_l6362a_io_t io = {.user = NULL,
                             .init = start,
                             .set_enable = enable,
                             .set_in2 = level,
                             .uart_configure = config,
                             .uart_send_complete = send_bytes,
                             .uart_recv = rx_byte,
                             .uart_rx_idle = rx_idle,
                             .uart_recv_frame = recv_frame,
                             .uart_rx_pending = rx_pending,
                             .consume_wakeup = wake,
                             .read_ol = read_ol,
                             .read_diag = read_diag};
    assert(iolink_phy_l6362a_init(&d, &io) == 0);
    const iolink_phy_api_t* p = iolink_phy_l6362a_get(&d);
    assert(p->init(p->user) == 0 && !en);
    p->set_cq_line(p->user, 1);
    p->set_mode(p->user, IOLINK_PHY_MODE_SIO);
    assert(en && in2); /* L6362A IN1=0 is noninverting. */
    diag = false;
    assert(iolink_phy_l6362a_fault(&d) == 1);
    diag = true;
    p->set_cq_line(p->user, 0);
    assert(en && !in2);
    p->set_mode(p->user, IOLINK_PHY_MODE_SDCI);
    assert(!en && baud == 4800);
    assert(iolink_phy_l6362a_fault(&d) == -1);
    p->set_baudrate(p->user, IOLINK_BAUDRATE_COM3);
    assert(baud == 230400);

    /* TX BYTE: one octet per request; EN is held for the frame. */
    uint8_t b = 1;
    assert(p->tx_octet(p->user, b) == 0 && en);
    p->tx_done(p->user);
    assert(!en);
    tx_fail = 1;
    assert(p->tx_octet(p->user, b) == -1);
    tx_fail = 0;

    /* RX BYTE: octet first, then the frame-end indication. */
    uint8_t data = 0;
    uint8_t st = 0;
    assert(p->rx_octet(p->user, &data, &st) == 1 && data == 0xa5 &&
           ((st & IOLINK_PHY_STATUS_DATA_READY) != 0U));
    rx_byte_avail = 0;
    rx_idle_flag = true;
    assert(p->rx_octet(p->user, &data, &st) == 0 &&
           ((st & IOLINK_PHY_STATUS_CHK) != 0U));
    rx_idle_flag = false;

    /* int_active: pending RX work gates the ISR pump; idle claims none. */
    rx_idle_flag = true;
    assert(p->int_active(p->user) == 1U);
    rx_idle_flag = false;
    assert(p->int_active(p->user) == 0U);

    ol = false;
    assert(iolink_phy_l6362a_overload(&d) == 1);
    assert(p->detect_wakeup(p->user) == 1);

    config_result = -1;
    p->set_mode(p->user, IOLINK_PHY_MODE_SDCI);
    assert(!en);
    p->set_mode(p->user, IOLINK_PHY_MODE_INACTIVE);
    assert(!en);
    assert(iolink_phy_l6362a_init(NULL, &io) == -1);
    puts("L6362A BYTE-mode polarity, RX/TX state, errors and diagnostics passed");
    return 0;
}
