/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_INTERNAL_H
#define IOLINK_DLL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "iolinki/internal/dll_main.h"

#include "dll_shared_internal.h"

/**
 * @file dll_internal.h
 * @brief 主上下文独占的内部 DLL 定义与结构。
 *
 * 两个上下文共享的部分在 dll_shared_internal.h；数据 ISR 翻译单元只包含
 * 后者，因此看不到这里的任何声明。
 */

/**
 * @name 主上下文跨文件入口
 *
 * `src/dll.c` 按职责拆成多个翻译单元后共享的内部符号：帧路径
 * （dll_frame.c）、状态机（dll_sm.c）、应答计划（dll_reply.c）。
 * 均为库私有，只供 `src/` 内部使用；dll_ 前缀表示它们不属于
 * 公共 iolink_ API。
 * @{
 */

void dll_set_state(iolink_dll_main_t* ctx, iolink_dll_state_t new_state);

/** @brief 主上下文侧应用 MasterCommand（ISR 侧用 @ref dll_apply_master_command ）。 */
bool dll_apply_master_command_main(iolink_dll_main_t* ctx, uint8_t cmd);

void dll_protocol_error_to_startup(iolink_dll_main_t* ctx);
uint8_t dll_pd_status_flags(const iolink_dll_main_t* ctx);

/** @brief A.1.6 校验和与 CKT 类型校验的结果。 */
typedef enum
{
    DLL_FRAME_INVALID,    /**< 校验和不匹配：丢弃，保持 DL 模式。 */
    DLL_FRAME_TYPE_ERROR, /**< 校验和有效，M-sequence 类型非法。 */
    DLL_FRAME_VALID       /**< 有效请求；按状态分发。 */
} dll_frame_result_t;

/** @} */

/** @name dll_frame.c */
/** @{ */
void dll_send(iolink_dll_main_t* ctx, const uint8_t* data, size_t len);
uint8_t dll_mseq_type_bits(uint8_t m_seq_type);
uint32_t dll_get_baud_hz(const iolink_dll_main_t* ctx);
uint32_t dll_get_t_bit_us(const iolink_dll_main_t* ctx);
uint32_t dll_get_t_ren_limit_us(void);
uint32_t dll_get_t_byte_limit_us(const iolink_dll_main_t* ctx);
bool dll_t_pd_active(const iolink_dll_main_t* ctx);
bool dll_drain_rx(iolink_dll_main_t* ctx);
void dll_poll_diagnostics(iolink_dll_main_t* ctx);
bool dll_is_fallback_command(const iolink_dll_main_t* ctx);
void dll_handle_fallback_command(iolink_dll_main_t* ctx);
void dll_handle_master_command(iolink_dll_main_t* ctx, uint8_t cmd);
void dll_handle_page_channel_read(iolink_dll_main_t* ctx, uint8_t mc);
void dll_handle_operate_type0(iolink_dll_main_t* ctx, uint8_t mc, uint8_t cks);
void dll_handle_type0_od_write(iolink_dll_main_t* ctx, uint8_t mc, uint8_t od_in);
void dll_handle_operate_type1_2(iolink_dll_main_t* ctx);
bool dll_frame_drop_if_stopped(iolink_dll_main_t* ctx);
void dll_check_cycle_time_at(iolink_dll_main_t* ctx, uint64_t frame_us);
void dll_check_cycle_time(iolink_dll_main_t* ctx);
/** @} */

/** @name dll_sm.c */
/** @{ */
dll_frame_result_t dll_frame_validate(iolink_dll_main_t* ctx);
void dll_handle_frame_error(iolink_dll_main_t* ctx, dll_frame_result_t result);
bool dll_handle_valid_frame(iolink_dll_main_t* ctx);
void dll_finish_frame(iolink_dll_main_t* ctx);
void dll_process_complete_frame(iolink_dll_main_t* ctx);
/** @} */

/** @name dll_reply.c */
/** @{ */
void dll_isdu_sync_snapshot(iolink_dll_main_t* ctx);
uint32_t dll_master_cycle_us(const iolink_dll_main_t* ctx);
void dll_mark_master_activity_ts(iolink_dll_main_t* ctx);
void dll_count_served_cycle(iolink_dll_main_t* ctx);
/** @} */

#endif  // IOLINK_DLL_INTERNAL_H
