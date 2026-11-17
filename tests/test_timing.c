/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_timing.c
 * @brief 时间工具函数与时序逻辑占位实现的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "iolinki/crc.h"
#include "iolinki/device.h"
#include "dll_access.h"
#include "test_helpers.h"

static void test_time_get_ms(void** state)
{
    (void) state;
    uint32_t t1 = iolink_test_time_ms();
    usleep(10000); /* 10ms */
    uint32_t t2 = iolink_test_time_ms();

    assert_true(t2 >= t1 + 10);
    assert_true(t2 < t1 + 50); /* 允许更大的抖动（原为 20） */
}

static void test_time_get_us(void** state)
{
    (void) state;
    uint64_t t1 = iolink_test_time_us(NULL);
    usleep(1000); /* 1ms */
    uint64_t t2 = iolink_test_time_us(NULL);

    assert_true(t2 >= t1 + 1000);
    assert_true(t2 < t1 + 5000); /* 允许更大的抖动（原为 2000） */
}

static void test_timing_constants(void** state)
{
    (void) state;
    /* Table 10：T_WU 脉冲 75..85 us（典型值 80），单个 T_REN <= 500 us。
       Table 42/47 T10：T_DSIO 60..300 ms，默认 300。 */
    assert_int_equal(IOLINK_T_WU_US, 80U);
    assert_int_equal(IOLINK_T_REN_US, 500U);
    assert_int_equal(IOLINK_T_DSIO_MS, 300U);
}

static void test_fake_clock_advances(void** state)
{
    (void) state;
    test_clock_install();
    const uint64_t t0 = test_clock_now_us();
    test_clock_advance_ms(10U);

    assert_int_equal(test_clock_now_us(), t0 + 10000ULL);
    assert_int_equal(iolink_test_time_us(NULL), t0 + 10000ULL);
    assert_int_equal(iolink_test_time_ms(), (uint32_t) ((t0 + 10000ULL) / 1000ULL));

    test_clock_uninstall();
    assert_null(g_iolink_test_time_us_override);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_time_get_ms),
        cmocka_unit_test(test_time_get_us),
        cmocka_unit_test(test_timing_constants),
        cmocka_unit_test(test_fake_clock_advances),
    };

    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
