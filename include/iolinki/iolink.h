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
#include <stdbool.h>
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
    IOLINK_M_SEQ_TYPE_0 = 0U,   /**< Type 0: 1 OD octet, no PD. */
    IOLINK_M_SEQ_TYPE_1_1 = 1U, /**< Type 1_1: 2 PD octets, no OD (legacy interleave only). */
    IOLINK_M_SEQ_TYPE_1_2 = 2U, /**< Type 1_2: 2 OD octets, no PD (A.7). */
    IOLINK_M_SEQ_TYPE_1_V = 3U, /**< Type 1_V: 8 or 32 OD octets, no PD (A.8). */
    IOLINK_M_SEQ_TYPE_2_1 = 4U, /**< Type 2_1: 1 PD-in + 1 OD, no PD-out (A.9). */
    IOLINK_M_SEQ_TYPE_2_2 = 5U, /**< Type 2_2: 2 PD-in + 1 OD, no PD-out (A.10). */
    IOLINK_M_SEQ_TYPE_2_3 = 7U, /**< Type 2_3: 1 PD-out + 1 OD, no PD-in (A.11). */
    IOLINK_M_SEQ_TYPE_2_4 = 8U, /**< Type 2_4: 2 PD-out + 1 OD, no PD-in (A.12). */
    IOLINK_M_SEQ_TYPE_2_5 = 9U, /**< Type 2_5: 1 PD-in + 1 PD-out + 1 OD (A.13). */
    IOLINK_M_SEQ_TYPE_2_V = 6U, /**< Type 2_V: 0..32 PD in/out + 1/2/8/32 OD (A.14). */
} iolink_m_seq_type_t;

/** @brief Highest valid ::iolink_m_seq_type_t value (used for range checks). */
#define IOLINK_M_SEQ_TYPE_MAX IOLINK_M_SEQ_TYPE_2_5

/**
 * @brief Decode a Table B.3 cycle-time octet into microseconds.
 * @param value Cycle-time octet (time base in bits 7:6, multiplier in 5:0).
 * @return Cycle time in microseconds; 0 for the reserved time base.
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
 * @brief Encode a cycle time in 0.1 ms units to a Table B.3 octet.
 *
 * Time base bits 7:6 and multiplier bits 5:0 per Table B.3. Values below the
 * 0.4 ms minimum are clamped to 0.4 ms; values above 132.8 ms saturate at
 * 132.8 ms (time base 10, multiplier 63).
 * @param units100us Cycle time in 100 us (0.1 ms) units.
 * @return Table B.3 encoded octet.
 */
static inline uint8_t iolink_cycle_time_encode(uint16_t units100us)
{
    if (units100us < 4U) {
        units100us = 4U; /* Table B.3 minimum is 0.4 ms. */
    }
    if (units100us <= 63U) {
        return (uint8_t) units100us; /* 0.4 .. 6.3 ms */
    }
    if (units100us <= 316U) {
        uint16_t m = (uint16_t) ((units100us - 64U + 3U) / 4U); /* ceil to 0.4 ms */
        if (m > 63U) {
            m = 63U;
        }
        return (uint8_t) (0x40U | (uint8_t) m);
    }
    uint32_t m = ((uint32_t) units100us - 320U + 15U) / 16U; /* ceil to 1.6 ms */
    if (m > 63U) {
        m = 63U;
    }
    return (uint8_t) (0x80U | (uint8_t) m);
}

/**
 * @brief Default OD octet count m for an M-sequence type (Table A.10).
 * @param m_seq_type M-sequence type value.
 * @return Default OD width in bytes.
 */
static inline uint8_t iolink_mseq_default_od_width(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_1:
            return 0U;
        case IOLINK_M_SEQ_TYPE_1_2:
            return 2U;
        case IOLINK_M_SEQ_TYPE_1_V:
            return 8U;
        default:
            return 1U;
    }
}

/**
 * @brief OPERATE M-sequence code (Table A.10) for a type and OD width.
 * @param m_seq_type M-sequence type value.
 * @param od_width OD octet count m.
 * @return OPERATE code 0..7.
 */
static inline uint8_t iolink_mseq_operate_code(uint8_t m_seq_type, uint8_t od_width)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_2:
            return 1U;
        case IOLINK_M_SEQ_TYPE_1_V:
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
                    return 4U;
            }
        default:
            return 0U;
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
    uint8_t min_cycle_time;         /**< Minimum cycle time in 0.1ms units (e.g. 20 = 2.0ms) */
    uint8_t pd_in_len;              /**< Process Data Input length (Device to Master) in bytes */
    uint8_t pd_out_len;             /**< Process Data Output length (Master to Device) in bytes */
    uint32_t t_pd_us;               /**< Power-on delay (t_pd) in microseconds */
    uint8_t od_width;               /**< OD octets (Table A.10); 0 = derive from m_seq_type */
    iolink_baudrate_t baudrate;     /**< Operating COM when baudrate_set is true */
    bool baudrate_set;              /**< Apply baudrate at init; false keeps the COM2 default */
    uint8_t master_lost_cycles;     /**< Missed master cycles before master_lost; 0 = disabled */
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
