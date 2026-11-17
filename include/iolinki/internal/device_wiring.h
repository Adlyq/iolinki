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

#ifdef __cplusplus
extern "C" {
#endif

/** @brief 绑定日志汇点（初始化期；ISR 日志启用时必须在 ISR 静止时调用）。 */
void iolink_device_set_log(iolink_device_ctx_t* ctx, iolink_log_write_fn IOLINK_NULLABLE write,
                           iolink_log_level_t level);

#ifdef __cplusplus
}
#endif

#endif /* IOLINK_INTERNAL_DEVICE_WIRING_H_ */
