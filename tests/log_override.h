/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Log override used by the compile matrix (log_active configuration) and as
 * a documented example of how an application wires the library print points
 * into its own logger. It is force-included with `-include` so it applies to
 * every library translation unit.
 *
 * The format strings are checked by the compiler here, which is the point of
 * the configuration: every print point in src/ is compiled with real format
 * arguments under -Werror.
 */

#ifndef IOLINK_TESTS_LOG_OVERRIDE_H
#define IOLINK_TESTS_LOG_OVERRIDE_H

#include <stdio.h>

#define IOLINK_LOG_ERROR(...)                               \
    do {                                                    \
        (void) fprintf(stderr, "[iolink][E] " __VA_ARGS__); \
    } while (0)
#define IOLINK_LOG_WARN(...)                                \
    do {                                                    \
        (void) fprintf(stderr, "[iolink][W] " __VA_ARGS__); \
    } while (0)
#define IOLINK_LOG_INFO(...)                                \
    do {                                                    \
        (void) fprintf(stderr, "[iolink][I] " __VA_ARGS__); \
    } while (0)
#define IOLINK_LOG_DEBUG(...)                               \
    do {                                                    \
        (void) fprintf(stderr, "[iolink][D] " __VA_ARGS__); \
    } while (0)

/* Exercise the ISR print points in this configuration as well. */
#define IOLINK_LOG_ENABLE_ISR 1

#endif /* IOLINK_TESTS_LOG_OVERRIDE_H */
