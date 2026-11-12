/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_CTX_H
#define IOLINK_DLL_CTX_H

#include <stddef.h>

#include "iolinki/dll_main.h"
#include "iolinki/dll_isr.h"

/**
 * @file dll_ctx.h
 * @brief DLL 的单次分配容器（主上下文 + 数据 ISR）。
 *
 * 应用通过设备上下文持有本容器；只有组装根（device.c）与测试
 * 需要看到完整布局。dll.c 只包含 dll_main.h，看不到 ISR 字段。
 */
struct iolink_dll_ctx
{
    iolink_dll_main_t main; /**< 主上下文状态（含共享区） */
    iolink_dll_isr_t isr;   /**< 数据 ISR 独占状态 */
};

/* 布局契约：共享区必须是主上下文的首成员；ISR 状态必须紧随主上下文。
 * 这两条断言让"单次分配、兄弟分区"的结构在编译期可见。 */
IOLINK_STATIC_ASSERT(offsetof(struct iolink_dll_main, shared) == 0U,
                     "the shared region must be the first member of the main context");
IOLINK_STATIC_ASSERT(offsetof(struct iolink_dll_ctx, isr) >= sizeof(iolink_dll_main_t),
                     "the ISR state must follow the main context");

#endif /* IOLINK_DLL_CTX_H */
