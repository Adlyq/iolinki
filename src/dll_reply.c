/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include "iolinki/port.h"
#include "dll_internal.h"
#include "iolinki/internal/dll_sm.h"
#include <string.h>

/**
 * @file dll_reply.c
 * @brief 应答计划发布与 ISR 读取视图。
 *
 * 主泵在这里把当前状态切片编译成 ISR 可直接匹配的
 * 应答计划（seqlock 发布），并维护 ISDU 响应快照与
 * 主站活动/周期时间推算。
 */

/* -------------------------------------------------------------------------
 * 应答计划发布
 * ------------------------------------------------------------------------- */

/** @brief 全零 OD 模板：过程通道读的固定应答。 */
static const uint8_t dll_reply_zero[IOLINK_OD_MAX_SIZE] = {0U};

/** @brief Type-0 短应答计划集的构建模式。 */
typedef enum
{
    DLL_PLAN_MODE_STARTUP = 0,    /**< 读与过渡命令在 ISR 内推进到 PREOPERATE */
    DLL_PLAN_MODE_PREOPERATE = 1, /**< 过渡命令进入 ESTAB_COM */
    DLL_PLAN_MODE_OPERATE = 2     /**< OPERATE 中的过渡命令不是合法计划 */
} dll_plan_mode_t;

/**
 * @brief 追加一条计划槽位。
 *
 * 只在 seqlock 的奇数窗口内调用（计划表正在被主泵覆盖）。
 * @p state 只作记录与调试用途：匹配的可见范围由状态切片决定。
 */
static void dll_plan_push(iolink_dll_main_t* ctx, uint8_t* count, uint8_t state, uint8_t match_mask,
                          uint8_t match_value, uint8_t addr_max, uint8_t req_len, uint8_t ckt_mask,
                          uint8_t ckt_value, uint8_t source, uint8_t len, uint8_t flags,
                          uint8_t action, const uint8_t* data)
{
    if (count == NULL) {
        return;
    }
    if (*count >= IOLINK_REPLY_PLAN_SLOTS) {
        /* 计划表被截断时必须可观测：对应请求按"计划未命中"丢弃，
         * 主站只能超时重发。 */
        ctx->reply_plan_overflows++;
        return;
    }
    iolink_reply_plan_t* plan = &ctx->shared.reply_plan[*count];
    plan->data = data;
    plan->deadline_us = (uint16_t) (10U * dll_get_t_bit_us(ctx)); /* t_A = 10 位时间 */
    plan->match_mask = match_mask;
    plan->match_value = match_value;
    plan->ckt_mask = ckt_mask;
    plan->ckt_value = ckt_value;
    plan->addr_max = addr_max;
    plan->req_len = req_len;
    plan->len = len;
    plan->flags = flags;
    plan->source = source;
    plan->action = action;
    plan->state = state;
    (*count)++;
}

/** @brief Type-1/2 长应答的计划集合（OD 头来自计划，尾部来自 PD-in）。
 *
 * CKT 类型位必须与配置的 M-sequence 族匹配：OPERATE 中的 Type-0 页读
 * 与 Type-1/2 页读共用 MC 形状，只有类型位能区分二者（前者在
 * 非兼容模式下是复位请求，绝不能拿到长应答）。
 */
static void dll_plan_push_type12(iolink_dll_main_t* ctx, uint8_t* count, uint8_t state)
{
    const uint8_t od_w = ctx->shared.od_width;
    const uint8_t addr_max = IOLINK_MC_ADDR_MASK;
    const uint8_t ckt = dll_mseq_type_bits(ctx->shared.m_seq_type);

    /* 读：OD（按通道来源，宽度 od_width）+ PD-in。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PROCESS, addr_max, 0U,
                  IOLINK_MSEQ_TYPE_MASK, ckt, IOLINK_REPLY_SRC_TEMPLATE, od_w,
                  IOLINK_REPLY_FLAG_PD_TAIL, IOLINK_REPLY_ACT_NONE, dll_reply_zero);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE, addr_max, 0U, IOLINK_MSEQ_TYPE_MASK,
                  ckt, IOLINK_REPLY_SRC_PAGE, od_w, IOLINK_REPLY_FLAG_PD_TAIL,
                  IOLINK_REPLY_ACT_NONE, NULL);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS, addr_max, 0U,
                  IOLINK_MSEQ_TYPE_MASK, ckt, IOLINK_REPLY_SRC_EVENT_MEM, od_w,
                  IOLINK_REPLY_FLAG_PD_TAIL, IOLINK_REPLY_ACT_NONE, NULL);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU, addr_max, 0U, IOLINK_MSEQ_TYPE_MASK,
                  ckt, IOLINK_REPLY_SRC_ISDU_SEG, od_w, IOLINK_REPLY_FLAG_PD_TAIL,
                  IOLINK_REPLY_ACT_NONE, NULL);

    /* 写：无 OD，只有 PD-in。转换命令标记（MC 地址 0x0F）不由 ISR 应答。 */
    const uint8_t wr_max = (uint8_t) (IOLINK_MC_TRANSITION_COMMAND - 1U);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_PROCESS, wr_max, 0U, IOLINK_MSEQ_TYPE_MASK, ckt,
                  IOLINK_REPLY_SRC_NONE, 0U, IOLINK_REPLY_FLAG_PD_TAIL, IOLINK_REPLY_ACT_NONE,
                  NULL);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_PAGE, addr_max, 0U, IOLINK_MSEQ_TYPE_MASK, ckt,
                  IOLINK_REPLY_SRC_NONE, 0U, IOLINK_REPLY_FLAG_PD_TAIL, IOLINK_REPLY_ACT_NONE,
                  NULL);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_DIAGNOSIS, addr_max, 0U, IOLINK_MSEQ_TYPE_MASK, ckt,
                  IOLINK_REPLY_SRC_NONE, 0U, IOLINK_REPLY_FLAG_PD_TAIL, IOLINK_REPLY_ACT_NONE,
                  NULL);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_ISDU, addr_max, 0U, IOLINK_MSEQ_TYPE_MASK, ckt,
                  IOLINK_REPLY_SRC_NONE, 0U, IOLINK_REPLY_FLAG_PD_TAIL | IOLINK_REPLY_FLAG_STAGED,
                  IOLINK_REPLY_ACT_NONE, NULL);
}

/** @brief Type-0 短应答的计划集合（STARTUP / PREOPERATE / Type-0 OPERATE）。 */
static void dll_plan_push_type0(iolink_dll_main_t* ctx, uint8_t* count, uint8_t state,
                                dll_plan_mode_t mode)
{
    /* STARTUP/AWAITING 的所有读与过渡命令都由 ISR 推进到 PREOPERATE；
     * 写帧交给主泵应用（主泵在同一帧上完成状态迁移）。 */
    const uint8_t read_action = (mode == DLL_PLAN_MODE_STARTUP)
                                    ? (uint8_t) IOLINK_REPLY_ACT_ENTER_PREOPERATE
                                    : (uint8_t) IOLINK_REPLY_ACT_NONE;

    if (mode != DLL_PLAN_MODE_OPERATE) {
        if (mode == DLL_PLAN_MODE_STARTUP) {
            /* STARTUP/AWAITING 的过渡命令交给主泵：SM 先进入 PREOPERATE、
             * 再由动作推进到 ESTAB_COM，应用层的状态回调依赖这条完整路径；
             * 它本身没有应答，不存在 t_A 风险。 */
            dll_plan_push(ctx, count, state, 0xFFU, IOLINK_MC_TRANSITION_COMMAND,
                          IOLINK_MC_ADDR_MASK, 2U, IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0,
                          IOLINK_REPLY_SRC_NONE, 0U, IOLINK_REPLY_FLAG_DEFER, IOLINK_REPLY_ACT_NONE,
                          NULL);
        }
        else {
            /* PREOPERATE 的过渡命令由 ISR 消费并推进到 ESTAB_COM（不应答）。 */
            dll_plan_push(ctx, count, state, 0xFFU, IOLINK_MC_TRANSITION_COMMAND,
                          IOLINK_MC_ADDR_MASK, 2U, IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0,
                          IOLINK_REPLY_SRC_NONE, 0U,
                          IOLINK_REPLY_FLAG_CLOSE_WINDOW | IOLINK_REPLY_FLAG_RESTART_TIMER_B |
                              IOLINK_REPLY_FLAG_CONSUME,
                          IOLINK_REPLY_ACT_ENTER_ESTAB_COM, NULL);
        }
    }

    /* 读：一个 OD 字节 + CKS。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 2U,
                  IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_PAGE, 1U,
                  IOLINK_REPLY_FLAG_CONSUME, read_action, NULL);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PROCESS, IOLINK_MC_ADDR_MASK, 2U,
                  IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_TEMPLATE, 1U,
                  IOLINK_REPLY_FLAG_CONSUME, read_action, dll_reply_zero);
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS, IOLINK_MC_ADDR_MASK, 2U,
                  IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_EVENT_MEM, 1U,
                  IOLINK_REPLY_FLAG_CONSUME, read_action, NULL);
    /* ISDU 读：START/COUNT（FlowCTRL < IDLE）由 ISR 直接从快照应答。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU, IOLINK_FLOWCTRL_START, 2U,
                  IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_ISDU_SEG, 1U,
                  IOLINK_REPLY_FLAG_CONSUME, read_action, NULL);
    /* ISDU 读 IDLE/IDLE2/ABORT（0x11..0x1F）：回 0x00 并请求主泵复位读
     * 传输层（shared.isr_isdu_read_reset）。线性查找顺序保证上面的
     * START/COUNT 计划先命中，本条兜住其余读。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU, IOLINK_MC_ADDR_MASK, 0U,
                  IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_ISDU_SEG, 1U,
                  IOLINK_REPLY_FLAG_CONSUME, read_action, NULL);

    /* 写：ISR 以 CKS 确认；除 ISDU 写（暂存后由主泵应用）外，帧仍进邮箱
     * 由主泵应用写入。MasterCommand（地址 0）在 PREOPERATE/OPERATE 由
     * ISR 直接应用并消费；STARTUP 交给主泵完成状态迁移。 */
    if (mode == DLL_PLAN_MODE_STARTUP) {
        /* 启动期 MasterCommand 的接受/拒绝依赖命令字节与兼容模式（V1.1 拒绝
         * 0x99）：ISR 不解析内容，帧交主泵判定。 */
        dll_plan_push(ctx, count, state, 0xFFU, IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 3U,
                      IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE, 0U,
                      IOLINK_REPLY_FLAG_DEFER, IOLINK_REPLY_ACT_NONE, NULL);
    }
    else {
        dll_plan_push(ctx, count, state, 0xFFU, IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 3U,
                      IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE, 0U,
                      IOLINK_REPLY_FLAG_CONSUME, IOLINK_REPLY_ACT_MASTER_COMMAND, NULL);
    }
    /* 普通页写（地址 1..0x1F）：ISR 先以 CKS 确认，帧仍进邮箱由主泵
     * 应用写入（DPP 写语义归主泵）。地址 0 已被上面的 MasterCommand
     * 计划先匹配。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 3U, IOLINK_MSEQ_TYPE_MASK,
                  IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE, 0U, 0U, IOLINK_REPLY_ACT_NONE, NULL);
    /* 诊断写（事件确认）：同上，主泵应用。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_DIAGNOSIS, IOLINK_MC_ADDR_MASK, 3U, IOLINK_MSEQ_TYPE_MASK,
                  IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE, 0U, 0U, IOLINK_REPLY_ACT_NONE, NULL);
    /* 过程通道写（Type-0 写无 OD，2 字节）：ESTAB_COM 的首帧由此到达主泵
     * 完成到 OPERATE 的迁移。 */
    dll_plan_push(ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                  IOLINK_MC_CHANNEL_PROCESS, IOLINK_MC_ADDR_MASK, 2U, IOLINK_MSEQ_TYPE_MASK,
                  IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE, 0U, 0U, IOLINK_REPLY_ACT_NONE, NULL);
    /* ISDU 写：仅在已成功暂存时确认（FlowCTRL 由传输层校验，计划不分类）。 */
    dll_plan_push(
        ctx, count, state, IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK, IOLINK_MC_CHANNEL_ISDU,
        IOLINK_MC_ADDR_MASK, 3U, IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE,
        0U, IOLINK_REPLY_FLAG_CONSUME | IOLINK_REPLY_FLAG_STAGED, IOLINK_REPLY_ACT_NONE, NULL);
}

void iolink_dll_invalidate_reply_plans(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* 发布字是计划表的唯一有效标志：先撤到 0，数据 ISR 从这一刻起把每一帧
     * 当作"计划未命中"丢弃，绝不执行与新能力位不符的旧计划。
     * seq = 0 也是"尚未发布"的哨兵，下一次 publish 必然走完整路径
     * （0 -> 1 -> 2），因此键清零只是让发布条件在形式上也不再命中。 */
    iolink_atomic_store(&ctx->shared.reply_plan_seq, (IOLINK_ATOMIC_UINT_T) 0U);
    ctx->reply_plan_key = 0U;
}

void iolink_dll_publish_reply_plans(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* 键 = {M-sequence 类型, OD 宽度, 波特率}：计划表与 t_A 预算都是这三项
     * 的纯函数。状态不再参与键：每个 ISR 可达状态一份切片，一次发布；
     * 状态迁移不再需要主泵重发布。能力位变化仍须先调用
     * iolink_dll_invalidate_reply_plans()，否则键不变会让这里提前返回。 */
    const uint16_t key = (uint16_t) (0x8000U | ((unsigned) (ctx->shared.m_seq_type & 0x0FU)) |
                                     ((unsigned) (ctx->shared.od_width & 0x1FU) << 4) |
                                     ((unsigned) ((uint8_t) ctx->baudrate & 0x03U) << 9));
    const IOLINK_ATOMIC_UINT_T seq = iolink_atomic_load(&ctx->shared.reply_plan_seq);
    if ((seq != 0U) && (key == ctx->reply_plan_key)) {
        return; /* 计划表是键的纯函数：没有变化就不写 */
    }
    ctx->reply_plan_key = key;

    /* seqlock：奇数 = 正在覆盖槽位；ISR 只接受两次读取相同的偶数窗口。 */
    iolink_atomic_store(&ctx->shared.reply_plan_seq, seq + 1U);
    uint8_t count = 0U;
    const bool type0 = (ctx->shared.m_seq_type == IOLINK_M_SEQ_TYPE_0);

    /* 切片 0：STARTUP / AWAITING_COMM。 */
    ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_STARTUP] = count;
    dll_plan_push_type0(ctx, &count, (uint8_t) IOLINK_DLL_STATE_STARTUP, DLL_PLAN_MODE_STARTUP);
    ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_STARTUP] =
        (uint8_t) (count - ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_STARTUP]);

    /* 切片 1：PREOPERATE。 */
    ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_PREOPERATE] = count;
    dll_plan_push_type0(ctx, &count, (uint8_t) IOLINK_DLL_STATE_PREOPERATE,
                        DLL_PLAN_MODE_PREOPERATE);
    ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_PREOPERATE] =
        (uint8_t) (count - ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_PREOPERATE]);

    /* 切片 2：ESTAB_COM。Type-1/2 配置下首帧可能是 Type-0 页读（C5），
     * 由 ISR 应答后仍交主泵完成迁移；Type-0 配置与 OPERATE 内容相同。 */
    ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_ESTAB_COM] = count;
    if (!type0) {
        dll_plan_push_type12(ctx, &count, (uint8_t) IOLINK_DLL_STATE_ESTAB_COM);
        dll_plan_push(ctx, &count, (uint8_t) IOLINK_DLL_STATE_ESTAB_COM,
                      IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                      IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 2U,
                      IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_PAGE, 1U,
                      IOLINK_REPLY_FLAG_CONSUME, IOLINK_REPLY_ACT_NONE, NULL);
        /* Type-0 页写：ISR 以 CKS 确认，帧交主泵应用（通用页写行同时覆盖
         * MasterCommand 地址 0，无需单独一行）。 */
        dll_plan_push(ctx, &count, (uint8_t) IOLINK_DLL_STATE_ESTAB_COM,
                      IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK, IOLINK_MC_CHANNEL_PAGE,
                      IOLINK_MC_ADDR_MASK, 3U, IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0,
                      IOLINK_REPLY_SRC_NONE, 0U, 0U, IOLINK_REPLY_ACT_NONE, NULL);
        ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_ESTAB_COM] =
            (uint8_t) (count - ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_ESTAB_COM]);
    }
    else {
        dll_plan_push_type0(ctx, &count, (uint8_t) IOLINK_DLL_STATE_ESTAB_COM,
                            DLL_PLAN_MODE_OPERATE);
        ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_ESTAB_COM] =
            (uint8_t) (count - ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_ESTAB_COM]);
    }

    /* 切片 3：OPERATE。 */
    if (type0) {
        /* Type-0 配置的 ESTAB 与 OPERATE 内容相同，共享同一切片。 */
        ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_OPERATE] =
            ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_ESTAB_COM];
        ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_OPERATE] =
            ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_ESTAB_COM];
    }
    else {
        ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_OPERATE] = count;
        dll_plan_push_type12(ctx, &count, (uint8_t) IOLINK_DLL_STATE_OPERATE);
        if (iolink_caps_has(ctx->shared.caps, IOLINK_CAP_COMPAT_T0_READ)) {
            /* 兼容模式：OPERATE 中的 Type-0 页读仍按普通页读应答。 */
            dll_plan_push(ctx, &count, (uint8_t) IOLINK_DLL_STATE_OPERATE,
                          IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                          IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 2U,
                          IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_PAGE, 1U,
                          IOLINK_REPLY_FLAG_CONSUME, IOLINK_REPLY_ACT_NONE, NULL);
        }
        else {
            /* Tables 57/67：OPERATE 中的 Type-0 页读是复位请求，不应答。
             * 计划显式表达"消费该帧并回到 STARTUP"，使主泵无需再看到它。 */
            dll_plan_push(ctx, &count, (uint8_t) IOLINK_DLL_STATE_OPERATE,
                          IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK,
                          IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE, IOLINK_MC_ADDR_MASK, 2U,
                          IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0, IOLINK_REPLY_SRC_NONE, 0U,
                          IOLINK_REPLY_FLAG_CLOSE_WINDOW | IOLINK_REPLY_FLAG_CONSUME,
                          IOLINK_REPLY_ACT_RESET_STARTUP, NULL);
        }
        /* Type-0 页写：ISR 以 CKS 确认，帧交主泵应用（通用页写行同时覆盖
         * MasterCommand 地址 0，无需单独一行）。 */
        dll_plan_push(ctx, &count, (uint8_t) IOLINK_DLL_STATE_OPERATE,
                      IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK, IOLINK_MC_CHANNEL_PAGE,
                      IOLINK_MC_ADDR_MASK, 3U, IOLINK_MSEQ_TYPE_MASK, IOLINK_MSEQ_TYPE_0,
                      IOLINK_REPLY_SRC_NONE, 0U, 0U, IOLINK_REPLY_ACT_NONE, NULL);
        ctx->shared.reply_plan_cnt[IOLINK_PLAN_SLOT_OPERATE] =
            (uint8_t) (count - ctx->shared.reply_plan_off[IOLINK_PLAN_SLOT_OPERATE]);
    }

    ctx->shared.reply_plan_total = count;
    iolink_atomic_store(&ctx->shared.reply_plan_seq, seq + 2U);
}

/**
 * @brief 为 ISR 读泵发布成帧的 ISDU 响应视图。
 *
 * 单写者交接：ctx->shared.isdu（以及响应视图槽）由主泵
 * 拥有；ISR 读取最后发布的缓冲区。{length, busy,
 * buffer} 作为一个原子字传递，因此无需临界区、无需拷贝，
 * ISR 总是读到完整、稳定的响应。
 *
 * 失效协议（防"写后立刻读"返回旧值）：
 *  - 每份**新**应答翻转代数位（位 10）。长度与缓冲区选择完全相同的
 *    两份应答因此仍是不同的发布字，ISR 的段索引序列会被打断；
 *  - ISR 暂存 ISDU 写后置 `isr_isdu_invalidated`；在写被应用并产出
 *    新应答之前，这里发布 len = 0（无服务），绝不让旧应答继续充当
 *    "当前值"。
 */
void dll_isdu_sync_snapshot(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    const uint8_t state = (uint8_t) ctx->shared.isdu.state;
    const bool ready = (state == (uint8_t) ISDU_STATE_RESPONSE_READY);
    /* 新应答：引擎切换响应视图槽（resp_sel 与已发布的不同），
     * 或刚刚进入 RESPONSE_READY。两种信号都意味着"内容已换"。 */
    const bool new_response = ready && ((ctx->shared.isdu.resp_sel != ctx->shared.isdu.resp_pub) ||
                                        (ctx->isdu_pub_state != state));
    ctx->isdu_pub_state = state;

    const bool busy = (state == (uint8_t) ISDU_STATE_SERVICE_EXECUTE);
    uint8_t count = 0U;
    uint8_t sel = ctx->shared.isdu.resp_pub;
    if (ready) {
        if (new_response) {
            ctx->isdu_pub_gen ^= 1U; /* 新应答：让 ISR 丢弃采纳的段索引 */
        }
        count = (ctx->shared.isdu.resp_total > 255U) ? 255U : (uint8_t) ctx->shared.isdu.resp_total;
        sel = ctx->shared.isdu.resp_sel;
        /* 完成的响应成为已发布响应；下一次应答会先以租约
         * 占用同一成帧缓冲再重建。 */
        ctx->shared.isdu.resp_pub = sel;
    }
    /* 写入口相位：RECEPTION/COMPLETE/ABORT 表示写尚未被主泵应用，
     * 已发布读视图必须显示"无服务"；IDLE 表示写已应用或已中止。 */
    const uint8_t stage = ctx->shared.isdu.stage_state;
    const bool write_pending = (stage == (uint8_t) IOLINK_ISDU_STAGE_RECEPTION) ||
                               (stage == (uint8_t) IOLINK_ISDU_STAGE_COMPLETE) ||
                               (stage == (uint8_t) IOLINK_ISDU_STAGE_ABORT);
    /* 失效旗标的清除条件：写入口已回到空闲（主泵已应用或已中止），
     * 即本轮发布的是"已应用该写之后"的视图。临界区把
     * "ISR 置旗标"与"主泵检查相位 + 清旗标"分开，
     * 避免清掉一个尚未被主泵接管的失效请求。 */
    if ((ctx->shared.isr_isdu_invalidated != 0U) && !write_pending) {
        IOLINK_CRITICAL_ENTER();
        ctx->shared.isr_isdu_invalidated = 0U;
        IOLINK_CRITICAL_EXIT();
    }
    if ((ctx->shared.isr_isdu_invalidated != 0U) && write_pending) {
        /* 写尚未应用：显式发布"无服务"，绝不重发旧应答。 */
        count = 0U;
        sel = 0U;
    }

    const IOLINK_ATOMIC_UINT_T word = IOLINK_ISDU_PUB_WORD_GEN(count, busy, sel, ctx->isdu_pub_gen);
    if (word != ctx->shared.isr_isdu_pub) {
        /* 最后发布：响应字节已在 resp_store[sel] 中。 */
        iolink_atomic_store(&ctx->shared.isr_isdu_pub, word);
    }
}

/** @brief 将 DPP1 MasterCycleTime/MinCycleTime 字节解码为 100 us 刻度（ST Table B.3）。 */
static uint16_t dll_decode_cycle_time_ticks(uint8_t value)
{
    if (value <= 3U) {
        return 0U;
    }
    if (value < 0x40U) {
        return (uint16_t) value;
    }
    if ((value & 0xC0U) == 0x40U) {
        return (uint16_t) (((uint16_t) (value & 0x3FU) * 4U) + 0x40U);
    }
    if ((value & 0xC0U) == 0x80U) {
        return (uint16_t) (((uint16_t) (value & 0x3FU) * 0x10U) + 0x140U);
    }
    return 0U;
}

/** @brief 主站轮询周期，单位微秒（优先 DPP1 MasterCycleTime，否则 MinCycleTime）。 */
uint32_t dll_master_cycle_us(const iolink_dll_main_t* ctx)
{
    uint16_t ticks = dll_decode_cycle_time_ticks(ctx->master_cycle_time);
    if (ticks == 0U) {
        ticks = (uint16_t) ((ctx->min_cycle_time_us + 99U) / 100U);
    }
    if (ticks == 0U) {
        ticks = 20U; /* 2 ms 回退 */
    }
    return (uint32_t) ticks * 100U;
}

/** @brief 刷新主站活动时间戳。
 *
 * 用于数据 ISR 看到帧、但 Back-to-box 周期计数仍必须由
 * 实际服务该请求的一方精确推进一次的场景。 */
void dll_mark_master_activity_ts(iolink_dll_main_t* ctx)
{
    ctx->master_last_activity_ms = dll_time_ms(ctx);
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;
}

/** @brief ST 风格主站活动：帧结束/RX 超时会重启计数器。 */
void dll_mark_master_activity(iolink_dll_main_t* ctx)
{
    dll_mark_master_activity_ts(ctx);

    /* 10.7.5：统计 Back-to-box 之后服务的帧数，以便设备在停止前
       至少继续应答 3 个 MasterCycle（参见
       dll_process_complete_frame，它执行实际停止）。每个
       服务周期精确计数一次：其自身应答的请求由 ISR 计数，
       否则由 dll_process_complete_frame 计数。 */
    if (ctx->back_to_box_pending) {
        ctx->back_to_box_cycles++;
    }
}
