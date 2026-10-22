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
 * The library default (src/platform.c) is a no-op. That is only safe for the
 * default streaming transport in a single-context bare-metal loop. A build with
 * IOLINK_PHY_FEATURE_ISR_PUMP enabled MUST provide a real implementation
 * (e.g. PRIMASK/BASEPRI on Cortex-M) or configure the application lock pair in
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
 * @brief Legacy platform NVM helper (not used by the stack).
 *
 * The stack persists data through iolink_device_config_t::storage
 * (see storage.h). These helpers remain for platform/test harnesses only.
 *
 * @param offset Offset in NVM
 * @param data Buffer to read into
 * @param len Length to read
 * @return int 0 on success
 */
int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len);

/**
 * @brief Write data to non-volatile memory (NVM).
 * @param offset Offset in NVM
 * @param data Data to write
 * @param len Length to write
 * @return int 0 on success
 */
int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len);

/* Default (Weak) Implementation typically provided in a platform source file.
 * If strictly header-only or macro-based is preferred, use #define macros here.
 * For this stack, we declere functions that the user must implement or we provide weak defaults.
 */

#ifdef __cplusplus
}
#endif

/** @} */ /* end of iolinki_platform */

#endif  // IOLINK_PLATFORM_H
