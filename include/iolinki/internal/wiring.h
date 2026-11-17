/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file internal/wiring.h
 * @brief 初始化与协议驱动模块之间的接线入口（不是应用 API）。
 *
 * 这些函数只由库内的初始化路径或协议状态机调用；应用应通过
 * `iolink_device_config_t` / `iolink_stack_config_t` 给出对应配置。
 * 直接使用 DLL/ISDU/事件/DS 子系统的集成代码可以调用它们，但必须满足
 * 各自的时机约束（通常是初始化期、数据 ISR 静止）。
 */

#ifndef IOLINK_INTERNAL_WIRING_H_
#define IOLINK_INTERNAL_WIRING_H_

#include <stdbool.h>
#include <stdint.h>

#include "iolinki/data_storage.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 设置通信波特率（初始化/恢复路径）。 */
int iolink_dll_set_baudrate(iolink_dll_main_t* ctx, iolink_baudrate_t baudrate);

/** @brief 切换到 SIO 模式（协议状态机）。 */
int iolink_dll_set_sio_mode(iolink_dll_main_t* ctx);

/** @brief 切换到 SDCI 模式（协议状态机）。 */
int iolink_dll_set_sdci_mode(iolink_dll_main_t* ctx);

/** @brief 复位 ErrorCount 计数器（SystemCommand 0x80/0x82/0x83）。 */
void iolink_dll_reset_error_counters(iolink_dll_main_t* ctx);

/** @brief 复位诊断事件与 DeviceStatus（SystemCommand/Back-to-box）。 */
void iolink_dll_reset_diagnostics(iolink_dll_main_t* ctx);

/** @brief 覆盖 A.6.4 事件窗口（初始化期）。 */
void iolink_events_set_window(iolink_events_ctx_t* ctx, uint16_t min_appear_ms,
                              uint16_t min_repeat_ms);

/** @brief 绑定 DS_UPLOAD_FLAG 持久化钩子（初始化期）。 */
void iolink_ds_set_flag_persist(iolink_ds_ctx_t* ctx, iolink_ds_flag_persist_fn IOLINK_NULLABLE fn,
                                iolink_app_t* IOLINK_NULLABLE self);

/** @brief 使 DPP1 映像失效，下一次读取时重建（主上下文）。 */
void iolink_isdu_dp_page1_invalidate(iolink_isdu_ctx_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* IOLINK_INTERNAL_WIRING_H_ */
