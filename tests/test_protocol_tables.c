/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_protocol_tables.c
 * @brief Unit tests for the pure specification-table helpers in iolink.h:
 *        cycle-time decoding (Table B.3), default OD width (Table A.8/A.10)
 *        and the OPERATE M-sequence code (Table A.10).
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/iolink.h"

/* Table B.3: cycle-time octet = time base (bits 7:6) | multiplier (bits 5:0). */
static void test_cycle_time_to_us(void** state)
{
    (void) state;

    /* Time base 00: multiplier * 0.1 ms. */
    assert_int_equal(iolink_cycle_time_to_us(0x00U), 0U);
    assert_int_equal(iolink_cycle_time_to_us(0x01U), 100U);
    assert_int_equal(iolink_cycle_time_to_us(0x3FU), 6300U);

    /* Time base 01: 6.4 ms + multiplier * 0.4 ms. */
    assert_int_equal(iolink_cycle_time_to_us(0x40U), 6400U);
    assert_int_equal(iolink_cycle_time_to_us(0x41U), 6800U);
    assert_int_equal(iolink_cycle_time_to_us(0x7FU), 31600U);

    /* Time base 10: 32.0 ms + multiplier * 1.6 ms. */
    assert_int_equal(iolink_cycle_time_to_us(0x80U), 32000U);
    assert_int_equal(iolink_cycle_time_to_us(0x81U), 33600U);
    assert_int_equal(iolink_cycle_time_to_us(0xBFU), 132800U);

    /* Time base 11 is reserved: decode to 0 (unsupported). */
    assert_int_equal(iolink_cycle_time_to_us(0xC0U), 0U);
    assert_int_equal(iolink_cycle_time_to_us(0xFFU), 0U);
}

/* Table B.3: encode a 0.1 ms unit cycle time into a time base | multiplier octet. */
static void test_cycle_time_encode(void** state)
{
    (void) state;

    /* Below the 0.4 ms minimum: clamp to 0x04 (0.4 ms). */
    assert_int_equal(iolink_cycle_time_encode(0U), 0x04U);
    assert_int_equal(iolink_cycle_time_encode(3U), 0x04U);
    assert_int_equal(iolink_cycle_time_encode(4U), 0x04U);

    /* Time base 00: exact 0.1 ms steps up to 6.3 ms. */
    assert_int_equal(iolink_cycle_time_encode(63U), 0x3FU);

    /* Time base 01: 6.4 .. 31.6 ms, ceiled to 0.4 ms steps. */
    assert_int_equal(iolink_cycle_time_encode(64U), 0x40U);  /* 6.4 ms */
    assert_int_equal(iolink_cycle_time_encode(65U), 0x41U);  /* 6.8 ms >= 6.5 ms */
    assert_int_equal(iolink_cycle_time_encode(316U), 0x7FU); /* 31.6 ms */

    /* Time base 10: 32.0 .. 132.8 ms, ceiled to 1.6 ms steps. */
    assert_int_equal(iolink_cycle_time_encode(317U), 0x80U); /* 32.0 ms >= 31.7 ms */
    assert_int_equal(iolink_cycle_time_encode(320U), 0x80U); /* 32.0 ms */
    assert_int_equal(iolink_cycle_time_encode(321U), 0x81U); /* 33.6 ms >= 32.1 ms */
    assert_int_equal(iolink_cycle_time_encode(1328U), 0xBFU);

    /* Saturation above 132.8 ms. */
    assert_int_equal(iolink_cycle_time_encode(2000U), 0xBFU);
}

/* Table B.3: the encoded octet must never advertise a cycle shorter than the
 * requested one (MinCycleTime is a *minimum*), across the representable range
 * (0.4 .. 132.8 ms). */
static void test_cycle_time_encode_never_shorter(void** state)
{
    (void) state;

    uint16_t units;
    for (units = 4U; units <= 1328U; units += 7U) {
        uint8_t octet = iolink_cycle_time_encode(units);
        uint32_t advertised_us = iolink_cycle_time_to_us(octet);
        assert_true(advertised_us >= (uint32_t) units * 100U);
    }
}

/* Table A.8/A.10: the type's default OD octet count m. */
static void test_mseq_default_od_width(void** state)
{
    (void) state;

    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_0), 1U);
    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_1_1), 0U);
    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_1_2), 2U);
    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_1_V), 8U);
    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_2_1), 1U);
    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_2_2), 1U);
    assert_int_equal(iolink_mseq_default_od_width(IOLINK_M_SEQ_TYPE_2_V), 1U);
}

/* Table A.10: OPERATE M-sequence code (0..7) from type + OD width. */
static void test_mseq_operate_code(void** state)
{
    (void) state;

    /* Fixed types. */
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_0, 1U), 0U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_1, 1U), 0U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_2, 1U), 0U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_2, 2U), 1U);

    /* TYPE_1_V is code 6 (8 OD) or 7 (32 OD); smaller widths clamp to 6. */
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 1U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 8U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 31U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 32U), 7U);

    /* TYPE_2_V: 1 OD = code 4, 2 = 5, 8 = 6, 32 = 7. */
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_V, 1U), 4U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_V, 2U), 5U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_V, 8U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_V, 32U), 7U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_cycle_time_to_us),
        cmocka_unit_test(test_cycle_time_encode),
        cmocka_unit_test(test_cycle_time_encode_never_shorter),
        cmocka_unit_test(test_mseq_default_od_width),
        cmocka_unit_test(test_mseq_operate_code),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
