/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file fuzz_frame.c
 * @brief libFuzzer target: the received-octet path.
 *
 * The input is the byte stream a PHY would deliver. A fake transceiver feeds
 * it to the real ISR and main pumps, so M-sequence length checks, checksum
 * validation, state transitions and reply construction all run on it.
 *
 * What the target asserts (in addition to the sanitizers):
 *  - a reply never exceeds the largest legal M-sequence (`tx_octet` aborts);
 *  - the DLL state stays inside its enumeration;
 *  - the device accepts the build's own reference configuration.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/device_info.h"
#include "iolinki/iolink.h"
#include "iolinki/phy.h"
#include "iolinki/port.h"

#define FUZZ_MAX_CYCLES 48U

/* The stack refuses to start while the weak no-op critical section is the only
 * implementation (it cannot tell "not ported" from "single-threaded by
 * design"). This harness is single-threaded, so it supplies the real ones. */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

typedef struct
{
    const uint8_t* data;
    size_t len;
    size_t pos;
    bool tx_active;
    uint8_t reply[IOLINK_M_SEQ_MAX_LEN];
    size_t reply_len;
} fuzz_phy_state_t;

static fuzz_phy_state_t g_phy;
static uint64_t g_now_us;
static uint32_t g_wakeup_pulses;
static iolink_device_ctx_t g_device;

static uint64_t fuzz_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return g_now_us;
}

static int fuzz_phy_init(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
}

static void fuzz_phy_set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}

static int fuzz_phy_rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
    if (g_phy.pos >= g_phy.len) {
        return 0;
    }
    *data = g_phy.data[g_phy.pos++];
    *status = 0U;
    return 1;
}

static int fuzz_phy_tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
{
    (void) self;
    if (!g_phy.tx_active) {
        /* The transceiver was idle: this octet starts a new reply, so the
         * buffer holds one reply, not every reply of the whole input. */
        g_phy.reply_len = 0U;
    }
    if (g_phy.reply_len >= sizeof(g_phy.reply)) {
        /* A reply longer than the largest legal M-sequence is a framing bug,
         * not a fuzz input; make it visible instead of truncating it. */
        abort();
    }
    g_phy.reply[g_phy.reply_len++] = data;
    g_phy.tx_active = true;
    return 0;
}

static void fuzz_phy_tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    g_phy.tx_active = false;
}

static uint8_t fuzz_phy_int_active(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    /* The transceiver reports service while it still owes a reply: the ISR
     * pump only drives `tx_step` while this is non-zero, so a reply in
     * progress has to keep the line active after the last RX octet. */
    return ((g_phy.pos < g_phy.len) || g_phy.tx_active) ? 1U : 0U;
}

static int fuzz_phy_detect_wakeup(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (g_wakeup_pulses > 0U) {
        g_wakeup_pulses--;
        return 1;
    }
    return 0;
}

static const iolink_phy_api_t k_phy = {
    .self = NULL,
    .init = fuzz_phy_init,
    .set_mode = fuzz_phy_set_mode,
    .rx_octet = fuzz_phy_rx_octet,
    .tx_octet = fuzz_phy_tx_octet,
    .tx_done = fuzz_phy_tx_done,
    .int_active = fuzz_phy_int_active,
    .detect_wakeup = fuzz_phy_detect_wakeup,
};

static const iolink_device_info_t k_identity = {
    .vendor_name = "fuzz",
    .vendor_text = "fuzz",
    .product_name = "fuzz",
    .product_id = "fuzz",
    .product_text = "fuzz",
    .serial_number = "fuzz",
    .hardware_revision = "0.1",
    .firmware_revision = "0.1",
    .vendor_id = 0x0001U,
    .device_id = 0x00000001U,
    .min_cycle_time = 0U,
    .revision_id = IOLINK_REVISION_ID_1_1,
};

static const iolink_device_app_t k_app = {.self = NULL, .time_us = fuzz_time_us};

/* Type 0 with no process data: the smallest configuration the stack accepts,
   so the fuzzer spends the input on framing rather than on PD size. */
static const iolink_stack_config_t k_stack = {
    .m_seq_type = IOLINK_M_SEQ_TYPE_0,
    .min_cycle_time = 0U,
    .pd_in_len = 0U,
    .pd_out_len = 0U,
    .od_width = 1U,
};

static const iolink_device_config_t k_config = {
    .phy = &k_phy,
    .stack = &k_stack,
    .app = &k_app,
    .device_info = &k_identity,
};

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0U) {
        return 0;
    }

    (void) memset(&g_phy, 0, sizeof(g_phy));
    g_phy.data = data;
    g_phy.len = (size > (size_t) IOLINK_M_SEQ_MAX_LEN) ? (size_t) IOLINK_M_SEQ_MAX_LEN : size;
    g_now_us = 0U;
    g_wakeup_pulses = 1U;

    if (iolink_device_init(&g_device, &k_config) != IOLINK_DEVICE_INIT_OK) {
        /* The configuration is constant and valid; a rejection means the
         * harness or the init contract changed, which must not pass silently. */
        abort();
    }

    for (uint32_t i = 0U; i < FUZZ_MAX_CYCLES; i++) {
        iolink_device_process_isr(&g_device);
        iolink_device_process(&g_device);
        g_now_us += 250U;
        if ((g_phy.pos >= g_phy.len) && !g_phy.tx_active && (i >= 8U)) {
            break;
        }
    }

    if ((uint32_t) iolink_device_get_state(&g_device) >= (uint32_t) IOLINK_DLL_STATE_COUNT) {
        abort();
    }
    return 0;
}
