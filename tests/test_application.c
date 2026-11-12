/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_application.c
 * @brief 应用层过程数据 API 的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/application.h"
#include "test_helpers.h"

static void test_pd_input_update_flow(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    uint8_t data[] = {0xAA, 0xBB};
    int res = iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true);
    assert_int_equal(res, 0);

    /* 通过输出读取检查内部状态（作为简单代理） */
    /* 注意：PD 输出是从主站读入的，因此这不是直接的镜像。
       这里只验证 API 不会崩溃。 */
}

static void test_pd_output_read_flow(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    uint8_t buf[16];
    int res = iolink_device_pd_output_read(&dev.ctx, buf, sizeof(buf));
    /* 初始状态应为长度 0 或已清零 */
    assert_int_equal(res, 0);
}

static int g_cb_startup;
static int g_cb_preoperate;
static int g_cb_operate;
static int g_cb_pd_output;
static int g_cb_state;
static void* g_expected_user;

static void cb_startup(void* user)
{
    assert_ptr_equal(user, g_expected_user);
    g_cb_startup++;
}
static void cb_preoperate(void* user)
{
    assert_ptr_equal(user, g_expected_user);
    g_cb_preoperate++;
}
static void cb_operate(void* user)
{
    assert_ptr_equal(user, g_expected_user);
    g_cb_operate++;
}
static void cb_pd_output(void* user, uint8_t* data, uint8_t len)
{
    assert_ptr_equal(user, g_expected_user);
    (void) data;
    (void) len;
    g_cb_pd_output++;
}
static void cb_state_change(void* user, iolink_dll_state_t state)
{
    assert_ptr_equal(user, g_expected_user);
    (void) state;
    g_cb_state++;
}

static void test_app_callbacks_lifecycle(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    g_cb_startup = 0;
    g_cb_preoperate = 0;
    g_cb_operate = 0;
    g_cb_pd_output = 0;
    g_cb_state = 0;

    static const iolink_app_callbacks_t cbs = {
        .on_startup = cb_startup,
        .on_preoperate = cb_preoperate,
        .on_operate = cb_operate,
        .on_pd_output = cb_pd_output,
        .on_state_change = cb_state_change,
    };

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, &cbs), 0);

    /* 在 init 之前注册 -> 已宣告初始 STARTUP。init 时未配置用户
     * （辅助函数自建配置），因此 init 阶段的 on_startup
     * 收到 NULL；下面的迁移钩子拿到哨兵值。 */
    assert_true(g_cb_startup >= 1);

    static int user_sentinel;
    g_expected_user = &user_sentinel;
    dev.cfg.user = g_expected_user;

    move_to_operate_ctx(&dev.ctx);
    assert_true(g_cb_state >= 1);

    /* PREOPERATE 在握手过程中被遍历；即使该迁移是瞬态的，
       状态变化钩子也会触发。 */
    assert_true(g_cb_preoperate >= 1);

    if (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_OPERATE) {
        assert_true(g_cb_operate >= 1);
        assert_true(g_cb_pd_output >= 1);
    }

    /* 在用户已配置的情况下重新初始化：init 阶段的
     * on_startup() 也必须把用户传进去（不只是迁移钩子）。
     * 不再调用 setup_mock_phy()：它的无限期望桶
     * 足以满足两次 init 的 PHY 流量。 */
    g_cb_startup = 0;
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), 0);
    assert_true(g_cb_startup >= 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_update_flow),
        cmocka_unit_test(test_pd_output_read_flow),
        cmocka_unit_test(test_app_callbacks_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
