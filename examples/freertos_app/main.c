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
#include "iolinki/port.h"
#include "iolinki/phy.h"
#include <stdbool.h>
#include <time.h>

/* Platform Override for Critical Sections.
 * taskENTER_CRITICAL() masks interrupts up to configMAX_SYSCALL_INTERRUPT_PRIORITY,
 * so the IO-Link data interrupt must be configured at that priority or lower
 * (numerically >= configMAX_SYSCALL_INTERRUPT_PRIORITY) for this hook to
 * exclude the pump; otherwise mask the interrupt directly here. */
void iolink_critical_enter(void)
{
    taskENTER_CRITICAL();
}

void iolink_critical_exit(void)
{
    taskEXIT_CRITICAL();
}

/* Mock PHY */
const iolink_phy_api_t g_phy_freertos = {.init = NULL, .set_baudrate = NULL};

static uint64_t freertos_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL);
}

static iolink_device_ctx_t g_device_ctx;
static const iolink_device_app_t g_device_app = {.self = NULL, .time_us = freertos_time_us};
static const iolink_device_info_t g_identity = {
    .vendor_name = "iolinki example",
    .product_name = "FreeRTOS device",
    .product_id = "freertos-device",
    .serial_number = "EXAMPLE-001",
    .hardware_revision = "1",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5678U,
    .min_cycle_time = 10U,
    .revision_id = IOLINK_REVISION_ID_1_1,
};
static const iolink_stack_config_t g_device_stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_0,
                                                     .od_width = 1U};
static iolink_device_config_t g_device_config = {
    .phy = &g_phy_freertos,
    .stack = &g_device_stack,
    .app = &g_device_app,
    .device_info = &g_identity,
};

/* Request posted by another task and served by the IO-Link task. The IO-Link
 * API has a single main-context caller, so only iolink_task_entry() calls it. */
static volatile bool g_event_requested;

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
        /* Serve requests posted by other tasks from this main context. */
        if (g_event_requested) {
            g_event_requested = false;
            iolink_event_trigger(iolink_device_get_events_ctx(&g_device_ctx), 0x1800,
                                 IOLINK_EVENT_TYPE_NOTIFICATION);
        }

        /* Process Stack */
        iolink_device_process(&g_device_ctx);

        /* Yield / Sleep to allow other tasks (1ms cycle) */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* Application Task: it never enters the IO-Link API, it only posts a request
 * for the IO-Link task to serve (a queue is the usual RTOS alternative). */
void app_task_entry(void* pvParameters)
{
    (void) pvParameters;

    for (;;) {
        /* Request an event every 5 seconds. */
        g_event_requested = true;

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
