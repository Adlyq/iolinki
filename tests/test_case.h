/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_case.h
 * @brief 统一 cmocka 用例的两种写法。
 *
 * 断言式用例直接写 `static void test_x(void** state)`。少数用例把断言写成
 * "返回首个失败的编号"，用 IOLINK_TEST_RC() 把这类函数暴露成 cmocka 用例：
 * 失败时会打印该编号，其余用例仍然执行。
 */

#ifndef IOLINK_TEST_CASE_H_
#define IOLINK_TEST_CASE_H_

#include <cmocka.h>

/* 声明一个返回 0 表示通过的用例函数，并生成同名的 cmocka 用例。
 * 定义式宏：调用时不要写结尾分号（文件作用域的 `;` 不是合法 C99）。 */
#define IOLINK_TEST_RC(fn)                                                                        \
    static void iolink_case_##fn(void** state)                                                    \
    {                                                                                             \
        (void) state;                                                                             \
        assert_int_equal(fn(), 0);                                                                \
    }

#endif /* IOLINK_TEST_CASE_H_ */
