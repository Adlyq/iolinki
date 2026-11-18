/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * 阶段 4b：在安装硬件 Timer B 钩子的情况下，主服务窗口
 * 只有在 iolink_dll_on_stack_tick() 授权之后才会执行。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include "iolinki/device.h"
#include "dll_access.h"
#include "test_helpers.h"

static int reset_calls;

static void reset_cb(iolink_app_t* IOLINK_NULLABLE self, iolink_reset_type_t type)
{
    (void) self;
    (void) type;
    reset_calls++;
}

static void timer_b_start(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
}

static void test_main_window_gated_by_tick(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    iolink_test_device_t dev;
    static const iolink_device_app_t app = {.time_us = iolink_test_time_us, .on_reset = reset_cb};
    assert_int_equal(iolink_test_device_init(&dev, NULL, &app), 0);

    /* 安装带 Timer B 钩子的 PHY，使门控生效。 */
    iolink_phy_api_t gated = g_phy_mock;
    gated.stack_timer_start = timer_b_start;
    test_dll_shared(&dev.ctx)->phy = &gated;

    test_dll_shared(&dev.ctx)->isdu.reset_pending = true;
    reset_calls = 0;

    /* 尚无 tick：主窗口不得执行。 */
    iolink_device_process(&dev.ctx);
    assert_int_equal(reset_calls, 0);
    assert_true(test_dll_shared(&dev.ctx)->isdu.reset_pending);

    /* 一次 tick 只授权一个窗口。 */
    iolink_dll_on_stack_tick(&test_dll(&dev.ctx)->shared);
    iolink_device_process(&dev.ctx);
    assert_int_equal(reset_calls, 1);

    /* 授权已被消耗；没有 tick 的第二轮不会做任何事。 */
    iolink_device_process(&dev.ctx);
    assert_int_equal(reset_calls, 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_main_window_gated_by_tick),
    };
    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
