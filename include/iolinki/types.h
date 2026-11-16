/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_TYPES_H
#define IOLINK_TYPES_H

/**
 * @file types.h
 * @brief 公共接口共用的应用上下文声明。
 */

/**
 * @brief 应用上下文：一个协议实例的唯一应用指针。
 *
 * 库只保存指针并把同一个值原样交给每个回调，从不访问它的字段，
 * 因此这里只有前置声明。应用在自己的翻译单元里定义
 * `struct iolink_app`（状态、驱动句柄、参数变量都可以放进去），
 * 然后在 `iolink_device_app_t::self` 里交出该实例的地址。
 *
 * 具名类型让"把别的指针传进来"变成编译期诊断：把
 * `&phy_state` 或 `&od_table` 赋给 `self` 需要显式强转，而
 * `void*` 不会报错。C 只能做到这一步——显式强转仍然可以绕过。
 */
typedef struct iolink_app iolink_app_t;

/**
 * @brief 有意擦除类型的指针（空注解）。
 *
 * 只用在接口处无法命名具体类型的场合：字节工具、符号表、以及下层回调
 * 的调用方上下文。写在指针声明之后，例如 `void* IOLINK_OPAQUE user;`。
 * 每处都必须在其 Doxygen 里说明为什么非擦除不可。
 */
#define IOLINK_OPAQUE

/**
 * @brief 指针的空值约定。
 *
 * - `IOLINK_NONNULL`：必须非 NULL（必需输入，缺失即初始化/调用失败）。
 * - `IOLINK_NULLABLE`：允许 NULL，声明"缺失即不支持/不需要"。NULL 是接口
 *   约定的一部分，不是走另一条路径的信号。
 *
 * 两者都写在被声明的名字前，例如
 * `const iolink_od_app_t* IOLINK_NULLABLE od;`。Clang 下展开为
 * `_Nonnull`/`_Nullable` 参与静态检查，其他编译器下为空。
 */
#if defined(__clang__)
#define IOLINK_NONNULL _Nonnull
#define IOLINK_NULLABLE _Nullable
/* 注解按需书写（只标注契约里有实际意义的指针），不要求每个指针都标。 */
#pragma clang diagnostic ignored "-Wnullability-completeness"
#else
#define IOLINK_NONNULL
#define IOLINK_NULLABLE
#endif

#endif /* IOLINK_TYPES_H */
