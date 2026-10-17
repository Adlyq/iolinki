/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Long-horizon stability test for the ISR two-pump transport.
 *
 * The mock clock accelerates N simulated seconds and a 32-bit millisecond
 * timestamp rollover into milliseconds of wall time. It checks that:
 *  - continuous mailbox traffic never trips the 1 s inactivity supervision;
 *  - valid frames are never rejected (crc/framing/timeout stay 0);
 *  - every consumed frame starts exactly one reply;
 *  - the millisecond timestamp wrap does not reset a healthy link.
 *
 * Only built in the IOLINK_ISR_PUMP configuration.
 */

#include <stdint.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/protocol.h"

static uint64_t now_us;
static unsigned tx_octets;
static unsigned tx_frames;

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (now_us / 1000U);
}

uint64_t iolink_time_get_us(void)
{
    return now_us;
}

static int rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    (void) data;
    (void) status;
    return 0; /* The test publishes into the mailbox directly. */
}

static int tx_octet(void* user, uint8_t data)
{
    (void) user;
    (void) data;
    tx_octets++;
    return 0;
}

static void tx_done(void* user)
{
    (void) user;
    tx_frames++;
}

static uint8_t int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((ctx != NULL) && (ctx->isr_tx_active != 0U)) ? 1U : 0U;
}

/* A valid Type-0 request: MC + CKT carrying the A.1.6 checksum. */
static void make_type0_frame(uint8_t mc, uint8_t frame[2])
{
    frame[0] = mc;
    frame[1] = 0x00U; /* Type 0; the checksum occupies bits 0-5. */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2U));
}

/* Feed one mailbox frame per cycle, drain the reply, verify the invariants.
 * Returns 0 on success, a non-zero test-failure code otherwise. */
static int run_cycles(iolink_dll_ctx_t* ctx, unsigned cycles, uint64_t step_us)
{
    uint8_t frame[2];
    make_type0_frame(0x80U, frame); /* Type-0 READ: two-octet reply. */

    for (unsigned i = 0U; i < cycles; i++) {
        now_us += step_us;
        ctx->isr_frame_ptr = frame;
        ctx->isr_frame_len = (uint8_t) sizeof(frame);
        ctx->isr_frame_ready = 1U;

        iolink_dll_process(ctx);
        while (ctx->isr_tx_active != 0U) {
            iolink_dll_process_isr(ctx);
        }

        if (ctx->isr_frame_ready != 0U) {
            return 1; /* mailbox not consumed */
        }
        if (ctx->timeout_errors != 0U) {
            return 2; /* idle supervision tripped */
        }
        if (ctx->crc_errors != 0U) {
            return 3;
        }
        if (ctx->framing_errors != 0U) {
            return 4;
        }
        if (ctx->state == IOLINK_DLL_STATE_STARTUP) {
            return 5;
        }
    }
    return 0;
}

int main(void)
{
    iolink_dll_ctx_t ctx;
    const iolink_phy_api_t phy = {
        .user = &ctx,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
    };

    iolink_dll_init(&ctx, &phy);
    ctx.state = IOLINK_DLL_STATE_ESTAB_COM;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    now_us = 1000000U;
    ctx.last_activity_ms = iolink_time_get_ms();

    /* Scenario A: 60 simulated seconds at 1 ms per cycle (60k frames). */
    if (run_cycles(&ctx, 60000U, 1000U) != 0) {
        return 10;
    }
    if (ctx.state != IOLINK_DLL_STATE_OPERATE) {
        return 11;
    }
    if (tx_frames != 60000U) {
        return 12; /* one reply per consumed frame */
    }
    if (tx_octets != 2U * 60000U) {
        return 13; /* Type-0 READ reply is OD + CKS */
    }

    /* Scenario B: cross the 32-bit millisecond timestamp wrap. */
    now_us = ((uint64_t) UINT32_MAX - 5U) * 1000U;
    ctx.last_activity_ms = iolink_time_get_ms();
    ctx.timeout_errors = 0U;
    if (run_cycles(&ctx, 20U, 600000U) != 0) {
        return 20;
    }
    if (ctx.state != IOLINK_DLL_STATE_OPERATE) {
        return 21;
    }
    if (ctx.timeout_errors != 0U) {
        return 22;
    }

    return 0;
}
