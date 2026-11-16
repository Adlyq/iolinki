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

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t simple_time_us(iolink_app_t* self)
{
    (void) self;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL);
}

/**
 * @brief Placeholder for a real hardware PHY implementation
 */
static const iolink_phy_api_t g_hw_phy = {.init = NULL, /* No hardware yet */
                                          .set_mode = NULL,
                                          .set_baudrate = NULL};

int main(void)
{
    printf("IO-Link Simple Device Example\n");

    /* Use default configuration */
    static const iolink_device_app_t app = {.self = NULL, .time_us = simple_time_us};
    iolink_device_ctx_t ctx;
    iolink_device_config_t dev_config = {
        .phy = &g_hw_phy,
        .app = &app,
    };

    if (iolink_device_init(&ctx, &dev_config) != 0) {
        printf("Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("IO-Link stack initialized successfully\n");

    return 0;
}
