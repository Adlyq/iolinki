/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_ISR_INTERNAL_H
#define IOLINK_DLL_ISR_INTERNAL_H

/**
 * @file dll_isr_internal.h
 * @brief DLL ISR 侧跨文件声明（仅供 src/ 使用）。
 *
 * `src/dll_isr.c` 与 `src/dll_isr_rx.c` 之间的库私有入口；
 * 两者都只在数据 ISR 上下文运行，不属于公共 iolink_ API。
 */

#include "iolinki/internal/dll_main.h"

bool dll_isr_is_isdu_write(const uint8_t* frame);
bool dll_isr_consume_isdu_write(iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len);
bool dll_isr_plan_lookup(const iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len,
                         iolink_reply_plan_t* out);
bool dll_isr_plan_execute(iolink_dll_main_t* ctx, const iolink_reply_plan_t* plan,
                          const uint8_t* frame, uint8_t len, bool staged);
void dll_isr_drain(iolink_dll_main_t* ctx);
void dll_isr_tx_step(iolink_dll_main_t* ctx);

#endif /* IOLINK_DLL_ISR_INTERNAL_H */
