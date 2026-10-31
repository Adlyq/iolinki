/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PORT_H
#define IOLINK_PORT_H

#include <stdint.h>
#include <stddef.h>

#include "iolinki/config.h"

/**
 * @file port.h
 * @brief Declarative concurrency-primitive contract between the stack and a port.
 *
 * The stack only needs four kinds of asynchronous primitives. Each one is
 * graded by strength, so a port implements exactly the grade the selected
 * concurrency profile requires and nothing more:
 *
 * - **Atomic width** (`IOLINK_ATOMIC_UINT_T`, default `uint32_t`): a naturally
 *   aligned value is read and written in one access (single-copy atomic).
 *   No read-modify-write is assumed.
 * - **Barrier** (`IOLINK_FENCE()`): orders data writes before a publish-word
 *   store, and a publish-word load before the data it publishes.
 * - **Mutual exclusion** (`IOLINK_CRITICAL_ENTER/EXIT()`): excludes the data
 *   interrupt, nesting-safe, callable from the ISR, bounded duration.
 * - **Thread lock** (`iolink_device_config_t::lock/unlock`): serialises
 *   main-context threads only; never taken from the ISR.
 *
 * The concurrency profile (`IOLINK_CONCURRENCY`) selects which primitives are
 * required; the library derives `IOLINK_NEED_*` from it and a missing
 * primitive is a compile error (or, for the critical section, an
 * `IOLINK_DEVICE_INIT_ERR_CRITICAL` at device initialization). A configuration
 * that needs no critical section does not reference the critical-section
 * symbols at all, so a bare-metal port can omit them.
 *
 * The stack never uses read-modify-write atomics (compare-and-swap, atomics
 * increment) on shared state, because several supported cores (ARMv6-M,
 * ARMv8-M Baseline, RV32I) have no atomic RMW instruction. Shared state is
 * single-writer with a publish word instead.
 *
 * See docs/PORTING.md for the full contract and per-platform mappings.
 */

/**
 * @defgroup iolinki_port Concurrency primitives
 * @brief Graded asynchronous primitives a port provides on demand.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Mutual exclusion
 *
 * Required only when `IOLINK_NEED_CRITICAL` is non-zero. The implementation
 * must be nesting-safe and callable from interrupt context; it should map to a
 * PRIMASK/BASEPRI save/restore (Cortex-M), an IRQ lock (RTOS) or an equivalent.
 * @{
 */
void iolink_critical_enter(void);
void iolink_critical_exit(void);

/**
 * @brief Probe whether the platform supplied real critical-section hooks.
 *
 * Calls iolink_critical_enter()/iolink_critical_exit() once and reports whether
 * the weak no-op defaults from src/platform.c were used. iolink_device_init()
 * consults this only when `IOLINK_NEED_CRITICAL` is non-zero.
 *
 * @return 0 when both hooks are overridden, -1 when the weak default ran.
 */
int iolink_platform_critical_sections_ready(void);
/** @} */

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------
 * Capability declaration (a port overrides these before including this file,
 * through the build system or IOLINK_PORT_CONFIG_FILE)
 * ------------------------------------------------------------------------- */

/** @brief Optional port configuration header, included before the defaults. */
#ifdef IOLINK_PORT_CONFIG_FILE
#include IOLINK_PORT_CONFIG_FILE
#endif

/**
 * @brief Naturally atomic unsigned integer type (single-copy atomic when aligned).
 *
 * Defaults to `uint32_t`. An 8/16-bit target overrides it with the widest
 * naturally atomic unsigned type. The ISR concurrency profile requires at
 * least 32 bits; a narrower target keeps the `SINGLE` profile.
 */
#ifndef IOLINK_ATOMIC_UINT_T
#define IOLINK_ATOMIC_UINT_T uint32_t
#endif

/** @brief Barrier grades: compiler only, device/DMA visible, SMP/system wide. */
#define IOLINK_FENCE_COMPILER 0
#define IOLINK_FENCE_DEVICE 1
#define IOLINK_FENCE_SMP 2

/**
 * @brief Barrier grade declared by the port.
 *
 * `COMPILER` is the portable default: enough when the two contexts are the
 * main loop and an ISR on one core, with no other bus master reading the
 * published data. A port whose PHY/DMA reads a published buffer while the CPU
 * writes it declares `DEVICE` and provides `IOLINK_FENCE_DEVICE_IMPL()`. A
 * port whose contexts are two cores (or two threads) declares `SMP` and
 * provides `IOLINK_FENCE_SMP_IMPL()`.
 */
#ifndef IOLINK_FENCE_LEVEL
#define IOLINK_FENCE_LEVEL IOLINK_FENCE_COMPILER
#endif

/* -------------------------------------------------------------------------
 * Requirement derivation and validation
 * ------------------------------------------------------------------------- */

/* The IOLINK_CONCURRENCY_SINGLE/_ISR/_RTOS/_SMP values come from config.h. */
#if (IOLINK_CONCURRENCY < IOLINK_CONCURRENCY_SINGLE) || \
    (IOLINK_CONCURRENCY > IOLINK_CONCURRENCY_SMP)
#error "IOLINK_CONCURRENCY must be IOLINK_CONCURRENCY_SINGLE/ISR/RTOS/SMP"
#endif

#if (IOLINK_STATS_MODE != IOLINK_STATS_SHARED) && (IOLINK_STATS_MODE != IOLINK_STATS_SPLIT)
#error "IOLINK_STATS_MODE must be IOLINK_STATS_SHARED or IOLINK_STATS_SPLIT"
#endif

/** @brief Non-zero when a barrier is required between contexts. */
#define IOLINK_NEED_FENCE (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR)

/**
 * @brief Non-zero when the stack calls the platform critical section.
 *
 * The bulk handoffs are lock-free (frame mailbox and staged-write rings, the
 * ISDU response publish word, the PD-in buffer lease and the reply staging
 * slots). The platform critical section still protects the deferred state
 * queue, the event engine, the PD-in index swap, the reply staging copy and
 * the pending length pair, so every asynchronous profile needs it; only
 * `SINGLE` runs without it.
 *
 * `IOLINK_STATS_MODE=SPLIT` removes the one remaining counter race (both
 * contexts incrementing the same field) but does not change this requirement.
 */
#define IOLINK_NEED_CRITICAL (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR)

/** @brief Non-zero when the application-supplied thread lock is required. */
#define IOLINK_NEED_THREAD_LOCK (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_RTOS)

#if (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_SMP) && (IOLINK_FENCE_LEVEL < IOLINK_FENCE_SMP)
#error "IOLINK_CONCURRENCY=SMP requires IOLINK_FENCE_LEVEL=IOLINK_FENCE_SMP"
#endif

#if (IOLINK_FENCE_LEVEL < IOLINK_FENCE_COMPILER) || (IOLINK_FENCE_LEVEL > IOLINK_FENCE_SMP)
#error "IOLINK_FENCE_LEVEL must be COMPILER, DEVICE or SMP"
#endif

/* -------------------------------------------------------------------------
 * Barrier
 * ------------------------------------------------------------------------- */

/** @brief Stop the compiler from moving memory operations across this point. */
#if defined(__GNUC__) || defined(__clang__)
#define IOLINK_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define IOLINK_COMPILER_BARRIER() ((void) 0)
#endif

#if IOLINK_NEED_FENCE
#if (IOLINK_FENCE_LEVEL == IOLINK_FENCE_COMPILER)
#define IOLINK_FENCE() IOLINK_COMPILER_BARRIER()
#elif (IOLINK_FENCE_LEVEL == IOLINK_FENCE_DEVICE)
#ifndef IOLINK_FENCE_DEVICE_IMPL
#error "IOLINK_FENCE_LEVEL=DEVICE requires the port to define IOLINK_FENCE_DEVICE_IMPL()"
#endif
#define IOLINK_FENCE() IOLINK_FENCE_DEVICE_IMPL()
#else
#ifndef IOLINK_FENCE_SMP_IMPL
#error "IOLINK_FENCE_LEVEL=SMP requires the port to define IOLINK_FENCE_SMP_IMPL()"
#endif
#define IOLINK_FENCE() IOLINK_FENCE_SMP_IMPL()
#endif
#else
#define IOLINK_FENCE() ((void) 0)
#endif

/* -------------------------------------------------------------------------
 * Publish words
 * ------------------------------------------------------------------------- */

/**
 * @brief Read a publish word with acquire ordering.
 *
 * The word must be naturally aligned (a `IOLINK_ATOMIC_UINT_T` member is, when
 * the structure places it at a multiple of its size). Each context is the
 * single writer of the words it publishes; readers never write them, so a
 * plain load plus the configured barrier is enough.
 */
static inline IOLINK_ATOMIC_UINT_T iolink_atomic_load(const volatile IOLINK_ATOMIC_UINT_T* word)
{
    const IOLINK_ATOMIC_UINT_T value = *word;
    IOLINK_FENCE();
    return value;
}

/**
 * @brief Write a publish word with release ordering.
 *
 * All data the word refers to must already be written before this call; the
 * barrier keeps the compiler from sinking those writes past the publish.
 */
static inline void iolink_atomic_store(volatile IOLINK_ATOMIC_UINT_T* word,
                                       IOLINK_ATOMIC_UINT_T value)
{
    IOLINK_FENCE();
    *word = value;
}

/**
 * @brief Serialise against the data interrupt (no-op without a critical need).
 *
 * Expand to the platform hooks when the profile requires them, and to nothing
 * otherwise, so a lock-free or single-context configuration does not
 * reference the symbols at all.
 */
#if IOLINK_NEED_CRITICAL
#define IOLINK_CRITICAL_ENTER() iolink_critical_enter()
#define IOLINK_CRITICAL_EXIT() iolink_critical_exit()
#else
#define IOLINK_CRITICAL_ENTER() ((void) 0)
#define IOLINK_CRITICAL_EXIT() ((void) 0)
#endif

/**
 * @brief Compile-time assertion that also works in C99 mode.
 *
 * A C11 `_Static_assert` is rejected by `-Wpedantic` in C99, which the
 * delivery matrix uses, so the fallback is the array-size idiom.
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define IOLINK_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
#define IOLINK_STATIC_ASSERT_JOIN_(a, b) a##b
#define IOLINK_STATIC_ASSERT_JOIN(a, b) IOLINK_STATIC_ASSERT_JOIN_(a, b)
#define IOLINK_STATIC_ASSERT(cond, msg) \
    typedef char IOLINK_STATIC_ASSERT_JOIN(iolink_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
IOLINK_STATIC_ASSERT(sizeof(IOLINK_ATOMIC_UINT_T) >= 4,
                     "the ISR concurrency profile needs a 32-bit naturally atomic type");
#endif

/** @} */ /* end of iolinki_port */

#endif  // IOLINK_PORT_H
