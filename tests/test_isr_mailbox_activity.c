/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Regression test: the ISR mailbox path must refresh the activity timestamps.
 *
 * Previously the mailbox branch consumed a published frame without updating
 * last_activity_ms/last_byte_us, so the 1 s inactivity supervision reset a
 * healthy ESTAB_COM link to STARTUP on every timeout tick.
 *
 * ISR two-pump mailbox-activity checks.
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/protocol.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

/** @brief Publish one request into the mailbox ring (the ISR does this). */
static void push_mailbox(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    iolink_frame_rec_t* rec = (iolink_frame_rec_t*) iolink_ring_claim(&ctx->isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    rec->len = len;
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, len);
    iolink_ring_publish(&ctx->isr_frame_ring);
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
    const iolink_phy_api_t phy = {0};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx, &phy);
    ctx.time_us = test_time_us;

    ctx.state = IOLINK_DLL_STATE_ESTAB_COM;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    now_us = 1000000U;
    ctx.last_activity_ms = ((uint32_t) (now_us / 1000U));

    uint8_t frame[2];
    make_type0_frame(0x80U, frame); /* Type-0 READ */

    for (unsigned i = 0U; i < 3U; i++) {
        /* 600 ms between served frames; >1 s accumulates over the loop. */
        now_us += 600000U;
        push_mailbox(&ctx, frame, (uint8_t) sizeof(frame));
        iolink_dll_process(&ctx);

        /* The mailbox frame is live traffic: it must not trip the idle timer.
           Explicit checks keep the test effective when NDEBUG is set. */
        if ((ctx.timeout_errors != 0U) || (ctx.state == IOLINK_DLL_STATE_STARTUP)) {
            return 1;
        }
    }

    return 0;
}
