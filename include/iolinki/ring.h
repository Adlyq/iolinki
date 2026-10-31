/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_RING_H
#define IOLINK_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "iolinki/port.h"

/**
 * @file ring.h
 * @brief Single-producer / single-consumer slot ring used by the ISR handoffs.
 *
 * The stack moves records (received frames, staged ISDU write segments) from
 * the data ISR to the main pump without a critical section. The pattern is the
 * Linux kfifo contract with fixed-size slots:
 *
 * - `in` is written only by the producer, `out` only by the consumer;
 * - the producer fills a claimed slot and then publishes `in` (release);
 * - the consumer reads the slot at `out` and only then advances `out`
 *   (acquire);
 * - the indices are monotonic and wrap with unsigned arithmetic, so
 *   `in - out` is the number of records with no ambiguity;
 * - the ring never queues more than `slots` records and the producer is
 *   rejected (returns NULL) when full, so it never overwrites a slot the
 *   consumer is still reading.
 *
 * `slots` must be a power of two and at least 2 for the queueing variant.
 * The descriptor is a POD: it points at caller-owned static storage, so no
 * dynamic memory is used.
 *
 * @note This header is part of the DLL transport contract, not the application
 * API: application code calls iolink_device_* / iolink_dll_* only.
 */

/**
 * @defgroup iolinki_ring SPSC slot ring
 * @brief Lock-free record handoff between the data ISR and the main pump.
 * @{
 */

/** @brief Ring descriptor (producer/consumer indices plus the slot storage). */
typedef struct
{
    uint8_t* data;                     /**< Caller-owned storage of `slots * slot_size` bytes */
    uint8_t slots;                     /**< Slot count, power of two */
    uint8_t slot_size;                 /**< Bytes per slot */
    volatile IOLINK_ATOMIC_UINT_T in;  /**< Written by the producer only */
    volatile IOLINK_ATOMIC_UINT_T out; /**< Written by the consumer only */
} iolink_ring_t;

/** @brief Bind a ring to its storage and reset both indices. */
static inline void iolink_ring_init(iolink_ring_t* ring, uint8_t* data, uint8_t slots,
                                    uint8_t slot_size)
{
    ring->data = data;
    ring->slots = slots;
    ring->slot_size = slot_size;
    ring->in = 0U;
    ring->out = 0U;
}

/** @brief Number of published, not yet released records. */
static inline uint32_t iolink_ring_count(const iolink_ring_t* ring)
{
    return (uint32_t) (IOLINK_ATOMIC_UINT_T) (ring->in - ring->out);
}

/** @brief True when the ring holds no record. */
static inline bool iolink_ring_is_empty(const iolink_ring_t* ring)
{
    return ring->in == ring->out;
}

/**
 * @brief Reserve the next slot for the producer.
 *
 * @return Writable slot, or NULL when the ring is full. Every successful claim
 *         must be followed by exactly one iolink_ring_publish() before the
 *         next claim.
 */
static inline uint8_t* iolink_ring_claim(iolink_ring_t* ring)
{
    const IOLINK_ATOMIC_UINT_T in = ring->in;
    const IOLINK_ATOMIC_UINT_T out = iolink_atomic_load(&ring->out);
    if ((uint32_t) (IOLINK_ATOMIC_UINT_T) (in - out) >= (uint32_t) ring->slots) {
        return NULL; /* full: never overwrite a slot the consumer may be reading */
    }
    return ring->data +
           ((uint32_t) (in & (IOLINK_ATOMIC_UINT_T) (ring->slots - 1U)) * ring->slot_size);
}

/** @brief Publish the claimed slot (release: data-first, index last). */
static inline void iolink_ring_publish(iolink_ring_t* ring)
{
    iolink_atomic_store(&ring->in, (IOLINK_ATOMIC_UINT_T) (ring->in + 1U));
}

/**
 * @brief Look at the oldest unreleased record without consuming it.
 *
 * @return Read-only slot, or NULL when the ring is empty. The caller must call
 *         iolink_ring_release() once it is done with the record.
 */
static inline const uint8_t* iolink_ring_peek(const iolink_ring_t* ring)
{
    const IOLINK_ATOMIC_UINT_T out = ring->out;
    const IOLINK_ATOMIC_UINT_T in = iolink_atomic_load(&ring->in);
    if (in == out) {
        return NULL;
    }
    return ring->data +
           ((uint32_t) (out & (IOLINK_ATOMIC_UINT_T) (ring->slots - 1U)) * ring->slot_size);
}

/** @brief Release the record returned by the last iolink_ring_peek(). */
static inline void iolink_ring_release(iolink_ring_t* ring)
{
    iolink_atomic_store(&ring->out, (IOLINK_ATOMIC_UINT_T) (ring->out + 1U));
}

/** @} */ /* end of iolinki_ring */

#endif  // IOLINK_RING_H
