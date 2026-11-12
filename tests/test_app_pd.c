/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_app_pd.c
 * @brief 过程数据应用功能的单元测试（应答布局）
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

/* 图 A.9/A.10：设备对 READ M-sequence 的应答是 [OD][PD-in] CKS
   且没有前导状态八位组。PD 有效性位于 CKS 位 6（1 = 无效）
   且没有翻转位（A.1.5）。配置的 TYPE_2_2 应答为
   OD(1) + PD-in(2) + CKS，字节精确数组固定了这一顺序。 */

static void test_reply_layout_od_first_no_status_octet(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .pd_in_len = 2, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[2] = {0x80, IOLINK_MSEQ_TYPE_2};
    const int frame_len = 2;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, (size_t) frame_len));

    uint8_t input[2] = {0x11, 0x22};
    iolink_device_pd_input_update(&dev.ctx, input, 2, true);

    for (int i = 0; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* MC 0x80 是 READ M-sequence，因此应答为 OD(1)、PD-in(2)、CKS：
       0x00 0x11 0x22 0x2D。过程通道读不提供 OD 八位组
       (0x00)；因为 PD 有效，CKS 位 6 为 0（A.1.6）。 */
    uint8_t expected[] = {0x00, 0x11, 0x22, 0x2D};
    expect_memory(mock_phy_send, data, expected, sizeof(expected));
    expect_value(mock_phy_send, len, sizeof(expected));
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
}

static void test_reply_flags_pd_invalid(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .pd_in_len = 2, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[2] = {0x80, IOLINK_MSEQ_TYPE_2};
    const int frame_len = 2;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, (size_t) frame_len));

    /* 将输入过程数据标记为无效；CKS 位 6 必须置位。 */
    iolink_device_pd_input_update(&dev.ctx, (const uint8_t*) "\x11\x22", 2, false);

    for (int i = 0; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* 相同的 READ 应答布局，PD 无效标志位于 CKS 位 6：
       0x40 | 0x35 = 0x75。 */
    uint8_t expected[] = {0x00, 0x11, 0x22, 0x75};
    expect_memory(mock_phy_send, data, expected, sizeof(expected));
    expect_value(mock_phy_send, len, sizeof(expected));
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reply_layout_od_first_no_status_octet),
        cmocka_unit_test(test_reply_flags_pd_invalid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
