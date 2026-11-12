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
#include "iolinki/dll.h"
#include "test_helpers.h"

static int reset_calls;

static void reset_cb(iolink_reset_type_t type)
{
    (void) type;
    reset_calls++;
}

static void timer_b_start(void* user)
{
    (void) user;
}

static void test_main_window_gated_by_tick(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    /* 安装带 Timer B 钩子的 PHY，使门控生效。 */
    iolink_phy_api_t gated = g_phy_mock;
    gated.stack_timer_start = timer_b_start;
    dev.ctx.dll.phy = &gated;

    iolink_device_set_reset_handler(&dev.ctx, reset_cb);
    dev.ctx.dll.isdu.reset_pending = true;
    reset_calls = 0;

    /* 尚无 tick：主窗口不得执行。 */
    iolink_device_process(&dev.ctx);
    assert_int_equal(reset_calls, 0);
    assert_true(dev.ctx.dll.isdu.reset_pending);

    /* 一次 tick 只授权一个窗口。 */
    iolink_dll_on_stack_tick(&dev.ctx.dll);
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
    return cmocka_run_group_tests(tests, NULL, NULL);
}
