/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CONFORMANCE_DEVICE_H
#define CONFORMANCE_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "conformance_profiles.h"
#include "conformance_storage.h"
#include "iolinki/device.h"
#include "iolinki/events.h"

#define CONFORMANCE_CONFIG1_INDEX 0x0100U
#define CONFORMANCE_CONFIG2_INDEX 0x0101U
#define CONFORMANCE_CONFIG3_INDEX 0x0102U
#define CONFORMANCE_CONFIG4_INDEX0 0x0110U

#define CONFORMANCE_CONFIG1_MAX 64U
#define CONFORMANCE_CONFIG3_MAX 200U
#define CONFORMANCE_CONFIG4_SIZE 32U
#define CONFORMANCE_CONFIG4_COUNT 4U

/**
 * @brief Application-visible reset reason for the reference integration.
 *
 * These values are observation hooks owned by the example application; they
 * are not part of the iolinki library API.
 */
typedef enum
{
    CONFORMANCE_RESET_POWER_ON = 0,
    CONFORMANCE_RESET_DEVICE,
    CONFORMANCE_RESET_APPLICATION,
    CONFORMANCE_RESET_FACTORY,
    CONFORMANCE_RESET_BACK_TO_BOX
} conformance_reset_reason_t;

/** @brief Startup/reset observation counters used by the Test Spec runners. */
typedef struct
{
    uint32_t startup_count;           /**< Number of times the application was initialised. */
    uint32_t device_reset_count;      /**< SystemCommand 0x80 observations. */
    uint32_t application_reset_count; /**< SystemCommand 0x81 observations. */
    uint32_t factory_reset_count;     /**< Application-level 0x82 completion hooks. */
    uint32_t back_to_box_count;       /**< Application-level 0x83 completion hooks. */
    conformance_reset_reason_t last_reset_reason; /**< Most recent observed transition. */
    bool valid;                                   /**< Counters have been initialised. */
} conformance_reset_observation_t;

typedef struct
{
    iolink_device_ctx_t device;
    const conformance_profile_t* profile;
    conformance_file_storage_t storage;
    iolink_device_config_t config;

    uint8_t input[32];
    uint8_t output[32];
    uint16_t counter;
    bool button;
    bool pd_valid;

    uint8_t config1[CONFORMANCE_CONFIG1_MAX];
    size_t config1_len;
    uint16_t config2;
    uint8_t config3[CONFORMANCE_CONFIG3_MAX];
    size_t config3_len;
    uint8_t config4[CONFORMANCE_CONFIG4_COUNT][CONFORMANCE_CONFIG4_SIZE];
    size_t config4_len[CONFORMANCE_CONFIG4_COUNT];

    conformance_reset_observation_t reset_observation;
    uint32_t local_change_count;
    uint8_t default_config1[CONFORMANCE_CONFIG1_MAX];
    uint16_t default_config2;
    uint8_t default_config3[CONFORMANCE_CONFIG3_MAX];
    uint8_t default_config4[CONFORMANCE_CONFIG4_COUNT][CONFORMANCE_CONFIG4_SIZE];
} conformance_device_t;

int conformance_device_init(conformance_device_t* app, const conformance_profile_t* profile,
                            const iolink_phy_api_t* phy, const char* nvm_path);
int conformance_device_power_cycle(conformance_device_t* app, const iolink_phy_api_t* phy,
                                   const char* nvm_path);
void conformance_device_close(conformance_device_t* app);
int conformance_device_tick(conformance_device_t* app, bool button);
void conformance_device_process(conformance_device_t* app);
int conformance_device_inject_event(conformance_device_t* app, uint16_t code,
                                    iolink_event_type_t type);
int conformance_device_clear_event(conformance_device_t* app, uint16_t code);
void conformance_device_set_pd_valid(conformance_device_t* app, bool valid);
int conformance_device_local_change(conformance_device_t* app);
int conformance_device_read_param(conformance_device_t* app, uint16_t index, uint8_t subindex,
                                  uint8_t* output, size_t* output_length);
int conformance_device_write_param(conformance_device_t* app, uint16_t index, uint8_t subindex,
                                   const uint8_t* input, size_t length);
void conformance_device_note_factory_reset(conformance_device_t* app);
void conformance_device_note_back_to_box(conformance_device_t* app);
const conformance_reset_observation_t* conformance_device_reset_observation(
    const conformance_device_t* app);
bool conformance_device_is_default_state(const conformance_device_t* app);

#endif /* CONFORMANCE_DEVICE_H */
