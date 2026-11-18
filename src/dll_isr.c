/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/internal/dll_isr.h"
#include "iolinki/crc.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include "iolinki/port.h"
#include "dll_internal.h"
#include <string.h>
#include "dll_isr_internal.h"

/**
 * @file dll_isr.c
 * @brief 数据链路层 ISR 双泵传输。
 * @ingroup iolinki_dll
 *
 * 从数据中断运行：请求成帧、应答计划执行、ISDU
 * 段索引以及分阶段 TX 泵。主泵与 M-sequence 内容
 * 分发位于 dll_frame.c / dll_sm.c；应用回调绝不能在此运行。
 *
 * 本文件命名约定：dll_isr_* 仅可在 ISR 上下文中调用；
 * 普通 dll_* 辅助函数与主泵共享（上下文约定表见
 * docs/CODE_FLOW.md 第 12.1 节）。例外：SINGLE 档下
 * dll_isr_start_staged_tx() 也由主泵的 dll_send() 调用（该档没有数据 ISR）。
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
 * 接收组装与帧完成决策位于 dll_isr_rx.c：
 *
 * dll_isr_complete_frame（两种 RX 粒度共用的每帧决策点，
 * 按以下顺序执行）：
 *  1. ISDU 写：dll_isr_consume_isdu_write 直接调用
 *     iolink_isdu_od_write（按 FlowCTRL 位置去重累计），并自增
 *     isr_isdu_write_gen；CHKPDU/解析/执行由主泵完成。
 *  2. 应答计划：dll_isr_plan_lookup 在 dll_reply.c 发布的计划表中查找
 *     命中项（只读，表在 ISR 运行期间不改变）；dll_isr_plan_execute 按计划执行
 *     动作 -> 取源 -> 追加 CKS -> 喂字节。
 *  3. 合法帧计划未命中：不应答、不解释内容，关闭 t_A 看门狗并直接
 *     丢弃（计划表是"哪些请求可被服务"的唯一出处）。
 *  4. 计划命中（CONSUME）的帧不再发布到邮箱，只在
 *     shared.isr_consumed_us/isr_consumed_seq 上交接帧结束时间戳，
 *     由主泵补做 T_CYCLE/T_REN 记账与 last_response_us 更新。
 *  5. 校验失败的帧以 eager=0 发布到邮箱，由主泵完成错误/回退处理。
 *
 * 交接与顺序约束：
 *  - 即时回复在邮箱发布*之前*启动（t_A）；记录随后
 *    连同其八位组一起携带 eager=1。
 *  - ctx->shared.isdu 的写入口（累计槽、`req_idx`/`req_total`、FlowCTRL
 *    水位、`stage_state` 与发布字 `req_pub`）**整体归数据 ISR**：主泵只读
 *    发布字、认领槽并写 `req_ack_gen`/`req_busy_sel`，从不改写累计相位，
 *    因此接管不需要临界区。已发布的读视图与其写序号仍由两边按序号协议
 *    交接。
 *  - 应答计划表同样归主泵所有：ISR 只读，绝不修改。
 *  - 64 位截止时间与 last_response_us 归主泵所有：ISR 只设置一次性
 *    标志或写 32 位时间戳，主泵用无符号差值重建 64 位绝对时间。
 */

/**
 * @brief 开始发送已暂存的回复槽位。
 *
 * 由 SINGLE 档的 dll_send()（没有数据 ISR，主上下文自己发送）
 * 与 ISR 待回复路径共享，因此两者以相同方式驱动 TX 钩子。
 *
 * @p len / @p has_cks 写入 ISR 独占的流式状态；BYTE 传输此后的每一步
 * 都只读该副本，主上下文的暂存元数据与在途帧无关。
 */
bool dll_isr_reset(iolink_dll_isr_t* isr)
{
    return iolink_ctx_zero(isr, sizeof(iolink_dll_isr_t));
}

void dll_isr_start_staged_tx(iolink_dll_main_t* ctx, uint8_t slot, uint8_t len, uint8_t has_cks)
{
    if (ctx->shared.isr == NULL) {
        return; /* 尚未 iolink_dll_init()：没有可驱动的 ISR 状态 */
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    (void) has_cks; /* 整帧交付：CKS 已在暂存字节里 */
    if (ctx->shared.phy->tx_frame != NULL) {
        if (ctx->shared.phy->tx_frame(ctx->shared.phy->self, ctx->shared.isr_tx_payload[slot],
                                      len) != 0) {
            IOLINK_LOG_ISR_WARN(&ctx->shared.log, "tx_frame failed: len=%u", (unsigned) len);
            /* 由 SINGLE 档主泵与 ISR 泵共享：将丢弃归属到
             * 正在运行的上下文，使 SPLIT 计数器保持精确。 */
            if (ctx->shared.in_isr != 0U) {
                IOLINK_STAT_ISR(ctx, isr_tx_dropped)++;
            }
            else {
                ctx->shared.isr_tx_dropped++;
            }
        }
    }
    else {
        /* 本构建按 TX FRAME 交付，PHY 却没有 tx_frame()：能力位不会置 TX，
         * 设备层校验因此看不见这种拼装错误。计数丢弃，使配置错误可观测。 */
        if (ctx->shared.in_isr != 0U) {
            IOLINK_STAT_ISR(ctx, isr_tx_dropped)++;
        }
        else {
            ctx->shared.isr_tx_dropped++;
        }
    }
#else
    if (ctx->shared.phy->tx_octet != NULL) {
        ctx->shared.isr->isr_tx_base = ctx->shared.isr_tx_payload[slot];
        ctx->shared.isr->isr_tx_slot = slot;
        ctx->shared.isr_tx_idx = 0U;
        ctx->shared.isr->tx_stream_len = len;
        ctx->shared.isr->tx_stream_has_cks = (has_cks != 0U) ? 1U : 0U;
        ctx->shared.isr_tx_active = 1U;
        dll_isr_tx_step(ctx);
    }
    else {
        /* TX BYTE 构建 + 只有 tx_frame() 的 PHY：同上，计数而不是静默丢弃。 */
        if (ctx->shared.in_isr != 0U) {
            IOLINK_STAT_ISR(ctx, isr_tx_dropped)++;
        }
        else {
            ctx->shared.isr_tx_dropped++;
        }
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
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
/* 发送活动回复的一步：下一个负载八位组，然后是计算出的
 * CKS（CKS 模式），最后是帧结束。每次收发器请求调用一次。 */
void dll_isr_tx_step(iolink_dll_main_t* ctx)
{
    const uint8_t len = ctx->shared.isr->tx_stream_len;
    const uint8_t has_cks = ctx->shared.isr->tx_stream_has_cks;

    if (ctx->shared.isr_tx_idx < len) {
        const uint8_t octet = ctx->shared.isr->isr_tx_base[ctx->shared.isr_tx_idx];
        (void) ctx->shared.phy->tx_octet(ctx->shared.phy->self, octet);
        if (has_cks != 0U) {
            ctx->shared.isr->isr_tx_acc = (uint8_t) (ctx->shared.isr->isr_tx_acc ^ octet);
        }
        ctx->shared.isr_tx_idx++;
    }
    else if ((has_cks != 0U) && (ctx->shared.isr_tx_idx == len)) {
        /* 负载完成：输出 CKS = flags | checksum6(acc ^ flags)。 */
        const uint8_t cks =
            dll_reply_cks(ctx->shared.isr->isr_tx_acc, ctx->shared.isr->isr_tx_flags);
        (void) ctx->shared.phy->tx_octet(ctx->shared.phy->self, cks);
        ctx->shared.isr_tx_idx++;
    }
    else {
        if (ctx->shared.phy->tx_done != NULL) {
            ctx->shared.phy->tx_done(ctx->shared.phy->self);
        }
        if (ctx->shared.phy->stack_timer_start != NULL) {
            ctx->shared.phy->stack_timer_start(ctx->shared.phy->self); /* 帧结束 -> Timer B */
        }
        ctx->shared.isr_tx_active = 0U;
        /* TX BYTE：回复已完成，因此 ISR 租用的 PD-in 槽位
         * 再次对主泵可用。 */
        ctx->shared.pd_in_captured = 0xFFU;
    }
}
#endif /* IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE */

/* 从已发布的响应视图填充 ISDU 读回复的 OD 八位组。 */
static void dll_isr_isdu_od(iolink_dll_main_t* ctx, uint8_t flowctrl, uint8_t* od, uint8_t od_width)
{
    if (od_width == 0U) {
        return;
    }
    for (uint8_t i = 0U; i < od_width; i++) {
        od[i] = 0x00U;
    }

    /* IDLE / IDLE2 / ABORT / 保留（0x11..0x1F）：无服务，并请求主泵
     * 复位读传输层。这条路径不受下面的"写失效"门控影响：中止/空闲读
     * 总是应当复位传输层。 */
    if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        od[0] = 0x00U;
        ctx->shared.isr_isdu_read_reset = 1U;
        /* 在读传输层被主泵复位之前，主站不得继续旧段序列：清掉段索引，
         * 使后续 COUNT 只能得到"无服务"，直到新的 START 重新采纳视图。 */
        ctx->shared.isr_isdu_last_flowctrl = 0xFFU;
        ctx->shared.isr->isr_isdu_idx = 0U;
        ctx->shared.isr->isr_isdu_seg_idx = 0U;
        return;
    }

    /* 失效协议：写序号与主泵的已应用序号不等，说明 ISR 已接受一个写
     * START/ABORT，而主泵尚未应用（或中止）它。零填充上一步已给出 0x00
     * （无服务），主站会重试。读序：先读本上下文写入的序号，再读主泵的
     * applied，相等时才读发布字。主泵先发布新视图、再追平 applied，因此
     * 两者相等时读到的必是已确认视图；不等时最多多拒一次，绝不放行
     * 写之前的旧视图。 */
    const IOLINK_ATOMIC_UINT_T write_gen = ctx->shared.isr_isdu_write_gen;
    const uint32_t write_applied = ctx->shared.isdu_write_gen_applied;
    if (write_gen != write_applied) {
        return;
    }
    IOLINK_FENCE();

    /* 一次原子加载得到一致的 {length, busy, buffer} 元组。 */
    const IOLINK_ATOMIC_UINT_T pub = iolink_atomic_load(&ctx->shared.isr_isdu_pub);
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
        ctx->shared.isr->isr_isdu_adopted = pub;
        ctx->shared.isr->isr_isdu_len = IOLINK_ISDU_PUB_LEN(pub);
        ctx->shared.isr->isr_isdu_sel = IOLINK_ISDU_PUB_SEL(pub);
        ctx->shared.isr->isr_isdu_idx = 0U;
        ctx->shared.isr->isr_isdu_seg_idx = 0U;
        ctx->shared.isr_isdu_last_flowctrl = IOLINK_FLOWCTRL_START;
    }
    else {
        /* COUNT（0x00..0x0F）。已发布视图发生变化（新响应或 Busy
         * 转换）会使段序列失效；主站必须
         * 以 START 重新开始。重复的 COUNT 重复前一个段；
         * 任何其他值必须是 +1。 */
        if ((ctx->shared.isr->isr_isdu_adopted != pub) ||
            (ctx->shared.isr_isdu_last_flowctrl == 0xFFU)) {
            od[0] = 0x00U; /* 无服务 */
            ctx->shared.isr_isdu_last_flowctrl = 0xFFU;
            return;
        }
        if (flowctrl == ctx->shared.isr_isdu_last_flowctrl) {
            repeat = true;
        }
        else {
            const uint8_t expected =
                (uint8_t) ((ctx->shared.isr_isdu_last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
            if (flowctrl != expected) {
                od[0] = 0x00U; /* 分段错误 -> 无服务 */
                ctx->shared.isr_isdu_last_flowctrl = 0xFFU;
                return;
            }
            ctx->shared.isr_isdu_last_flowctrl = flowctrl;
        }
    }

    /* START（就绪）、新的 COUNT 或重复：处理一个 od_width 段。
     * 重复会重新发送前一个段而不推进索引。 */
    uint8_t idx = repeat ? ctx->shared.isr->isr_isdu_seg_idx : ctx->shared.isr->isr_isdu_idx;
    const uint8_t len = ctx->shared.isr->isr_isdu_len;
    const uint8_t* resp = ctx->shared.isdu.resp_store[ctx->shared.isr->isr_isdu_sel];
    uint8_t count = 0U;
    while ((count < od_width) && (idx < len)) {
        od[count++] = resp[idx++];
    }
    if (!repeat) {
        ctx->shared.isr->isr_isdu_seg_idx = ctx->shared.isr->isr_isdu_idx;
        ctx->shared.isr->isr_isdu_idx = idx;
    }
}

/* CKS 状态标志：事件（bit7）+ PD 无效（bit6）。ISR 安全（普通读取）。
 *
 * @p pub 是本帧开始时加载的 PD-in 发布字：状态位与稍后捕获的
 * active 索引因此取自同一代，主上下文的发布不会让一条应答混用两代。 */
static uint8_t dll_isr_status_flags(const iolink_dll_main_t* ctx, IOLINK_ATOMIC_UINT_T pub)
{
    uint8_t flags = 0U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        flags |= 0x80U;
    }
    flags |= dll_pd_status_flags_at(ctx, pub);
    return flags;
}

/* ISR 总是使用自己的暂存槽位（IOLINK_TX_ISR_SLOT），因此主泵
 * 可以继续在 0/1 之间交替，而不与中断上下文共享计数器。 */

/* 从 ISR 发送一个短应答（负载 + CKS），由计划执行器的
 * 短应答路径使用。负载缓冲区是库自己的暂存缓冲区，因此在 TX FRAME 传输
 * 期间保持有效。校验和以
 * flags | checksum6(acc ^ flags) 的形式追加到同一缓冲区。 */
static void dll_isr_send_staged(iolink_dll_main_t* ctx, uint8_t slot, uint8_t len, uint8_t flags)
{
    if (ctx->shared.phy->response_timer_stop != NULL) {
        ctx->shared.phy->response_timer_stop(ctx->shared.phy->self); /* 回复开始 -> 停止 t_A */
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 短回复：第一个八位组一移入
     * 移位寄存器，DMA 就会读取 CKS（帧开始时 TDR 立即清空），因此
     * CKS 必须在 tx_frame() 之前就在缓冲区中。只有长 PD 回复
     * 才有足够的距离（>= 28 个八位组）在 DMA 启动后计算它。 */
    uint8_t* payload = ctx->shared.isr_tx_payload[slot];
    uint8_t acc = 0x52U;
    for (uint8_t i = 0U; i < len; i++) {
        acc = (uint8_t) (acc ^ payload[i]);
    }
    payload[len] = dll_reply_cks(acc, flags);
    const uint8_t frame_len = (uint8_t) (len + 1U);
    ctx->shared.isr_tx_active = 0U;
    if (ctx->shared.phy->tx_frame != NULL) {
        if (ctx->shared.phy->tx_frame(ctx->shared.phy->self, payload, frame_len) != 0) {
            IOLINK_STAT_ISR(ctx, isr_tx_dropped)++; /* PHY 忙：回复丢失 */
        }
    }
#else
    ctx->shared.isr->isr_tx_base = ctx->shared.isr_tx_payload[slot];
    ctx->shared.isr->isr_tx_slot = slot;
    ctx->shared.isr->tx_stream_len = len;
    ctx->shared.isr->isr_tx_flags = flags;
    ctx->shared.isr_tx_idx = 0U;
    ctx->shared.isr->isr_tx_acc = 0x52U;
    ctx->shared.isr->tx_stream_has_cks = 1U;
    ctx->shared.isr_tx_active = 1U;
    dll_isr_tx_step(ctx);
#endif
}

/** @brief 帧已由 ISR 处理：刷新活动时间戳。 */
static void dll_isr_touch_activity(iolink_dll_main_t* ctx)
{
    ctx->shared.last_activity_ms = dll_time_ms(ctx);
    ctx->shared.last_byte_us = (uint32_t) dll_time_us(ctx);
}

/** @brief 对 ISDU 写请求返回 true（其 OD 由引擎消费）。 */
bool dll_isr_is_isdu_write(const uint8_t* frame)
{
    return ((frame[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) &&
           ((frame[0] & IOLINK_MC_RW_MASK) == 0U);
}

/* ISDU 写请求在数据 ISR 中直接累计（按 FlowCTRL 位置去重）。
 * FlowCTRL 序与字节累计归 ISR；CHKPDU 校验、请求解析与服务执行归
 * 主泵（iolink_isdu_process）。与 ST 的 StackBaseOdWrite /
 * StackISDUProcess 拆分一致。有效写段总是被消费（返回 true），
 * 因此写计划总是回 ACK；重发段覆盖同一位置，不消耗新缓冲。 */
bool dll_isr_consume_isdu_write(iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len)
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
    if (dll_ckt_is_type0(frame[1])) {
        od_off = 2U; /* Type-0 写：MC + CKT + OD */
        od_n = 1U;
    }
    else {
        od_off = (uint8_t) (2U + ctx->shared.pd_out_len_current);
        od_n = ctx->shared.od_width;
    }
    if (((uint16_t) od_off + (uint16_t) od_n) > (uint16_t) len) {
        return false;
    }
    if (od_n > IOLINK_OD_MAX_SIZE) {
        od_n = (uint8_t) IOLINK_OD_MAX_SIZE;
    }

    const uint8_t flowctrl = (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK);
    const bool write_phase =
        (flowctrl == IOLINK_FLOWCTRL_START) || (flowctrl == IOLINK_FLOWCTRL_ABORT);

    /* 选槽、覆盖计数与发布都在引擎内（iolink_isdu_od_write）：只有它
     * 知道两个请求槽里哪一条已被主泵接管。 */
    iolink_isdu_od_write(&ctx->shared.isdu, flowctrl, &frame[od_off], od_n);
    if (write_phase) {
        /* 失效协议：先发布写入口相位（START/ABORT），再用序号发布这次写；
         * 主泵读到新序号时必然已经看到新相位。在它应用（或中止）该写并
         * 发布新读视图之前，读路径因 gen != applied 一律回 0x00。序号只有
         * 本上下文自增，普通自增即可。 */
        IOLINK_FENCE();
        ctx->shared.isr_isdu_write_gen++;
    }
    return true;
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/** @brief TX FRAME：就地组装应答并交给 PHY。 */
static bool dll_isr_start_reply_frame(iolink_dll_main_t* ctx, uint8_t* base, uint16_t out_len,
                                      uint8_t flags)
{
    ctx->shared.isr_tx_active = 0U;
    if (ctx->shared.phy->response_timer_stop != NULL) {
        ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
    }
    /* 对于 1-2 个八位组的应答，DMA 几乎立即读取 CKS（TDR
     * 在帧开始时就清空）。只有当 CKS 之前至少有两个八位组时（out_len >= 3），
     * 在 tx_frame() 之后计算 CKS 才安全：此时 DMA 必须
     * 先移出第一个八位组（一个完整八位组时间，按波特率缩放），然后
     * 才读取校验和。忙的 PHY（上一个应答仍在总线上）
     * 会拒绝传输而不是破坏它；随后由主泵
     * 负责该帧。 */
    if (out_len >= 3U) {
        int tx_rc = -1;
        if (ctx->shared.phy->tx_frame != NULL) {
            tx_rc = ctx->shared.phy->tx_frame(ctx->shared.phy->self, base, out_len);
        }
        if (tx_rc != 0) {
            return false;
        }
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        base[out_len - 1U] = dll_reply_cks(acc, flags);
    }
    else {
        /* 短应答：在启动 DMA 之前填入 CKS。 */
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        base[out_len - 1U] = dll_reply_cks(acc, flags);
        if ((ctx->shared.phy->tx_frame == NULL) ||
            (ctx->shared.phy->tx_frame(ctx->shared.phy->self, base, out_len) != 0)) {
            return false;
        }
    }
    return true;
}
#else
/** @brief TX BYTE：暂存应答；CKS 在发送过程中计算。 */
static bool dll_isr_start_reply_byte(iolink_dll_main_t* ctx, uint8_t* base, uint16_t out_len,
                                     uint8_t flags)
{
    ctx->shared.isr->isr_tx_base = base;
    ctx->shared.isr->tx_stream_len = (uint8_t) (out_len - 1U);
    ctx->shared.isr->isr_tx_flags = flags;
    ctx->shared.isr_tx_idx = 0U;
    ctx->shared.isr->isr_tx_acc = 0x52U;
    ctx->shared.isr->tx_stream_has_cks = 1U;
    ctx->shared.isr_tx_active = 1U;
    if (ctx->shared.phy->response_timer_stop != NULL) {
        ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
    }
    dll_isr_tx_step(ctx);
    return true;
}
#endif

/* -------------------------------------------------------------------------
 * 应答计划执行器
 *
 * ISR 不再解读 M-sequence 内容：主泵把"哪种请求、用哪段字节、多长、
 * 是否附带 PD 尾部"编译成计划表并原子发布；这里只做 查计划 ->
 * 取源字节 -> 追加 CKS -> 喂字节。t_A 的时序语义（何时开始应答、
 * 回多少字节）留在本文件，内容语义只在 dll_frame.c。
 * ------------------------------------------------------------------------- */

/** @brief 状态到计划切片的映射（FALLBACK 没有计划）。 */
static int dll_plan_slot_of(iolink_dll_state_t state)
{
    switch (state) {
        case IOLINK_DLL_STATE_STARTUP:
        case IOLINK_DLL_STATE_AWAITING_COMM:
            return (int) IOLINK_PLAN_SLOT_STARTUP;
        case IOLINK_DLL_STATE_PREOPERATE:
            return (int) IOLINK_PLAN_SLOT_PREOPERATE;
        case IOLINK_DLL_STATE_ESTAB_COM:
            return (int) IOLINK_PLAN_SLOT_ESTAB_COM;
        case IOLINK_DLL_STATE_OPERATE:
            return (int) IOLINK_PLAN_SLOT_OPERATE;
        /* FALLBACK 没有计划；COMM_DISABLED 不回帧。枚举项全部列出，
         * 新增状态时必须显式选择槽位。 */
        case IOLINK_DLL_STATE_FALLBACK:
        case IOLINK_DLL_STATE_COMM_DISABLED:
        case IOLINK_DLL_STATE_COUNT:
        default:
            return -1;
    }
}

/**
 * @brief 在已发布的计划表中查找命中项。
 *
 * 计划表在初始化时一次写就，ISR 运行期间不再改变（重建要求数据 ISR
 * 静止），因此查表不需要序号校验：只需扫描当前状态切片并拷贝命中项。
 * 切片内没有匹配项时返回 false，本帧按"计划未命中"处理（丢弃）。
 */
bool dll_isr_plan_lookup(const iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len,
                         iolink_reply_plan_t* out)
{
    if ((ctx == NULL) || (frame == NULL) || (out == NULL) || (len < IOLINK_M_SEQ_HEADER_LEN)) {
        return false;
    }
    const uint8_t mc = frame[0];
    const uint8_t ckt = frame[1];
    const uint8_t addr = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);

    const int slot = dll_plan_slot_of(ctx->shared.state);
    if (slot < 0) {
        return false; /* FALLBACK / 未定义状态：没有可提前应答的计划 */
    }
    {
        const uint8_t off = ctx->shared.reply_plan_off[slot];
        const uint8_t room =
            (off <= IOLINK_REPLY_PLAN_SLOTS) ? (uint8_t) (IOLINK_REPLY_PLAN_SLOTS - off) : 0U;
        uint8_t count = ctx->shared.reply_plan_cnt[slot];
        if (count > room) {
            count = room;
        }
        for (uint8_t i = 0U; i < count; i++) {
            const iolink_reply_plan_t* plan = &ctx->shared.reply_plan[off + i];
            if (((mc & plan->match_mask) != plan->match_value) || (addr > plan->addr_max) ||
                ((plan->req_len != 0U) && (plan->req_len != len)) ||
                ((plan->ckt_mask != 0U) && ((ckt & plan->ckt_mask) != plan->ckt_value))) {
                continue;
            }
            *out = *plan;
            return true;
        }
    }
    return false;
}

/**
 * @brief 从计划指定的源填充 payload 字节。
 *
 * 所有来源都是只读查找或已暂存的段：这里不做任何"这个通道意味着
 * 什么"的判断。@ref IOLINK_REPLY_SRC_PD_IN 只能通过 PD 尾部标志使用。
 */
static bool dll_isr_plan_source(iolink_dll_main_t* ctx, const iolink_reply_plan_t* plan,
                                uint8_t addr, uint8_t* out)
{
    switch (plan->source) {
        case IOLINK_REPLY_SRC_NONE:
            return true;
        case IOLINK_REPLY_SRC_TEMPLATE:
            if (plan->data == NULL) {
                return false;
            }
            for (uint8_t i = 0U; i < plan->len; i++) {
                out[i] = plan->data[i];
            }
            return true;
        case IOLINK_REPLY_SRC_PAGE:
            /* 没有可用的设备身份就没有 DPP1：不答（主站超时）而不是发出
             * 全零页。设备构建在初始化时已强制要求身份。 */
            if (!dll_dp_page1_available(ctx)) {
                return false;
            }
            /* 单次 seqlock 窗口复制整段：页读载荷整帧同代，且避免逐字节
             * 访问器的函数调用与序号双读（M0+ 上尤其明显）。 */
            (void) iolink_isdu_dp_page1_read_range(&ctx->shared.isdu, addr, out, plan->len);
            return true;
        case IOLINK_REPLY_SRC_EVENT_MEM:
            for (uint8_t i = 0U; i < plan->len; i++) {
                out[i] = DLL_EVENTS_MEMORY_READ(&ctx->shared.events, (uint8_t) (addr + i));
            }
            return true;
        case IOLINK_REPLY_SRC_ISDU_SEG:
            dll_isr_isdu_od(ctx, addr, out, plan->len);
            return true;
        default:
            return false; /* PD-in 只能作为尾部来源 */
    }
}

/** @brief 执行计划要求的应答前状态动作。 */
static bool dll_isr_plan_action(iolink_dll_main_t* ctx, const iolink_reply_plan_t* plan,
                                uint8_t cmd)
{
    switch (plan->action) {
        case IOLINK_REPLY_ACT_NONE:
            return true;
        case IOLINK_REPLY_ACT_ENTER_PREOPERATE:
            dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
            ctx->shared.isr_dsio_clear = 1U;
            return true;
        case IOLINK_REPLY_ACT_ENTER_ESTAB_COM:
            dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
            ctx->shared.fallback_count = 0U;
            return true;
        case IOLINK_REPLY_ACT_RESET_STARTUP:
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            return true;
        case IOLINK_REPLY_ACT_MASTER_COMMAND:
            if (cmd == IOLINK_CMD_FALLBACK) {
                /* T_FBD 的 64 位截止时间归主泵所有：只递请求。 */
                if (dll_phy_has_wakeup(&ctx->shared)) {
                    ctx->shared.isr_fallback_pending = 1U;
                    dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
                }
                return true;
            }
            return dll_apply_master_command(ctx, cmd);
        default:
            return false;
    }
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/** @brief TX FRAME：报告线路状态的 PHY 表示上一应答可能仍在总线上。 */
static bool dll_isr_phy_busy(const iolink_dll_main_t* ctx)
{
    return (ctx->shared.phy->tx_busy != NULL) &&
           (ctx->shared.phy->tx_busy(ctx->shared.phy->self) != 0U);
}

/** @brief 回收上一应答的 PD-in 租约（TX FRAME）。
 *
 * 帧末没有库回调可以释放租约，因此由 ISR 在下一次进入时按 tx_busy()
 * 判定：线路空闲说明那次应答已经结束，缓冲区重新对主泵可用。
 * 没有 tx_busy() 的 PHY 无法观测传输结束，租约保持到下一次捕获，
 * 由 `IOLINK_PD_IN_BUFFERS` 的第三个缓冲区吸收主泵的更新。
 */
static void dll_isr_release_pd_lease(iolink_dll_main_t* ctx)
{
    if ((ctx->shared.pd_in_captured != 0xFFU) && (ctx->shared.phy->tx_busy != NULL) &&
        (ctx->shared.phy->tx_busy(ctx->shared.phy->self) == 0U)) {
        ctx->shared.pd_in_captured = 0xFFU;
    }
}
#else
static bool dll_isr_phy_busy(const iolink_dll_main_t* ctx)
{
    (void) ctx;
    return false;
}
#endif

/**
 * @brief 执行一条计划：动作 -> 取源 -> 追加 CKS -> 喂字节。
 *
 * @param ctx    DLL 上下文
 * @param plan   已命中的计划（拷贝，不受并发发布影响）
 * @param frame  请求字节（用于 address / MasterCommand 参数）
 * @param len    请求长度
 * @param staged 本帧的 ISDU 写是否已成功暂存（STAGED 计划的先决条件）
 * @return true 表示 ISR 已应答（或按计划有意不应答但消费了该帧）
 */
bool dll_isr_plan_execute(iolink_dll_main_t* ctx, const iolink_reply_plan_t* plan,
                          const uint8_t* frame, uint8_t len, bool staged)
{
    if (!dll_phy_has_tx(&ctx->shared)) {
        return false; /* 只收不发的 PHY：交给主泵（其路径同样不发送） */
    }
    if (((plan->flags & IOLINK_REPLY_FLAG_STAGED) != 0U) && !staged) {
        return false; /* 写未进入传输层：交给主泵 */
    }
    const uint8_t addr = (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK);
    const uint8_t cmd = (len > IOLINK_M_SEQ_HEADER_LEN) ? frame[IOLINK_M_SEQ_HEADER_LEN] : 0U;
    if (!dll_isr_plan_action(ctx, plan, cmd)) {
        return false;
    }

    if ((plan->flags & IOLINK_REPLY_FLAG_CLOSE_WINDOW) != 0U) {
        /* 按规范有意不应答（转换命令、复位请求）：关闭 t_A 窗口。 */
        if (ctx->shared.phy->response_timer_stop != NULL) {
            ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
        }
        if (((plan->flags & IOLINK_REPLY_FLAG_RESTART_TIMER_B) != 0U) &&
            (ctx->shared.phy->stack_timer_start != NULL)) {
            ctx->shared.phy->stack_timer_start(ctx->shared.phy->self);
        }
        dll_isr_touch_activity(ctx);
        return true;
    }

    const IOLINK_ATOMIC_UINT_T pd = dll_pd_pub_load(&ctx->shared);
    const uint8_t flags = dll_isr_status_flags(ctx, pd);

    if ((plan->flags & IOLINK_REPLY_FLAG_PD_TAIL) != 0U) {
        /* 长应答零拷贝：OD 头写入活动 PD-in 槽位，PD 尾部紧随其后。
         * 只有长应答在 DMA 启动后仍有足够距离计算 CKS，因此也只有在
         * 这里才需要 tx_busy 门控（短应答由 PHY 自行串行化）。 */
        if (dll_isr_phy_busy(ctx) || (plan->len > ctx->shared.od_width)) {
            return false;
        }
        /* 与状态位同一个发布字：捕获的缓冲区就是这一代的 active 槽。 */
        const uint8_t captured = IOLINK_PD_PUB_ACTIVE(pd);
        uint8_t* buf = ctx->shared.pd_in[captured];
        if (!dll_isr_plan_source(ctx, plan, addr, buf)) {
            return false;
        }
        uint8_t* base = buf + (uint8_t) (ctx->shared.od_width - plan->len);
        const uint16_t total =
            (uint16_t) ((uint16_t) plan->len + (uint16_t) ctx->shared.pd_in_len + 1U);
        if (total > (uint16_t) IOLINK_PD_REPLY_MAX) {
            return false;
        }
        ctx->shared.pd_in_captured = captured;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        return dll_isr_start_reply_frame(ctx, base, total, flags);
#else
        return dll_isr_start_reply_byte(ctx, base, total, flags);
#endif
    }

    uint8_t* payload = ctx->shared.isr_tx_payload[IOLINK_TX_ISR_SLOT];
    if (!dll_isr_plan_source(ctx, plan, addr, payload)) {
        return false;
    }
    dll_isr_send_staged(ctx, IOLINK_TX_ISR_SLOT, plan->len, flags);
    dll_isr_touch_activity(ctx);
    return true;
}

/** @brief 主上下文处理器暂存了一个逐字回复；现在发送它。
 *
 * @return true 表示本次处理已经结束（发送已启动或已丢弃）；false 表示因
 *         在途帧仍在发送而丢弃，调用方应继续正常的服务流程。
 */
static bool dll_isr_tx_pending(iolink_dll_main_t* ctx)
{
    /* 取走暂存必须与主泵的发布互斥：三字段分读时，主泵的新一轮暂存会让
     * 这里拿到撕裂的 {slot, len, has_cks}，或把新发布连同 pending 一起吞掉。
     * 在途帧仍在流式发送时丢弃本次暂存：接受它会从中间替换掉主站正在等的
     * 那一帧（后半段与 CKS 永不发出）。 */
    IOLINK_CRITICAL_ENTER();
    const bool busy = (ctx->shared.isr_tx_active != 0U);
    uint8_t slot = 0U;
    uint8_t len = 0U;
    uint8_t has_cks = 0U;
    if (!busy) {
        slot = ctx->shared.isr_tx_pending_slot;
        len = ctx->shared.stage_len;
        has_cks = ctx->shared.stage_has_cks;
    }
    ctx->shared.isr_tx_pending = 0U;
    IOLINK_CRITICAL_EXIT();
    if (busy) {
        IOLINK_STAT_ISR(ctx, isr_tx_dropped)++;
        return false;
    }
    if (ctx->shared.phy->response_timer_stop != NULL) {
        ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
    }
    dll_isr_start_staged_tx(ctx, slot, len, has_cks);
    return true;
}

/**
 * @brief 撤回已过代的 PD 有效位命令（ISR 单写者）。
 *
 * 命令字只覆盖它记录的那一代；主上下文下一次发布之后它即失效。撤回由
 * 它唯一的写者完成：否则 8 位代号回绕到同一个值时，一条早已失效的命令
 * 会重新对上当前一代。每次进入泵都检查一次，回绕因此需要连续
 * 256 次发布之间没有任何数据中断。
 */
static void dll_isr_retire_pd_cmd(iolink_dll_main_t* ctx)
{
    const IOLINK_ATOMIC_UINT_T cmd = iolink_atomic_load(&ctx->shared.pd_valid_cmd);
    if (IOLINK_PD_CMD_VALID(cmd) == 0U) {
        return;
    }
    if (IOLINK_PD_CMD_BASE_GEN(cmd) == IOLINK_PD_PUB_GEN(dll_pd_pub_load(&ctx->shared))) {
        return;
    }
    iolink_atomic_store(&ctx->shared.pd_valid_cmd, 0U);
}

static void dll_isr_process(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || !dll_phy_has_rx(&ctx->shared) || (ctx->shared.phy->int_active == NULL)) {
        return;
    }

    dll_isr_retire_pd_cmd(ctx);

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 上一次应答的租约在本入口回收，且必须在任何新捕获之前：
     * 主站静默期间没有中断，租约会一直保留到本次进入。 */
    dll_isr_release_pd_lease(ctx);
#endif

    /* 在 int_active 门控之前检查：软件请求的服务没有
     * 自身的收发器 RX/TX 活动可报告。 */
    if (ctx->shared.isr_tx_pending != 0U) {
        if (dll_isr_tx_pending(ctx)) {
            return;
        }
        /* 忙时只丢弃并计数：继续走下面的服务流程，让在途帧在本中断里
         * 照常推进一个八位组（边沿触发的 PHY 不保证再次请求服务）。 */
    }

    /* 电平复查：数据中断可能锁存串扰边沿；在零
     * 成本下退出，除非收发器确实在请求服务。 */
    if (ctx->shared.phy->int_active(ctx->shared.phy->self) == 0U) {
        return;
    }

    dll_isr_drain(ctx);
}

void iolink_dll_process_isr(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.isr == NULL) ||
        (iolink_atomic_load(&ctx->shared.reply_plans_ready) == 0U)) {
        /* 尚未 iolink_dll_init()，或应答计划表尚未完整发布（初始化窗口内）：
         * 传输泵不参与，绝不读取半更新的计划。 */
        return;
    }
    if (ctx->shared.in_isr != 0U) {
        /* 更高优先级的中断在一个较低优先级中断
         * 已在运行泵时调用了该泵（在没有 BASEPRI 的板子上
         * 该泵绑定到多个向量）。泵状态只有一个写入者，
         * 因此嵌套会破坏回复暂存和邮箱：推迟处理，
         * 让外层调用在返回前再运行一次。 */
        ctx->shared.isr->isr_reentry_pending = 1U;
        return;
    }
    ctx->shared.in_isr = 1U;
    do {
        ctx->shared.isr->isr_reentry_pending = 0U;
        dll_isr_process(ctx);
    } while (ctx->shared.isr->isr_reentry_pending != 0U);
    ctx->shared.in_isr = 0U;
}

void iolink_dll_on_rx_timeout(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.isr == NULL)) {
        return;
    }
    ctx->shared.isr_master_activity = 1U;
    IOLINK_LOG_ISR_DEBUG(&ctx->shared.log, "rx timeout (t_A)");
    IOLINK_STAT_ISR(ctx, timeout_errors)++;
    DLL_DIAG_PULSE_ISR(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_TIMEOUT);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    /* RX 组装状态只归数据 ISR，而本入口是 Timer A：两个字段必须作为
     * 一个相位复位，否则数据中断可能在其间重新播种半帧。短、无回调；
     * 向量优先级本身由端口保证（docs/PORTING.md）。 */
    IOLINK_CRITICAL_ENTER();
    ctx->shared.isr->isr_rx_cnt = 0U;
    ctx->shared.isr->isr_rx_expect = 0U;
    IOLINK_CRITICAL_EXIT();
#endif
    if ((ctx->shared.phy != NULL) && (ctx->shared.phy->response_timer_stop != NULL)) {
        ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
    }
}

void iolink_dll_on_stack_tick(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.isr == NULL)) {
        return;
    }
    ctx->shared.isr->stack_tick_pending = 1U;
}

uint8_t iolink_dll_take_service_window(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.isr == NULL) || (ctx->shared.phy == NULL)) {
        return 0U;
    }
    if (ctx->shared.phy->stack_timer_start == NULL) {
        return 1U; /* 无硬件窗口：传统自由运行主循环 */
    }
    if (ctx->shared.isr->stack_tick_pending == 0U) {
        return 0U;
    }
    ctx->shared.isr->stack_tick_pending = 0U;
    return 1U;
}
