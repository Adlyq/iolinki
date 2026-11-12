/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Runtime log sink contract tests.
 *
 * The library owns the print points but not the output: each context carries
 * an iolink_log_sink_t holding a callback, an opaque user pointer and the
 * runtime threshold. These tests pin down the sink semantics: zero sink is
 * silent, the threshold filters by level, arguments are only evaluated when a
 * line is emitted, ISR print points need IOLINK_LOG_ENABLE_ISR, and the device
 * setter propagates the sink to every subsystem.
 */

#include <assert.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/log.h"

int g_calls;
iolink_log_level_t g_last_level;
char g_last_msg[128];
void* g_last_user;

/* Shared with test_log_isr_on.c, which compiles IOLINK_LOG_ENABLE_ISR=1. */
void log_capture(void* user, iolink_log_level_t level, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    (void) vsnprintf(g_last_msg, sizeof(g_last_msg), format, args);
    va_end(args);
    g_last_level = level;
    g_last_user = user;
    g_calls++;
}

static void reset_capture(void)
{
    g_calls = 0;
    g_last_level = IOLINK_LOG_LEVEL_SILENT;
    g_last_msg[0] = '\0';
    g_last_user = NULL;
}

static int g_side_effects;

static int side_effect(void)
{
    g_side_effects++;
    return 1;
}

void test_log_isr_on_run(void);

static void test_zero_sink_is_silent(void)
{
    iolink_log_sink_t sink;

    (void) memset(&sink, 0, sizeof(sink));
    reset_capture();
    IOLINK_LOG_ERROR(&sink, "never %d", 1);
    IOLINK_LOG_WARN(&sink, "never %d", 2);
    assert(g_calls == 0);

    /* A missing sink is equally harmless. */
    IOLINK_LOG_INFO(NULL, "never %d", 3);
    assert(g_calls == 0);
}

static void test_level_filter(void)
{
    iolink_log_sink_t sink;
    iolink_log_sink_init(&sink, log_capture, NULL, IOLINK_LOG_LEVEL_WARN);

    reset_capture();
    IOLINK_LOG_ERROR(&sink, "error");
    IOLINK_LOG_WARN(&sink, "warn");
    IOLINK_LOG_INFO(&sink, "info");
    IOLINK_LOG_DEBUG(&sink, "debug");
    assert(g_calls == 2);
    assert(g_last_level == IOLINK_LOG_LEVEL_WARN);
    assert(strcmp(g_last_msg, "warn") == 0);

    sink.level = IOLINK_LOG_LEVEL_DEBUG;
    reset_capture();
    IOLINK_LOG_DEBUG(&sink, "debug %lu", 7UL);
    assert(g_calls == 1);
    assert(g_last_level == IOLINK_LOG_LEVEL_DEBUG);
    assert(strcmp(g_last_msg, "debug 7") == 0);

    /* SILENT is the runtime off switch, even with a live callback. */
    sink.level = IOLINK_LOG_LEVEL_SILENT;
    reset_capture();
    IOLINK_LOG_ERROR(&sink, "error");
    assert(g_calls == 0);
}

static void test_forwarding_and_user(void)
{
    iolink_log_sink_t sink;
    int token;

    iolink_log_sink_init(&sink, log_capture, &token, IOLINK_LOG_LEVEL_DEBUG);
    reset_capture();
    IOLINK_LOG_INFO(&sink, "idx=0x%04X sub=%u", 0x0042U, 3U);
    assert(g_calls == 1);
    assert(g_last_level == IOLINK_LOG_LEVEL_INFO);
    assert(g_last_user == &token);
    assert(strcmp(g_last_msg, "idx=0x0042 sub=3") == 0);
}

static void test_arguments_not_evaluated_when_filtered(void)
{
    iolink_log_sink_t sink;
    iolink_log_sink_init(&sink, log_capture, NULL, IOLINK_LOG_LEVEL_ERROR);

    g_side_effects = 0;
    reset_capture();
    IOLINK_LOG_DEBUG(&sink, "filtered %d", side_effect());
    IOLINK_LOG_INFO(&sink, "filtered %d", side_effect());
    assert(g_calls == 0);
    assert(g_side_effects == 0);

    IOLINK_LOG_ERROR(&sink, "kept %d", side_effect());
    assert(g_calls == 1);
    assert(g_side_effects == 1);
}

static void test_isr_family_is_off_by_default(void)
{
    iolink_log_sink_t sink;
    iolink_log_sink_init(&sink, log_capture, NULL, IOLINK_LOG_LEVEL_DEBUG);

    reset_capture();
    IOLINK_LOG_ISR_ERROR(&sink, "isr %d", 1);
    IOLINK_LOG_ISR_WARN(&sink, "isr %d", 2);
    IOLINK_LOG_ISR_INFO(&sink, "isr %d", 3);
    IOLINK_LOG_ISR_DEBUG(&sink, "isr %d", 4);
    assert(g_calls == 0);

    /* The enabled translation unit reaches the same callback. */
    reset_capture();
    test_log_isr_on_run();
    assert(g_calls == 1);
    assert(strcmp(g_last_msg, "isr on 7") == 0);
}

static void test_device_setter_propagates(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    int token;

    (void) memset(&ctx, 0, sizeof(ctx));
    (void) memset(&config, 0, sizeof(config));
    config.user = &token;
    ctx.config = &config;

    iolink_device_set_log(&ctx, log_capture, IOLINK_LOG_LEVEL_INFO);

    assert(ctx.dll.log.write == log_capture);
    assert(ctx.dll.log.user == &token);
    assert(ctx.dll.log.level == IOLINK_LOG_LEVEL_INFO);
    assert(ctx.dll.isdu.log.write == log_capture);
    assert(ctx.dll.isdu.log.user == &token);
#if IOLINK_FEATURE_DATA_STORAGE
    assert(ctx.dll.ds.log.write == log_capture);
    assert(ctx.dll.ds.log.user == &token);
#endif
#if IOLINK_FEATURE_EVENTS
    assert(ctx.dll.events.log.write == log_capture);
    assert(ctx.dll.events.log.user == &token);
#endif

    /* Logging through a propagated subsystem sink works. */
    reset_capture();
    IOLINK_LOG_INFO(&ctx.dll.isdu.log, "from isdu");
    assert((g_calls == 1) && (strcmp(g_last_msg, "from isdu") == 0));

    iolink_device_set_log(&ctx, NULL, IOLINK_LOG_LEVEL_SILENT);
    assert(ctx.dll.log.write == NULL);
    assert(ctx.dll.log.level == IOLINK_LOG_LEVEL_SILENT);
}

int main(void)
{
    test_zero_sink_is_silent();
    test_level_filter();
    test_forwarding_and_user();
    test_arguments_not_evaluated_when_filtered();
    test_isr_family_is_off_by_default();
    test_device_setter_propagates();
    return 0;
}
