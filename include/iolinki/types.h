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

#endif /* IOLINK_TYPES_H */
