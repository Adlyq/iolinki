/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/crc.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "dll_internal.h"
#include "iolinki/internal/dll_sm.h"
#include <string.h>

/**
 * @file dll_sm.c
 * @brief DLL 状态机骨架与完整帧分发。
 *
 * `g_dll_sm[state][event]` 由 `DLL_SM_ROWS` 展开，本文件
 * 负责事件分类、动作执行与校验失败后的状态处理。
 */

/* -------------------------------------------------------------------------
 * 主泵：帧校验、分发与时序监控
 * ------------------------------------------------------------------------- */

/** 连续 CRC 错误达到该数量时发一次 COMM_CRC 诊断脉冲（不切换 DL 模式）。 */
#define DLL_CRC_DIAG_BATCH 3U

/**
 * @brief 校验 A.1.6 校验和与 CKT M-sequence 类型。
 *
 * A.1.6/Figure A.2：校验和位于 CKT 字节（第二个字节）；计算校验和
 * 之前，其位 0-5 被清零，类型位（6-7）保留。
 * C5/Table 47：类型必须对当前状态合法。
 */
dll_frame_result_t dll_frame_validate(iolink_dll_main_t* ctx)
{
    uint8_t crc_buf[sizeof(ctx->frame_buf)];
    (void) memcpy(crc_buf, ctx->frame_buf, ctx->req_len);
    const uint8_t expected_ck = dll_rx_expected_cks(crc_buf);
    crc_buf[1] = (uint8_t) (crc_buf[1] & IOLINK_MSEQ_TYPE_MASK);
    const bool crc_ok = (iolink_checksum6(crc_buf, ctx->req_len) == expected_ck);
    const bool type_ok = dll_type_allowed(ctx, ctx->frame_buf);

    if (crc_ok && type_ok) {
        return DLL_FRAME_VALID;
    }
    if (crc_ok) {
        return DLL_FRAME_TYPE_ERROR;
    }
    return DLL_FRAME_INVALID;
}

/**
 * @brief 处理无效或类型非法的帧。
 *
 * 校验和失败保持 DL 模式（ST rc=2 语义）。校验和有效
 * 但类型非法时，在 OPERATE/ESTAB 之外属于协议错误并返回
 * STARTUP；在 OPERATE/ESTAB 内则视为重叠 PD 流造成的
 * 成帧伪影，仅计数。
 */
void dll_handle_frame_error(iolink_dll_main_t* ctx, dll_frame_result_t result)
{
    IOLINK_LOG_DEBUG(&ctx->shared.log, "frame error: result=%d state=%u", (int) result,
                     (unsigned) ctx->shared.state);
    if (result == DLL_FRAME_TYPE_ERROR) {
        ctx->frame_replied_by_isr = 0U;
        ctx->shared.framing_errors++;
        ctx->frame_index = 0U;
        if ((ctx->shared.state == IOLINK_DLL_STATE_OPERATE) ||
            (ctx->shared.state == IOLINK_DLL_STATE_ESTAB_COM)) {
            return;
        }
        DLL_DIAG_PULSE(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_FRAMING);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        return;
    }

    /* 校验和失败：ST rc=2 表示“丢弃该帧并返回
     * 监听”；DLL 模式保持不变。此处不要降级到 SIO：
     * 单个截断/碰撞帧（例如主站在我们的应答仍在 C/Q 上时
     * 开始下一个 M-sequence）不能使链路下线。
     * SIO 保留给 Fallback MasterCommand 和 T_DSIO 超时。 */
    ctx->crc_errors++;
    ctx->shared.framing_errors++;
    ctx->invalid_frames++;
    /* 保留诊断事件（ST 没有计数器，但应用用它
     * 作为事件标志），同时不改变 DL 模式。 */
    ctx->shared.fallback_count++;
    if (ctx->shared.fallback_count >= DLL_CRC_DIAG_BATCH) {
        ctx->shared.fallback_count = 0U;
        DLL_DIAG_PULSE(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_CRC);
    }
}

/**
 * @brief 拒绝在启动期间收到的 OPERATE 请求。
 *
 * Table 53（SDCI_TC_0035）：只有 V1.0 设备会对启动期间收到的
 * OPERATE 请求作出反应。V1.1 设备不得应答它，也不得
 * 推进到 PREOPERATE/ESTAB_COM。
 *
 * @return 当帧被拒绝（跳过尾部清理）时返回 true。
 */
static bool dll_reject_startup_operate(iolink_dll_main_t* ctx, bool was_establishing)
{
    if (was_establishing && (ctx->req_len == 3U) &&
        ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U) &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
        ((ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK) == 0x00U) &&
        ((ctx->frame_buf[2] == IOLINK_CMD_DEVICE_OPERATE) ||
         (ctx->frame_buf[2] == IOLINK_CMD_PROCESS_DATA_OPERATE))) {
        ctx->shared.framing_errors++;
        ctx->frame_index = 0U;
        return true;
    }
    return false;
}

/* -------------------------------------------------------------------------
 * 状态机骨架表
 *
 * state × event -> (进入状态, 动作)。事件分类只看帧字节与长度；动作仍是
 * 普通函数，连续条件（通道宽度、兼容开关、长度）留在函数里。
 * 表由 include/iolinki/internal/dll_sm.h 的 DLL_SM_ROWS 展开，tools/
 * gen_dll_statemachine.py 从同一份宏行生成 mermaid/CSV/覆盖测试。
 * ------------------------------------------------------------------------- */

/**
 * @brief 对已校验帧做互斥事件分类。
 *
 * 判定顺序与原 dispatch 的优先级一致：过渡命令 > FALLBACK > 非 Type-0
 * > 2 字节 Type-0（页读 / 其他）> 页写地址 0 > 页写其他地址 > Type-0 写。
 * 长度参与分类，使畸形帧仍走与重构前完全相同的分支。
 */
static iolink_dll_event_t dll_sm_classify(const iolink_dll_main_t* ctx)
{
    const uint8_t mc = ctx->frame_buf[0];
    const uint8_t ckt = ctx->frame_buf[1];
    const bool type0 = dll_ckt_is_type0(ckt);
    const bool write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    const uint8_t channel = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);

    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        return DLL_EV_TRANSITION;
    }
    if ((ctx->req_len == 3U) && dll_is_fallback_command(ctx)) {
        return DLL_EV_FALLBACK_CMD;
    }
    if (!type0) {
        return DLL_EV_PD_EXCHANGE;
    }
    if (ctx->req_len == 2U) {
        /* 2 字节 Type-0：页读单独分类（启动探测 / OPERATE 复位），其余
         * （含畸形 2 字节页写）都走 operate_type0 的 CKS/OD 应答。 */
        if (!write && (channel == IOLINK_MC_CHANNEL_PAGE)) {
            return DLL_EV_PAGE_READ_T0;
        }
        return DLL_EV_T0_SHORT;
    }
    if (write && (channel == IOLINK_MC_CHANNEL_PAGE)) {
        return ((mc & IOLINK_MC_ADDR_MASK) == 0x00U) ? DLL_EV_PAGE_WRITE_ADDR0 : DLL_EV_PAGE_WRITE;
    }
    /* 3 字节 Type-0 写，或畸形 3 字节读：原 dispatch 一律走 OD 写路径。 */
    return DLL_EV_OD_WRITE_T0;
}

static bool dll_sm_none(iolink_dll_main_t* ctx)
{
    (void) ctx;
    return true;
}

static bool dll_sm_transition(iolink_dll_main_t* ctx)
{
    dll_handle_preoperate(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
    return true;
}

static bool dll_sm_fallback(iolink_dll_main_t* ctx)
{
    dll_handle_fallback_command(ctx);
    return true;
}

/** @brief 启动探测：用 Direct Parameter 页面字节应答 Type-0 页读。 */
static bool dll_sm_page_probe(iolink_dll_main_t* ctx)
{
    dll_handle_page_channel_read(ctx, ctx->frame_buf[0]);
    return true;
}

static bool dll_sm_master_command(iolink_dll_main_t* ctx)
{
    dll_handle_master_command(ctx, ctx->frame_buf[2]);
    return true;
}

static bool dll_sm_od_write_t0(iolink_dll_main_t* ctx)
{
    dll_handle_type0_od_write(ctx, ctx->frame_buf[0], ctx->frame_buf[2]);
    return true;
}

static bool dll_sm_type0_read(iolink_dll_main_t* ctx)
{
    dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
    return true;
}

static bool dll_sm_pd_exchange(iolink_dll_main_t* ctx)
{
    dll_handle_operate_type1_2(ctx);
    return true;
}

/** @brief OPERATE 中的过渡命令非法：计数但不改变 DL 模式（ST rc=2 语义）。 */
static bool dll_sm_transition_illegal(iolink_dll_main_t* ctx)
{
    ctx->shared.framing_errors++;
    return true;
}

/** @brief OPERATE 中的 Type-0 页读：复位请求（兼容开关除外）。 */
static bool dll_sm_operate_page_read(iolink_dll_main_t* ctx)
{
    if (!iolink_caps_has(ctx->shared.caps, IOLINK_CAP_COMPAT_T0_READ)) {
        /* Table 57/67（SDCI_TC_0038/0049）：不复位兼容模式下，OPERATE 期间
           的 Type-0 页读是复位请求：不应答，返回 STARTUP。 */
        if (ctx->shared.m_seq_type != IOLINK_M_SEQ_TYPE_0) {
            ctx->shared.framing_errors++;
            ctx->frame_index = 0U;
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            return false; /* 复位路径跳过公共尾部清理 */
        }
    }
    dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
    return true;
}

/** @brief state × event 动作表（唯一事实源：dll_sm.h 的 DLL_SM_ROWS）。 */
static const iolink_dll_sm_cell_t g_dll_sm[IOLINK_DLL_STATE_COUNT][DLL_EV_COUNT] = {
#define DLL_SM_ROW(state, event, action, enter, clause) [state][event] = {action, enter},
    DLL_SM_ROWS(DLL_SM_ROW)
#undef DLL_SM_ROW
};

const iolink_dll_sm_cell_t* IOLINK_NULLABLE iolink_dll_sm_cell(iolink_dll_state_t state,
                                                               iolink_dll_event_t event)
{
    if (((uint32_t) state >= (uint32_t) IOLINK_DLL_STATE_COUNT) ||
        ((uint32_t) event >= (uint32_t) DLL_EV_COUNT)) {
        return NULL;
    }
    return &g_dll_sm[(uint8_t) state][(uint8_t) event];
}

/**
 * @brief 按 DL 状态与事件执行状态机骨架。
 *
 * @return 当帧被拒绝且未推进状态、调用方必须跳过
 *         公共尾部清理时返回 false。
 */
bool dll_handle_valid_frame(iolink_dll_main_t* ctx)
{
    const bool was_establishing = (ctx->shared.state == IOLINK_DLL_STATE_AWAITING_COMM) ||
                                  (ctx->shared.state == IOLINK_DLL_STATE_STARTUP);
    if (!iolink_caps_has(ctx->shared.caps, IOLINK_CAP_COMPAT_V10_OP) &&
        dll_reject_startup_operate(ctx, was_establishing)) {
        return false;
    }
    (void) was_establishing;

    const iolink_dll_event_t event = dll_sm_classify(ctx);
    const iolink_dll_sm_cell_t* cell = &g_dll_sm[(uint8_t) ctx->shared.state][(uint8_t) event];
    if (cell->enter_state != DLL_SM_STATE_KEEP) {
        dll_set_state(ctx, (iolink_dll_state_t) cell->enter_state);
        if (cell->enter_state == (uint8_t) IOLINK_DLL_STATE_PREOPERATE) {
            /* 有效报文取消 T_DSIO（Table 47 T10 只约束首帧前）。 */
            dll_timer_cancel(ctx, IOLINK_TIMER_T_DSIO);
        }
    }
    return cell->action(ctx);
}

/** @brief 清除每帧簿记并刷新 ISR 的 ISDU 快照。 */
void dll_finish_frame(iolink_dll_main_t* ctx)
{
    ctx->frame_index = 0U;
    ctx->frame_replied_by_isr = 0U;
    /* 在下一主站帧之前，为 ISR 读泵反映任何 ISDU 状态变化
     * （例如完成的写请求）。 */
    dll_isdu_sync_snapshot(ctx);
}

void dll_process_complete_frame(iolink_dll_main_t* ctx)
{
    if (dll_frame_drop_if_stopped(ctx)) {
        return;
    }
    dll_check_cycle_time(ctx);

    /* 单字节“帧”不是 M-sequence：它是收发器在 SIO 中转发的
     * 唤醒字节（或线路噪声）。忽略它，而不是让校验和
     * 失败——那会读取不存在的第二个字节并驱动一次
     * 虚假回退。 */
    if (ctx->req_len < IOLINK_M_SEQ_HEADER_LEN) {
        ctx->frame_index = 0U;
        return;
    }

    /* ST 在帧结束时重启其回调定时器；一个完成的帧（即使是
       坏帧）证明链路仍被驱动。该帧由主上下文自己服务，因此
       Back-to-box 计数在这里直接推进；ISR 完全消费的帧经
       shared.isr_cycle_served 交接，由主泵在下一轮计数。 */
    dll_mark_master_activity_ts(ctx);
    dll_count_served_cycle(ctx);

    const dll_frame_result_t result = dll_frame_validate(ctx);
    if (result == DLL_FRAME_VALID) {
        /* 请求结束边沿：装载 t_A 的软件镜像（PHY Timer A 仍是权威）。
         * 应答开始（dll_send）或到期时取消。 */
        dll_timer_arm(ctx, IOLINK_TIMER_T_A,
                      dll_time_us(ctx) + (uint64_t) (IOLINK_T_A_MAX_BITS * dll_get_t_bit_us(ctx)));
        if (!dll_handle_valid_frame(ctx)) {
            return;
        }
    }
    else {
        dll_handle_frame_error(ctx, result);
        if (result == DLL_FRAME_TYPE_ERROR) {
            return; /* 类型错误路径自行清除簿记 */
        }
    }

    dll_finish_frame(ctx);
}
