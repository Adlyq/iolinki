/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "conformance_profiles.h"

#include <string.h>

#define CONFORMANCE_CONFIG1_INDEX 0x0100U
#define CONFORMANCE_CONFIG2_INDEX 0x0101U
#define CONFORMANCE_CONFIG3_INDEX 0x0102U
#define CONFORMANCE_CONFIG4_INDEX0 0x0110U

static const iolink_device_info_t g_full_identity = {
    .vendor_name = "iolinki conformance reference",
    .vendor_text = "Synthetic test profile; not a product identity",
    .product_name = "Conformance reference device",
    .product_id = "conformance-full",
    .product_text = "Test fixture for IO-Link Test Specification coverage",
    .serial_number = "CONF-0001",
    .hardware_revision = "host",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5680U,
    .min_cycle_time = 10U,
    .revision_id = 0x0111U,
};

static const iolink_device_info_t g_config_identity = {
    .vendor_name = "iolinki conformance reference",
    .vendor_text = "Synthetic test profile; not a product identity",
    .product_name = "Conformance config profile",
    .product_id = "conformance-config",
    .product_text = "Config1/2/3/4 and fixed-length test parameters",
    .serial_number = "CONF-0002",
    .hardware_revision = "host",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5681U,
    .min_cycle_time = 10U,
    .revision_id = 0x0111U,
};

static const iolink_device_info_t g_pdout_identity = {
    .vendor_name = "iolinki conformance reference",
    .vendor_text = "Synthetic test profile; not a product identity",
    .product_name = "Conformance PDOut-only profile",
    .product_id = "conformance-pdout",
    .product_text = "No process data input; one output octet",
    .serial_number = "CONF-0003",
    .hardware_revision = "host",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5682U,
    .min_cycle_time = 10U,
    .revision_id = 0x0111U,
};

static const iolink_device_info_t g_minimal_identity = {
    .vendor_name = "iolinki conformance reference",
    .vendor_text = "Synthetic test profile; not a product identity",
    .product_name = "Conformance minimal profile",
    .product_id = "conformance-minimal",
    .product_text = "TYPE_0, no process data",
    .serial_number = "CONF-0004",
    .hardware_revision = "host",
    .firmware_revision = "1",
    .vendor_id = 1234U,
    .device_id = 5683U,
    .min_cycle_time = 10U,
    .revision_id = 0x0111U,
};

static const iolink_ds_param_desc_t g_full_ds_params[] = {
    {0x0018U, 0U, 32U}, /* ApplicationSpecificTag */
    {0x0019U, 0U, 32U}, /* FunctionTag */
    {0x001AU, 0U, 32U}, /* LocationTag */
    {CONFORMANCE_CONFIG1_INDEX, 0U, 64U},
    {CONFORMANCE_CONFIG2_INDEX, 0U, 2U},
    {CONFORMANCE_CONFIG3_INDEX, 0U, 200U},
    {CONFORMANCE_CONFIG4_INDEX0 + 0U, 0U, 32U},
    {CONFORMANCE_CONFIG4_INDEX0 + 1U, 0U, 32U},
    {CONFORMANCE_CONFIG4_INDEX0 + 2U, 0U, 32U},
    {CONFORMANCE_CONFIG4_INDEX0 + 3U, 0U, 32U},
};

static const iolink_ds_param_desc_t g_minimal_ds_params[] = {
    {0x0018U, 0U, 32U},
    {0x0019U, 0U, 32U},
    {0x001AU, 0U, 32U},
};

static const conformance_profile_t g_profiles[] = {
    {
        .name = "full",
        .identity = &g_full_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 3U, .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .ds_params = g_full_ds_params,
        .ds_param_count = sizeof(g_full_ds_params) / sizeof(g_full_ds_params[0]),
        .has_pd_in = true,
        .has_pd_out = true,
    },
    {
        .name = "config",
        .identity = &g_config_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1U, .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .ds_params = g_full_ds_params,
        .ds_param_count = sizeof(g_full_ds_params) / sizeof(g_full_ds_params[0]),
        .has_pd_in = true,
        .has_pd_out = true,
    },
    {
        .name = "pdout-only",
        .identity = &g_pdout_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_3, .pd_in_len = 0U, .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .ds_params = g_minimal_ds_params,
        .ds_param_count = sizeof(g_minimal_ds_params) / sizeof(g_minimal_ds_params[0]),
        .has_pd_in = false,
        .has_pd_out = true,
    },
    {
        .name = "minimal",
        .identity = &g_minimal_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0U, .pd_out_len = 0U,
                  .min_cycle_time = 10U},
        .ds_params = g_minimal_ds_params,
        .ds_param_count = sizeof(g_minimal_ds_params) / sizeof(g_minimal_ds_params[0]),
        .has_pd_in = false,
        .has_pd_out = false,
    },
    {
        .name = "type0",
        .identity = &g_full_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 3U, .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .ds_params = g_full_ds_params,
        .ds_param_count = sizeof(g_full_ds_params) / sizeof(g_full_ds_params[0]),
        .has_pd_in = false,
        .has_pd_out = false,
    },
};

const conformance_profile_t* conformance_profile_get(conformance_profile_id_t id)
{
    if ((size_t) id >= (sizeof(g_profiles) / sizeof(g_profiles[0]))) {
        return NULL;
    }
    return &g_profiles[id];
}

const conformance_profile_t* conformance_profile_from_name(const char* name)
{
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < (sizeof(g_profiles) / sizeof(g_profiles[0])); i++) {
        if (strcmp(name, g_profiles[i].name) == 0) {
            return &g_profiles[i];
        }
    }
    return NULL;
}
