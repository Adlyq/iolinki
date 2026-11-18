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
#include "isdu_internal.h"
#include "iolinki/internal/dll_sm.h"
#include <string.h>

/**
 * @file dll_reply.c
 * @brief 应答计划发布与 ISR 读取视图。
 *
 * 这里把各状态切片编译成 ISR 可直接匹配的应答计划（初始化时一次
 * 发布，之后只读），并维护 ISDU 响应快照与主站活动/周期时间推算。
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
 * 只在 `iolink_dll_publish_reply_plans()` 内调用，即数据 ISR 尚未看到
 * 这张表时（就绪标志为 0）。
 * @p state 只作记录与调试用途：匹配的可见范围由状态切片决定。
 */
/* 只在发布时调用（每次发布一次，绝不每帧）：强制不内联，让它只保留一份
 * 副本——内联进两个状态族的构建函数会各复制一遍，实测净多 128 B 文本。 */
static void IOLINK_NOINLINE dll_plan_push(iolink_dll_main_t* ctx, uint8_t* count, uint8_t state,
                                          uint8_t match_mask, uint8_t match_value, uint8_t addr_max,
                                          uint8_t req_len, uint8_t ckt_mask, uint8_t ckt_value,
                                          uint8_t source, uint8_t len, uint8_t flags,
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

int iolink_dll_publish_reply_plans(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return IOLINK_DLL_PUBLISH_ERR_NULL;
    }
    if (ctx->shared.isr == NULL) {
        /* iolink_dll_init() 尚未成功完成：没有可发布的上下文。 */
        return IOLINK_DLL_PUBLISH_ERR_UNINIT;
    }
    /* 计划表由数据 ISR 查表读取：重建必须与泵互斥。这里显式拒绝误从数据
     * ISR 的调用，而不是静默重写正在被查的表。 */
    if (ctx->shared.in_isr != 0U) {
        IOLINK_LOG_ISR_ERROR(&ctx->shared.log, "reply plan publish rejected inside the data ISR");
        return IOLINK_DLL_PUBLISH_ERR_ISR;
    }
    /* 计划内容是 {M-sequence 类型, OD 宽度, 能力位} 的纯函数，三项都只在
     * 初始化期间定义；各状态的应答差异由**状态切片**表达，状态迁移不需要
     * 重发布。因此本函数只在初始化（或数据 ISR 静止时的重新初始化）里调用，
     * 没有运行期并发协议：先撤下"就绪"标志，整张表写完后原子置回，ISR 入口
     * 只在标志为 1 时进入传输泵。 */
    iolink_atomic_store(&ctx->shared.reply_plans_ready, 0U);
    /* 该计数描述"当前这张表被截断了几条"，不是跨多次发布的累计值：
     * 设备初始化会发布两次（先默认配置、后最终配置），保留累计值会把
     * 被丢弃的那张表的截断也算进去。 */
    ctx->reply_plan_overflows = 0U;
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

    iolink_atomic_store(&ctx->shared.reply_plans_ready, 1U);
    return IOLINK_DLL_PUBLISH_OK;
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
 *  - 写失效用单调序号：ISR 每接受一个写 START/ABORT 就自增
 *    `isr_isdu_write_gen`。发布代号尚未被 ack 时这里发布
 *    len = 0（无服务），绝不让旧应答继续充当"当前值"；写被应用或
 *    中止后先发布新视图，再把 `isdu_write_gen_applied` 追平，
 *    确认必须放在发布之后：否则 ISR 可能在两者之间看到两值相等而
 *    读到旧视图。
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
    /* 写入口：发布字已有一代未被主泵处理（正在累计或已收齐待解析）时，
     * 写尚未生效，已发布读视图必须显示"无服务"。判据只读发布字与主泵
     * 自己的 ack 序号——数据 ISR 的累计字段（stage_state 等）不参与。 */
    const IOLINK_ATOMIC_UINT_T write_gen = iolink_atomic_load(&ctx->shared.isr_isdu_write_gen);
    const bool write_pending = isdu_write_in_progress(&ctx->shared.isdu);
    if (write_pending) {
        /* 写尚未应用：显式发布"无服务"，绝不重发旧应答。 */
        count = 0U;
        sel = 0U;
    }

    const IOLINK_ATOMIC_UINT_T word = IOLINK_ISDU_PUB_WORD_GEN(count, busy, sel, ctx->isdu_pub_gen);
    if (word != ctx->shared.isr_isdu_pub) {
        /* 最后发布：响应字节已在 resp_store[sel] 中。 */
        iolink_atomic_store(&ctx->shared.isr_isdu_pub, word);
    }
    if (!write_pending && (write_gen != ctx->shared.isdu_write_gen_applied)) {
        /* 写已应用或已中止：新视图已在上方发布，现在确认它的序号。
         * 释放顺序保证 ISR 看到 applied == gen 时发布字已是新视图。 */
        IOLINK_FENCE();
        ctx->shared.isdu_write_gen_applied = write_gen;
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

/** @brief 主站轮询周期，单位微秒（优先 DPP1 MasterCycleTime，否则 MinCycleTime）。
 *
 * 两个来源都未知时返回 0：本函数不提供默认周期，调用方必须把 0 当作
 * "周期时间尚不可知"处理（master-lost 检测在此时不武装）。
 */
uint32_t dll_master_cycle_us(const iolink_dll_main_t* ctx)
{
    uint16_t ticks = dll_decode_cycle_time_ticks(ctx->master_cycle_time);
    if (ticks == 0U) {
        ticks = (uint16_t) ((ctx->min_cycle_time_us + (IOLINK_CYCLE_TIME_UNIT_US - 1U)) /
                            IOLINK_CYCLE_TIME_UNIT_US);
    }
    return (uint32_t) ticks * IOLINK_CYCLE_TIME_UNIT_US;
}

/** @brief 刷新主站活动时间戳。
 *
 * 只由主上下文调用：主上下文自己完成的帧直接调用；ISR 完全消费的帧与
 * 进了邮箱的帧一样，由 ISR 置 `shared.isr_master_activity`，主泵在
 * `dll_take_isr_master_activity()` 中消费该标志后调用这里。只刷新时间戳与
 * 主站丢失提示，不涉及 Back-to-box 周期计数（该计数归主上下文独占，
 * 见 @ref dll_count_served_cycle 与 `shared.isr_cycle_served`）。 */
void dll_mark_master_activity_ts(iolink_dll_main_t* ctx)
{
    ctx->master_last_activity_ms = dll_time_ms(ctx);
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;
}

/** @brief 服务了一个 MasterCycle：推进 Back-to-box 计数（10.7.5）。
 *
 * 只由主上下文调用：主上下文自己处理的帧直接调用；ISR 完全消费的帧经
 * `shared.isr_cycle_served` 交接后由主泵调用。 */
void dll_count_served_cycle(iolink_dll_main_t* ctx)
{
    /* 10.7.5：统计 Back-to-box 之后服务的帧数，以便设备在停止前
       至少继续应答 3 个 MasterCycle（参见
       dll_process_complete_frame，它执行实际停止）。 */
    if (ctx->back_to_box_pending) {
        ctx->back_to_box_cycles++;
    }
}
