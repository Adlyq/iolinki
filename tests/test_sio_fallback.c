/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "iolinki/device.h"
#include "dll_access.h"
#include "iolinki/phy.h"
#include "crc_oracle.h"
#include "test_helpers.h"

static void test_crc_errors_do_not_enter_sio(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .od_width = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* 确认初始处于 SIO 模式（新的默认行为） */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);

    /* 切换到 OPERATE */
    move_to_operate_ctx(&dev.ctx);

    /* 确认处于 SDCI 模式 */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* 注入 CRC 错误：ST rc=2 会丢弃每个帧并保持 SDCI。 */
    for (int i = 0; i < 3; i++) {
        uint8_t bad_frame[2] = {0x95, 0xFF}; /* 无效 CRC */

        mock_phy_feed_byte(bad_frame[0]);
        mock_phy_feed_byte(bad_frame[1]);
        mock_phy_feed_chk();

        test_io_cycle(&dev.ctx);
    }

    /* 无 SIO 回退：CRC 错误不影响 DL 模式。 */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    assert_int_equal(test_dll(&dev.ctx)->crc_errors, 3U);
    assert_int_equal(test_dll(&dev.ctx)->invalid_frames, 3U);
}

static void test_valid_frame_after_crc_errors(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .od_width = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* 切换到 OPERATE */
    move_to_operate_ctx(&dev.ctx);

    /* CRC 错误就地丢弃；链路保持 SDCI。 */
    for (int i = 0; i < 3; i++) {
        uint8_t bad_frame[2] = {0x95, 0xFF};
        mock_phy_feed_byte(bad_frame[0]);
        mock_phy_feed_byte(bad_frame[1]);
        mock_phy_feed_chk();
        test_io_cycle(&dev.ctx);
    }

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* 合法帧仍会在同一条链路上被服务。 */
    uint8_t idle_mc = 0x00;
    uint8_t idle_ck = test_frame_checksum(idle_mc);
    mock_phy_feed_byte(idle_mc);
    mock_phy_feed_byte(idle_ck);
    mock_phy_feed_chk();

    test_io_cycle(&dev.ctx);
    assert_tx_len(1); /* Type-0 写只回复 CKS */

    /* 仍为 SDCI：未发生 SIO 回退。 */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
}

static void test_t_dsio_returns_to_sio_without_message(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .od_width = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* 检测到唤醒：设备离开 SIO 并等待报文。 */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    iolink_phy_mock_set_wakeup(0);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* 在 T_DSIO（Table 47 T10）内没有合法报文到达：返回 SIO。 */
    test_clock_advance_ms(IOLINK_T_DSIO_MS + 50U);
    test_io_cycle(&dev.ctx);

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_fallback_command_switches_to_sio_after_t_fbd(void** state)
{
    (void) state;

    /* min_cycle_time 以 0.1 ms 为单位：10 -> 1 ms -> T_FBD = 3 ms。 */
    iolink_stack_config_t config = {.pd_in_len = 0,
                                    .pd_out_len = 0,
                                    .m_seq_type = IOLINK_M_SEQ_TYPE_0,
                                    .od_width = 1,
                                    .min_cycle_time = 10U};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* Page 通道 Type-0 写 MasterCommand FALLBACK（0x5A，Table B.2）。 */
    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = test_expected_checksum6(frame, 3U);
    for (size_t i = 0U; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);
    assert_tx_len(1); /* Type-0 写只回复 CKS */

    /* 在 T_FBD 到期之前仍处于 SDCI（设备等待 3 个 MasterCycleTimes）。 */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    test_clock_advance_ms(10U);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_fallback_uses_master_cycle_time(void** state)
{
    (void) state;

    /* MinCycleTime alone would give 3 ms. DPP1 MasterCycleTime 0x50 decodes to
     * 128 * 0.1 ms = 12.8 ms, so T_FBD is 3 * 12.8 ms = 38.4 ms (Table 43). */
    iolink_stack_config_t config = {.pd_in_len = 0,
                                    .pd_out_len = 0,
                                    .m_seq_type = IOLINK_M_SEQ_TYPE_0,
                                    .od_width = 1,
                                    .min_cycle_time = 10U};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    move_to_operate_ctx(&dev.ctx);
    test_dll(&dev.ctx)->shared.m_master_cycle_time = 0x50U;

    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = test_expected_checksum6(frame, 3U);
    for (size_t i = 0U; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);
    assert_tx_len(1);

    /* 3 * 12.8 ms = 38.4 ms: the deadline is rounded up to 39 ms. At 38 ms the
     * device must still be in SDCI (a floor rounding to 38 ms would fail here). */
    test_clock_advance_ms(38U);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    test_clock_advance_ms(1U);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_fallback_command_ignored_without_wakeup(void** state)
{
    (void) state;

    iolink_stack_config_t config = {.pd_in_len = 0,
                                    .pd_out_len = 0,
                                    .m_seq_type = IOLINK_M_SEQ_TYPE_0,
                                    .od_width = 1,
                                    .min_cycle_time = 10U};
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    /* 相同 PHY，但没有唤醒检测器：DLL 从 SDCI 启动，
     * 且无法回到 SIO。 */
    iolink_test_device_t dev;
    static iolink_phy_api_t phy;
    memset(&dev, 0, sizeof(dev));
    (void) memset(&phy, 0, sizeof(phy));
    phy = g_phy_mock;
    phy.detect_wakeup = NULL;
    dev.cfg.phy = &phy;
    dev.cfg.app = iolink_test_default_app();
    dev.cfg.device_info = iolink_test_identity();
    dev.stack = config;
    dev.cfg.stack = &dev.stack;
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), 0);

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* Page 通道 Type-0 写 MasterCommand FALLBACK（0x5A，Table B.2）。 */
    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = test_expected_checksum6(frame, 3U);
    for (size_t i = 0U; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);
    assert_tx_len(1); /* Type-0 写只回复 CKS */

    /* 无唤醒时不支持该命令：会确认应答，但 DL 模式/状态不变。 */
    assert_false(dll_timer_armed(test_dll(&dev.ctx), IOLINK_TIMER_T_FBD));
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    test_clock_advance_ms(10U);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
}

static int group_setup(void** state)
{
    (void) state;
    test_clock_install();
    return 0;
}

static int group_teardown(void** state)
{
    test_clock_uninstall();
    return test_tx_all_asserted(state);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc_errors_do_not_enter_sio),
        cmocka_unit_test(test_valid_frame_after_crc_errors),
        cmocka_unit_test(test_t_dsio_returns_to_sio_without_message),
        cmocka_unit_test(test_fallback_command_switches_to_sio_after_t_fbd),
        cmocka_unit_test(test_fallback_uses_master_cycle_time),
        cmocka_unit_test(test_fallback_command_ignored_without_wakeup),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
