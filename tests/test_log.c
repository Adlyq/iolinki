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

#if IOLINK_NEED_CRITICAL
/* Strong replacements for the weak no-op defaults: iolink_device_init()
 * rejects builds that still use the defaults, and the integration test below
 * initializes a real device. */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}
#endif

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

/* --- Real device initialization ------------------------------------------
 * The setter test above drives the sink propagation directly. These helpers
 * exercise the production path: iolink_device_init() must bind the config
 * sink after the submodules are zero-initialized, and the early failure path
 * must still log through the config (the context is not usable yet).
 */

static uint64_t fake_time_us(void* user)
{
    (void) user;
    return 1000U;
}

static int fake_storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return -1; /* no persisted data */
}

static int fake_storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

static int fake_storage_erase(void* user, uint32_t offset, size_t len)
{
    (void) user;
    (void) offset;
    (void) len;
    return 0;
}

static const iolink_storage_api_t g_fake_storage = {
    NULL,
    fake_storage_read,
    fake_storage_write,
    fake_storage_erase,
};

static void make_minimal_config(iolink_device_config_t* config)
{
    (void) memset(config, 0, sizeof(*config));
    config->time_us = fake_time_us;
#if IOLINK_FEATURE_DATA_STORAGE
    config->storage = &g_fake_storage;
#endif
    config->stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    config->stack.pd_in_len = 2U;
    config->stack.pd_out_len = 2U;
    config->stack.od_width = 1U;
}

static void test_device_init_binds_config_sink(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    int token;

    make_minimal_config(&config);
    config.user = &token;
    config.log_write = log_capture;
    config.log_level = IOLINK_LOG_LEVEL_INFO;

    reset_capture();
    assert(iolink_device_init(&ctx, &config) == 0);

    /* The init summary is emitted through the config sink, with config.user. */
    assert(g_calls >= 1);
    assert(g_last_level == IOLINK_LOG_LEVEL_INFO);
    assert(g_last_user == &token);
    assert(strstr(g_last_msg, "device init") != NULL);

    assert(ctx.dll.log.write == log_capture);
    assert(ctx.dll.log.user == &token);
    assert(ctx.dll.log.level == IOLINK_LOG_LEVEL_INFO);
    assert(ctx.dll.isdu.log.write == log_capture);
#if IOLINK_FEATURE_DATA_STORAGE
    assert(ctx.dll.ds.log.write == log_capture);
#endif
#if IOLINK_FEATURE_EVENTS
    assert(ctx.dll.events.log.write == log_capture);
#endif

    /* The runtime switch silences an initialized device immediately. */
    iolink_device_set_log(&ctx, NULL, IOLINK_LOG_LEVEL_SILENT);
    reset_capture();
    iolink_ds_check(&ctx.dll.ds, 0U); /* would log an upload request */
    assert(g_calls == 0);
}

static void test_device_init_failure_logs_through_config(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    int token;

    make_minimal_config(&config);
    config.time_us = NULL; /* rejected before the context is initialized */
    config.user = &token;
    config.log_write = log_capture;
    config.log_level = IOLINK_LOG_LEVEL_DEBUG;

    reset_capture();
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_TIME);
    assert(g_calls == 1);
    assert(g_last_level == IOLINK_LOG_LEVEL_ERROR);
    assert(g_last_user == &token);
    assert(strstr(g_last_msg, "init rejected") != NULL);
}

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
    test_device_init_binds_config_sink();
    test_device_init_failure_logs_through_config();
    return 0;
}
