/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PARAMS_H
#define IOLINK_PARAMS_H

#include "iolinki/config.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iolinki/device_info.h"
#include "iolinki/result.h"
#include "iolinki/storage.h"

/**
 * @file params.h
 * @brief IO-Link 参数管理器
 */

/**
 * @defgroup iolinki_params 参数管理器
 * @brief 按 ISDU 索引/子索引读写 IO-Link 参数。
 * @{
 */

/**
 * @brief 每设备参数管理器上下文。
 */
typedef struct
{
    char application_tag[33];              /**< 应用标签（32 字节 + NUL）。 */
    char function_tag[33];                 /**< 功能标签（32 字节 + NUL）。 */
    char location_tag[33];                 /**< 位置标签（32 字节 + NUL）。 */
    bool application_tag_valid;            /**< 已设置应用标签时为 true。 */
    bool function_tag_valid;               /**< 已设置功能标签时为 true。 */
    bool location_tag_valid;               /**< 已设置位置标签时为 true。 */
    iolink_device_info_ctx_t* device_info; /**< 关联的设备信息上下文。 */
    const iolink_storage_api_t* storage;   /**< 持久化后端（可为 NULL）。 */
} iolink_params_ctx_t;

/**
 * @brief 初始化参数管理器上下文。
 * @param ctx          要初始化的上下文。
 * @param device_info  要绑定的设备信息上下文（必须保持有效）。
 */
void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info);

/**
 * @brief 将持久化存储后端绑定到参数上下文。
 * @param ctx     参数管理器上下文。
 * @param storage 统一存储后端（NULL 表示禁用持久化）。
 */
void iolink_params_bind_storage(iolink_params_ctx_t* ctx, const iolink_storage_api_t* storage);

/**
 * @brief 按 IO-Link 地址从上下文读取参数值（结果模型）。
 *
 * 这是内部权威接口：成功返回 `{0, len}`；未知索引返回
 * ::IOLINK_RESULT_NOT_HANDLED；协议层错误（如长度超长）以正的
 * ISDU ErrorCode 返回（如 0x33），由 ISDU 应答边界原样上送。
 *
 * @param ctx          参数管理器上下文。
 * @param index        ISDU 索引。
 * @param subindex     ISDU 子索引（0 表示整个索引，或 1-255）。
 * @param[out] buffer  用于存放值的目标缓冲区。
 * @param max_len      目标缓冲区的大小。
 */
iolink_result_t iolink_params_ctx_read(const iolink_params_ctx_t* ctx, uint16_t index,
                                       uint8_t subindex, uint8_t* buffer, size_t max_len);

/**
 * @brief 更新上下文中的参数值（结果模型）。
 *
 * @param ctx       参数管理器上下文。
 * @param index     ISDU 索引。
 * @param subindex  ISDU 子索引。
 * @param data      指向待写入新数据的指针。
 * @param len       新数据的长度，以字节计。
 * @param persist   为 true 时，将变更同步提交到 NVM。
 * NVM 提交失败时返回失败，且不改变上下文/回读值。
 */
iolink_result_t iolink_params_ctx_write(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                        const uint8_t* data, size_t len, iolink_persist_t persist);

/**
 * @brief 按 IO-Link 地址从上下文读取参数值（旧版整型接口）。
 * @deprecated 使用 ::iolink_params_ctx_read；本包装器只保留一个版本，
 *             用于兼容既有应用与测试，错误细节会退化为 -1。
 */
int iolink_params_ctx_get(const iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          uint8_t* buffer, size_t max_len);

/**
 * @brief 更新上下文中的参数值（旧版整型接口）。
 * @deprecated 使用 ::iolink_params_ctx_write；本包装器只保留一个版本，
 *             用于兼容既有应用与测试。
 */
int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len, iolink_persist_t persist);

/**
 * @brief 将上下文中的所有参数恢复为出厂默认值。
 * 复位 RAM 并尝试写入 NVM；该旧版 void API 无法报告存储失败。
 * @param ctx 参数管理器上下文。
 */
void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx);

/** @} */ /* end of iolinki_params */

#endif  // IOLINK_PARAMS_H
