/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device_requires_critical.c
 * @brief Device initialization rejects the library's weak no-op critical
 *        sections; this executable deliberately links no strong override.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"

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

static uint64_t test_time_us(void* user)
{
    (void) user;
    return 1000000ULL;
}

int main(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    (void) memset(&config, 0, sizeof(config));
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.storage = &g_test_storage;
    config.time_us = test_time_us;

    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_CRITICAL);
    return 0;
}
