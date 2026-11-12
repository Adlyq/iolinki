/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd.c
 * @brief 过程数据（PD）处理的单元测试
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
#include "test_helpers.h"

static void test_pd_input_output(void** state)
{
    (void) state;

    iolink_stack_config_t config = {
        .pd_in_len = 2, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* 进入 OPERATE */
    move_to_operate_ctx(&dev.ctx);

    /* 1. 设置输入 PD */
    uint8_t input[2] = {0x11, 0x22};
    iolink_device_pd_input_update(&dev.ctx, input, 2, true);

    /* 2. 模拟主站帧（Type 2_2 READ：MC、CKT [+ 旧式流式传输上的
       OD，它在每个请求中计入一个 OD 八位字节]）。
       Table A.10：TYPE_2_2 携带一个 OD 八位字节且无 PD-out（A.10）。 */
    uint8_t frame[2] = {0x80, IOLINK_MSEQ_TYPE_2};
    const int frame_len = 2;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, (size_t) frame_len));

    for (int i = 0; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* 响应：TYPE_2_2 为 PD_IN(2)、OD(1)、CKS(1) = 4 字节（A.1.5） */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_output),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
