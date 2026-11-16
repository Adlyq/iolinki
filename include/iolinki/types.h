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
/**
 * @brief 声明的默认空值约定：区域内未标注的指针按"必须非 NULL"处理。
 *
 * 每个公共头文件把声明区包在 BEGIN/END 之间，因此"必需"是默认，"可缺省"
 * 才需要写 @ref IOLINK_NULLABLE —— 与 Rust 的 `&T` / `Option<&T>` 同一取向。
 * Clang 的 `assume_nonnull` 是**文件作用域**的，不能在一个头里开、在另一个
 * 头里继承（`#pragma ... was not ended within this file`），所以每个头都要
 * 自带一对。
 */
#define IOLINK_ASSUME_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#define IOLINK_ASSUME_NONNULL_END _Pragma("clang assume_nonnull end")
/* 注解按需书写（只标注契约里有实际意义的指针），不要求每个指针都标，
 * 因此关掉完整性诊断。该 pragma 在包含本头之后对整个翻译单元生效：需要
 * 完整性检查的代码可在包含 iolinki 头之后用
 * `#pragma clang diagnostic warning "-Wnullability-completeness"` 恢复。
 *
 * 另外两条只在 `-Wpedantic` 下才会触发的"扩展用法"诊断也在这里关掉，否则
 * 公共头无法被 `-Wpedantic -Werror` 的调用方使用：
 * - `-Wnullability-extension`：`_Nonnull`/`_Nullable` 是 Clang 扩展，
 *   但注解本身是接口契约的一部分；语义检查（`-Wnullability`）仍然生效。
 * - `-Wc2y-extensions`：`port.h` 的 `IOLINK_STATIC_ASSERT` 用 `__COUNTER__`
 *   生成唯一名（C99 无 `_Static_assert`），这也是扩展用法。
 * 两者都不改变任何诊断的语义，只是不再因为"不是标准写法"而拒绝编译。
 * `-Wc2y-extensions` 只在较新的 clang 上存在，旧版本用 `__has_warning`
 * 保护，否则 `#pragma` 本身会触发 `-Wunknown-warning-option`。 */
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"
/* cppcheck 2.7 只认内建宏，不会求值 `__has_warning()`，会把它当成语法错误；
   这一行在真实编译器里是标准的"探测告警是否存在"写法。 */
// cppcheck-suppress syntaxError
#if defined(__has_warning) && __has_warning("-Wc2y-extensions")
#pragma clang diagnostic ignored "-Wc2y-extensions"
#endif
#else
#define IOLINK_NONNULL
#define IOLINK_NULLABLE
#define IOLINK_ASSUME_NONNULL_BEGIN
#define IOLINK_ASSUME_NONNULL_END
#endif

#endif /* IOLINK_TYPES_H */
