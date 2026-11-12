/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_crc.c
 * @brief IO-Link A.1.6 报文校验和的单元测试
 *
 * 期望值来自 IO-Link Interface Specification V1.1.5 A.1.6 中的
 * 规范性公式 / 公式（A.1），而不是来自被测实现。
 * 传入校验和字节时其校验和位（0-5）须已清零；
 * type/event/PD-status 位保持不变。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/crc.h"

/* 通过清零 bit 0-5 规范化校验和/类型字节（A.1.6）。 */
static uint8_t zero_checksum_bits(uint8_t octet)
{
    return (uint8_t) (octet & 0xC0U);
}

static void test_checksum6_master_frames(void** state)
{
    (void) state;
    /* 主站帧：MC、CKT（仅类型位）、数据... */
    const uint8_t frame1[] = {0x00, 0x00}; /* (0x00,0x00) -> 0x2D */
    assert_int_equal(iolink_checksum6(frame1, 2U), 0x2DU);

    const uint8_t frame2[] = {0xA2, 0x00}; /* (0xA2,0x00) -> 0x00 */
    assert_int_equal(iolink_checksum6(frame2, 2U), 0x00U);

    const uint8_t frame3[] = {0x20, 0x00, 0x99}; /* -> 0x06 */
    assert_int_equal(iolink_checksum6(frame3, 3U), 0x06U);

    /* 一个 TYPE_1 写帧 MC=0x00、CKT 类型位=0x40、OD=A5、CK=5A。 */
    const uint8_t frame4[] = {0x00, 0x40, 0xA5, 0x5A};
    assert_int_equal(iolink_checksum6(frame4, 4U), 0x35U);

    /* 一个 TYPE_2 读帧 MC=0x80、CKT 类型位=0x80 -> 0x2D。 */
    const uint8_t frame5[] = {0x80, 0x80};
    assert_int_equal(iolink_checksum6(frame5, 2U), 0x2DU);
}

static void test_checksum6_device_replies(void** state)
{
    (void) state;
    /* 设备应答：数据...、CKS（event<<7 | pdinvalid<<6 | ck6）。 */
    const uint8_t reply1[] = {0x10}; /* -> 0x39 */
    assert_int_equal(iolink_checksum6(reply1, 1U), 0x39U);

    const uint8_t reply2[] = {0xA5}; /* -> 0x22 */
    assert_int_equal(iolink_checksum6(reply2, 1U), 0x22U);

    /* 标志位在压缩前参与 XOR 运算：在 0xA5 上叠加 Event 标志
       会改变结果（A.1.6）。 */
    const uint8_t reply3[] = {0xC5};
    assert_int_equal(iolink_checksum6(reply3, 1U), 0x1EU);

    const uint8_t reply4[] = {0x00}; /* 空数据 -> 0x2D */
    assert_int_equal(iolink_checksum6(reply4, 1U), 0x2DU);
}

static void test_checksum6_zeroing_is_caller_duty(void** state)
{
    (void) state;
    /* 该辅助函数要求 bit 0-5 已清零。验证把同一字节的低位
       清零后能得到文档中给出的值。 */
    const uint8_t with_bits[] = {0x00, 0x2DU};
    const uint8_t cleared[] = {zero_checksum_bits(with_bits[0]), zero_checksum_bits(with_bits[1])};
    assert_int_equal(iolink_checksum6(cleared, 2U), 0x2DU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_checksum6_master_frames),
        cmocka_unit_test(test_checksum6_device_replies),
        cmocka_unit_test(test_checksum6_zeroing_is_caller_duty),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
