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
 * `iolinki/internal/dll_ctx.h` 只允许在本文件中直接包含（由
 * `tools/check_test_whitebox.sh` 校验）。
 */

#ifndef TEST_DLL_ACCESS_H_
#define TEST_DLL_ACCESS_H_

#include "iolinki/device.h"
#include "iolinki/internal/dll_ctx.h"

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

/** @brief 主上下文发布一次 PD-in（与 `device.c` 同一个发布字）。 */
static inline void test_pd_publish(iolink_dll_main_t* dll, uint8_t active, bool valid)
{
    dll_pd_publish(&dll->shared, active, valid);
}

/** @brief 当前一代的 active 索引。 */
static inline uint8_t test_pd_active(const iolink_dll_main_t* dll)
{
    return IOLINK_PD_PUB_ACTIVE(dll_pd_pub_load(&dll->shared));
}

/** @brief 保留当前 active 索引，只发布有效位。 */
static inline void test_pd_set_valid(iolink_dll_main_t* dll, bool valid)
{
    dll_pd_publish(&dll->shared, test_pd_active(dll), valid);
}

/** @brief 当前有效位（含数据 ISR 命令字的覆盖）。 */
static inline bool test_pd_valid(const iolink_dll_main_t* dll)
{
    return dll_pd_valid_at(&dll->shared, dll_pd_pub_load(&dll->shared));
}

#endif /* TEST_DLL_ACCESS_H_ */
