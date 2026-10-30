/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "reference_device.h"
#include <string.h>

#include "iolinki/time_utils.h"

static const iolink_device_info_t identity = {
    .vendor_name = "iolinki example",
    .vendor_text = "Experimental identity; replace before shipping",
    .product_name = "Counter button LED",
    .product_id = "reference-device",
    .product_text = "Three input bytes and one output byte",
    .serial_number = "EXAMPLE-001",
    .hardware_revision = "simulation",
    .firmware_revision = "1",
    .vendor_id = 1234,
    .device_id = 5678,
    .min_cycle_time = 10,
    .revision_id = 0x0111,
};

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

static uint64_t reference_time_us(void* user)
{
    (void) user;
    return iolink_time_get_us();
}

int reference_device_init(reference_device_t* app, const iolink_phy_api_t* phy)
{
    if ((app == NULL) || (phy == NULL)) {
        return -1;
    }
    memset(app, 0, sizeof(*app));
    app->config.phy = *phy;
    app->config.time_us = reference_time_us;
#if IOLINK_FEATURE_DATA_STORAGE
    app->config.storage = &g_storage_api;
#endif
    app->config.device_info = &identity;
    app->config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    app->config.stack.pd_in_len = sizeof(app->input);
    app->config.stack.pd_out_len = 1;
    app->config.stack.min_cycle_time = 10;
    return iolink_device_init(&app->device, &app->config);
}

int reference_device_tick(reference_device_t* app, bool button)
{
    if (app == NULL) {
        return -1;
    }
    app->counter++;
    app->input[0] = (uint8_t) (app->counter >> 8);
    app->input[1] = (uint8_t) app->counter;
    app->input[2] = button ? 1U : 0U;
    if (button && !app->button) {
#if IOLINK_FEATURE_EVENTS
        iolink_event_trigger(iolink_device_get_events_ctx(&app->device), 0x8CA0,
                             IOLINK_EVENT_TYPE_NOTIFICATION);
#endif
    }
    app->button = button;
    uint8_t output = 0;
    if (iolink_device_pd_output_read(&app->device, &output, 1) == 1) {
        app->led = (output & 1U) != 0U;
    }
    return iolink_device_pd_input_update(&app->device, app->input, sizeof(app->input), true);
}
