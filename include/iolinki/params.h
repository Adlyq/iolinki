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

/**
 * @file params.h
 * @brief 三个标识标签（0x0018-0x001A）的 RAM 值存储。
 *
 * 标签是唯一需要栈内 RAM 存储的标准参数；它们的持久化与其它
 * `IOLINK_OD_STORAGE_PERSIST` 表项一起由参数镜像统一完成
 * （`iolink_ds_save_persistent()`）。本模块不再持有存储后端，
 * 也不定义独立的 NVM 记录格式。
 */

/**
 * @defgroup iolinki_params 参数管理器
 * @brief 按 ISDU 索引/子索引读写标签值。
 * @{
 */

/**
 * @brief 标签值上下文（随设备上下文分配）。
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
} iolink_params_ctx_t;

/**
 * @brief 初始化标签上下文（应用标签取设备信息的默认值）。
 * @param ctx          要初始化的上下文。
 * @param device_info  要绑定的设备信息上下文（必须保持有效）。
 */
void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info);

/**
 * @brief 按 IO-Link 地址读取标签值（结果模型）。
 *
 * 这是内部权威接口：成功返回 `{0, len}`；未知索引返回
 * ::IOLINK_RESULT_NOT_HANDLED；协议层错误（如长度超长）以正的
 * ISDU ErrorCode 返回（如 0x33），由 ISDU 应答边界原样上送。
 *
 * @param ctx          标签上下文。
 * @param index        ISDU 索引。
 * @param subindex     ISDU 子索引（0 表示整个索引）。
 * @param[out] buffer  用于存放值的目标缓冲区。
 * @param max_len      目标缓冲区的大小。
 */
iolink_result_t iolink_params_ctx_read(const iolink_params_ctx_t* ctx, uint16_t index,
                                       uint8_t subindex, uint8_t* buffer, size_t max_len);

/**
 * @brief 更新标签值（结果模型）。
 *
 * 只改 RAM；持久化由调用方在写入成功后触发参数镜像保存。
 *
 * @param ctx       标签上下文。
 * @param index     ISDU 索引。
 * @param subindex  ISDU 子索引。
 * @param data      指向待写入新数据的指针。
 * @param len       新数据的长度，以字节计。
 */
iolink_result_t iolink_params_ctx_write(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                        const uint8_t* data, size_t len);

/**
 * @brief 将三个标签恢复为空（RAM）。
 *
 * 持久化由调用方在成功后保存参数镜像；存储失败不会回滚 RAM。
 * @param ctx 标签上下文。
 */
void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx);

/** @} */ /* end of iolinki_params */

#endif  // IOLINK_PARAMS_H
