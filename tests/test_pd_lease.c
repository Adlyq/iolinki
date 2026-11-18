/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd_lease.c
 * @brief PD-in 缓冲区借用：跳过计数与过期借用恢复。
 *
 * TX BYTE 在 ISR 内部结束应答（帧末 tx_done），因此记录下来的
 * PD-in 捕获已不再被流式发送。在帧中途卡住的收发器
 * 不得让 PD-in 更新在本次会话剩余时间内
 * 冻结。
 *
 * "无空闲缓冲区"只在 TX BYTE 的两缓冲区布局可达：TX FRAME 里 PHY 用
 * tx_busy() 报告帧结束，数据 ISR 因此能在下次进入时释放租约
 * （见 test_pd_lease_frame.c）；没有该钩子时第三个缓冲区吸收更新。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/device.h"
#include "dll_access.h"
#include "test_helpers.h"

static void test_lease_recovery_and_skip_counter(void** state)
{
    (void) state;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 该场景手工占用"活动缓冲区之外的唯一缓冲区"，只在两缓冲区的 TX BYTE
     * 布局可达。TX FRAME 下 PHY 通过 tx_busy() 报告帧结束（mock 随即报告
     * 线路空闲），数据 ISR 在下次进入时释放租约；三缓冲区布局又总有一个
     * 可写槽位。 */
    skip();
#endif
    const iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_2, .od_width = 1, .pd_in_len = 2, .pd_out_len = 0};
    iolink_test_device_t dev;
    const uint8_t data[2] = {0x11U, 0x22U};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* 首次发布：存在空闲缓冲区，未跳过。 */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), 0);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 0U);

    /* 模拟在应答中途卡住的收发器：尽管没有应答在传输中，ISR 的捕获
     * 仍被记录。 */
    test_dll_shared(&dev.ctx)->pd_in_captured =
        (uint8_t) (test_dll_shared(&dev.ctx)->pd_in_active ^ 1U);
    test_dll_shared(&dev.ctx)->isr_tx_active = 0U;

    /* 无空闲缓冲区：每次更新都被跳过并计数，绝不静默。 */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), -1);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 1U);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), -1);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 2U);

    /* 在无法证明传输已失效时（卡住的 PHY 必须复位），库有意保留借用；
     * 一旦槽位被释放（即 ISR 在帧结束时所做的），
     * 发布便恢复工作。 */
    test_dll_shared(&dev.ctx)->pd_in_captured = 0xFFU;
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), 0);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 2U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_lease_recovery_and_skip_counter),
    };
    const int rc = cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 本文件只有两缓冲区 TX BYTE 才可达的场景；让 ctest 报告 Skipped
     * （CMake 侧 SKIP_RETURN_CODE 77）而不是一个什么都没断言的 Passed。 */
    return (rc == 0) ? 77 : rc;
#else
    return rc;
#endif
}
