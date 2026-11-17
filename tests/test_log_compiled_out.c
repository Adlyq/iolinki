/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_log_compiled_out.c
 * @brief 编译期日志级别为 0 时，消息整条消失且参数不被求值。
 *
 * 本目标以 `-DIOLINK_LOG_COMPILE_LEVEL=0` 编译（`tests/CMakeLists.txt`），
 * 因此四个宏必须展开成空语句：既不调用 sink，也不求值可变参数里的表达式。
 * 这一步是 ROM 预算的前提——`tools/size_matrix.py` 用同一档位测量。常规构建
 * 里的运行时阈值过滤由 `test_log.c` 覆盖。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/log.h"

static int g_calls;
static int g_arg_evaluations;

static void counting_write(iolink_app_t* IOLINK_NULLABLE self, iolink_log_level_t level,
                           const char* format, ...)
{
    (void) self;
    (void) level;
    (void) format;
    g_calls++;
}

/** @brief 作为日志参数被求值时计数；编译期关闭后它一次都不该运行。 */
static int evaluated(void)
{
    g_arg_evaluations++;
    return 1;
}

static void test_messages_are_compiled_out(void** state)
{
    (void) state;
    iolink_log_sink_t sink;
    /* 运行时阈值开到最大：若消息还在，它一定会被转发。 */
    iolink_log_sink_init(&sink, counting_write, NULL, IOLINK_LOG_LEVEL_DEBUG);

    /* 参数函数本身是活的：先直接调一次，后面的断言才有判别力。 */
    assert_int_equal(evaluated(), 1);
    assert_int_equal(g_arg_evaluations, 1);

    IOLINK_LOG_ERROR(&sink, "error %d", evaluated());
    IOLINK_LOG_WARN(&sink, "warn %d", evaluated());
    IOLINK_LOG_INFO(&sink, "info %d", evaluated());
    IOLINK_LOG_DEBUG(&sink, "debug %d", evaluated());
    IOLINK_LOG_ISR_WARN(&sink, "isr %d", evaluated());

    assert_int_equal(g_calls, 0);
    assert_int_equal(g_arg_evaluations, 1); /* 宏一次都没有求值参数 */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_messages_are_compiled_out),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
