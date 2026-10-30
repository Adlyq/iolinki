/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/device.h"
#include "iolinki/phy_virtual.h"
#include "iolinki/time_utils.h"
#include <string.h>

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

static uint64_t baremetal_time_us(void* user)
{
    (void) user;
    return iolink_time_get_us();
}

/* Extern the volatile tick counter from time_utils_baremetal.c */
extern volatile uint32_t g_iolink_ticks_ms;

void sys_tick_handler(void)
{
    g_iolink_ticks_ms++;
}

int main(void)
{
    /* Initialize stack with virtual PHY (simplified for bare metal, typically uses UART/SPI) */
    iolink_phy_virtual_set_port("/dev/null");
    const iolink_phy_api_t* phy = iolink_phy_virtual_get();
    iolink_device_ctx_t ctx;
    iolink_device_config_t dev_config = {
        .phy = *phy,
        .time_us = baremetal_time_us,
#if IOLINK_FEATURE_DATA_STORAGE
        .storage = &g_storage_api,
#endif
    };

    if (iolink_device_init(&ctx, &dev_config) != 0) {
        return -1;
    }

    /* Main Super-Loop */
    while (1) {
        /* Process Stack */
        iolink_device_process(&ctx);

        /* Simulate System Tick (Time Passage) */
        sys_tick_handler();

        /* In a real bare-metal app, we would wait for interrupt or sleep until next tick
         * For this test/demo, we just loop for a bit to prevent 100% CPU on host simulation
         * or just break after N cycles for automated testing.
         */
        if (g_iolink_ticks_ms > 100) {
            break; /* Exit for test success */
        }
    }

    return 0;
}
