/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Log hook contract tests.
 *
 * The library ships the print points but no sink: every IOLINK_LOG_* macro
 * defaults to a no-op and an integrator overrides it in a private header.
 * These tests pin that contract down:
 *   - an overridden macro forwards the format string and its arguments;
 *   - the default macro evaluates nothing (no argument side effects);
 *   - the ISR family stays silent unless IOLINK_LOG_ENABLE_ISR is 1.
 *
 * Overrides must be defined before <iolinki/config.h> is included, which is
 * exactly how an integration header works, so each translation unit here sets
 * up its own view of the macros.
 */

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int g_calls;
static char g_msg[128];

/** @brief Collects one forwarded call for assertions. */
void log_capture(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    (void) vsnprintf(g_msg, sizeof(g_msg), format, args);
    va_end(args);
    g_calls++;
}

#define IOLINK_LOG_ERROR(...) log_capture(__VA_ARGS__)
#define IOLINK_LOG_WARN(...) log_capture(__VA_ARGS__)
#define IOLINK_LOG_INFO(...) log_capture(__VA_ARGS__)
#define IOLINK_LOG_DEBUG(...) log_capture(__VA_ARGS__)

#include "iolinki/config.h"

/* Defined by test_log_defaults.c: no override, arguments must not be
 * evaluated. */
void test_log_defaults_run(void);
/* Defined by test_log_isr_on.c: ISR family forwarded with the switch on. */
void test_log_isr_on_run(void);

static void reset_capture(void)
{
    g_calls = 0;
    g_msg[0] = '\0';
}

int main(void)
{
    reset_capture();
    IOLINK_LOG_ERROR("error %d", 1);
    assert((g_calls == 1) && (strcmp(g_msg, "error 1") == 0));

    reset_capture();
    IOLINK_LOG_WARN("warn 0x%02X", 0xABU);
    assert((g_calls == 1) && (strcmp(g_msg, "warn 0xAB") == 0));

    reset_capture();
    IOLINK_LOG_INFO("info %s", "ok");
    assert((g_calls == 1) && (strcmp(g_msg, "info ok") == 0));

    reset_capture();
    IOLINK_LOG_DEBUG("debug %lu", 7UL);
    assert((g_calls == 1) && (strcmp(g_msg, "debug 7") == 0));

    /* Default: ISR print points are compiled out even when the main macros
     * are active, and their arguments are not evaluated. */
    reset_capture();
    IOLINK_LOG_ISR_ERROR("isr %d", 1);
    IOLINK_LOG_ISR_WARN("isr %d", 2);
    IOLINK_LOG_ISR_INFO("isr %d", 3);
    IOLINK_LOG_ISR_DEBUG("isr %d", 4);
    assert(g_calls == 0);
    assert(g_msg[0] == '\0');

    test_log_defaults_run();

    reset_capture();
    test_log_isr_on_run();
    assert((g_calls == 1) && (strcmp(g_msg, "isr on 7") == 0));

    return 0;
}
