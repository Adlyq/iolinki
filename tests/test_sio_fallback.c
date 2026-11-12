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
#include <unistd.h>
#include <stdio.h>

#include "iolinki/device.h"
#include "iolinki/dll.h"
#include "iolinki/phy.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_crc_errors_do_not_enter_sio(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
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
}

static void test_valid_frame_after_crc_errors(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
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

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 写只回复 CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* 仍为 SDCI：未发生 SIO 回退。 */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
}

static void test_t_dsio_returns_to_sio_without_message(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
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
    usleep((IOLINK_T_DSIO_MS + 50U) * 1000U);
    test_io_cycle(&dev.ctx);

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_fallback_command_switches_to_sio_after_t_fbd(void** state)
{
    (void) state;

    /* min_cycle_time 以 0.1 ms 为单位：10 -> 1 ms -> T_FBD = 3 ms。 */
    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .min_cycle_time = 10U};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* Page 通道 Type-0 写 MasterCommand FALLBACK（0x5A，Table B.2）。 */
    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = iolink_checksum6(frame, 3U);
    for (size_t i = 0U; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 写只回复 CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* 在 T_FBD 到期之前仍处于 SDCI（设备等待 3 个 MasterCycleTimes）。 */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    usleep(10U * 1000U);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_fallback_command_ignored_without_wakeup(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .min_cycle_time = 10U};
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    /* 相同 PHY，但没有唤醒检测器：DLL 从 SDCI 启动，
     * 且无法回到 SIO。 */
    iolink_test_device_t dev;
    memset(&dev, 0, sizeof(dev));
    dev.cfg.phy = g_phy_mock;
    dev.cfg.phy.detect_wakeup = NULL;
    dev.cfg.time_us = iolink_test_time_us;
    dev.cfg.storage = &g_ds_storage_mock;
    dev.cfg.stack = config;
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), 0);

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* Page 通道 Type-0 写 MasterCommand FALLBACK（0x5A，Table B.2）。 */
    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = iolink_checksum6(frame, 3U);
    for (size_t i = 0U; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 写只回复 CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* 无唤醒时不支持该命令：会确认应答，但 DL 模式/状态不变。 */
    assert_int_equal(dev.ctx.dll.fallback_deadline_ms, 0U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    usleep(10U * 1000U);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc_errors_do_not_enter_sio),
        cmocka_unit_test(test_valid_frame_after_crc_errors),
        cmocka_unit_test(test_t_dsio_returns_to_sio_without_message),
        cmocka_unit_test(test_fallback_command_switches_to_sio_after_t_fbd),
        cmocka_unit_test(test_fallback_command_ignored_without_wakeup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
