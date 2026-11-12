/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ctx_sizes.c
 * @brief 上下文尺寸契约：任何布局或缓冲尺寸变化都必须显式更新本文件。
 *
 * 精确数值是 64 位主机、默认缓冲（include/iolinki/config.h 默认值）下的
 * 实测值，由 docs/MEMORY_GUIDE.md 引用为权威数字。断言假定：
 *  - 64 位指针（32 位目标跳过精确断言，只校验宽度无关的嵌套关系）；
 *  - 默认 `IOLINK_ATOMIC_UINT_T`（uint32_t）、默认枚举/bool 宽度，
 *    未启用 -fshort-enums 或 #pragma pack。
 *
 * 裁剪构建不编译单元测试（见 tests/CMakeLists.txt），因此这里只覆盖
 * 完整特性配置；裁剪/32 位配置的尺寸打印尚未实现，计划见
 * docs/EVOLUTION_ROADMAP.md 的 P8。
 *
 * 修改布局或缓冲默认值后，CI 会在这里失败——把新数值改进来，并同步
 * MEMORY_GUIDE.md。这正是本测试存在的目的：尺寸变化必须是有意识的决定。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/port.h"

/* 宽度无关的嵌套关系：32 位与 64 位目标都成立。 */
IOLINK_STATIC_ASSERT(sizeof(iolink_isdu_ctx_t) < sizeof(iolink_dll_ctx_t),
                     "the ISDU context must be nested inside the DLL context");
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) >= sizeof(iolink_isdu_ctx_t) +
                                                     sizeof(iolink_ds_ctx_t) +
                                                     sizeof(iolink_events_ctx_t),
                     "the DLL context must contain the ISDU, DS and events contexts");

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
#if IOLINK_STATS_MODE == IOLINK_STATS_SHARED
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 3376u, "dll ctx (STATS_SHARED) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 3688u, "device ctx (STATS_SHARED) changed");
#else
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 3384u, "dll ctx (STATS_SPLIT) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 3696u, "device ctx (STATS_SPLIT) changed");
#endif
IOLINK_STATIC_ASSERT(sizeof(iolink_isdu_ctx_t) == 1488u, "isdu ctx changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_ds_ctx_t) == 376u, "ds ctx changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_events_ctx_t) == 392u, "events ctx changed");
#endif

int main(void)
{
    printf("ctx sizes: dll=%zu device=%zu isdu=%zu ds=%zu events=%zu\n", sizeof(iolink_dll_ctx_t),
           sizeof(iolink_device_ctx_t), sizeof(iolink_isdu_ctx_t), sizeof(iolink_ds_ctx_t),
           sizeof(iolink_events_ctx_t));
    return 0;
}
