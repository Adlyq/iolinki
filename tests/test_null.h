/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_null.h
 * @brief 兜底路径测试用的 NULL。
 *
 * 公共头把指针声明为默认非空（`IOLINK_ASSUME_NONNULL_BEGIN`）。函数内部
 * `if (ctx == NULL) return err;` 那样的分支只是调用方出错时的兜底，不属于契约，
 * 因此这些参数不会被标成 `IOLINK_NULLABLE`。
 *
 * 少数用例仍要验证这些分支。直接写 `f(NULL)` 会触发 `-Wnonnull`（契约确实要求
 * 非空），所以通过本函数把 NULL 传进去：前端不会对函数返回值做常量传播，检查
 * 不会误报，而"这是故意违反契约以测试兜底"的意图在测试里是显式的。
 */

#ifndef IOLINK_TEST_NULL_H_
#define IOLINK_TEST_NULL_H_

#include <stddef.h>

static inline void* test_null_ptr(void)
{
    return NULL;
}

/** @brief 按目标指针类型转换的 NULL，用法：`TEST_NULL(const iolink_od_app_t*)`。 */
#define TEST_NULL(type) ((type) test_null_ptr())

#endif /* IOLINK_TEST_NULL_H_ */
