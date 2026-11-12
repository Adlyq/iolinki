/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_CTX_H
#define IOLINK_DLL_CTX_H

#include "iolinki/dll_main.h"
#include "iolinki/dll_isr.h"

/**
 * @file dll_ctx.h
 * @brief DLL 的单次分配容器（主上下文 + 数据 ISR）。
 *
 * 应用通过设备上下文持有本容器；只有组装根（device.c）与测试
 * 需要看到完整布局。dll.c 只包含 dll_main.h，看不到 ISR 字段。
 *
 * @warning 主上下文的共享区保存指向 @c isr 成员的指针
 *          （@c main.shared.isr）。容器只能原地初始化与使用；按值拷贝
 *          会让该指针继续指向原对象，必须整体重新初始化。
 */
struct iolink_dll_ctx
{
    iolink_dll_main_t main; /**< 主上下文状态（含共享区） */
    iolink_dll_isr_t isr;   /**< 数据 ISR 独占状态 */
};

#endif /* IOLINK_DLL_CTX_H */
