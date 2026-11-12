/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_config_trim_zero.c
 * @brief IOLINK_MINIMAL_PROFILE=0 must not trim features.
 *
 * The old `#ifdef` form enabled the profile for any definition, including the
 * value 0. The explicit form (`#if defined(...) && (VALUE + 0)`) requires a
 * non-zero value. This translation unit is the regression guard for that.
 */

#define IOLINK_MINIMAL_PROFILE 0

#include <assert.h>

#include "iolinki/config.h"

/* Compile-time guard: the value 0 must leave every feature enabled. The
 * runtime asserts below are a second line of defence (and keep the file
 * exercised when NDEBUG is not overridden). */
#if IOLINK_FEATURE_DATA_STORAGE != 1
#error "IOLINK_MINIMAL_PROFILE=0 must not disable Data Storage"
#endif
#if IOLINK_FEATURE_SYSTEM_COMMAND != 1
#error "IOLINK_MINIMAL_PROFILE=0 must not disable SystemCommand"
#endif
#if IOLINK_FEATURE_EVENTS != 1
#error "IOLINK_MINIMAL_PROFILE=0 must not disable events"
#endif
#if IOLINK_PHY_FEATURE_WAKEUP != 1
#error "IOLINK_MINIMAL_PROFILE=0 must not disable wake-up"
#endif
#if IOLINK_PHY_FEATURE_SIO_CQ != 1
#error "IOLINK_MINIMAL_PROFILE=0 must not disable SIO C/Q"
#endif

int main(void)
{
    assert(IOLINK_FEATURE_DATA_STORAGE == 1);
    assert(IOLINK_FEATURE_SYSTEM_COMMAND == 1);
    assert(IOLINK_FEATURE_EVENTS == 1);
    assert(IOLINK_PHY_FEATURE_WAKEUP == 1);
    assert(IOLINK_PHY_FEATURE_SIO_CQ == 1);
    return 0;
}
