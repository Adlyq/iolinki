/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "iolinki/device.h"
#include "iolinki/phy_virtual.h"
#include <time.h>

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t host_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL);
}

/** @brief Run the ISR pump and drain any reply it is transmitting. */
static void pump_isr(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.main.shared.b_isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
}

static uint8_t g_tags[3][32];
static uint8_t g_tag_len[3];
static iolink_od_entry_t g_tag_entries[3];
static iolink_od_app_t g_tag_od;

/* 三个标签是应用数据：以 STORAGE 表项提供，默认应用标签 "DefaultTag"。 */
static void host_tags_init(void)
{
    static const char default_tag[] = "DefaultTag";
    (void) memcpy(g_tags[0], default_tag, sizeof(default_tag) - 1U);
    g_tag_len[0] = (uint8_t) (sizeof(default_tag) - 1U);
    g_tag_entries[0] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_APPLICATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "ApplicationSpecificTag", g_tags[0], &g_tag_len[0]);
    g_tag_entries[1] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_FUNCTION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "FunctionTag", g_tags[1], &g_tag_len[1]);
    g_tag_entries[2] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_LOCATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "LocationTag", g_tags[2], &g_tag_len[2]);
    g_tag_od.entries = g_tag_entries;
    g_tag_od.count = 3U;
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

int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s <tty_device> [m_seq_type] [pd_len]\n", argv[0]);
        printf("  m_seq_type: 0=TYPE_0, 2=TYPE_1_2, 3=TYPE_1_V, 4=TYPE_2_1,\n");
        printf("              5=TYPE_2_2, 6=TYPE_2_V, 7=TYPE_2_3, 8=TYPE_2_4, 9=TYPE_2_5\n");
        printf("  pd_len: TYPE_2_V PD-in/out length (default 2)\n");
        return -1;
    }

    printf("=== iolinki Host Demo ===\n");
    printf("IO-Link Device Stack v0.1.0\n");
    printf("Connecting to: %s\n", argv[1]);

    /* Parse optional config. The M-sequence value is the C enum value, so the
     * Python Master can pass its MSequenceType directly. */
    iolink_stack_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0,
                                    .od_width = 1,
                                    .min_cycle_time = 0,
                                    .pd_in_len = 0,
                                    .pd_out_len = 0};
    int type = IOLINK_M_SEQ_TYPE_0;
    int pd_len = 2;

    if (argc >= 3) {
        type = atoi(argv[2]);
    }
    if (argc >= 4) {
        pd_len = atoi(argv[3]);
    }

    config.m_seq_type = (iolink_m_seq_type_t) type;
    switch (type) {
        case IOLINK_M_SEQ_TYPE_0:
            break;
        case IOLINK_M_SEQ_TYPE_1_2:
            config.od_width = 2U;
            break;
        case IOLINK_M_SEQ_TYPE_1_V:
            config.od_width = 8U;
            break;
        case IOLINK_M_SEQ_TYPE_2_1:
            config.pd_in_len = 1U;
            config.od_width = 1U;
            break;
        case IOLINK_M_SEQ_TYPE_2_2:
            config.pd_in_len = 2U;
            config.od_width = 1U;
            break;
        case IOLINK_M_SEQ_TYPE_2_3:
            config.pd_out_len = 1U;
            config.od_width = 1U;
            break;
        case IOLINK_M_SEQ_TYPE_2_4:
            config.pd_out_len = 2U;
            config.od_width = 1U;
            break;
        case IOLINK_M_SEQ_TYPE_2_5:
            config.pd_in_len = 1U;
            config.pd_out_len = 1U;
            config.od_width = 1U;
            break;
        case IOLINK_M_SEQ_TYPE_2_V:
            config.pd_in_len = (uint8_t) pd_len;
            config.pd_out_len = (uint8_t) pd_len;
            config.od_width = 2U;
            break;
        default:
            printf("ERROR: M-sequence type %d is not supported\n", type);
            return -1;
    }
    printf("Config: M-sequence type %d, PD-in=%u, PD-out=%u, OD=%u\n", type,
           (unsigned) config.pd_in_len, (unsigned) config.pd_out_len, (unsigned) config.od_width);

    printf("\n");

    /* Configure virtual PHY port */
    iolink_phy_virtual_set_port(argv[1]);

    /* Initialize stack with virtual PHY and config */
    const iolink_phy_api_t* phy = iolink_phy_virtual_get();
    static const iolink_device_app_t app = {.self = NULL, .time_us = host_time_us};
    iolink_device_ctx_t ctx;
    iolink_device_config_t dev_config = {
        .phy = phy,
        .stack = &config,
        .app = &app,
        .device_info = &g_identity,
    };
    host_tags_init();
    dev_config.od = &g_tag_od;

    if (iolink_device_init(&ctx, &dev_config) != 0) {
        printf("ERROR: Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("Stack initialized successfully\n");
    printf("Running protocol state machine...\n\n");

    /* Simulate periodic processing and PD exchange */

    while (1) {
        pump_isr(&ctx);
        iolink_device_process(&ctx);
        /* Send a reply staged by a main-context handler (request_service). */
        pump_isr(&ctx);

        /* Check for output data (Master -> Device) */
        uint8_t pd_buffer[32];
        int len = iolink_device_pd_output_read(&ctx, pd_buffer, sizeof(pd_buffer));
        /* Fixed types may have no PD-in (e.g. TYPE_2_3/2_4): never publish more
         * input octets than the type carries. */
        int in_len = (int) iolink_device_get_pd_in_len(&ctx);
        if (len > in_len) {
            len = in_len;
        }
        if (len > 0) {
            /* Echo + 1 */
            for (int i = 0; i < len; i++) {
                pd_buffer[i]++;
            }
            iolink_device_pd_input_update(&ctx, pd_buffer, (size_t) len, true);
        }

        /* 1ms cycle time simulation */
        /* Note: For accurate timing, use a timer or sleep less */
        usleep(1000); /* 1ms polling to reduce CPU load */
    }

    return 0;
}
