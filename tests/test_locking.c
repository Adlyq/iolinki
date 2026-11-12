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
#include "iolinki/events.h"
#include "iolinki/platform.h"

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

    /* 触发事件应进入临界区 */
    iolink_event_trigger(&ctx, 0x1800, IOLINK_EVENT_TYPE_NOTIFICATION);

#if IOLINK_NEED_CRITICAL
    assert_int_equal(g_enter_count, 1);
    assert_int_equal(g_exit_count, 1);

    /* 弹出事件同样应验证加锁行为 */
    iolink_event_t ev;
    iolink_events_pop(&ctx, &ev);

    assert_int_equal(g_enter_count, 2);
    assert_int_equal(g_exit_count, 2);
#else
    /* SINGLE 配置：协议栈不引用临界区，因此上面的强符号覆盖
     * 绝不应被执行。 */
    iolink_event_t ev;
    iolink_events_pop(&ctx, &ev);

    assert_int_equal(g_enter_count, 0);
    assert_int_equal(g_exit_count, 0);
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_locking),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
