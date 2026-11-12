/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ring.c
 * @brief ISR 交接所使用的 SPSC 槽位环形缓冲区的单元测试。
 */

#include <assert.h>
#include <string.h>

#include "iolinki/ring.h"

#define TEST_SLOTS 2U
#define TEST_SLOT_SIZE 4U

static uint8_t g_storage[TEST_SLOTS * TEST_SLOT_SIZE];

/** @brief 认领、填充并发布一条记录；返回槽位（满时为 NULL）。 */
static uint8_t* push(iolink_ring_t* ring, uint8_t value)
{
    uint8_t* s = iolink_ring_claim(ring);
    if (s != NULL) {
        memset(s, value, TEST_SLOT_SIZE);
        s[0] = value;
        iolink_ring_publish(ring);
    }
    return s;
}

int main(void)
{
    iolink_ring_t ring;
    iolink_ring_init(&ring, g_storage, TEST_SLOTS, TEST_SLOT_SIZE);

    /* 空环形缓冲区：没有可 peek 的记录。 */
    assert(iolink_ring_is_empty(&ring));
    assert(iolink_ring_count(&ring) == 0U);
    assert(iolink_ring_peek(&ring) == NULL);

    /* 单条记录往返：peek 不消费，release 才消费。 */
    assert(push(&ring, 0xA1U) != NULL);
    assert(iolink_ring_count(&ring) == 1U);
    assert(!iolink_ring_is_empty(&ring));

    const uint8_t* rec = iolink_ring_peek(&ring);
    assert(rec != NULL);
    assert(rec[0] == 0xA1U);
    assert(rec[1] == 0xA1U);
    assert(iolink_ring_count(&ring) == 1U);
    iolink_ring_release(&ring);
    assert(iolink_ring_is_empty(&ring));

    /* 填满到容量上限后拒绝生产者（绝不覆盖
     * 消费者可能仍在读取的槽位）。 */
    assert(push(&ring, 0x01U) != NULL);
    assert(push(&ring, 0x02U) != NULL);
    assert(iolink_ring_count(&ring) == TEST_SLOTS);
    assert(iolink_ring_claim(&ring) == NULL);

    /* FIFO：最旧的记录最先取出。 */
    const uint8_t* first = iolink_ring_peek(&ring);
    assert(first != NULL);
    assert(first[0] == 0x01U);
    iolink_ring_release(&ring);

    /* 释放出的槽位可以再次发布，而另一条记录仍在等待。 */
    assert(push(&ring, 0x03U) != NULL);
    assert(iolink_ring_count(&ring) == TEST_SLOTS);

    const uint8_t* second = iolink_ring_peek(&ring);
    assert(second != NULL);
    assert(second[0] == 0x02U);
    iolink_ring_release(&ring);

    const uint8_t* third = iolink_ring_peek(&ring);
    assert(third != NULL);
    assert(third[0] == 0x03U);
    iolink_ring_release(&ring);
    assert(iolink_ring_is_empty(&ring));

    /* 槽位索引在大量记录之后回绕；顺序保持不变。 */
    for (uint8_t i = 0U; i < 10U; i++) {
        assert(push(&ring, i) != NULL);
        const uint8_t* r = iolink_ring_peek(&ring);
        assert(r != NULL);
        assert(r[0] == i);
        assert(r[2] == i);
        iolink_ring_release(&ring);
        assert(iolink_ring_is_empty(&ring));
    }

    return 0;
}
