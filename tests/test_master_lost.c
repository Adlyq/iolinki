/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_master_lost.c
 * @brief ST 风格的主站丢失计数器、可配置的周期倍数以及回调。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/dll_ctx.h"
#include "test_helpers.h"

static uint32_t g_now_ms;

static uint64_t master_lost_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_now_ms * 1000U;
}

static int lost_calls;
static int online_calls;
static void* g_expected_user;

static void on_lost(void* user)
{
    assert_ptr_equal(user, g_expected_user);
    lost_calls++;
}

static void on_online(void* user)
{
    assert_ptr_equal(user, g_expected_user);
    online_calls++;
}

static int test_setup(void** state)
{
    (void) state;
    g_iolink_test_time_us_override = master_lost_time_us;
    g_now_ms = 0U;
    lost_calls = 0;
    online_calls = 0;
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    g_iolink_test_time_us_override = NULL;
    return 0;
}

static void init_device(iolink_test_device_t* dev, uint8_t cycles,
                        const iolink_app_callbacks_t* cbs)
{
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0,
        .min_cycle_time = 20U, /* 2 ms */
        .master_lost_cycles = cycles,
    };
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(dev, &config, cbs), 0);
}

static void test_master_lost_after_n_cycles(void** state)
{
    (void) state;
    static const iolink_app_callbacks_t cbs = {
        .on_master_lost = on_lost,
        .on_master_online = on_online,
    };
    iolink_test_device_t dev;
    static int user_sentinel;
    g_expected_user = &user_sentinel;
    init_device(&dev, 3U, &cbs);
    dev.cfg.user = g_expected_user;

    /* 启动/预操作阶段的间隔不得触发主站丢失。 */
    g_now_ms = 1000U;
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 0);

    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    g_now_ms = 1005U; /* 2 个完整 2 ms 周期 -> 尚未丢失（N=3） */
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 0);

    g_now_ms = 1006U; /* 3 个周期 -> 丢失 */
    iolink_device_process(&dev.ctx);
    assert_true(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 1);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 一次帧结束活动会清零计数器（ST StartCallbackTimer）。 */
    dev.ctx.dll.main.shared.isr_master_activity = 1U;
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(online_calls, 1);

    /* 只要活动持续到达，就仍视为在线。 */
    dev.ctx.dll.main.shared.isr_master_activity = 1U;
    g_now_ms = 1007U;
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
}

static void test_master_lost_uses_master_cycle_time(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, 3U, NULL);
    move_to_operate_ctx(&dev.ctx);

    /* DPP1 0x01 = 10 -> 10 * 100 us = 1 ms（而非 2 ms 的 MinCycleTime）。 */
    dev.ctx.dll.main.master_cycle_time = 10U;
    g_now_ms = 4U;
    iolink_device_process(&dev.ctx);
    assert_true(iolink_device_master_lost(&dev.ctx));
}

static void test_master_lost_disabled(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, 0U, NULL);
    move_to_operate_ctx(&dev.ctx);

    g_now_ms = 1000U;
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
}

/* 主泵繁忙时由数据 ISR 应答的帧证明主站在轮询：较长的
 * 主窗口不得被计为一次丢失。 */
static void test_isr_frame_counts_as_master_activity(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    const iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1,
        .min_cycle_time = 20U, /* 2 ms */
        .master_lost_cycles = 3U,
        .pd_in_len = 1U,
        .pd_out_len = 0U,
    };
    uint8_t frame[3] = {0x00U, IOLINK_MSEQ_TYPE_2, 0x11U};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    g_now_ms = 1000U;
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 一次周期性 Type-2 WRITE，仅由 ISR 完成组帧与应答。 */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    mock_phy_feed_byte(frame[0]);
    mock_phy_feed_byte(frame[1]);
    mock_phy_feed_byte(frame[2]);
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2U); /* PD-in（1）+ CKS */
    will_return(mock_phy_send, 0);
    iolink_device_process_isr(&dev.ctx);
    while (dev.ctx.dll.main.shared.isr_tx_active != 0U) {
        iolink_device_process_isr(&dev.ctx);
    }

    /* ISR 侧的监控时间戳必须已经知道主站仍存活。
     * 否则丢失窗口会从主泵最后一次运行的时刻开始度量，繁忙的
     * 主窗口就会被误判为主站静默。 */
    assert_int_equal(dev.ctx.dll.main.shared.isr_master_activity, 1U);

    g_now_ms += 10U;
    iolink_device_process(&dev.ctx);

    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 0);
    assert_int_equal(online_calls, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_master_lost_after_n_cycles, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_master_lost_uses_master_cycle_time, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_master_lost_disabled, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isr_frame_counts_as_master_activity, test_setup,
                                        test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
