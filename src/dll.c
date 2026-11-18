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
#include "iolinki/port.h"
#include "iolinki/internal/wiring.h"
#include "dll_internal.h"
#include "events_internal.h"
#include "isdu_internal.h"
#include "iolinki/internal/dll_sm.h"
#include <string.h>

/**
 * @file dll.c
 * @brief 数据链路层（DLL）上下文、时序表与主泵。
 * @ingroup iolinki_dll
 *
 * 驱动 IO-Link 设备 DLL：唤醒检测、模式与波特率管理、
 * STARTUP -> PREOPERATE -> OPERATE 状态机、时序强制、
 * 回退处理，以及把 Type-0/Type-1/Type-2 M-sequence
 * 分发到 ISDU 引擎。本文件持有 DL 状态、共享截止时间表
 * 与主泵入口；帧路径在 dll_frame.c，状态机骨架在
 * dll_sm.c，应答计划在 dll_reply.c，查询/设置入口在
 * dll_api.c。
 */

/* -------------------------------------------------------------------------
 * 文件结构与主泵调用树
 *
 *  - 状态转换与回调：dll_set_state、iolink_dll_flush_state_callbacks
 *  - 截止时间表：dll_timer_arm ... dll_timer_due
 *  - 生命周期：iolink_dll_init
 *  - ISR 交接与定时器分发：dll_take_isr_*、dll_timers_dispatch
 *  - 主泵：dll_process_body、iolink_dll_process
 *
 * 帧路径（dll_frame.c）、状态机表（dll_sm.c）、应答计划
 * （dll_reply.c）与公共 API（dll_api.c）是独立翻译单元。
 *
 * 主泵（主上下文；入口 iolink_dll_process）：
 *
 *   iolink_dll_process
 *   `- dll_process_body
 *      |- dll_take_isr_master_activity       消费 ISR 帧结束时间戳
 *      |- dll_apply_isr_consumed_frame       消费 CONSUME 帧的交接时间戳
 *      |                                     （补做 T_CYCLE/T_REN 记账）
 *      |- dll_apply_isr_deadline_requests    设置/清除 T_FBD 与 T_DSIO
 *      |                                     （64 位截止时间由主上下文持有）
 *      |                                     并消费 i_isr_cycle_served（Back-to-box 计数）
 *      |- iolink_isdu_process                应用 ISR 累计完成的写请求并执行 ISDU 服务
 *      |- dll_apply_isr_isdu_read_reset      消费 ISR 的 IDLE/ABORT 读复位请求
 *      |                                     （只复位读传输层）
 *      |- dll_isdu_sync_snapshot             发布 ISR 读取视图
 *      |- dll_poll_diagnostics
 *      |- dll_take_isr_master_activity       第二次采样：一帧可能已在上述步骤中
 *      |                                     完成
 *      |- dll_timer_sync_master_lost / dll_timer_sync_idle /
 *      |  dll_timers_dispatch(phase 0)        截止时间表
 *      |- dll_check_t_pd            -- 提前返回
 *      |- dll_handle_sio_wakeup     -- 提前返回
 *      |- dll_timers_dispatch(phase 1)   t_byte 到期检查
 *      |- dll_wakeup_window_active  -- 提前返回
 *      |- dll_consume_isr_mailbox
 *      |  `- dll_process_complete_frame
 *      |     |- dll_frame_drop_if_stopped / dll_check_cycle_time /
 *      |     |  dll_mark_master_activity_ts + dll_count_served_cycle
 *      |     |- dll_frame_validate -> dll_handle_valid_frame
 *      |     |  `- 状态机骨架表 g_dll_sm[state][event]（见 dll_sm.h）
 *      |     |     `- dll_handle_operate_type0 / dll_handle_type0_od_write /
 *      |     |        dll_handle_operate_type1_2 -> dll_dispatch_od
 *      |     |        （页面/诊断/ISDU 通道） -> dll_send
 *      |     |- dll_handle_frame_error
 *      |     `- dll_finish_frame （刷新 ISDU 快照）
 *
 * 修改主泵前值得了解的隐式结构：
 *  - dll_take_isr_master_activity() 故意运行两次（在 ISDU 服务之前和
 *    之后）。
 *  - 三个提前返回门控（t_pd、SIO 唤醒、唤醒窗口）是
 *    唯一跳过邮箱的路径；t_byte 的到期检查只做测量，
 *    从不提前返回。保持这些门控在排空之前。
 *  - 一个 eager 帧由 ISR 应答，*同时* 仍在此处处理：
 *    邮箱记录携带 eager=1，因此 dll_send() 被抑制，但状态
 *    机、PD-out 拷贝和计数器仍必须运行。
 *
 * 与数据 ISR 的交接（dll_isr.c）：
 *  - 主上下文 -> ISR：m_isr_tx_payload[0..1] + b_isr_tx_pending（dll_send）、ISDU
 *    发布字 isr_isdu_pub/resp_store（dll_isdu_sync_snapshot）、
 *    isdu.req_ack_gen/isdu.req_busy_sel（主泵对已发布写请求的 ack 与占用）
 *    以及 m_pd_pub（PD-in 发布字 `{active, valid, gen}`）。
 *  - ISR -> 主上下文：i_isr_frame_ring（dll_consume_isr_mailbox）、
 *    isdu.req_pub（双请求槽的写请求发布字）、i_isr_master_activity、
 *    i_isr_isdu_read_reset（IDLE/ABORT 读与写 ABORT 的传输层复位）、
 *    b_pd_valid_cmd（0x98/0x99 对 PD 有效位的本代覆盖）、
 *    i_isr_consumed_us/i_isr_consumed_seq（CONSUME 帧的 T_CYCLE/T_REN 记账）、
 *    i_isr_cycle_served（ISR 服务的 MasterCycle，Back-to-box 计数由主泵推进）
 *    以及截止时间标志。
 *
 * ISR 双泵传送层位于 dll_isr.c；完整上下文契约与缓冲区归属表见
 * docs/CODE_FLOW.md。
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * 截止时间表
 *
 * 所有软件超时共享一张静态表与一个 next_deadline。主泵正常路径只做
 * 一次“现在是否到达 next_deadline”的比较；装载/取消是边沿事件。
 * 时钟源唯一：全部 deadline 都由 dll_time_us() 换算成绝对微秒。
 * ------------------------------------------------------------------------- */

/** @brief 契约表：顺序 = @ref iolink_timer_id_t 顺序 = 同刻到期优先级。 */
static const iolink_timer_contract_t g_timer_contract[IOLINK_TIMER_COUNT] = {
    {"master_lost", "实现相关（ST Timer B 语义，master_lost_cycles）", IOLINK_TIMER_MASTER_LOST,
     IOLINK_TIMER_KIND_DEADLINE, 0U},
    {"idle_timeout", "实现诊断（1 s 静默链路，非规范超时）", IOLINK_TIMER_IDLE,
     IOLINK_TIMER_KIND_DEADLINE, 0U},
    {"t_dsio", "Table 47 T10（60..300 ms）", IOLINK_TIMER_T_DSIO, IOLINK_TIMER_KIND_DEADLINE, 0U},
    {"t_fbd", "Table 47 T8/T9；Table 43（3 × MasterCycleTime，≤500 ms）", IOLINK_TIMER_T_FBD,
     IOLINK_TIMER_KIND_DEADLINE, 0U},
    {"t_pd", "A.2 / Table 47（t_PD 上电与 PD 保护窗口）", IOLINK_TIMER_T_PD,
     IOLINK_TIMER_KIND_WINDOW, 0U},
    {"t_wu", "Table 47（T_WU 唤醒后首帧窗口）", IOLINK_TIMER_T_WU, IOLINK_TIMER_KIND_WINDOW, 0U},
    {"t_byte", "A.2 / Table 47（帧内字节间超时）", IOLINK_TIMER_T_BYTE, IOLINK_TIMER_KIND_DEADLINE,
     1U},
    {"t_ren", "C6 / Table 10（t_REN 应答时间上限）", IOLINK_TIMER_T_REN, IOLINK_TIMER_KIND_WINDOW,
     0U},
    {"t_cycle", "C5 / Table 47（MinCycleTime）", IOLINK_TIMER_T_CYCLE, IOLINK_TIMER_KIND_WINDOW,
     0U},
    {"t_a", "Table 47 / A.2（t_A = 10 位时间；PHY 定时器为权威）", IOLINK_TIMER_T_A,
     IOLINK_TIMER_KIND_WINDOW, 0U},
};

/** @brief 回绕安全的“已到达”判定：唯一允许做跨回绕减法的地方。 */
bool dll_time_reached(uint64_t now_us, uint64_t deadline_us)
{
    return (int64_t) (now_us - deadline_us) >= 0;
}

/** @brief 回绕安全的“已越过”判定（严格大于）。 */
bool dll_time_passed(uint64_t now_us, uint64_t deadline_us)
{
    return (int64_t) (now_us - deadline_us) > 0;
}

/**
 * @brief 回绕安全的“早于”判定：@p a 早于 @p b 返回 true。
 *
 * 与 @ref dll_time_reached 同源：只比较差值的符号位，因此跨 2^63 的
 * 绝对时间仍保持先后关系。相等时返回 false。
 */
static bool dll_time_before(uint64_t a, uint64_t b)
{
    return (int64_t) (a - b) < 0;
}

/** @brief 重算 next_deadline：只考虑 DEADLINE 类 timer。 */
static void dll_next_deadline_recompute(iolink_dll_main_t* ctx)
{
    uint64_t next = 0U;
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_TIMER_COUNT; i++) {
        const uint64_t deadline = ctx->timers[i].deadline;
        if ((deadline != 0U) && (g_timer_contract[i].kind == IOLINK_TIMER_KIND_DEADLINE) &&
            ((next == 0U) || dll_time_before(deadline, next))) {
            next = deadline;
        }
    }
    ctx->next_deadline = next;
}

void dll_timer_arm(iolink_dll_main_t* ctx, iolink_timer_id_t id, uint64_t deadline_us)
{
    if ((ctx == NULL) || ((uint32_t) id >= (uint32_t) IOLINK_TIMER_COUNT) || (deadline_us == 0U)) {
        return;
    }
    const uint8_t index = (uint8_t) id;
    const uint64_t previous = ctx->timers[index].deadline;
    ctx->timers[index].deadline = deadline_us;
    ctx->timers[index].id = index;
    if (g_timer_contract[index].kind != IOLINK_TIMER_KIND_DEADLINE) {
        return;
    }
    if ((ctx->next_deadline == 0U) || dll_time_before(deadline_us, ctx->next_deadline)) {
        ctx->next_deadline = deadline_us;
    }
    else if ((previous == ctx->next_deadline) && dll_time_passed(deadline_us, previous)) {
        /* 把表头推后：只有这种情况需要重新扫描。 */
        dll_next_deadline_recompute(ctx);
    }
}

void dll_timer_cancel(iolink_dll_main_t* ctx, iolink_timer_id_t id)
{
    if ((ctx == NULL) || ((uint32_t) id >= (uint32_t) IOLINK_TIMER_COUNT)) {
        return;
    }
    const uint8_t index = (uint8_t) id;
    const uint64_t previous = ctx->timers[index].deadline;
    ctx->timers[index].deadline = 0U;
    if ((previous != 0U) && (previous == ctx->next_deadline)) {
        dll_next_deadline_recompute(ctx);
    }
}

bool dll_timer_armed(const iolink_dll_main_t* ctx, iolink_timer_id_t id)
{
    return (ctx != NULL) && ((uint32_t) id < (uint32_t) IOLINK_TIMER_COUNT) &&
           (ctx->timers[(uint8_t) id].deadline != 0U);
}

uint64_t dll_timer_deadline(const iolink_dll_main_t* ctx, iolink_timer_id_t id)
{
    if ((ctx == NULL) || ((uint32_t) id >= (uint32_t) IOLINK_TIMER_COUNT)) {
        return 0U;
    }
    return ctx->timers[(uint8_t) id].deadline;
}

bool dll_timer_due(const iolink_dll_main_t* ctx, iolink_timer_id_t id, uint64_t now_us)
{
    const uint64_t deadline = dll_timer_deadline(ctx, id);
    return (deadline != 0U) && dll_time_reached(now_us, deadline);
}

const iolink_timer_contract_t* iolink_dll_timer_contract(uint8_t* IOLINK_NULLABLE count)
{
    if (count != NULL) {
        *count = (uint8_t) IOLINK_TIMER_COUNT;
    }
    return g_timer_contract;
}

/* -------------------------------------------------------------------------
 * 状态转换与回调
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_EVENTS
/**
 * @brief 应用数据 ISR 登记的事件复位请求（主上下文）。
 *
 * 事件引擎只有一个主上下文写者，ISR 迁入 STARTUP 时只置起复位请求
 * （`reset_req`）。这里在投递状态回调、以及任何事件/诊断写入之前应用它：
 * 请求与已应用标记不等期间，flag/memory 读侧已经把内容掩码为“无事件”，
 * 因此复位前的事件不会经 CKS 位或 Table 58 泄漏。
 */
static void dll_events_apply_isr_reset(iolink_dll_main_t* ctx)
{
    if (ctx->shared.b_events.reset_req != ctx->shared.b_events.reset_applied) {
        iolink_events_reset(&ctx->shared.b_events);
    }
}
#endif

/**
 * @brief 集中的状态转换。
 *
 * 立即更新协议状态。当转换发生在主上下文时，
 * 应用钩子会立刻收到通知（电平检查，无队列）；
 * 在数据 ISR 中进行的转换只设置状态，并由
 * 下一次主上下文 flush 投递。ISR 中进入 STARTUP 的转换会作为
 * 通知保留，以免快速的 STARTUP -> PREOPERATE 序列对应用隐藏
 * 协议复位。
 *
 * 两个上下文各有一个入口：数据 ISR 只调用 @ref dll_set_state_shared ，
 * 它只写共享区；主上下文调用本函数，额外维护主站丢失簿记、事件/ISDU
 * 复位并投递回调。
 */
void dll_set_state_shared(iolink_dll_shared_t* shared, iolink_dll_state_t new_state)
{
    if (shared->b_state == new_state) {
        return;
    }
    /* ISR 驱动的迁移只在显式启用 ISR 日志时输出。 */
    IOLINK_LOG_ISR_DEBUG(&shared->c_log, "dll state (isr) %u -> %u", (unsigned) shared->b_state,
                         (unsigned) new_state);
    /* 单次对齐存储：数据 ISR 可能随时读取该状态，且
     * 两个上下文都可能转换它（ISR 快速路径为 t_A 这么做）。 */
    shared->b_state = new_state;
    if (new_state == IOLINK_DLL_STATE_STARTUP) {
        /* A.6.4：排队的事件在通信被中断时会被丢弃，或被取消。事件引擎的
           写状态归主上下文，因此 ISR 侧的迁移只登记复位请求，由主泵在
           投递回调之前应用。条件表清空后，下一轮 PHY 采样会对仍存在的
           故障重新产生上升沿。 */
#if IOLINK_FEATURE_EVENTS
        iolink_events_reset_request(&shared->b_events);
#endif
        shared->i_startup_notification_pending = 1U;
        shared->i_isdu_restart_pending = 1U;
    }
}

void dll_set_state(iolink_dll_main_t* ctx, iolink_dll_state_t new_state)
{
    if (ctx->shared.b_state == new_state) {
        return;
    }
    IOLINK_LOG_INFO(&ctx->shared.c_log, "dll state %u -> %u", (unsigned) ctx->shared.b_state,
                    (unsigned) new_state);
    ctx->shared.b_state = new_state;
    if (new_state == IOLINK_DLL_STATE_OPERATE) {
        /* 主站丢失监控仅在 OPERATE 内有意义；
         * 启动序列可能合法地暂停数个周期。 */
        ctx->master_last_activity_ms = dll_time_ms(&ctx->shared);
        ctx->master_lost_count = 0U;
        ctx->master_lost = false;
    }
    if (new_state == IOLINK_DLL_STATE_STARTUP) {
        /* A.6.4：主上下文迁移立即清空事件引擎的写状态。 */
#if IOLINK_FEATURE_EVENTS
        iolink_events_reset(&ctx->shared.b_events);
#endif
        /* 主上下文重启：立即作废在途写并复位读传输层，避免重启前的
         * 应答视图在下一轮被重新发布。 */
        isdu_abandon_write(&ctx->shared.b_isdu);
        isdu_read_reset(&ctx->shared.b_isdu);
    }
    /* 主上下文：立即投递，因此发生在一次帧
     * 分发内的转换序列仍会逐状态上报。 */
    iolink_dll_flush_state_callbacks(ctx);
}

void iolink_dll_flush_state_callbacks(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
#if IOLINK_FEATURE_EVENTS
    /* ISR 侧的复位请求必须早于应用能看到任何事件内容。 */
    dll_events_apply_isr_reset(ctx);
#endif
    if (ctx->state_cb == NULL) {
        return;
    }
    /* 来自 ISR 的 STARTUP 转换是一个边沿，不能在主上下文
     * 观察到更晚的 PREOPERATE 状态时丢失。绝不在中断上下文中
     * 调用，因此应用钩子不会进入 ISR。 */
    if (ctx->shared.i_startup_notification_pending != 0U) {
        if (ctx->last_notified_state != IOLINK_DLL_STATE_STARTUP) {
            ctx->last_notified_state = IOLINK_DLL_STATE_STARTUP;
            ctx->state_cb(ctx->state_cb_user, IOLINK_DLL_STATE_STARTUP);
        }
        ctx->shared.i_startup_notification_pending = 0U;
    }

    /* 基于电平的通知：只投递当前状态一次。 */
    const iolink_dll_state_t state = ctx->shared.b_state;
    if (state == ctx->last_notified_state) {
        return;
    }
    ctx->last_notified_state = state;
    ctx->state_cb(ctx->state_cb_user, state);
}
/* -------------------------------------------------------------------------
 * 生命周期与 ISDU 快照
 * ------------------------------------------------------------------------- */

void iolink_dll_init(iolink_dll_main_t* ctx, iolink_dll_isr_t* isr, const iolink_phy_api_t* phy)
{
    if ((phy == NULL) || (isr == NULL) || (!iolink_ctx_zero(ctx, sizeof(iolink_dll_main_t))) ||
        (!dll_isr_reset(isr))) {
        return;
    }
    ctx->shared.c_isr = isr;
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->last_notified_state = IOLINK_DLL_STATE_STARTUP;
    ctx->shared.c_phy = phy;
    /* UART 速率由 PHY 在自己的 init 中按编译期 IOLINK_COM 配置。 */
    /* 先解析实例能力，初始线路模式依赖"PHY 能否观测唤醒"这一位。
     * 设备层随后会用完整的 stack/app 配置重新解析一次。 */
    ctx->shared.c_caps = iolink_caps_resolve(phy, NULL, NULL, NULL);
    ctx->enforce_timing = (iolink_caps_build()->timing_enforce_default != 0U);
    ctx->master_last_activity_ms = dll_time_ms(&ctx->shared);
    ctx->master_lost_cycles = 0U;
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;
    ctx->log_locked = false; /* 重新初始化允许按新配置绑定日志汇点 */
    ctx->master_lost_activity_ms = ctx->master_last_activity_ms;
    ctx->next_deadline = 0U;
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_TIMER_COUNT; i++) {
        ctx->timers[i].deadline = 0U;
        ctx->timers[i].id = i;
    }

    /* PD-in 初值：缓冲区 0、有效（应用第一次发布之前），没有命令覆盖。
     * 发布字与命令字此后分别由主上下文与数据 ISR 单写。 */
    iolink_atomic_store(&ctx->shared.m_pd_pub, IOLINK_PD_PUB_WORD(0U, true, 0U));
    iolink_atomic_store(&ctx->shared.b_pd_valid_cmd, 0U);
    ctx->shared.b_pd_in_captured = 0xFFU; /* 无进行中的应答 */

    /* 帧邮箱：ISR 发布到环中，主泵消费。 */
    iolink_ring_init(&ctx->shared.i_isr_frame_ring, (uint8_t*) ctx->isr_frame_slot,
                     IOLINK_MBX_SLOTS, (uint8_t) sizeof(iolink_frame_rec_t));

    DLL_EVENTS_INIT(&ctx->shared.b_events);
#if IOLINK_FEATURE_EVENTS
    /* 事件引擎唯一的 ISR 写入口是复位请求。绑定本上下文的 ISR 深度标志，
       使误从数据 ISR 调用的写入口被拒绝，而不是与主泵竞争。 */
    ctx->shared.b_events.isr_active = &ctx->shared.i_in_isr;
#endif
    iolink_isdu_init(&ctx->shared.b_isdu);
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_init(&ctx->ds);
#endif
#if IOLINK_FEATURE_EVENTS
    ctx->shared.b_isdu.event_ctx = &ctx->shared.b_events;
#else
    ctx->shared.b_isdu.event_ctx = NULL;
#endif
    ctx->shared.b_isdu.dll_ctx = ctx;
    /* 绑定上下文后立即按真实配置建立 DPP1 映像（否则首次 ISR 页读会看到
     * "失效"状态并回 0）。 */
    iolink_isdu_dp_page1_invalidate(&ctx->shared.b_isdu);
#if IOLINK_FEATURE_DATA_STORAGE
    ctx->shared.b_isdu.ds_ctx = &ctx->ds;
#else
    ctx->shared.b_isdu.ds_ctx = NULL;
#endif

    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);

    /*
     * 初始线路模式取决于 PHY 能否观测到电气
     * C/Q 唤醒脉冲：
     *
     *  - 能观测到的 PHY（detect_wakeup != NULL，例如收发器前端或
     *    虚拟 PHY）从 SIO 开始，并在检测到唤醒时切换到 SDCI，
     *    与 IO-Link 启动握手一致。
     *
     *  - 不能观测到的 PHY（detect_wakeup == NULL，例如位于已建立 COM 的
     *    收发器之后的普通 UART）否则会
     *    永远停在 SIO 且从不处理 M-sequence。按约定，
     *    这样的链路属于已建立的 SDCI 链路，因此从 SDCI 开始并
     *    立即接受 M-sequence。
     */
    if (dll_phy_has_wakeup(&ctx->shared)) {
        iolink_dll_set_sio_mode(ctx);
    }
    else {
        iolink_dll_set_sdci_mode(ctx);
    }
    /* 初始状态（STARTUP）的计划：第一个应答由 ISR 提前给出。 */
    (void) iolink_dll_publish_reply_plans(ctx);
}

/** @brief 消费 ISR 主站活动标志（仅时间戳，不计周期数）。 */
static void dll_take_isr_master_activity(iolink_dll_main_t* ctx)
{
    if (ctx->shared.i_isr_master_activity != 0U) {
        ctx->shared.i_isr_master_activity = 0U;
        /* 只刷新活动时间戳。Back-to-box 计数由主上下文独占推进：
         * ISR 应答的帧经 i_isr_cycle_served 交接，主上下文应答的帧在
         * dll_process_complete_frame 中直接推进。 */
        dll_mark_master_activity_ts(ctx);
    }
}

/**
 * @brief 取 ISR 完全消费（CONSUME 计划）帧的交接时间戳。
 *
 * 单写者交接：ISR 先写 32 位时间戳，穿过 IOLINK_FENCE() 后再自增序号作为
 * 发布（release）边沿。这里做 seq -> fence -> us -> fence -> seq 双读
 * （acquire），只有两次序号相同才采纳时间戳；序号已变化说明 ISR 在读取
 * 过程中又发布了一帧，重试（最多 3 次），失败则留给下一轮。
 *
 * @param ctx      DLL 上下文
 * @param[out] stamp_us ISR 记录的帧结束时间低 32 位
 * @return 有新交接时返回 true
 */
static bool dll_take_isr_consumed_frame(iolink_dll_main_t* ctx, uint32_t* stamp_us)
{
    bool taken = false;
    for (uint8_t attempt = 0U; (attempt < 3U) && !taken; attempt++) {
        const uint8_t seq = ctx->shared.i_isr_consumed_seq;
        IOLINK_FENCE(); /* 获取：fence 之后的读取必须看到该序号对应的数据 */
        if (seq == ctx->isr_consumed_seq_seen) {
            break; /* 没有新帧 */
        }
        const uint32_t stamp = ctx->shared.i_isr_consumed_us;
        IOLINK_FENCE(); /* 复读序号前的屏障：序号变化必须能被看见 */
        if (ctx->shared.i_isr_consumed_seq == seq) {
            ctx->isr_consumed_seq_seen = seq;
            *stamp_us = stamp;
            taken = true;
        }
        /* 否则：读取期间 ISR 又消费了一帧，重读（最多 3 次）。 */
    }
    return taken;
}

/**
 * @brief 把 ISR 消费帧的交接时间戳折算成主泵的 T_CYCLE/T_REN 记账。
 *
 * CONSUME 帧不进邮箱，主泵看不到它们，但每个 MasterCycle 都必须经过
 * MinCycleTime/T_REN 检查并刷新 `last_response_us`。32 位时间戳用无符号
 * 差值重建 64 位绝对时间：`now - (uint32_t) (now - stamp)`。该重建依赖
 * "主泵两次运行间隔 < 2^31 us"（约 35 分钟），与 `b_last_byte_us` 的既有
 * uint32 假设同级；超时未运行主泵时链路本就已按主站丢失处理。
 */
static void dll_apply_isr_consumed_frame(iolink_dll_main_t* ctx)
{
    uint32_t stamp = 0U;
    if (!dll_take_isr_consumed_frame(ctx, &stamp)) {
        return;
    }
    const uint64_t now_us = dll_time_us(&ctx->shared);
    const uint64_t frame_us = now_us - (uint64_t) ((uint32_t) now_us - stamp);
    dll_check_cycle_time_at(ctx, frame_us);
    ctx->last_response_us = frame_us;
}

/**
 * @brief 应用数据 ISR 仅作标记的请求（截止时间与 Back-to-box 计数）。
 *
 * 两个 64 位截止时间由主上下文持有，因此绝不从
 * 中断上下文写入 64 位值（撕裂读会触发虚假的 T_DSIO/T_FBD）。
 * ISR 设置标志；主泵在同一轮的任何截止时间检查之前，
 * 在此设置或清除截止时间。`i_isr_cycle_served` 同样在此消费：
 * Back-to-box 的周期计数归主上下文独占，ISR 只置一次性标志。
 */
static void dll_apply_isr_deadline_requests(iolink_dll_main_t* ctx)
{
    if (ctx->shared.i_isr_fallback_pending != 0U) {
        ctx->shared.i_isr_fallback_pending = 0U;
        /* 仅在 DL 模式仍为 FALLBACK 时设置：后续命令可能
         * 已离开该模式，此时 T_FBD 截止时间将是虚假的。 */
        if (ctx->shared.b_state == IOLINK_DLL_STATE_FALLBACK) {
            /* Table 47 T8/T9：T_FBD 为 3 个 MasterCycleTime，上限 500 ms。
             * 参考点是本次主上下文轮询，比帧结束晚一轮；
             * 这只会延长 T_FBD（绝不缩短它），
             * 至多一个主循环延迟。 */
            uint32_t t_fbd_ms = IOLINK_T_FBD_MAX_MS;
            const uint32_t cycle_us = dll_master_cycle_us(ctx);
            if (cycle_us > 0U) {
                t_fbd_ms = (IOLINK_T_FBD_MASTER_CYCLES * cycle_us) / 1000U;
                if (t_fbd_ms > IOLINK_T_FBD_MAX_MS) {
                    t_fbd_ms = IOLINK_T_FBD_MAX_MS;
                }
            }
            /* 绝对截止时间仍按毫秒基准换算，与原比较（now_ms >= deadline_ms）逐拍一致。 */
            dll_timer_arm(ctx, IOLINK_TIMER_T_FBD,
                          (uint64_t) (dll_time_us(&ctx->shared) / 1000U + t_fbd_ms) * 1000U);
        }
    }
    if (ctx->shared.b_fallback_clear_request != 0U) {
        ctx->shared.b_fallback_clear_request = 0U;
        /* 已应用非 FALLBACK 的命令：丢弃任何待处理的 T_FBD。 */
        if (ctx->shared.b_state != IOLINK_DLL_STATE_FALLBACK) {
            dll_timer_cancel(ctx, IOLINK_TIMER_T_FBD);
        }
    }
    if (ctx->shared.i_isr_dsio_clear != 0U) {
        ctx->shared.i_isr_dsio_clear = 0U;
        dll_timer_cancel(ctx, IOLINK_TIMER_T_DSIO);
    }
    if (ctx->shared.i_isr_cycle_served != 0U) {
        /* ISR 完全服务了一个 MasterCycle：计数归主上下文（ISR 只置标志），
         * 一个泵周期最多推进一次。
         * 后续的 ISDU 服务与邮箱消费都在此之后运行，因此本轮的
         * Back-to-box 停止判定能看到这次推进。 */
        ctx->shared.i_isr_cycle_served = 0U;
        dll_count_served_cycle(ctx);
    }
}

/**
 * @brief 应用数据 ISR 的 ISDU 读传输层复位请求。
 *
 * 复位只经 `isdu_read_reset()` 清空读方向的字段（`state`/`header`/应答
 * 缓冲/错误码）。写入口（累计槽、`req_idx`/`req_total`、`stage_state`、
 * 发布字）归数据 ISR，主泵从不改写，所以复位本身不会踩掉 ISR 刚建立的
 * 新写请求；只有旗标的读-清需要临界区（见下）。
 */
static void dll_apply_isr_isdu_read_reset(iolink_dll_main_t* ctx)
{
    /* 快路径：没有请求时不取锁。旗标仍是双向的（ISR 置 1、主泵清 0），
     * 有请求时读-清必须在同一临界区内，否则清 0 会吞掉刚好在两条指令
     * 之间到达的复位请求，主站 ABORT/IDLE 之后的下一次读就会重新拿到
     * 上一次的应答。 */
    if (ctx->shared.i_isr_isdu_read_reset == 0U) {
        return;
    }
    IOLINK_CRITICAL_ENTER();
    if (ctx->shared.i_isr_isdu_read_reset != 0U) {
        ctx->shared.i_isr_isdu_read_reset = 0U;
        isdu_read_reset(&ctx->shared.b_isdu);
    }
    IOLINK_CRITICAL_EXIT();
}

/** @brief 同步主站丢失 timer：活动边沿推进截止时间，丢失后保持直到新活动。
 *
 * 周期时间未知（DPP1 MasterCycleTime 与 MinCycleTime 都为 0）时不武装：
 * master_lost_cycles 无法折算成截止时间，虚构一个周期会制造假丢失。
 * 此时取消表项并清掉丢失提示，等待周期时间可知后的下一次同步。
 */
static void dll_timer_sync_master_lost(iolink_dll_main_t* ctx)
{
    const bool was_lost = ctx->master_lost;
    const uint32_t cycle_us = dll_master_cycle_us(ctx);
    if ((ctx->shared.b_state == IOLINK_DLL_STATE_OPERATE) && (ctx->master_lost_cycles != 0U) &&
        (cycle_us != 0U)) {
        if (ctx->master_lost && (ctx->master_lost_activity_ms == ctx->master_last_activity_ms)) {
            return; /* 已判定丢失且无新活动：保持，不重复装载 */
        }
        uint32_t cycle_ms = (cycle_us + 999U) / 1000U;
        if (cycle_ms == 0U) {
            cycle_ms = 1U;
        }
        if (ctx->master_lost) {
            /* 新活动边沿：ST StartCallbackTimer 清零计数。 */
            ctx->master_lost_count = 0U;
            ctx->master_lost = false;
        }
        ctx->master_lost_activity_ms = ctx->master_last_activity_ms;
        const uint64_t deadline = ((uint64_t) ctx->master_last_activity_ms +
                                   ((uint64_t) ctx->master_lost_cycles * (uint64_t) cycle_ms)) *
                                  1000U;
        dll_timer_arm(ctx, IOLINK_TIMER_MASTER_LOST, deadline);
    }
    else {
        /* 离开 OPERATE、未配置周期数，或周期时间尚不可知：不检测。 */
        dll_timer_cancel(ctx, IOLINK_TIMER_MASTER_LOST);
        ctx->master_lost_count = 0U;
        ctx->master_lost = false;
    }

    if ((ctx->master_lost != was_lost) && !ctx->master_lost) {
        IOLINK_LOG_INFO(&ctx->shared.c_log, "master online");
    }
}

/** @brief MASTER_LOST 到期：按 ST 语义饱和计数并置位提示（不改状态/波特率）。 */
static void dll_timer_expire_master_lost(iolink_dll_main_t* ctx)
{
    dll_timer_cancel(ctx, IOLINK_TIMER_MASTER_LOST);
    if ((ctx->shared.b_state != IOLINK_DLL_STATE_OPERATE) || (ctx->master_lost_cycles == 0U)) {
        return;
    }
    const uint32_t cycle_us = dll_master_cycle_us(ctx);
    if (cycle_us == 0U) {
        /* 周期时间未知时本 timer 不会武装；到期说明配置刚被撤掉，
         * 直接放弃本次判定，不虚构周期。 */
        return;
    }
    uint32_t cycle_ms = (cycle_us + 999U) / 1000U;
    if (cycle_ms == 0U) {
        cycle_ms = 1U;
    }
    uint32_t elapsed = dll_time_ms(&ctx->shared) - ctx->master_last_activity_ms;
    uint32_t missed = elapsed / cycle_ms;
    if (missed > 0xFFU) {
        missed = 0xFFU;
    }
    const bool was_lost = ctx->master_lost;
    ctx->master_lost_count = (uint8_t) missed;
    ctx->master_lost = (ctx->master_lost_count >= ctx->master_lost_cycles);
    if (ctx->master_lost && !was_lost) {
        IOLINK_LOG_WARN(&ctx->shared.c_log, "master lost: %u missed cycles",
                        (unsigned) ctx->master_lost_count);
    }
}

/** @brief 同步 1 s 空闲诊断 timer（活动时间戳由主/ISR 共同推进）。 */
static void dll_timer_sync_idle(iolink_dll_main_t* ctx)
{
    const uint32_t b_last_activity_ms = ctx->shared.b_last_activity_ms;
    if (b_last_activity_ms == 0U) {
        dll_timer_cancel(ctx, IOLINK_TIMER_IDLE);
        return;
    }
    /* 原条件为 now_ms - last > 1000（32 位无符号差值）。这里用同一差值
     * 换算绝对微秒截止时间，跨 49 天毫秒回绕仍成立。 */
    const uint64_t now_us = dll_time_us(&ctx->shared);
    const uint32_t elapsed = (uint32_t) (now_us / 1000U) - b_last_activity_ms;
    const uint64_t deadline =
        (elapsed > 1000U) ? now_us : now_us + (uint64_t) (1001U - elapsed) * 1000U;
    dll_timer_arm(ctx, IOLINK_TIMER_IDLE, deadline);
}

/**
 * @brief 1 s 空闲链路诊断计数器。
 *
 * 这不是 ST 风格的主站丢失提示；它不会离开当前 DL
 * 模式，因此迟到的主站可在现有模式/波特率上恢复。
 */
static void dll_timer_expire_idle(iolink_dll_main_t* ctx)
{
    dll_timer_cancel(ctx, IOLINK_TIMER_IDLE);
    /* 静默链路不是响应时间违规：单独计数，使
     * timeout_errors 仍是真正的 t_A 指标。 */
    ctx->idle_timeouts++;
    ctx->shared.b_last_activity_ms = dll_time_ms(&ctx->shared);
}

/**
 * @brief Table 47 T10：唤醒后在 T_DSIO 内没有有效报文会使
 * 设备返回 SIO。仅在仍等待第一条报文时适用。
 */
static void dll_timer_expire_dsio(iolink_dll_main_t* ctx)
{
    dll_timer_cancel(ctx, IOLINK_TIMER_T_DSIO);
    if ((ctx->phy_mode == IOLINK_PHY_MODE_SIO) ||
        ((ctx->shared.b_state != IOLINK_DLL_STATE_AWAITING_COMM) &&
         (ctx->shared.b_state != IOLINK_DLL_STATE_STARTUP))) {
        return;
    }
    ctx->shared.b_timeout_errors++;
    iolink_dll_set_sio_mode(ctx);
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->frame_index = 0U;
}

/**
 * @brief Table 47 T8/T9 + Table 43：收到 FALLBACK MasterCommand 后，设备
 * 在 T_FBD（3 个 MasterCycleTime，最多 500 ms）后切换到 SIO。
 * 应答已在处理命令时发送。
 */
static void dll_timer_expire_fbd(iolink_dll_main_t* ctx)
{
    dll_timer_cancel(ctx, IOLINK_TIMER_T_FBD);
    if ((ctx->shared.b_state != IOLINK_DLL_STATE_FALLBACK) &&
        (ctx->shared.b_state != IOLINK_DLL_STATE_OPERATE) &&
        (ctx->shared.b_state != IOLINK_DLL_STATE_ESTAB_COM) &&
        (ctx->shared.b_state != IOLINK_DLL_STATE_PREOPERATE)) {
        return;
    }
    IOLINK_LOG_WARN(&ctx->shared.c_log, "fallback timeout: switching to SIO");
    iolink_dll_set_sio_mode(ctx);
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->frame_index = 0U;
}

/**
 * @brief 强制执行 t_PD 保护窗口。
 *
 * @return 当保护有效且调用方必须直接返回、不处理
 *         邮箱帧时返回 true。
 */
static bool dll_check_t_pd(iolink_dll_main_t* ctx)
{
    if (!dll_t_pd_active(ctx)) {
        return false;
    }
    if (dll_drain_rx(ctx)) {
        ctx->timing_errors++;
        ctx->t_pd_violations++;
        DLL_DIAG_PULSE(&ctx->shared.b_events, IOLINK_DIAG_SRC_COMM_TIMING);
    }
    return true;
}

/**
 * @brief SIO：检测 C/Q 唤醒并启动通信窗口。
 *
 * @return 当 DLL 处于（或保持）SIO 模式且调用方必须
 *         返回时返回 true。
 */
static bool dll_handle_sio_wakeup(iolink_dll_main_t* ctx)
{
    if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
        return false;
    }
    if ((ctx->frame_index == 0U) && dll_phy_has_wakeup(&ctx->shared)) {
        if (dll_phy_detect_wakeup(&ctx->shared) > 0) {
            dll_set_state(ctx, IOLINK_DLL_STATE_AWAITING_COMM);
            const uint64_t now_us = dll_time_us(&ctx->shared);
            dll_timer_arm(ctx, IOLINK_TIMER_T_WU, now_us + IOLINK_T_WU_US);
            /* Table 47 T10：没有有效报文时，设备在 T_DSIO 内
               （60..300 ms，默认 300 ms）返回 SIO。原条件为
               now_ms > deadline_ms：换算成“下一毫秒的起点”。 */
            dll_timer_arm(ctx, IOLINK_TIMER_T_DSIO,
                          ((uint64_t) (now_us / 1000U) + IOLINK_T_DSIO_MS + 1U) * 1000U);
            iolink_dll_set_sdci_mode(ctx);
            /* 唤醒会开启一个新的通信建立窗口。
               重置不活动计时器，使先前交换后的再次唤醒
               不会被陈旧的活动时间戳立刻终止。 */
            ctx->shared.b_last_activity_ms = dll_time_ms(&ctx->shared);
            dll_mark_master_activity_ts(ctx); /* ST StackOnWakeUp 重启 Timer B */
        }
    }
    return true;
}

/** @brief ST Timer A：帧内字节间隙会中止该帧并重新监听。 */
static void dll_timer_sync_t_byte(iolink_dll_main_t* ctx)
{
    const uint32_t b_last_byte_us = ctx->shared.b_last_byte_us;
    if ((ctx->frame_index == 0U) || (!ctx->enforce_timing) || (ctx->t_byte_limit_us == 0U) ||
        (b_last_byte_us == 0U)) {
        dll_timer_cancel(ctx, IOLINK_TIMER_T_BYTE);
        return;
    }
    const uint64_t now_us = dll_time_us(&ctx->shared);
    const uint32_t elapsed = (uint32_t) now_us - b_last_byte_us;
    uint64_t deadline;
    if (elapsed > ctx->t_byte_limit_us) {
        deadline = now_us; /* 已超限：本次分发立即到期 */
    }
    else {
        /* 原条件为 elapsed > limit：多等 1 us 保持严格大于语义。 */
        deadline = now_us + (uint64_t) (ctx->t_byte_limit_us - elapsed) + 1U;
    }
    dll_timer_arm(ctx, IOLINK_TIMER_T_BYTE, deadline);
}

static void dll_timer_expire_t_byte(iolink_dll_main_t* ctx)
{
    dll_timer_cancel(ctx, IOLINK_TIMER_T_BYTE);
    IOLINK_LOG_DEBUG(
        &ctx->shared.c_log, "t_byte violation: %lu us",
        (unsigned long) ((uint32_t) (dll_time_us(&ctx->shared) - ctx->shared.b_last_byte_us)));
    ctx->timing_errors++;
    ctx->t_byte_violations++;
    ctx->shared.b_framing_errors++;
    DLL_DIAG_PULSE(&ctx->shared.b_events, IOLINK_DIAG_SRC_COMM_TIMING);
    /* 中止当前帧并重新监听；DL 模式不变。 */
    ctx->frame_index = 0U;
}

/** @brief T_WU：在唤醒窗口内继续等待第一条报文。 */
static bool dll_wakeup_window_active(iolink_dll_main_t* ctx)
{
    if ((ctx->shared.b_state == IOLINK_DLL_STATE_AWAITING_COMM) && (ctx->enforce_timing)) {
        return dll_timer_armed(ctx, IOLINK_TIMER_T_WU) &&
               !dll_timer_due(ctx, IOLINK_TIMER_T_WU, dll_time_us(&ctx->shared));
    }
    return false;
}

/**
 * @brief 将 ISR 发布的帧移入主上下文并分发。
 *
 * 帧通过 SPSC 邮箱环到达：拷贝最旧的记录，
 * 释放其槽位，然后运行校验/分发。无需临界区
 * （ISR 从不写入消费者正在读取的槽位），
 * 且记录携带自身的长度与 eager 标志。
 */
static void dll_consume_isr_mailbox(iolink_dll_main_t* ctx)
{
    const iolink_frame_rec_t* record =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx->shared.i_isr_frame_ring);
    if (record == NULL) {
        return;
    }
    uint8_t count = record->len;
    if (count > sizeof(ctx->frame_buf)) {
        count = (uint8_t) sizeof(ctx->frame_buf);
    }
    (void) memcpy(ctx->frame_buf, record->data, count);
    ctx->frame_index = count;
    ctx->req_len = count;
    /* eager 随记录一起携带，因此无需跨帧快照。 */
    ctx->frame_replied_by_isr = record->eager;
    /* 拷贝后释放：ISR 随后可复用该槽位，而另一个
     * 槽位保持完整。无需临界区，因为 ISR 从不
     * 写入 'out' 处的槽位。 */
    iolink_ring_release(&ctx->shared.i_isr_frame_ring);
    /* 邮箱帧是接收流量：刷新不活动时间戳和字节间隙
       时间戳，与流式 RX 路径完全一致。否则 1 s 空闲
       监控会复位健康的 ISR 链路。 */
    ctx->shared.b_last_activity_ms = dll_time_ms(&ctx->shared);
    ctx->shared.b_last_byte_us = (uint32_t) dll_time_us(&ctx->shared);
    dll_process_complete_frame(ctx);
}

/**
 * @brief 分发一个阶段的到期 timer。
 *
 * 正常路径只做一次 `now >= next_deadline` 比较；只有跨过截止时间才按
 * 表序（= 同刻优先级）扫描本阶段表项。WINDOW 类 timer 不参与分发，
 * 由各自的边沿检查比较。
 */
static void dll_timers_dispatch(iolink_dll_main_t* ctx, uint8_t phase)
{
    if (ctx->next_deadline == 0U) {
        return;
    }
    const uint64_t now_us = dll_time_us(&ctx->shared);
    if (!dll_time_reached(now_us, ctx->next_deadline)) {
        return;
    }
    for (uint8_t i = 0U; i < (uint8_t) IOLINK_TIMER_COUNT; i++) {
        if ((g_timer_contract[i].kind != IOLINK_TIMER_KIND_DEADLINE) ||
            (g_timer_contract[i].phase != phase)) {
            continue;
        }
        if (!dll_timer_due(ctx, (iolink_timer_id_t) i, now_us)) {
            continue;
        }
        switch ((iolink_timer_id_t) i) {
            case IOLINK_TIMER_MASTER_LOST:
                dll_timer_expire_master_lost(ctx);
                break;
            case IOLINK_TIMER_IDLE:
                dll_timer_expire_idle(ctx);
                break;
            case IOLINK_TIMER_T_DSIO:
                dll_timer_expire_dsio(ctx);
                break;
            case IOLINK_TIMER_T_FBD:
                dll_timer_expire_fbd(ctx);
                break;
            case IOLINK_TIMER_T_BYTE:
                dll_timer_expire_t_byte(ctx);
                break;
            /* WINDOW 类表项由边沿检查处理，没有到期动作；枚举项全部
             * 列出，新增定时器时必须在这里显式选择。 */
            case IOLINK_TIMER_T_PD:
            case IOLINK_TIMER_T_WU:
            case IOLINK_TIMER_T_REN:
            case IOLINK_TIMER_T_CYCLE:
            case IOLINK_TIMER_T_A:
            case IOLINK_TIMER_COUNT:
            default:
                break;
        }
    }
    /* 到期动作可能取消或重装表项：统一重算一次表头。 */
    dll_next_deadline_recompute(ctx);
}

/** @brief 主泵主体；公共入口随后投递状态。 */
static void dll_process_body(iolink_dll_main_t* ctx)
{
#if IOLINK_FEATURE_EVENTS
    /* ISR 迁入 STARTUP 只登记了复位请求：在诊断轮询（会重新置位条件）
       之前应用，使事件引擎的写状态始终只属于本上下文。 */
    dll_events_apply_isr_reset(ctx);
#endif
    if (ctx->shared.i_isr_device_startup_requested != 0U) {
        /* 0x97 DeviceStartup 由 ISR 只登记请求（应答已按计划发出）：
         * 协议错误记账与 STARTUP 迁移都在主上下文中完成。 */
        ctx->shared.i_isr_device_startup_requested = 0U;
        dll_protocol_error_to_startup(ctx);
    }
    dll_take_isr_master_activity(ctx);
    dll_apply_isr_consumed_frame(ctx);
    dll_apply_isr_deadline_requests(ctx);

    if (ctx->shared.i_isdu_restart_pending != 0U) {
        /* ISR 触发的重启：未收齐的写请求作废（已收齐的仍由本轮处理），
         * 读传输层回到 Idle——否则重启前的 RESPONSE_READY 视图会被重新
         * 发布，主站新一轮的读 START 拿到上一轮的应答。 */
        ctx->shared.i_isdu_restart_pending = 0U;
        isdu_abandon_write(&ctx->shared.b_isdu);
        isdu_read_reset(&ctx->shared.b_isdu);
    }
    iolink_isdu_process(&ctx->shared.b_isdu);
    /* ISR 对 IDLE/IDLE2/ABORT 读只回 0x00；读传输层复位归主泵。
     * Type-0 下该帧由 CONSUME 计划完全消费，主泵只能经此标志收到请求。 */
    dll_apply_isr_isdu_read_reset(ctx);
    /* 服务可能已在此完成：刷新 ISR 响应快照。 */
    dll_isdu_sync_snapshot(ctx);
    dll_poll_diagnostics(ctx);
    /* 在上述步骤期间，ISR 中可能已完成一帧。 */
    dll_take_isr_master_activity(ctx);
    dll_apply_isr_consumed_frame(ctx);

    /* 在延迟活动刷新之后采样，使下面的经过时间减法
     * 不会看到比 now_ms 更新的时间戳。单次时钟采样保持
     * 毫秒与微秒视图一致。
     */
    dll_timer_sync_master_lost(ctx);
    dll_timer_sync_idle(ctx);
    /* t_byte 的 re-arm/cancel 与门控无关；提前同步，避免提前返回时留下
     * 已到期的表项把 next_deadline 卡在过去。到期动作仍在 phase 1 执行。 */
    dll_timer_sync_t_byte(ctx);
    dll_timers_dispatch(ctx, 0U);

    if (dll_check_t_pd(ctx)) {
        return;
    }
    if (dll_handle_sio_wakeup(ctx)) {
        return;
    }
    dll_timers_dispatch(ctx, 1U);
    if (dll_wakeup_window_active(ctx)) {
        return;
    }

    dll_consume_isr_mailbox(ctx);
}

void iolink_dll_process(iolink_dll_main_t* ctx)
{
    if (ctx->shared.c_phy == NULL) {
        return;
    }
    dll_process_body(ctx);
    /* 应答计划不在这里重发布：它是 {M-sequence 类型, OD 宽度, 能力位} 的
     * 纯函数，三项都只在初始化期间定义，各状态的差异由计划表的**状态切片**
     * 表达。运行期重编译只会重写同样的内容，并让 ISR 冒读到半更新表的风险。 */
    /* 基于电平的应用通知：状态钩子对设备层和直接 DLL
     * 用户的唯一运行位置。设备层在其事件/DS 服务之后
     * 不再 flush。 */
    iolink_dll_flush_state_callbacks(ctx);
}

uint8_t iolink_dll_take_service_window(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    /* 消费实现与 ISR 状态放在 dll_isr.c：主上下文只传共享区，
     * 因此本翻译单元仍看不到 ISR 独占字段。 */
    return dll_service_window_take(&ctx->shared);
}
