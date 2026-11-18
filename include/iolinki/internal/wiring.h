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

#include "iolinki/types.h"

#include "iolinki/data_storage.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"

#ifdef __cplusplus
extern "C" {
#endif

IOLINK_ASSUME_NONNULL_BEGIN

/** @brief 切换到 SIO 模式（协议状态机）。 */
int iolink_dll_set_sio_mode(iolink_dll_main_t* ctx);

/** @brief 切换到 SDCI 模式（协议状态机）。 */
int iolink_dll_set_sdci_mode(iolink_dll_main_t* ctx);

/**
 * @brief 请求执行 Back-to-box 序列（10.7.5）。
 *
 * ISDU 处理程序在成功应答 0x83 SystemCommand 后调用它。DLL 至少会继续
 * 服务帧 3 个 MasterCycle，随后停止并禁用通信（COMM_DISABLED）以及 PHY
 * 输出驱动器，直到下一次上电。
 *
 * @param ctx DLL 上下文
 */
void iolink_dll_request_back_to_box(iolink_dll_main_t* ctx);

/**
 * @brief MC 字节为 @p mc 的主站请求的期望长度（字节）。
 *
 * 纯函数、ISR 安全且只读：仅由 DLL 状态 / 配置的类型和 MC 字节推导得出。
 * 库的 ISR 泵用它界定输入的 M 序列；主泵对同一帧复用该结果。
 * @param ctx DLL 上下文（只读）
 * @param mc  请求的主站命令（第一个）字节
 * @return 期望的请求长度，以字节计（Type-0 报文为 2）。
 */
uint8_t iolink_dll_expected_req_len(const iolink_dll_main_t* ctx, uint8_t mc);

/** @brief 查询一个 timer 的绝对截止时间（0 = 未装载）。 */
uint64_t iolink_dll_timer_deadline(const iolink_dll_main_t* ctx, iolink_timer_id_t id);

/** @brief 查询一个 timer 是否已装载。 */
bool iolink_dll_timer_armed(const iolink_dll_main_t* ctx, iolink_timer_id_t id);

/** @brief 复位 ErrorCount 计数器（SystemCommand 0x80/0x82/0x83）。
 *  @note 只清计数器，保留活动事件（Table 101 "Clear" 与 "Restore factory
 *        settings" 的区别由调用方选择）。
 */
void iolink_dll_reset_error_counters(iolink_dll_main_t* ctx);

/** @brief 复位诊断事件与 DeviceStatus（SystemCommand/Back-to-box）。
 *  @note 同时清计数器、事件队列、Event 标志与 Table 58 存储。
 */
void iolink_dll_reset_diagnostics(iolink_dll_main_t* ctx);

/** @brief 覆盖 A.6.4 事件窗口（初始化期）。 */
void iolink_events_set_window(iolink_events_ctx_t* ctx, uint16_t min_appear_ms,
                              uint16_t min_repeat_ms);

/** @brief 绑定 DS_UPLOAD_FLAG 持久化钩子（初始化期）。 */
void iolink_ds_set_flag_persist(iolink_ds_ctx_t* ctx, iolink_ds_flag_persist_fn IOLINK_NULLABLE fn,
                                iolink_app_t* IOLINK_NULLABLE self);

/** @brief 使 DPP1 映像失效，下一次读取时重建（主上下文）。
 *  @note 取值为纯函数，缓存由本函数失效；主上下文重建整页，ISR 只读。
 */
void iolink_isdu_dp_page1_invalidate(iolink_isdu_ctx_t* ctx);

IOLINK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* IOLINK_INTERNAL_WIRING_H_ */
