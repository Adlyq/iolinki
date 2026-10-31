/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ring.c
 * @brief Unit tests for the SPSC slot ring used by the ISR handoffs.
 */

#include <assert.h>
#include <string.h>

#include "iolinki/ring.h"

#define TEST_SLOTS 2U
#define TEST_SLOT_SIZE 4U

static uint8_t g_storage[TEST_SLOTS * TEST_SLOT_SIZE];

/** @brief Claim, fill and publish one record; returns the slot (NULL if full). */
static uint8_t* push(iolink_ring_t* ring, uint8_t value)
{
    uint8_t* s = iolink_ring_claim(ring);
    if (s != NULL) {
        memset(s, value, TEST_SLOT_SIZE);
        s[0] = value;
        iolink_ring_publish(ring);
    }
    return s;
}

int main(void)
{
    iolink_ring_t ring;
    iolink_ring_init(&ring, g_storage, TEST_SLOTS, TEST_SLOT_SIZE);

    /* Empty ring: no record to peek. */
    assert(iolink_ring_is_empty(&ring));
    assert(iolink_ring_count(&ring) == 0U);
    assert(iolink_ring_peek(&ring) == NULL);

    /* One record round trip: peek does not consume, release does. */
    assert(push(&ring, 0xA1U) != NULL);
    assert(iolink_ring_count(&ring) == 1U);
    assert(!iolink_ring_is_empty(&ring));

    const uint8_t* rec = iolink_ring_peek(&ring);
    assert(rec != NULL);
    assert(rec[0] == 0xA1U);
    assert(rec[1] == 0xA1U);
    assert(iolink_ring_count(&ring) == 1U);
    iolink_ring_release(&ring);
    assert(iolink_ring_is_empty(&ring));

    /* Fill to capacity, then reject the producer (never overwrite a slot the
     * consumer may still be reading). */
    assert(push(&ring, 0x01U) != NULL);
    assert(push(&ring, 0x02U) != NULL);
    assert(iolink_ring_count(&ring) == TEST_SLOTS);
    assert(iolink_ring_claim(&ring) == NULL);

    /* FIFO: the oldest record comes out first. */
    const uint8_t* first = iolink_ring_peek(&ring);
    assert(first != NULL);
    assert(first[0] == 0x01U);
    iolink_ring_release(&ring);

    /* A freed slot can be published again while the other record waits. */
    assert(push(&ring, 0x03U) != NULL);
    assert(iolink_ring_count(&ring) == TEST_SLOTS);

    const uint8_t* second = iolink_ring_peek(&ring);
    assert(second != NULL);
    assert(second[0] == 0x02U);
    iolink_ring_release(&ring);

    const uint8_t* third = iolink_ring_peek(&ring);
    assert(third != NULL);
    assert(third[0] == 0x03U);
    iolink_ring_release(&ring);
    assert(iolink_ring_is_empty(&ring));

    /* The slot index wraps across many records; order is preserved. */
    for (uint8_t i = 0U; i < 10U; i++) {
        assert(push(&ring, i) != NULL);
        const uint8_t* r = iolink_ring_peek(&ring);
        assert(r != NULL);
        assert(r[0] == i);
        assert(r[2] == i);
        iolink_ring_release(&ring);
        assert(iolink_ring_is_empty(&ring));
    }

    return 0;
}
