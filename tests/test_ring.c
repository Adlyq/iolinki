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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "iolinki/ring.h"

#define TEST_SLOTS 2U
#define TEST_SLOT_SIZE 4U

static uint8_t g_storage[TEST_SLOTS * TEST_SLOT_SIZE];
static iolink_ring_t g_ring;

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

static void ring_reset(void)
{
    iolink_ring_init(&g_ring, g_storage, TEST_SLOTS, TEST_SLOT_SIZE);
}

static void test_empty_ring_has_no_record(void** state)
{
    (void) state;
    ring_reset();

    assert_true(iolink_ring_is_empty(&g_ring));
    assert_int_equal(iolink_ring_count(&g_ring), 0U);
    assert_null(iolink_ring_peek(&g_ring));
}

static void test_single_record_roundtrip(void** state)
{
    (void) state;
    ring_reset();

    /* 单条记录往返：peek 不消费，release 才消费。 */
    assert_non_null(push(&g_ring, 0xA1U));
    assert_int_equal(iolink_ring_count(&g_ring), 1U);
    assert_false(iolink_ring_is_empty(&g_ring));

    const uint8_t* rec = iolink_ring_peek(&g_ring);
    assert_non_null(rec);
    assert_int_equal(rec[0], 0xA1U);
    assert_int_equal(rec[1], 0xA1U);
    assert_int_equal(iolink_ring_count(&g_ring), 1U);
    iolink_ring_release(&g_ring);
    assert_true(iolink_ring_is_empty(&g_ring));
}

static void test_full_ring_rejects_producer(void** state)
{
    (void) state;
    ring_reset();

    /* 填满到容量上限后拒绝生产者（绝不覆盖
     * 消费者可能仍在读取的槽位）。 */
    assert_non_null(push(&g_ring, 0x01U));
    assert_non_null(push(&g_ring, 0x02U));
    assert_int_equal(iolink_ring_count(&g_ring), TEST_SLOTS);
    assert_null(iolink_ring_claim(&g_ring));
}

static void test_records_are_fifo(void** state)
{
    (void) state;
    ring_reset();

    assert_non_null(push(&g_ring, 0x01U));
    assert_non_null(push(&g_ring, 0x02U));

    /* FIFO：最旧的记录最先取出。 */
    const uint8_t* first = iolink_ring_peek(&g_ring);
    assert_non_null(first);
    assert_int_equal(first[0], 0x01U);
    iolink_ring_release(&g_ring);

    /* 释放出的槽位可以再次发布，而另一条记录仍在等待。 */
    assert_non_null(push(&g_ring, 0x03U));
    assert_int_equal(iolink_ring_count(&g_ring), TEST_SLOTS);

    const uint8_t* second = iolink_ring_peek(&g_ring);
    assert_non_null(second);
    assert_int_equal(second[0], 0x02U);
    iolink_ring_release(&g_ring);

    const uint8_t* third = iolink_ring_peek(&g_ring);
    assert_non_null(third);
    assert_int_equal(third[0], 0x03U);
    iolink_ring_release(&g_ring);
    assert_true(iolink_ring_is_empty(&g_ring));
}

static void test_slot_index_wraps(void** state)
{
    (void) state;
    ring_reset();

    /* 槽位索引在大量记录之后回绕；顺序保持不变。 */
    for (uint8_t i = 0U; i < 10U; i++) {
        assert_non_null(push(&g_ring, i));
        const uint8_t* r = iolink_ring_peek(&g_ring);
        assert_non_null(r);
        assert_int_equal(r[0], i);
        assert_int_equal(r[2], i);
        iolink_ring_release(&g_ring);
        assert_true(iolink_ring_is_empty(&g_ring));
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_empty_ring_has_no_record),
        cmocka_unit_test(test_single_record_roundtrip),
        cmocka_unit_test(test_full_ring_rejects_producer),
        cmocka_unit_test(test_records_are_fifo),
        cmocka_unit_test(test_slot_index_wraps),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
