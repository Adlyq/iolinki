/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_APPLICATION_H
#define IOLINK_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/config.h"
#include "iolinki/dll.h"

/**
 * @brief PD 输入路径（运行时选择，默认值与完整说明见 config.h）。
 *
 * 两条路径不再互斥：`iolink_device_pd_input_update()` 始终可用；如果
 * `iolink_app_callbacks_t::on_pd_input` 非 NULL，DLL 会在每个 OPERATE
 * 周期改用它填充空闲的 PD-in 缓冲区。混用（同一次初始化既给回调又
 * 轮询）会在 `iolink_device_init()` 被拒绝：能力位
 * `IOLINK_CAP_PD_IN_CALLBACK` 一旦置位，轮询调用返回
 * `IOLINK_DEVICE_INIT_ERR_PD_INPUT` 的同类约束由
 * `iolink_device_pd_input_update()` 的返回值表达（见 device.c）。
 */

/**
 * @file application.h
 * @brief 用于过程数据的 IO-Link 应用层 API
 */

/**
 * @defgroup iolinki_application 应用层回调
 * @brief 应用生命周期与过程数据回调钩子。
 * @{
 */

/**
 * @brief 应用生命周期与过程数据回调。
 *
 * 任何字段都可以为 NULL。`on_pd_input` 非 NULL 时，它是 PD 输入的
 * 唯一来源（DLL 每周期调用它）；为 NULL 时应用改用
 * `iolink_device_pd_input_update()` 轮询。两条路径可以共存于同一份
 * 头文件，运行期按成员是否为 NULL 选择。
 * 每个回调都会收到以 iolink_device_config_t::user 传入的
 * 不透明 @c user 指针，因此应用无需全局变量即可
 * 访问自身状态。生命周期回调在 DLL 进入
 * 对应状态时触发一次。PD 回调在每次处理的
 * OPERATE 周期触发。@ref on_pd_output 报告从主站
 * 收到的输出过程数据（主站 -> 设备）。
 * on_pd_input() 填充一个空闲的 PD-in 缓冲区（设备 -> 主站），
 * 其返回值成为 PD 有效标志。
 */
typedef struct
{
    void (*on_startup)(void* user);                                /**< 已进入 STARTUP */
    void (*on_preoperate)(void* user);                             /**< 已进入 PREOPERATE */
    void (*on_operate)(void* user);                                /**< 已进入 OPERATE */
    bool (*on_pd_input)(void* user, uint8_t* data, uint8_t len);   /**< 填充输入 PD；
                                                                        false 表示无效。
                                                                        NULL = 使用轮询 API。 */
    void (*on_pd_output)(void* user, uint8_t* data, uint8_t len);  /**< 已收到输出 PD */
    void (*on_state_change)(void* user, iolink_dll_state_t state); /**< 基于电平通知时的
                                                                    当前 DLL 状态。
                                                                    在主上下文中运行，可能位于
                                                                    iolink_dll_process() 内部；
                                                                    在此处不得重入 iolink_*。 */
    void (*on_master_lost)(void* user);                            /**< 主站通信丢失 */
    void (*on_master_online)(void* user);                          /**< 主站通信恢复 */
} iolink_app_callbacks_t;

/** @} */ /* end of iolinki_application */

#endif  // IOLINK_APPLICATION_H
