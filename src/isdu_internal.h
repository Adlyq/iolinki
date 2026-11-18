/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_ISDU_INTERNAL_H
#define IOLINK_ISDU_INTERNAL_H

/**
 * @file isdu_internal.h
 * @brief ISDU 引擎内部跨文件声明（仅供 src/ 使用）。
 *
 * `src/isdu.c` 按职责拆成多个翻译单元后共享的库私有入口；
 * 它们不属于公共 iolink_ API。
 */

#include "iolinki/isdu.h"

/** @name isdu.c */
/** @{ */
uint16_t isdu_access_locks(const iolink_isdu_ctx_t* ctx);
bool isdu_cap_default_on(const iolink_isdu_ctx_t* ctx, iolink_caps_t bit);
IOLINK_MAYBE_UNUSED bool isdu_cap(const iolink_isdu_ctx_t* ctx, iolink_caps_t bit);
iolink_device_info_ctx_t* isdu_info_ctx(iolink_isdu_ctx_t* ctx);
const iolink_device_info_t* isdu_info(const iolink_isdu_ctx_t* ctx);
uint16_t isdu_locks_implemented(const iolink_isdu_ctx_t* ctx);
/** @brief 以两字节否定应答终止 ISDU 服务。 */
void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error);
/** @brief 将统一结果翻译为 ISDU 否定应答。 */
void isdu_respond_result(iolink_isdu_ctx_t* ctx, iolink_result_t result);
/**
 * @brief 整个传输层复位：主泵的读传输层字段与写入口相位都清零。
 *
 * 初始化与错误恢复路径使用；它会同时丢弃数据 ISR 正在累计的写请求。
 */
void isdu_transport_reset(iolink_isdu_ctx_t* ctx);
/**
 * @brief 只复位主泵独占的读传输层（`state`/`header`/应答缓冲/错误码）。
 *
 * 读方向的 IDLE/ABORT 与读完成复位走这里。写入口相位
 * （`req_idx`/`req_total`/`flowctrl_seen`/`stage_state`）保持原样。
 */
void isdu_read_reset(iolink_isdu_ctx_t* ctx);
/**
 * @brief 释放写入口相位：`req_idx`/`req_total`/`flowctrl_seen`/`stage_state`。
 *
 * 这些字段归数据 ISR 的写入口（@ref iolink_isdu_od_write）：START 建立、
 * COUNT 推进，主泵只在 COMPLETE/ABORT 接管时交还。不触碰读传输层。
 */
void isdu_stage_release(iolink_isdu_ctx_t* ctx);
void isdu_factory_reset(iolink_isdu_ctx_t* ctx);
/** @} */

/** @name isdu_cmd.c */
/** @{ */
void dp_page1_ensure(iolink_isdu_ctx_t* ctx);
bool dp_page1_main_context(const iolink_isdu_ctx_t* ctx);
void handle_access_locks(iolink_isdu_ctx_t* ctx);
void handle_detailed_device_status(iolink_isdu_ctx_t* ctx);
void handle_direct_parameters(iolink_isdu_ctx_t* ctx);
void handle_error_stats(iolink_isdu_ctx_t* ctx);
void isdu_read_error_count(iolink_isdu_ctx_t* ctx);
bool isdu_read_identification_string(iolink_isdu_ctx_t* ctx, const iolink_device_info_t* info,
                                     const iolink_od_entry_t* entry);
void isdu_read_process_data(iolink_isdu_ctx_t* ctx, bool input);
void isdu_read_profile_characteristic(iolink_isdu_ctx_t* ctx, const iolink_device_info_t* info);
void isdu_write_u32_be(uint8_t* buf, size_t* idx, uint32_t value);
void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd);
uint8_t isdu_ds_result_error(int ret, uint8_t fallback);
void isdu_read_device_status(iolink_isdu_ctx_t* ctx);
/** @} */

/** @name isdu_ds.c */
/** @{ */
bool isdu_reject_ds_access(iolink_isdu_ctx_t* ctx);
void isdu_serve_entry(iolink_isdu_ctx_t* ctx, const iolink_od_entry_t* entry);
/** @} */

#endif /* IOLINK_ISDU_INTERNAL_H */
