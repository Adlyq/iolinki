/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file internal/device_wiring.h
 * @brief 设备层初始化接线（不是应用 API）。
 *
 * 与 `internal/wiring.h` 分开，避免主上下文 DLL 翻译单元间接包含
 * `device.h`（`tools/check_ownership.sh` 的负向边界）。
 */

#ifndef IOLINK_INTERNAL_DEVICE_WIRING_H_
#define IOLINK_INTERNAL_DEVICE_WIRING_H_

#include "iolinki/device.h"
#include "iolinki/log.h"
#include "iolinki/types.h"

#ifdef __cplusplus
extern "C" {
#endif

IOLINK_ASSUME_NONNULL_BEGIN

/** @brief 绑定日志汇点（仅初始化期）。
 *  @note `iolink_device_init()` 返回后该汇点已锁定，再调用本函数是空操作：
 *        数据 ISR 读取同一组 `write`/`self`/`level` 字段。重新初始化会解锁。
 */
void iolink_device_set_log(iolink_device_ctx_t* ctx, iolink_log_write_fn IOLINK_NULLABLE write,
                           iolink_log_level_t level);

IOLINK_ASSUME_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* IOLINK_INTERNAL_DEVICE_WIRING_H_ */
