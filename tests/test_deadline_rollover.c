/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "dll_access.h"
static uint64_t now;
static int wake_pending;
static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return now;
}

static int wake(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    int result = wake_pending;
    wake_pending = 0;
    return result;
}

static void test_t_dsio_deadline_survives_ms_rollover(void** state)
{
    (void) state;
    const iolink_phy_api_t phy = {.detect_wakeup = wake};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx.main, &ctx.isr, &phy);
    ctx.main.shared.clock.time_us = test_time_us;
    now = ((uint64_t) UINT32_MAX - 100U) * 1000U;
    wake_pending = 1;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.state, IOLINK_DLL_STATE_AWAITING_COMM);
    /* 原条件为 now_ms > deadline_ms：绝对截止时间落在下一个毫秒起点。 */
    const uint64_t deadline = (now / 1000U + IOLINK_T_DSIO_MS + 1U) * 1000U;
    assert_int_equal(iolink_dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_DSIO), deadline);
    now += 150000U; /* 跨过发生回绕的毫秒 API，截止时间仍未到达 */
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.state, IOLINK_DLL_STATE_AWAITING_COMM);
    now = deadline - 1U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.shared.state, IOLINK_DLL_STATE_AWAITING_COMM);
    now = deadline;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.phy_mode, IOLINK_PHY_MODE_SIO);
}

static void test_t_fbd_deadline_expires_in_fallback(void** state)
{
    (void) state;
    const iolink_phy_api_t phy = {.detect_wakeup = wake};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx.main, &ctx.isr, &phy);
    ctx.main.shared.clock.time_us = test_time_us;
    now = ((uint64_t) UINT32_MAX - 100U) * 1000U;

    ctx.main.shared.state = IOLINK_DLL_STATE_FALLBACK;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.shared.last_activity_ms = 0;
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_T_FBD);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_FBD, (now / 1000U + 50U) * 1000U);
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.phy_mode, IOLINK_PHY_MODE_SDCI);
    now += 50000U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.phy_mode, IOLINK_PHY_MODE_SIO);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_t_dsio_deadline_survives_ms_rollover),
        cmocka_unit_test(test_t_fbd_deadline_expires_in_fallback),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
