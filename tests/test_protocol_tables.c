/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_protocol_tables.c
 * @brief iolink.h 中纯规范表辅助函数的单元测试：
 *        周期时间解码（Table B.3）、默认 OD 宽度（Table A.8/A.10）
 *        以及 OPERATE M-sequence 编码（Table A.10）。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/iolink.h"

#include "conformance_test.h"

/* Table B.3：周期时间字节 = 时间基准（bit 7:6）| 乘数（bit 5:0）。 */
static void test_cycle_time_to_us(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0090");
    (void) state;

    /* 时间基准 00：乘数 * 0.1 ms。 */
    assert_int_equal(iolink_cycle_time_to_us(0x00U), 0U);
    assert_int_equal(iolink_cycle_time_to_us(0x01U), 100U);
    assert_int_equal(iolink_cycle_time_to_us(0x3FU), 6300U);

    /* 时间基准 01：6.4 ms + 乘数 * 0.4 ms。 */
    assert_int_equal(iolink_cycle_time_to_us(0x40U), 6400U);
    assert_int_equal(iolink_cycle_time_to_us(0x41U), 6800U);
    assert_int_equal(iolink_cycle_time_to_us(0x7FU), 31600U);

    /* 时间基准 10：32.0 ms + 乘数 * 1.6 ms。 */
    assert_int_equal(iolink_cycle_time_to_us(0x80U), 32000U);
    assert_int_equal(iolink_cycle_time_to_us(0x81U), 33600U);
    assert_int_equal(iolink_cycle_time_to_us(0xBFU), 132800U);

    /* 时间基准 11 为保留值：解码为 0（不支持）。 */
    assert_int_equal(iolink_cycle_time_to_us(0xC0U), 0U);
    assert_int_equal(iolink_cycle_time_to_us(0xFFU), 0U);
}

/* Table B.3：把以 0.1 ms 为单位的周期时间编码为时间基准 | 乘数字节。 */
static void test_cycle_time_encode(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0090");
    (void) state;

    /* B.1.3：0 表示“未提供 MinCycleTime”，保持为 0x00。 */
    assert_int_equal(iolink_cycle_time_encode(0U), 0x00U);

    /* 低于 0.4 ms 的下限：钳制到 0x04（0.4 ms）。 */
    assert_int_equal(iolink_cycle_time_encode(1U), 0x04U);
    assert_int_equal(iolink_cycle_time_encode(3U), 0x04U);
    assert_int_equal(iolink_cycle_time_encode(4U), 0x04U);

    /* 时间基准 00：以 0.1 ms 精确步进直到 6.3 ms。 */
    assert_int_equal(iolink_cycle_time_encode(63U), 0x3FU);

    /* 时间基准 01：6.4 .. 31.6 ms，向上取整到 0.4 ms 步进。 */
    assert_int_equal(iolink_cycle_time_encode(64U), 0x40U);  /* 6.4 ms */
    assert_int_equal(iolink_cycle_time_encode(65U), 0x41U);  /* 6.8 ms >= 6.5 ms */
    assert_int_equal(iolink_cycle_time_encode(316U), 0x7FU); /* 31.6 ms */

    /* 时间基准 10：32.0 .. 132.8 ms，向上取整到 1.6 ms 步进。 */
    assert_int_equal(iolink_cycle_time_encode(317U), 0x80U); /* 32.0 ms >= 31.7 ms */
    assert_int_equal(iolink_cycle_time_encode(320U), 0x80U); /* 32.0 ms */
    assert_int_equal(iolink_cycle_time_encode(321U), 0x81U); /* 33.6 ms >= 32.1 ms */
    assert_int_equal(iolink_cycle_time_encode(1328U), 0xBFU);

    /* 超过 132.8 ms 时饱和。 */
    assert_int_equal(iolink_cycle_time_encode(2000U), 0xBFU);
}

/* Table B.3：编码后的字节绝不能声明比请求值更短的周期
 * （MinCycleTime 是 *最小值*），覆盖全部可表示范围
 * （0.4 .. 132.8 ms）。 */
static void test_cycle_time_encode_never_shorter(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0375");
    (void) state;

    uint16_t units;
    for (units = 4U; units <= 1328U; units += 7U) {
        uint8_t octet = iolink_cycle_time_encode(units);
        uint32_t advertised_us = iolink_cycle_time_to_us(octet);
        assert_true(advertised_us >= (uint32_t) units * 100U);
    }
}

/* 独立的 Table B.3 解码器，作为所有可能字节的参考实现。 */
static uint32_t reference_b3_us(uint8_t octet)
{
    switch (octet >> 6) {
        case 0:
            return (uint32_t) (octet & 0x3FU) * 100U; /* 0.1 ms 基准 */
        case 1:
            return 6400U + (uint32_t) (octet & 0x3FU) * 400U; /* 0.4 ms 基准 */
        case 2:
            return 32000U + (uint32_t) (octet & 0x3FU) * 1600U; /* 1.6 ms 基准 */
        default:
            return 0U; /* 保留 */
    }
}

/* Table B.3：每个字节的解码结果都与独立参考表完全一致，
 * 支持范围为 0.4 .. 132.8 ms（另加 0x00 = 无 MinCycleTime）。 */
static void test_cycle_time_table_b3_matches_reference(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0375");
    (void) state;

    for (uint32_t octet = 0U; octet <= 0xFFU; octet++) {
        const uint32_t expected = reference_b3_us((uint8_t) octet);
        assert_int_equal(iolink_cycle_time_to_us((uint8_t) octet), expected);
        if ((octet >= 0x04U) && (octet <= 0xBFU)) {
            assert_true((expected >= 400U) && (expected <= 132800U));
        }
        else if ((octet == 0x00U) || (octet >= 0xC0U)) {
            /* 0x00 = 无 MinCycleTime；0b11 时间基准为保留值。 */
            assert_int_equal(expected, 0U);
        }
    }

    /* Table B.3 中给出的边界值。 */
    assert_int_equal(reference_b3_us(0x04U), 400U);
    assert_int_equal(reference_b3_us(0x3FU), 6300U);
    assert_int_equal(reference_b3_us(0x40U), 6400U);
    assert_int_equal(reference_b3_us(0x7FU), 31600U);
    assert_int_equal(reference_b3_us(0x80U), 32000U);
    assert_int_equal(reference_b3_us(0xBFU), 132800U);
}

/* Table A.8/A.10：该类型默认的 OD 字节数 m。 */
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

/* Table A.10：由类型 + OD 宽度得到 OPERATE M-sequence 编码（0..7）。 */
static void test_mseq_operate_code(void** state)
{
    (void) state;

    /* 固定类型。 */
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_0, 1U), 0U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_1, 1U), 0U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_2_2, 1U), 0U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_2, 2U), 1U);

    /* TYPE_1_V 为编码 6（8 OD）或 7（32 OD）；更小的宽度钳制到 6。 */
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 1U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 8U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 31U), 6U);
    assert_int_equal(iolink_mseq_operate_code(IOLINK_M_SEQ_TYPE_1_V, 32U), 7U);

    /* TYPE_2_V：1 OD = 编码 4，2 = 5，8 = 6，32 = 7。 */
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
        cmocka_unit_test(test_cycle_time_table_b3_matches_reference),
        cmocka_unit_test(test_mseq_default_od_width),
        cmocka_unit_test(test_mseq_operate_code),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
