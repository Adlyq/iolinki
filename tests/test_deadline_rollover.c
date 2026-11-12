/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include "iolinki/dll_ctx.h"
static uint64_t now;
static int wake_pending;
static uint64_t test_time_us(void* user)
{
    (void) user;
    return now;
}

static int wake(void* user)
{
    (void) user;
    int result = wake_pending;
    wake_pending = 0;
    return result;
}
int main(void)
{
    const iolink_phy_api_t phy = {.detect_wakeup = wake};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx.main, &ctx.isr, &phy);
    ctx.main.shared.time_us = test_time_us;
    now = ((uint64_t) UINT32_MAX - 100U) * 1000U;
    wake_pending = 1;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.shared.state == IOLINK_DLL_STATE_AWAITING_COMM);
    uint64_t deadline = now / 1000U + IOLINK_T_DSIO_MS;
    assert(ctx.main.dsio_deadline_ms == deadline);
    now += 150000U; /* 跨过发生回绕的毫秒 API，截止时间仍未到达 */
    iolink_dll_process(&ctx.main);
    assert(ctx.main.shared.state == IOLINK_DLL_STATE_AWAITING_COMM);
    now = (deadline + 1U) * 1000U;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.phy_mode == IOLINK_PHY_MODE_SIO);

    ctx.main.shared.state = IOLINK_DLL_STATE_FALLBACK;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.shared.last_activity_ms = 0;
    ctx.main.fallback_deadline_ms = now / 1000U + 50U;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.phy_mode == IOLINK_PHY_MODE_SDCI);
    now += 50000U;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.phy_mode == IOLINK_PHY_MODE_SIO);
    return 0;
}
