/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CRC_H
#define IOLINK_CRC_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file crc.h
 * @brief IO-Link 报文校验和（Spec V1.1.5 A.1.6）
 */

/**
 * @defgroup iolinki_crc 校验和
 * @brief IO-Link 6 位报文校验和计算（A.1.6）。
 * @{
 */

/**
 * @brief 计算 IO-Link 报文校验和（A.1.6）。
 *
 * 报文的每个八位组都与种子 0x52 进行 XOR 运算，然后
 * 使用公式（A.1）从 8 位压缩到 6 位。校验和/M 序列
 * 类型八位组（主站报文为 CKT，设备应答为 CKS）是报文的
 * 一部分，其校验和位（0-5）置零；其类型/状态位
 * 按原样纳入。调用方将返回的 6 位值 OR 进该八位组。
 *
 * @param octets 报文八位组；校验和八位组的位 0-5 必须已
 *               清零。
 * @param len @p octets 中的八位组数量。
 * @return uint8_t 压缩后的 6 位校验和（0x00-0x3F）。
 */
uint8_t iolink_checksum6(const uint8_t* octets, size_t len);

/**
 * @brief 将已 XOR 累积的 A.1.6 种子压缩为 6 位校验和。
 *
 * iolink_checksum6() 的流式对应版本：调用方将每个八位组 XOR 进
 * 以 0x52 为种子的累加器（供 ISR 接收路径使用）。
 * @param acc 累积的校验和状态（种子 0x52 XOR 所有报文八位组）。
 * @return uint8_t 压缩后的 6 位校验和（0x00-0x3F）。
 */
uint8_t iolink_checksum6_from_acc(uint8_t acc);

/** @} */ /* end of iolinki_crc */

#endif  // IOLINK_CRC_H
