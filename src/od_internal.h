/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_OD_INTERNAL_H
#define IOLINK_OD_INTERNAL_H

#include "iolinki/od.h"

/**
 * @file od_internal.h
 * @brief 应用对象字典表的初始化期自检（库内部）。
 *
 * 表项宏只能校验常量之间的交叉关系；指针关系（`on_change`/`validate`/
 * `read`/`write` 是否为 NULL）在 C 里不是整数常量表达式，宏里查不了。
 * 设备初始化在应用表生效之前，用本文件的一次走表把这类未定义配置拒绝掉。
 */

#if IOLINK_FEATURE_VALIDATE

/**
 * @brief 应用表自检结果。
 *
 * 0 表示通过，负值是第一个命中的问题（`file:line` 定位由构建期断言负责，
 * 运行期只回报类别）。
 */
typedef enum
{
    OD_VALIDATE_OK = 0,
    OD_VALIDATE_ERR_TABLE = -1,             /**< entries 为 NULL 但 count 非 0。 */
    OD_VALIDATE_ERR_HANDLER_NONE = -2,      /**< 应用表项 handler 非 NONE。 */
    OD_VALIDATE_ERR_DATATYPE = -3,          /**< datatype 越界。 */
    OD_VALIDATE_ERR_ACCESS = -4,            /**< 既不可读也不可写。 */
    OD_VALIDATE_ERR_SEMANTICS = -5,         /**< write_semantics 取值非法。 */
    OD_VALIDATE_ERR_STORAGE_BITS = -6,      /**< storage 含未知位。 */
    OD_VALIDATE_ERR_PERSIST_SEMANTICS = -7, /**< PERSIST 未配 STORE 语义。 */
    OD_VALIDATE_ERR_TRIGGER_STORAGE = -8,   /**< TRIGGER 带了 storage 位。 */
    OD_VALIDATE_ERR_BYTES_UNBOUNDED = -9,   /**< 可写字节串未给长度上限。 */
    OD_VALIDATE_ERR_RO_HOOK = -10,          /**< 只读项带写钩子（永不触发）。 */
    OD_VALIDATE_ERR_RANGE = -11,            /**< range_min > range_max。 */
    OD_VALIDATE_ERR_DEFAULT_RANGE = -12,    /**< 默认值越界。 */
    OD_VALIDATE_ERR_BACKEND = -13,          /**< backend 取值非法。 */
    OD_VALIDATE_ERR_STORAGE_NULL = -14,     /**< STORAGE 后端缺变量/长度指针。 */
    OD_VALIDATE_ERR_HANDLER_READ = -15,     /**< 可读 HANDLER 缺 read。 */
    OD_VALIDATE_ERR_HANDLER_WRITE = -16,    /**< 可写 HANDLER 缺 write。 */
    OD_VALIDATE_ERR_DUPLICATE = -17,        /**< 重复 index/subindex。 */
    OD_VALIDATE_ERR_STD_INDEX = -18         /**< 应用表占用标准索引（< 0x40）。 */
} od_validate_result_t;

/**
 * @brief 校验一张应用对象字典表。
 *
 * @param app 应用表；NULL 视为合法（设备没有应用参数）。
 * @return ::OD_VALIDATE_OK 或第一个命中的负值错误。
 */
int od_validate_app(const iolink_od_app_t* app);

#endif /* IOLINK_FEATURE_VALIDATE */

#endif  // IOLINK_OD_INTERNAL_H
