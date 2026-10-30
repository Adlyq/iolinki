/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PLATFORM_H
#define IOLINK_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file platform.h
 * @brief Platform encapsulation for RTOS integration.
 *
 * This file provides weak definitions or macros for critical sections.
 * Platforms/RTOS integrations should override these to ensure thread safety.
 */

/**
 * @defgroup iolinki_platform Platform Abstraction
 * @brief Critical-section and non-volatile memory hooks the integrator provides.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter a critical section (disable interrupts/scheduler).
 *
 * This function must prevent context switches or ISRs that could
 * corrupt shared data structures (Events, ISDU).
 *
 * The library default (src/platform.c) is a no-op. That is only safe in a
 * single-context bare-metal loop without an ISR pump. The ISR two-pump
 * transport is the only transport and MUST have a real implementation
 * (e.g. PRIMASK/BASEPRI on Cortex-M) or an application lock pair configured in
 * iolink_device_config_t: the ISR pump shares the mailbox, the ISDU response
 * snapshot and the reply staging buffer with the main pump.
 *
 * The implementation must be nesting-safe: the library may enter the critical
 * section while already inside one (e.g. a DLL state transition that resets the
 * event queue). A PRIMASK save/restore port must therefore keep a nesting depth
 * (the STM32G0 raw-PHY example does).
 *
 * The stack is designed for one main-context caller; with an RTOS, the
 * configured lock must also cover every thread that enters iolink_dll_process()
 * / iolink_device_process(), because disabling interrupts does not serialise
 * threads.
 */
void iolink_critical_enter(void);

/**
 * @brief Exit a critical section (restore interrupts/scheduler).
 */
void iolink_critical_exit(void);

/**
 * @brief Probe whether the platform supplied real critical-section hooks.
 *
 * Calls iolink_critical_enter()/iolink_critical_exit() once and reports whether
 * the weak no-op defaults from src/platform.c were used. iolink_device_init()
 * uses this to reject builds without a usable critical-section port.
 *
 * @return 0 when both hooks are overridden, -1 when the weak default ran.
 */
int iolink_platform_critical_sections_ready(void);

/* Weak no-op critical-section definitions live in src/platform.c; a real port
 * overrides them and iolink_platform_critical_sections_ready() verifies that.
 */

#ifdef __cplusplus
}
#endif

/** @} */ /* end of iolinki_platform */

#endif  // IOLINK_PLATFORM_H
