/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PLATFORM_H
#define IOLINK_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#include "iolinki/port.h"

/**
 * @file platform.h
 * @brief 面向 RTOS 与裸机端口的平台封装。
 *
 * 原语集合、其能力等级以及需求的推导位于
 * iolinki/port.h，并与
 * iolink_critical_enter()、iolink_critical_exit() 和
 * iolink_platform_critical_sections_ready() 的声明放在一起。本头文件保留历史上的
 * 平台入口点，使仅包含 platform.h 的现有端口可以
 * 无需修改地继续编译。
 *
 * 端口实现其并发配置所需的原语
 * （iolinki/port.h 中的 `IOLINK_NEED_*`），其余可以省略。每个
 * 异步配置（`IOLINK_CONCURRENCY >= ISR`）都需要临界区；
 * 一个 `SINGLE` 配置从不引用这些符号，因此裸机或
 * 宿主端口在其中无需临界区实现。
 *
 * 实现必须是嵌套安全的，并且可从中断上下文调用：库可能
 * 在已处于临界区内时再次进入（一次会复位事件队列的
 * DLL 状态迁移），而 DLL 状态迁移本身也可能来自
 * 数据 ISR。因此采用 PRIMASK 保存/恢复的端口必须
 * 维护嵌套深度（STM32G0 原始 PHY 示例就是这样做的）。
 * 库在每个上下文中只有一个主上下文调用方：数据 ISR 可能
 * 抢占它，但其他任务不得进入 iolink_dll_process() /
 * iolink_device_process()，应用也不得用自己的锁包裹入口点
 * （临界区的粒度由库掌控）。因此，一个同时串行化任务的
 * 端口临界区并不能使并发 API 调用获得支持；它只会给
 * 调用方增加延迟。
 *
 * 完整约定、各平台映射以及禁止的组合参见
 * docs/PORTING.md。
 */

/**
 * @defgroup iolinki_platform 平台抽象
 * @brief 集成方提供的临界区与非易失性内存钩子。
 * @{
 */

/** @} */ /* end of iolinki_platform */

#endif  // IOLINK_PLATFORM_H
