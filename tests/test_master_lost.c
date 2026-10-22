/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_master_lost.c
 * @brief ST-style master-loss counter, configurable cycle multiplier and callbacks.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/dll.h"
#include "iolinki/time_utils.h"
#include "test_helpers.h"

static uint32_t g_now_ms;

uint32_t iolink_time_get_ms(void)
{
    return g_now_ms;
}

uint64_t iolink_time_get_us(void)
{
    return (uint64_t) g_now_ms * 1000U;
}

static int lost_calls;
static int online_calls;

static void on_lost(void)
{
    lost_calls++;
}

static void on_online(void)
{
    online_calls++;
}

static int test_setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    g_now_ms = 0U;
    lost_calls = 0;
    online_calls = 0;
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static void init_device(iolink_test_device_t* dev, uint8_t cycles,
                        const iolink_app_callbacks_t* cbs)
{
    iolink_config_t config = {
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
    init_device(&dev, 3U, &cbs);

    /* Startup/preoperate gaps must not raise master-lost. */
    g_now_ms = 1000U;
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 0);

    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    g_now_ms = 1005U; /* 2 complete 2 ms cycles -> not lost yet (N=3) */
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 0);

    g_now_ms = 1006U; /* 3 cycles -> lost */
    iolink_device_process(&dev.ctx);
    assert_true(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 1);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* A frame-end activity clears the counter (ST StartCallbackTimer). */
    dev.ctx.dll.isr_master_activity = 1U;
    iolink_device_process(&dev.ctx);
    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(online_calls, 1);

    /* Still online while activity keeps arriving. */
    dev.ctx.dll.isr_master_activity = 1U;
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

    /* DPP1 0x01 = 10 -> 10 * 100 us = 1 ms (not the 2 ms MinCycleTime). */
    dev.ctx.dll.master_cycle_time = 10U;
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

#if IOLINK_PHY_FEATURE_ISR_PUMP
/* A frame answered by the data ISR while the main pump is busy proves the
 * master is polling: a long main window must not be counted as a loss. */
static void test_isr_frame_counts_as_master_activity(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    const iolink_config_t config = {
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

    /* One cyclic Type-2 WRITE, framed and answered by the ISR only. */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    mock_phy_feed_byte(frame[0]);
    mock_phy_feed_byte(frame[1]);
    mock_phy_feed_byte(frame[2]);
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2U); /* PD-in (1) + CKS */
    will_return(mock_phy_send, 0);
    iolink_device_process_isr(&dev.ctx);
    while (dev.ctx.dll.isr_tx_active != 0U) {
        iolink_device_process_isr(&dev.ctx);
    }

    /* The ISR-side supervision timestamp must already know the master is
     * alive. Without it the loss window would be measured from the last time
     * the main pump ran, so a busy main window looks like a silent master. */
    assert_int_equal(dev.ctx.dll.isr_master_activity, 1U);

    g_now_ms += 10U;
    iolink_device_process(&dev.ctx);

    assert_false(iolink_device_master_lost(&dev.ctx));
    assert_int_equal(lost_calls, 0);
    assert_int_equal(online_calls, 0);
}
#endif

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_master_lost_after_n_cycles, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_master_lost_uses_master_cycle_time, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_master_lost_disabled, test_setup, test_teardown),
#if IOLINK_PHY_FEATURE_ISR_PUMP
        cmocka_unit_test_setup_teardown(test_isr_frame_counts_as_master_activity, test_setup,
                                        test_teardown),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
