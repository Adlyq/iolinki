/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_UTILS_H
#define IOLINK_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @file utils.h
 * @brief 杂项辅助工具。
 *
 * 供整个 IO-Link 协议栈使用的缓冲区校验与上下文清零
 * 小型内联辅助函数。
 */

/**
 * @defgroup iolinki_utils 杂项工具
 * @brief 内联的缓冲区校验与内存辅助函数。
 * @{
 */

/**
 * @brief 校验（指针，长度）缓冲区对。
 *
 * 除非指针为 NULL 且声称长度非零，否则缓冲区
 * 被视为有效。长度为零的 NULL 指针有效。
 *
 * @param data  要校验的缓冲区指针。
 * @param len   @p data 声称的长度，以字节计。
 * @return 该对有效时返回 true，否则返回 false。
 */
static inline bool iolink_buf_is_valid(const void* data, size_t len)
{
    return !((data == NULL) && (len > 0U));
}

/**
 * @brief 将上下文对象清零初始化。
 *
 * @param[out] ctx  要清零的对象；为 NULL 时忽略（返回 false）。
 * @param len       要清零的字节数。
 * @return @p ctx 已清零时返回 true，@p ctx 为 NULL 时返回 false。
 */
static inline bool iolink_ctx_zero(void* ctx, size_t len)
{
    if (ctx == NULL) {
        return false;
    }
    (void) memset(ctx, 0, len);
    return true;
}

/** @} */ /* end of iolinki_utils */

#endif /* IOLINK_UTILS_H */
