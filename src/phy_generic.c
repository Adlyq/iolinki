/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file phy_generic.c
 * @brief 模板/骨架式通用 UART PHY 适配器。
 * @ingroup iolinki_phy_generic
 *
 * 提供一个非功能性的参考 PHY 实现，其钩子需由目标相关的
 * UART/GPIO/收发器代码填充。
 */

#include "iolinki/phy_generic.h"

/** @brief 模板 PHY 初始化钩子（未实现前返回失败）。 */
static int generic_init(void* user)
{
    (void) user;
    /* 模板：在此初始化 UART/GPIO/收发器。 */
    return -1;
}

/** @brief 用于为 SIO/SDCI 模式配置收发器的模板钩子。 */
static void generic_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
    /* 模板：为 SIO/SDCI 配置收发器。 */
}

/** @brief 用于配置 COM1/2/3 下 UART 速率的模板钩子。 */
static void generic_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
    /* 模板：配置 COM1/2/3 的 UART 速率。 */
}

#if IOLINK_PHY_FEATURE_WAKEUP
/** @brief 用于检测 C/Q 线上 80us 唤醒脉冲的模板钩子。 */
static int generic_detect_wakeup(void* user)
{
    (void) user;
    /* 模板：检测 C/Q 线上的 80us 唤醒脉冲。 */
    return 0;
}
#endif

#if IOLINK_PHY_FEATURE_SIO_CQ
/** @brief 用于在 SIO 模式下驱动 C/Q 线高/低的模板钩子。 */
static void generic_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    (void) state;
    /* 模板：在 SIO 模式下驱动 C/Q 线高/低。 */
}
#endif

#if IOLINK_PHY_FEATURE_LEGACY_DIAG
/** @brief 用于读取电源电压（mV）的模板钩子（不可用时返回负值）。 */
static int generic_get_voltage_mv(void* user)
{
    (void) user;
    /* 模板：返回电源电压（mV），不可用时返回负值。 */
    return -1;
}

/** @brief 检测到短路故障时返回真的模板钩子。 */
static bool generic_is_short_circuit(void* user)
{
    (void) user;
    /* 模板：若检测到故障则返回真。 */
    return false;
}
#endif

static const iolink_phy_api_t g_phy_generic = {.init = generic_init,
                                               .set_mode = generic_set_mode,
                                               .set_baudrate = generic_set_baudrate,
#if IOLINK_PHY_FEATURE_WAKEUP
                                               .detect_wakeup = generic_detect_wakeup,
#endif
#if IOLINK_PHY_FEATURE_SIO_CQ
                                               .set_cq_line = generic_set_cq_line,
#endif
#if IOLINK_PHY_FEATURE_LEGACY_DIAG
                                               .get_voltage_mv = generic_get_voltage_mv,
                                               .is_short_circuit = generic_is_short_circuit
#endif
};

const iolink_phy_api_t* iolink_phy_generic_get(void)
{
    return &g_phy_generic;
}
