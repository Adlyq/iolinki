/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file platform.c
 * @brief 默认的弱符号平台抽象实现。
 * @ingroup iolinki_platform
 *
 * 提供弱符号的空操作临界区默认实现以便协议栈链接，并提供一个探针，
 * 使 iolink_device_init() 能拒绝仍在使用这些实现的构建。真实移植会
 * 覆盖临界区符号。
 */

#include "iolinki/platform.h"

/* 弱定义使应用能够覆盖它们而不会产生链接错误。下面的探针仅在
 * 所选并发配置需要临界区（IOLINK_NEED_CRITICAL）时才会被
 * 查询。 */

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

static volatile uint8_t g_critical_default_used = 0U;

WEAK void iolink_critical_enter(void)
{
    /* 默认：不做任何事（若没有 IRQ 竞争，裸机单循环隐含是安全的） */
    g_critical_default_used = 1U;
}

WEAK void iolink_critical_exit(void)
{
    /* 默认：不做任何事 */
    g_critical_default_used = 1U;
}

int iolink_platform_critical_sections_ready(void)
{
    g_critical_default_used = 0U;
    iolink_critical_enter();
    iolink_critical_exit();
    const uint8_t used = g_critical_default_used;
    g_critical_default_used = 0U;
    return (used != 0U) ? -1 : 0;
}
