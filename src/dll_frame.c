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
#include "iolinki/port.h"
#include "iolinki/internal/wiring.h"
#include "dll_internal.h"
#include "iolinki/internal/dll_sm.h"
#include <string.h>

/**
 * @file dll_frame.c
 * @brief DLL 帧路径：类型判定、应答发送与各通道处理。
 *
 * 从 M-sequence 类型校验开始，覆盖 MasterCommand、
 * Fallback、页通道、诊断/ISDU 通道与 PD 交换的处理，
 * 以及 PHY 故障轮询和帧级时序检查。
 */

/* -------------------------------------------------------------------------
 * M-sequence 类型判定与发送
 * ------------------------------------------------------------------------- */

/** @brief 返回配置类型对应的 CKT M-sequence 类型位（A.1.3，Table A.3）。 */
uint8_t dll_mseq_type_bits(uint8_t m_seq_type)
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
bool dll_type_allowed(const iolink_dll_shared_t* ctx, const uint8_t* frame)
{
    const uint8_t rx_type = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    const uint8_t ch = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    uint8_t exp_type = IOLINK_MSEQ_TYPE_0;

    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                                                     (frame[0] != IOLINK_MC_TRANSITION_COMMAND))) {
        if ((ch == IOLINK_MC_CHANNEL_PAGE) && (rx_type == IOLINK_MSEQ_TYPE_0)) {
            return true;
        }
        exp_type = dll_mseq_type_bits(ctx->c_m_seq_type);
    }
    return rx_type == exp_type;
}

/**
 * @brief 返回设备在当前状态下期望的 CKT 类型位。
 *
 * 在 STARTUP 和 PREOPERATE 中每条报文都是 Type 0（Table A.3，CKT 位 00）；
 * 在 OPERATE 中期望配置的 M-sequence 类型。
 */
uint8_t dll_expected_type_bits(const iolink_dll_shared_t* ctx)
{
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || (ctx->state == IOLINK_DLL_STATE_ESTAB_COM)) {
        return dll_mseq_type_bits(ctx->c_m_seq_type);
    }
    return IOLINK_MSEQ_TYPE_0;
}

/* 契约与参数含义见 dll_shared_internal.h 的声明。 */
uint8_t dll_req_len_for(const iolink_dll_shared_t* ctx, uint8_t mc, uint8_t ckt)
{
    bool type0 = dll_ckt_is_type0(ckt);
    if (!type0) {
        /* 设备此刻只接受 Type-0（STARTUP/PREOPERATE）时，长度同样由 MC 决定，
           与帧自报的类型无关。 */
        type0 = (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0);
    }
    if (type0) {
        return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + (dll_mc_is_type0_od_write(mc) ? 1U : 0U));
    }
    if ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) && (mc == IOLINK_MC_TRANSITION_COMMAND)) {
        return IOLINK_M_SEQ_HEADER_LEN;
    }
    const uint8_t od_req = ((mc & IOLINK_MC_RW_MASK) == 0U) ? ctx->c_od_width : 0U;
    return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->c_pd_out_len_current + od_req);
}

/**
 * @brief 通过 ISR 传送层发送设备应答。
 *
 * 将字节交给原始字节/帧 TX 钩子；ISR 逐个送入（TX BYTE）
 * 或由 PHY 发送整帧（TX FRAME）。应答字节（包括
 * CKS）由共享处理函数构建，因此两种方式的帧布局
 * 相同。
 *
 * 槽位 0/1 归主上下文独占，ISR 有自己的槽位。槽位选择、载荷复制、
 * 暂存元数据与 `isr_tx_pending` 的发布在同一个短临界区内完成：
 * “ISR 是否正在流式发送”的检查与发布必须是一次操作，且被拒绝的
 * 暂存不得推进槽位或写载荷——TX BYTE 的在途帧按指针引用发送槽，
 * 写到它就会换掉尚未发出的字节。ISR 取走时同样在临界区内一次性
 * 复制元数据并清标志，随后在自己的流式状态里持有副本。
 * 只有 SINGLE 档从主上下文驱动 PHY（该档没有数据 ISR）；ISR/SMP 档的
 * 应答只能由 request_service() 通知的 ISR 发送。
 */
void dll_send(iolink_dll_main_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (ctx->shared.c_phy == NULL) || (data == NULL) || (len == 0U)) {
        return;
    }
    if (len > sizeof(ctx->shared.isr_tx_payload[0])) {
        return;
    }
    /* 该帧已由 ISR 即时应答：主泵只消费内容，绝不再发第二帧。 */
    if (ctx->frame_replied_by_isr != 0U) {
        ctx->frame_replied_by_isr = 0U;
        return;
    }
    /* 槽位只在主上下文内推进；这里先取值，是否真正使用由下面的接受分支决定。 */
    const uint8_t slot = ctx->isr_tx_next;
    /* 检查“ISR 正在流式发送”与发布暂存必须对 ISR 是一次操作：
     * 分开做时，ISR 可在检查之后启动即时应答，再由下一次取走把在途帧从
     * 中间替换掉（在途帧因此丢掉后半段与 CKS）。SINGLE 档没有数据 ISR，
     * 该检查不适用，由下面的直发分支结束上一帧。 */
    bool rejected;
    IOLINK_CRITICAL_ENTER();
#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
    rejected = (ctx->shared.isr_tx_active != 0U);
#else
    rejected = false;
#endif
    if (rejected) {
        ctx->shared.isr_tx_dropped++;
    }
    else {
        /* 槽位与载荷只在接受后推进：被拒绝的暂存不得碰在途槽。 */
        ctx->isr_tx_next = (uint8_t) (ctx->isr_tx_next ^ 1U);
        (void) memcpy(ctx->shared.isr_tx_payload[slot], data, len);
        ctx->shared.isr_tx_pending_slot = slot;
        ctx->shared.stage_len = (uint8_t) len;
        ctx->shared.stage_has_cks = 0U;
        IOLINK_FENCE();                  /* 暂存字节先于发布 */
        ctx->shared.isr_tx_pending = 1U; /* 最后发布 */
    }
    IOLINK_CRITICAL_EXIT();
    if (rejected) {
        return;
    }
    /* 应答开始边沿：关闭 t_A 软件镜像（PHY 的 response_timer_stop 另行停止）。 */
    dll_timer_cancel(ctx, IOLINK_TIMER_T_A);

    if (ctx->shared.c_phy->request_service != NULL) {
        /* 在发布之后、且在任何关中断窗口之外通知 PHY：
         * 该钩子属于端口代码（可能进行 SPI 访问或获取锁）。
         * 其间自然产生的数据中断只会排空暂存
         * 应答，随后额外的服务请求便无事可做。 */
        ctx->shared.c_phy->request_service(ctx->shared.c_phy->self);
        return;
    }

#if IOLINK_CONCURRENCY == IOLINK_CONCURRENCY_SINGLE
    /* SINGLE 档没有数据 ISR：主上下文是唯一发送者，只能自己驱动
     * TX 钩子。上一帧没有上下文可以推进它，因此先结束它再开始新帧，
     * 否则 isr_tx_active 会永久保持。 */
    ctx->shared.isr_tx_pending = 0U;
    if (ctx->shared.isr_tx_active != 0U) {
        if (ctx->shared.c_phy->tx_done != NULL) {
            ctx->shared.c_phy->tx_done(ctx->shared.c_phy->self);
        }
        ctx->shared.isr_tx_active = 0U;
        ctx->shared.isr_tx_dropped++; /* 上一帧未发完即被收尾 */
    }
    dll_isr_start_staged_tx(&ctx->shared, slot, (uint8_t) len, 0U);
#else
    /* ISR/SMP 档没有 request_service()：暂存应答无人发送。
     * iolink_device_init() 已拒绝这种 PHY；直接使用 DLL 的调用方
     * 违反同一契约，此处计数丢弃，使配置错误可被观测而非静默。 */
    ctx->shared.isr_tx_pending = 0U;
    ctx->shared.isr_tx_dropped++;
#endif
}

/* -------------------------------------------------------------------------
 * 时序、主站命令与 OPERATE 处理
 * ------------------------------------------------------------------------- */

/** @brief 返回响应时间（t_REN）上限，单位 us（Table 10：500 us，与波特率无关）。 */
uint32_t dll_get_t_ren_limit_us(void)
{
    return IOLINK_T_REN_US;
}

/** @brief 返回当前生效的波特率，单位 Hz（编译期 `IOLINK_COM`）。 */
uint32_t dll_get_baud_hz(const iolink_dll_main_t* ctx)
{
    (void) ctx;
    return IOLINK_COM_HZ;
}

/** @brief 返回当前波特率下的一个位时间（us）。 */
uint32_t dll_get_t_bit_us(const iolink_dll_main_t* ctx)
{
    (void) ctx;
    return 1000000U / IOLINK_COM_HZ;
}

/** @brief 返回当前波特率下的字节间超时（`IOLINK_T_BYTE_WINDOW_BITS` 个位时间），单位 us。 */
uint32_t dll_get_t_byte_limit_us(const iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return dll_get_t_bit_us(ctx) * IOLINK_T_BYTE_WINDOW_BITS;
}

/** @brief 当掉电/PD 保护窗口（t_PD）仍有效时返回 true。 */
bool dll_t_pd_active(const iolink_dll_main_t* ctx)
{
    return dll_timer_armed(ctx, IOLINK_TIMER_T_PD) &&
           !dll_timer_due(ctx, IOLINK_TIMER_T_PD, dll_time_us(&ctx->shared));
}

/** @brief 排空 PHY 中所有待处理的 RX 字节；若读到任何字节则返回 true。 */
bool dll_drain_rx(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.c_phy == NULL)) {
        return false;
    }
    /* ISR 泵填充邮箱；排空它会丢弃在 t_pd 保护窗口
     * 有效期间到达的帧。丢弃数量必须可观测：现场用它区分
     * “主泵被阻塞”与“暂存环满”。 */
    if (!iolink_ring_is_empty(&ctx->shared.isr_frame_ring)) {
        const uint32_t dropped = iolink_ring_count(&ctx->shared.isr_frame_ring);
        while (iolink_ring_peek(&ctx->shared.isr_frame_ring) != NULL) {
            iolink_ring_release(&ctx->shared.isr_frame_ring);
        }
        ctx->t_pd_discards += dropped;
        return true;
    }
    return false;
}

/** @brief ST rc=3：协议错误丢弃该帧并降级到 STARTUP。
 *
 * ST 6362 库只在 Fallback MasterCommand
 * （T8/T9）或 T_DSIO 唤醒超时（T10）时返回 SIO。成帧/CRC 错误
 * 本身从不会离开当前模式。 */
void dll_protocol_error_to_startup(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->shared.fallback_count = 0U;
    ctx->invalid_frames++;
    ctx->frame_index = 0U;
    if (ctx->shared.state != IOLINK_DLL_STATE_STARTUP) {
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    }
}

/** @brief 判断是否为页通道 Type-0 WRITE 的 MasterCommand FALLBACK（Table B.2）。 */
bool dll_is_fallback_command(const iolink_dll_main_t* ctx)
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
void dll_handle_fallback_command(iolink_dll_main_t* ctx)
{
    /* 没有唤醒检测器时，PHY 无法再次离开 SIO，因此
     * 不支持 FALLBACK 转换：应答该 Type-0 写而不改变
     * DL 模式。 */
    if (dll_phy_has_wakeup(&ctx->shared)) {
        uint32_t t_fbd_ms = IOLINK_T_FBD_MAX_MS;
        if (ctx->min_cycle_time_us > 0U) {
            t_fbd_ms = (IOLINK_T_FBD_MASTER_CYCLES * ctx->min_cycle_time_us) / 1000U;
            if (t_fbd_ms > IOLINK_T_FBD_MAX_MS) {
                t_fbd_ms = IOLINK_T_FBD_MAX_MS;
            }
        }
        IOLINK_LOG_WARN(&ctx->shared.c_log, "fallback command: SIO in %lu ms",
                        (unsigned long) t_fbd_ms);
        /* 绝对截止时间仍按毫秒基准换算，与原比较（now_ms >= deadline_ms）逐拍一致。 */
        dll_timer_arm(ctx, IOLINK_TIMER_T_FBD,
                      (uint64_t) (dll_time_us(&ctx->shared) / 1000U + t_fbd_ms) * 1000U);
        dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
    }

    uint8_t resp[1];
    resp[0] = 0x00U;
    resp[0] = iolink_checksum6(resp, 1U);
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(&ctx->shared);
}

/** @brief MasterCommand 的目标状态；返回 false 表示该命令不迁移状态。 */
static bool dll_master_command_target(uint8_t cmd, iolink_dll_state_t current,
                                      iolink_dll_state_t* out)
{
    switch (cmd) {
        case IOLINK_CMD_DEVICE_PREOPERATE:
            *out = IOLINK_DLL_STATE_PREOPERATE;
            return true;
        case IOLINK_CMD_PROCESS_DATA_OPERATE:
            *out = IOLINK_DLL_STATE_OPERATE;
            return true;
        case IOLINK_CMD_DEVICE_OPERATE:
            /* 来自 PREOPERATE 的 DeviceOperate 会建立通信
               （ESTAB_COM）；随后的 PDOperate 0x98 进入 OPERATE。
               在 OPERATE/ESTAB 中它只是幂等的确认。 */
            *out = (current == IOLINK_DLL_STATE_PREOPERATE) ? IOLINK_DLL_STATE_ESTAB_COM
                                                            : IOLINK_DLL_STATE_OPERATE;
            return true;
        default:
            return false;
    }
}

/**
 * @brief 主上下文：将 MasterCommand 的 PD 有效位副作用落到发布字。
 *
 * 主上下文本就是 `pd_pub` 的写者：命令直接发布新的一代（代号前进，
 * 任何在途命令随之失效），语义与"只在本代有效"一致。
 */
static void dll_pd_valid_command(iolink_dll_main_t* ctx, bool valid)
{
    const IOLINK_ATOMIC_UINT_T pub = dll_pd_pub_load(&ctx->shared);
    dll_pd_publish(&ctx->shared, IOLINK_PD_PUB_ACTIVE(pub), valid);
}

/**
 * @brief 数据 ISR：将 PD 有效位写进命令字（ISR 是该字段的唯一写者）。
 *
 * 命令字记录值与当时 `pd_pub` 的代号，主上下文下一次发布即让该覆盖失效。
 */
static void dll_pd_valid_command_isr(iolink_dll_shared_t* shared, bool valid)
{
    const IOLINK_ATOMIC_UINT_T pub = dll_pd_pub_load(shared);
    iolink_atomic_store(&shared->pd_valid_cmd, IOLINK_PD_CMD_WORD(valid, IOLINK_PD_PUB_GEN(pub)));
}

/**
 * @brief 数据 ISR 侧应用 MasterCommand（只写共享区）。
 *
 * 对 FALLBACK（0x5A）返回 false，它需要 T_FBD 截止时间，由主上下文的
 * dll_handle_fallback_command() 处理。
 */
bool dll_apply_master_command(iolink_dll_shared_t* ctx, uint8_t cmd)
{
    if (cmd == IOLINK_CMD_FALLBACK) {
        return false;
    }
    if (cmd == IOLINK_CMD_DEVICE_STARTUP) {
        /* 协议错误记账（invalid_frames/frame_index）与 STARTUP 迁移都是
         * 主上下文字段：只登记一次性请求，本帧应答仍按计划发出。 */
        ctx->isr_device_startup_requested = 1U;
        return true;
    }
    iolink_dll_state_t target = ctx->state;
    if (!dll_master_command_target(cmd, ctx->state, &target)) {
        return true;
    }
    ctx->fallback_clear_request = 1U;
    if (cmd == IOLINK_CMD_PROCESS_DATA_OPERATE) {
        dll_pd_valid_command_isr(ctx, true);
    }
    else if (cmd == IOLINK_CMD_DEVICE_OPERATE) {
        dll_pd_valid_command_isr(ctx, false);
    }
    if (target != ctx->state) {
        dll_set_state_shared(ctx, target);
    }
    return true;
}

/**
 * @brief 主上下文侧应用 MasterCommand（与 ISR 侧共享同一份目标状态推导）。
 *
 * 状态迁移通过 dll_set_state() 完成，保留主站丢失簿记与逐状态回调。
 */
bool dll_apply_master_command_main(iolink_dll_main_t* ctx, uint8_t cmd)
{
    if (cmd == IOLINK_CMD_FALLBACK) {
        return false;
    }
    if (cmd == IOLINK_CMD_DEVICE_STARTUP) {
        dll_protocol_error_to_startup(ctx);
        return true;
    }
    iolink_dll_state_t target = ctx->shared.state;
    if (!dll_master_command_target(cmd, ctx->shared.state, &target)) {
        return true;
    }
    ctx->shared.fallback_clear_request = 1U;
    if (cmd == IOLINK_CMD_PROCESS_DATA_OPERATE) {
        dll_pd_valid_command(ctx, true);
    }
    else if (cmd == IOLINK_CMD_DEVICE_OPERATE) {
        dll_pd_valid_command(ctx, false);
    }
    if (target != ctx->shared.state) {
        dll_set_state(ctx, target);
    }
    return true;
}

/** @brief 主上下文中的 MasterCommand 页面写入：应用状态并发送 CKS。 */
/* CKS PD 状态位：位 6 为 0 表示“Process Data valid”。
 * 没有输入 PD 的设备不可能有无效输入 PD，因此始终上报
 * 有效；否则上报 @p pub 这一代的 PD 有效位（含 ISR 命令字的覆盖）。
 * 数据 ISR 用它与自己捕获的发布字共用一次加载。 */
uint8_t dll_pd_status_flags_at(const iolink_dll_shared_t* ctx, IOLINK_ATOMIC_UINT_T pub)
{
    if ((ctx->c_pd_in_len == 0U) && (ctx->c_pd_out_len_current > 0U)) {
        return 0U;
    }
    return dll_pd_valid_at(ctx, pub) ? 0U : 0x40U;
}

uint8_t dll_pd_status_flags(const iolink_dll_main_t* ctx)
{
    return dll_pd_status_flags_at(&ctx->shared, dll_pd_pub_load(&ctx->shared));
}

void dll_handle_master_command(iolink_dll_main_t* ctx, uint8_t cmd)
{
    if (cmd == IOLINK_CMD_FALLBACK) {
        dll_handle_fallback_command(ctx);
        return;
    }

    (void) dll_apply_master_command_main(ctx, cmd);

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(&ctx->shared);
}

/** @brief 处理 PREOPERATE 主站命令：在转换命令上推进到 ESTAB_COM。 */
void dll_handle_preoperate(iolink_dll_main_t* ctx, uint8_t mc, uint8_t ck)
{
    (void) ck;
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->shared.fallback_count = 0U;
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
void dll_handle_page_channel_read(iolink_dll_main_t* ctx, uint8_t mc)
{
    if (!dll_dp_page1_available(&ctx->shared)) {
        /* 没有设备身份就没有 DPP1：保持沉默，不发全零页。 */
        return;
    }
    uint8_t resp[2];
    resp[0] = iolink_isdu_direct_param_page1_octet(&ctx->shared.isdu,
                                                   (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
    resp[1] = 0x00U;
    resp[1] = iolink_checksum6(resp, 2U);
    dll_send(ctx, resp, 2);
    ctx->last_response_us = dll_time_us(&ctx->shared);
}

/**
 * @brief 按通信通道分发一条 OD 报文（C2，7.3.5.3）。
 *
 * 诊断通道：事件存储器（Table 58）。ISDU 通道：ISDU
 * 传送层。页通道和过程通道由调用方处理（页通道由
 * 启动探测和 Task 4 之前的旧 ISDU 路径处理）。若该通道
 * 在此处理则返回 true。
 */
static bool dll_dispatch_od(iolink_dll_main_t* ctx, uint8_t mc, const uint8_t* od_in,
                            uint8_t od_len, uint8_t* od_out)
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
            (void) dll_apply_master_command_main(ctx, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x01U)) {
            ctx->shared.master_cycle_time = od_in[0];
            /* DPP1 0x01 是页读应答的输入：ISR 的页读缓存必须失效。 */
            iolink_isdu_dp_page1_invalidate(&ctx->shared.isdu);
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x0FU)) {
            /* 7.3.5.3：通过 Direct Parameter 页面 1 的 SystemCommand（无响应）。 */
            iolink_isdu_execute_system_command(&ctx->shared.isdu, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) != 0U) && (od_len > 0U)) {
            /* 周期帧的 OD 槽位必须填满。没有可用的设备身份时不构造臆造页，
             * 该字节报 0（A.1.2 的"未实现地址"）；Type-0 页读由
             * dll_handle_operate_type0() 直接不应答。 */
            od_out[0] = dll_dp_page1_available(&ctx->shared)
                            ? iolink_isdu_direct_param_page1_octet(
                                  &ctx->shared.isdu, (uint8_t) (mc & IOLINK_MC_ADDR_MASK))
                            : 0U;
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
                od_out[i] = DLL_EVENTS_MEMORY_READ(&ctx->shared.events, (uint8_t) (addr + i));
            }
        }
        else if (od_len > 0U) {
            DLL_EVENTS_MEMORY_WRITE(&ctx->shared.events, addr, od_in[0]);
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
            iolink_isdu_od_read(&ctx->shared.isdu, flowctrl, od_out, od_len);
        }
        return true;
    }

    (void) od_in;
    (void) ctx;
    return false;
}

/** @brief 通过 ISDU 处理 2 字节 Type-0 请求并发送 OD 响应。 */
void dll_handle_operate_type0(iolink_dll_main_t* ctx, uint8_t mc, uint8_t cks)
{
    (void) cks;
    const bool type0_write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    const bool page_channel = ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE);
    if (!type0_write && page_channel && !dll_dp_page1_available(&ctx->shared)) {
        /* DPP1 读需要绑定的设备身份：不答（主站超时），不发全零页。
         * 没有发出应答，因此也不刷新 last_response_us（t_A/T_REN 统计
         * 只记录真实应答）。 */
        return;
    }

    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &cks, 1U, &od_resp);

    uint8_t resp[2];
    uint8_t ck_flags = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        ck_flags |= 0x80U;
    }
    ck_flags |= dll_pd_status_flags(ctx);

    /* Figure A.5：Type-0 WRITE 在每个通信通道上都只由 CKS
       应答；每个 Type-0 READ 由 OD 后跟 CKS
       应答。 */
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
    ctx->last_response_us = dll_time_us(&ctx->shared);
}

/** @brief 处理 3 字节 Type-0 OD WRITE（MC、CKT、OD），仅用 CKS 应答（Figure A.5）。 */
void dll_handle_type0_od_write(iolink_dll_main_t* ctx, uint8_t mc, uint8_t od_in)
{
    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &od_in, 1U, &od_resp);

    /* 计划未带 CONSUME 的 Type-0 写（普通 DPP 页写/诊断写）由 ISR 先回
     * CKS，帧仍进邮箱让主泵应用写入；此时主泵不得再发第二个应答。 */
    if (ctx->frame_replied_by_isr != 0U) {
        ctx->frame_replied_by_isr = 0U;
        ctx->last_response_us = dll_time_us(&ctx->shared);
        return;
    }

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(&ctx->shared);
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
static bool dll_handle_eager_isdu_read(iolink_dll_main_t* ctx, bool write)
{
    if (!ctx->frame_replied_by_isr ||
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) != IOLINK_MC_CHANNEL_ISDU) || write) {
        return false;
    }
    const uint8_t flowctrl = (uint8_t) (ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK);
    if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2) ||
        (flowctrl == IOLINK_FLOWCTRL_ABORT)) {
        uint8_t dummy = 0U;
        iolink_isdu_od_read(&ctx->shared.isdu, flowctrl, &dummy, 0U); /* 仅重置 */
        dll_isdu_sync_snapshot(ctx);
    }
    ctx->frame_replied_by_isr = 0U;
    ctx->last_response_us = dll_time_us(&ctx->shared);
    return true;
}

/** @brief 分发请求 OD；当通道拒绝时把应答 OD 清零。 */
static void dll_dispatch_od_reply(iolink_dll_main_t* ctx, bool write, uint16_t od_offset,
                                  uint8_t* od_out)
{
    const uint8_t od_w = ctx->shared.c_od_width; /* 来自 opt-in 补丁的 1/2/8/32 */
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
static void dll_send_od_reply(iolink_dll_main_t* ctx, bool write, const uint8_t* od_out)
{
    const uint8_t resp_od = write ? 0U : ctx->shared.c_od_width;

    /* Figure A.9/A.10/A.13/A.14：对 READ M-sequence 的应答为
       [OD 字节][PD-in 字节] CKS；对 WRITE M-sequence 的应答不携带
       OD，为 [PD-in 字节] CKS。CKS 携带 Event 标志（位 7）、
       PD 有效标志（位 6，1 = 无效）以及 6 位报文校验和
       （位 0-5）；没有前置状态字节（A.1.5）。 */
    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + IOLINK_OD_MAX_SIZE + 1];
    uint16_t pos = 0U;
    /* OD 字节与 PD-in 字节、以及 CKS 的有效位都来自同一次发布字加载。 */
    const IOLINK_ATOMIC_UINT_T pd = dll_pd_pub_load(&ctx->shared);
    if (!write && (resp_od > 0U)) {
        (void) memcpy(&resp[pos], od_out, resp_od);
        pos += resp_od;
    }
    if (ctx->shared.c_pd_in_len > 0U) {
        const uint8_t active = IOLINK_PD_PUB_ACTIVE(pd);
        (void) memcpy(&resp[pos], &ctx->shared.pd_in[active][ctx->shared.c_od_width],
                      ctx->shared.c_pd_in_len);
        pos += ctx->shared.c_pd_in_len;
    }

    uint8_t cks = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        cks |= 0x80U;
    }
    cks |= dll_pd_status_flags_at(&ctx->shared, pd);
    resp[pos] = cks;
    resp[pos] = (uint8_t) (resp[pos] | iolink_checksum6(resp, pos + 1U));
    pos++;

    if (dll_phy_has_tx(&ctx->shared)) {
        dll_send(ctx, resp, pos);
        ctx->shared.fallback_count = 0U;
        const uint64_t end_us = dll_time_us(&ctx->shared);
        const uint32_t limit = dll_get_t_ren_limit_us();
        const uint64_t ren_deadline = dll_timer_deadline(ctx, IOLINK_TIMER_T_REN);

        if ((ren_deadline != 0U) && ctx->enforce_timing && (limit > 0U) &&
            dll_time_passed(end_us, ren_deadline)) {
            ctx->timing_errors++;
            ctx->t_ren_violations++;
            DLL_DIAG_PULSE(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_TIMING);
        }
        dll_timer_cancel(ctx, IOLINK_TIMER_T_REN);
    }
    ctx->last_response_us = dll_time_us(&ctx->shared);
}

void dll_handle_operate_type1_2(iolink_dll_main_t* ctx)
{
    /* Type-1/2 主站报文：MC | CKT | PD | OD；校验和在 CKT 中。 */
    const uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    const uint16_t od_offset = (uint16_t) (pd_offset + ctx->shared.c_pd_out_len_current);

    if (ctx->shared.c_pd_out_len_current > 0U) {
        (void) memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->shared.c_pd_out_len_current);
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
        ctx->shared.fallback_count = 0U;
        ctx->last_response_us = dll_time_us(&ctx->shared);
        ctx->frame_replied_by_isr = 0U;
        return;
    }
    if (ctx->frame_replied_by_isr != 0U) {
        /* ISR 已确认该 ISDU 写；不要再发送第二个
         * 应答。ISDU 读在 dll_dispatch_od() 之前处理。 */
        ctx->frame_replied_by_isr = 0U;
        ctx->last_response_us = dll_time_us(&ctx->shared);
        return;
    }

    dll_send_od_reply(ctx, write, od_out);
}

/* -------------------------------------------------------------------------
 * 诊断轮询
 * ------------------------------------------------------------------------- */

/**
 * @brief 轮询 PHY 故障位掩码并把每个位交给诊断注册表。
 *
 * 注册表的 PHY 来源是电平源：`iolink_diag_set_level()` 用“当前电平 vs 条件
 * 锁存”产生上升/下降沿，稳态不重复计数。Table D.1 映射（含 EventType 与首选
 * DeviceStatus）只在 diag.c 的 ROM 表出现一次：
 *  - UNDERVOLTAGE   -> 0x5111 供电电压欠压（Warning）
 *  - OVERVOLTAGE    -> 0x5110 供电电压过压（Warning）
 *  - OVERTEMP       -> 0x4000 温度过载（Error）
 *  - SHORT_CIRCUIT  -> 0x7710 短路（Error）
 *
 * 寄存器复位（STARTUP、0x80、0x82）清空条件表后，下一轮采样对仍为高电平的
 * 来源重新产生上升沿，因此现存故障会在新会话中重新上报（B.2.21.2）。
 */
static void dll_poll_phy_faults(iolink_dll_main_t* ctx)
{
#if IOLINK_FEATURE_EVENTS
    const uint8_t faults = ctx->shared.c_phy->get_faults(ctx->shared.c_phy->self);

    if (DLL_DIAG_SET_LEVEL(&ctx->shared.events, IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE,
                           (faults & IOLINK_PHY_FAULT_UNDERVOLTAGE) != 0U)) {
        ctx->voltage_faults++;
    }
    if (DLL_DIAG_SET_LEVEL(&ctx->shared.events, IOLINK_DIAG_SRC_PHY_OVERVOLTAGE,
                           (faults & IOLINK_PHY_FAULT_OVERVOLTAGE) != 0U)) {
        ctx->voltage_faults++;
    }
    if (DLL_DIAG_SET_LEVEL(&ctx->shared.events, IOLINK_DIAG_SRC_PHY_OVERTEMP,
                           (faults & IOLINK_PHY_FAULT_OVERTEMP) != 0U)) {
        ctx->overtemp_faults++;
    }
    if (DLL_DIAG_SET_LEVEL(&ctx->shared.events, IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT,
                           (faults & IOLINK_PHY_FAULT_SHORT_CIRCUIT) != 0U)) {
        ctx->short_circuits++;
    }
#else
    /* 事件引擎被裁剪：诊断来源恒不可用，物理故障不产生事件/统计。
     * 这与 IOLINK_FEATURE_EVENTS=0 的降级语义一致（0x0025 返回索引不可用）。 */
    (void) ctx;
#endif
}

/** @brief 轮询诊断：先消费 ISR 脉冲/保持窗口，再采样 PHY 电平源。 */
void dll_poll_diagnostics(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* ISR 只发布 one-shot；这里在主上下文完成条件锁存与事件发布。 */
    DLL_DIAG_SERVICE(&ctx->shared.events, dll_time_ms(&ctx->shared));

    if (ctx->shared.c_phy == NULL) {
        return;
    }

    /* 故障位掩码是唯一的物理诊断通道。 */
    if (ctx->shared.c_phy->get_faults != NULL) {
        dll_poll_phy_faults(ctx);
    }
}

/**
 * @brief 处理不再接受帧的两个状态。
 *
 * @return 当帧被丢弃（调用方不得运行公共
 *         尾部清理）时返回 true。
 */
bool dll_frame_drop_if_stopped(iolink_dll_main_t* ctx)
{
    if (ctx->back_to_box_pending && (ctx->back_to_box_cycles >= 3U)) {
        /* 10.7.5：在至少 3 个 MasterCycle 之后，设备停止并禁用
           通信和输出驱动器，直到下次上电。 */
        ctx->back_to_box_pending = false;
        dll_set_state(ctx, IOLINK_DLL_STATE_COMM_DISABLED);
        if ((ctx->shared.c_phy != NULL) && (ctx->shared.c_phy->set_mode != NULL)) {
            ctx->shared.c_phy->set_mode(ctx->shared.c_phy->self, IOLINK_PHY_MODE_INACTIVE);
        }
        ctx->frame_index = 0U;
        return true;
    }
    if (ctx->shared.state == IOLINK_DLL_STATE_COMM_DISABLED) {
        /* 通信已停止，直到下次上电（10.7.5）。 */
        ctx->frame_index = 0U;
        return true;
    }
    return false;
}

/** @brief T_CYCLE 监控：统计过短的 MasterCycle 并重新开始。
 *
 * @param ctx      DLL 上下文
 * @param frame_us 该 MasterCycle 的请求结束时间（微秒绝对时间）。
 *
 * ISR 立即应答（CONSUME）的帧由主泵用交接来的帧时间戳调用本函数，
 * 因此 MinCycleTime/T_REN 的参考点始终是帧结束，而不是主泵轮询时刻。
 */
void dll_check_cycle_time_at(iolink_dll_main_t* ctx, uint64_t frame_us)
{
    const uint64_t cycle_deadline = dll_timer_deadline(ctx, IOLINK_TIMER_T_CYCLE);
    if (ctx->enforce_timing && (ctx->min_cycle_time_us > 0U) && (cycle_deadline != 0U) &&
        !dll_time_reached(frame_us, cycle_deadline)) {
        ctx->timing_errors++;
        ctx->t_cycle_violations++;
        DLL_DIAG_PULSE(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_TIMING);
    }
    /* 与原实现一致：时间戳为 0（未绑定时钟）的第一拍没有参考点，不装载。 */
    if (ctx->enforce_timing && (ctx->min_cycle_time_us > 0U) && (frame_us != 0U)) {
        dll_timer_arm(ctx, IOLINK_TIMER_T_CYCLE, frame_us + ctx->min_cycle_time_us);
        const uint32_t ren_limit = dll_get_t_ren_limit_us();
        if (ren_limit > 0U) {
            dll_timer_arm(ctx, IOLINK_TIMER_T_REN, frame_us + ren_limit);
        }
        else {
            dll_timer_cancel(ctx, IOLINK_TIMER_T_REN);
        }
    }
    else {
        dll_timer_cancel(ctx, IOLINK_TIMER_T_CYCLE);
        dll_timer_cancel(ctx, IOLINK_TIMER_T_REN);
    }
}

/** @brief 主上下文路径的 T_CYCLE 检查：参考点即当前轮询时刻。 */
void dll_check_cycle_time(iolink_dll_main_t* ctx)
{
    dll_check_cycle_time_at(ctx, dll_time_us(&ctx->shared));
}
