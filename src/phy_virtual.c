/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/* cfmakeraw() is a BSD/GNU extension; request it before any libc header so the
   declaration is visible under strict libc configurations (e.g. Zephyr native_sim). */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

/**
 * @file phy_virtual.c
 * @brief Virtual PHY backed by a POSIX serial port / pseudo-terminal.
 * @ingroup iolinki_phy_virtual
 *
 * Implements the PHY API over a host file descriptor (TTY or pipe) so the
 * device stack can be exercised via loopback or a virtual serial link during
 * development and testing.
 */

#include "iolinki/phy_virtual.h"
#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

#if IOLINK_PHY_FEATURE_ISR_PUMP &&                                                       \
    ((IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_BYTE) || (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_BYTE))
#error "phy_virtual implements the ISR BYTE hooks only (FRAME mode is not supported)"
#endif

static int g_fd = -1;
static const char* g_port_path = NULL;

#if IOLINK_PHY_FEATURE_ISR_PUMP
/* Largest M-sequence the stack can transmit (see IOLINK_M_SEQ_MAX_LEN). */
#define VIRTUAL_TX_MAX (IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U)

static iolink_phy_mode_t g_mode = IOLINK_PHY_MODE_INACTIVE;
static uint8_t g_tx_buf[VIRTUAL_TX_MAX];
static uint8_t g_tx_len = 0U;
static uint8_t g_tx_active = 0U;
#endif

void iolink_phy_virtual_set_port(const char* port)
{
    g_port_path = port;
}

/** @brief Open and configure the backing serial port in raw, non-blocking mode. */
static int virtual_init(void* user)
{
    (void) user;

    if (g_port_path == NULL) {
        printf("[PHY-VIRTUAL] Error: Port not set\n");
        return -1;
    }

    g_fd = open(g_port_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_fd < 0) {
        printf("[PHY-VIRTUAL] Error opening %s: %s\n", g_port_path, strerror(errno));
        return -1;
    }

    /* Config for raw mode if it's a TTY */
    if (isatty(g_fd)) {
        struct termios tty;
        if (tcgetattr(g_fd, &tty) != 0) {
            printf("[PHY-VIRTUAL] Error from tcgetattr: %s\n", strerror(errno));
            return -1;
        }

        cfmakeraw(&tty);

        /* Set timeouts: No blocking */
        tty.c_cc[VMIN] = 0U;
        tty.c_cc[VTIME] = 0U;

        if (tcsetattr(g_fd, TCSANOW, &tty) != 0) {
            printf("[PHY-VIRTUAL] Error from tcsetattr: %s\n", strerror(errno));
            return -1;
        }
        printf("[PHY-VIRTUAL] Initialized TTY connection to %s (fd=%d)\n", g_port_path, g_fd);
    }
    else {
        printf("[PHY-VIRTUAL] Initialized non-TTY connection to %s (fd=%d)\n", g_port_path, g_fd);
    }
#if IOLINK_PHY_FEATURE_ISR_PUMP
    g_mode = IOLINK_PHY_MODE_INACTIVE;
    g_tx_len = 0U;
    g_tx_active = 0U;
#endif
    return 0;
}

/** @brief Log the requested PHY mode (no electrical effect on the virtual link). */
static void virtual_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* int_active() only claims service in SDCI, mirroring a real transceiver:
     * in SIO the wake-up path belongs to detect_wakeup() so the pump must not
     * consume the 0x55 marker as a data octet. */
    g_mode = mode;
#endif
    printf("[PHY-VIRTUAL] Mode set to: %d\n", (int) mode);
}

/** @brief Log the requested baudrate (no effect on the virtual link). */
static void virtual_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    printf("[PHY-VIRTUAL] Baudrate set to: %d\n", (int) baudrate);
}

/** @brief Write a buffer to the backing file descriptor. */
static int virtual_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;

    if ((g_fd < 0) || (data == NULL)) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    return (int) write(g_fd, data, len);
}

/** @brief Non-blocking read of a single byte from the backing file descriptor. */
static int virtual_recv_byte(void* user, uint8_t* byte)
{
    (void) user;

    if ((g_fd < 0) || (byte == NULL)) {
        return 0;
    }

    ssize_t n = read(g_fd, byte, 1);
    return (n > 0) ? 1 : 0;
}

#if IOLINK_PHY_FEATURE_ISR_PUMP
/** @brief Read one received octet for the ISR framing engine (non-blocking). */
static int virtual_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;

    if ((g_fd < 0) || (data == NULL) || (status == NULL)) {
        return 0;
    }

    ssize_t n = read(g_fd, data, 1);
    if (n == 1) {
        *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if ((n < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        return -1;
    }
    return 0;
}

/** @brief Buffer one reply octet; tx_done() flushes the whole frame. */
static int virtual_tx_octet(void* user, uint8_t data)
{
    (void) user;

    if ((size_t) g_tx_len >= sizeof(g_tx_buf)) {
        return -1;
    }
    g_tx_buf[g_tx_len] = data;
    g_tx_len++;
    g_tx_active = 1U;
    return 0;
}

/** @brief Flush the buffered reply to the PTY and release the line. */
static void virtual_tx_done(void* user)
{
    (void) user;

    if (g_fd >= 0) {
        size_t offset = 0U;
        while (offset < (size_t) g_tx_len) {
            ssize_t n = write(g_fd, &g_tx_buf[offset], (size_t) g_tx_len - offset);
            if (n <= 0) {
                break;
            }
            offset += (size_t) n;
        }
    }
    g_tx_len = 0U;
    g_tx_active = 0U;
}

/** @brief Data-interrupt level: TX in progress or an RX octet is available. */
static uint8_t virtual_int_active(void* user)
{
    (void) user;

    /* Only SDCI uses the framing pump; in SIO the wake-up byte is consumed by
     * detect_wakeup(), so claiming service here would desynchronise startup. */
    if (g_mode != IOLINK_PHY_MODE_SDCI) {
        return 0U;
    }
    if (g_tx_active != 0U) {
        return 1U;
    }
    if (g_fd < 0) {
        return 0U;
    }

    struct pollfd pfd;
    pfd.fd = g_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int ready = poll(&pfd, 1, 0);
    if (ready <= 0) {
        return 0U;
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return 0U;
    }
    return ((pfd.revents & POLLIN) != 0) ? 1U : 0U;
}

/** @brief Ask the ISR pump to run now (the host demo polls every cycle). */
static void virtual_request_service(void* user)
{
    (void) user;
}
#endif /* IOLINK_PHY_FEATURE_ISR_PUMP */

#if IOLINK_PHY_FEATURE_WAKEUP
/** @brief Scan buffered input for a 0x55 wake-up marker byte. */
static int virtual_detect_wakeup(void* user)
{
    (void) user;

    if (g_fd < 0) {
        return 0;
    }

    /* Try to read bytes until we find 0x55 or run out of data */
    uint8_t b;
    while (read(g_fd, &b, 1) > 0) {
        if (b == 0x55) {
            return 1;
        }
    }
    return 0;
}
#endif

static const iolink_phy_api_t g_phy_virtual = {.init = virtual_init,
                                               .set_mode = virtual_set_mode,
                                               .set_baudrate = virtual_set_baudrate,
                                               .send = virtual_send,
                                               .recv_byte = virtual_recv_byte,
#if IOLINK_PHY_FEATURE_ISR_PUMP
                                               .rx_octet = virtual_rx_octet,
                                               .tx_octet = virtual_tx_octet,
                                               .tx_done = virtual_tx_done,
                                               .int_active = virtual_int_active,
                                               .request_service = virtual_request_service,
#endif
#if IOLINK_PHY_FEATURE_WAKEUP
                                               .detect_wakeup = virtual_detect_wakeup,
#endif
};

const iolink_phy_api_t* iolink_phy_virtual_get(void)
{
    return &g_phy_virtual;
}
