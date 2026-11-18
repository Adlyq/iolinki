/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd_lease_frame.c
 * @brief TX FRAME + tx_busy()：PD-in 租约只由数据 ISR 释放。
 *
 * 主站静默期间没有数据中断，上一应答占用的缓冲区保持租约：主泵仍可填
 * 另一个缓冲区，但静默期的连续第二次更新必须计 pd_in_skips 并保留在途
 * 数据。主泵周期不轮询 tx_busy()，租约在下一次 ISR 进入时回收。
 *
 * 针对 TX FRAME + IOLINK_PHY_FEATURE_TX_BUSY=1 构建
 * （tests/CMakeLists.txt 的 iolink_tx_busy_fixture）。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/device.h"
#include "dll_access.h"
#include "test_helpers.h"

static void test_silent_period_lease_is_isr_owned(void** state)
{
    (void) state;
    const iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_2, .od_width = 1, .pd_in_len = 2, .pd_out_len = 0};
    iolink_test_device_t dev;
    const uint8_t first[2] = {0x11U, 0x22U};
    const uint8_t second[2] = {0x33U, 0x44U};
    const uint8_t third[2] = {0x55U, 0x66U};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    assert_int_equal(IOLINK_PD_IN_BUFFERS, 2U);
    iolink_dll_shared_t* shared = test_dll_shared(&dev.ctx);

    /* 首次发布：存在空闲缓冲区，未跳过。 */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, first, sizeof(first), true),
                     IOLINK_DEVICE_PD_OK);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 0U);

    /* ISR 捕获活动缓冲区并交给 DMA；主站静默，没有新的数据中断。 */
    const uint8_t captured = test_pd_active(test_dll(&dev.ctx));
    shared->b_pd_in_captured = captured;

    /* 静默期第一次更新：另一个缓冲区仍可填，租约不阻塞它。 */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, second, sizeof(second), true),
                     IOLINK_DEVICE_PD_OK);
    assert_int_not_equal(test_pd_active(test_dll(&dev.ctx)), captured);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 0U);

    /* 静默期第二次更新：两个缓冲区都被占用，跳过并计数。 */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, third, sizeof(third), true),
                     IOLINK_DEVICE_PD_ERR_BUSY);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 1U);
    /* 在途数据保持不变。 */
    assert_memory_equal(&shared->m_pd_in[captured][shared->c_od_width], first, sizeof(first));

    /* 主泵周期不再轮询 tx_busy()：线路空闲也不释放租约。 */
    iolink_device_process(&dev.ctx);
    assert_int_equal(shared->b_pd_in_captured, captured);

    /* 下一次 ISR 进入时 tx_busy() == 0：回收租约，更新恢复。 */
    iolink_device_process_isr(&dev.ctx);
    assert_int_equal(shared->b_pd_in_captured, 0xFFU);
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, third, sizeof(third), true),
                     IOLINK_DEVICE_PD_OK);
    assert_int_equal(test_dll(&dev.ctx)->pd_in_skips, 1U);
    assert_memory_equal(&shared->m_pd_in[test_pd_active(test_dll(&dev.ctx))][shared->c_od_width],
                        third, sizeof(third));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_silent_period_lease_is_isr_owned),
    };
    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
