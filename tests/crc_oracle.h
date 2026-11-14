/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file crc_oracle.h
 * @brief 仅测试用：按 A.1.6 独立实现的期望值校验和，不复用 src/crc.c。
 *
 * 定义（IO-Link Interface Specification V1.1.5 A.1.6 + Table A.1）：
 * 所有八位组（含校验和八位组，其 bit 0-5 已清零）从初值/种子 0x52
 * 起逐字节 XOR；得到的 8 位累加器按 Table A.1 压缩为 6 位：
 *   D5 = parity(D7 ^ D5 ^ D3 ^ D1)   掩码 0xAA
 *   D4 = parity(D6 ^ D4 ^ D2 ^ D0)   掩码 0x55
 *   D3 = parity(D7 ^ D6)             掩码 0xC0
 *   D2 = parity(D5 ^ D4)             掩码 0x30
 *   D1 = parity(D3 ^ D2)             掩码 0x0C
 *   D0 = parity(D1 ^ D0)             掩码 0x03
 */

#ifndef CRC_ORACLE_H_
#define CRC_ORACLE_H_

#include <stddef.h>
#include <stdint.h>

static inline uint8_t test_expected_parity(uint8_t mask)
{
    mask ^= (uint8_t) (mask >> 4U);
    mask ^= (uint8_t) (mask >> 2U);
    mask ^= (uint8_t) (mask >> 1U);
    return (uint8_t) (mask & 0x01U);
}

static inline uint8_t test_expected_checksum6_from_acc(uint8_t acc)
{
    uint8_t ck6 = 0U;

    ck6 |= (uint8_t) (test_expected_parity((uint8_t) (acc & 0xAAU)) << 5U);
    ck6 |= (uint8_t) (test_expected_parity((uint8_t) (acc & 0x55U)) << 4U);
    ck6 |= (uint8_t) (test_expected_parity((uint8_t) (acc & 0xC0U)) << 3U);
    ck6 |= (uint8_t) (test_expected_parity((uint8_t) (acc & 0x30U)) << 2U);
    ck6 |= (uint8_t) (test_expected_parity((uint8_t) (acc & 0x0CU)) << 1U);
    ck6 |= test_expected_parity((uint8_t) (acc & 0x03U));

    return ck6;
}

static inline uint8_t test_expected_checksum6(const uint8_t* octets, size_t len)
{
    uint8_t acc = 0x52U;

    for (size_t i = 0U; i < len; i++) {
        acc = (uint8_t) (acc ^ octets[i]);
    }

    return test_expected_checksum6_from_acc(acc);
}

#endif  // CRC_ORACLE_H_
