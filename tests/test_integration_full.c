/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_integration_full.c
 * @brief iolinki 的全栈集成测试
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
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/application.h"
#include "iolinki/data_storage.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_full_stack_lifecycle(void** state)
{
    (void) state;
    iolink_phy_mock_reset();
    iolink_ds_mock_reset();

    /* 用 mock PHY 和 mock 存储初始化协议栈 */
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_ds_init(iolink_device_get_ds_ctx(&dev.ctx), &g_ds_storage_mock);

    /*** 步骤 1：STARTUP -> PREOPERATE ***/

    /* 1. 注入 WakeUp（状态从 SIO 迁移到 AWAITING_COMM） */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* 注意：我们依赖 STEP 2（ISDU 读）的第一个字节完成
       AWAITING_COMM -> PREOPERATE 的迁移并被立即处理。 */

    /*** 步骤 2：PREOPERATE（ISDU 读索引 0x10 - Vendor Name） ***/
    /* 主站发送：MC=0xBB（读索引 0x10）+ CK */
    uint8_t mc = 0xBB;
    uint8_t ck = test_frame_checksum(mc);

    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();

    /* 设备响应：OD + CK = 2 字节 */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /*** 步骤 3：事件触发 ***/
    iolink_events_ctx_t* evt_ctx = iolink_device_get_events_ctx(&dev.ctx);
    iolink_event_trigger(evt_ctx, 0x1234, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending(evt_ctx));

    /* 下一周期：主站发送空闲 MC=0x00，CK=0x00 */
    uint8_t idle_mc = 0x00;
    uint8_t idle_ck = test_frame_checksum(idle_mc);
    mock_phy_feed_byte(idle_mc);
    mock_phy_feed_byte(idle_ck);
    mock_phy_feed_chk();

    /* 设备发送带 Event 位设置的响应（Type-0 写：仅 CKS） */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    assert_true(iolink_events_pending(evt_ctx));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_full_stack_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
