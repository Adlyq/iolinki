/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device_requires_storage.c
 * @brief With Data Storage compiled in, initialization requires a complete
 *        storage backend.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t test_time_us(void* user)
{
    (void) user;
    return 1000000ULL;
}

static int storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return -1;
}

static int storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

int main(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    (void) memset(&config, 0, sizeof(config));
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.time_us = test_time_us;

    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_STORAGE);

    config.storage = &(iolink_storage_api_t){.read = storage_read, .write = NULL};
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_STORAGE);

    config.storage = &(iolink_storage_api_t){.read = storage_read, .write = storage_write};
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_OK);
    return 0;
}
