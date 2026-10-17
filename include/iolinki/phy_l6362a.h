/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOLINK_PHY_L6362A_H
#define IOLINK_PHY_L6362A_H
#include "iolinki/phy.h"

/* L6362A ISO 1.1 transceiver adapter for the ISR transport path.
 *
 * The board supplies the low-level I/O. Which hooks are used depends on the
 * build's ISR mode switches:
 *   - RX BYTE : uart_recv() + uart_rx_idle() (byte-gap frame end)
 *   - RX FRAME: uart_recv_frame() (whole frame, e.g. DMA + UART IDLE)
 *   - TX BYTE : uart_send_complete() of one octet per request
 *   - TX FRAME: uart_send_complete() of the whole reply frame
 *
 * IN1 must be hardwired low: CQ=IN2, RX=OUTI/Q (ST tables 14/15). EN must drive
 * through the board series resistor; DIAG samples its IC side. init leaves EN
 * low. uart_send_complete is synchronous and suppresses the local echo. OL is
 * overload/current limitation, not a dedicated wake output. */
typedef struct
{
    void* user;
    int (*init)(void*);
    void (*set_enable)(void*, bool);
    void (*set_in2)(void*, bool);
    int (*uart_configure)(void*, uint32_t);

    /* TX: transmit one octet (TX BYTE) or a whole frame (TX FRAME), synchronous. */
    int (*uart_send_complete)(void*, const uint8_t*, size_t);

    /* RX BYTE: non-blocking single-octet receive (1 = got one, 0 = none, <0 err). */
    int (*uart_recv)(void*, uint8_t*);
    /* RX BYTE: true once after the line went idle (frame end since last call). */
    bool (*uart_rx_idle)(void*);

    /* RX FRAME: hand out one whole received frame (zero-copy pointer). */
    int (*uart_recv_frame)(void*, const uint8_t**, uint8_t*, uint8_t*);

    /* True when receive work is pending (octet / frame / frame-end). */
    bool (*uart_rx_pending)(void*);

    int (*consume_wakeup)(void*);
    bool (*read_ol)(void*);
    bool (*read_diag)(void*);
} iolink_l6362a_io_t;

typedef struct
{
    iolink_phy_api_t phy;
    iolink_l6362a_io_t io;
    iolink_phy_mode_t mode;
    uint32_t baud;
    int error;
    bool initialized;
    bool enabled;
    bool cq;
    bool tx_active;
} iolink_l6362a_ctx_t;

int iolink_phy_l6362a_init(iolink_l6362a_ctx_t*, const iolink_l6362a_io_t*);
const iolink_phy_api_t* iolink_phy_l6362a_get(iolink_l6362a_ctx_t*);
/* 1=overload asserted; 0=clear; -1=unavailable. No short/thermal cause inferred. */
int iolink_phy_l6362a_overload(const iolink_l6362a_ctx_t*);
/* DIAG fault only meaningful while EN requested high; -1 otherwise.
 * Asserted aggregates cut-off/UVLO/overtemperature; cannot identify cause. */
int iolink_phy_l6362a_fault(const iolink_l6362a_ctx_t*);

#endif
