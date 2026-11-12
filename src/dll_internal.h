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
#include "iolinki/dll.h"

/**
 * @file dll_internal.h
 * @brief 内部 DLL 定义与结构
 */

#include "iolinki/protocol.h"

/**
 * @name 内部时钟辅助函数
 *
 * 时间戳来自设备初始化时绑定的应用时钟
 * （iolink_device_config_t::time_us）。缺少时钟时返回 0：设备层会拒绝它，
 * 但裸 DLL 上下文在绑定之前仍可被检查。
 * @{
 */
static inline uint64_t dll_time_us(const iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->time_us == NULL)) {
        return 0U;
    }
    return ctx->time_us(ctx->time_user);
}

static inline uint32_t dll_time_ms(const iolink_dll_ctx_t* ctx)
{
    return (uint32_t) (dll_time_us(ctx) / 1000U);
}
/** @} */

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
#define DLL_EVENT_TRIGGER(ctx, code, type) iolink_event_trigger((ctx), (code), (type))
#define DLL_EVENT_CLEAR(ctx, code) iolink_event_clear((ctx), (code))
#define DLL_EVENTS_FLAG(ctx) iolink_events_flag(ctx)
#define DLL_EVENTS_MEMORY_READ(ctx, addr) iolink_events_memory_read((ctx), (addr))
#define DLL_EVENTS_MEMORY_WRITE(ctx, addr, value) iolink_events_memory_write((ctx), (addr), (value))
#else
#define DLL_EVENTS_INIT(ctx) ((void) 0)
#define DLL_EVENT_TRIGGER(ctx, code, type) ((void) 0)
#define DLL_EVENT_CLEAR(ctx, code) ((void) 0)
#define DLL_EVENTS_FLAG(ctx) false
#define DLL_EVENTS_MEMORY_READ(ctx, addr) 0U
#define DLL_EVENTS_MEMORY_WRITE(ctx, addr, value) ((void) 0)
#endif

/**
 * @name 跨 TU 的 DLL 声明
 *
 * dll.c 提供 ISR 使用的状态/时序辅助函数，dll_isr.c
 * 提供 dll_send() 使用的暂存 TX 入口。这些符号是库私有的
 * （不安装），并有意保留 dll_ 前缀；它们
 * 不属于公共 iolink_ API。关于主/ISR
 * 上下文约定，参见 docs/CODE_FLOW.md。
 * @{
 */
void dll_set_state(iolink_dll_ctx_t* ctx, iolink_dll_state_t new_state);
bool dll_apply_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd);
bool dll_type_allowed(const iolink_dll_ctx_t* ctx, const uint8_t* frame);
uint8_t dll_pd_status_flags(const iolink_dll_ctx_t* ctx);
void dll_mark_master_activity(iolink_dll_ctx_t* ctx);
void dll_isr_start_staged_tx(iolink_dll_ctx_t* ctx, uint8_t slot, uint8_t len);
/** @} */

#endif  // IOLINK_DLL_INTERNAL_H
