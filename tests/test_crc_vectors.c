/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_crc_vectors.c
 * @brief A.1.6 校验和：已知压缩向量（主站帧与设备应答），256 值穷举，
 *        以及 ISR 发送路径所使用的流式（累加器）实现与之一致性。
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
#include "test_null.h"

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

/* 用 A.1 公式压缩 8 位累加器，与被测实现无关
 * （Table A.1）。 */
static uint8_t reference_compress(uint8_t acc)
{
    const uint8_t d7 = (uint8_t) ((acc >> 7U) & 1U);
    const uint8_t d6 = (uint8_t) ((acc >> 6U) & 1U);
    const uint8_t d5 = (uint8_t) ((acc >> 5U) & 1U);
    const uint8_t d4 = (uint8_t) ((acc >> 4U) & 1U);
    const uint8_t d3 = (uint8_t) ((acc >> 3U) & 1U);
    const uint8_t d2 = (uint8_t) ((acc >> 2U) & 1U);
    const uint8_t d1 = (uint8_t) ((acc >> 1U) & 1U);
    const uint8_t d0 = (uint8_t) (acc & 1U);

    uint8_t out = 0U;
    out |= (uint8_t) ((d7 ^ d5 ^ d3 ^ d1) << 5U);
    out |= (uint8_t) ((d6 ^ d4 ^ d2 ^ d0) << 4U);
    out |= (uint8_t) ((d7 ^ d6) << 3U);
    out |= (uint8_t) ((d5 ^ d4) << 2U);
    out |= (uint8_t) ((d3 ^ d2) << 1U);
    out |= (uint8_t) (d1 ^ d0);
    return out;
}

static void test_checksum6_from_acc_vectors(void** state)
{
    (void) state;

    /* 穷举比较流式压缩器与 A.1 公式的结果。 */
    for (uint16_t acc = 0U; acc <= 0xFFU; acc++) {
        assert_int_equal(iolink_checksum6_from_acc((uint8_t) acc),
                         reference_compress((uint8_t) acc));
    }

    /* 从公式中直接读出的一些显式值。 */
    assert_int_equal(iolink_checksum6_from_acc(0x52U), 0x2DU); /* 仅种子值 */
    assert_int_equal(iolink_checksum6_from_acc(0x00U), 0x00U);
    assert_int_equal(iolink_checksum6_from_acc(0xFFU), 0x00U);
    assert_int_equal(iolink_checksum6_from_acc(0x01U), 0x11U);
    assert_int_equal(iolink_checksum6_from_acc(0x80U), 0x28U);
}

static void test_checksum6_from_acc_matches_stream(void** state)
{
    (void) state;

    const uint8_t frame1[] = {0x00U, 0x00U};
    const uint8_t frame2[] = {0xA2U, 0x00U};
    const uint8_t frame3[] = {0x20U, 0x00U, 0x99U};
    const uint8_t frame4[] = {0x00U, 0x40U, 0xA5U, 0x5AU};
    const uint8_t frame5[] = {0x80U, 0x80U};
    const uint8_t frame6[] = {0x10U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U};

    const uint8_t* frames[] = {frame1, frame2, frame3, frame4, frame5, frame6};
    const size_t lens[] = {sizeof(frame1), sizeof(frame2), sizeof(frame3),
                           sizeof(frame4), sizeof(frame5), sizeof(frame6)};

    for (size_t f = 0U; f < (sizeof(frames) / sizeof(frames[0])); f++) {
        uint8_t acc = 0x52U;
        for (size_t i = 0U; i < lens[f]; i++) {
            acc = (uint8_t) (acc ^ frames[f][i]);
        }
        assert_int_equal(iolink_checksum6_from_acc(acc), iolink_checksum6(frames[f], lens[f]));
    }
}

static void test_checksum6_empty_message(void** state)
{
    (void) state;

    /* len == 0 时循环不执行，校验和即种子值折叠一次的结果。缓冲区契约
     * 仍要求非空，因此这里用 TEST_NULL 显式违反它。 */
    assert_int_equal(iolink_checksum6(TEST_NULL(const uint8_t*), 0U), 0x2DU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_checksum6_master_frames),
        cmocka_unit_test(test_checksum6_device_replies),
        cmocka_unit_test(test_checksum6_zeroing_is_caller_duty),
        cmocka_unit_test(test_checksum6_from_acc_vectors),
        cmocka_unit_test(test_checksum6_from_acc_matches_stream),
        cmocka_unit_test(test_checksum6_empty_message),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
