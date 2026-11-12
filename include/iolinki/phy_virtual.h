/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_VIRTUAL_H
#define IOLINK_PHY_VIRTUAL_H

#include "iolinki/phy.h"

/**
 * @file phy_virtual.h
 * @brief 用于仿真的虚拟 PHY 实现
 */

/**
 * @defgroup iolinki_phy_virtual 虚拟 PHY（回环/测试）
 * @brief 由 Socket/TTY 支撑的 PHY，用于仿真 IO-Link 主站。
 * @{
 */

/**
 * @brief 获取虚拟 PHY 提供者
 *
 * 该 PHY 通过伪终端与虚拟 IO-Link 主站通信。
 * 它实现 ISR BYTE 钩子
 * （`rx_octet`/`tx_octet`/`tx_done`/`int_active`/`request_service`）。
 * 有意不实现 ISR FRAME 模式：PTY 是不带
 * 帧边界的字节流。`int_active()` 仅在 SDCI 中声明需要服务，因此泵送
 * 不会消费 SIO 唤醒字节。
 *
 * @return const iolink_phy_api_t*
 */
const iolink_phy_api_t* iolink_phy_virtual_get(void);

/**
 * @brief 设置虚拟 PHY 的串口
 * @param port TTY 路径（例如 "/dev/pts/5"）
 */
void iolink_phy_virtual_set_port(const char* port);

/** @} */ /* iolinki_phy_virtual 结束 */

#endif  // IOLINK_PHY_VIRTUAL_H
