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
 * @brief 共享的 IO-Link 协议栈类型
 */

/**
 * @defgroup iolinki_core 核心类型与波特率
 * @brief 共享的 IO-Link 枚举、配置与回调类型。
 * @{
 */

/**
 * @brief IO-Link M 序列类型
 *
 * 定义通信周期的帧结构与能力。
 * 参见 IO-Link Interface and System Specification V1.1.2，第 7.3 节。
 */
typedef enum
{
    IOLINK_M_SEQ_TYPE_0 = 0U,   /**< Type 0：1 个 OD 八位组，无 PD。 */
    IOLINK_M_SEQ_TYPE_1_1 = 1U, /**< Type 1_1：2 个 PD 八位组，无 OD（仅旧版交错）。 */
    IOLINK_M_SEQ_TYPE_1_2 = 2U, /**< Type 1_2：2 个 OD 八位组，无 PD（A.7）。 */
    IOLINK_M_SEQ_TYPE_1_V = 3U, /**< Type 1_V：8 或 32 个 OD 八位组，无 PD（A.8）。 */
    IOLINK_M_SEQ_TYPE_2_1 = 4U, /**< Type 2_1：1 个 PD-in + 1 个 OD，无 PD-out（A.9）。 */
    IOLINK_M_SEQ_TYPE_2_2 = 5U, /**< Type 2_2：2 个 PD-in + 1 个 OD，无 PD-out（A.10）。 */
    IOLINK_M_SEQ_TYPE_2_V = 6U, /**< Type 2_V：0..32 个 PD 输入/输出 + 1/2/8/32 个 OD（A.14）。 */
    IOLINK_M_SEQ_TYPE_2_3 = 7U, /**< Type 2_3：1 个 PD-out + 1 个 OD，无 PD-in（A.11）。 */
    IOLINK_M_SEQ_TYPE_2_4 = 8U, /**< Type 2_4：2 个 PD-out + 1 个 OD，无 PD-in（A.12）。 */
    IOLINK_M_SEQ_TYPE_2_5 = 9U, /**< Type 2_5：1 个 PD-in + 1 个 PD-out + 1 个 OD（A.13）。 */
} iolink_m_seq_type_t;

/** @brief ::iolink_m_seq_type_t 的最大有效值（用于范围检查）。 */
#define IOLINK_M_SEQ_TYPE_MAX IOLINK_M_SEQ_TYPE_2_5

/**
 * @brief 将 Table B.3 的周期时间八位组解码为微秒。
 * @param value 周期时间八位组（位 7:6 为时基，位 5:0 为乘数）。
 * @return 周期时间，单位为微秒；保留时基返回 0。
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
 * @brief 将以 0.1 ms 为单位的周期时间编码为 Table B.3 八位组。
 *
 * 时基为位 7:6，乘数为位 5:0，参见 Table B.3。值为 0 时
 * 编码为 0x00，B.1.3 将其定义为 "no MinCycleTime provided"；低于
 * 最小值 0.4 ms 的值被钳制到 0.4 ms；高于 132.8 ms 的值
 * 饱和于 132.8 ms（时基 10，乘数 63）。位于可表示步长
 * 之间的值向上取整，因此编码得到的最小值绝不会声称
 * 比请求更短的周期时间。
 * @param units100us 周期时间，单位为 100 us（0.1 ms）。
 * @return 按 Table B.3 编码的八位组。
 */
static inline uint8_t iolink_cycle_time_encode(uint16_t units100us)
{
    if (units100us == 0U) {
        return 0x00U; /* B.1.3：未提供 MinCycleTime。 */
    }
    if (units100us < 4U) {
        units100us = 4U; /* Table B.3 的最小值为 0.4 ms。 */
    }
    if (units100us <= 63U) {
        return (uint8_t) units100us; /* 0.1 ms × 乘数：0.4 .. 6.3 ms */
    }
    if (units100us <= 316U) {
        const uint16_t m = (uint16_t) ((units100us - 64U + 3U) / 4U); /* 向上取整到 0.4 ms 步长 */
        return (uint8_t) (0x40U | (uint8_t) m);
    }
    /* 32.0 ms + 1.6 ms × 乘数。317..320 均向上取整为乘数 0。 */
    const uint32_t excess = (units100us > 320U) ? ((uint32_t) units100us - 320U) : 0U;
    uint32_t m = (excess + 15U) / 16U; /* 向上取整到 1.6 ms 步长 */
    if (m > 63U) {
        m = 63U; /* 饱和于 132.8 ms */
    }
    return (uint8_t) (0x80U | (uint8_t) m);
}

/**
 * @brief M 序列类型的默认 OD 八位组数量 m（Table A.10）。
 * @param m_seq_type M 序列类型值。
 * @return 默认 OD 宽度，以字节计。
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
 * @brief 给定类型与 OD 宽度时，返回 OPERATE M 序列代码（Table A.10）。
 * @param m_seq_type M 序列类型值。
 * @param od_width OD 八位组数量 m。
 * @return OPERATE 代码 0..7。
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
 * @brief IO-Link 协议栈配置
 *
 * 保存初始化协议栈行为所需的参数。
 */
typedef struct
{
    iolink_m_seq_type_t m_seq_type; /**< 设备支持的主 M 序列类型 */
    uint8_t min_cycle_time;         /**< 最小周期时间，单位为 0.1ms（例如 20 = 2.0ms） */
    uint8_t pd_in_len;              /**< 过程数据输入长度（设备到主站），以字节计 */
    uint8_t pd_out_len;             /**< 过程数据输出长度（主站到设备），以字节计 */
    uint32_t t_pd_us;               /**< 上电延迟（t_pd），单位为微秒 */
    uint8_t od_width;               /**< OD 八位组数（Table A.10）；0 表示由 m_seq_type 推导 */
    iolink_baudrate_t baudrate;     /**< baudrate_set 为 true 时使用的 COM */
    bool baudrate_set;              /**< 初始化时应用波特率；false 保持 COM2 默认值 */
    uint8_t master_lost_cycles;     /**< 触发 master_lost 前允许丢失的主站周期数；0 表示禁用 */
} iolink_stack_config_t;

/**
 * @brief 传递给应用复位处理器的复位请求类型。
 */
typedef enum
{
    IOLINK_RESET_DEVICE = 0,     /**< System Command 0x80（设备复位） */
    IOLINK_RESET_APPLICATION = 1 /**< System Command 0x81（应用复位） */
} iolink_reset_type_t;

/**
 * @brief 主站请求复位时调用的应用回调。
 *
 * @param type IOLINK_RESET_DEVICE 或 IOLINK_RESET_APPLICATION
 */
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);

/** @} */ /* end of iolinki_core */

#endif  // IOLINK_H
