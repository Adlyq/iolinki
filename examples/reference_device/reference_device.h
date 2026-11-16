/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef REFERENCE_DEVICE_H
#define REFERENCE_DEVICE_H
#include "iolinki/device.h"

/* Config and identity must outlive the stack. Keep the entire app in static
 * storage on embedded targets; each target supplies a real PHY and clock. */
typedef struct
{
    iolink_device_ctx_t device;
    iolink_device_config_t config;
    iolink_device_app_t app_trait;
    uint16_t counter;
    uint8_t input[3];
    bool button;
    bool led;
    uint8_t tags[3][32];
    uint8_t tag_len[3];
    iolink_od_entry_t od_entries[3];
    iolink_od_app_t od;
} reference_device_t;

int reference_device_init(reference_device_t* app, const iolink_phy_api_t* phy,
                          iolink_device_time_us_fn time_us);
/* Call once per application sampling tick, independently of the much faster
 * protocol processing loop. Input: big-endian counter + button; output: LED bit 0. */
int reference_device_tick(reference_device_t* app, bool button);
#endif
