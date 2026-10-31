/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file platform.c
 * @brief Default weak platform abstraction implementations.
 * @ingroup iolinki_platform
 *
 * Provides weak no-op critical-section defaults so the stack links, and a probe
 * that lets iolink_device_init() reject builds still using them. A real port
 * overrides the critical-section symbols.
 */

#include "iolinki/platform.h"

/* Weak definitions allow the application to override them without link errors.
 * The probe below is only consulted when the selected concurrency profile
 * requires a critical section (IOLINK_NEED_CRITICAL). */

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

static volatile uint8_t g_critical_default_used = 0U;

WEAK void iolink_critical_enter(void)
{
    /* Default: Do nothing (Bare metal single loop is implicitly safe if no IRQ contention) */
    g_critical_default_used = 1U;
}

WEAK void iolink_critical_exit(void)
{
    /* Default: Do nothing */
    g_critical_default_used = 1U;
}

int iolink_platform_critical_sections_ready(void)
{
    g_critical_default_used = 0U;
    iolink_critical_enter();
    iolink_critical_exit();
    const uint8_t used = g_critical_default_used;
    g_critical_default_used = 0U;
    return (used != 0U) ? -1 : 0;
}
