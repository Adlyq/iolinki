/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_EVENTS_INTERNAL_H
#define IOLINK_EVENTS_INTERNAL_H

#include <stdint.h>

#include "iolinki/events.h"

/**
 * @file events_internal.h
 * @brief 库内部使用、不安装的事件引擎入口。
 */

IOLINK_ASSUME_NONNULL_BEGIN

#if IOLINK_FEATURE_EVENTS

/**
 * @brief 触发事件并声明其诊断来源。
 *
 * 与 iolink_event_trigger() 相同，但当 @p code 获得（或已持有）一个
 * pending[] 槽位时，把该槽位的 @c source 写成 @p src。来源标记与槽位
 * 分配在同一临界区内完成，调用方不需要在触发之后再次查找条件表。
 *
 * @param ctx 事件上下文
 * @param code 16 位 IO-Link EventCode
 * @param type 严重级别（注册表登记的代码会被规范化）
 * @param src iolink_diag_src_t 取值，或 IOLINK_DIAG_SRC_NONE 表示调用方
 *            不声明来源；Notification 没有 pending 槽位，该参数被忽略
 * @note 主上下文专属入口。
 */
void iolink_event_trigger_src(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type,
                              uint8_t src);

#endif /* IOLINK_FEATURE_EVENTS */

IOLINK_ASSUME_NONNULL_END

#endif /* IOLINK_EVENTS_INTERNAL_H */
