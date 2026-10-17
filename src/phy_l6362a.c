/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/phy_l6362a.h"
#include <limits.h>
#include <string.h>

static void enable(iolink_l6362a_ctx_t* d, bool on)
{
    d->io.set_enable(d->io.user, on);
    d->enabled = on;
}

static int initialize(void* u)
{
    iolink_l6362a_ctx_t* d = u;
    if (d->initialized) enable(d, false);
    d->error = d->io.init(d->io.user);
    d->initialized = d->error == 0;
    d->mode = IOLINK_PHY_MODE_INACTIVE;
    d->tx_active = false;
    if (d->initialized) enable(d, false);
    return d->error;
}

static void mode(void* u, iolink_phy_mode_t m)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized) return;
    enable(d, false);
    d->mode = m;
    if (m == IOLINK_PHY_MODE_SIO) {
        d->io.set_in2(d->io.user, d->cq);
        if (d->error == 0) enable(d, true);
    }
    else if (m == IOLINK_PHY_MODE_SDCI)
        d->error = d->io.uart_configure(d->io.user, d->baud);
    else if (m != IOLINK_PHY_MODE_INACTIVE)
        d->error = -1;
}

static void speed(void* u, iolink_baudrate_t b)
{
    iolink_l6362a_ctx_t* d = u;
    switch (b) {
        case IOLINK_BAUDRATE_COM1:
            d->baud = 4800;
            break;
        case IOLINK_BAUDRATE_COM2:
            d->baud = 38400;
            break;
        case IOLINK_BAUDRATE_COM3:
            d->baud = 230400;
            break;
        default:
            d->error = -1;
            if (d->initialized) enable(d, false);
            return;
    }
    if (d->initialized && d->mode == IOLINK_PHY_MODE_SDCI) {
        enable(d, false);
        d->error = d->io.uart_configure(d->io.user, d->baud);
    }
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME: hand out the whole frame the board just delimited (zero-copy). */
static const uint8_t* rx_frame(void* u, uint8_t* status, uint8_t* len)
{
    iolink_l6362a_ctx_t* d = u;
    if (status) *status = 0U;
    if (len) *len = 0U;
    if (!d->initialized || d->error || !d->io.uart_recv_frame) return NULL;
    const uint8_t* frame = NULL;
    uint8_t n = 0U;
    uint8_t st = 0U;
    if (d->io.uart_recv_frame(d->io.user, &frame, &n, &st) > 0) {
        if (status) *status = st;
        if (len) *len = n;
        return frame;
    }
    return NULL;
}
#else
/* RX BYTE: pull one octet; when the board reports the line idle, deliver CHK. */
static int rx_octet(void* u, uint8_t* data, uint8_t* status)
{
    iolink_l6362a_ctx_t* d = u;
    if (status) *status = 0U;
    if (!d->initialized || d->error) return -1;
    uint8_t b = 0U;
    if (d->io.uart_recv(d->io.user, &b) > 0) {
        if (data) *data = b;
        if (status) *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if (d->io.uart_rx_idle && d->io.uart_rx_idle(d->io.user)) {
        if (status) *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* TX FRAME: the whole reply in one synchronous transfer. */
static int tx_frame(void* u, const uint8_t* data, size_t len)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized || d->mode != IOLINK_PHY_MODE_SDCI || d->error || !data || !len ||
        len > INT_MAX)
        return -1;
    enable(d, true);
    int r = d->io.uart_send_complete(d->io.user, data, len);
    enable(d, false);
    return (r == (int) len) ? 0 : -1;
}
#else
/* TX BYTE: one octet per transceiver request; EN is held for the frame. */
static int tx_octet(void* u, uint8_t b)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized || d->mode != IOLINK_PHY_MODE_SDCI || d->error) return -1;
    if (!d->tx_active) {
        enable(d, true);
        d->tx_active = true;
    }
    return (d->io.uart_send_complete(d->io.user, &b, 1) == 1) ? 0 : -1;
}
#endif

static void tx_done(void* u)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized) return;
    if (d->tx_active) {
        enable(d, false);
        d->tx_active = false;
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* The synchronous transfer already released EN; nothing else to do. */
#endif
}

static uint8_t int_active(void* u)
{
    iolink_l6362a_ctx_t* d = u;
    if (!d->initialized || d->error || d->mode != IOLINK_PHY_MODE_SDCI) return 0U;
    if (d->tx_active) return 1U;
    return (d->io.uart_rx_pending && d->io.uart_rx_pending(d->io.user)) ? 1U : 0U;
}

static int wake(void* u)
{
    iolink_l6362a_ctx_t* d = u;
    return d->initialized ? d->io.consume_wakeup(d->io.user) : 0;
}

#if IOLINK_PHY_FEATURE_SIO_CQ
static void cq(void* u, uint8_t b)
{
    iolink_l6362a_ctx_t* d = u;
    d->cq = b != 0;
    if (d->initialized && !d->error && d->mode == IOLINK_PHY_MODE_SIO) {
        /* IN1=GND: table 14 CQ=IN2. IN2 transitions get dead-time protection. */
        d->io.set_in2(d->io.user, d->cq);
        enable(d, true);
    }
}
#endif

int iolink_phy_l6362a_init(iolink_l6362a_ctx_t* d, const iolink_l6362a_io_t* io)
{
    if (!d || !io || !io->init || !io->set_enable || !io->set_in2 || !io->uart_configure ||
        !io->uart_send_complete || !io->uart_rx_pending || !io->consume_wakeup)
        return -1;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    if (!io->uart_recv_frame) return -1;
#else
    if (!io->uart_recv || !io->uart_rx_idle) return -1;
#endif
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->baud = 4800;
    d->phy.user = d;
    d->phy.init = initialize;
    d->phy.set_mode = mode;
    d->phy.set_baudrate = speed;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    d->phy.rx_frame = rx_frame;
#else
    d->phy.rx_octet = rx_octet;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    d->phy.tx_frame = tx_frame;
#else
    d->phy.tx_octet = tx_octet;
#endif
    d->phy.tx_done = tx_done;
    d->phy.int_active = int_active;
#if IOLINK_PHY_FEATURE_WAKEUP
    d->phy.detect_wakeup = wake;
#endif
#if IOLINK_PHY_FEATURE_SIO_CQ
    d->phy.set_cq_line = cq;
#endif
    return 0;
}

const iolink_phy_api_t* iolink_phy_l6362a_get(iolink_l6362a_ctx_t* d)
{
    return d ? &d->phy : NULL;
}

int iolink_phy_l6362a_fault(const iolink_l6362a_ctx_t* d)
{
    if (!d || !d->initialized || !d->enabled || !d->io.read_diag) return -1;
    return d->io.read_diag(d->io.user) ? 0 : 1;
}

int iolink_phy_l6362a_overload(const iolink_l6362a_ctx_t* d)
{
    if (!d || !d->initialized || !d->io.read_ol) return -1;
    return d->io.read_ol(d->io.user) ? 0 : 1;
}
