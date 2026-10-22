/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "iolinki/device.h"
#include "iolinki/phy_virtual.h"

#if IOLINK_PHY_FEATURE_ISR_PUMP
/** @brief Run the ISR pump and drain any reply it is transmitting. */
static void pump_isr(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
}
#endif

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
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .min_cycle_time = 0, .pd_in_len = 0, .pd_out_len = 0};
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
           (unsigned) config.pd_in_len, (unsigned) config.pd_out_len,
           (unsigned) config.od_width);

    printf("\n");

    /* Configure virtual PHY port */
    iolink_phy_virtual_set_port(argv[1]);

    /* Initialize stack with virtual PHY and config */
    const iolink_phy_api_t* phy = iolink_phy_virtual_get();
    iolink_device_ctx_t ctx;
    iolink_device_config_t dev_config = {.phy = *phy, .stack = config};

    if (iolink_device_init(&ctx, &dev_config) != 0) {
        printf("ERROR: Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("Stack initialized successfully\n");
    printf("Running protocol state machine...\n\n");

    /* Simulate periodic processing and PD exchange */

    while (1) {
#if IOLINK_PHY_FEATURE_ISR_PUMP
        pump_isr(&ctx);
#endif
        iolink_device_process(&ctx);
#if IOLINK_PHY_FEATURE_ISR_PUMP
        /* Send a reply staged by a main-context handler (request_service). */
        pump_isr(&ctx);
#endif

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
