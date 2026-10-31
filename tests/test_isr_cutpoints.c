/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isr_cutpoints.c
 * @brief Deterministic cut-point tests for the lock-free ISR handoffs.
 *
 * The host suite is single-threaded, so concurrency is modelled by injecting
 * the data-ISR publish at every step of the consumer:
 *
 *  - right after the consumer peeked (before it copied the record),
 *  - right after it copied (before it released the slot),
 *  - right after it released (before the next peek),
 *
 * plus an end-to-end case where a PHY callback publishes a record while the
 * main pump is inside iolink_dll_process(), i.e. on its way to the mailbox.
 *
 * The property under test is the SPSC ring contract: the producer never writes
 * the slot the consumer is reading, and a record's metadata travels with it.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/dll.h"
#include "iolinki/iolink.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

/** @brief Publish a frame record exactly as the data ISR does. */
static bool publish_frame(iolink_dll_ctx_t* ctx, uint8_t tag, uint8_t len)
{
    iolink_frame_rec_t* rec = (iolink_frame_rec_t*) iolink_ring_claim(&ctx->isr_frame_ring);
    if (rec == NULL) {
        ctx->isr_mailbox_drops++;
        return false;
    }
    rec->len = len;
    rec->eager = (uint8_t) (tag & 1U);
    for (uint8_t i = 0U; i < len; i++) {
        rec->data[i] = (uint8_t) (tag + i);
    }
    iolink_ring_publish(&ctx->isr_frame_ring);
    return true;
}

/** @brief Publish a staged ISDU write segment exactly as the data ISR does. */
static bool publish_od(iolink_dll_ctx_t* ctx, uint8_t flowctrl, uint8_t tag)
{
    iolink_od_rec_t* rec = (iolink_od_rec_t*) iolink_ring_claim(&ctx->isr_od_ring);
    if (rec == NULL) {
        ctx->isr_od_overruns++;
        return false;
    }
    rec->flowctrl = flowctrl;
    rec->len = 2U;
    rec->data[0] = tag;
    rec->data[1] = (uint8_t) (tag ^ 0xFFU);
    iolink_ring_publish(&ctx->isr_od_ring);
    return true;
}

static void reset_rings(iolink_dll_ctx_t* ctx)
{
    iolink_ring_init(&ctx->isr_frame_ring, (uint8_t*) ctx->isr_frame_slot, IOLINK_MBX_SLOTS,
                     (uint8_t) sizeof(iolink_frame_rec_t));
    iolink_ring_init(&ctx->isr_od_ring, (uint8_t*) ctx->isr_od_slot, IOLINK_OD_SLOTS,
                     (uint8_t) sizeof(iolink_od_rec_t));
}

/* The producer publishes between every pair of consumer steps; the consumed
 * record must stay intact and the queued one must arrive afterwards. */
static int test_frame_cutpoints(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));

    for (uint8_t cut = 0U; cut < 3U; cut++) {
        reset_rings(&ctx);

        assert(publish_frame(&ctx, 0x10U, 4U));

        const iolink_frame_rec_t* rec =
            (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
        assert(rec != NULL);

        if (cut == 0U) {
            assert(publish_frame(&ctx, 0x20U, 3U)); /* preempted after peek */
        }
        uint8_t copy[8];
        const uint8_t n = rec->len;
        const uint8_t eager = rec->eager;
        (void) memcpy(copy, rec->data, n);
        if (cut == 1U) {
            assert(publish_frame(&ctx, 0x20U, 3U)); /* preempted after copy */
        }
        iolink_ring_release(&ctx.isr_frame_ring);
        if (cut == 2U) {
            assert(publish_frame(&ctx, 0x20U, 3U)); /* preempted after release */
        }

        /* The consumed record was never touched by the producer. */
        assert(n == 4U);
        assert(eager == 0U);
        for (uint8_t i = 0U; i < n; i++) {
            assert(copy[i] == (uint8_t) (0x10U + i));
        }

        /* The second record is intact and next in line. */
        const iolink_frame_rec_t* next =
            (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
        assert(next != NULL);
        assert(next->len == 3U);
        assert(next->eager == 0U);
        for (uint8_t i = 0U; i < next->len; i++) {
            assert(next->data[i] == (uint8_t) (0x20U + i));
        }
        iolink_ring_release(&ctx.isr_frame_ring);
        assert(iolink_ring_is_empty(&ctx.isr_frame_ring));
    }
    assert(ctx.isr_mailbox_drops == 0U);
    return 0;
}

/* A full ring rejects the producer instead of overwriting a leased slot. */
static int test_frame_full_rejects(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    reset_rings(&ctx);

    assert(publish_frame(&ctx, 0x30U, 2U));
    assert(publish_frame(&ctx, 0x40U, 2U));
    assert(!publish_frame(&ctx, 0x50U, 2U));
    assert(ctx.isr_mailbox_drops == 1U);
    assert(iolink_ring_count(&ctx.isr_frame_ring) == IOLINK_MBX_SLOTS);

    /* The two queued records are unchanged. */
    const iolink_frame_rec_t* first =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
    assert(first != NULL);
    assert(first->data[0] == 0x30U);
    iolink_ring_release(&ctx.isr_frame_ring);
    const iolink_frame_rec_t* second =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
    assert(second != NULL);
    assert(second->data[0] == 0x40U);
    iolink_ring_release(&ctx.isr_frame_ring);
    return 0;
}

/* The same cut-point property for the staged ISDU write ring. */
static int test_od_cutpoints(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    reset_rings(&ctx);

    assert(publish_od(&ctx, IOLINK_FLOWCTRL_START, 0x61U));

    const iolink_od_rec_t* rec = (const iolink_od_rec_t*) iolink_ring_peek(&ctx.isr_od_ring);
    assert(rec != NULL);
    assert(publish_od(&ctx, 1U, 0x71U)); /* preempted after peek */
    uint8_t fc = rec->flowctrl;
    uint8_t len = rec->len;
    uint8_t od[IOLINK_OD_MAX_SIZE];
    (void) memcpy(od, rec->data, len);
    iolink_ring_release(&ctx.isr_od_ring);

    assert(fc == IOLINK_FLOWCTRL_START);
    assert(len == 2U);
    assert(od[0] == 0x61U);
    assert(od[1] == (uint8_t) (0x61U ^ 0xFFU));

    const iolink_od_rec_t* next = (const iolink_od_rec_t*) iolink_ring_peek(&ctx.isr_od_ring);
    assert(next != NULL);
    assert(next->flowctrl == 1U);
    assert(next->data[0] == 0x71U);
    iolink_ring_release(&ctx.isr_od_ring);
    assert(ctx.isr_od_overruns == 0U);
    return 0;
}

#if IOLINK_PHY_FEATURE_LEGACY_DIAG
/* The PHY callback that publishes the second frame while the main pump is
 * inside iolink_dll_process(), before it consumes the mailbox. */
static uint8_t g_publish_on_poll;
static iolink_dll_ctx_t* g_poll_ctx;

static int poll_voltage_publish(void* user)
{
    (void) user;
    if (g_publish_on_poll != 0U) {
        g_publish_on_poll = 0U;
        assert(publish_frame(g_poll_ctx, 0x90U, 2U));
    }
    return -1; /* measurement unavailable: no fault, no event */
}

/* End-to-end: the real consumer (dll_consume_isr_mailbox) drains one record
 * per pass while a producer publishes mid-pass. */
static int test_main_pass_cutpoint(void)
{
    iolink_dll_ctx_t ctx;
    const iolink_phy_api_t phy = {
        .user = NULL,
        .get_voltage_mv = poll_voltage_publish,
    };

    iolink_dll_init(&ctx, &phy);
    ctx.time_us = test_time_us;
    ctx.state = IOLINK_DLL_STATE_PREOPERATE;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    ctx.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    now_us = 1000000U;

    reset_rings(&ctx);
    assert(publish_frame(&ctx, 0xA0U, 2U));

    g_poll_ctx = &ctx;
    g_publish_on_poll = 1U;
    iolink_dll_process(&ctx); /* polls diagnostics -> publishes B -> consumes A */

    assert(iolink_ring_count(&ctx.isr_frame_ring) == 1U);
    assert(ctx.isr_mailbox_drops == 0U);
    const iolink_frame_rec_t* pending =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
    assert(pending != NULL);
    assert(pending->len == 2U);
    assert(pending->data[0] == 0x90U);
    assert(pending->data[1] == 0x91U);

    iolink_dll_process(&ctx); /* consumes B */
    assert(iolink_ring_is_empty(&ctx.isr_frame_ring));
    return 0;
}
#endif /* legacy diagnostics */

int main(void)
{
    int rc = test_frame_cutpoints();
    if (rc != 0) {
        return rc;
    }
    rc = test_frame_full_rejects();
    if (rc != 0) {
        return rc;
    }
    rc = test_od_cutpoints();
    if (rc != 0) {
        return rc;
    }
#if IOLINK_PHY_FEATURE_LEGACY_DIAG
    rc = test_main_pass_cutpoint();
    if (rc != 0) {
        return rc;
    }
#endif
    return 0;
}
