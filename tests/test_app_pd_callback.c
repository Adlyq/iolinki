/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * PD 输入路径（同一构建内的运行时选择）：
 *  - 应用不提供 on_pd_input 时走轮询 API iolink_device_pd_input_update()；
 *  - 提供 on_pd_input 时能力位 IOLINK_CAP_PD_IN_CALLBACK 置位，DLL 每
 *    OPERATE 周期调用回调，轮询 API 被拒绝（ERR_PD_INPUT）而不是静默
 *    覆盖回调输出；
 *  - 构建档位（capability.h）与实例能力位一致。
 *
 * 两条路径现在编进同一个二进制，不再需要独立的 fixture 库。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/capability.h"
#include "iolinki/device.h"

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t test_time_us(void* user)
{
    (void) user;
    return 1000000ULL;
}

static int phy_init(void* user)
{
    (void) user;
    return 0;
}

static void phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static const iolink_phy_api_t g_phy = {.init = phy_init,
                                       .set_mode = phy_set_mode,
                                       .set_baudrate = phy_set_baudrate,
                                       .detect_wakeup = NULL};

static int g_pd_calls;
static uint8_t g_pd_pattern;
static void* g_expected_user;

static bool on_pd_input(void* user, uint8_t* data, uint8_t len)
{
    assert_ptr_equal(user, g_expected_user);
    g_pd_calls++;
    (void) memset(data, g_pd_pattern, len);
    return true;
}

static const iolink_app_callbacks_t g_cbs = {.on_pd_input = on_pd_input};

static void make_config(iolink_device_config_t* cfg, const iolink_app_callbacks_t* cbs)
{
    (void) memset(cfg, 0, sizeof(*cfg));
    cfg->phy = g_phy;
    cfg->stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    cfg->stack.pd_in_len = 2U;
    cfg->stack.pd_out_len = 0U;
    cfg->app_callbacks = cbs;
    cfg->time_us = test_time_us;
}

static void test_polling_path_without_callback(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;
    const uint8_t data[2] = {0x11U, 0x22U};

    make_config(&cfg, NULL);
    assert_int_equal(iolink_device_init(&ctx, &cfg), 0);

    const iolink_caps_t caps = iolink_device_capabilities(&ctx);
    assert_false(iolink_caps_has(caps, IOLINK_CAP_PD_IN_CALLBACK));

    assert_int_equal(iolink_device_pd_input_update(&ctx, data, sizeof(data), true), 0);
    assert_true(ctx.dll.main.pd_valid);
    assert_int_equal(ctx.dll.main.shared.pd_in_len, 2U);
    assert_int_equal(
        ctx.dll.main.shared.pd_in[ctx.dll.main.shared.pd_in_active][ctx.dll.main.shared.od_width],
        0x11U);
    assert_int_equal(ctx.dll.main.shared.pd_in[ctx.dll.main.shared.pd_in_active]
                                              [ctx.dll.main.shared.od_width + 1U],
                     0x22U);
}

static void test_callback_path_selected_by_presence(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;
    const uint8_t data[2] = {0x33U, 0x44U};

    g_pd_calls = 0;
    g_pd_pattern = 0xA5U;
    static int user_sentinel;
    g_expected_user = &user_sentinel;
    make_config(&cfg, &g_cbs);
    cfg.user = g_expected_user;
    assert_int_equal(iolink_device_init(&ctx, &cfg), 0);

    const iolink_caps_t caps = iolink_device_capabilities(&ctx);
    assert_true(iolink_caps_has(caps, IOLINK_CAP_PD_IN_CALLBACK));

    /* 混用被显式拒绝：轮询 API 返回 PD 输入错误码。 */
    assert_int_equal(iolink_device_pd_input_update(&ctx, data, sizeof(data), true),
                     IOLINK_DEVICE_INIT_ERR_PD_INPUT);

    /* 在 OPERATE 下驱动一次主循环：回调发布 PD-in。 */
    ctx.dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    iolink_device_process(&ctx);

    assert_int_equal(g_pd_calls, 1);
    assert_true(ctx.dll.main.pd_valid);
    assert_int_equal(ctx.dll.main.shared.pd_in_len, 2U);
    assert_int_equal(
        ctx.dll.main.shared.pd_in[ctx.dll.main.shared.pd_in_active][ctx.dll.main.shared.od_width],
        0xA5U);
    assert_int_equal(ctx.dll.main.shared.pd_in[ctx.dll.main.shared.pd_in_active]
                                              [ctx.dll.main.shared.od_width + 1U],
                     0xA5U);
}

static void test_build_profile_is_consistent(void** state)
{
    (void) state;
    const iolink_build_caps_t* build = iolink_caps_build();

    assert_int_equal(build->abi, IOLINK_CAP_ABI);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    assert_int_equal(build->isr_rx, IOLINK_CAP_ISR_RX_FRAME);
#else
    assert_int_equal(build->isr_rx, IOLINK_CAP_ISR_RX_BYTE);
#endif
    assert_int_equal(build->concurrency, (uint8_t) IOLINK_CONCURRENCY);
    assert_int_equal(build->stats, (uint8_t) IOLINK_STATS_MODE);
    assert_int_equal(build->pd_in_buffers, (uint8_t) IOLINK_PD_IN_BUFFERS);
    assert_int_equal(build->event_min_appear_ms, IOLINK_EVENT_MIN_APPEAR_MS);
    assert_int_equal(build->event_min_repeat_ms, IOLINK_EVENT_MIN_REPEAT_MS);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_build_profile_is_consistent),
        cmocka_unit_test(test_polling_path_without_callback),
        cmocka_unit_test(test_callback_path_selected_by_presence),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
