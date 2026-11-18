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
#include "dll_internal.h"
#include <string.h>
#include "dll_isr_internal.h"

/**
 * @file dll_isr_rx.c
 * @brief DLL ISR 接收侧：帧合法性、逐帧完成决策与 RX 组装。
 *
 * 两种 RX 粒度（PHY 整帧 / 逐八位组）在这里汇合：
 * 长度与类型校验、ISDU 写暂存、应答计划命中后的即时回复，
 * 以及把仍需主泵处理的帧投递到邮箱。
 */

static bool dll_isr_type_ok(const iolink_dll_main_t* ctx, const uint8_t* frame)
{
    return dll_type_allowed(ctx, frame);
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* 通过重新扫描帧来校验请求校验和（用于 FRAME 路径，
 * 该路径下 PHY 交出整个缓冲区）。 */
static bool dll_isr_frame_valid(const iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((ctx == NULL) || (frame == NULL) || (len < 2U)) {
        return false;
    }
    const uint8_t expected = dll_rx_expected_cks(frame);
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
static bool dll_isr_frame_valid_acc(const iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len,
                                    uint8_t acc)
{
    if ((ctx == NULL) || (frame == NULL) || (len < 2U)) {
        return false;
    }
    return (iolink_checksum6_from_acc(acc) == dll_rx_expected_cks(frame)) &&
           dll_isr_type_ok(ctx, frame);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/* 将已完成的请求复制到主泵的邮箱环中。由 ISR
 * 调用；当环已满时返回 false（并计入一次丢弃）。八位组
 * 在发布之前复制，因此 ISR 一返回 PHY 即可复用其接收
 * 缓冲区。@p eager 记录该 ISR 是否已
 * 发送回复（此时主泵必须保持静默）。只有校验失败的帧与由计划
 * 应答但需要主泵副作用的帧会进入邮箱。 */
static bool dll_isr_publish_frame(iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len,
                                  uint8_t eager)
{
    if ((frame == NULL) || (len > IOLINK_M_SEQ_MAX_LEN)) {
        IOLINK_LOG_ISR_DEBUG(&ctx->shared.log, "frame publish rejected: len=%u", (unsigned) len);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        DLL_DIAG_PULSE_ISR(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
        return false;
    }
    iolink_frame_rec_t* record =
        (iolink_frame_rec_t*) iolink_ring_claim(&ctx->shared.isr_frame_ring);
    if (record == NULL) {
        IOLINK_LOG_ISR_WARN(&ctx->shared.log, "mailbox full: frame dropped");
        ctx->shared.isr_mailbox_drops++;
        return false;
    }
    record->len = len;
    record->eager = eager;
    (void) memcpy(record->data, frame, len);
    iolink_ring_publish(&ctx->shared.isr_frame_ring); /* 最后发布 */
    ctx->shared.isr_frames_handoff++;
    return true;
}

/**
 * @brief 两种 RX 粒度共享的帧完成路径。
 *
 * 消费 ISDU 写，然后按主泵发布的应答计划决定是否提前应答。
 * 计划命中项在邮箱复制之前执行，因此第一个应答八位组绝不会等待
 * 发布（t_A）。合法帧未命中计划时 ISR 不解释内容、关闭 t_A 看门狗
 * 并丢弃该帧：计划表是"哪些请求可被服务"的唯一出处，主泵只消费
 * 不立即需要应答的数据。校验失败的帧仍发布到邮箱，由主泵完成
 * 错误/回退处理与计数。
 */
static inline void dll_isr_complete_frame(iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len,
                                          bool valid)
{
    bool staged = false;
    IOLINK_LOG_ISR_DEBUG(&ctx->shared.log, "frame complete: len=%u valid=%u", (unsigned) len,
                         (unsigned) valid);
    if (dll_isr_is_isdu_write(frame)) {
        /* 只缓冲有效请求：损坏的 ISDU 写绝不能到达
         * 传输层（ST 在 StackBaseOdWrite 之前校验）。 */
        if (!valid) {
            ctx->shared.isr->isr_eager_rejects++;
            return;
        }
        staged = dll_isr_consume_isdu_write(ctx, frame, len);
    }
    if (!valid) {
        /* 与历史即时路径统计一致：非 Type-0 形状的坏帧计为拒绝，
         * 错误/回退处理仍由主泵负责。 */
        if (dll_phy_has_tx(&ctx->shared) && !dll_ckt_is_type0(frame[1])) {
            ctx->shared.isr->isr_eager_rejects++;
        }
        /* 非法帧不解释内容、不计计划未命中：交给主泵做错误/状态处理。 */
        (void) dll_isr_publish_frame(ctx, frame, len, 0U);
        return;
    }

    /* ESTAB_COM 的首帧必须让主泵看到才能完成到 OPERATE 的迁移：
     * ISR 只负责即时应答，不消费该帧。 */
    const bool estab_com = (ctx->shared.state == IOLINK_DLL_STATE_ESTAB_COM);

    iolink_reply_plan_t plan;
    bool replied = false;
    bool planned = dll_isr_plan_lookup(ctx, frame, len, &plan);
    if (planned) {
        if ((plan.flags & IOLINK_REPLY_FLAG_DEFER) != 0U) {
            /* 内容相关判定归主泵（例如启动期 MasterCommand 的接受/拒绝）：
             * ISR 不应答、不消费、不计未命中，帧原样交给主泵。 */
            if (ctx->shared.phy->response_timer_stop != NULL) {
                ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
            }
            (void) dll_isr_publish_frame(ctx, frame, len, 0U);
            return;
        }
        replied = dll_isr_plan_execute(ctx, &plan, frame, len, staged);
        if (replied && ((plan.flags & IOLINK_REPLY_FLAG_CONSUME) != 0U) && !estab_com) {
            /* 活动标志已在 RX 完成路径置位（shared.isr_master_activity），
             * 由主泵在 dll_take_isr_master_activity() 中刷新活动时间戳与
             * 主站丢失提示；这里不重复写主上下文字段。
             * 该帧不进邮箱，主泵看不到它：把帧结束时间戳交接给主泵，
             * 由它在下一轮补做 T_CYCLE/T_REN 记账。先写时间戳，穿过
             * IOLINK_FENCE() 后再自增序号作为发布（release）边沿。 */
            ctx->shared.isr_consumed_us = (uint32_t) dll_time_us(ctx);
            IOLINK_FENCE();
            ctx->shared.isr_consumed_seq = (uint8_t) (ctx->shared.isr_consumed_seq + 1U);
            ctx->shared.isr_frames_consumed++;
            /* 本周期已被服务：Back-to-box 计数归主上下文，这里只置
             * 一次性标志（记账完成后再发布）。 */
            ctx->shared.isr_cycle_served = 1U;
            return;
        }
    }
    if (!replied) {
        if (planned) {
            /* 计划命中但本次无法执行（例如线路忙、ISDU 写尚未就绪）：
             * 不是覆盖缺口，交给主泵兜底处理（旧行为）。 */
            if (ctx->shared.phy->response_timer_stop != NULL) {
                ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
            }
            (void) dll_isr_publish_frame(ctx, frame, len, 0U);
            return;
        }
        /* 合法帧计划未命中：不应答、不解释内容，关闭 t_A 看门狗并
         * 直接丢弃。主泵不再接手计划未命中的合法帧。 */
        ctx->shared.isr_plan_misses++;
#if IOLINK_LOG_ENABLE_ISR
        /* 计数是主信号，日志只是辅助。FALLBACK/COMM_DISABLED 下"无计划"是
         * 常态，记 DEBUG；OPERATE 下合法帧却没有计划更可疑，记 WARN。整块随
         * IOLINK_LOG_ENABLE_ISR 编译开关消失，默认零开销。 */
        if (ctx->shared.state == IOLINK_DLL_STATE_OPERATE) {
            IOLINK_LOG_ISR_WARN(&ctx->shared.log,
                                "no reply plan in OPERATE (unknown shape): mc=0x%02X len=%u",
                                (unsigned) frame[0], (unsigned) len);
        }
        else {
            IOLINK_LOG_ISR_DEBUG(&ctx->shared.log, "no reply plan: mc=0x%02X len=%u",
                                 (unsigned) frame[0], (unsigned) len);
        }
#endif
        if (ctx->shared.phy->response_timer_stop != NULL) {
            ctx->shared.phy->response_timer_stop(ctx->shared.phy->self);
        }
        return;
    }
    /* 计划已应答但帧仍需主泵副作用（例如 DPP1 写）：邮箱副本携带
     * eager=1，主泵不得重发应答。 */
    (void) dll_isr_publish_frame(ctx, frame, len, 1U);
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/** @brief PHY 交付帧的长度合理性检查（计入成帧错误）。 */
static bool dll_isr_frame_len_ok(iolink_dll_main_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((len < 2U) || (len > IOLINK_M_SEQ_MAX_LEN)) {
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        DLL_DIAG_PULSE_ISR(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
        return false;
    }
    /* Type-0 的长度由 MC/CKT 固定，与 OPERATE 配置的 PD/OD 宽度无关；
       多字节类型按配置推导。两条 RX 路径共用 dll_req_len_for()，因此
       OPERATE 中的 Type-0 页读（复位请求）不会被按 PD 长度衡量。 */
    const uint8_t expected_len = dll_req_len_for(ctx, frame[0], frame[1]);
    if (len != expected_len) {
        IOLINK_LOG_ISR_DEBUG(&ctx->shared.log, "frame length reject: len=%u expected=%u",
                             (unsigned) len, (unsigned) expected_len);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        DLL_DIAG_PULSE_ISR(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
        return false;
    }
    return true;
}

/** @brief RX FRAME：取一个 PHY 交付的帧并交给共享尾部。 */
static void dll_isr_drain_rx_frame(iolink_dll_main_t* ctx)
{
    /* 即使邮箱已满也要消费 PHY 帧：即时
     * 回复仍须发出（与 RX BYTE 路径相同），此时发布只会
     * 计入丢弃，而不是静默丢失该帧。八位组在
     * ISR 返回之前复制，因此 PHY 可以复用其缓冲区。 */
    uint8_t status = 0U;
    uint8_t len = 0U;
    const uint8_t* frame = ctx->shared.phy->rx_frame(ctx->shared.phy->self, &status, &len);
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
        ctx->shared.isr_master_activity = 1U;
        if (ctx->shared.phy->response_timer_start != NULL) {
            /* 仅为将被应答的请求装载 t_A 看门狗：
             * 被拒绝或虚假的帧不得计为 t_A 超时。 */
            ctx->shared.phy->response_timer_start(ctx->shared.phy->self);
        }
    }
    dll_isr_complete_frame(ctx, frame, len, valid);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/** @brief 丢弃正在组装的帧（不触碰 TX 引擎）。 */
static void dll_isr_rx_byte_reset(iolink_dll_main_t* ctx)
{
    ctx->shared.isr->isr_rx_cnt = 0U;
    ctx->shared.isr->isr_rx_expect = 0U;
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
static void dll_isr_rx_byte_gap(iolink_dll_main_t* ctx)
{
    const bool incomplete_ms = (ctx->shared.isr->isr_rx_expect != 0U) &&
                               (ctx->shared.isr->isr_rx_cnt >= IOLINK_M_SEQ_HEADER_LEN) &&
                               (ctx->shared.isr->isr_rx_cnt < ctx->shared.isr->isr_rx_expect);
    if (incomplete_ms) {
        IOLINK_LOG_ISR_DEBUG(&ctx->shared.log, "incomplete frame: got %u of %u bytes",
                             (unsigned) ctx->shared.isr->isr_rx_cnt,
                             (unsigned) ctx->shared.isr->isr_rx_expect);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        DLL_DIAG_PULSE_ISR(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
    }
    dll_isr_rx_byte_reset(ctx);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    /* 释放收发器的接收引擎，除非有回复正在传输
     * （TX 排空会调用 tx_done）。在 TX FRAME 构建中没有 tx_done：
     * PHY 拥有两端传输并自行释放接收器。 */
    if ((ctx->shared.isr_tx_active == 0U) && (ctx->shared.phy->tx_done != NULL)) {
        ctx->shared.phy->tx_done(ctx->shared.phy->self);
    }
#endif
}

/** @brief RX BYTE：写入校验和种子并追加一个接收到的八位组。 */
static void dll_isr_rx_byte_accumulate(iolink_dll_main_t* ctx, uint8_t data)
{
    if ((ctx->shared.isr->isr_rx_cnt == 0U) && (ctx->shared.phy->response_timer_start != NULL)) {
        ctx->shared.phy->response_timer_start(ctx->shared.phy->self); /* 第一个八位组 -> Timer A */
    }
    if (ctx->shared.isr->isr_rx_cnt == 0U) {
        ctx->shared.isr->isr_rx_acc = 0x52U; /* A.1.6 种子（帧起始） */
    }
    if (ctx->shared.isr->isr_rx_cnt < sizeof(ctx->shared.isr->isr_rx_buf)) {
        uint8_t octet = data;
        if (ctx->shared.isr->isr_rx_cnt == 1U) {
            octet = (uint8_t) (octet & IOLINK_MSEQ_TYPE_MASK); /* 仅 CKT 类型位 */
        }
        ctx->shared.isr->isr_rx_acc = (uint8_t) (ctx->shared.isr->isr_rx_acc ^ octet);
        ctx->shared.isr->isr_rx_buf[ctx->shared.isr->isr_rx_cnt] = data;
        ctx->shared.isr->isr_rx_cnt++;
    }
}

/**
 * @brief RX BYTE：头部完整后推导预期请求长度。
 *
 * CKT 类型位标识 M-sequence 类型。Type-0 没有长度字段，但
 * 其大小由 MC/CKT 固定（读 2，OD 写 3）；13 Tbit 间隙只是
 * 超时，绝不是正常结束。
 */
static void dll_isr_rx_byte_derive_expect(iolink_dll_main_t* ctx)
{
    if (ctx->shared.isr->isr_rx_cnt != 2U) {
        return;
    }
    const uint8_t mc = (uint8_t) ctx->shared.isr->isr_rx_buf[0];
    const uint8_t ckt = (uint8_t) ctx->shared.isr->isr_rx_buf[1];
    /* 与 FRAME 路径同一个实现：Type-0 的长度由 MC 固定，其余按配置推导。 */
    ctx->shared.isr->isr_rx_expect = dll_req_len_for(ctx, mc, ckt);
}

/** @brief RX BYTE：已知长度已完整；将帧交给共享尾部。 */
static void dll_isr_rx_byte_complete(iolink_dll_main_t* ctx)
{
    /* 长度完整的 M-sequence 证明主站在轮询；主泵
     * 可能在其监控窗口结束前无法消费邮箱（参见
     * RX FRAME 分支）。 */
    ctx->shared.isr_master_activity = 1U;
    const bool valid =
        dll_isr_frame_valid_acc(ctx, ctx->shared.isr->isr_rx_buf, ctx->shared.isr->isr_rx_expect,
                                ctx->shared.isr->isr_rx_acc);
    dll_isr_complete_frame(ctx, ctx->shared.isr->isr_rx_buf, ctx->shared.isr->isr_rx_expect, valid);
    dll_isr_rx_byte_reset(ctx);
}

/** @brief RX BYTE：消费一个接收到的八位组。当排空应停止时返回 true。 */
static bool dll_isr_drain_rx_byte(iolink_dll_main_t* ctx)
{
    uint8_t data = 0U;
    uint8_t status = 0U;
    const int ok = ctx->shared.phy->rx_octet(ctx->shared.phy->self, &data, &status);

    if ((status & IOLINK_PHY_STATUS_CHK) != 0U) {
        dll_isr_rx_byte_gap(ctx);
        return true;
    }
    if (ok < 0) {
        /* 传输错误：丢弃部分帧并计数。 */
        IOLINK_LOG_ISR_WARN(&ctx->shared.log, "rx_octet error: %d", ok);
        IOLINK_STAT_ISR(ctx, framing_errors)++;
        DLL_DIAG_PULSE_ISR(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
        dll_isr_rx_byte_reset(ctx);
        return true;
    }
    if (ok == 0) {
        return true;
    }
    dll_isr_rx_byte_accumulate(ctx, data);
    dll_isr_rx_byte_derive_expect(ctx);
    if ((ctx->shared.isr->isr_rx_expect != 0U) &&
        (ctx->shared.isr->isr_rx_cnt >= ctx->shared.isr->isr_rx_expect)) {
        dll_isr_rx_byte_complete(ctx);
        return true;
    }
    return false;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/**
 * @brief 处理本次中断请求的服务事件，设有迭代上限。
 *
 * 每轮只处理一个服务事件并让出：TX BYTE 喂一个应答八位组，RX FRAME 取走
 * 一帧，RX BYTE 推进八位组直到本帧完成、遇到 CHK/间隙或读空。上限 64 次
 * 只保证单个请求不会把中断钩子拖住：合法请求最多
 * `IOLINK_M_SEQ_MAX_LEN` 个八位组，正常路径在单帧内返回。
 */
void dll_isr_drain(iolink_dll_main_t* ctx)
{
    for (uint32_t guard = 0U;
         (guard < 64U) && (ctx->shared.phy->int_active(ctx->shared.phy->self) != 0U); guard++) {
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        /* 回复排空（TX BYTE）：收发器请求下一个回复八位组。
         * 只喂一个并让出（下一个请求作为另一个 IRQ 到达）。 */
        if (ctx->shared.isr_tx_active != 0U) {
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
