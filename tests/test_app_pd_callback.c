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
#include "dll_access.h"

/* 库不再提供默认身份：这些 NO_HELPERS 用例只需要一个有效的身份对象。 */
static const iolink_device_info_t k_test_identity = {.revision_id = IOLINK_REVISION_ID_1_1};

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 1000000ULL;
}

static int phy_init(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
}

static void phy_set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}

static const iolink_phy_api_t g_phy = {
    .init = phy_init, .set_mode = phy_set_mode, .detect_wakeup = NULL};

static int g_pd_calls;
static uint8_t g_pd_pattern;
static void* g_expected_user;

static bool on_pd_input(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t len)
{
    assert_ptr_equal(self, g_expected_user);
    g_pd_calls++;
    (void) memset(data, g_pd_pattern, len);
    return true;
}

static void make_config(iolink_device_config_t* cfg, iolink_device_app_t* app,
                        bool (*pd_input)(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data,
                                         uint8_t len))
{
    (void) memset(app, 0, sizeof(*app));
    app->self = g_expected_user;
    app->time_us = test_time_us;
    app->on_pd_input = pd_input;

    /* config 只存指针，故用 static 存储。 */
    static iolink_stack_config_t stack;

    (void) memset(cfg, 0, sizeof(*cfg));
    (void) memset(&stack, 0, sizeof(stack));
    cfg->phy = &g_phy;
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    stack.pd_in_len = 2U;
    stack.pd_out_len = 0U;
    stack.od_width = 2U;
    cfg->stack = &stack;
    cfg->app = app;
    cfg->device_info = &k_test_identity;
}

static void test_polling_path_without_callback(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;
    iolink_device_app_t app;
    const uint8_t data[2] = {0x11U, 0x22U};

    g_expected_user = NULL;
    make_config(&cfg, &app, NULL);
    assert_int_equal(iolink_device_init(&ctx, &cfg), 0);

    const iolink_caps_t caps = iolink_device_capabilities(&ctx);
    assert_false(iolink_caps_has(caps, IOLINK_CAP_PD_IN_CALLBACK));

    assert_int_equal(iolink_device_pd_input_update(&ctx, data, sizeof(data), true), 0);
    assert_true(test_dll(&ctx)->pd_valid);
    const iolink_dll_shared_t* shared = test_dll_shared(&ctx);
    assert_int_equal(shared->pd_in_len, 2U);
    assert_int_equal(shared->pd_in[shared->pd_in_active][shared->od_width], 0x11U);
    assert_int_equal(shared->pd_in[shared->pd_in_active][shared->od_width + 1U], 0x22U);
}

static void test_callback_path_selected_by_presence(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;
    iolink_device_app_t app;
    const uint8_t data[2] = {0x33U, 0x44U};

    g_pd_calls = 0;
    g_pd_pattern = 0xA5U;
    static int user_sentinel;
    g_expected_user = &user_sentinel;
    make_config(&cfg, &app, on_pd_input);
    assert_int_equal(iolink_device_init(&ctx, &cfg), 0);

    const iolink_caps_t caps = iolink_device_capabilities(&ctx);
    assert_true(iolink_caps_has(caps, IOLINK_CAP_PD_IN_CALLBACK));

    /* 混用被显式拒绝：轮询 API 返回 PD 输入错误码。 */
    assert_int_equal(iolink_device_pd_input_update(&ctx, data, sizeof(data), true),
                     IOLINK_DEVICE_INIT_ERR_PD_INPUT);

    /* 在 OPERATE 下驱动一次主循环：回调发布 PD-in。 */
    test_dll_shared(&ctx)->state = IOLINK_DLL_STATE_OPERATE;
    iolink_device_process(&ctx);

    assert_int_equal(g_pd_calls, 1);
    assert_true(test_dll(&ctx)->pd_valid);
    const iolink_dll_shared_t* shared = test_dll_shared(&ctx);
    assert_int_equal(shared->pd_in_len, 2U);
    assert_int_equal(shared->pd_in[shared->pd_in_active][shared->od_width], 0xA5U);
    assert_int_equal(shared->pd_in[shared->pd_in_active][shared->od_width + 1U], 0xA5U);
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
