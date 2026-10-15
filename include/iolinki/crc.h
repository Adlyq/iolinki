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
 * @brief IO-Link message checksum (Spec V1.1.5 A.1.6)
 */

/**
 * @defgroup iolinki_crc Checksum
 * @brief IO-Link 6-bit message checksum computation (A.1.6).
 * @{
 */

/**
 * @brief Calculate the IO-Link message checksum (A.1.6).
 *
 * Every octet of the message is XOR processed with a seed of 0x52, then
 * compressed from 8 to 6 bits using equations (A.1). The checksum/M-sequence
 * type octet (CKT for master messages, CKS for device replies) is part of the
 * message with its checksum bits (0-5) set to zero; its type/status bits are
 * included as-is. Callers OR the returned 6-bit value into that octet.
 *
 * @param octets Message octets; the checksum octet must already have bits 0-5
 *               cleared.
 * @param len Number of octets in @p octets.
 * @return uint8_t 6-bit compressed checksum (0x00-0x3F).
 */
uint8_t iolink_checksum6(const uint8_t* octets, size_t len);

/**
 * @brief Compress an 8-bit XOR accumulator to the 6-bit IO-Link checksum (A.1).
 *
 * The A.1.6 checksum is `C(0x52 XOR octet0 XOR octet1 ...)`, where C is the
 * linear 8-to-6-bit compression. Because XOR accumulation is order-independent
 * and C is GF(2)-linear, a caller that streams the message octet by octet can
 * fold each octet in as `acc ^= octet` (starting from the 0x52 seed) and apply
 * this function once at the end. Used by the library ISR pump so the checksum is
 * computed while the reply is transmitted.
 *
 * @param acc 8-bit accumulator (seed already folded in)
 * @return uint8_t 6-bit compressed checksum (0x00-0x3F)
 */
uint8_t iolink_checksum6_from_acc(uint8_t acc);

/** @} */ /* end of iolinki_crc */

#endif  // IOLINK_CRC_H
