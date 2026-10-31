/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdio.h>
#include "iolinki/device.h"
#include <time.h>
#include <string.h>

/**
 * @brief Placeholder for a real hardware PHY implementation
 */
#if IOLINK_FEATURE_DATA_STORAGE
static uint8_t g_storage[IOLINK_STORAGE_TOTAL_SIZE];

static int storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    if ((offset > sizeof(g_storage)) || (len > sizeof(g_storage) - offset)) {
        return -1;
    }
    (void) memcpy(buf, &g_storage[offset], len);
    return 0;
}

static int storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    if ((offset > sizeof(g_storage)) || (len > sizeof(g_storage) - offset)) {
        return -1;
    }
    (void) memcpy(&g_storage[offset], buf, len);
    return 0;
}

static const iolink_storage_api_t g_storage_api = {
    .read = storage_read,
    .write = storage_write,
    .erase = NULL,
};
#endif

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t simple_time_us(void* user)
{
    (void) user;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL);
}

static const iolink_phy_api_t g_hw_phy = {.init = NULL, /* No hardware yet */
                                          .set_mode = NULL,
                                          .set_baudrate = NULL};

int main(void)
{
    printf("IO-Link Simple Device Example\n");

    /* Use default configuration */
    iolink_device_ctx_t ctx;
    iolink_device_config_t dev_config = {
        .phy = g_hw_phy,
        .time_us = simple_time_us,
#if IOLINK_FEATURE_DATA_STORAGE
        .storage = &g_storage_api,
#endif
    };

    if (iolink_device_init(&ctx, &dev_config) != 0) {
        printf("Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("IO-Link stack initialized successfully\n");

    return 0;
}
