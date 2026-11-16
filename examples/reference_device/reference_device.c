/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "reference_device.h"
#include <string.h>

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
    .revision_id = IOLINK_REVISION_ID_1_1,
};

int reference_device_init(reference_device_t* app, const iolink_phy_api_t* phy,
                          iolink_device_time_us_fn time_us)
{
    if ((app == NULL) || (phy == NULL) || (time_us == NULL)) {
        return -1;
    }
    memset(app, 0, sizeof(*app));
    app->config.phy = phy;
    app->app_trait.self = (iolink_app_t*) app;
    app->app_trait.time_us = time_us;
    app->config.app = &app->app_trait;
    app->config.device_info = &identity;
    /* 三个标签是应用数据：以 STORAGE 表项提供，默认应用标签 "DefaultTag"。 */
    static const char default_tag[] = "DefaultTag";
    (void) memcpy(app->tags[0], default_tag, sizeof(default_tag) - 1U);
    app->tag_len[0] = (uint8_t) (sizeof(default_tag) - 1U);
    app->od_entries[0] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_APPLICATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "ApplicationSpecificTag", app->tags[0], &app->tag_len[0]);
    app->od_entries[1] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_FUNCTION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "FunctionTag", app->tags[1], &app->tag_len[1]);
    app->od_entries[2] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_LOCATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "LocationTag", app->tags[2], &app->tag_len[2]);
    app->od.entries = app->od_entries;
    app->od.count = 3U;
    app->config.od = &app->od;
    app->stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    app->stack.od_width = 2U;
    app->stack.pd_in_len = sizeof(app->input);
    app->stack.pd_out_len = 1;
    app->stack.min_cycle_time = 10;
    app->config.stack = &app->stack;
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
