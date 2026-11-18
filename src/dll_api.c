/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/port.h"
#include "iolinki/internal/wiring.h"
#include "dll_internal.h"
#include "iolinki/internal/dll_sm.h"
#include <string.h>

/**
 * @file dll_api.c
 * @brief DLL 公共查询与配置入口。
 *
 * 这些函数只读写上下文中的配置与统计字段，不参与
 * 帧处理；协议行为在 dll_frame.c / dll_sm.c。
 */

void iolink_dll_request_back_to_box(iolink_dll_main_t* ctx)
{
    ctx->back_to_box_pending = true;
    ctx->back_to_box_cycles = 0U;
}

/* -------------------------------------------------------------------------
 * 公共配置与查询 API
 * ------------------------------------------------------------------------- */

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_main_t* ctx)
{
    return ctx->phy_mode;
}

iolink_baudrate_t iolink_dll_get_baudrate(void)
{
    return IOLINK_COM_BAUDRATE;
}

/** @brief 根据 M-sequence 时序计算设备 MinCycleTime（Table B.3）。
 *
 * 使用 A.3.6 的最坏情况：主站请求八位组按 11 + t1(max) 位、设备应答
 * 八位组按 11 + t2(max) 位，再加固定项 t_A(max) - t1(max) - t2(max)，
 * 乘以 1e6/baud 换算为微秒。取 PD 写与 PD/ISDU 读的最坏情况，
 * 向上取整到 0.1 ms 单位，并钳制到 Table B.3 的 0.4 ms 下限与应用下限。 */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_main_t* ctx)
{
    const uint32_t baud = dll_get_baud_hz(ctx);
    const uint32_t pdi = ctx->shared.pd_in_len;
    const uint32_t pdo = ctx->shared.pd_out_len_current;
    const uint32_t od = ctx->shared.od_width;
    /* 写请求：MC+CKT + PD-out + OD；应答：PD-in + CKS。
     * 读请求：MC+CKT + PD-out（无 OD）；应答：OD + PD-in + CKS。
     * 这适用于所有 Annex A 类型：固定类型只是在不携带的
     * 字段中为 0 字节（参见 A.6-A.13）。 */
    const uint32_t t_write = ((pdi + 1U) * IOLINK_MSEQ_DEVICE_OCTET_BITS +
                              (IOLINK_M_SEQ_HEADER_LEN + pdo + od) * IOLINK_MSEQ_MASTER_OCTET_BITS +
                              IOLINK_MSEQ_TIMING_FIXED_BITS) *
                             1000000U / baud;
    const uint32_t t_read = ((od + pdi + 1U) * IOLINK_MSEQ_DEVICE_OCTET_BITS +
                             (IOLINK_M_SEQ_HEADER_LEN + pdo) * IOLINK_MSEQ_MASTER_OCTET_BITS +
                             IOLINK_MSEQ_TIMING_FIXED_BITS) *
                            1000000U / baud;
    const uint32_t t = (t_write > t_read) ? t_write : t_read;
    uint32_t units = (t + (IOLINK_CYCLE_TIME_UNIT_US - 1U)) / IOLINK_CYCLE_TIME_UNIT_US;
    const uint32_t app_units =
        (ctx->shared.min_cycle_time_app_us + (IOLINK_CYCLE_TIME_UNIT_US - 1U)) /
        IOLINK_CYCLE_TIME_UNIT_US;
    if (units < app_units) {
        units = app_units;
    }
    if (units < IOLINK_MIN_CYCLE_TIME_UNITS) {
        units = IOLINK_MIN_CYCLE_TIME_UNITS;
    }
    return (units > 255U) ? 255U : (uint8_t) units;
}

void iolink_dll_get_pd_length(const iolink_dll_main_t* ctx, uint8_t* IOLINK_NULLABLE pd_in_len,
                              uint8_t* IOLINK_NULLABLE pd_out_len)
{
    if (pd_in_len) *pd_in_len = ctx->shared.pd_in_len;
    if (pd_out_len) *pd_out_len = ctx->shared.pd_out_len_current;
}

int iolink_dll_set_sio_mode(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->shared.phy->set_mode != NULL)
        ctx->shared.phy->set_mode(ctx->shared.phy->self, IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;
    /* SIO 转换会中止 PHY 中任何进行中的应答；丢弃
     * 匹配的 ISR TX 状态，使陈旧的 active 索引不会泄漏到下一个
     * SDCI 会话（SIO 中 int_active() 为 false，因此没有东西会完成它）。
     * 这四个字段由数据 ISR 写，因此清理必须与中断互斥：否则中断可在
     * 半帧处重新置上 active/idx，留下一个 tx_done 永不调用的挂起帧。
     * PHY 调用留在锁外（端口代码可能很慢）。丢弃本身必须可观测：
     * 暂存应答或在途帧被清掉时计一次 isr_tx_dropped；该字段由主上下文
     * 自增（SPLIT 模式下 ISR 只写 isr_stat.isr_tx_dropped，读取端求和）。 */
    IOLINK_CRITICAL_ENTER();
    if ((ctx->shared.isr_tx_pending != 0U) || (ctx->shared.isr_tx_active != 0U)) {
        ctx->shared.isr_tx_dropped++;
    }
    ctx->shared.isr_tx_pending = 0U;
    ctx->shared.isr_tx_active = 0U;
    ctx->shared.isr_tx_idx = 0U;
    ctx->shared.pd_in_captured = 0xFFU;
    IOLINK_CRITICAL_EXIT();
    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->shared.phy->set_mode != NULL)
        ctx->shared.phy->set_mode(ctx->shared.phy->self, IOLINK_PHY_MODE_SDCI);
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    return 0;
}

void iolink_dll_get_stats(const iolink_dll_main_t* ctx, iolink_dll_stats_t* out_stats)
{
    out_stats->crc_errors = ctx->crc_errors;
    out_stats->timeout_errors = IOLINK_STAT_TOTAL(ctx, timeout_errors);
    out_stats->idle_timeouts = ctx->idle_timeouts;
    out_stats->framing_errors = IOLINK_STAT_TOTAL(ctx, framing_errors);
    out_stats->timing_errors = ctx->timing_errors;
    out_stats->t_ren_violations = ctx->t_ren_violations;
    out_stats->t_cycle_violations = ctx->t_cycle_violations;
    out_stats->t_byte_violations = ctx->t_byte_violations;
    out_stats->t_pd_violations = ctx->t_pd_violations;
    out_stats->invalid_frames = ctx->invalid_frames;
    out_stats->voltage_faults = ctx->voltage_faults;
    out_stats->overtemp_faults = ctx->overtemp_faults;
    out_stats->short_circuits = ctx->short_circuits;
    out_stats->isr_tx_dropped = IOLINK_STAT_TOTAL(ctx, isr_tx_dropped);
    out_stats->isr_od_overruns = ctx->shared.isdu.req_overruns;
    out_stats->isr_plan_misses = ctx->shared.isr_plan_misses;
    out_stats->isr_eager_rejects = ctx->shared.isr_eager_rejects;
    out_stats->isr_mailbox_drops = ctx->shared.isr_mailbox_drops;
    out_stats->isr_frames_consumed = ctx->shared.isr_frames_consumed;
    out_stats->isr_frames_handoff = ctx->shared.isr_frames_handoff;
    out_stats->t_pd_discards = ctx->t_pd_discards;
    out_stats->reply_plan_overflows = ctx->reply_plan_overflows;
    out_stats->pd_in_skips = ctx->pd_in_skips;
}

void iolink_dll_reset_error_counters(iolink_dll_main_t* ctx)
{
    IOLINK_CRITICAL_ENTER();
    ctx->crc_errors = 0U;
    ctx->shared.timeout_errors = 0U;
    ctx->shared.framing_errors = 0U;
    ctx->timing_errors = 0U;
#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    ctx->shared.isr_stat.timeout_errors = 0U;
    ctx->shared.isr_stat.framing_errors = 0U;
#endif
    IOLINK_CRITICAL_EXIT();
}

void iolink_dll_reset_diagnostics(iolink_dll_main_t* ctx)
{
    /* B.2.20/Table 101：ErrorCount 统计上电或复位以来的错误，
       诊断/状态由重置 SystemCommand 清除。事件
       引擎拥有 DeviceStatus 和 DetailedDeviceStatus；reset() 保留
       绑定的时钟。 */
#if IOLINK_FEATURE_EVENTS
    iolink_events_reset(&ctx->shared.events);
#endif
    iolink_dll_reset_error_counters(ctx);
}

uint8_t iolink_dll_expected_req_len(const iolink_dll_main_t* ctx, uint8_t mc)
{
    /* 本查询问的是“若主站按当前配置发送，这条请求有多长”，
       所以 CKT 取期望的类型位，而不是某个真实帧自报的类型。 */
    return dll_req_len_for(&ctx->shared, mc, dll_expected_type_bits(&ctx->shared));
}
