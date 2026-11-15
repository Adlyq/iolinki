/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_RING_H
#define IOLINK_RING_H

#include <stdbool.h>
#include <stdint.h>

#include "iolinki/port.h"

/**
 * @file ring.h
 * @brief 供 ISR 交接使用的单生产者 / 单消费者槽位环形缓冲区。
 *
 * 协议栈在无需临界区的情况下，将记录（接收的帧、暂存的 ISDU 写入段）
 * 从数据 ISR 移动到主循环泵。该模式即 Linux kfifo 约定，
 * 使用固定大小的槽位：
 *
 * - `in` 仅由生产者写入，`out` 仅由消费者写入；
 * - 生产者填充已认领的槽位，然后发布 `in`（release）；
 * - 消费者读取 `out` 处的槽位，之后才推进 `out`
 *   （acquire）；
 * - 索引单调递增，并按无符号算术回绕，因此
 *   `in - out` 即为记录数，不存在歧义；
 * - 环形缓冲区排队的记录数永不超过 `slots`，队列满时生产者
 *   会被拒绝（返回 NULL），因此它绝不会覆盖消费者
 *   仍在读取的槽位。
 *
 * `slots` 必须是 2 的幂；对于排队变体，至少为 2。
 * 描述符是 POD：它指向调用方拥有的静态存储，因此不使用
 * 动态内存。
 *
 * @note 本头文件属于 DLL 传输约定，而非应用
 * API：应用代码只调用 iolink_device_* / iolink_dll_*。
 */

/**
 * @defgroup iolinki_ring SPSC 槽位环形缓冲区
 * @brief 数据 ISR 与主循环泵之间的无锁记录交接。
 * @{
 */

/** @brief 环形缓冲区描述符（生产者/消费者索引以及槽位存储）。 */
typedef struct
{
    uint8_t* data;                     /**< 调用方拥有的 `slots * slot_size` 字节存储 */
    uint8_t slots;                     /**< 槽位数量，必须为 2 的幂 */
    uint8_t slot_size;                 /**< 每个槽位的字节数 */
    volatile IOLINK_ATOMIC_UINT_T in;  /**< 仅由生产者写入 */
    volatile IOLINK_ATOMIC_UINT_T out; /**< 仅由消费者写入 */
} iolink_ring_t;

/** @brief 将环形缓冲区绑定到其存储，并复位两个索引。 */
static inline void iolink_ring_init(iolink_ring_t* ring, uint8_t* data, uint8_t slots,
                                    uint8_t slot_size)
{
    ring->data = data;
    ring->slots = slots;
    ring->slot_size = slot_size;
    ring->in = 0U;
    ring->out = 0U;
}

/** @brief 已发布但尚未释放的记录数。 */
static inline uint32_t iolink_ring_count(const iolink_ring_t* ring)
{
    return (uint32_t) (IOLINK_ATOMIC_UINT_T) (ring->in - ring->out);
}

/** @brief 环形缓冲区中没有记录时返回 true。 */
static inline bool iolink_ring_is_empty(const iolink_ring_t* ring)
{
    return ring->in == ring->out;
}

/**
 * @brief 为生产者预留下一个槽位。
 *
 * @return 可写槽位；环形缓冲区已满时返回 NULL。每次成功认领
 *         后必须恰好调用一次 iolink_ring_publish()，之后才能
 *         再次认领。
 */
static inline uint8_t* iolink_ring_claim(iolink_ring_t* ring)
{
    const IOLINK_ATOMIC_UINT_T in = ring->in;
    const IOLINK_ATOMIC_UINT_T out = iolink_atomic_load(&ring->out);
    if ((uint32_t) (IOLINK_ATOMIC_UINT_T) (in - out) >= (uint32_t) ring->slots) {
        return NULL; /* 已满：绝不覆盖消费者可能正在读取的槽位 */
    }
    return ring->data +
           ((uint32_t) (in & (IOLINK_ATOMIC_UINT_T) (ring->slots - 1U)) * ring->slot_size);
}

/** @brief 发布已认领的槽位（release：先数据，后索引）。 */
static inline void iolink_ring_publish(iolink_ring_t* ring)
{
    iolink_atomic_store(&ring->in, (IOLINK_ATOMIC_UINT_T) (ring->in + 1U));
}

/**
 * @brief 查看最旧的未释放记录，但不消费它。
 *
 * @return 只读槽位；环形缓冲区为空时返回 NULL。调用方处理完该
 *         记录后必须调用 iolink_ring_release()。
 */
static inline const uint8_t* iolink_ring_peek(const iolink_ring_t* ring)
{
    const IOLINK_ATOMIC_UINT_T out = ring->out;
    const IOLINK_ATOMIC_UINT_T in = iolink_atomic_load(&ring->in);
    if (in == out) {
        return NULL;
    }
    return ring->data +
           ((uint32_t) (out & (IOLINK_ATOMIC_UINT_T) (ring->slots - 1U)) * ring->slot_size);
}

/** @brief 释放最近一次 iolink_ring_peek() 返回的记录。 */
static inline void iolink_ring_release(iolink_ring_t* ring)
{
    iolink_atomic_store(&ring->out, (IOLINK_ATOMIC_UINT_T) (ring->out + 1U));
}

/** @} */ /* end of iolinki_ring */

#endif  // IOLINK_RING_H
