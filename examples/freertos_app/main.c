/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdio.h>
/* Mock FreeRTOS headers if not present, but for example we assume they exist in include path */
/* To make this compile in the CI environment without FreeRTOS, we will guard it or use a stub */
#if 0
#include "FreeRTOS.h"
#include "task.h"
#else
/* Stub for demonstration if FreeRTOS is not found */
#define taskENTER_CRITICAL()
#define taskEXIT_CRITICAL()
#define vTaskDelay(x)
#define pdMS_TO_TICKS(x) ((x) / 10)
#endif

#include "iolinki/device.h"
#include "iolinki/platform.h"
#include "iolinki/phy.h"
#include "iolinki/time_utils.h"
#include <string.h>

/* Platform Override for Critical Sections */
void iolink_critical_enter(void)
{
    taskENTER_CRITICAL();
}

void iolink_critical_exit(void)
{
    taskEXIT_CRITICAL();
}

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

/* Mock PHY */
const iolink_phy_api_t g_phy_freertos = {.init = NULL, .set_baudrate = NULL};

static uint64_t freertos_time_us(void* user)
{
    (void) user;
    return iolink_time_get_us();
}

static iolink_device_ctx_t g_device_ctx;
static iolink_device_config_t g_device_config = {
    .phy = g_phy_freertos,
    .time_us = freertos_time_us,
#if IOLINK_FEATURE_DATA_STORAGE
    .storage = &g_storage_api,
#endif
};

/* IO-Link Stack Task */
void iolink_task_entry(void* pvParameters)
{
    (void) pvParameters;

    /* Initialize Stack */
    if (iolink_device_init(&g_device_ctx, &g_device_config) != 0) {
        printf("IO-Link init failed\n");
        return;
    }

    printf("IO-Link Task Started\n");

    for (;;) {
        /* Process Stack */
        iolink_device_process(&g_device_ctx);

        /* Yield / Sleep to allow other tasks (1ms cycle) */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* Application Task (simulating events trigger from another thread) */
void app_task_entry(void* pvParameters)
{
    (void) pvParameters;

    for (;;) {
        /* Trigger an event every 5 seconds safely */
        iolink_event_trigger(iolink_device_get_events_ctx(&g_device_ctx), 0x1800,
                             IOLINK_EVENT_TYPE_NOTIFICATION);

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

int main(void)
{
    printf("Starting FreeRTOS Example...\n");

    /* Create Tasks */
    /* xTaskCreate(iolink_task_entry, "IOLink", 1024, NULL, 2, NULL); */
    /* xTaskCreate(app_task_entry, "App", 1024, NULL, 1, NULL); */

    /* Start Scheduler */
    /* vTaskStartScheduler(); */

    return 0;
}
