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
 * @brief 只复位主泵独占的读传输层（`state`/`header`/应答缓冲/错误码）。
 *
 * 读方向的 IDLE/ABORT、读完成与错误恢复走这里。写入口相位
 * （`stage_state`、累计位置与发布字）归数据 ISR，主泵从不改写，因此
 * 该复位不需要临界区。
 */
void isdu_read_reset(iolink_isdu_ctx_t* ctx);
/**
 * @brief 发布字是否指向一条主泵尚未处理**且已收齐**的写请求（主泵只读）。
 *
 * `gen != req_ack_gen` 且 `len > 0` 即为真：ISR 先写完槽内容、再发布
 * 代号，因此为真时槽内容必然完整。
 */
bool isdu_request_pending(const iolink_isdu_ctx_t* ctx);
/**
 * @brief 写入口是否有一代尚未被主泵处理（含"正在累计、len == 0"的那些代）。
 *
 * 主泵用它在写期间让已发布的读视图失效。只读发布字与主泵自己的 ack
 * 序号，不读数据 ISR 的相位字段。
 */
bool isdu_write_in_progress(const iolink_isdu_ctx_t* ctx);
void isdu_abandon_write(iolink_isdu_ctx_t* ctx);
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
