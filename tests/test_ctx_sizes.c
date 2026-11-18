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
 * 完整特性配置；`IOLINK_WORKSPACE_EMBEDDED=0` 的裁剪尺寸由
 * docs/MEMORY_GUIDE.md 记录，并由 `-DIOLINK_WORKSPACE_EMBEDDED=0` 的
 * 库构建验证可编译。
 *
 * 修改布局或缓冲默认值后，CI 会在这里失败——把新数值改进来，并同步
 * MEMORY_GUIDE.md。这正是本测试存在的目的：尺寸变化必须是有意识的决定。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdio.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/port.h"
#include "iolinki/workspace.h"

/* 宽度无关的嵌套关系：32 位与 64 位目标都成立。 */
IOLINK_STATIC_ASSERT(sizeof(iolink_isdu_ctx_t) < sizeof(iolink_dll_ctx_t),
                     "the ISDU context must be nested inside the DLL context");
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) >= sizeof(iolink_isdu_ctx_t) +
                                                     sizeof(iolink_ds_ctx_t) +
                                                     sizeof(iolink_events_ctx_t),
                     "the DLL context must contain the ISDU, DS and events contexts");

/* 工作区的每个缓冲都是显式契约；任何容量变化必须同步本文件与
 * docs/MEMORY_GUIDE.md。 */
IOLINK_STATIC_ASSERT(sizeof(((iolink_isdu_scratch_t*) 0)->request) ==
                         2U * (IOLINK_ISDU_BUFFER_SIZE + 6U),
                     "ISDU request scratch changed");
IOLINK_STATIC_ASSERT(sizeof(((iolink_isdu_scratch_t*) 0)->payload) == IOLINK_ISDU_BUFFER_SIZE,
                     "ISDU payload scratch changed");
IOLINK_STATIC_ASSERT(sizeof(((iolink_isdu_scratch_t*) 0)->response) == IOLINK_ISDU_BUFFER_SIZE,
                     "ISDU response scratch changed");
IOLINK_STATIC_ASSERT(sizeof(((iolink_isdu_scratch_t*) 0)->frame) == IOLINK_ISDU_BUFFER_SIZE + 3U,
                     "ISDU frame scratch changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_isdu_scratch_t) == 5U * IOLINK_ISDU_BUFFER_SIZE + 15U,
                     "ISDU scratch must be five buffers without padding");
#if IOLINK_FEATURE_DATA_STORAGE
IOLINK_STATIC_ASSERT(sizeof(((iolink_workspace_t*) 0)->ds_stage) == IOLINK_DS_IMAGE_MAX,
                     "DS stage scratch changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_workspace_t) ==
                         sizeof(iolink_isdu_scratch_t) + IOLINK_DS_IMAGE_MAX,
                     "workspace must contain the ISDU scratch and the DS stage");
#else
IOLINK_STATIC_ASSERT(sizeof(iolink_workspace_t) == sizeof(iolink_isdu_scratch_t),
                     "workspace must contain the ISDU scratch");
#endif

IOLINK_STATIC_ASSERT(sizeof(((iolink_events_ctx_t*) 0)->memory[0]) == 19U,
                     "the event memory image must cover 0x00-0x12");
IOLINK_STATIC_ASSERT(sizeof(((iolink_events_ctx_t*) 0)->memory) == 2U * 19U,
                     "the event memory must be a double image");

#if UINTPTR_MAX == 0xFFFFFFFFFFFFFFFFu
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
#if IOLINK_STATS_MODE == IOLINK_STATS_SHARED
#if IOLINK_PD_IN_BUFFERS == 3U
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 5000u, "dll ctx (RX BYTE, TX FRAME) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5184u,
                     "device ctx (RX BYTE, TX FRAME) changed");
#else
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 4936u, "dll ctx (RX BYTE, TX BYTE) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5120u, "device ctx (RX BYTE, TX BYTE) changed");
#endif
#else
#if IOLINK_PD_IN_BUFFERS == 3U
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 5008u,
                     "dll ctx (RX BYTE, TX FRAME, SPLIT) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5192u,
                     "device ctx (RX BYTE, TX FRAME, SPLIT) changed");
#else
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 4944u,
                     "dll ctx (RX BYTE, TX BYTE, SPLIT) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5128u,
                     "device ctx (RX BYTE, TX BYTE, SPLIT) changed");
#endif
#endif
#else /* IOLINK_ISR_RX_FRAME */
#if IOLINK_STATS_MODE == IOLINK_STATS_SHARED
#if IOLINK_PD_IN_BUFFERS == 3U
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 4936u, "dll ctx (RX FRAME, TX FRAME) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5120u,
                     "device ctx (RX FRAME, TX FRAME) changed");
#else
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 4872u, "dll ctx (RX FRAME, TX BYTE) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5056u,
                     "device ctx (RX FRAME, TX BYTE) changed");
#endif
#else
#if IOLINK_PD_IN_BUFFERS == 3U
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 4944u,
                     "dll ctx (RX FRAME, TX FRAME, SPLIT) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5128u,
                     "device ctx (RX FRAME, TX FRAME, SPLIT) changed");
#else
IOLINK_STATIC_ASSERT(sizeof(iolink_dll_ctx_t) == 4880u,
                     "dll ctx (RX FRAME, TX BYTE, SPLIT) changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_device_ctx_t) == 5064u,
                     "device ctx (RX FRAME, TX BYTE, SPLIT) changed");
#endif
#endif
#endif
IOLINK_STATIC_ASSERT(sizeof(iolink_isdu_ctx_t) == 1616u, "isdu ctx changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_ds_ctx_t) == 352u, "ds ctx changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_events_ctx_t) == 504u, "events ctx changed");
IOLINK_STATIC_ASSERT(sizeof(iolink_workspace_t) == 1551u, "workspace changed");

/* 布局契约：共享区必须是主上下文的首成员，ISR 状态紧随其后。
 * 使用显式的 typedef 名称，避免 C99 下按 __LINE__ 命名的断言在跨头文件
 * 展开时发生 typedef 重名。 */
typedef char ctx_layout_shared_first[offsetof(struct iolink_dll_main, shared) == 0U ? 1 : -1];
typedef char
    ctx_layout_isr_follows[offsetof(struct iolink_dll_ctx, isr) >= sizeof(iolink_dll_main_t) ? 1
                                                                                             : -1];
#endif

static void test_context_sizes(void** state)
{
    (void) state;
    /* 尺寸契约由本文件的编译期 IOLINK_STATIC_ASSERT 承担。 */
    printf("ctx sizes: dll=%zu device=%zu isdu=%zu ds=%zu events=%zu workspace=%zu\n",
           sizeof(iolink_dll_ctx_t), sizeof(iolink_device_ctx_t), sizeof(iolink_isdu_ctx_t),
           sizeof(iolink_ds_ctx_t), sizeof(iolink_events_ctx_t), sizeof(iolink_workspace_t));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_context_sizes),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
