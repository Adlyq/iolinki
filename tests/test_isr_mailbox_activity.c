/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Regression test: the ISR mailbox path must refresh the activity timestamps.
 *
 * Previously the mailbox branch consumed a published frame without updating
 * last_activity_ms/last_byte_us, so the 1 s inactivity supervision reset a
 * healthy ESTAB_COM link to STARTUP on every timeout tick.
 *
 * Only built in the IOLINK_ISR_PUMP configuration.
 */

#include <stdint.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
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

static int receive(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}

/* A valid Type-0 request: MC + CKT carrying the A.1.6 checksum. */
static void make_type0_frame(uint8_t mc, uint8_t frame[2])
{
    frame[0] = mc;
    frame[1] = 0x00U; /* Type 0; the checksum occupies bits 0-5. */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2U));
}

int main(void)
{
    const iolink_phy_api_t phy = {.recv_byte = receive};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx, &phy);

    ctx.state = IOLINK_DLL_STATE_ESTAB_COM;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    now_us = 1000000U;
    ctx.last_activity_ms = iolink_time_get_ms();

    uint8_t frame[2];
    make_type0_frame(0x80U, frame); /* Type-0 READ */

    for (unsigned i = 0U; i < 3U; i++) {
        /* 600 ms between served frames; >1 s accumulates over the loop. */
        now_us += 600000U;
        ctx.isr_frame_ptr = frame;
        ctx.isr_frame_len = (uint8_t) sizeof(frame);
        ctx.isr_frame_ready = 1U;
        iolink_dll_process(&ctx);

        /* The mailbox frame is live traffic: it must not trip the idle timer.
           Explicit checks keep the test effective when NDEBUG is set. */
        if ((ctx.timeout_errors != 0U) || (ctx.state == IOLINK_DLL_STATE_STARTUP)) {
            return 1;
        }
    }

    return 0;
}
