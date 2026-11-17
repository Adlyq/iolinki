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
    .revision_id = IOLINK_REVISION_ID_1_1,
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
    .revision_id = IOLINK_REVISION_ID_1_1,
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
    .revision_id = IOLINK_REVISION_ID_1_1,
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
    .revision_id = IOLINK_REVISION_ID_1_1,
};

static const conformance_profile_t g_profiles[] = {
    {
        .name = "full",
        .identity = &g_full_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                  .od_width = 2,
                  .pd_in_len = 3U,
                  .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .has_pd_in = true,
        .has_pd_out = true,
    },
    {
        .name = "config",
        .identity = &g_config_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                  .od_width = 2,
                  .pd_in_len = 1U,
                  .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .has_pd_in = true,
        .has_pd_out = true,
    },
    {
        .name = "pdout-only",
        .identity = &g_pdout_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_3,
                  .od_width = 1,
                  .pd_in_len = 0U,
                  .pd_out_len = 1U,
                  .min_cycle_time = 10U},
        .has_pd_in = false,
        .has_pd_out = true,
    },
    {
        .name = "minimal",
        .identity = &g_minimal_identity,
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_0,
                  .od_width = 1,
                  .pd_in_len = 0U,
                  .pd_out_len = 0U,
                  .min_cycle_time = 10U},
        .has_pd_in = false,
        .has_pd_out = false,
    },
    {
        .name = "type0",
        .identity = &g_full_identity,
        /* 已知夹具偏差：TYPE_0 按 Table A.10 不携带 PD，但 Linux master runner
         * 与 IODD oracle 复用了 full profile 的 DPP1 期望（PD 3/1、
         * MinCycleTime 3000），因此这里保留 3/1。库把配置长度当作常量，
         * 不对 TYPE_0 做特判；修正夹具（改用 full profile + T0-read 兼容位，
         * 或把这里改成 0/0 并同步期望）是独立任务。 */
        .stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_0,
                  .od_width = 1,
                  .pd_in_len = 3U,
                  .pd_out_len = 1U,
                  .min_cycle_time = 10U},
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
