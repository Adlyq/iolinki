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

#include "iolinki/port.h"

/**
 * @file platform.h
 * @brief Platform encapsulation for RTOS and bare-metal ports.
 *
 * The primitive set, its capability grades and the requirement derivation live
 * in iolinki/port.h, together with the declarations of
 * iolink_critical_enter(), iolink_critical_exit() and
 * iolink_platform_critical_sections_ready(). This header keeps the historical
 * platform entry point so an existing port that includes only platform.h keeps
 * compiling unchanged.
 *
 * A port implements the primitives its concurrency profile requires
 * (`IOLINK_NEED_*` in iolinki/port.h) and may omit the rest. The critical
 * section is required by every asynchronous profile (`IOLINK_CONCURRENCY >=
 * ISR`); a `SINGLE` profile never references the symbols, so a bare-metal or
 * hosted port needs no critical-section implementation there.
 *
 * The implementation must be nesting-safe and callable from interrupt context:
 * the library may enter the critical section while already inside one (a DLL
 * state transition that resets the event queue), and a DLL state transition can
 * itself be taken from the data ISR. A PRIMASK save/restore port must therefore
 * keep a nesting depth (the STM32G0 raw-PHY example does). The library has a
 * single main-context caller per context: the data ISR may preempt it, but no
 * other task may enter iolink_dll_process() / iolink_device_process(), and the
 * application must not wrap the entry points in its own lock either (the
 * library owns the granularity of its critical sections). A port critical
 * section that also serialises tasks therefore does not make concurrent API
 * calls supported; it only adds latency to the caller.
 *
 * See docs/PORTING.md for the full contract, per-platform mappings and the
 * forbidden combinations.
 */

/**
 * @defgroup iolinki_platform Platform Abstraction
 * @brief Critical-section and non-volatile memory hooks the integrator provides.
 * @{
 */

/** @} */ /* end of iolinki_platform */

#endif  // IOLINK_PLATFORM_H
