/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_port_capabilities.c
 * @brief 校验 iolinki/port.h 的分级并发原语约定。
 *
 * 该可执行文件在每个 IOLINK_CONCURRENCY 配置下都会构建，因此它证明了
 * 推导逻辑、可移植屏障和发布字辅助函数
 * 在各自配置所需的最小原语集合下均可编译并链接。
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/port.h"

static volatile IOLINK_ATOMIC_UINT_T g_word;

/** 先发布字、后数据的布局，即协议栈使用的布局。 */
typedef struct
{
    uint8_t leading;
    IOLINK_ATOMIC_UINT_T word;
} publish_probe_t;

#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
IOLINK_STATIC_ASSERT(offsetof(publish_probe_t, word) % sizeof(IOLINK_ATOMIC_UINT_T) == 0,
                     "the publish word must be naturally aligned");
#endif

int main(void)
{
    /* 配置必须是文档中列出的取值之一。 */
    assert(IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_SINGLE);
    assert(IOLINK_CONCURRENCY <= IOLINK_CONCURRENCY_SMP);

    /* 需求标志保持布尔语义。 */
    assert((IOLINK_NEED_FENCE == 0) || (IOLINK_NEED_FENCE == 1));
    assert((IOLINK_NEED_CRITICAL == 0) || (IOLINK_NEED_CRITICAL == 1));

    /* 每个异步配置都需要屏障；SINGLE 会将其折叠掉。 */
#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
    assert(IOLINK_NEED_FENCE != 0);
    assert(IOLINK_NEED_CRITICAL != 0);
#else
    assert(IOLINK_NEED_FENCE == 0);
    assert(IOLINK_NEED_CRITICAL == 0);
#endif

    /* 可移植屏障与互斥包装在每个配置下均可编译。 */
    IOLINK_FENCE();
    IOLINK_CRITICAL_ENTER();
    IOLINK_CRITICAL_EXIT();

    /* 发布字往返测试（每个上下文是其字的唯一写入者）。 */
    iolink_atomic_store(&g_word, (IOLINK_ATOMIC_UINT_T) 0x5AU);
    assert(iolink_atomic_load(&g_word) == (IOLINK_ATOMIC_UINT_T) 0x5AU);
    iolink_atomic_store(&g_word, (IOLINK_ATOMIC_UINT_T) 0U);
    assert(iolink_atomic_load(&g_word) == (IOLINK_ATOMIC_UINT_T) 0U);

    return 0;
}
