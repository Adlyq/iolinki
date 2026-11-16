/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include "iolinki/device.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static int noop_user(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
}

static void test_device_context_api_initializes_two_configs(void** state)
{
    (void) state;
    iolink_device_ctx_t dev_a;
    iolink_device_ctx_t dev_b;
    static iolink_phy_api_t phy = {.init = noop_user};
    static const iolink_device_app_t app = {.time_us = iolink_test_time_us};
    static const iolink_stack_config_t stack_a = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
        .od_width = 2,
        .min_cycle_time = 10U,
        .pd_in_len = 1U,
        .pd_out_len = 0U,
        .t_pd_us = 0U,
    };
    static const iolink_stack_config_t stack_b = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
        .od_width = 2,
        .min_cycle_time = 10U,
        .pd_in_len = 2U,
        .pd_out_len = 2U,
        .t_pd_us = 0U,
    };
    iolink_device_config_t cfg_a = {
        .phy = &phy,
        .app = &app,
        .stack = &stack_a,
        .device_info = iolink_test_identity(),
    };
    iolink_device_config_t cfg_b = {
        .phy = &phy,
        .app = &app,
        .stack = &stack_b,
        .device_info = iolink_test_identity(),
    };

    assert_int_equal(iolink_device_init(&dev_a, &cfg_a), 0);
    assert_int_equal(iolink_device_init(&dev_b, &cfg_b), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev_a), 1U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev_a), 0U);
    assert_int_equal(iolink_device_get_pd_in_len(&dev_b), 2U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev_b), 2U);
}

static void test_parameter_contexts_keep_writable_tags_isolated(void** state)
{
    (void) state;
    iolink_device_info_ctx_t info_a;
    iolink_device_info_ctx_t info_b;
    iolink_test_params_t params_a;
    iolink_test_params_t params_b;
    const uint8_t tag_a[] = "DeviceA";
    const uint8_t tag_b[] = "DeviceB";
    uint8_t out_a[32] = {0};
    uint8_t out_b[32] = {0};

    assert_int_equal(iolink_device_info_ctx_init(&info_a, iolink_test_identity()), 0);
    assert_int_equal(iolink_device_info_ctx_init(&info_b, iolink_test_identity()), 0);
    test_params_init(&params_a, &info_a);
    test_params_init(&params_b, &info_b);

    assert_int_equal(
        test_params_write(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, tag_a, sizeof(tag_a) - 1U)
            .code,
        0);
    assert_int_equal(
        test_params_write(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, tag_b, sizeof(tag_b) - 1U)
            .code,
        0);
    const iolink_result_t read_a =
        test_params_read(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, out_a, sizeof(out_a));
    const iolink_result_t read_b =
        test_params_read(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, out_b, sizeof(out_b));
    assert_int_equal(read_a.code, 0);
    assert_int_equal(read_a.len, sizeof(tag_a) - 1U);
    assert_int_equal(read_b.code, 0);
    assert_int_equal(read_b.len, sizeof(tag_b) - 1U);
    assert_memory_equal(out_a, tag_a, sizeof(tag_a) - 1U);
    assert_memory_equal(out_b, tag_b, sizeof(tag_b) - 1U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_context_api_initializes_two_configs),
        cmocka_unit_test(test_parameter_contexts_keep_writable_tags_isolated),
    };
    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
