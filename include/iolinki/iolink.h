/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_H
#define IOLINK_H

#include <stdint.h>
#include "iolinki/phy.h"

/**
 * @file iolink.h
 * @brief Shared IO-Link stack types
 */

/**
 * @defgroup iolinki_core Core Types and Baudrates
 * @brief Shared IO-Link enumerations, configuration and callback types.
 * @{
 */

/**
 * @brief IO-Link M-sequence types
 *
 * Defines the frame structure and capabilities of the communication cycle.
 * See IO-Link Interface and System Specification V1.1.2, section 7.3.
 */
typedef enum
{
    IOLINK_M_SEQ_TYPE_0 = 0U,   /**< Type 0: OD only, no PD (Figure A.5). */
    IOLINK_M_SEQ_TYPE_1_1 = 1U, /**< Type 1_1: PD (fixed), OD = 0 octets (Figure A.6). */
    IOLINK_M_SEQ_TYPE_1_2 = 2U, /**< Type 1_2: OD = 2 octets, no PD (Table A.10 code 1). */
    IOLINK_M_SEQ_TYPE_1_V = 3U, /**< Type 1_V: OD = 8 or 32 octets, no PD (codes 2/6/7). */
    IOLINK_M_SEQ_TYPE_2_1 = 4U, /**< Type 2_1: PD 1 byte, OD = 1 octet. */
    IOLINK_M_SEQ_TYPE_2_2 = 5U, /**< Type 2_2: PD 2 bytes, OD = 1 octet. */
    IOLINK_M_SEQ_TYPE_2_V = 6U, /**< Type 2_V: PD variable, OD = 1/2/8/32 octets (codes 4/5/6/7). */
} iolink_m_seq_type_t;

/**
 * @brief Decode a Direct Parameter cycle-time octet to microseconds (Table B.3).
 *
 * The octet is "time base (bits 7:6) | multiplier (bits 5:0)":
 *   00 -> multiplier * 0.1 ms (0.4..6.3 ms)
 *   01 -> 6.4 ms + multiplier * 0.4 ms
 *   10 -> 32.0 ms + multiplier * 1.6 ms
 *   11 -> Reserved.
 *
 * @param value Encoded MasterCycleTime / MinCycleTime octet
 * @return Cycle time in microseconds; 0 for the reserved time base
 */
static inline uint32_t iolink_cycle_time_to_us(uint8_t value)
{
    switch (value >> 6) {
        case 0U:
            return (uint32_t) (value & 0x3FU) * 100U;
        case 1U:
            return 6400U + (uint32_t) (value & 0x3FU) * 400U;
        case 2U:
            return 32000U + (uint32_t) (value & 0x3FU) * 1600U;
        default:
            return 0U;
    }
}

/**
 * @brief Default On-request Data width for an M-sequence type (Table A.10/A.8).
 *
 * This is the type's OD octet count m. TYPE_1_V and TYPE_2_V are variable, so the
 * returned value is only a default; a device that needs another width (e.g. the
 * 2/8/32-octet TYPE_2_V variants) must set iolink_config_t::od_width.
 *
 * @param m_seq_type M-sequence type
 * @return Default OD width in octets
 */
static inline uint8_t iolink_mseq_default_od_width(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_1:
            return 0U; /* Figure A.6: no OD channel */
        case IOLINK_M_SEQ_TYPE_1_2:
            return 2U; /* Table A.10 code 1 */
        case IOLINK_M_SEQ_TYPE_1_V:
            return 8U; /* OPERATE code 6; 32 (code 7) with an explicit od_width */
        default:
            return 1U; /* TYPE_0 / 2_1 / 2_2 / 2_V default (code 0 / 4) */
    }
}

/**
 * @brief OPERATE M-sequence code for a type + OD width (Table A.10).
 *
 * Single source of truth for the M-sequenceCapability advertisement
 * (Direct Parameter 0x03, Figure B.3) and the frame-width derivation.
 *
 * @param m_seq_type M-sequence type
 * @param od_width   OD width in octets
 * @return OPERATE code 0..7
 */
static inline uint8_t iolink_mseq_operate_code(uint8_t m_seq_type, uint8_t od_width)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_2:
            return 1U; /* 2 OD, no PD */
        case IOLINK_M_SEQ_TYPE_1_V:
            /* OPERATE TYPE_1_V is only code 6 (8 OD) or 7 (32 OD); widths below
               8 are invalid for this type, so they clamp to the code-6 minimum. */
            return (od_width >= 32U) ? 7U : 6U;
        case IOLINK_M_SEQ_TYPE_2_V:
            switch (od_width) {
                case 2U:
                    return 5U;
                case 8U:
                    return 6U;
                case 32U:
                    return 7U;
                default:
                    return 4U; /* 1 OD */
            }
        default:
            return 0U; /* TYPE_0 / 2_1 / 2_2 */
    }
}

/**
 * @brief IO-Link stack configuration
 *
 * Holds parameters required to initialize the stack behavior.
 */
typedef struct
{
    iolink_m_seq_type_t m_seq_type; /**< Primary M-sequence type supported by device */
    uint8_t od_width;               /**< OPERATE M-sequence OD width m (Table A.10:
                                         1/2/8/32); 0 = derive from m_seq_type */
    uint8_t min_cycle_time;         /**< Minimum cycle time octet (Table B.3 coding) */
    uint8_t pd_in_len;              /**< Process Data Input length (Device to Master) in bytes */
    uint8_t pd_out_len;             /**< Process Data Output length (Master to Device) in bytes */
    uint32_t t_pd_us;               /**< Power-on delay (t_pd) in microseconds */
    iolink_baudrate_t baudrate;     /**< Device operating baudrate (COM1..COM3), fixed. */
} iolink_config_t;

/**
 * @brief Reset request type delivered to the application reset handler.
 */
typedef enum
{
    IOLINK_RESET_DEVICE = 0,     /**< System Command 0x80 (Device Reset) */
    IOLINK_RESET_APPLICATION = 1 /**< System Command 0x81 (Application Reset) */
} iolink_reset_type_t;

/**
 * @brief Application callback invoked when the Master requests a reset.
 *
 * @param type IOLINK_RESET_DEVICE or IOLINK_RESET_APPLICATION
 */
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);

/** @} */ /* end of iolinki_core */

#endif  // IOLINK_H
