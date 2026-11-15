/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file dll_access.h
 * @brief 测试访问 DLL 内部状态的唯一边界。
 *
 * 行为/一致性测试应优先使用公开 API（`iolink_device_get_state()`、
 * `iolink_device_get_dll_stats()`、`iolink_device_get_events_ctx()` 等）。
 * 没有公开等价物的观测点（PD 缓冲布局、ISR 交接状态、应答计划表）通过
 * 本文件的访问器取得：设备上下文如何包含 DLL 只在 `test_dll()` 里表达
 * 一次，而不是散落在每个用例中。
 *
 * `iolinki/dll_ctx.h` 只允许在本文件中直接包含（由
 * `tools/check_test_whitebox.sh` 校验）。
 */

#ifndef TEST_DLL_ACCESS_H_
#define TEST_DLL_ACCESS_H_

#include "iolinki/device.h"
#include "iolinki/dll_ctx.h"

/** @brief 设备上下文内嵌的完整 DLL 上下文。 */
static inline iolink_dll_ctx_t* test_dll_ctx(iolink_device_ctx_t* ctx)
{
    return &ctx->dll;
}

/** @brief 设备上下文内嵌的 DLL 主上下文。 */
static inline iolink_dll_main_t* test_dll(iolink_device_ctx_t* ctx)
{
    return &ctx->dll.main;
}

/** @brief 只读版本的 @ref test_dll。 */
static inline const iolink_dll_main_t* test_dll_const(const iolink_device_ctx_t* ctx)
{
    return &ctx->dll.main;
}

/** @brief 主上下文与 ISR 共享的状态区。 */
static inline iolink_dll_shared_t* test_dll_shared(iolink_device_ctx_t* ctx)
{
    return &ctx->dll.main.shared;
}

/** @brief 内嵌的 ISDU 上下文。 */
static inline iolink_isdu_ctx_t* test_dll_isdu(iolink_device_ctx_t* ctx)
{
    return &ctx->dll.main.shared.isdu;
}

#endif /* TEST_DLL_ACCESS_H_ */
