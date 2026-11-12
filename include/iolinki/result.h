/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_RESULT_H
#define IOLINK_RESULT_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @file result.h
 * @brief 统一操作结果（评审决策 D7）。
 *
 * 协议栈内部只使用一种结果语义：
 *  - `code == 0`：成功；`len` 为传输的字节数（无意义时为 0）。
 *  - `code < 0`：内部错误，不进入线上协议；调用方在最外层统一翻译。
 *  - `code > 0`：IO-Link ISDU ErrorCode（Table C.1，见 protocol.h），
 *    原样放进 ISDU 否定应答，不再被静默降级。
 */

typedef struct
{
    int16_t code; /**< 0 成功；负值内部错误；正值 ISDU ErrorCode。 */
    uint16_t len; /**< 成功时传输的字节数，否则为 0。 */
} iolink_result_t;

/** 该后端不拥有此索引：调用方应回退到下一层（例如厂商服务）。 */
#define IOLINK_RESULT_NOT_HANDLED (-1)
/** 参数无效或内部故障。 */
#define IOLINK_RESULT_INVALID (-2)
/** 非易失性存储操作失败。 */
#define IOLINK_RESULT_IO (-3)

/** @brief 构造成功结果。 */
static inline iolink_result_t iolink_result_ok(uint16_t len)
{
    iolink_result_t result;

    result.code = 0;
    result.len = len;
    return result;
}

/** @brief 构造错误结果（负值内部错误或正值 ISDU ErrorCode）。 */
static inline iolink_result_t iolink_result_error(int16_t code)
{
    iolink_result_t result;

    result.code = code;
    result.len = 0U;
    return result;
}

/** @brief 结果是否成功。 */
static inline bool iolink_result_succeeded(iolink_result_t result)
{
    return result.code == 0;
}

#endif  // IOLINK_RESULT_H
