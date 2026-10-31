/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_port_capabilities.c
 * @brief Checks the graded concurrency-primitive contract of iolinki/port.h.
 *
 * The executable is built under every IOLINK_CONCURRENCY profile, so it proves
 * that the derivation, the portable barrier and the publish-word helpers
 * compile and link with the minimum primitive set each profile requires.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/port.h"

static volatile IOLINK_ATOMIC_UINT_T g_word;

/** Publish word followed by data, the layout the stack uses. */
typedef struct
{
    uint8_t leading;
    IOLINK_ATOMIC_UINT_T word;
} publish_probe_t;

#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
IOLINK_STATIC_ASSERT(offsetof(publish_probe_t, word) % sizeof(IOLINK_ATOMIC_UINT_T) == 0,
                     "the publish word must be naturally aligned");
#endif

int main(void)
{
    /* The profile must be one of the documented values. */
    assert(IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_SINGLE);
    assert(IOLINK_CONCURRENCY <= IOLINK_CONCURRENCY_SMP);

    /* The requirement flags stay boolean. */
    assert((IOLINK_NEED_FENCE == 0) || (IOLINK_NEED_FENCE == 1));
    assert((IOLINK_NEED_CRITICAL == 0) || (IOLINK_NEED_CRITICAL == 1));
    assert((IOLINK_NEED_THREAD_LOCK == 0) || (IOLINK_NEED_THREAD_LOCK == 1));

    /* Every asynchronous profile needs a barrier; SINGLE collapses it. */
#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
    assert(IOLINK_NEED_FENCE != 0);
    assert(IOLINK_NEED_CRITICAL != 0);
#else
    assert(IOLINK_NEED_FENCE == 0);
    assert(IOLINK_NEED_CRITICAL == 0);
#endif

#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_RTOS
    assert(IOLINK_NEED_THREAD_LOCK != 0);
#else
    assert(IOLINK_NEED_THREAD_LOCK == 0);
#endif

    /* The portable barrier and the mutex wrappers compile in every profile. */
    IOLINK_FENCE();
    IOLINK_CRITICAL_ENTER();
    IOLINK_CRITICAL_EXIT();

    /* Publish-word round trip (each context is the single writer of its word). */
    iolink_atomic_store(&g_word, (IOLINK_ATOMIC_UINT_T) 0x5AU);
    assert(iolink_atomic_load(&g_word) == (IOLINK_ATOMIC_UINT_T) 0x5AU);
    iolink_atomic_store(&g_word, (IOLINK_ATOMIC_UINT_T) 0U);
    assert(iolink_atomic_load(&g_word) == (IOLINK_ATOMIC_UINT_T) 0U);

    return 0;
}
