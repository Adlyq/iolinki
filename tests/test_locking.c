/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
#include "iolinki/diag.h"
#include "iolinki/events.h"
#include "iolinki/port.h"

/* Mock 计数器 */
static int g_enter_count = 0;
static int g_exit_count = 0;

/* 覆盖库中的弱符号 */
void iolink_critical_enter(void)
{
    g_enter_count++;
}

void iolink_critical_exit(void)
{
    g_exit_count++;
}

static void test_event_locking(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    g_enter_count = 0;
    g_exit_count = 0;

    /* 事件写路径不再取临界区：主上下文是唯一写者，ISR 只经发布索引
     * 读存储器。 */
    iolink_event_trigger(&ctx, 0x1800, IOLINK_EVENT_TYPE_NOTIFICATION);

    iolink_event_t ev;
    iolink_events_pop(&ctx, &ev);
    (void) iolink_events_peek(&ctx, &ev);

    /* 队列与 pending 表都归主上下文：读/clear 同样不需要与中断串行化。 */
    iolink_event_trigger(&ctx, 0x1000, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, 0x1000);

    assert_int_equal(g_enter_count, 0);
    assert_int_equal(g_exit_count, 0);
}

/* 诊断来源随触发一起写入：一次 diag_raise 与普通事件触发的临界区次数
 * 完全相同，不存在“返回后再查一次 pending[]”的第二处。DeviceStatus 与
 * DDS 快照只读主上下文独占的 pending 表，同样不取临界区。 */
static void test_diag_paths_add_no_critical_section(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    g_enter_count = 0;
    g_exit_count = 0;
    iolink_event_trigger(&ctx, 0x1000, IOLINK_EVENT_TYPE_ERROR);
    const int trigger_enters = g_enter_count;
    const int trigger_exits = g_exit_count;

    g_enter_count = 0;
    g_exit_count = 0;
    iolink_diag_pulse(&ctx, IOLINK_DIAG_SRC_COMM_CRC);

    assert_int_equal(g_enter_count, trigger_enters);
    assert_int_equal(g_exit_count, trigger_exits);

    g_enter_count = 0;
    g_exit_count = 0;
    (void) iolink_diag_device_status(&ctx);
    iolink_diag_dds_entry_t entries[IOLINK_EVENT_PENDING_MAX];
    (void) iolink_diag_dds_snapshot(&ctx, entries, IOLINK_EVENT_PENDING_MAX);

    assert_int_equal(g_enter_count, 0);
    assert_int_equal(g_exit_count, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_locking),
        cmocka_unit_test(test_diag_paths_add_no_critical_section),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
