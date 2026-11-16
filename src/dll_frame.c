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
bool dll_type_allowed(const iolink_dll_main_t* ctx, const uint8_t* frame)
{
    const uint8_t rx_type = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    const uint8_t ch = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    uint8_t exp_type = IOLINK_MSEQ_TYPE_0;

    if ((ctx->shared.state == IOLINK_DLL_STATE_OPERATE) ||
        ((ctx->shared.state == IOLINK_DLL_STATE_ESTAB_COM) &&
         (frame[0] != IOLINK_MC_TRANSITION_COMMAND))) {
        if ((ch == IOLINK_MC_CHANNEL_PAGE) && (rx_type == IOLINK_MSEQ_TYPE_0)) {
            return true;
        }
        exp_type = dll_mseq_type_bits(ctx->shared.m_seq_type);
    }
    return rx_type == exp_type;
}

/**
 * @brief 返回设备在当前状态下期望的 CKT 类型位。
 *
 * 在 STARTUP 和 PREOPERATE 中每条报文都是 Type 0（Table A.3，CKT 位 00）；
 * 在 OPERATE 中期望配置的 M-sequence 类型。
 */
uint8_t dll_expected_type_bits(const iolink_dll_main_t* ctx)
{
    if ((ctx->shared.state == IOLINK_DLL_STATE_OPERATE) ||
        (ctx->shared.state == IOLINK_DLL_STATE_ESTAB_COM)) {
        return dll_mseq_type_bits(ctx->shared.m_seq_type);
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
void dll_send(iolink_dll_main_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (ctx->shared.phy == NULL) || (data == NULL) || (len == 0U)) {
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
    /* 应答开始边沿：关闭 t_A 软件镜像（PHY 的 response_timer_stop 另行停止）。 */
    dll_timer_cancel(ctx, IOLINK_TIMER_T_A);
    /* 仅主上下文槽位：ISR 有自己的槽位，因此该计数器只有
     * 单个写者，无需读-改-写原子操作。 */
    const uint8_t slot = ctx->isr_tx_next;
    ctx->isr_tx_next = (uint8_t) (ctx->isr_tx_next ^ 1U);
    (void) memcpy(ctx->shared.isr_tx_payload[slot], data, len);
    ctx->shared.isr_tx_pending_slot = slot;
    ctx->shared.isr_tx_len = (uint8_t) len;
    ctx->shared.isr_tx_has_cks = 0U;
    IOLINK_FENCE();                  /* 暂存字节先于发布 */
    ctx->shared.isr_tx_pending = 1U; /* 最后发布 */

    if (ctx->shared.phy->request_service != NULL) {
        /* 在发布之后、且在任何关中断窗口之外通知 PHY：
         * 该钩子属于端口代码（可能进行 SPI 访问或获取锁）。
         * 其间自然产生的数据中断只会排空暂存
         * 应答，随后额外的服务请求便无事可做。 */
        ctx->shared.phy->request_service(ctx->shared.phy->self);
        return;
    }

    if (!iolink_caps_has(ctx->shared.caps, IOLINK_CAP_REQUIRE_ISR_TX)) {
        /* 适用于无 service 钩子的 PHY 的旧回退路径（流式/主机测试）。
         * 它从此上下文驱动 TX 钩子，因此数据中断不能与
         * 传输交错。 */
        IOLINK_CRITICAL_ENTER();
        ctx->shared.isr_tx_pending = 0U;
        dll_isr_start_staged_tx(ctx, slot, (uint8_t) len);
        IOLINK_CRITICAL_EXIT();
        return;
    }
    /* 仅有 ISR、且没有 request_service() 的 PHY：应答无法发送。
     * 对其计数，使配置错误可被观测而非静默。 */
    ctx->shared.isr_tx_pending = 0U;
    ctx->shared.isr_tx_dropped++;
}

/* -------------------------------------------------------------------------
 * 时序、主站命令与 OPERATE 处理
 * ------------------------------------------------------------------------- */

/** @brief 返回响应时间（t_REN）上限，单位 us（C6/Table 10：500 us）。 */
uint32_t dll_get_t_ren_limit_us(const iolink_dll_main_t* ctx)
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
uint32_t dll_get_baud_hz(const iolink_dll_main_t* ctx)
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

/** @brief 返回当前波特率下的一个位时间（us）。 */
uint32_t dll_get_t_bit_us(const iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return 26U;
    }
    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            return 208U;
        case IOLINK_BAUDRATE_COM2:
            return 26U;
        case IOLINK_BAUDRATE_COM3:
            return 4U;
        default:
            return 26U;
    }
}

/** @brief 返回当前波特率下的字节间超时（16 个位时间），单位 us。 */
uint32_t dll_get_t_byte_limit_us(const iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return dll_get_t_bit_us(ctx) * 16U;
}

/** @brief 当掉电/PD 保护窗口（t_PD）仍有效时返回 true。 */
bool dll_t_pd_active(const iolink_dll_main_t* ctx)
{
    return dll_timer_armed(ctx, IOLINK_TIMER_T_PD) &&
           !dll_timer_due(ctx, IOLINK_TIMER_T_PD, dll_time_us(ctx));
}

/** @brief 排空 PHY 中所有待处理的 RX 字节；若读到任何字节则返回 true。 */
bool dll_drain_rx(iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.phy == NULL)) {
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
static void dll_protocol_error_to_startup(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->shared.fallback_count = 0U;
    ctx->total_retries++;
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
        uint32_t t_fbd_ms = 500U;
        if (ctx->min_cycle_time_us > 0U) {
            t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
            if (t_fbd_ms > 500U) {
                t_fbd_ms = 500U;
            }
        }
        IOLINK_LOG_WARN(&ctx->shared.log, "fallback command: SIO in %lu ms",
                        (unsigned long) t_fbd_ms);
        /* 绝对截止时间仍按毫秒基准换算，与原比较（now_ms >= deadline_ms）逐拍一致。 */
        dll_timer_arm(ctx, IOLINK_TIMER_T_FBD,
                      (uint64_t) (dll_time_us(ctx) / 1000U + t_fbd_ms) * 1000U);
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
bool dll_apply_master_command(iolink_dll_main_t* ctx, uint8_t cmd)
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
            if (ctx->shared.state == IOLINK_DLL_STATE_PREOPERATE) {
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
uint8_t dll_pd_status_flags(const iolink_dll_main_t* ctx)
{
    if ((ctx->pd_in_len_current == 0U) && (ctx->shared.pd_out_len_current > 0U)) {
        return 0U;
    }
    return ctx->pd_valid ? 0U : 0x40U;
}

void dll_handle_master_command(iolink_dll_main_t* ctx, uint8_t cmd)
{
    if (cmd == IOLINK_CMD_FALLBACK) {
        dll_handle_fallback_command(ctx);
        return;
    }

    (void) dll_apply_master_command(ctx, cmd);

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
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
    if (ctx->shared.isdu.device_info == NULL) {
        /* 没有设备身份就没有 DPP1：保持沉默，不发全零页。 */
        return;
    }
    uint8_t resp[2];
    resp[0] = iolink_isdu_direct_param_page1_octet(&ctx->shared.isdu,
                                                   (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
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
            (void) dll_apply_master_command(ctx, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x01U)) {
            ctx->master_cycle_time = od_in[0];
            /* DPP1 0x01 是页读应答的输入：ISR 的页读缓存必须失效。 */
            iolink_isdu_dp_page1_invalidate(&ctx->shared.isdu);
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x0FU)) {
            /* 7.3.5.3：通过 Direct Parameter 页面 1 的 SystemCommand（无响应）。 */
            iolink_isdu_execute_system_command(&ctx->shared.isdu, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) != 0U) && (od_len > 0U)) {
            od_out[0] = iolink_isdu_direct_param_page1_octet(&ctx->shared.isdu,
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
    if (!type0_write && page_channel && (ctx->shared.isdu.device_info == NULL)) {
        /* DPP1 读需要绑定的设备身份：不答（主站超时），不发全零页。 */
        ctx->last_response_us = dll_time_us(ctx);
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
    ctx->last_response_us = dll_time_us(ctx);
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
        ctx->last_response_us = dll_time_us(ctx);
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
    ctx->last_response_us = dll_time_us(ctx);
    return true;
}

/** @brief 分发请求 OD；当通道拒绝时把应答 OD 清零。 */
static void dll_dispatch_od_reply(iolink_dll_main_t* ctx, bool write, uint16_t od_offset,
                                  uint8_t* od_out)
{
    const uint8_t od_w = ctx->shared.od_width; /* 来自 opt-in 补丁的 1/2/8/32 */
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
    const uint8_t resp_od = write ? 0U : ctx->shared.od_width;

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
    if (ctx->shared.pd_in_len > 0U) {
        (void) memcpy(&resp[pos],
                      &ctx->shared.pd_in[ctx->shared.pd_in_active][ctx->shared.od_width],
                      ctx->shared.pd_in_len);
        pos += ctx->shared.pd_in_len;
    }

    uint8_t cks = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->shared.events)) {
        cks |= 0x80U;
    }
    cks |= dll_pd_status_flags(ctx);
    resp[pos] = cks;
    resp[pos] = (uint8_t) (resp[pos] | iolink_checksum6(resp, pos + 1U));
    pos++;

    if (dll_phy_has_tx(&ctx->shared)) {
        dll_send(ctx, resp, pos);
        ctx->shared.fallback_count = 0U;
        const uint64_t end_us = dll_time_us(ctx);
        const uint32_t end_tx_us = (uint32_t) end_us;
        const uint32_t limit = dll_get_t_ren_limit_us(ctx);
        const uint64_t ren_deadline = dll_timer_deadline(ctx, IOLINK_TIMER_T_REN);
        const uint32_t start_us = (ren_deadline != 0U) ? (uint32_t) (ren_deadline - limit) : 0U;
        ctx->response_time_us = end_tx_us - start_us;

        if ((ren_deadline != 0U) && ctx->enforce_timing && (limit > 0U) &&
            dll_time_passed(end_us, ren_deadline)) {
            ctx->timing_errors++;
            ctx->t_ren_violations++;
            DLL_DIAG_PULSE(&ctx->shared.events, IOLINK_DIAG_SRC_COMM_TIMING);
        }
        dll_timer_cancel(ctx, IOLINK_TIMER_T_REN);
    }
    ctx->last_response_us = dll_time_us(ctx);
}

void dll_handle_operate_type1_2(iolink_dll_main_t* ctx)
{
    /* Type-1/2 主站报文：MC | CKT | PD | OD；校验和在 CKT 中。 */
    const uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    const uint16_t od_offset = (uint16_t) (pd_offset + ctx->shared.pd_out_len_current);

    if (ctx->shared.pd_out_len_current > 0U) {
        (void) memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->shared.pd_out_len_current);
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
        ctx->shared.fallback_count = 0U;
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
    const uint8_t faults = ctx->shared.phy->get_faults(ctx->shared.phy->self);

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
    DLL_DIAG_SERVICE(&ctx->shared.events, dll_time_ms(ctx));

    if (ctx->shared.phy == NULL) {
        return;
    }

    /* 故障位掩码是唯一的物理诊断通道。 */
    if (ctx->shared.phy->get_faults != NULL) {
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
        if ((ctx->shared.phy != NULL) && (ctx->shared.phy->set_mode != NULL)) {
            ctx->shared.phy->set_mode(ctx->shared.phy->self, IOLINK_PHY_MODE_INACTIVE);
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
        const uint32_t ren_limit = dll_get_t_ren_limit_us(ctx);
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
    dll_check_cycle_time_at(ctx, dll_time_us(ctx));
}
