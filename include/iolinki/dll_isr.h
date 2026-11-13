/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_ISR_H
#define IOLINK_DLL_ISR_H

#include "iolinki/dll.h"

/**
 * @file dll_isr.h
 * @brief 数据 ISR 独占的 DLL 状态。
 *
 * 只有 dll_isr.c（以及负责布局的容器头 dll_ctx.h）包含此文件。
 * 主上下文（dll.c）不包含它，因此无法引用这些字段。
 */
struct iolink_dll_isr
{
    IOLINK_ATOMIC_UINT_T isr_isdu_adopted; /**< 由 ISR 采纳的字 */

    uint8_t isr_isdu_len; /**< 采纳的应答长度（0 = 无） */

    uint8_t isr_isdu_sel; /**< 采纳的应答缓冲区索引 */

    uint8_t isr_isdu_idx; /**< 下一个应答字节（由 ISR 推进） */

    uint8_t isr_isdu_seg_idx; /**< 最后服务的段偏移 */

    /* 原始字节 ISR 泵状态（由库拥有，由 PHY 数据
     * 中断驱动）。ISR 上下文只触碰这些字段；应答
     * 暂存缓冲区与字节喂送由库拥有（PHY 只负责
     * SPI 电气访问）。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    uint8_t isr_rx_buf[IOLINK_M_SEQ_MAX_LEN]; /**< 已累积的 RX 字节 */
    uint8_t isr_rx_cnt;                       /**< 本帧已累积的字节 */
    uint8_t isr_rx_acc;                       /**< 按 A.1.6 逐字节累积的校验和 */
    uint8_t isr_rx_expect;                    /**< 期望长度（0 = 未知） */
#endif

    uint32_t isr_eager_rejects; /**< eager 路径拒绝的帧
                                     （校验和/类型错误） */

    volatile uint8_t stack_tick_pending; /**< Timer B 节拍已授权一个
                                              主上下文服务窗口 */

    volatile uint8_t isr_reentry_pending; /**< 一个抢占的泵调用
                                                已延后到外层调用 */

    const uint8_t* isr_tx_base; /**< 当前应答来源：
                                     长 PD 应答为 pd_in[active]，
                                     短应答/主上下文应答为
                                     isr_tx_payload */

    volatile uint8_t isr_tx_slot; /**< 正在发送的槽位 */

    volatile uint8_t isr_tx_flags; /**< CKS 状态标志 */

    volatile uint8_t isr_tx_acc; /**< 运行中的校验和累加器 */
};

#endif /* IOLINK_DLL_ISR_H */
