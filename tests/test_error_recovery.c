/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_error_recovery.c
 * @brief 错误检测与恢复的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/events.h"
#include "iolinki/dll.h"
#include "test_helpers.h"

static void test_crc_error_recovery(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_to_operate_ctx(&dev.ctx);

    /* 2. 模拟 3 次 CRC 错误（Type 1_1、1 字节 PD：MC、CKT、PD(1)、OD(1)、CK = 5 字节） */
    for (int r = 0; r < 3; r++) {
        /* 将完整帧放入 mock 队列 */
        mock_phy_feed_byte(0x00);               /* MC（写） */
        mock_phy_feed_byte(IOLINK_MSEQ_TYPE_2); /* 校验和错误的 CKT */
        mock_phy_feed_byte(0x00);               /* PD-out */
        mock_phy_feed_byte(0x00);               /* OD0 */
        mock_phy_feed_byte(0x00);               /* OD1 */
        mock_phy_feed_chk();                    /* 结束帧 */

        /* 一次调用即可处理整个可用帧 */
        test_io_cycle(&dev.ctx);
    }

    /* 3. 检查是否检测到错误（3 次重试后触发 Event 0x5000） */
    assert_true(iolink_events_pending(iolink_device_get_events_ctx(&dev.ctx)));
}

static void test_communication_timeout(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);

    /* 触发唤醒以从 SIO 进入 SDCI */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    iolink_phy_mock_set_wakeup(0);

    /* 确保 SDCI 模式下没有可用数据 */
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc_error_recovery),
        cmocka_unit_test(test_communication_timeout),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
