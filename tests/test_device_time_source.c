/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device_time_source.c
 * @brief The application clock supplied via iolink_device_config_t::time_us is
 *        used by the DLL and the event engine instead of the platform clock.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/events.h"

static int test_storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return -1;
}

static int test_storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

static const iolink_storage_api_t g_test_storage = {
    .read = test_storage_read, .write = test_storage_write, .erase = NULL};

static uint64_t g_app_time_us;

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t app_time_us(void* user)
{
    (void) user;
    return g_app_time_us;
}

static void init_device(iolink_device_ctx_t* ctx, iolink_device_time_us_fn time_us)
{
    iolink_device_config_t config;

    (void) memset(&config, 0, sizeof(config));
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.storage = &g_test_storage;
    config.stack.t_pd_us = 500U;
    config.time_us = time_us;

    assert(iolink_device_init(ctx, &config) == 0);
}

static void test_initialization_uses_app_clock(void)
{
    iolink_device_ctx_t ctx;

    g_app_time_us = 1000000ULL;
    init_device(&ctx, app_time_us);

    /* t_pd deadline and the master-activity seed come from the app clock. */
    assert(ctx.dll.t_pd_deadline_us == (1000000ULL + 500ULL));
    assert(ctx.dll.master_last_activity_ms == 1000U);
}

static void test_event_engine_uses_app_clock(void)
{
    iolink_device_ctx_t ctx;

    g_app_time_us = 1234000ULL;
    init_device(&ctx, app_time_us);

    iolink_event_trigger(&ctx.dll.events, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD,
                         IOLINK_EVENT_TYPE_ERROR);

    assert(ctx.dll.events.pending[0].active);
    assert(ctx.dll.events.pending[0].appears_valid);
    assert(ctx.dll.events.pending[0].last_appears_ms == 1234U);
}

static void test_null_clock_is_rejected_at_init(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    (void) memset(&config, 0, sizeof(config));
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;

    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_TIME);
}

int main(void)
{
    test_initialization_uses_app_clock();
    test_event_engine_uses_app_clock();
    test_null_clock_is_rejected_at_init();
    return 0;
}
