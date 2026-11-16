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
#include "iolinki/log.h"
#include "iolinki/types.h"

/**
 * @brief PD 输入路径（运行时选择，默认值与完整说明见 config.h）。
 *
 * 两条路径不再互斥：`iolink_device_pd_input_update()` 始终可用；如果
 * `iolink_device_app_t::on_pd_input` 非 NULL，DLL 会在每个 OPERATE
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
 * @brief 应用 trait：一个协议实例的上下文与全部应用回调。
 *
 * 库把这张表当作"应用实现了什么"的唯一来源：`self` 是实例指针，
 * 其余字段是可选操作。每个回调收到的都是同一个 `self`，因此
 * 回调之间不需要互相寻找对方的上下文，也不需要全局变量。
 *
 * 表本身由应用以 `static const` 构造，生命周期必须覆盖设备上下文；
 * @ref iolink_device_init() 只保存指针。
 *
 * `on_pd_input` 非 NULL 时，它是 PD 输入的唯一来源（DLL 每周期调用
 * 它）；为 NULL 时应用改用 `iolink_device_pd_input_update()` 轮询。
 * 两条路径可以共存于同一份头文件，运行期按成员是否为 NULL 选择。
 * 生命周期回调在 DLL 进入对应状态时触发一次；PD 回调在每次处理的
 * OPERATE 周期触发。`on_pd_output` 报告从主站收到的输出过程数据
 * （主站 -> 设备）。on_pd_input() 填充一个空闲的 PD-in 缓冲区
 * （设备 -> 主站），其返回值成为 PD 有效标志。
 *
 * `time_us` 是唯一必需的成员：DLL、ISR 泵和事件引擎的每个时间戳都
 * 取自它，缺失时 iolink_device_init() 返回
 * `IOLINK_DEVICE_INIT_ERR_TIME`。
 */
typedef struct
{
    iolink_app_t* self; /**< 实例指针；原样传给下面每个回调（可为 NULL）。 */

    /**
     * @brief 单调微秒时钟（必需）。
     *
     * 数据 ISR 会读取它，因此实现必须是单调、无阻塞且 ISR 安全的。
     */
    uint64_t (*time_us)(iolink_app_t* self);

    /** @brief 可选日志回调；NULL 表示静默（由 `config.log_level` 过滤）。 */
    void (*log_write)(iolink_app_t* self, iolink_log_level_t level, const char* format, ...)
        IOLINK_LOG_PRINTF_FORMAT(3, 4);

    void (*on_startup)(iolink_app_t* self);    /**< 已进入 STARTUP */
    void (*on_preoperate)(iolink_app_t* self); /**< 已进入 PREOPERATE */
    void (*on_operate)(iolink_app_t* self);    /**< 已进入 OPERATE */
    bool (*on_pd_input)(iolink_app_t* self, uint8_t* data,
                        uint8_t len);                                     /**< 填充输入 PD；
                                                                              false 表示无效。
                                                                              NULL = 使用轮询 API。 */
    void (*on_pd_output)(iolink_app_t* self, uint8_t* data, uint8_t len); /**< 已收到输出 PD */
    void (*on_state_change)(iolink_app_t* self,
                            iolink_dll_state_t state); /**< 基于电平通知时的当前 DLL 状态。
                                                            在主上下文中运行，可能位于
                                                            iolink_dll_process() 内部；
                                                            在此处不得重入 iolink_*。 */
    void (*on_master_lost)(iolink_app_t* self);        /**< 主站通信丢失 */
    void (*on_master_online)(iolink_app_t* self);      /**< 主站通信恢复 */
} iolink_device_app_t;

/** @} */ /* end of iolinki_application */

#endif  // IOLINK_APPLICATION_H
