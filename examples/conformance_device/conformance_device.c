/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "conformance_device.h"

#include <string.h>

#include "iolinki/protocol.h"

static int copy_out(uint8_t* output, size_t* output_length, const uint8_t* src, size_t len)
{
    if ((output == NULL) || (output_length == NULL)) {
        return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    }
    if (*output_length < len) {
        return IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN;
    }
    if (len > 0U) {
        (void) memcpy(output, src, len);
    }
    *output_length = len;
    return 0;
}

static int check_length(size_t length, size_t minimum, size_t maximum)
{
    if (length > maximum) {
        return IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN;
    }
    if (length < minimum) {
        return IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN;
    }
    return 0;
}

static uint8_t conformance_vendor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                                          const uint8_t* input, size_t length, uint8_t* output,
                                          size_t* output_length)
{
    conformance_device_t* app = (conformance_device_t*) user;
    if (app == NULL) {
        return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    }
    if (subindex != 0U) {
        return IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
    }

    if (index == CONFORMANCE_CONFIG1_INDEX) {
        if (write) {
            const int err = check_length(length, CONFORMANCE_CONFIG1_MAX, CONFORMANCE_CONFIG1_MAX);
            if (err != 0) {
                return (uint8_t) err;
            }
            (void) memcpy(app->config1, input, CONFORMANCE_CONFIG1_MAX);
            app->config1_len = CONFORMANCE_CONFIG1_MAX;
            return 0;
        }
        return (uint8_t) copy_out(output, output_length, app->config1, app->config1_len);
    }
    if (index == CONFORMANCE_CONFIG2_INDEX) {
        if (write) {
            const int err = check_length(length, 2U, 2U);
            if (err != 0) {
                return (uint8_t) err;
            }
            app->config2 = (uint16_t) (((uint16_t) input[0] << 8) | input[1]);
            return 0;
        }
        const uint8_t value[2] = {(uint8_t) (app->config2 >> 8), (uint8_t) app->config2};
        return (uint8_t) copy_out(output, output_length, value, sizeof(value));
    }
    if (index == CONFORMANCE_CONFIG3_INDEX) {
        if (write) {
            const int err = check_length(length, 1U, CONFORMANCE_CONFIG3_MAX);
            if (err != 0) {
                return (uint8_t) err;
            }
            (void) memcpy(app->config3, input, length);
            app->config3_len = length;
            return 0;
        }
        return (uint8_t) copy_out(output, output_length, app->config3, app->config3_len);
    }
    if ((index >= CONFORMANCE_CONFIG4_INDEX0) &&
        (index < (CONFORMANCE_CONFIG4_INDEX0 + CONFORMANCE_CONFIG4_COUNT))) {
        const size_t slot = (size_t) (index - CONFORMANCE_CONFIG4_INDEX0);
        if (write) {
            const int err =
                check_length(length, CONFORMANCE_CONFIG4_SIZE, CONFORMANCE_CONFIG4_SIZE);
            if (err != 0) {
                return (uint8_t) err;
            }
            (void) memcpy(app->config4[slot], input, CONFORMANCE_CONFIG4_SIZE);
            app->config4_len[slot] = CONFORMANCE_CONFIG4_SIZE;
            return 0;
        }
        return (uint8_t) copy_out(output, output_length, app->config4[slot],
                                  app->config4_len[slot]);
    }
    return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
}

static void conformance_defaults(conformance_device_t* app)
{
    (void) memset(app->config1, 0x49, sizeof(app->config1));
    app->config1_len = sizeof(app->config1);
    app->config2 = 0U;
    for (size_t i = 0U; i < sizeof(app->config3); i++) {
        app->config3[i] = (uint8_t) (0x30U + (i & 0x07U));
    }
    app->config3_len = sizeof(app->config3);
    for (size_t i = 0U; i < CONFORMANCE_CONFIG4_COUNT; i++) {
        (void) memset(app->config4[i], (int) (0x40U + i), sizeof(app->config4[i]));
        app->config4_len[i] = sizeof(app->config4[i]);
    }
}

static void conformance_capture_defaults(conformance_device_t* app)
{
    (void) memcpy(app->default_config1, app->config1, sizeof(app->default_config1));
    app->default_config2 = app->config2;
    (void) memcpy(app->default_config3, app->config3, sizeof(app->default_config3));
    for (size_t i = 0U; i < CONFORMANCE_CONFIG4_COUNT; i++) {
        (void) memcpy(app->default_config4[i], app->config4[i], sizeof(app->default_config4[i]));
    }
}

bool conformance_device_is_default_state(const conformance_device_t* app)
{
    if (app == NULL) {
        return false;
    }
    if ((app->config1_len != sizeof(app->default_config1)) ||
        (app->config2 != app->default_config2) ||
        (app->config3_len != sizeof(app->default_config3))) {
        return false;
    }
    if (memcmp(app->config1, app->default_config1, sizeof(app->default_config1)) != 0) {
        return false;
    }
    if (memcmp(app->config3, app->default_config3, sizeof(app->default_config3)) != 0) {
        return false;
    }
    for (size_t i = 0U; i < CONFORMANCE_CONFIG4_COUNT; i++) {
        if ((app->config4_len[i] != sizeof(app->default_config4[i])) ||
            (memcmp(app->config4[i], app->default_config4[i], sizeof(app->default_config4[i])) !=
             0)) {
            return false;
        }
    }
    return true;
}

static void conformance_observe_reset(conformance_device_t* app, conformance_reset_reason_t reason)
{
    if (app == NULL) {
        return;
    }
    app->reset_observation.valid = true;
    app->reset_observation.last_reset_reason = reason;
    switch (reason) {
        case CONFORMANCE_RESET_DEVICE:
            app->reset_observation.device_reset_count++;
            break;
        case CONFORMANCE_RESET_APPLICATION:
            app->reset_observation.application_reset_count++;
            break;
        case CONFORMANCE_RESET_FACTORY:
            app->reset_observation.factory_reset_count++;
            break;
        case CONFORMANCE_RESET_BACK_TO_BOX:
            app->reset_observation.back_to_box_count++;
            break;
        case CONFORMANCE_RESET_POWER_ON:
        default:
            break;
    }
}

static conformance_device_t* g_reset_observer = NULL;

static void conformance_reset_handler(iolink_reset_type_t type)
{
    conformance_device_t* app = g_reset_observer;
    if (app == NULL) {
        return;
    }
    if (type == IOLINK_RESET_APPLICATION) {
        /* An application reset restores the application parameter defaults.
         * Identification data and the writable tags are not touched. */
        conformance_defaults(app);
        conformance_observe_reset(app, CONFORMANCE_RESET_APPLICATION);
    }
    else {
        /* A device reset restarts communication but preserves parameters. */
        conformance_observe_reset(app, CONFORMANCE_RESET_DEVICE);
    }
}

int conformance_device_init(conformance_device_t* app, const conformance_profile_t* profile,
                            const iolink_phy_api_t* phy, const char* nvm_path)
{
    if ((app == NULL) || (profile == NULL) || (phy == NULL)) {
        return -1;
    }
    (void) memset(app, 0, sizeof(*app));
    app->profile = profile;
    app->pd_valid = true;
    app->reset_observation.valid = true;
    app->reset_observation.startup_count = 1U;
    app->reset_observation.last_reset_reason = CONFORMANCE_RESET_POWER_ON;
    conformance_defaults(app);
    conformance_capture_defaults(app);

    if (conformance_file_storage_init(&app->storage, nvm_path) != 0) {
        return -1;
    }

    app->config.phy = *phy;
    app->config.stack = profile->stack;
    app->config.device_info = profile->identity;
    app->config.storage = &app->storage.api;
    app->config.vendor_service = conformance_vendor_service;
    app->config.user = app;

    const int rc = iolink_device_init(&app->device, &app->config);
    if (rc != 0) {
        conformance_file_storage_close(&app->storage);
        return rc;
    }
    iolink_device_set_reset_handler(&app->device, conformance_reset_handler);
    g_reset_observer = app;

#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&app->device);
    if ((ds != NULL) && (profile->ds_params != NULL) && (profile->ds_param_count > 0U)) {
        iolink_ds_set_param_desc(ds, profile->ds_params, profile->ds_param_count);
    }
#endif
    return 0;
}

int conformance_device_power_cycle(conformance_device_t* app, const iolink_phy_api_t* phy,
                                   const char* nvm_path)
{
    if (app == NULL) {
        return -1;
    }
    const conformance_profile_t* profile = app->profile;
    const conformance_reset_observation_t observation = app->reset_observation;
    const uint32_t local_change_count = app->local_change_count;

    conformance_device_close(app);
    const int rc = conformance_device_init(app, profile, phy, nvm_path);
    if (rc == 0) {
        app->reset_observation = observation;
        app->reset_observation.valid = true;
        app->reset_observation.startup_count++;
        app->reset_observation.last_reset_reason = CONFORMANCE_RESET_POWER_ON;
        app->local_change_count = local_change_count;
    }
    return rc;
}

void conformance_device_close(conformance_device_t* app)
{
    if (app != NULL) {
        if (g_reset_observer == app) {
            g_reset_observer = NULL;
        }
        conformance_file_storage_close(&app->storage);
    }
}

int conformance_device_tick(conformance_device_t* app, bool button)
{
    if (app == NULL) {
        return -1;
    }
    app->counter++;
    app->input[0] = button ? 1U : 0U;
    app->input[1] = (uint8_t) (app->counter >> 8);
    app->input[2] = (uint8_t) app->counter;
    if (button && !app->button) {
#if IOLINK_FEATURE_EVENTS
        iolink_event_trigger(iolink_device_get_events_ctx(&app->device), 0x8CA0U,
                             IOLINK_EVENT_TYPE_NOTIFICATION);
#endif
    }
    app->button = button;

    if (app->profile->has_pd_in) {
        const size_t len = app->profile->stack.pd_in_len;
        if (iolink_device_pd_input_update(&app->device, app->input, len, app->pd_valid) != 0) {
            return -1;
        }
    }
    if (app->profile->has_pd_out) {
        const size_t len = app->profile->stack.pd_out_len;
        if (iolink_device_pd_output_read(&app->device, app->output, len) >= 0) {
            /* Application consumes output[0] as the LED state. */
        }
    }
    return 0;
}

void conformance_device_process(conformance_device_t* app)
{
    if (app == NULL) {
        return;
    }
    iolink_device_process_isr(&app->device);
    while (app->device.dll.isr_tx_active != 0U) {
        iolink_device_process_isr(&app->device);
    }
    iolink_device_process(&app->device);
    while (app->device.dll.isr_tx_active != 0U) {
        iolink_device_process_isr(&app->device);
    }
}

int conformance_device_inject_event(conformance_device_t* app, uint16_t code,
                                    iolink_event_type_t type)
{
    if (app == NULL) {
        return -1;
    }
#if IOLINK_FEATURE_EVENTS
    iolink_event_trigger(iolink_device_get_events_ctx(&app->device), code, type);
    return 0;
#else
    (void) code;
    (void) type;
    return -1;
#endif
}

int conformance_device_clear_event(conformance_device_t* app, uint16_t code)
{
    if (app == NULL) {
        return -1;
    }
#if IOLINK_FEATURE_EVENTS
    iolink_event_clear(iolink_device_get_events_ctx(&app->device), code);
    return 0;
#else
    (void) code;
    return -1;
#endif
}

void conformance_device_set_pd_valid(conformance_device_t* app, bool valid)
{
    if (app != NULL) {
        app->pd_valid = valid;
    }
}

int conformance_device_local_change(conformance_device_t* app)
{
    if (app == NULL) {
        return -1;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&app->device);
    if (ds == NULL) {
        return -1;
    }
    /* Emulate a local teach-in/panel edit: change an application parameter,
     * then report the local parameter set change to the DS engine. */
    app->config2 = (uint16_t) (app->config2 + 1U);
    app->local_change_count++;
    iolink_ds_request_upload(ds);
    return 0;
#else
    return -1;
#endif
}

void conformance_device_note_factory_reset(conformance_device_t* app)
{
    if (app == NULL) {
        return;
    }
    conformance_defaults(app);
    conformance_observe_reset(app, CONFORMANCE_RESET_FACTORY);
}

void conformance_device_note_back_to_box(conformance_device_t* app)
{
    if (app == NULL) {
        return;
    }
    conformance_defaults(app);
    conformance_observe_reset(app, CONFORMANCE_RESET_BACK_TO_BOX);
}

const conformance_reset_observation_t* conformance_device_reset_observation(
    const conformance_device_t* app)
{
    return (app != NULL) ? &app->reset_observation : NULL;
}

int conformance_device_read_param(conformance_device_t* app, uint16_t index, uint8_t subindex,
                                  uint8_t* output, size_t* output_length)
{
    if ((app == NULL) || (output_length == NULL)) {
        return -1;
    }
    return (int) conformance_vendor_service(app, index, subindex, false, NULL, 0U, output,
                                            output_length);
}

int conformance_device_write_param(conformance_device_t* app, uint16_t index, uint8_t subindex,
                                   const uint8_t* input, size_t length)
{
    if (app == NULL) {
        return -1;
    }
    return (int) conformance_vendor_service(app, index, subindex, true, input, length, NULL, NULL);
}
