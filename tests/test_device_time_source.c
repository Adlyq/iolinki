/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device_time_source.c
 * @brief 通过 iolink_device_app_t::time_us 提供的应用时钟会被
 *        DLL 和事件引擎使用，而不使用平台时钟。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/events.h"
#include "dll_access.h"

/* 库不再提供默认身份：这些 NO_HELPERS 用例只需要一个有效的身份对象。 */
static const iolink_device_info_t k_test_identity = {.revision_id = IOLINK_REVISION_ID_1_1};

static uint64_t g_app_time_us;

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t app_time_us(iolink_app_t* self)
{
    (void) self;
    return g_app_time_us;
}

static void init_device(iolink_device_ctx_t* ctx, const iolink_device_app_t* app)
{
    iolink_device_config_t config;
    /* config 只存指针：phy/stack 必须比 ctx 存活更久。 */
    static iolink_stack_config_t stack;
    static iolink_phy_api_t phy;

    (void) memset(&config, 0, sizeof(config));
    (void) memset(&stack, 0, sizeof(stack));
    (void) memset(&phy, 0, sizeof(phy));
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    stack.od_width = 1U;
    stack.t_pd_us = 500U;
    config.stack = &stack;
    config.phy = &phy;
    config.app = app;
    config.device_info = &k_test_identity;

    assert_int_equal(iolink_device_init(ctx, &config), 0);
}

static void test_initialization_uses_app_clock(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    static const iolink_device_app_t app = {.self = NULL, .time_us = app_time_us};

    g_app_time_us = 1000000ULL;
    init_device(&ctx, &app);

    /* T_PD 截止时间与主站活动种子都来自应用时钟。 */
    assert_int_equal(iolink_dll_timer_deadline(test_dll(&ctx), IOLINK_TIMER_T_PD),
                     (1000000ULL + 500ULL));
    assert_int_equal(test_dll(&ctx)->master_last_activity_ms, 1000U);
}

static void test_event_engine_uses_app_clock(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    static const iolink_device_app_t app = {.self = NULL, .time_us = app_time_us};

    g_app_time_us = 1234000ULL;
    init_device(&ctx, &app);

    iolink_events_ctx_t* events = iolink_device_get_events_ctx(&ctx);
    iolink_event_trigger(events, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD, IOLINK_EVENT_TYPE_ERROR);

    assert_true(events->pending[0].active);
    assert_true(events->pending[0].appears_valid);
    assert_int_equal(events->pending[0].last_appears_ms, 1234U);
}

static void test_null_clock_is_rejected_at_init(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    iolink_device_app_t app;

    iolink_stack_config_t stack;
    static iolink_phy_api_t phy;

    (void) memset(&config, 0, sizeof(config));
    (void) memset(&stack, 0, sizeof(stack));
    (void) memset(&phy, 0, sizeof(phy));
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    stack.od_width = 1U;
    config.stack = &stack;
    config.phy = &phy;
    config.device_info = &k_test_identity;

    /* 没有应用 trait：初始化先拒绝 APP。 */
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_APP);

    /* 有 trait 但缺少时钟：初始化拒绝 TIME。 */
    (void) memset(&app, 0, sizeof(app));
    config.app = &app;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_TIME);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_initialization_uses_app_clock),
        cmocka_unit_test(test_event_engine_uses_app_clock),
        cmocka_unit_test(test_null_clock_is_rejected_at_init),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
