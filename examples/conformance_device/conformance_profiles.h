/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CONFORMANCE_PROFILES_H
#define CONFORMANCE_PROFILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/device_info.h"

/**
 * @file conformance_profiles.h
 * @brief Selectable test profiles for the conformance reference device.
 *
 * These profiles are test fixtures, not product identities. They let the same
 * generic iolinki library be exercised with different PD layouts and parameter
 * sets without compiling any device-specific constants into the library.
 */

typedef enum
{
    CONFORMANCE_PROFILE_FULL = 0,
    CONFORMANCE_PROFILE_CONFIG,
    CONFORMANCE_PROFILE_PDOUT_ONLY,
    CONFORMANCE_PROFILE_MINIMAL,
    CONFORMANCE_PROFILE_TYPE0
} conformance_profile_id_t;

typedef struct
{
    const char* name;
    const iolink_device_info_t* identity;
    iolink_stack_config_t stack;
    const iolink_ds_param_desc_t* ds_params;
    size_t ds_param_count;
    bool has_pd_in;
    bool has_pd_out;
} conformance_profile_t;

const conformance_profile_t* conformance_profile_get(conformance_profile_id_t id);
const conformance_profile_t* conformance_profile_from_name(const char* name);

#endif /* CONFORMANCE_PROFILES_H */
