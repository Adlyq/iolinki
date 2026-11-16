/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/device.h"
#include "iolinki/phy_virtual.h"

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static volatile uint32_t g_tick_ms;

/* Demo-only clock: a 1 ms tick is too coarse for t_A/t_ren timing. A real
 * device must feed the application trait's time_us from a microsecond timer
 * (or expand the tick counter with the timer's free-running counter). */
static uint64_t baremetal_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return (uint64_t) g_tick_ms * 1000ULL;
}

static void sys_tick_handler(void)
{
    g_tick_ms++;
}

/* 设备身份：库没有默认身份，每个设备都必须自己提供。 */
static const iolink_device_info_t g_identity = {
    .vendor_name = "iolinki example",
    .vendor_text = "Example identity; replace before shipping",
    .product_name = "Simple IO-Link Device",
    .product_id = "simple-device",
    .product_text = "Minimal example",
    .serial_number = "EXAMPLE-001",
    .hardware_revision = "simulation",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5678U,
    .min_cycle_time = 10U,
    .revision_id = IOLINK_REVISION_ID_1_1,
};

int main(void)
{
    /* Initialize stack with virtual PHY (simplified for bare metal, typically uses UART/SPI) */
    iolink_phy_virtual_set_port("/dev/null");
    const iolink_phy_api_t* phy = iolink_phy_virtual_get();
    static const iolink_device_app_t app = {.self = NULL, .time_us = baremetal_time_us};
    static const iolink_stack_config_t stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .od_width = 1U};
    iolink_device_ctx_t ctx;
    iolink_device_config_t dev_config = {
        .phy = phy,
        .stack = &stack,
        .app = &app,
        .device_info = &g_identity,
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
        if (g_tick_ms > 100) {
            break; /* Exit for test success */
        }
    }

    return 0;
}
