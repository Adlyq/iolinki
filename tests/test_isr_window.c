/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * Phase 4b: with a hardware Timer B hook installed, the main service window
 * only runs after iolink_dll_on_stack_tick() authorises it.
 */

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

    /* Install a PHY with the Timer B hook so the gate is active. */
    iolink_phy_api_t gated = g_phy_mock;
    gated.stack_timer_start = timer_b_start;
    dev.ctx.dll.phy = &gated;

    iolink_device_set_reset_handler(&dev.ctx, reset_cb);
    dev.ctx.dll.isdu.reset_pending = true;
    reset_calls = 0;

    /* No tick yet: the main window must not run. */
    iolink_device_process(&dev.ctx);
    assert_int_equal(reset_calls, 0);
    assert_true(dev.ctx.dll.isdu.reset_pending);

    /* One tick authorises exactly one window. */
    iolink_dll_on_stack_tick(&dev.ctx.dll);
    iolink_device_process(&dev.ctx);
    assert_int_equal(reset_calls, 1);

    /* The authorisation is consumed; a second pass without a tick does nothing. */
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
