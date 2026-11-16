/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device_requires_critical.c
 * @brief 设备初始化会拒绝库中弱符号的空操作临界区；
 *        本可执行文件有意不链接强覆盖实现。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"

static uint64_t test_time_us(iolink_app_t* self)
{
    (void) self;
    return 1000000ULL;
}

static const iolink_device_app_t test_app = {.self = NULL, .time_us = test_time_us};

static void test_device_init_critical_hook(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    iolink_stack_config_t stack;
    static iolink_phy_api_t phy;

    (void) memset(&config, 0, sizeof(config));
    (void) memset(&stack, 0, sizeof(stack));
    (void) memset(&phy, 0, sizeof(phy));
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.stack = &stack;
    config.phy = &phy;
    config.app = &test_app;

    /* 不含异步上下文的配置文件（SINGLE）从不引用
     * 临界区钩子，因此那里缺少 port 不再是致命错误。 */
#if IOLINK_NEED_CRITICAL
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_CRITICAL);
#else
    assert_int_not_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_CRITICAL);
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_init_critical_hook),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
