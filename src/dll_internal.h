/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_INTERNAL_H
#define IOLINK_DLL_INTERNAL_H

#include <stdint.h>
#include "iolinki/internal/dll_main.h"

/**
 * @file dll_internal.h
 * @brief 内部 DLL 定义与结构
 */

#include "iolinki/protocol.h"
#include "iolinki/crc.h"

/**
 * @brief DPP1 是否可读：绑定了设备身份，且该身份上下文确实已初始化。
 *
 * 只判指针非 NULL 不够——调用方可能绑定一个从未 `iolink_device_info_ctx_init()`
 * 的上下文，那样页映像仍是全零。
 */
static inline bool dll_dp_page1_available(const iolink_dll_main_t* ctx)
{
    const iolink_device_info_ctx_t* info = ctx->shared.isdu.device_info;
    return (info != NULL) && (info->configured != NULL);
}

/**
 * @name 内部时钟辅助函数
 *
 * 时间戳来自设备初始化时绑定的应用时钟
 * （iolink_device_app_t::time_us）。缺少时钟时返回 0：设备层会拒绝它，
 * 但裸 DLL 上下文在绑定之前仍可被检查。
 * @{
 */
static inline uint64_t dll_time_us(const iolink_dll_main_t* ctx)
{
    if ((ctx == NULL) || (ctx->shared.clock.time_us == NULL)) {
        return 0U;
    }
    return ctx->shared.clock.time_us(ctx->shared.clock.self);
}

static inline uint32_t dll_time_ms(const iolink_dll_main_t* ctx)
{
    return (uint32_t) (dll_time_us(ctx) / 1000U);
}
/** @} */

/**
 * @brief 请求校验和八位组：CKT 的低 6 位（A.1.6）。
 *
 * 主泵与两条 ISR 校验路径共用，避免三份"取 CKS"同时漂移。
 */
static inline uint8_t dll_rx_expected_cks(const uint8_t* frame)
{
    return (uint8_t) (frame[1] & IOLINK_MSEQ_CHECKSUM_MASK);
}

/** @brief 这个 CKT 八位组声明的 M-sequence 类型是否为 Type-0（A.1.6 类型位）。 */
static inline bool dll_ckt_is_type0(uint8_t ckt)
{
    return (ckt & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0;
}

/**
 * @brief MC 是否为 Type-0 的 OD 写（Figure A.5）。
 *
 * Type-0 写只携带一个 OD 八位组（参数/诊断/ISDU 通道 + 写访问），与 OPERATE
 * 配置的 OD 宽度无关。成帧长度推导按这一条判定；ISDU 写暂存先按 CKT 判形状，
 * 只在 Type-0 帧上才取这一个 OD 八位组。
 */
static inline bool dll_mc_is_type0_od_write(uint8_t mc)
{
    const uint8_t channel = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);
    return ((mc & IOLINK_MC_RW_MASK) == 0U) &&
           ((channel == IOLINK_MC_CHANNEL_PAGE) || (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
            (channel == IOLINK_MC_CHANNEL_ISDU));
}

/**
 * @brief 应答 CKS 八位组：@c flags | checksum6(acc ^ flags)。
 *
 * @p acc 是已经含 A.1.6 种子的负载累加器。BYTE 泵在发送过程中累加、
 * FRAME 路径在交出缓冲区前后整帧累加，收尾公式只有这一份。
 */
static inline uint8_t dll_reply_cks(uint8_t acc, uint8_t flags)
{
    return (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
}

#define IOLINK_M_SEQ_TYPE1_LEN(pd_len)                         \
    (4 + (pd_len)) /* MC(8) + CKT(8) + PD(var) + OD(8) + CK(8) \
                    */
#define IOLINK_M_SEQ_TYPE2_LEN(pd_len, od_len) \
    (3 + (pd_len) + (od_len)) /* MC(8) + CKT(8) + PD(var) + OD(var) + CK(8) */

/** @brief 解码后的 2 八位组 Type-0 M-sequence（主站命令 + 校验和）。 */
typedef struct
{
    uint8_t master_command; /**< 主站命令八位组（MC）。 */
    uint8_t ck;             /**< 校验和八位组（CK）。 */
} iolink_m_seq_type0_t;

/** @brief 携带有效性标志的按请求数据（OD）八位组。 */
typedef struct
{
    uint8_t od_data; /**< 按请求数据八位组。 */
    uint8_t valid;   /**< 当 @ref od_data 持有有效值时非零。 */
} iolink_od_req_t;

/* 事件引擎是 DLL/ISDU 的内部实现。当它被编译排除时，
 * 下面的辅助函数折叠为常量，以便不引用任何被禁用的接口。 */
#if IOLINK_FEATURE_EVENTS
#define DLL_EVENTS_INIT(ctx) iolink_events_init(ctx)
#define DLL_EVENTS_FLAG(ctx) iolink_events_flag(ctx)
#define DLL_EVENTS_MEMORY_READ(ctx, addr) iolink_events_memory_read((ctx), (addr))
#define DLL_EVENTS_MEMORY_WRITE(ctx, addr, value) iolink_events_memory_write((ctx), (addr), (value))
#define DLL_DIAG_PULSE(ctx, src) iolink_diag_pulse((ctx), (src))
#define DLL_DIAG_PULSE_ISR(ctx, src) iolink_diag_pulse_isr((ctx), (src))
#define DLL_DIAG_SERVICE(ctx, now_ms) iolink_diag_service((ctx), (now_ms))
#define DLL_DIAG_SET_LEVEL(ctx, src, level) iolink_diag_set_level((ctx), (src), (level))
#else
#define DLL_EVENTS_INIT(ctx) ((void) 0)
#define DLL_EVENTS_FLAG(ctx) false
#define DLL_EVENTS_MEMORY_READ(ctx, addr) 0U
#define DLL_EVENTS_MEMORY_WRITE(ctx, addr, value) ((void) 0)
#define DLL_DIAG_PULSE(ctx, src) ((void) 0)
#define DLL_DIAG_PULSE_ISR(ctx, src) ((void) 0)
#define DLL_DIAG_SERVICE(ctx, now_ms) ((void) 0)
#define DLL_DIAG_SET_LEVEL(ctx, src, level) false
#endif

/**
 * @name 跨 TU 的 DLL 声明
 *
 * 主上下文翻译单元提供状态/时序辅助函数与 dll_send()，
 * dll_isr.c 提供 dll_send() 使用的暂存 TX 入口。这些符号是库私有的
 * （不安装），并有意保留 dll_ 前缀；它们
 * 不属于公共 iolink_ API。关于主/ISR
 * 上下文约定，参见 docs/CODE_FLOW.md。
 * @{
 */
void dll_set_state(iolink_dll_main_t* ctx, iolink_dll_state_t new_state);
bool dll_apply_master_command(iolink_dll_main_t* ctx, uint8_t cmd);
bool dll_type_allowed(const iolink_dll_main_t* ctx, const uint8_t* frame);
uint8_t dll_pd_status_flags(const iolink_dll_main_t* ctx);
void dll_mark_master_activity(iolink_dll_main_t* ctx);
void dll_isr_start_staged_tx(iolink_dll_main_t* ctx, uint8_t slot, uint8_t len);

/**
 * @name 主上下文跨文件入口
 *
 * `src/dll.c` 按职责拆成多个翻译单元后共享的内部符号：帧路径
 * （dll_frame.c）、状态机（dll_sm.c）、应答计划（dll_reply.c）。
 * 均为库私有，只供 `src/` 内部使用；dll_ 前缀表示它们不属于
 * 公共 iolink_ API。
 * @{
 */

/** @brief A.1.6 校验和与 CKT 类型校验的结果。 */
typedef enum
{
    DLL_FRAME_INVALID,    /**< 校验和不匹配：丢弃，保持 DL 模式。 */
    DLL_FRAME_TYPE_ERROR, /**< 校验和有效，M-sequence 类型非法。 */
    DLL_FRAME_VALID       /**< 有效请求；按状态分发。 */
} dll_frame_result_t;

/** @} */

/** @name dll_frame.c */
/** @{ */
void dll_send(iolink_dll_main_t* ctx, const uint8_t* data, size_t len);
uint8_t dll_mseq_type_bits(uint8_t m_seq_type);
uint8_t dll_expected_type_bits(const iolink_dll_main_t* ctx);

/**
 * @brief 请求 M-sequence 的期望总长度（含 MC/CKT 与 CKS 位）。
 *
 * @p ckt 说明这个请求自报的类型：两条 ISR 成帧检查传帧里的 CKT 八位组
 * （“这个帧有多长”），公共查询 @ref iolink_dll_expected_req_len 传当前期望的
 * 类型位（“若主站按配置发送会有多长”）。Type-0 的长度只由 MC 决定（读 2、
 * OD 写 3）；多字节类型按配置的 PD-out / OD 宽度推导，ESTAB_COM 的转换命令
 * 是唯一的 Type-0 交换。
 *
 * 纯只读，ISR 可直接调用；定义在 dll_frame.c（与 dll_expected_type_bits 同处），
 * 两条 RX 路径与公共查询共用这一份实现。
 */
uint8_t dll_req_len_for(const iolink_dll_main_t* ctx, uint8_t mc, uint8_t ckt);

uint32_t dll_get_baud_hz(const iolink_dll_main_t* ctx);
uint32_t dll_get_t_bit_us(const iolink_dll_main_t* ctx);
uint32_t dll_get_t_ren_limit_us(const iolink_dll_main_t* ctx);
uint32_t dll_get_t_byte_limit_us(const iolink_dll_main_t* ctx);
bool dll_t_pd_active(const iolink_dll_main_t* ctx);
bool dll_drain_rx(iolink_dll_main_t* ctx);
void dll_poll_diagnostics(iolink_dll_main_t* ctx);
bool dll_is_fallback_command(const iolink_dll_main_t* ctx);
void dll_handle_fallback_command(iolink_dll_main_t* ctx);
void dll_handle_master_command(iolink_dll_main_t* ctx, uint8_t cmd);
void dll_handle_preoperate(iolink_dll_main_t* ctx, uint8_t mc, uint8_t ck);
void dll_handle_page_channel_read(iolink_dll_main_t* ctx, uint8_t mc);
void dll_handle_operate_type0(iolink_dll_main_t* ctx, uint8_t mc, uint8_t cks);
void dll_handle_type0_od_write(iolink_dll_main_t* ctx, uint8_t mc, uint8_t od_in);
void dll_handle_operate_type1_2(iolink_dll_main_t* ctx);
bool dll_frame_drop_if_stopped(iolink_dll_main_t* ctx);
void dll_check_cycle_time_at(iolink_dll_main_t* ctx, uint64_t frame_us);
void dll_check_cycle_time(iolink_dll_main_t* ctx);
/** @} */

/** @name dll_sm.c */
/** @{ */
dll_frame_result_t dll_frame_validate(iolink_dll_main_t* ctx);
void dll_handle_frame_error(iolink_dll_main_t* ctx, dll_frame_result_t result);
bool dll_handle_valid_frame(iolink_dll_main_t* ctx);
void dll_finish_frame(iolink_dll_main_t* ctx);
void dll_process_complete_frame(iolink_dll_main_t* ctx);
/** @} */

/** @name dll_reply.c */
/** @{ */
void dll_isdu_sync_snapshot(iolink_dll_main_t* ctx);
uint32_t dll_master_cycle_us(const iolink_dll_main_t* ctx);
void dll_mark_master_activity_ts(iolink_dll_main_t* ctx);
/** @} */

/** @brief 清零 ISR 独占状态（由 iolink_dll_init 调用）。 */
bool dll_isr_reset(iolink_dll_isr_t* isr);
/** @} */

#endif  // IOLINK_DLL_INTERNAL_H
