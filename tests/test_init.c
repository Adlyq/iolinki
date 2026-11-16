/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_init.c
 * @brief IO-Link 协议栈初始化的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "test_helpers.h"
#include "test_null.h"

/* 本地 mock，避免与 CMocka 符号的链接问题 */
static int local_mock_phy_init(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return (int) mock();
}
static void local_mock_phy_set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}
static void local_mock_phy_set_baudrate(iolink_app_t* IOLINK_NULLABLE self,
                                        iolink_baudrate_t baudrate)
{
    (void) self;
    (void) baudrate;
}
static const iolink_phy_api_t local_phy_mock = {.init = local_mock_phy_init,
                                                .set_mode = local_mock_phy_set_mode,
                                                .set_baudrate = local_mock_phy_set_baudrate};

static const iolink_device_app_t g_test_app = {.self = NULL, .time_us = iolink_test_time_us};

static iolink_device_config_t make_device_config(void)
{
    /* config 只存指针，故用 static 存储。 */
    static iolink_stack_config_t stack;
    iolink_device_config_t config;
    (void) memset(&config, 0, sizeof(config));
    (void) memset(&stack, 0, sizeof(stack));
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    stack.od_width = 1U;
    config.phy = &local_phy_mock;
    config.stack = &stack;
    config.app = &g_test_app;
    config.device_info = iolink_test_identity();
    return config;
}

/* --- 测试 --- */

static void test_iolink_init_success(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config = make_device_config();

    will_return(local_mock_phy_init, 0);
    int result = iolink_device_init(&ctx, &config);
    assert_int_equal(result, 0);
}

static void test_iolink_init_fail_null(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    int result = iolink_device_init(&ctx, TEST_NULL(const iolink_device_config_t*));
    assert_int_not_equal(result, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_iolink_init_success),
        cmocka_unit_test(test_iolink_init_fail_null),
    };
    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
