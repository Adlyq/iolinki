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

#include <stdio.h>

/**
 * @file dll.c
 * @brief 数据链路层（DLL）：M-sequence 状态机与成帧。
 * @ingroup iolinki_dll
 *
 * 驱动 IO-Link 设备 DLL：唤醒检测、模式与波特率管理、
 * 逐帧接收与 CRC 校验、STARTUP -> PREOPERATE ->
 * OPERATE 状态机、时序强制、回退处理，以及把
 * Type-0/Type-1/Type-2 M-sequence 分发到 ISDU 引擎。
 */

/* -------------------------------------------------------------------------
 * 文件结构与主泵调用树
 *
 *  - 状态转换与回调：dll_queue_state ... dll_set_state
 *  - M-sequence 类型判定与发送：dll_mseq_type_bits ... dll_send
 *  - 时序、主站命令与 OPERATE 处理：dll_get_t_ren_limit_us ...
 *    dll_handle_operate_type1_2
 *  - 诊断轮询：dll_poll_diagnostics
 *  - 生命周期与 ISDU 快照：iolink_dll_init ...
 *  - 主泵：dll_frame_drop_if_stopped ... iolink_dll_process
 *  - 公共配置与查询 API：iolink_dll_get_state ...
 *
 * 主泵（主上下文；入口 iolink_dll_process）：
 *
 *   iolink_dll_process
 *   `- dll_process_body
 *      |- dll_take_isr_master_activity       消费 ISR 帧结束时间戳
 *      |- dll_apply_isr_deadline_requests    设置/清除 T_FBD 与 T_DSIO
 *      |                                     （64 位截止时间由主上下文持有）
 *      |- [TX FRAME] pd_in 租约释放          在临界区中轮询 tx_busy
 *      |- dll_restore_operating_baudrate     撤销恢复到 COM1 的降级
 *      |- dll_drain_staged_isdu_write        排空 isr_od_ring -> ISDU 传送层
 *      |- iolink_isdu_process                执行排队的 ISDU 服务
 *      |- dll_isdu_sync_snapshot             发布 ISR 读取视图
 *      |- dll_poll_diagnostics
 *      |- dll_take_isr_master_activity       第二次采样：一帧可能已在上述步骤中
 *      |                                     完成
 *      |- dll_update_master_lost / dll_check_idle_timeout /
 *      |  dll_check_dsio_deadline / dll_check_fallback_deadline
 *      |- dll_check_t_pd            -- 提前返回
 *      |- dll_handle_sio_wakeup     -- 提前返回
 *      |- dll_check_t_byte
 *      |- dll_wakeup_window_active  -- 提前返回
 *      |- dll_consume_isr_mailbox
 *      |  `- dll_process_complete_frame
 *      |     |- dll_frame_drop_if_stopped / dll_check_cycle_time /
 *      |     |  dll_mark_master_activity
 *      |     |- dll_frame_validate -> dll_handle_valid_frame
 *      |     |  `- 状态分发：dll_dispatch_preoperate /
 *      |     |     dll_dispatch_estab_com / dll_dispatch_operate
 *      |     |     `- dll_handle_operate_type0 / dll_handle_type0_od_write /
 *      |     |        dll_handle_operate_type1_2 -> dll_dispatch_od
 *      |     |        （页面/诊断/ISDU 通道） -> dll_send
 *      |     |- dll_handle_frame_error
 *      |     `- dll_finish_frame （刷新 ISDU 快照）
 *      `- dll_restore_operating_baudrate      撤销同一次调用中的 COM1 降级
 *
 * 修改主泵前值得了解的隐式结构：
 *  - dll_take_isr_master_activity() 故意运行两次（在 ISDU 服务之前和
 *    之后）；dll_restore_operating_baudrate() 同样（在提前返回之前和
 *    邮箱排空之后）。
 *  - 三个提前返回门控（t_pd、SIO 唤醒、唤醒窗口）是
 *    唯一跳过邮箱的路径；dll_check_t_byte() 只做测量，
 *    从不提前返回。保持这些门控在排空之前。
 *  - 一个 eager 帧由 ISR 应答，*同时* 仍在此处处理：
 *    邮箱记录携带 eager=1，因此 dll_send() 被抑制，但状态
 *    机、PD-out 拷贝和计数器仍必须运行。
 *
 * 与数据 ISR 的交接（dll_isr.c）：
 *  - 主上下文 -> ISR：isr_tx_payload[0..1] + isr_tx_pending（dll_send）、ISDU
 *    发布字 isr_isdu_pub/resp_store（dll_isdu_sync_snapshot）以及
 *    pd_in_active（PD-in 发布）。
 *  - ISR -> 主上下文：isr_frame_ring（dll_consume_isr_mailbox）、isr_od_ring
 *    （dll_drain_staged_isdu_write）、isr_master_activity 以及截止时间标志。
 *
 * ISR 双泵传送层位于 dll_isr.c；完整上下文契约与缓冲区归属表见
 * docs/CODE_FLOW.md。
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * 状态转换与回调
 * ------------------------------------------------------------------------- */

/**
 * @brief 集中的状态转换。
 *
 * 立即更新协议状态。当转换发生在主上下文时，
 * 应用钩子会立刻收到通知（电平检查，无队列）；
 * 在数据 ISR 中进行的转换只设置状态，并由
 * 下一次主上下文 flush 投递。ISR 中进入 STARTUP 的转换会作为
 * 通知保留，以免快速的 STARTUP -> PREOPERATE 序列对应用隐藏
 * 协议复位。
 */
void dll_set_state(iolink_dll_ctx_t* ctx, iolink_dll_state_t new_state)
{
    if (ctx->state == new_state) {
        return;
    }
    if (ctx->in_isr == 0U) {
        IOLINK_LOG_INFO(&ctx->log, "dll state %u -> %u", (unsigned) ctx->state,
                        (unsigned) new_state);
    }
    else {
        /* ISR 驱动的迁移只在显式启用 ISR 日志时输出。 */
        IOLINK_LOG_ISR_DEBUG(&ctx->log, "dll state (isr) %u -> %u", (unsigned) ctx->state,
                             (unsigned) new_state);
    }
    /* 单次对齐存储：数据 ISR 可能随时读取该状态，且
     * 两个上下文都可能转换它（ISR 快速路径为 t_A 这么做）。
     * 下文依赖状态的簿记由主上下文持有，或仅为提示。 */
    ctx->state = new_state;
    if (new_state == IOLINK_DLL_STATE_OPERATE) {
        /* 主站丢失监控仅在 OPERATE 内有意义；
         * 启动序列可能合法地暂停数个周期。 */
        ctx->master_last_activity_ms = dll_time_ms(ctx);
        ctx->master_lost_count = 0U;
        ctx->master_lost = false;
    }
    if (new_state == IOLINK_DLL_STATE_STARTUP) {
        /* A.6.4：排队的事件在通信被中断时会被丢弃，
           或被取消。iolink_events_reset() 自带一个短的
           临界区，因此不需要外层的。 */
#if IOLINK_FEATURE_EVENTS
        iolink_events_reset(&ctx->events);
        /* 事件引擎丢弃了所有条件；仍然存在的物理故障
         * 必须再次上报。轮询（主上下文）消费该标志
         * 并重新武装边沿锁存，因此这个字节写入对 ISR 安全。 */
        ctx->phy_faults_rearm = 1U;
#endif
        if (ctx->in_isr != 0U) {
            ctx->startup_notification_pending = 1U;
        }
    }
    if (ctx->in_isr == 0U) {
        /* 主上下文：立即投递，因此发生在一次帧
         * 分发内的转换序列仍会逐状态上报。 */
        iolink_dll_flush_state_callbacks(ctx);
    }
}

void iolink_dll_flush_state_callbacks(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->state_cb == NULL)) {
        return;
    }
    /* 来自 ISR 的 STARTUP 转换是一个边沿，不能在主上下文
     * 观察到更晚的 PREOPERATE 状态时丢失。绝不在中断上下文中
     * 调用，因此应用钩子不会进入 ISR。 */
    if (ctx->startup_notification_pending != 0U) {
        if (ctx->last_notified_state != IOLINK_DLL_STATE_STARTUP) {
            ctx->last_notified_state = IOLINK_DLL_STATE_STARTUP;
            ctx->state_cb(ctx->state_cb_user, IOLINK_DLL_STATE_STARTUP);
        }
        ctx->startup_notification_pending = 0U;
    }

    /* 基于电平的通知：只投递当前状态一次。 */
    const iolink_dll_state_t state = ctx->state;
    if (state == ctx->last_notified_state) {
        return;
    }
    ctx->last_notified_state = state;
    ctx->state_cb(ctx->state_cb_user, state);
}

/* -------------------------------------------------------------------------
 * M-sequence 类型判定与发送
 * ------------------------------------------------------------------------- */

/** @brief 返回配置类型对应的 CKT M-sequence 类型位（A.1.3，Table A.3）。 */
static uint8_t dll_mseq_type_bits(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            return IOLINK_MSEQ_TYPE_1;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_3:
        case IOLINK_M_SEQ_TYPE_2_4:
        case IOLINK_M_SEQ_TYPE_2_5:
        case IOLINK_M_SEQ_TYPE_2_V:
            return IOLINK_MSEQ_TYPE_2;
        default:
            return IOLINK_MSEQ_TYPE_0;
    }
}

/**
 * @brief 该请求的 CKT 类型在当前 DLL 状态下是否合法？
 *
 * STARTUP/PREOPERATE 使用 Type-0。OPERATE/ESTAB_COM 使用配置的
 * M-sequence 类型，但有一个例外：ST/Table 47 允许在链路已处于
 * OPERATE 时进行 Type-0 Direct Parameter 页面访问（启动探测、MasterCommand
 * 页面写入，如 DevicePreoperate 0x9A/DeviceOperate 0x99/PDOperate 0x98，
 * 以及 DPP 参数写入）。主站会周期性使用这些操作，
 * 因此把它们当作非法类型会导致主站每次重发页面写入序列时
 * 链路都掉到 STARTUP。
 */
bool dll_type_allowed(const iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    const uint8_t rx_type = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    const uint8_t ch = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    uint8_t exp_type = IOLINK_MSEQ_TYPE_0;

    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                                                     (frame[0] != IOLINK_MC_TRANSITION_COMMAND))) {
        if ((ch == IOLINK_MC_CHANNEL_PAGE) && (rx_type == IOLINK_MSEQ_TYPE_0)) {
            return true;
        }
        exp_type = dll_mseq_type_bits(ctx->m_seq_type);
    }
    return rx_type == exp_type;
}

/**
 * @brief 返回设备在当前状态下期望的 CKT 类型位。
 *
 * 在 STARTUP 和 PREOPERATE 中每条报文都是 Type 0（Table A.3，CKT 位 00）；
 * 在 OPERATE 中期望配置的 M-sequence 类型。
 */
static uint8_t dll_expected_type_bits(const iolink_dll_ctx_t* ctx)
{
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || (ctx->state == IOLINK_DLL_STATE_ESTAB_COM)) {
        return dll_mseq_type_bits(ctx->m_seq_type);
    }
    return IOLINK_MSEQ_TYPE_0;
}

/**
 * @brief 返回给定 M-sequence 类型位对应的期望请求长度。
 *
 * 请求为 MC + CKT + PD-out 宽度 + OD 宽度；校验和承载在
 * CKT 字节中（A.1.6），因此没有尾随的校验和字节。Type-0
 * 报文不携带 PD，只有一个仅在写访问时写入的 OD 字节。
 */

static void dll_isdu_sync_snapshot(iolink_dll_ctx_t* ctx);

/**
 * @brief 通过 ISR 传送层发送设备应答。
 *
 * 将字节交给原始字节/帧 TX 钩子；ISR 逐个送入（TX BYTE）
 * 或由 PHY 发送整帧（TX FRAME）。应答字节（包括
 * CKS）由共享处理函数构建，因此两种方式的帧布局
 * 相同。
 *
 * 协议栈只有单个主上下文调用者，因此暂存无需锁：
 * 主泵拥有槽位 0/1；数据 ISR 拥有自己的槽位。ISR 只在
 * isr_tx_pending 发布后读取暂存应答（数据先行顺序加
 * 编译器屏障）。只有旧的直接发送回退路径会从此上下文
 * 驱动 PHY，因此会排除数据中断。
 */
static void dll_send(iolink_dll_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (data == NULL) || (len == 0U)) {
        return;
    }
    if (len > sizeof(ctx->isr_tx_payload[0])) {
        return;
    }
    /* 仅主上下文槽位：ISR 有自己的槽位，因此该计数器只有
     * 单个写者，无需读-改-写原子操作。 */
    const uint8_t slot = ctx->isr_tx_next;
    ctx->isr_tx_next = (uint8_t) (ctx->isr_tx_next ^ 1U);
    (void) memcpy(ctx->isr_tx_payload[slot], data, len);
    ctx->isr_tx_pending_slot = slot;
    ctx->isr_tx_len = (uint8_t) len;
    ctx->isr_tx_has_cks = 0U;
    IOLINK_FENCE();           /* 暂存字节先于发布 */
    ctx->isr_tx_pending = 1U; /* 最后发布 */

    if (ctx->phy->request_service != NULL) {
        /* 在发布之后、且在任何关中断窗口之外通知 PHY：
         * 该钩子属于端口代码（可能进行 SPI 访问或获取锁）。
         * 其间自然产生的数据中断只会排空暂存
         * 应答，随后额外的服务请求便无事可做。 */
        ctx->phy->request_service(ctx->phy->user);
        return;
    }

#if !IOLINK_PHY_FEATURE_ISR_TX_ONLY
    /* 适用于无 service 钩子的 PHY 的旧回退路径（流式/主机测试）。
     * 它从此上下文驱动 TX 钩子，因此数据中断不能与
     * 传输交错。 */
    IOLINK_CRITICAL_ENTER();
    ctx->isr_tx_pending = 0U;
    dll_isr_start_staged_tx(ctx, slot, (uint8_t) len);
    IOLINK_CRITICAL_EXIT();
#else
    /* 仅有 ISR、且没有 request_service() 的 PHY：应答无法发送。
     * 对其计数，使配置错误可被观测而非静默。 */
    ctx->isr_tx_pending = 0U;
    ctx->isr_tx_dropped++;
#endif /* !IOLINK_PHY_FEATURE_ISR_TX_ONLY */
}

/* -------------------------------------------------------------------------
 * 时序、主站命令与 OPERATE 处理
 * ------------------------------------------------------------------------- */

/** @brief 返回响应时间（t_REN）上限，单位 us（C6/Table 10：500 us）。 */
static uint32_t dll_get_t_ren_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    if (ctx->t_ren_override) {
        return ctx->t_ren_limit_us;
    }
    /* Table 10：T_REN 只有一个值，最大 500 us，与波特率无关。 */
    return IOLINK_T_REN_US;
}

/** @brief 返回当前生效的波特率，单位 Hz。 */
static uint32_t dll_get_baud_hz(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 38400U;
    }
    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            return 4800U;
        case IOLINK_BAUDRATE_COM2:
            return 38400U;
        case IOLINK_BAUDRATE_COM3:
            return 230400U;
        default:
            return 38400U;
    }
}

/** @brief 返回当前波特率下的字节间超时（16 个位时间），单位 us。 */
static uint32_t dll_get_t_byte_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    uint32_t t_bit_us;
    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            t_bit_us = 208U;
            break;
        case IOLINK_BAUDRATE_COM2:
            t_bit_us = 26U;
            break;
        case IOLINK_BAUDRATE_COM3:
            t_bit_us = 4U;
            break;
        default:
            t_bit_us = 26U;
            break;
    }
    return t_bit_us * 16U;
}

/** @brief 当掉电/PD 保护窗口（t_PD）仍有效时返回 true。 */
static bool dll_t_pd_active(const iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->t_pd_deadline_us == 0U)) {
        return false;
    }
    return dll_time_us(ctx) < ctx->t_pd_deadline_us;
}

/** @brief 排空 PHY 中所有待处理的 RX 字节；若读到任何字节则返回 true。 */
static bool dll_drain_rx(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return false;
    }
    /* ISR 泵填充邮箱；排空它会丢弃在 t_pd 保护窗口
     * 有效期间到达的帧。 */
    if (!iolink_ring_is_empty(&ctx->isr_frame_ring)) {
        while (iolink_ring_peek(&ctx->isr_frame_ring) != NULL) {
            iolink_ring_release(&ctx->isr_frame_ring);
        }
        return true;
    }
    return false;
}

/** @brief ST rc=3：协议错误丢弃该帧并降级到 STARTUP。
 *
 * ST 6362 库只在 Fallback MasterCommand
 * （T8/T9）或 T_DSIO 唤醒超时（T10）时返回 SIO。成帧/CRC 错误
 * 本身从不会离开当前模式。 */
static void dll_protocol_error_to_startup(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->fallback_count = 0U;
    ctx->total_retries++;
    ctx->frame_index = 0U;
    if (ctx->state != IOLINK_DLL_STATE_STARTUP) {
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    }
}

/** @brief 判断是否为页通道 Type-0 WRITE 的 MasterCommand FALLBACK（Table B.2）。 */
static bool dll_is_fallback_command(const iolink_dll_ctx_t* ctx)
{
    uint8_t mc = ctx->frame_buf[0];
    return ((mc & IOLINK_MC_RW_MASK) == 0U) &&
           ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
           ((mc & IOLINK_MC_ADDR_MASK) == 0x00U) && (ctx->frame_buf[2] == IOLINK_CMD_FALLBACK);
}

/**
 * @brief 处理来自主站的 FALLBACK MasterCommand（0x5A，Table B.2）。
 *
 * 设备应在 3 个 MasterCycleTime 之后、且最多
 * 500 ms 内（T_FBD，Table 43）切换到 SIO。截止时间在此设置，并在
 * iolink_dll_process() 中强制执行；应答是仅含 CKS 的 Type-0 写响应。 */
static void dll_handle_fallback_command(iolink_dll_ctx_t* ctx)
{
    /* 没有唤醒检测器时，PHY 无法再次离开 SIO，因此
     * 不支持 FALLBACK 转换：应答该 Type-0 写而不改变
     * DL 模式。 */
    if (dll_phy_has_wakeup(ctx)) {
        uint32_t t_fbd_ms = 500U;
        if (ctx->min_cycle_time_us > 0U) {
            t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
            if (t_fbd_ms > 500U) {
                t_fbd_ms = 500U;
            }
        }
        IOLINK_LOG_WARN(&ctx->log, "fallback command: SIO in %lu ms", (unsigned long) t_fbd_ms);
        ctx->fallback_deadline_ms = dll_time_us(ctx) / 1000U + t_fbd_ms;
        dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
    }

    uint8_t resp[1];
    resp[0] = 0x00U;
    resp[0] = iolink_checksum6(resp, 1U);
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
}

/**
 * @brief 以 ST 风格将 MasterCommand（Table B.2）应用到 DL 状态。
 *
 * 对 FALLBACK（0x5A）返回 false，它需要 T_FBD 截止时间，并由
 * dll_handle_fallback_command() 处理。其余命令由调用方
 * 以仅含 CKS 的应答确认。
 */
bool dll_apply_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_FALLBACK:
            return false;
        case IOLINK_CMD_DEVICE_STARTUP:
            dll_protocol_error_to_startup(ctx);
            return true;
        case IOLINK_CMD_DEVICE_PREOPERATE:
            ctx->fallback_clear_request = 1U;
            dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
            return true;
        case IOLINK_CMD_PROCESS_DATA_OPERATE:
            ctx->fallback_clear_request = 1U;
            ctx->pd_valid = true;
            dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
            return true;
        case IOLINK_CMD_DEVICE_OPERATE:
            ctx->fallback_clear_request = 1U;
            ctx->pd_valid = false;
            /* 来自 PREOPERATE 的 DeviceOperate 会建立通信
               （ESTAB_COM）；随后的 PDOperate 0x98 进入 OPERATE。
               在 OPERATE/ESTAB 中它只是幂等的确认。 */
            if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
                dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
            }
            else {
                dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
            }
            return true;
        case IOLINK_CMD_MASTER_IDENT:
        case IOLINK_CMD_DEVICE_IDENT:
        default:
            return true;
    }
}

/** @brief 主上下文中的 MasterCommand 页面写入：应用状态并发送 CKS。 */
/* CKS PD 状态位：位 6 为 0 表示“Process Data valid”。
 * 没有输入 PD 的设备不可能有无效输入 PD，因此始终上报
 * 有效；否则上报当前 pd_valid 状态。 */
uint8_t dll_pd_status_flags(const iolink_dll_ctx_t* ctx)
{
    if ((ctx->pd_in_len_current == 0U) && (ctx->pd_out_len_current > 0U)) {
        return 0U;
    }
    return ctx->pd_valid ? 0U : 0x40U;
}

static void dll_handle_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd)
{
    if (cmd == IOLINK_CMD_FALLBACK) {
        dll_handle_fallback_command(ctx);
        return;
    }

    (void) dll_apply_master_command(ctx, cmd);

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
}

/** @brief 处理 PREOPERATE 主站命令：在转换命令上推进到 ESTAB_COM。 */
static void dll_handle_preoperate(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t ck)
{
    (void) ck;
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->fallback_count = 0U;
        /* 按规范，转换命令无需响应 */
    }
}

/**
 * @brief 应答页通信通道上的启动 Type-0 读。
 *
 * 规范启动转换 T1：主站读取一个 Direct Parameter 页面字节
 * （地址在 MC 地址字段中，例如 0x02 = MinCycleTime）。用一个
 * 承载该字节的 2 字节 Type-0 帧应答，而不是把 MC 送入
 * ISDU 引擎。
 */
static void dll_handle_page_channel_read(iolink_dll_ctx_t* ctx, uint8_t mc)
{
    uint8_t resp[2];
    resp[0] =
        iolink_isdu_direct_param_page1_octet(&ctx->isdu, (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
    resp[1] = 0x00U;
    resp[1] = iolink_checksum6(resp, 2U);
    dll_send(ctx, resp, 2);
    ctx->last_response_us = dll_time_us(ctx);
}

/**
 * @brief 按通信通道分发一条 OD 报文（C2，7.3.5.3）。
 *
 * 诊断通道：事件存储器（Table 58）。ISDU 通道：ISDU
 * 传送层。页通道和过程通道由调用方处理（页通道由
 * 启动探测和 Task 4 之前的旧 ISDU 路径处理）。若该通道
 * 在此处理则返回 true。
 */
static bool dll_dispatch_od(iolink_dll_ctx_t* ctx, uint8_t mc, const uint8_t* od_in, uint8_t od_len,
                            uint8_t* od_out)
{
    const uint8_t channel = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);

    if (channel == IOLINK_MC_CHANNEL_PAGE) {
        /* 7.3.5.3：Direct Parameter 页面读/写。读操作返回
           该地址处的页面字节；未实现的地址返回 0，写入
           被忽略（A.1.2）。Table 45 T6/T7/T9：MasterCommand（Table B.2）
           写入 Direct Parameter 页面 1 的地址 0。TYPE_0 帧路径
           在分发前拦截它；OPERATE 的 M-sequence 将其作为
           OD 字节携带相同命令，因此在此应用该写入。 */
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
            (void) dll_apply_master_command(ctx, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x01U)) {
            ctx->master_cycle_time = od_in[0];
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x0FU)) {
            /* 7.3.5.3：通过 Direct Parameter 页面 1 的 SystemCommand（无响应）。 */
            iolink_isdu_execute_system_command(&ctx->isdu, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) != 0U) && (od_len > 0U)) {
            od_out[0] = iolink_isdu_direct_param_page1_octet(&ctx->isdu,
                                                             (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
        }
        return true;
    }

    if (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) {
        /* 7.3.8 Table 58：事件存储器由诊断通道服务。
           读操作返回该地址处的存储字节；对地址 0 的写入
           确认事件读取并清除 Event 标志。 */
        const uint8_t addr = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
#if !IOLINK_FEATURE_EVENTS
        (void) addr;
#endif
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            for (uint8_t i = 0U; i < od_len; i++) {
                od_out[i] = DLL_EVENTS_MEMORY_READ(&ctx->events, (uint8_t) (addr + i));
            }
        }
        else if (od_len > 0U) {
            DLL_EVENTS_MEMORY_WRITE(&ctx->events, addr, od_in[0]);
        }
        return true;
    }

    if (channel == IOLINK_MC_CHANNEL_ISDU) {
        /* C3/Table 52：FlowCTRL 位于 MC 地址位中。写操作携带
           请求字节；读操作获取成帧响应（或 Busy）。ISR
           暂存每个有效的 ISDU 写，主泵在 iolink_dll_process()
           顶部排空它，因此写入在此绝不会被应用两次。 */
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            const uint8_t flowctrl = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
            iolink_isdu_od_read(&ctx->isdu, flowctrl, od_out, od_len);
        }
        return true;
    }

    (void) od_in;
    (void) ctx;
    return false;
}

/** @brief 通过 ISDU 处理 2 字节 Type-0 请求并发送 OD 响应。 */
static void dll_handle_operate_type0(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t cks)
{
    (void) cks;
    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &cks, 1U, &od_resp);

    uint8_t resp[2];
    uint8_t ck_flags = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        ck_flags |= 0x80U;
    }
    ck_flags |= dll_pd_status_flags(ctx);

    /* Figure A.5：Type-0 WRITE 在每个通信通道上都只由 CKS
       应答；每个 Type-0 READ 由 OD 后跟 CKS
       应答。 */
    const bool type0_write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    if (type0_write) {
        resp[0] = ck_flags;
        resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
        dll_send(ctx, resp, 1);
    }
    else {
        resp[0] = od_resp;
        resp[1] = ck_flags;
        resp[1] = (uint8_t) (resp[1] | iolink_checksum6(resp, 2U));
        dll_send(ctx, resp, 2);
    }
    ctx->last_response_us = dll_time_us(ctx);
}

/** @brief 处理 3 字节 Type-0 OD WRITE（MC、CKT、OD），仅用 CKS 应答（Figure A.5）。 */
static void dll_handle_type0_od_write(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t od_in)
{
    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &od_in, 1U, &od_resp);

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
}

/** @brief 处理 Type-1/Type-2 OPERATE 帧（PD+OD）并构建响应，同时强制执行 t_REN。 */
/**
 * @brief 处理只需要重置传送层的 eager ISDU 读。
 *
 * OPERATE 的 ISDU 读由 ISR 从响应快照中应答；
 * 主泵不得再次推进 ISDU 段索引。只有 IDLE/ABORT
 * 读需要重置传送层（以及刷新快照）。
 *
 * @return 当帧被完全处理时返回 true。
 */
static bool dll_handle_eager_isdu_read(iolink_dll_ctx_t* ctx, bool write)
{
    if (!ctx->frame_replied_by_isr ||
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) != IOLINK_MC_CHANNEL_ISDU) || write) {
        return false;
    }
    const uint8_t flowctrl = (uint8_t) (ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK);
    if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2) ||
        (flowctrl == IOLINK_FLOWCTRL_ABORT)) {
        uint8_t dummy = 0U;
        iolink_isdu_od_read(&ctx->isdu, flowctrl, &dummy, 0U); /* 仅重置 */
        dll_isdu_sync_snapshot(ctx);
    }
    ctx->frame_replied_by_isr = 0U;
    ctx->last_response_us = dll_time_us(ctx);
    return true;
}

/** @brief 分发请求 OD；当通道拒绝时把应答 OD 清零。 */
static void dll_dispatch_od_reply(iolink_dll_ctx_t* ctx, bool write, uint16_t od_offset,
                                  uint8_t* od_out)
{
    const uint8_t od_w = ctx->od_width; /* 来自 opt-in 补丁的 1/2/8/32 */
    const uint8_t req_od = write ? od_w : 0U;

    uint8_t od_in[IOLINK_OD_MAX_SIZE];
    (void) memset(od_in, 0, sizeof(od_in));
    (void) memset(od_out, 0, IOLINK_OD_MAX_SIZE);
    if (req_od > 0U) {
        (void) memcpy(od_in, &ctx->frame_buf[od_offset], req_od);
    }

    const uint8_t dispatch_od = write ? req_od : od_w;
    if (!dll_dispatch_od(ctx, ctx->frame_buf[0], od_in, dispatch_od, od_out)) {
        for (uint16_t i = 0; i < dispatch_od; i++) {
            od_out[i] = 0U;
        }
    }
}

/** @brief 构建并发送 Type-1/2 帧的 [OD][PD-in]CKS 应答。 */
static void dll_send_od_reply(iolink_dll_ctx_t* ctx, bool write, const uint8_t* od_out)
{
    const uint8_t resp_od = write ? 0U : ctx->od_width;

    /* Figure A.9/A.10/A.13/A.14：对 READ M-sequence 的应答为
       [OD 字节][PD-in 字节] CKS；对 WRITE M-sequence 的应答不携带
       OD，为 [PD-in 字节] CKS。CKS 携带 Event 标志（位 7）、
       PD 有效标志（位 6，1 = 无效）以及 6 位报文校验和
       （位 0-5）；没有前置状态字节（A.1.5）。 */
    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + IOLINK_OD_MAX_SIZE + 1];
    uint16_t pos = 0U;
    if (!write && (resp_od > 0U)) {
        (void) memcpy(&resp[pos], od_out, resp_od);
        pos += resp_od;
    }
    if (ctx->pd_in_len > 0U) {
        (void) memcpy(&resp[pos], &ctx->pd_in[ctx->pd_in_active][ctx->od_width], ctx->pd_in_len);
        pos += ctx->pd_in_len;
    }

    uint8_t cks = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        cks |= 0x80U;
    }
    cks |= dll_pd_status_flags(ctx);
    resp[pos] = cks;
    resp[pos] = (uint8_t) (resp[pos] | iolink_checksum6(resp, pos + 1U));
    pos++;

    if (dll_phy_has_tx(ctx)) {
        dll_send(ctx, resp, pos);
        ctx->fallback_count = 0U;
        const uint32_t end_tx_us = (uint32_t) dll_time_us(ctx);
        ctx->response_time_us = end_tx_us - (uint32_t) ctx->last_cycle_start_us;

        if (ctx->enforce_timing) {
            const uint32_t limit = dll_get_t_ren_limit_us(ctx);
            if ((limit > 0U) && (ctx->response_time_us > limit)) {
                ctx->timing_errors++;
                ctx->t_ren_violations++;
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                  IOLINK_EVENT_TYPE_WARNING);
            }
        }
    }
    ctx->last_response_us = dll_time_us(ctx);
}

static void dll_handle_operate_type1_2(iolink_dll_ctx_t* ctx)
{
    /* Type-1/2 主站报文：MC | CKT | PD | OD；校验和在 CKT 中。 */
    const uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    const uint16_t od_offset = (uint16_t) (pd_offset + ctx->pd_out_len_current);

    if (ctx->pd_out_len_current > 0U) {
        (void) memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->pd_out_len_current);
        ctx->pd_out_seen = true; /* B.2.24：最后写入应用的 PDout */
    }

    const bool write = ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U);
    if (dll_handle_eager_isdu_read(ctx, write)) {
        return;
    }

    uint8_t od_out[IOLINK_OD_MAX_SIZE];
    dll_dispatch_od_reply(ctx, write, od_offset, od_out);

    if (ctx->frame_replied_by_isr &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PROCESS)) {
        /* ISR 泵已在帧结束时发送了过程通道
         * 预暂存的周期应答。 */
        ctx->fallback_count = 0U;
        ctx->last_response_us = dll_time_us(ctx);
        ctx->frame_replied_by_isr = 0U;
        return;
    }
    if (ctx->frame_replied_by_isr != 0U) {
        /* ISR 已确认该 ISDU 写；不要再发送第二个
         * 应答。ISDU 读在 dll_dispatch_od() 之前处理。 */
        ctx->frame_replied_by_isr = 0U;
        ctx->last_response_us = dll_time_us(ctx);
        return;
    }

    dll_send_od_reply(ctx, write, od_out);
}

/* -------------------------------------------------------------------------
 * 诊断轮询
 * ------------------------------------------------------------------------- */

/**
 * @brief 轮询 PHY 故障位掩码并对其边沿作出反应。
 *
 * 只上报边沿：持续有效的故障只计数并触发一次
 * 事件，故障消失时清除该事件，使事件引擎能够发出
 * 对应的“disappears”。映射到 Table D.1 设备代码：
 *  - UNDERVOLTAGE   -> 0x5111 供电电压欠压（Warning）
 *  - OVERVOLTAGE    -> 0x5110 供电电压过压（Warning）
 *  - OVERTEMP       -> 0x4000 温度过载（Error）
 *  - SHORT_CIRCUIT  -> 0x7710 短路（Error）
 *
 * 待处理的“rearm”请求（事件引擎被 STARTUP 或诊断复位
 * 重置）会重启边沿检测，因此仍然存在的故障
 * 会在新会话中再次上报。
 */
static void dll_poll_phy_faults(iolink_dll_ctx_t* ctx)
{
    /* 事件引擎可能在数据 ISR 中被 dll_set_state(STARTUP) 复位，
     * 那里只设置 phy_faults_rearm=1。读-清除操作上的临界区
     * 可防止该设置与清除竞争，因此仍然存在的故障
     * 会在新会话中再次上报。 */
    IOLINK_CRITICAL_ENTER();
    if (ctx->phy_faults_rearm != 0U) {
        ctx->phy_faults_rearm = 0U;
        ctx->phy_faults_last = 0U;
    }
    IOLINK_CRITICAL_EXIT();
    const uint8_t faults = ctx->phy->get_faults(ctx->phy->user);
    const uint8_t rising = (uint8_t) (faults & (uint8_t) ~ctx->phy_faults_last);
    const uint8_t falling = (uint8_t) ((uint8_t) ~faults & ctx->phy_faults_last);

    if ((rising & IOLINK_PHY_FAULT_UNDERVOLTAGE) != 0U) {
        ctx->voltage_faults++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN,
                          IOLINK_EVENT_TYPE_WARNING);
    }
    if ((rising & IOLINK_PHY_FAULT_OVERVOLTAGE) != 0U) {
        ctx->voltage_faults++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN,
                          IOLINK_EVENT_TYPE_WARNING);
    }
    if ((rising & IOLINK_PHY_FAULT_OVERTEMP) != 0U) {
        ctx->overtemp_faults++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD,
                          IOLINK_EVENT_TYPE_ERROR);
    }
    if ((rising & IOLINK_PHY_FAULT_SHORT_CIRCUIT) != 0U) {
        ctx->short_circuits++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
    }
    if ((falling & IOLINK_PHY_FAULT_UNDERVOLTAGE) != 0U) {
        DLL_EVENT_CLEAR(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
    }
    if ((falling & IOLINK_PHY_FAULT_OVERVOLTAGE) != 0U) {
        DLL_EVENT_CLEAR(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN);
    }
    if ((falling & IOLINK_PHY_FAULT_OVERTEMP) != 0U) {
        DLL_EVENT_CLEAR(&ctx->events, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD);
    }
    if ((falling & IOLINK_PHY_FAULT_SHORT_CIRCUIT) != 0U) {
        DLL_EVENT_CLEAR(&ctx->events, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    }
    ctx->phy_faults_last = faults;
}

/** @brief 轮询 PHY 诊断并在故障时触发事件。 */
static void dll_poll_diagnostics(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    /* 故障位掩码是唯一的物理诊断通道。 */
    if (ctx->phy->get_faults != NULL) {
        dll_poll_phy_faults(ctx);
    }
}

/* -------------------------------------------------------------------------
 * 生命周期与 ISDU 快照
 * ------------------------------------------------------------------------- */

void iolink_dll_init(iolink_dll_ctx_t* ctx, const iolink_phy_api_t* phy)
{
    if ((phy == NULL) || (!iolink_ctx_zero(ctx, sizeof(iolink_dll_ctx_t)))) {
        return;
    }
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->last_notified_state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    ctx->enforce_timing = (IOLINK_TIMING_ENFORCE_DEFAULT != 0U);
    ctx->sio_fallback_threshold = 3U;
    ctx->operating_baudrate = IOLINK_BAUDRATE_UNSET;
    ctx->master_last_activity_ms = dll_time_ms(ctx);
    ctx->master_lost_cycles = 0U;
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;

    /* Table A.10：TYPE_2_1/TYPE_2_2 携带一个 On-request Data 字节；TYPE_2_V 是
       双字节变体（OPERATE M-sequence 代码 5）。 */
    ctx->od_len = (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;
    ctx->isr_isdu_last_flowctrl = 0xFFU; /* 尚未见到 START */

    ctx->pd_in_len_current = ctx->pd_in_len;
    ctx->pd_out_len_current = ctx->pd_out_len;
    ctx->pd_in_len_max = ctx->pd_in_len;
    ctx->pd_out_len_max = ctx->pd_out_len;
    ctx->pd_valid = true;
    ctx->pd_in_captured = 0xFFU; /* 无进行中的应答 */

    /* 帧邮箱：ISR 发布到环中，主泵消费。 */
    iolink_ring_init(&ctx->isr_frame_ring, (uint8_t*) ctx->isr_frame_slot, IOLINK_MBX_SLOTS,
                     (uint8_t) sizeof(iolink_frame_rec_t));
    /* 暂存的 ISDU 写段（ISR -> 主上下文）。 */
    iolink_ring_init(&ctx->isr_od_ring, (uint8_t*) ctx->isr_od_slot, IOLINK_OD_SLOTS,
                     (uint8_t) sizeof(iolink_od_rec_t));

    ctx->baudrate = IOLINK_BAUDRATE_COM2;
    if (ctx->phy->set_baudrate != NULL) {
        ctx->phy->set_baudrate(ctx->phy->user, IOLINK_BAUDRATE_COM2);
    }

    DLL_EVENTS_INIT(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_init(&ctx->ds, NULL);
#endif
#if IOLINK_FEATURE_EVENTS
    ctx->isdu.event_ctx = &ctx->events;
#else
    ctx->isdu.event_ctx = NULL;
#endif
    ctx->isdu.dll_ctx = ctx;
#if IOLINK_FEATURE_DATA_STORAGE
    ctx->isdu.ds_ctx = &ctx->ds;
#else
    ctx->isdu.ds_ctx = NULL;
#endif

    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
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
    if (dll_phy_has_wakeup(ctx)) {
        iolink_dll_set_sio_mode(ctx);
    }
    else {
        iolink_dll_set_sdci_mode(ctx);
    }
}

/** @brief 校验并分发 ctx->frame_buf 中保存的一个完整请求。
 *
 * 由流式 RX 循环和 ISR 泵邮箱共享：两者都先组装一个
 * 完整 M-sequence，然后运行完全相同的校验 + 分发。
 */
/**
 * @brief 为 ISR 读泵发布成帧的 ISDU 响应视图。
 *
 * 单写者交接：ctx->isdu（以及双缓冲响应）由主泵
 * 拥有；ISR 读取最后发布的缓冲区。{length, busy,
 * buffer} 作为一个原子字传递，因此无需临界区、无需拷贝，
 * ISR 总是读到完整、稳定的响应。
 */
static void dll_isdu_sync_snapshot(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    const bool busy = (ctx->isdu.state == ISDU_STATE_SERVICE_EXECUTE);
    uint8_t count = 0U;
    uint8_t sel = ctx->isdu.resp_pub;
    if (ctx->isdu.state == ISDU_STATE_RESPONSE_READY) {
        count = (ctx->isdu.resp_total > 255U) ? 255U : (uint8_t) ctx->isdu.resp_total;
        sel = ctx->isdu.resp_sel;
        /* 完成的响应成为已发布响应；引擎将在
         * 另一个缓冲区中构建下一个响应。 */
        ctx->isdu.resp_pub = sel;
    }
    const IOLINK_ATOMIC_UINT_T word = IOLINK_ISDU_PUB_WORD(count, busy, sel);
    if (word != ctx->isr_isdu_pub) {
        /* 最后发布：响应字节已在 resp_store[sel] 中。 */
        iolink_atomic_store(&ctx->isr_isdu_pub, word);
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
static uint32_t dll_master_cycle_us(const iolink_dll_ctx_t* ctx)
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
static void dll_mark_master_activity_ts(iolink_dll_ctx_t* ctx)
{
    ctx->master_last_activity_ms = dll_time_ms(ctx);
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;
}

/** @brief ST 风格主站活动：帧结束/RX 超时会重启计数器。 */
void dll_mark_master_activity(iolink_dll_ctx_t* ctx)
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

void iolink_dll_request_back_to_box(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->back_to_box_pending = true;
    ctx->back_to_box_cycles = 0U;
}

/* -------------------------------------------------------------------------
 * 主泵：帧校验、分发与时序监控
 * ------------------------------------------------------------------------- */

/** @brief A.1.6 校验和与 CKT 类型校验的结果。 */
typedef enum
{
    DLL_FRAME_INVALID,    /**< 校验和不匹配：丢弃，保持 DL 模式。 */
    DLL_FRAME_TYPE_ERROR, /**< 校验和有效，M-sequence 类型非法。 */
    DLL_FRAME_VALID       /**< 有效请求；按状态分发。 */
} dll_frame_result_t;

/**
 * @brief 处理不再接受帧的两个状态。
 *
 * @return 当帧被丢弃（调用方不得运行公共
 *         尾部清理）时返回 true。
 */
static bool dll_frame_drop_if_stopped(iolink_dll_ctx_t* ctx)
{
    if (ctx->back_to_box_pending && (ctx->back_to_box_cycles >= 3U)) {
        /* 10.7.5：在至少 3 个 MasterCycle 之后，设备停止并禁用
           通信和输出驱动器，直到下次上电。 */
        ctx->back_to_box_pending = false;
        dll_set_state(ctx, IOLINK_DLL_STATE_COMM_DISABLED);
        if ((ctx->phy != NULL) && (ctx->phy->set_mode != NULL)) {
            ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_INACTIVE);
        }
        ctx->frame_index = 0U;
        return true;
    }
    if (ctx->state == IOLINK_DLL_STATE_COMM_DISABLED) {
        /* 通信已停止，直到下次上电（10.7.5）。 */
        ctx->frame_index = 0U;
        return true;
    }
    return false;
}

/** @brief T_CYCLE 监控：统计过短的 MasterCycle 并重新开始。 */
static void dll_check_cycle_time(iolink_dll_ctx_t* ctx)
{
    const uint64_t now_us = dll_time_us(ctx);
    if ((ctx->enforce_timing) && (ctx->min_cycle_time_us > 0U) &&
        (ctx->last_cycle_start_us != 0U) &&
        (now_us - ctx->last_cycle_start_us < (uint64_t) ctx->min_cycle_time_us)) {
        ctx->timing_errors++;
        ctx->t_cycle_violations++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
    }
    ctx->last_cycle_start_us = now_us;
}

/**
 * @brief 校验 A.1.6 校验和与 CKT M-sequence 类型。
 *
 * A.1.6/Figure A.2：校验和位于 CKT 字节（第二个字节）；计算校验和
 * 之前，其位 0-5 被清零，类型位（6-7）保留。
 * C5/Table 47：类型必须对当前状态合法。
 */
static dll_frame_result_t dll_frame_validate(iolink_dll_ctx_t* ctx)
{
    uint8_t crc_buf[sizeof(ctx->frame_buf)];
    (void) memcpy(crc_buf, ctx->frame_buf, ctx->req_len);
    const uint8_t expected_ck = (uint8_t) (crc_buf[1] & 0x3FU);
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
static void dll_handle_frame_error(iolink_dll_ctx_t* ctx, dll_frame_result_t result)
{
    IOLINK_LOG_DEBUG(&ctx->log, "frame error: result=%d state=%u", (int) result,
                     (unsigned) ctx->state);
    if (result == DLL_FRAME_TYPE_ERROR) {
        ctx->frame_replied_by_isr = 0U;
        ctx->framing_errors++;
        ctx->frame_index = 0U;
        if ((ctx->state == IOLINK_DLL_STATE_OPERATE) ||
            (ctx->state == IOLINK_DLL_STATE_ESTAB_COM)) {
            return;
        }
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_FRAMING, IOLINK_EVENT_TYPE_ERROR);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        return;
    }

    /* 校验和失败：ST rc=2 表示“丢弃该帧并返回
     * 监听”；DLL 模式保持不变。此处不要降级到 SIO：
     * 单个截断/碰撞帧（例如主站在我们的应答仍在 C/Q 上时
     * 开始下一个 M-sequence）不能使链路下线。
     * SIO 保留给 Fallback MasterCommand 和 T_DSIO 超时。 */
    ctx->crc_errors++;
    ctx->framing_errors++;
    ctx->total_retries++;
    /* 保留诊断事件（ST 没有计数器，但应用用它
     * 作为事件标志），同时不改变 DL 模式。 */
    ctx->fallback_count++;
    if (ctx->fallback_count >= ctx->sio_fallback_threshold) {
        ctx->fallback_count = 0U;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_WARNING);
    }
}

#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
/**
 * @brief 拒绝在启动期间收到的 OPERATE 请求。
 *
 * Table 53（SDCI_TC_0035）：只有 V1.0 设备会对启动期间收到的
 * OPERATE 请求作出反应。V1.1 设备不得应答它，也不得
 * 推进到 PREOPERATE/ESTAB_COM。
 *
 * @return 当帧被拒绝（跳过尾部清理）时返回 true。
 */
static bool dll_reject_startup_operate(iolink_dll_ctx_t* ctx, bool was_establishing)
{
    if (was_establishing && (ctx->req_len == 3U) &&
        ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U) &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
        ((ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK) == 0x00U) &&
        ((ctx->frame_buf[2] == IOLINK_CMD_DEVICE_OPERATE) ||
         (ctx->frame_buf[2] == IOLINK_CMD_PROCESS_DATA_OPERATE))) {
        ctx->framing_errors++;
        ctx->frame_index = 0U;
        return true;
    }
    return false;
}
#endif /* !IOLINK_COMPAT_V10_STARTUP_OPERATE */

/**
 * @brief PREOPERATE：拦截唤醒后的第一条报文。
 *
 * 转换 T1：在页通道上对 Direct Parameter 页面字节的 Type-0 READ
 * 是规范定义的启动探测。从直接参数源应答，
 * 而不是把 MC 送入 ISDU。后续 PREOPERATE 的 Type-0 帧是 ISDU
 * 流量，不会被拦截。
 */
static bool dll_handle_startup_probe(iolink_dll_ctx_t* ctx, bool was_establishing, uint8_t mc)
{
    const bool page_read =
        ((mc & IOLINK_MC_RW_MASK) != 0U) && ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == 0x20U);
    if (was_establishing && (ctx->req_len == 2U) && page_read) {
        dll_handle_page_channel_read(ctx, mc);
        return true;
    }
    return false;
}

/** @brief PREOPERATE 分发：仅 Type-0，外加 FALLBACK/MasterCommand。 */
static void dll_dispatch_preoperate(iolink_dll_ctx_t* ctx, bool was_establishing)
{
    const uint8_t mc = ctx->frame_buf[0];
    if (dll_handle_startup_probe(ctx, was_establishing, mc)) {
        return;
    }
    if (ctx->req_len == 2U) {
        if (mc == IOLINK_MC_TRANSITION_COMMAND) {
            dll_handle_preoperate(ctx, mc, ctx->frame_buf[1]);
        }
        else {
            dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
        }
    }
    else if ((ctx->req_len == 3U) && dll_is_fallback_command(ctx)) {
        /* Table B.2/43：FALLBACK MasterCommand 0x5A。 */
        dll_handle_fallback_command(ctx);
    }
    else if ((ctx->req_len == 3U) && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
             ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
             ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
        /* MasterCommand 的页通道 Type-0 WRITE（Table B.2）：
           0x97 DeviceStartup、0x98 PDOperate、0x99 DeviceOperate、
           0x9A DevicePreoperate、0x95/0x96 ident、0x5A Fallback。
           ST 应用模式变更并仅用 CKS 应答。 */
        dll_handle_master_command(ctx, ctx->frame_buf[2]);
    }
    else if (ctx->req_len == 3U) {
        /* PREOPERATE 中其他任何 3 字节 Type-0 OD 写属于 ISDU 或诊断
           流量（7.3.6 允许在 OPERATE 之前使用 ISDU）。它是 TYPE_0 帧：
           偏移 2 处有一个 OD 字节，应答仅含 CKS（Figure A.5），绝不使用
           配置的 OPERATE PD 宽度。 */
        dll_handle_type0_od_write(ctx, ctx->frame_buf[0], ctx->frame_buf[2]);
    }
}

/** @brief ESTAB_COM 分发：首个有效的非转换帧进入 OPERATE。 */
static void dll_dispatch_estab_com(iolink_dll_ctx_t* ctx)
{
    const uint8_t mc = ctx->frame_buf[0];
    const bool type0 = ((ctx->frame_buf[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0);
    const bool page_write = type0 && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
                            ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE);
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        dll_handle_preoperate(ctx, mc, ctx->frame_buf[1]);
    }
    else if (page_write && ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
        /* 首个周期帧之前的 MasterCommand：应用它，而不是把
           Type-0 页面写入误认为 Type-2 PD。 */
        dll_handle_master_command(ctx, ctx->frame_buf[2]);
    }
    else {
        /* 首个有效的非转换帧建立通信并
           进入 OPERATE，包含 Type 0。 */
        dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
        if (page_write) {
            dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
        }
        else if (type0 && (ctx->req_len == 2U)) {
            dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
        }
        else if (type0) {
            /* Type-0 OD 写（ISDU/诊断）：偏移 2 处一个 OD 字节。 */
            dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
        }
        else {
            /* 固定的 TYPE_1_x/TYPE_2_x READ 仅为 MC + CKT（req_len == 2），
               因此 req_len 无法将其与 Type-0 区分：CKT 类型位才是
               权威依据。 */
            dll_handle_operate_type1_2(ctx);
        }
    }
}

/** @brief OPERATE 分发：所有通道均有效，禁止转换。 */
static bool dll_dispatch_operate(iolink_dll_ctx_t* ctx)
{
    const uint8_t mc = ctx->frame_buf[0];
    const bool type0 = ((ctx->frame_buf[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0);
    const bool page_write = type0 && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
                            ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE);
    if ((ctx->req_len == 3U) && (dll_is_fallback_command(ctx))) {
        /* Table B.2/43、Table 47 T9：T_FBD 之后 OPERATE -> SIO。 */
        dll_handle_fallback_command(ctx);
    }
    else if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        /* 转换命令在 OPERATE 中非法。上报它但保持 DL
           模式：主站在健康链路上从不发送它，且此处 ST rc=2/rc=3
           恢复只会增加抖动。 */
        ctx->framing_errors++;
    }
    else if (type0 && (ctx->req_len == 2U)) {
#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
        /* Table 57/67（SDCI_TC_0038/0049）：OPERATE 生效期间的 TYPE_0 页面 READ
           是复位请求。不要应答它；返回 STARTUP，使下一次读
           被当作启动探测处理。TYPE_0 页面写保持
           其正常行为。 */
        if (((mc & IOLINK_MC_RW_MASK) != 0U) &&
            ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
            (ctx->m_seq_type != IOLINK_M_SEQ_TYPE_0)) {
            ctx->framing_errors++;
            ctx->frame_index = 0U;
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            return false; /* 复位路径跳过公共尾部清理 */
        }
#endif
        dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
    }
    else if (page_write && ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
        /* OPERATE 中的 MasterCommand 页面写（0x9A/0x98/0x99/...）。ST 在
           每种 DL 模式下都处理它们，只改变状态，从不把它们
           当作非法 M-sequence。 */
        dll_handle_master_command(ctx, ctx->frame_buf[2]);
    }
    else if (page_write) {
        /* DPP 参数写（例如 MasterCycleTime）：仅含 CKS 的确认。 */
        dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
    }
    else if (type0) {
        /* ISDU/诊断上的 Type-0 OD 写：仅含 CKS 的确认。 */
        dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
    }
    else {
        /* 固定的 TYPE_1_x/TYPE_2_x READ 可能仅为 MC + CKT（req_len == 2）；
           用 CKT 类型位而非长度来区分。 */
        dll_handle_operate_type1_2(ctx);
    }
    return true;
}

/**
 * @brief 按 DL 状态分发已校验的帧。
 *
 * @return 当帧被拒绝且未推进状态、调用方必须跳过
 *         公共尾部清理时返回 false。
 */
static bool dll_handle_valid_frame(iolink_dll_ctx_t* ctx)
{
    ctx->last_valid_frame_ms = dll_time_ms(ctx);
    const bool was_establishing =
        (ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) || (ctx->state == IOLINK_DLL_STATE_STARTUP);
#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
    if (dll_reject_startup_operate(ctx, was_establishing)) {
        return false;
    }
#endif
    if (was_establishing) {
        dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
        ctx->dsio_deadline_ms = 0U; /* 有效报文取消 T_DSIO */
    }

    if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
        dll_dispatch_preoperate(ctx, was_establishing);
    }
    else if (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) {
        dll_dispatch_estab_com(ctx);
    }
    else if (ctx->state == IOLINK_DLL_STATE_OPERATE) {
        return dll_dispatch_operate(ctx);
    }
    return true;
}

/** @brief 清除每帧簿记并刷新 ISR 的 ISDU 快照。 */
static void dll_finish_frame(iolink_dll_ctx_t* ctx)
{
    ctx->frame_index = 0U;
    ctx->frame_replied_by_isr = 0U;
    /* 在下一主站帧之前，为 ISR 读泵反映任何 ISDU 状态变化
     * （例如完成的写请求）。 */
    dll_isdu_sync_snapshot(ctx);
}

static void dll_process_complete_frame(iolink_dll_ctx_t* ctx)
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
       坏帧）证明链路仍被驱动。 */
    dll_mark_master_activity(ctx);

    const dll_frame_result_t result = dll_frame_validate(ctx);
    if (result == DLL_FRAME_VALID) {
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

/** 在恢复性降级到 COM1 之后重新应用配置的工作 COM。 */
static void dll_restore_operating_baudrate(iolink_dll_ctx_t* ctx)
{
    /* 不要在应答过程中重配 UART：等待暂存的主应答
     * （isr_tx_pending）或 TX BYTE 字节送入
     * （isr_tx_active）排空。TX FRAME 传输由 PHY 拥有，此处
     * 不可见：若在传输进行时改变波特率，
     * PHY 会在其临界区中中止它，主站会重试该请求。 */
    if ((ctx->isr_tx_active != 0U) || (ctx->isr_tx_pending != 0U)) {
        return;
    }
    if ((ctx->operating_baudrate != IOLINK_BAUDRATE_UNSET) &&
        (ctx->baudrate != ctx->operating_baudrate)) {
        (void) iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
    }
}

/** @brief 消费 ISR 主站活动标志（仅时间戳，不计周期数）。 */
static void dll_take_isr_master_activity(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_master_activity != 0U) {
        ctx->isr_master_activity = 0U;
        /* 仅时间戳：Back-to-box 周期计数每个服务请求
         * 精确推进一次（此处用于 ISR 应答的帧，或在
         * dll_process_complete_frame 中用于主上下文应答的帧）。 */
        dll_mark_master_activity_ts(ctx);
    }
}

/**
 * @brief 应用数据 ISR 仅作标记的截止时间请求。
 *
 * 两个 64 位截止时间由主上下文持有，因此绝不从
 * 中断上下文写入 64 位值（撕裂读会触发虚假的 T_DSIO/T_FBD）。
 * ISR 设置标志；主泵在同一轮的任何截止时间检查之前，
 * 在此设置或清除截止时间。
 */
static void dll_apply_isr_deadline_requests(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_fallback_pending != 0U) {
        ctx->isr_fallback_pending = 0U;
        /* 仅在 DL 模式仍为 FALLBACK 时设置：后续命令可能
         * 已离开该模式，此时 T_FBD 截止时间将是虚假的。 */
        if (ctx->state == IOLINK_DLL_STATE_FALLBACK) {
            /* Table 47 T8/T9：T_FBD 为 3 个 MasterCycleTime，上限 500 ms。
             * 参考点是本次主上下文轮询，比帧结束晚一轮；
             * 这只会延长 T_FBD（绝不缩短它），
             * 至多一个主循环延迟。 */
            uint32_t t_fbd_ms = 500U;
            if (ctx->min_cycle_time_us > 0U) {
                t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
                if (t_fbd_ms > 500U) {
                    t_fbd_ms = 500U;
                }
            }
            ctx->fallback_deadline_ms = dll_time_us(ctx) / 1000U + t_fbd_ms;
        }
    }
    if (ctx->fallback_clear_request != 0U) {
        ctx->fallback_clear_request = 0U;
        /* 已应用非 FALLBACK 的命令：丢弃任何待处理的 T_FBD。 */
        if (ctx->state != IOLINK_DLL_STATE_FALLBACK) {
            ctx->fallback_deadline_ms = 0U;
        }
    }
    if (ctx->isr_dsio_clear != 0U) {
        ctx->isr_dsio_clear = 0U;
        ctx->dsio_deadline_ms = 0U;
    }
}

/**
 * @brief 将 ISR 暂存的 ISDU 写交给传送层。
 *
 * ISR 在环中暂存完整段；主泵拥有传送层
 * 和响应缓冲区。每次迭代从环中拷贝一条记录，
 * 释放其槽位（以便 ISR 复用），然后在中断使能的情况下
 * 运行（可能较慢的）传送层。不涉及临界区。
 */
static void dll_drain_staged_isdu_write(iolink_dll_ctx_t* ctx)
{
    for (;;) {
        const iolink_od_rec_t* record =
            (const iolink_od_rec_t*) iolink_ring_peek(&ctx->isr_od_ring);
        if (record == NULL) {
            return;
        }
        uint8_t staged_fc = record->flowctrl;
        uint8_t staged_len = record->len;
        if (staged_len > IOLINK_OD_MAX_SIZE) {
            staged_len = (uint8_t) IOLINK_OD_MAX_SIZE;
        }
        uint8_t staged_od[IOLINK_OD_MAX_SIZE];
        for (uint8_t i = 0U; i < staged_len; i++) {
            staged_od[i] = record->data[i];
        }
        iolink_ring_release(&ctx->isr_od_ring);
        if (staged_len > 0U) {
            iolink_isdu_od_write(&ctx->isdu, staged_fc, staged_od, staged_len);
        }
    }
}

/** @brief 基于完成的帧/RX 超时的 ST 风格主站丢失监控。 */
static void dll_update_master_lost(iolink_dll_ctx_t* ctx, uint32_t now_ms)
{
    const bool was_lost = ctx->master_lost;

    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) && (ctx->master_lost_cycles != 0U)) {
        uint32_t cycle_ms = (dll_master_cycle_us(ctx) + 999U) / 1000U;
        if (cycle_ms == 0U) {
            cycle_ms = 1U;
        }
        uint32_t elapsed = now_ms - ctx->master_last_activity_ms;
        uint32_t missed = elapsed / cycle_ms;
        if (missed > 0xFFU) {
            missed = 0xFFU;
        }
        ctx->master_lost_count = (uint8_t) missed;
        ctx->master_lost = (ctx->master_lost_count >= ctx->master_lost_cycles);
    }
    else {
        ctx->master_lost_count = 0U;
        ctx->master_lost = false;
    }

    if (ctx->master_lost != was_lost) {
        if (ctx->master_lost) {
            IOLINK_LOG_WARN(&ctx->log, "master lost: %u missed cycles",
                            (unsigned) ctx->master_lost_count);
        }
        else {
            IOLINK_LOG_INFO(&ctx->log, "master online");
        }
    }
}

/**
 * @brief 1 s 空闲链路诊断计数器。
 *
 * 这不是 ST 风格的主站丢失提示；它不会离开当前 DL
 * 模式，因此迟到的主站可在现有模式/波特率上恢复。
 */
static void dll_check_idle_timeout(iolink_dll_ctx_t* ctx, uint32_t now_ms)
{
    if ((ctx->last_activity_ms != 0U) && (now_ms - ctx->last_activity_ms > 1000U)) {
        /* 静默链路不是响应时间违规：单独计数，使
         * timeout_errors 仍是真正的 t_A 指标。 */
        ctx->idle_timeouts++;
        ctx->last_activity_ms = now_ms;
    }
}

/**
 * @brief Table 47 T10：唤醒后在 T_DSIO 内没有有效报文会使
 * 设备返回 SIO。仅在仍等待第一条报文时适用。
 */
static void dll_check_dsio_deadline(iolink_dll_ctx_t* ctx, uint64_t now_ms64)
{
    const uint64_t dsio_deadline = ctx->dsio_deadline_ms;
    if ((ctx->phy_mode != IOLINK_PHY_MODE_SIO) && (dsio_deadline != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
         (ctx->state == IOLINK_DLL_STATE_STARTUP))) {
        if (now_ms64 > dsio_deadline) {
            ctx->dsio_deadline_ms = 0U;
            ctx->wakeup_seen = false;
            ctx->timeout_errors++;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }
}

/**
 * @brief Table 47 T8/T9 + Table 43：收到 FALLBACK MasterCommand 后，设备
 * 在 T_FBD（3 个 MasterCycleTime，最多 500 ms）后切换到 SIO。
 * 应答已在处理命令时发送。
 */
static void dll_check_fallback_deadline(iolink_dll_ctx_t* ctx, uint64_t now_ms64)
{
    const uint64_t fallback_deadline = ctx->fallback_deadline_ms;
    if ((fallback_deadline != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_FALLBACK) || (ctx->state == IOLINK_DLL_STATE_OPERATE) ||
         (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) ||
         (ctx->state == IOLINK_DLL_STATE_PREOPERATE))) {
        if (now_ms64 >= fallback_deadline) {
            ctx->fallback_deadline_ms = 0U;
            IOLINK_LOG_WARN(&ctx->log, "fallback timeout: switching to SIO");
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }
}

/**
 * @brief 强制执行 t_PD 保护窗口。
 *
 * @return 当保护有效且调用方必须直接返回、不处理
 *         邮箱帧时返回 true。
 */
static bool dll_check_t_pd(iolink_dll_ctx_t* ctx)
{
    if (!dll_t_pd_active(ctx)) {
        return false;
    }
    if (dll_drain_rx(ctx)) {
        ctx->timing_errors++;
        ctx->t_pd_violations++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
    }
    return true;
}

/**
 * @brief SIO：检测 C/Q 唤醒并启动通信窗口。
 *
 * @return 当 DLL 处于（或保持）SIO 模式且调用方必须
 *         返回时返回 true。
 */
static bool dll_handle_sio_wakeup(iolink_dll_ctx_t* ctx)
{
    if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
        return false;
    }
    if ((ctx->frame_index == 0U) && dll_phy_has_wakeup(ctx)) {
        if (dll_phy_detect_wakeup(ctx) > 0) {
            ctx->wakeup_seen = true;
            dll_set_state(ctx, IOLINK_DLL_STATE_AWAITING_COMM);
            ctx->wakeup_deadline_us = dll_time_us(ctx) + IOLINK_T_WU_US;
            /* Table 47 T10：没有有效报文时，设备在 T_DSIO 内
               （60..300 ms，默认 300 ms）返回 SIO。 */
            ctx->dsio_deadline_ms = dll_time_us(ctx) / 1000U + IOLINK_T_DSIO_MS;
            iolink_dll_set_sdci_mode(ctx);
            /* 唤醒会开启一个新的通信建立窗口。
               重置不活动计时器，使先前交换后的再次唤醒
               不会被陈旧的活动时间戳立刻终止。 */
            ctx->last_activity_ms = dll_time_ms(ctx);
            dll_mark_master_activity(ctx); /* ST StackOnWakeUp 重启 Timer B */
        }
    }
    return true;
}

/** @brief ST Timer A：帧内字节间隙会中止该帧并重新监听。 */
static void dll_check_t_byte(iolink_dll_ctx_t* ctx)
{
    if ((ctx->frame_index > 0U) && (ctx->enforce_timing) && (ctx->t_byte_limit_us > 0U)) {
        /* ISR 写入字节时间戳的低 32 位；无符号
         * 差值对毫秒级 t_byte 窗口是回绕安全的。 */
        const uint32_t last_byte_us = ctx->last_byte_us;
        if (last_byte_us != 0U) {
            const uint32_t now_us = (uint32_t) dll_time_us(ctx);
            if ((uint32_t) (now_us - last_byte_us) > ctx->t_byte_limit_us) {
                IOLINK_LOG_DEBUG(&ctx->log, "t_byte violation: %lu us",
                                 (unsigned long) (now_us - last_byte_us));
                ctx->timing_errors++;
                ctx->t_byte_violations++;
                ctx->framing_errors++;
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                  IOLINK_EVENT_TYPE_WARNING);
                /* 中止当前帧并重新监听；DL 模式
                   不变。 */
                ctx->frame_index = 0U;
            }
        }
    }
}

/** @brief T_WU：在唤醒窗口内继续等待第一条报文。 */
static bool dll_wakeup_window_active(iolink_dll_ctx_t* ctx)
{
    if ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) && (ctx->enforce_timing)) {
        if ((ctx->wakeup_deadline_us != 0U) && (dll_time_us(ctx) < ctx->wakeup_deadline_us)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief 将 ISR 发布的帧移入主上下文并分发。
 *
 * 帧通过 SPSC 邮箱环到达：拷贝最旧的记录，
 * 释放其槽位，然后运行校验/分发。无需临界区
 * （ISR 从不写入消费者正在读取的槽位），
 * 且记录携带自身的长度和 eager 标志。
 */
static void dll_consume_isr_mailbox(iolink_dll_ctx_t* ctx)
{
    const iolink_frame_rec_t* record =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx->isr_frame_ring);
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
    /* 记录随其字节携带 eager 标志，因此无需跨帧
     * 快照。 */
    ctx->frame_replied_by_isr = record->eager;
    /* 拷贝后释放：ISR 随后可复用该槽位，而另一个
     * 槽位保持完整。无需临界区，因为 ISR 从不
     * 写入 'out' 处的槽位。 */
    iolink_ring_release(&ctx->isr_frame_ring);
    /* 邮箱帧是接收流量：刷新不活动时间戳和字节间隙
       时间戳，与流式 RX 路径完全一致。否则 1 s 空闲
       监控会复位健康的 ISR 链路。 */
    ctx->last_activity_ms = dll_time_ms(ctx);
    ctx->last_byte_us = (uint32_t) dll_time_us(ctx);
    dll_process_complete_frame(ctx);
}

/** @brief 主泵主体；公共入口随后投递状态。 */
static void dll_process_body(iolink_dll_ctx_t* ctx)
{
    dll_take_isr_master_activity(ctx);
    dll_apply_isr_deadline_requests(ctx);

    /* 捕获的 PD-in 缓冲区在应答完成时由 TX BYTE 泵释放。
     * TX FRAME 类型的 PHY 可通过 tx_busy() 报告传输结束；
     * 没有该钩子时，库无法区分“仍在线上”与
     * “收发器停滞”，因此会保持租约直到下次捕获，并由
     * pd_in_skips 暴露停滞。该临界区防止数据 ISR 在
     * 轮询与释放之间捕获新缓冲区。 */
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if ((ctx->phy != NULL) && (ctx->phy->tx_busy != NULL)) {
        IOLINK_CRITICAL_ENTER();
        if ((ctx->pd_in_captured != 0xFFU) && (ctx->phy->tx_busy(ctx->phy->user) == 0U)) {
            ctx->pd_in_captured = 0xFFU;
        }
        IOLINK_CRITICAL_EXIT();
    }
#endif

    /* 在 SIO/上电提前返回之前恢复工作 COM：
     * 恢复性降级到 COM1 不能让 UART 停留在错误波特率。 */
    dll_restore_operating_baudrate(ctx);

    dll_drain_staged_isdu_write(ctx);
    iolink_isdu_process(&ctx->isdu);
    /* 服务可能已在此完成：刷新 ISR 响应快照。 */
    dll_isdu_sync_snapshot(ctx);
    dll_poll_diagnostics(ctx);
    /* 在上述步骤期间，ISR 中可能已完成一帧。 */
    dll_take_isr_master_activity(ctx);

    /* 在延迟活动刷新之后采样，使下面的经过时间减法
     * 不会看到比 now_ms 更新的时间戳。单次时钟采样保持
     * 毫秒与微秒视图一致。 */
    const uint64_t now_ms64 = dll_time_us(ctx) / 1000U;
    const uint32_t now_ms = (uint32_t) now_ms64;

    dll_update_master_lost(ctx, now_ms);
    dll_check_idle_timeout(ctx, now_ms);
    dll_check_dsio_deadline(ctx, now_ms64);
    dll_check_fallback_deadline(ctx, now_ms64);

    if (dll_check_t_pd(ctx)) {
        return;
    }
    if (dll_handle_sio_wakeup(ctx)) {
        return;
    }
    dll_check_t_byte(ctx);
    if (dll_wakeup_window_active(ctx)) {
        return;
    }

    dll_consume_isr_mailbox(ctx);

    /* 撤销同一次调用中的恢复性 COM1 降级（SDCI 路径会到达此处）。 */
    dll_restore_operating_baudrate(ctx);
}

void iolink_dll_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }
    dll_process_body(ctx);
    /* 基于电平的应用通知：状态钩子对设备层和直接 DLL
     * 用户的唯一运行位置。设备层在其事件/DS 服务之后
     * 不再 flush。 */
    iolink_dll_flush_state_callbacks(ctx);
}

/* -------------------------------------------------------------------------
 * 公共配置与查询 API
 * ------------------------------------------------------------------------- */

iolink_dll_state_t iolink_dll_get_state(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->state : IOLINK_DLL_STATE_STARTUP;
}

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->phy_mode : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->baudrate : IOLINK_BAUDRATE_COM2;
}

/** @brief 根据 M-sequence 时序计算设备 MinCycleTime（Table B.3）。
 *
 * 使用 ST ministry 公式：mseq_us = (reply_octets*14 + request_octets*12
 * + 6) * 1e6 / baud。取 PD 写与 PD/ISDU 读的最坏情况，
 * 向上取整到 0.1 ms 单位，并钳制到应用下限。 */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    const uint32_t baud = dll_get_baud_hz(ctx);
    const uint32_t pdi = ctx->pd_in_len_current;
    const uint32_t pdo = ctx->pd_out_len_current;
    const uint32_t od = ctx->od_width;
    /* 写请求：MC+CKT + PD-out + OD；应答：PD-in + CKS。
     * 读请求：MC+CKT + PD-out（无 OD）；应答：OD + PD-in + CKS。
     * 这适用于所有 Annex A 类型：固定类型只是在不携带的
     * 字段中为 0 字节（参见 A.6-A.13）。 */
    const uint32_t t_write = ((pdi + 1U) * 14U + (2U + pdo + od) * 12U + 6U) * 1000000U / baud;
    const uint32_t t_read = ((od + pdi + 1U) * 14U + (2U + pdo) * 12U + 6U) * 1000000U / baud;
    const uint32_t t = (t_write > t_read) ? t_write : t_read;
    uint32_t units = (t + 99U) / 100U; /* 向上取整到 0.1 ms */
    const uint32_t app_units = (ctx->min_cycle_time_app_us + 99U) / 100U;
    if (units < app_units) {
        units = app_units;
    }
    if (units < 4U) {
        units = 4U; /* Table B.3 最小值为 0.4 ms。 */
    }
    return (units > 255U) ? 255U : (uint8_t) units;
}

int iolink_dll_set_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) return -1;
    const bool changing = (ctx->baudrate != baudrate);
    if (changing) {
        IOLINK_LOG_INFO(&ctx->log, "baudrate %u -> %u", (unsigned) ctx->baudrate,
                        (unsigned) baudrate);
    }
    ctx->baudrate = baudrate;
    if (ctx->phy->set_baudrate != NULL) ctx->phy->set_baudrate(ctx->phy->user, baudrate);
    if (changing) {
        /* UART 重配时 PHY 会中止进行中的应答；
         * 丢弃匹配的 ISR TX 状态，使其无法在下次请求时
         * 恢复陈旧的帧。 */
        ctx->isr_tx_pending = 0U;
        ctx->isr_tx_active = 0U;
        ctx->isr_tx_has_cks = 0U;
        ctx->isr_tx_idx = 0U;
        ctx->pd_in_captured = 0xFFU;
    }
    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);
    ctx->min_cycle_time_us = (uint32_t) iolink_dll_compute_min_cycle_time(ctx) * 100U;
    return 0;
}

int iolink_dll_set_operating_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) {
        return -1;
    }
    if ((uint8_t) baudrate > (uint8_t) IOLINK_BAUDRATE_COM3) {
        return -1;
    }
    ctx->operating_baudrate = baudrate;
    return iolink_dll_set_baudrate(ctx, baudrate);
}

int iolink_dll_set_pd_length(iolink_dll_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if ((ctx == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
        (pd_out_len > IOLINK_PD_OUT_MAX_SIZE))
        return -1;
    /* 数据 ISR 读取 pd_out_len_current 来成帧请求；
     * 原子发布 PD-in/PD-out 对，使其无法观察到半更新。 */
    IOLINK_CRITICAL_ENTER();
    ctx->pd_in_len_current = pd_in_len;
    ctx->pd_out_len_current = pd_out_len;
    IOLINK_CRITICAL_EXIT();
    return 0;
}

void iolink_dll_get_pd_length(const iolink_dll_ctx_t* ctx, uint8_t* pd_in_len, uint8_t* pd_out_len)
{
    if (ctx == NULL) return;
    if (pd_in_len) *pd_in_len = ctx->pd_in_len_current;
    if (pd_out_len) *pd_out_len = ctx->pd_out_len_current;
}

int iolink_dll_set_sio_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;
    /* SIO 转换会中止 PHY 中任何进行中的应答；丢弃
     * 匹配的 ISR TX 状态，使陈旧的 active 索引不会泄漏到下一个
     * SDCI 会话（SIO 中 int_active() 为 false，因此没有东西会完成它）。 */
    ctx->isr_tx_pending = 0U;
    ctx->isr_tx_active = 0U;
    ctx->isr_tx_has_cks = 0U;
    ctx->isr_tx_idx = 0U;
    ctx->pd_in_captured = 0xFFU;
    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_SDCI);
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    return 0;
}

void iolink_dll_get_stats(const iolink_dll_ctx_t* ctx, iolink_dll_stats_t* out_stats)
{
    if ((ctx == NULL) || (out_stats == NULL)) return;
    out_stats->crc_errors = ctx->crc_errors;
    out_stats->timeout_errors = IOLINK_STAT_TOTAL(ctx, timeout_errors);
    out_stats->idle_timeouts = ctx->idle_timeouts;
    out_stats->framing_errors = IOLINK_STAT_TOTAL(ctx, framing_errors);
    out_stats->timing_errors = ctx->timing_errors;
    out_stats->t_ren_violations = ctx->t_ren_violations;
    out_stats->t_cycle_violations = ctx->t_cycle_violations;
    out_stats->t_byte_violations = ctx->t_byte_violations;
    out_stats->t_pd_violations = ctx->t_pd_violations;
    out_stats->total_retries = ctx->total_retries;
    out_stats->voltage_faults = ctx->voltage_faults;
    out_stats->overtemp_faults = ctx->overtemp_faults;
    out_stats->short_circuits = ctx->short_circuits;
    out_stats->isr_tx_dropped = IOLINK_STAT_TOTAL(ctx, isr_tx_dropped);
}

void iolink_dll_reset_error_counters(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return;

    IOLINK_CRITICAL_ENTER();
    ctx->crc_errors = 0U;
    ctx->timeout_errors = 0U;
    ctx->framing_errors = 0U;
    ctx->timing_errors = 0U;
#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    ctx->isr_stat.timeout_errors = 0U;
    ctx->isr_stat.framing_errors = 0U;
#endif
    IOLINK_CRITICAL_EXIT();
}

void iolink_dll_reset_diagnostics(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return;

    /* B.2.20/Table 101：ErrorCount 统计上电或复位以来的错误，
       诊断/状态由重置 SystemCommand 清除。事件
       引擎拥有 DeviceStatus 和 DetailedDeviceStatus；reset() 保留
       绑定的时钟。 */
#if IOLINK_FEATURE_EVENTS
    iolink_events_reset(&ctx->events);
    ctx->phy_faults_rearm = 0U;
    ctx->phy_faults_last = 0U;
#endif
    iolink_dll_reset_error_counters(ctx);
}

void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t* ctx, bool enable)
{
    if (ctx != NULL) ctx->enforce_timing = enable;
}

void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t* ctx, uint32_t limit_us)
{
    if (ctx == NULL) return;
    ctx->t_ren_limit_us = limit_us;
    ctx->t_ren_override = (limit_us != 0U);
}

/* 根据当前 DLL 状态，推导首字节（MC 字节）为 @p mc_byte 的
 * M-sequence 的期望请求长度。纯只读函数，因此
 * ISR 泵可直接调用；由两条 RX 路径共享。 */
static uint8_t dll_compute_req_len(const iolink_dll_ctx_t* ctx, uint8_t mc_byte)
{
    const uint8_t type0_channel = (uint8_t) (mc_byte & IOLINK_MC_COMM_CHANNEL_MASK);
    const bool type0_od_write = (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0) &&
                                ((mc_byte & IOLINK_MC_RW_MASK) == 0U) &&
                                ((type0_channel == IOLINK_MC_CHANNEL_PAGE) ||
                                 (type0_channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                                 (type0_channel == IOLINK_MC_CHANNEL_ISDU));
    if (type0_od_write) {
        /* Figure A.5：Type-0 写只携带一个 OD 字节，
           无论 OPERATE M-sequence 使用何种 OD 宽度。 */
        return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + 1U);
    }
    /* STARTUP/PREOPERATE 期望 Type 0；ESTAB_COM 和 OPERATE 使用配置的
       类型（C5/Table 47）。配置为多字节类型后，转换命令是唯一的
       Type-0 交换。 */
    uint8_t type_bits = dll_expected_type_bits(ctx);
    bool transition_cmd =
        (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) && (mc_byte == IOLINK_MC_TRANSITION_COMMAND);
    if (!transition_cmd && (type_bits != IOLINK_MSEQ_TYPE_0)) {
        const uint8_t od_req = ((mc_byte & IOLINK_MC_RW_MASK) == 0U) ? ctx->od_width : 0U;
        return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current + od_req);
    }
    return 2U;
}

uint8_t iolink_dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t mc)
{
    if (ctx == NULL) {
        return 0U;
    }
    return dll_compute_req_len(ctx, mc);
}
