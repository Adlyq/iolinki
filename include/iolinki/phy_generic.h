/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_GENERIC_H
#define IOLINK_PHY_GENERIC_H

#include "iolinki/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file phy_generic.h
 * @brief 用于真实硬件端口的参考 PHY 模板。
 *
 * 该模板提供桩实现，必须适配
 * 目标收发器以及 MCU/SoC 外设。
 */

/**
 * @defgroup iolinki_phy_generic 通用 UART PHY 适配器
 * @brief 供真实收发器硬件适配的参考 PHY 模板。
 * @{
 */

/**
 * @brief 获取通用 PHY API（模板）。
 *
 * @return const iolink_phy_api_t* 指向模板 PHY API 的指针
 */
const iolink_phy_api_t* iolink_phy_generic_get(void);

/** @} */ /* iolinki_phy_generic 结束 */

#ifdef __cplusplus
}
#endif

#endif  // IOLINK_PHY_GENERIC_H
