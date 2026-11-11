/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_timing.c
 * @brief Unit tests for time utilities and timing logic placeholders
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
#include "test_helpers.h"

static void test_time_get_ms(void** state)
{
    (void) state;
    uint32_t t1 = iolink_test_time_ms();
    usleep(10000); /* 10ms */
    uint32_t t2 = iolink_test_time_ms();

    assert_true(t2 >= t1 + 10);
    assert_true(t2 < t1 + 50); /* Allow more jitter (was 20) */
}

static void test_time_get_us(void** state)
{
    (void) state;
    uint64_t t1 = iolink_test_time_us(NULL);
    usleep(1000); /* 1ms */
    uint64_t t2 = iolink_test_time_us(NULL);

    assert_true(t2 >= t1 + 1000);
    assert_true(t2 < t1 + 5000); /* Allow more jitter (was 2000) */
}

static void test_timing_constants(void** state)
{
    (void) state;
    /* Table 10: T_WU pulse 75..85 us (typ. 80) and a single T_REN <= 500 us.
       Table 42/47 T10: T_DSIO 60..300 ms, default 300. */
    assert_int_equal(IOLINK_T_WU_US, 80U);
    assert_int_equal(IOLINK_T_REN_US, 500U);
    assert_int_equal(IOLINK_T_DSIO_MS, 300U);
}

static void test_t_ren_single_value_for_every_baudrate(void** state)
{
    (void) state;
    iolink_stack_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* Table 10: T_REN is one device property, independent of the baudrate. */
    const iolink_baudrate_t rates[] = {IOLINK_BAUDRATE_COM1, IOLINK_BAUDRATE_COM2,
                                       IOLINK_BAUDRATE_COM3};
    for (size_t i = 0U; i < (sizeof(rates) / sizeof(rates[0])); i++) {
        assert_int_equal(iolink_dll_set_baudrate(&dev.ctx.dll, rates[i]), 0);
        assert_int_equal(dev.ctx.dll.t_ren_limit_us, IOLINK_T_REN_US);
    }

    /* An explicit override still wins (kept public API). */
    iolink_device_set_t_ren_limit_us(&dev.ctx, 1234U);
    assert_int_equal(dev.ctx.dll.t_ren_limit_us, 1234U);
    assert_int_equal(iolink_dll_set_baudrate(&dev.ctx.dll, IOLINK_BAUDRATE_COM3), 0);
    assert_int_equal(dev.ctx.dll.t_ren_limit_us, 1234U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_time_get_ms),
        cmocka_unit_test(test_time_get_us),
        cmocka_unit_test(test_timing_constants),
        cmocka_unit_test(test_t_ren_single_value_for_every_baudrate),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
