/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include "iolinki/platform.h"
#include "dll_internal.h"
#include <string.h>

/**
 * @file dll_isr.c
 * @brief 数据链路层 ISR 双泵传输。
 * @ingroup iolinki_dll
 *
 * 从数据中断运行：请求成帧、即时回复、ISDU
 * 快照读取路径以及分阶段 TX 泵。主泵与帧
 * 分发位于 dll.c；应用回调绝不能在此运行。
 *
 * 本文件命名约定：dll_isr_* 仅可在 ISR 上下文中调用；
 * 普通 dll_* 辅助函数与主泵共享（上下文约定表见
 * docs/CODE_FLOW.md 第 12.1 节）。例外：dll_isr_start_staged_tx()
 * 也会在主泵通过 dll_send() 的直接发送
 * 回退路径中被调用。
 *
 * ISR 泵（入口 iolink_dll_process_isr）：
 *
 *   iolink_dll_process_isr              重入保护：in_isr 加上
 *   |                                   isr_reentry_pending 循环
 *   +- dll_isr_process
 *      |- isr_tx_pending -> dll_isr_tx_pending -> dll_isr_start_staged_tx
 *      |                    （发送由主泵暂存的回复）
 *      |- int_active() == 0 -> return  （电平复查）
 *      +- dll_isr_drain                突发循环，最多 64 次迭代
 *         |- [TX BYTE] isr_tx_active -> dll_isr_tx_step （一个八位组，break）
 *         |- [RX FRAME] dll_isr_drain_rx_frame
 *         |  +- phy->rx_frame -> dll_isr_frame_len_ok ->
 *         |     dll_isr_frame_valid -> dll_isr_complete_frame
 *         +- [RX BYTE] dll_isr_drain_rx_byte
 *            +- phy->rx_octet -> gap / accumulate / derive_expect /
 *               rx_byte_complete -> dll_isr_complete_frame
 *
 * dll_isr_complete_frame（两种 RX 粒度共用的每帧决策点，
 * 按以下顺序执行）：
 *  1. ISDU 写：dll_isr_consume_isdu_write -> 暂存到 isr_od_ring（
 *     传输本身稍后由主泵执行）。
 *  2. Type-0 快速路径：dll_isr_handle_type0 -> 启动探测 / 转换 /
 *     读 / 主站命令，由 ISR 应答。
 *  3. 即时决策：dll_isr_wants_eager；dll_isr_start_reply 构建 OD
 *     来源为 ISDU 快照 / 直接参数页 / 事件存储器，并启动
 *     TX（帧或字节模式）。
 *  4. 将帧连同即时标志发布到邮箱（dll_isr_publish_frame），
 *     使主泵知道回复已经在总线上。
 *
 * 交接与顺序约束：
 *  - 即时回复在邮箱发布*之前*启动（t_A）；记录随后
 *    连同其八位组一起携带 eager=1。
 *  - ctx->isdu 归主泵所有：ISR 只读取已发布的响应视图
 *    并在 isr_od_ring 中暂存写操作。
 *  - 64 位截止时间归主泵所有：ISR 只设置一次性标志。
 */

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static void dll_isr_tx_step(iolink_dll_ctx_t* ctx);
#endif

/**
 * @brief 开始发送已暂存的回复槽位。
 *
 * 由 dll_send() 的直接发送回退路径（没有 request_service() 的 PHY）
 * 与 ISR 待回复路径共享，因此两者以相同方式驱动 TX 钩子。
 */
void dll_isr_start_staged_tx(iolink_dll_ctx_t* ctx, uint8_t slot, uint8_t len)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if (ctx->phy->tx_frame != NULL) {
        if (ctx->phy->tx_frame(ctx->phy->user, ctx->isr_tx_payload[slot], len) != 0) {
            IOLINK_LOG_ISR_WARN("tx_frame failed: len=%u", (unsigned) len);
            /* 由主泵回退路径与 ISR 泵共享：将丢弃归属到
             * 正在运行的上下文，使 SPLIT 计数器保持精确。 */
            if (ctx->in_isr != 0U) {
                IOLINK_STAT_ISR(ctx, isr_tx_dropped)++;
            }
            else {
                ctx->isr_tx_dropped++;
            }
        }
    }
#else
    (void) len;
    if (ctx->phy->tx_octet != NULL) {
        if (ctx->isr_tx_active != 0U) {
            if (ctx->phy->tx_done != NULL) {
                ctx->phy->tx_done(ctx->phy->user);
            }
            ctx->isr_tx_active = 0U;
        }
        ctx->isr_tx_base = ctx->isr_tx_payload[slot];
        ctx->isr_tx_slot = slot;
        ctx->isr_tx_idx = 0U;
        ctx->isr_tx_has_cks = 0U;
        ctx->isr_tx_active = 1U;
        dll_isr_tx_step(ctx);
    }
#endif
}

/* -------------------------------------------------------------------------
 * ISR 双泵传输
 *
 * 以下所有内容均从数据中断运行。主泵暂存回复
 * 并将 ISDU 快照移交到这里；应用回调绝不能在此
 * 段落中运行。
 * ------------------------------------------------------------------------- */

/* ISR 即时回复的一致性门控：主站请求必须携带
 * 有效的 A.1.6 校验和，并且对当前状态是合法的 M-sequence 类型，设备
 * 才会应答。该检查与 dll_process_rx_frame() 稍后
 * 执行的校验一致；即时回复绝不能应答主泵本会拒绝的
 * 损坏或非法帧。只读且 ISR 安全。
 *
 * 当前状态的合法 M-sequence 类型（规则与主泵相同）：
 * STARTUP/PREOPERATE 为 Type-0；OPERATE（以及转换完成后
 * 的 ESTAB_COM）期望配置的类型。 */
static bool dll_isr_type_ok(const iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    return dll_type_allowed(ctx, frame);
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* 通过重新扫描帧来校验请求校验和（用于 FRAME 路径，
 * 该路径下 PHY 交出整个缓冲区）。 */
static bool dll_isr_frame_valid(const iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((ctx == NULL) || (frame == NULL) || (len < 2U)) {
        return false;
    }
    const uint8_t expected = (uint8_t) (frame[1] & 0x3FU);
    /* A.1.6 种子；八位组 1（CKT）被掩码到其类型位。将其放在
     * 循环外处理，使其余八位组运行紧凑的 XOR 循环。 */
    uint8_t acc = (uint8_t) (0x52U ^ frame[0]);
    acc = (uint8_t) (acc ^ (frame[1] & IOLINK_MSEQ_TYPE_MASK));
    for (uint8_t i = 2U; i < len; i++) {
        acc = (uint8_t) (acc ^ frame[i]);
    }
    return (iolink_checksum6_from_acc(acc) == expected) && dll_isr_type_ok(ctx, frame);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/* 根据每接收一个八位组所构建的累加器校验请求校验和
 * （BYTE 路径）：ISR 中不再对帧做第二遍扫描。 */
static bool dll_isr_frame_valid_acc(const iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len,
                                    uint8_t acc)
{
    if ((ctx == NULL) || (frame == NULL) || (len < 2U)) {
        return false;
    }
    const uint8_t expected = (uint8_t) (frame[1] & 0x3FU);
    return (iolink_checksum6_from_acc(acc) == expected) && dll_isr_type_ok(ctx, frame);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/* 将已完成的请求复制到主泵的邮箱环中。由 ISR
 * 调用；当环已满时返回 false（并计入一次丢弃）。八位组
 * 在发布之前复制，因此 ISR 一返回 PHY 即可复用其接收
 * 缓冲区。@p eager 记录该 ISR 是否已
 * 发送回复（此时主泵必须保持静默）。 */
static bool dll_isr_publish_frame(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len,
                                  uint8_t eager)
{
    if ((frame == NULL) || (len > IOLINK_M_SEQ_MAX_LEN)) {
        IOLINK_LOG_ISR_DEBUG("frame publish rejected: len=%u", (unsigned) len);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        return false;
    }
    iolink_frame_rec_t* record = (iolink_frame_rec_t*) iolink_ring_claim(&ctx->isr_frame_ring);
    if (record == NULL) {
        IOLINK_LOG_ISR_WARN("mailbox full: frame dropped");
        ctx->isr_mailbox_drops++;
        return false;
    }
    record->len = len;
    record->eager = eager;
    (void) memcpy(record->data, frame, len);
    iolink_ring_publish(&ctx->isr_frame_ring); /* 最后发布 */
    return true;
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
/* 发送活动回复的一步：下一个负载八位组，然后是计算出的
 * CKS（CKS 模式），最后是帧结束。每次收发器请求调用一次。 */
static void dll_isr_tx_step(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_tx_idx < ctx->isr_tx_len) {
        const uint8_t octet = ctx->isr_tx_base[ctx->isr_tx_idx];
        (void) ctx->phy->tx_octet(ctx->phy->user, octet);
        if (ctx->isr_tx_has_cks != 0U) {
            ctx->isr_tx_acc = (uint8_t) (ctx->isr_tx_acc ^ octet);
        }
        ctx->isr_tx_idx++;
    }
    else if ((ctx->isr_tx_has_cks != 0U) && (ctx->isr_tx_idx == ctx->isr_tx_len)) {
        /* 负载完成：输出 CKS = flags | checksum6(acc ^ flags)。 */
        const uint8_t acc = (uint8_t) (ctx->isr_tx_acc ^ ctx->isr_tx_flags);
        const uint8_t cks = (uint8_t) (ctx->isr_tx_flags | iolink_checksum6_from_acc(acc));
        (void) ctx->phy->tx_octet(ctx->phy->user, cks);
        ctx->isr_tx_idx++;
    }
    else {
        if (ctx->phy->tx_done != NULL) {
            ctx->phy->tx_done(ctx->phy->user);
        }
        if (ctx->phy->stack_timer_start != NULL) {
            ctx->phy->stack_timer_start(ctx->phy->user); /* 帧结束 -> Timer B */
        }
        ctx->isr_tx_active = 0U;
        /* TX BYTE：回复已完成，因此 ISR 租用的 PD-in 槽位
         * 再次对主泵可用。 */
        ctx->pd_in_captured = 0xFFU;
    }
}
#endif /* IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE */

/* 从已发布的响应视图填充 ISDU 读回复的 OD 八位组。 */
static void dll_isr_isdu_od(iolink_dll_ctx_t* ctx, uint8_t flowctrl, uint8_t* od, uint8_t od_width)
{
    if (od_width == 0U) {
        return;
    }
    for (uint8_t i = 0U; i < od_width; i++) {
        od[i] = 0x00U;
    }

    /* 一次原子加载得到一致的 {length, busy, buffer} 元组。 */
    const IOLINK_ATOMIC_UINT_T pub = iolink_atomic_load(&ctx->isr_isdu_pub);
    bool repeat = false;

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        if (IOLINK_ISDU_PUB_BUSY(pub) != 0U) {
            od[0] = 0x01U; /* 忙 */
            return;
        }
        if (IOLINK_ISDU_PUB_LEN(pub) == 0U) {
            od[0] = 0x00U; /* 无服务 */
            return;
        }
        /* 采纳已发布的响应：从此处起 ISR 拥有段
         * 索引，且缓冲区选择在下一次 START 之前保持稳定。 */
        ctx->isr_isdu_adopted = pub;
        ctx->isr_isdu_len = IOLINK_ISDU_PUB_LEN(pub);
        ctx->isr_isdu_sel = IOLINK_ISDU_PUB_SEL(pub);
        ctx->isr_isdu_idx = 0U;
        ctx->isr_isdu_seg_idx = 0U;
        ctx->isr_isdu_last_flowctrl = IOLINK_FLOWCTRL_START;
    }
    else if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        od[0] = 0x00U; /* IDLE / IDLE2 / ABORT / 保留：无服务 */
        return;
    }
    else {
        /* COUNT（0x00..0x0F）。已发布视图发生变化（新响应或 Busy
         * 转换）会使段序列失效；主站必须
         * 以 START 重新开始。重复的 COUNT 重复前一个段；
         * 任何其他值必须是 +1。 */
        if ((ctx->isr_isdu_adopted != pub) || (ctx->isr_isdu_last_flowctrl == 0xFFU)) {
            od[0] = 0x00U; /* 无服务 */
            ctx->isr_isdu_last_flowctrl = 0xFFU;
            return;
        }
        if (flowctrl == ctx->isr_isdu_last_flowctrl) {
            repeat = true;
        }
        else {
            const uint8_t expected =
                (uint8_t) ((ctx->isr_isdu_last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
            if (flowctrl != expected) {
                od[0] = 0x00U; /* 分段错误 -> 无服务 */
                ctx->isr_isdu_last_flowctrl = 0xFFU;
                return;
            }
            ctx->isr_isdu_last_flowctrl = flowctrl;
        }
    }

    /* START（就绪）、新的 COUNT 或重复：处理一个 od_width 段。
     * 重复会重新发送前一个段而不推进索引。 */
    uint8_t idx = repeat ? ctx->isr_isdu_seg_idx : ctx->isr_isdu_idx;
    const uint8_t len = ctx->isr_isdu_len;
    const uint8_t* resp = ctx->isdu.resp_store[ctx->isr_isdu_sel];
    uint8_t count = 0U;
    while ((count < od_width) && (idx < len)) {
        od[count++] = resp[idx++];
    }
    if (!repeat) {
        ctx->isr_isdu_seg_idx = ctx->isr_isdu_idx;
        ctx->isr_isdu_idx = idx;
    }
}

/* CKS 状态标志：事件（bit7）+ PD 无效（bit6）。ISR 安全（普通读取）。 */
static uint8_t dll_isr_status_flags(const iolink_dll_ctx_t* ctx)
{
    uint8_t flags = 0U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        flags |= 0x80U;
    }
    flags |= dll_pd_status_flags(ctx);
    return flags;
}

/* ISR 总是使用自己的暂存槽位，因此主泵可以继续
 * 在 0/1 之间交替，而不与中断上下文共享计数器。 */
static uint8_t dll_isr_tx_alloc(const iolink_dll_ctx_t* ctx)
{
    (void) ctx;
    return IOLINK_TX_ISR_SLOT;
}

/* 从 ISR 发送一个短回复（负载 + CKS），由 Type-0 快速
 * 路径使用。负载缓冲区是库自己的暂存缓冲区，因此在 TX FRAME 传输
 * 期间保持有效。校验和以
 * flags | checksum6(acc ^ flags) 的形式追加到同一缓冲区。 */
static void dll_isr_send_staged(iolink_dll_ctx_t* ctx, uint8_t slot, uint8_t len, uint8_t flags)
{
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user); /* 回复开始 -> 停止 t_A */
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 短回复：第一个八位组一移入
     * 移位寄存器，DMA 就会读取 CKS（帧开始时 TDR 立即清空），因此
     * CKS 必须在 tx_frame() 之前就在缓冲区中。只有长 PD 回复
     * 才有足够的距离（>= 28 个八位组）在 DMA 启动后计算它。 */
    uint8_t* payload = ctx->isr_tx_payload[slot];
    uint8_t acc = 0x52U;
    for (uint8_t i = 0U; i < len; i++) {
        acc = (uint8_t) (acc ^ payload[i]);
    }
    payload[len] = (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
    ctx->isr_tx_len = (uint8_t) (len + 1U);
    ctx->isr_tx_active = 0U;
    if (ctx->phy->tx_frame != NULL) {
        if (ctx->phy->tx_frame(ctx->phy->user, payload, ctx->isr_tx_len) != 0) {
            IOLINK_STAT_ISR(ctx, isr_tx_dropped)++; /* PHY 忙：回复丢失 */
        }
    }
#else
    ctx->isr_tx_base = ctx->isr_tx_payload[slot];
    ctx->isr_tx_slot = slot;
    ctx->isr_tx_len = len;
    ctx->isr_tx_flags = flags;
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_acc = 0x52U;
    ctx->isr_tx_has_cks = 1U;
    ctx->isr_tx_active = 1U;
    dll_isr_tx_step(ctx);
#endif
}

static void dll_isr_send_payload(iolink_dll_ctx_t* ctx, const uint8_t* payload, uint8_t len,
                                 uint8_t flags)
{
    const uint8_t slot = dll_isr_tx_alloc(ctx);
    if (payload != NULL) {
        for (uint8_t i = 0U; i < len; i++) {
            ctx->isr_tx_payload[slot][i] = payload[i];
        }
    }
    dll_isr_send_staged(ctx, slot, len, flags);
}

/* 阶段 3 快速路径：应答不改变 DLL 状态的 Type-0 帧。
 * 仅 PREOPERATE/OPERATE；ISDU 读/写、页读和过程读。
 * 页/诊断写（MasterCommand/Event 处理）和启动探测
 * 仍走主状态机。 */
/** @brief 帧已由 ISR 处理：刷新活动时间戳。 */
static void dll_isr_touch_activity(iolink_dll_ctx_t* ctx)
{
    ctx->last_activity_ms = dll_time_ms(ctx);
    ctx->last_byte_us = (uint32_t) dll_time_us(ctx);
}

/** @brief Type-0 启动探测（T1）：等待首条报文时进行页读。 */
static bool dll_isr_type0_startup_probe(iolink_dll_ctx_t* ctx, bool read, uint8_t channel,
                                        uint8_t addr, uint8_t len)
{
    if (!read || (channel != IOLINK_MC_CHANNEL_PAGE) || (len != 2U)) {
        return false;
    }
    dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
    /* 64 位 T_DSIO 截止时间归主泵所有：请求清除它，而
     * 不是从 ISR 写入（避免主泵中 64 位读取被撕裂）。 */
    ctx->isr_dsio_clear = 1U;
    const uint8_t od = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
    dll_isr_send_payload(ctx, &od, 1U, dll_isr_status_flags(ctx));
    dll_isr_touch_activity(ctx);
    return true;
}

/** @brief 转换命令（PREOPERATE -> ESTAB_COM）：按规范无响应。 */
static bool dll_isr_type0_transition(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((ctx->state == IOLINK_DLL_STATE_PREOPERATE) && (len == 2U) &&
        (frame[0] == IOLINK_MC_TRANSITION_COMMAND)) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->fallback_count = 0U;
        /* 对转换命令不应答：关闭 t_A 窗口。 */
        if (ctx->phy->response_timer_stop != NULL) {
            ctx->phy->response_timer_stop(ctx->phy->user);
        }
        if (ctx->phy->stack_timer_start != NULL) {
            ctx->phy->stack_timer_start(ctx->phy->user);
        }
        dll_isr_touch_activity(ctx);
        return true;
    }
    return false;
}

/** @brief Type-0 读：从 ISR 应答页/ISDU/过程/诊断。 */
static bool dll_isr_type0_read(iolink_dll_ctx_t* ctx, uint8_t channel, uint8_t addr)
{
#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
    /* Tables 57/67（SDCI_TC_0038/0049）：OPERATE 生效时的一次 TYPE_0 页读
       是复位请求。不再应答而是消费它并返回
       STARTUP；下一次读将作为启动探测被处理。 */
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) && (channel == IOLINK_MC_CHANNEL_PAGE) &&
        (ctx->m_seq_type != IOLINK_M_SEQ_TYPE_0)) {
        /* 有意不应答（Tables 57/67）：关闭 t_A 窗口。 */
        if (ctx->phy->response_timer_stop != NULL) {
            ctx->phy->response_timer_stop(ctx->phy->user);
        }
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        dll_isr_touch_activity(ctx);
        return true;
    }
#endif
    uint8_t od = 0U;
    if (channel == IOLINK_MC_CHANNEL_PAGE) {
        od = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
    }
    else if (channel == IOLINK_MC_CHANNEL_ISDU) {
        /* IDLE/ABORT 在主窗口中重置 ISDU 传输；将这些
         * 交给主泵处理。 */
        if (addr >= IOLINK_FLOWCTRL_IDLE) {
            return false;
        }
        dll_isr_isdu_od(ctx, addr, &od, 1U);
    }
    else if (channel == IOLINK_MC_CHANNEL_PROCESS) {
        od = 0U;
    }
    else if (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) {
        od = DLL_EVENTS_MEMORY_READ(&ctx->events, addr);
    }
    else {
        return false;
    }
    dll_isr_send_payload(ctx, &od, 1U, dll_isr_status_flags(ctx));
    dll_isr_touch_activity(ctx);
    return true;
}

/** @brief Type-0 页写 addr 0：Fallback 与 MasterCommand，仅 CKS 的确认。 */
static bool dll_isr_type0_master_command(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len,
                                         uint8_t channel, uint8_t addr)
{
    if (((ctx->state != IOLINK_DLL_STATE_PREOPERATE) && (ctx->state != IOLINK_DLL_STATE_OPERATE)) ||
        (len != 3U) || (channel != IOLINK_MC_CHANNEL_PAGE) || (addr != 0U)) {
        return false;
    }
    if (frame[2] == IOLINK_CMD_FALLBACK) {
        /* 没有唤醒检测的 PHY 无法从 SIO 返回，因此
         * 对该不支持的转换只做确认，不改变 DL 模式。 */
        if (dll_phy_has_wakeup(ctx)) {
            /* 64 位 T_FBD 截止时间归主泵所有：ISR 只请求
             * 它，主泵根据当前 MinCycleTime 装载。 */
            ctx->isr_fallback_pending = 1U;
            dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
        }
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        dll_isr_touch_activity(ctx);
        return true;
    }
    if (dll_apply_master_command(ctx, frame[2])) {
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        dll_isr_touch_activity(ctx);
        return true;
    }
    return false;
}

static bool dll_isr_handle_type0(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len,
                                 bool staged_write)
{
    if ((frame == NULL) || (len < 2U) || !dll_phy_has_tx(ctx)) {
        return false;
    }
    if ((frame[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0) {
        return false;
    }

    const bool read = ((frame[0] & IOLINK_MC_RW_MASK) != 0U);
    const uint8_t channel = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    const uint8_t addr = (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK);
    const bool was_establishing =
        (ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) || (ctx->state == IOLINK_DLL_STATE_STARTUP);

    if (was_establishing) {
        return dll_isr_type0_startup_probe(ctx, read, channel, addr, len);
    }
    if ((ctx->state != IOLINK_DLL_STATE_PREOPERATE) && (ctx->state != IOLINK_DLL_STATE_OPERATE)) {
        return false;
    }
    if (dll_isr_type0_transition(ctx, frame, len)) {
        return true;
    }
    if (read) {
        return dll_isr_type0_read(ctx, channel, addr);
    }
    if (dll_isr_type0_master_command(ctx, frame, len, channel, addr)) {
        return true;
    }

    /* ISDU 写：ISR 已缓冲该请求八位组。 */
    if ((channel == IOLINK_MC_CHANNEL_ISDU) && staged_write) {
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        dll_isr_touch_activity(ctx);
        return true;
    }
    return false;
}

/* 直接在 ISR 中缓冲 ISDU 写请求。这里只执行传输
 * （字节收集 + FlowCTRL + CHKPDU 检查）；应用
 * 分发仍位于 iolink_isdu_process()（主窗口），与 ST 的
 * StackBaseOdWrite 与 StackISDUProcess 拆分方式一致。当某个段
 * 已暂存到环中时返回 true。 */
static bool dll_isr_consume_isdu_write(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((frame == NULL) || (len < 2U)) {
        return false;
    }
    if ((frame[0] & IOLINK_MC_COMM_CHANNEL_MASK) != IOLINK_MC_CHANNEL_ISDU) {
        return false;
    }
    if ((frame[0] & IOLINK_MC_RW_MASK) != 0U) {
        return false; /* 读 */
    }
    uint8_t od_off;
    uint8_t od_n;
    if ((frame[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0) {
        od_off = 2U; /* Type-0 写：MC + CKT + OD */
        od_n = 1U;
    }
    else {
        od_off = (uint8_t) (2U + ctx->pd_out_len_current);
        od_n = ctx->od_width;
    }
    if (((uint16_t) od_off + (uint16_t) od_n) > (uint16_t) len) {
        return false;
    }
    if (od_n > IOLINK_OD_MAX_SIZE) {
        od_n = (uint8_t) IOLINK_OD_MAX_SIZE;
    }
    /* 把写暂存给主泵，而不是从 ISR 触碰 ctx->isdu：
     * 这样 ctx->isdu 只有一个写入者（主泵），ISDU 状态
     * 机不再需要在关闭中断的情况下运行。两个环槽位
     * 可吸收在排空之前到达的第二个段；第三个会被
     * 丢弃并计数（IO-Link 段由主站节拍控制）。 */
    iolink_od_rec_t* record = (iolink_od_rec_t*) iolink_ring_claim(&ctx->isr_od_ring);
    if (record == NULL) {
        ctx->isr_od_overruns++;
        return false;
    }
    record->flowctrl = (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK);
    record->len = od_n;
    for (uint8_t i = 0U; i < od_n; i++) {
        record->data[i] = frame[od_off + i];
    }
    iolink_ring_publish(&ctx->isr_od_ring);
    return true;
}

/**
 * @brief 为捕获的缓冲区填充回复 OD。
 *
 * 读操作来自 ISDU 快照、直接参数页或事件
 * 存储器；写回复只需要 PD-in 尾部。对于 ISR 不得应答的通道
 * 返回 false（由主泵负责）。
 */
static bool dll_isr_build_reply_od(iolink_dll_ctx_t* ctx, uint8_t* buf, bool read, uint8_t channel,
                                   uint8_t flowctrl)
{
    if (read) {
        if (channel == IOLINK_MC_CHANNEL_ISDU) {
            uint8_t od[IOLINK_OD_MAX_SIZE];
            dll_isr_isdu_od(ctx, flowctrl, od, ctx->od_width);
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = od[i];
            }
        }
        else if (channel == IOLINK_MC_CHANNEL_PROCESS) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = 0x00U;
            }
        }
        else if (channel == IOLINK_MC_CHANNEL_PAGE) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = iolink_isdu_direct_param_page1_octet(&ctx->isdu, (uint8_t) (flowctrl + i));
            }
        }
        else if (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = DLL_EVENTS_MEMORY_READ(&ctx->events, (uint8_t) (flowctrl + i));
            }
        }
        else {
            return false;
        }
    }
    else if ((channel != IOLINK_MC_CHANNEL_ISDU) && (channel != IOLINK_MC_CHANNEL_PROCESS) &&
             (channel != IOLINK_MC_CHANNEL_PAGE) && (channel != IOLINK_MC_CHANNEL_DIAGNOSIS)) {
        return false;
    }
    return true;
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/** @brief TX FRAME：就地组装回复并交给 PHY。 */
static bool dll_isr_start_reply_frame(iolink_dll_ctx_t* ctx, uint8_t* base, uint16_t out_len,
                                      uint8_t flags)
{
    ctx->isr_tx_len = (uint8_t) out_len;
    ctx->isr_tx_active = 0U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    /* 对于 1-2 个八位组的回复，DMA 几乎立即读取 CKS（TDR
     * 在帧开始时就清空）。只有当 CKS 之前至少有两个八位组时（out_len >= 3），
     * 在 tx_frame() 之后计算 CKS 才安全：此时 DMA 必须
     * 先移出第一个八位组（一个完整八位组时间，按波特率缩放），然后
     * 才读取校验和。忙的 PHY（上一个回复仍在总线上）
     * 会拒绝传输而不是破坏它；随后由主泵
     * 负责该帧。 */
    if (out_len >= 3U) {
        int tx_rc = -1;
        if (ctx->phy->tx_frame != NULL) {
            tx_rc = ctx->phy->tx_frame(ctx->phy->user, base, out_len);
        }
        if (tx_rc != 0) {
            return false;
        }
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        base[out_len - 1U] = (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
    }
    else {
        /* 短回复：在启动 DMA 之前填入 CKS。 */
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        base[out_len - 1U] = (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
        if ((ctx->phy->tx_frame == NULL) ||
            (ctx->phy->tx_frame(ctx->phy->user, base, out_len) != 0)) {
            return false;
        }
    }
    return true;
}
#else
/** @brief TX BYTE：暂存回复；CKS 在发送过程中计算。 */
static bool dll_isr_start_reply_byte(iolink_dll_ctx_t* ctx, uint8_t* base, uint16_t out_len,
                                     uint8_t flags)
{
    ctx->isr_tx_base = base;
    ctx->isr_tx_len = (uint8_t) (out_len - 1U);
    ctx->isr_tx_flags = flags;
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_acc = 0x52U;
    ctx->isr_tx_has_cks = 1U;
    ctx->isr_tx_active = 1U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    dll_isr_tx_step(ctx);
    return true;
}
#endif

/* 选择与刚成帧的请求匹配的预暂存 OPERATE 回复负载，
 * 并从 ISR 开始发送它。TX BYTE：CKS 在
 * 八位组发送过程中计算（每次收发器请求一个八位组）。TX FRAME：负载及其
 * CKS 在暂存缓冲区中组装，并在一次调用中移交。Type-0
 * 请求没有预暂存变体；由主泵应答。 */
static bool dll_isr_start_reply(iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    if ((frame == NULL) || !dll_phy_has_tx(ctx)) {
        return false;
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 报告线路状态的 PHY 表示上一个回复可能仍在
     * 总线上：让主泵应答该帧，而不是破坏它。 */
    if ((ctx->phy->tx_busy != NULL) && (ctx->phy->tx_busy(ctx->phy->user) != 0U)) {
        return false;
    }
#endif
    const uint8_t first = frame[0];
    if (first == IOLINK_MC_TRANSITION_COMMAND) {
        return false; /* 在 OPERATE 中无效；由主泵处理 */
    }
    const bool read = ((first & IOLINK_MC_RW_MASK) != 0U);
    const uint8_t channel = (uint8_t) (first & IOLINK_MC_COMM_CHANNEL_MASK);
    const uint8_t flowctrl = (uint8_t) (first & IOLINK_MC_ADDR_MASK);

    /* 零拷贝回复：PD-in 已经位于活动回复缓冲区中
     * （主泵填充空闲副本并以原子方式发布）。
     * 布局：[OD x od_width][PD-in][CKS]。只捕获一次索引，这样主泵
     * 在下一次回复之前永远不会把目标设为该缓冲区。 */
    const uint8_t captured = ctx->pd_in_active;
    uint8_t* buf = ctx->pd_in[captured];
    if (!dll_isr_build_reply_od(ctx, buf, read, channel, flowctrl)) {
        return false;
    }

    /* 读：[OD][PD-in]CKS 来自 buf。写：[PD-in]CKS 来自 buf+od_width（
     * PD-in 始终存储在配置的 OD 头部之后）。 */
    uint8_t* base = read ? buf : (buf + ctx->od_width);
    const uint16_t total =
        (uint16_t) (((uint16_t) ctx->od_width) + ((uint16_t) ctx->pd_in_len) + 1U);
    const uint16_t out_len = read ? total : (uint16_t) (total - (uint16_t) ctx->od_width);
    if (out_len > (uint16_t) IOLINK_PD_REPLY_MAX) {
        return false;
    }
    /* 仅当确认回复有效后才发布捕获的缓冲区。 */
    ctx->pd_in_captured = captured;
    const uint8_t flags = dll_isr_status_flags(ctx);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    return dll_isr_start_reply_frame(ctx, base, out_len, flags);
#else
    return dll_isr_start_reply_byte(ctx, base, out_len, flags);
#endif
}

/** @brief 对 ISDU 写请求返回 true（其 OD 由引擎消费）。 */
static bool dll_isr_is_isdu_write(const uint8_t* frame)
{
    return ((frame[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) &&
           ((frame[0] & IOLINK_MC_RW_MASK) == 0U);
}

/**
 * @brief 判断 ISR 是否必须自己应答该请求。
 *
 * 预缓存的（TYPE_2_V）回复由 ISR 为过程
 * 通道、ISDU 写 ACK 和任何 ISDU 读提供应答。Type-0 帧及其他
 * 类型/通道由主泵在其自身校验之后应答。损坏或非法的帧
 * 绝不会被即时应答（调用方会拒绝它）。
 */
static bool dll_isr_wants_eager(const iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    const bool type12 = ((frame[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0);
    const uint8_t channel = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    const bool isdu = (channel == IOLINK_MC_CHANNEL_ISDU);
    const bool isdu_write = isdu && ((frame[0] & IOLINK_MC_RW_MASK) == 0U);
    /* 每个 ISDU 读都由 ISR 应答，ISR 拥有响应段
     * 索引，并从主泵在服务完成时刷新的
     * 快照中提供数据。这使 t_A 保持在 10 个位时间内（无需主循环
     * 往返）。 */
    const bool isdu_read = isdu && ((frame[0] & IOLINK_MC_RW_MASK) != 0U);
    return type12 && dll_phy_has_tx(ctx) &&
           ((channel == IOLINK_MC_CHANNEL_PROCESS) || (channel == IOLINK_MC_CHANNEL_PAGE) ||
            (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) || isdu_write || isdu_read);
}

/**
 * @brief 两种 RX 粒度共享的帧完成路径。
 *
 * 消费 ISDU 写、运行 Type-0 快速路径并决定即时
 * 回复。回复在邮箱复制之前启动，因此第一个回复八位组
 * 绝不会等待发布（t_A），随后记录会带上回复标志
 * 与八位组。
 */
static inline void dll_isr_complete_frame(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len,
                                          bool valid)
{
    bool staged = false;
    IOLINK_LOG_ISR_DEBUG("frame complete: len=%u valid=%u", (unsigned) len, (unsigned) valid);
    if (dll_isr_is_isdu_write(frame)) {
        /* 只缓冲有效请求：损坏的 ISDU 写绝不能到达
         * 传输层（ST 在 StackBaseOdWrite 之前校验）。 */
        if (!valid) {
            ctx->isr_eager_rejects++;
            return;
        }
        staged = dll_isr_consume_isdu_write(ctx, frame, len);
    }
    if (valid && dll_isr_handle_type0(ctx, frame, len, staged)) {
        dll_mark_master_activity(ctx); /* 本周期由 ISR 处理 */
        return;
    }

    const bool eager = dll_isr_wants_eager(ctx, frame);
    if (eager && !valid) {
        ctx->isr_eager_rejects++; /* 主泵校验/回退 */
    }
    /* 在邮箱复制之前启动即时回复：第一个回复八位组绝不能
     * 等待发布，且邮箱满绝不能抑制该回复。
     * 负载由头部和 pd_in 同步构建。 */
    uint8_t replied = 0U;
    if (eager && valid) {
        if (dll_isr_start_reply(ctx, frame)) {
            replied = 1U;
        }
    }
    if ((valid != 0U) && (replied == 0U)) {
        /* ISR 不应答该帧（由主泵负责，或该
         * 交换被有意设为无应答）：13 位时间看门狗在主上下文
         * 中总会超时，因此在此关闭它，而不是
         * 为一个仍将到来的回复计入超时。 */
        if (ctx->phy->response_timer_stop != NULL) {
            ctx->phy->response_timer_stop(ctx->phy->user);
        }
    }
    /* 即使即时校验失败也发布：无效帧的错误/回退
     * 统计由主泵负责。 */
    (void) dll_isr_publish_frame(ctx, frame, len, replied);
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/** @brief PHY 交付帧的长度合理性检查（计入成帧错误）。 */
static bool dll_isr_frame_len_ok(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((len < 2U) || (len > IOLINK_M_SEQ_MAX_LEN)) {
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        return false;
    }
    /* Type-0 M-sequence 从不携带过程数据（OD 写恰好携带
     * 一个 OD 八位组，Figure A.5），因此其长度不遵循
     * OPERATE 布局。与 dll_isr_rx_byte_derive_expect() 保持一致；否则
     * OPERATE 中的 Type-0 页读（复位请求）会按配置的 PD 长度
     * 衡量，被计为成帧错误并丢弃，而不是
     * 复位设备。 */
    uint8_t expected_len;
    if ((frame[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0) {
        const bool write = ((frame[0] & IOLINK_MC_RW_MASK) == 0U);
        const uint8_t channel = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
        const bool od_write = write && ((channel == IOLINK_MC_CHANNEL_PAGE) ||
                                        (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                                        (channel == IOLINK_MC_CHANNEL_ISDU));
        expected_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + (od_write ? 1U : 0U));
    }
    else {
        expected_len = iolink_dll_expected_req_len(ctx, frame[0]);
    }
    if ((expected_len == 0U) || (len != expected_len)) {
        IOLINK_LOG_ISR_DEBUG("frame length reject: len=%u expected=%u", (unsigned) len,
                             (unsigned) expected_len);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        return false;
    }
    return true;
}

/** @brief RX FRAME：取一个 PHY 交付的帧并交给共享尾部。 */
static void dll_isr_drain_rx_frame(iolink_dll_ctx_t* ctx)
{
    /* 即使邮箱已满也要消费 PHY 帧：即时
     * 回复仍须发出（与 RX BYTE 路径相同），此时发布只会
     * 计入丢弃，而不是静默丢失该帧。八位组在
     * ISR 返回之前复制，因此 PHY 可以复用其缓冲区。 */
    uint8_t status = 0U;
    uint8_t len = 0U;
    const uint8_t* frame = ctx->phy->rx_frame(ctx->phy->user, &status, &len);
    (void) status;
    if (frame == NULL) {
        return;
    }
    if (!dll_isr_frame_len_ok(ctx, frame, len)) {
        return;
    }
    const bool valid = dll_isr_frame_valid(ctx, frame, len);
    if (valid) {
        /* 校验通过的帧证明主站在轮询。主泵可能
         * 很忙（例如慢速 ISDU 服务），无法及时消费邮箱；
         * 没有这一点，主站丢失监控会把一个繁忙的
         * 主窗口计为丢失。 */
        ctx->isr_master_activity = 1U;
        if (ctx->phy->response_timer_start != NULL) {
            /* 仅为将被应答的请求装载 t_A 看门狗：
             * 被拒绝或虚假的帧不得计为 t_A 超时。 */
            ctx->phy->response_timer_start(ctx->phy->user);
        }
    }
    dll_isr_complete_frame(ctx, frame, len, valid);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/** @brief 丢弃正在组装的帧（不触碰 TX 引擎）。 */
static void dll_isr_rx_byte_reset(iolink_dll_ctx_t* ctx)
{
    ctx->isr_rx_cnt = 0U;
    ctx->isr_rx_expect = 0U;
}

/**
 * @brief RX BYTE：为当前缓冲区处理 CHK/gap 帧结束（Timer A）。
 *
 * 间隙是超时，而不是正常帧结束。它只能结束一个短
 * M-sequence（少于两个头部八位组）或被截断的序列：字节
 * 引擎在预期长度到达时就会消费帧，因此长度完整的帧
 * 绝不会在此结束。两种情况都会被丢弃；只有真正的
 * 截断才计为成帧错误。
 */
static void dll_isr_rx_byte_gap(iolink_dll_ctx_t* ctx)
{
    const bool incomplete_ms = (ctx->isr_rx_expect != 0U) &&
                               (ctx->isr_rx_cnt >= IOLINK_M_SEQ_HEADER_LEN) &&
                               (ctx->isr_rx_cnt < ctx->isr_rx_expect);
    if (incomplete_ms) {
        IOLINK_LOG_ISR_DEBUG("incomplete frame: got %u of %u bytes", (unsigned) ctx->isr_rx_cnt,
                             (unsigned) ctx->isr_rx_expect);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
    }
    dll_isr_rx_byte_reset(ctx);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    /* 释放收发器的接收引擎，除非有回复正在传输
     * （TX 排空会调用 tx_done）。在 TX FRAME 构建中没有 tx_done：
     * PHY 拥有两端传输并自行释放接收器。 */
    if ((ctx->isr_tx_active == 0U) && (ctx->phy->tx_done != NULL)) {
        ctx->phy->tx_done(ctx->phy->user);
    }
#endif
}

/** @brief RX BYTE：写入校验和种子并追加一个接收到的八位组。 */
static void dll_isr_rx_byte_accumulate(iolink_dll_ctx_t* ctx, uint8_t data)
{
    if ((ctx->isr_rx_cnt == 0U) && (ctx->phy->response_timer_start != NULL)) {
        ctx->phy->response_timer_start(ctx->phy->user); /* 第一个八位组 -> Timer A */
    }
    if (ctx->isr_rx_cnt == 0U) {
        ctx->isr_rx_acc = 0x52U; /* A.1.6 种子（帧起始） */
    }
    if (ctx->isr_rx_cnt < sizeof(ctx->isr_rx_buf)) {
        uint8_t octet = data;
        if (ctx->isr_rx_cnt == 1U) {
            octet = (uint8_t) (octet & IOLINK_MSEQ_TYPE_MASK); /* 仅 CKT 类型位 */
        }
        ctx->isr_rx_acc = (uint8_t) (ctx->isr_rx_acc ^ octet);
        ctx->isr_rx_buf[ctx->isr_rx_cnt] = data;
        ctx->isr_rx_cnt++;
    }
}

/**
 * @brief RX BYTE：头部完整后推导预期请求长度。
 *
 * CKT 类型位标识 M-sequence 类型。Type-0 没有长度字段，但
 * 其大小由 MC/CKT 固定（读 2，OD 写 3）；13 Tbit 间隙只是
 * 超时，绝不是正常结束。
 */
static void dll_isr_rx_byte_derive_expect(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_rx_cnt != 2U) {
        return;
    }
    const uint8_t mc = (uint8_t) ctx->isr_rx_buf[0];
    const uint8_t ckt = (uint8_t) ctx->isr_rx_buf[1];
    if ((ckt & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0) {
        const bool wr = ((mc & IOLINK_MC_RW_MASK) == 0U);
        const uint8_t channel = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);
        ctx->isr_rx_expect = (wr && ((channel == IOLINK_MC_CHANNEL_PAGE) ||
                                     (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                                     (channel == IOLINK_MC_CHANNEL_ISDU)))
                                 ? 3U
                                 : 2U;
    }
    else {
        ctx->isr_rx_expect = iolink_dll_expected_req_len(ctx, mc);
    }
}

/** @brief RX BYTE：已知长度已完整；将帧交给共享尾部。 */
static void dll_isr_rx_byte_complete(iolink_dll_ctx_t* ctx)
{
    /* 长度完整的 M-sequence 证明主站在轮询；主泵
     * 可能在其监控窗口结束前无法消费邮箱（参见
     * RX FRAME 分支）。 */
    ctx->isr_master_activity = 1U;
    const bool valid =
        dll_isr_frame_valid_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect, ctx->isr_rx_acc);
    dll_isr_complete_frame(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect, valid);
    dll_isr_rx_byte_reset(ctx);
}

/** @brief RX BYTE：消费一个接收到的八位组。当排空应停止时返回 true。 */
static bool dll_isr_drain_rx_byte(iolink_dll_ctx_t* ctx)
{
    uint8_t data = 0U;
    uint8_t status = 0U;
    const int ok = ctx->phy->rx_octet(ctx->phy->user, &data, &status);

    if ((status & IOLINK_PHY_STATUS_CHK) != 0U) {
        dll_isr_rx_byte_gap(ctx);
        return true;
    }
    if (ok < 0) {
        /* 传输错误：丢弃部分帧并计数。 */
        IOLINK_LOG_ISR_WARN("rx_octet error: %d", ok);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        dll_isr_rx_byte_reset(ctx);
        return true;
    }
    if (ok == 0) {
        return true;
    }
    dll_isr_rx_byte_accumulate(ctx, data);
    dll_isr_rx_byte_derive_expect(ctx);
    if ((ctx->isr_rx_expect != 0U) && (ctx->isr_rx_cnt >= ctx->isr_rx_expect)) {
        dll_isr_rx_byte_complete(ctx);
        return true;
    }
    return false;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/**
 * @brief 排空一个背靠背突发，设有上限，使卡死的线路无法无限自旋。
 */
static void dll_isr_drain(iolink_dll_ctx_t* ctx)
{
    for (uint32_t guard = 0U; (guard < 64U) && (ctx->phy->int_active(ctx->phy->user) != 0U);
         guard++) {
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        /* 回复排空（TX BYTE）：收发器请求下一个回复八位组。
         * 只喂一个并让出（下一个请求作为另一个 IRQ 到达）。 */
        if (ctx->isr_tx_active != 0U) {
            dll_isr_tx_step(ctx);
            break;
        }
#endif
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
        dll_isr_drain_rx_frame(ctx);
        break;
#else
        if (dll_isr_drain_rx_byte(ctx)) {
            break;
        }
#endif
    }
}

/** @brief 主上下文处理器暂存了一个逐字回复；现在发送它。 */
static void dll_isr_tx_pending(iolink_dll_ctx_t* ctx)
{
    ctx->isr_tx_pending = 0U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    dll_isr_start_staged_tx(ctx, ctx->isr_tx_pending_slot, ctx->isr_tx_len);
}

static void dll_isr_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || !dll_phy_has_rx(ctx) || (ctx->phy->int_active == NULL)) {
        return;
    }

    /* 在 int_active 门控之前检查：软件请求的服务没有
     * 自身的收发器 RX/TX 活动可报告。 */
    if (ctx->isr_tx_pending != 0U) {
        dll_isr_tx_pending(ctx);
        return;
    }

    /* 电平复查：数据中断可能锁存串扰边沿；在零
     * 成本下退出，除非收发器确实在请求服务。 */
    if (ctx->phy->int_active(ctx->phy->user) == 0U) {
        return;
    }

    dll_isr_drain(ctx);
}

void iolink_dll_process_isr(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->in_isr != 0U) {
        /* 更高优先级的中断在一个较低优先级中断
         * 已在运行泵时调用了该泵（在没有 BASEPRI 的板子上
         * 该泵绑定到多个向量）。泵状态只有一个写入者，
         * 因此嵌套会破坏回复暂存和邮箱：推迟处理，
         * 让外层调用在返回前再运行一次。 */
        ctx->isr_reentry_pending = 1U;
        return;
    }
    ctx->in_isr = 1U;
    do {
        ctx->isr_reentry_pending = 0U;
        dll_isr_process(ctx);
    } while (ctx->isr_reentry_pending != 0U);
    ctx->in_isr = 0U;
}

void iolink_dll_on_rx_timeout(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->isr_master_activity = 1U;
    IOLINK_LOG_ISR_DEBUG("rx timeout (t_A)");
    IOLINK_STAT_ISR(ctx, timeout_errors)++;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    ctx->isr_rx_cnt = 0U;
    ctx->isr_rx_expect = 0U;
#endif
    if ((ctx->phy != NULL) && (ctx->phy->response_timer_stop != NULL)) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
}

void iolink_dll_on_stack_tick(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->stack_tick_pending = 1U;
}

uint8_t iolink_dll_take_service_window(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return 0U;
    }
    if (ctx->phy->stack_timer_start == NULL) {
        return 1U; /* 无硬件窗口：传统自由运行主循环 */
    }
    if (ctx->stack_tick_pending == 0U) {
        return 0U;
    }
    ctx->stack_tick_pending = 0U;
    return 1U;
}
