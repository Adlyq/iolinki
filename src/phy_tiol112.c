/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "iolinki/phy_tiol112.h"
#include <limits.h>
#include <string.h>

static int initialize(iolink_app_t* IOLINK_NULLABLE self)
{
    iolink_tiol112_ctx_t* d = (iolink_tiol112_ctx_t*) self;
    if (d->initialized) d->io.set_enable(d->io.board, false);
    d->mode = IOLINK_PHY_MODE_INACTIVE;
    d->error = d->io.init(d->io.board);
    d->initialized = d->error == 0;
    if (d->initialized) d->io.set_enable(d->io.board, false);
    return d->error;
}

static void set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    iolink_tiol112_ctx_t* d = (iolink_tiol112_ctx_t*) self;
    if (!d->initialized) return;
    d->io.set_enable(d->io.board, false);
    d->mode = mode;
    if (mode == IOLINK_PHY_MODE_SIO) {
        d->io.set_sio_tx(d->io.board, d->cq == 0U);
        if (d->error == 0) d->io.set_enable(d->io.board, true);
    }
    else if (mode == IOLINK_PHY_MODE_SDCI) {
        d->error = d->io.uart_configure(d->io.board, d->baud);
    }
    else if (mode != IOLINK_PHY_MODE_INACTIVE) {
        d->error = -1;
    }
}

static int detect_wakeup(iolink_app_t* IOLINK_NULLABLE self)
{
    iolink_tiol112_ctx_t* d = (iolink_tiol112_ctx_t*) self;
    if (!d->initialized) return 0;
    return d->io.consume_wakeup(d->io.board);
}

static void set_cq(iolink_app_t* IOLINK_NULLABLE self, uint8_t state)
{
    iolink_tiol112_ctx_t* d = (iolink_tiol112_ctx_t*) self;
    d->cq = state != 0U ? 1U : 0U;
    if ((d->mode == IOLINK_PHY_MODE_SIO) && (d->error == 0)) {
        /* TI table 8-2：TX 高电平时 CQ 为灌电流，TX 低电平时 CQ 为拉电流。 */
        d->io.set_sio_tx(d->io.board, d->cq == 0U);
        d->io.set_enable(d->io.board, true);
    }
}

int iolink_phy_tiol112_fault(const iolink_tiol112_ctx_t* d)
{
    if ((d == NULL) || !d->initialized || (d->io.read_nfault == NULL)) return -1;
    return d->io.read_nfault(d->io.board) ? 0 : 1;
}

int iolink_phy_tiol112_init(iolink_tiol112_ctx_t* d, const iolink_tiol112_io_t* io)
{
    if ((d == NULL) || (io == NULL) || (io->init == NULL) || (io->set_enable == NULL) ||
        (io->set_sio_tx == NULL) || (io->uart_configure == NULL) ||
        (io->uart_send_complete == NULL) || (io->uart_recv == NULL) || (io->consume_wakeup == NULL))
        return -1;
    memset(d, 0, sizeof(*d));
    d->io = *io;
    d->baud = IOLINK_COM_HZ;
    d->phy.self = (iolink_app_t * IOLINK_NULLABLE) d;
    d->phy.init = initialize;
    d->phy.set_mode = set_mode;
    d->phy.detect_wakeup = detect_wakeup;
    d->phy.set_cq_line = set_cq;
    return 0;
}

const iolink_phy_api_t* IOLINK_NULLABLE iolink_phy_tiol112_get(iolink_tiol112_ctx_t* d)
{
    return d != NULL ? &d->phy : NULL;
}
