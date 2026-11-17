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
    if (ctx == NULL) {
        return;
    }
    ctx->back_to_box_pending = true;
    ctx->back_to_box_cycles = 0U;
}

/* -------------------------------------------------------------------------
 * 公共配置与查询 API
 * ------------------------------------------------------------------------- */

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_main_t* ctx)
{
    return (ctx != NULL) ? ctx->phy_mode : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_main_t* ctx)
{
    return (ctx != NULL) ? ctx->baudrate : IOLINK_BAUDRATE_COM2;
}

/** @brief 根据 M-sequence 时序计算设备 MinCycleTime（Table B.3）。
 *
 * 使用 ST ministry 公式：mseq_us = (reply_octets*14 + request_octets*12
 * + 6) * 1e6 / baud。取 PD 写与 PD/ISDU 读的最坏情况，
 * 向上取整到 0.1 ms 单位，并钳制到应用下限。 */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_main_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    const uint32_t baud = dll_get_baud_hz(ctx);
    const uint32_t pdi = ctx->pd_in_len_current;
    const uint32_t pdo = ctx->shared.pd_out_len_current;
    const uint32_t od = ctx->shared.od_width;
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

int iolink_dll_set_baudrate(iolink_dll_main_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) return -1;
    const bool changing = (ctx->baudrate != baudrate);
    if (changing) {
        IOLINK_LOG_INFO(&ctx->shared.log, "baudrate %u -> %u", (unsigned) ctx->baudrate,
                        (unsigned) baudrate);
    }
    ctx->baudrate = baudrate;
    if (ctx->shared.phy->set_baudrate != NULL)
        ctx->shared.phy->set_baudrate(ctx->shared.phy->self, baudrate);
    if (changing) {
        /* UART 重配时 PHY 会中止进行中的应答；
         * 丢弃匹配的 ISR TX 状态，使其无法在下次请求时
         * 恢复陈旧的帧。 */
        ctx->shared.isr_tx_pending = 0U;
        ctx->shared.isr_tx_active = 0U;
        ctx->shared.isr_tx_has_cks = 0U;
        ctx->shared.isr_tx_idx = 0U;
        ctx->shared.pd_in_captured = 0xFFU;
    }
    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);
    ctx->min_cycle_time_us = (uint32_t) iolink_dll_compute_min_cycle_time(ctx) * 100U;
    /* 波特率参与 MinCycleTime（DPP1 0x02）：页读缓存必须失效。 */
    iolink_isdu_dp_page1_invalidate(&ctx->shared.isdu);
    return 0;
}

int iolink_dll_set_operating_baudrate(iolink_dll_main_t* ctx, iolink_baudrate_t baudrate)
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

int iolink_dll_set_pd_length(iolink_dll_main_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if ((ctx == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
        (pd_out_len > IOLINK_PD_OUT_MAX_SIZE))
        return -1;
    /* 数据 ISR 读取 pd_out_len_current 来成帧请求；
     * 原子发布 PD-in/PD-out 对，使其无法观察到半更新。 */
    IOLINK_CRITICAL_ENTER();
    ctx->pd_in_len_current = pd_in_len;
    ctx->shared.pd_out_len_current = pd_out_len;
    IOLINK_CRITICAL_EXIT();
    /* DPP1 0x05/0x06（以及由它们推导的 MinCycleTime）是页读应答的输入。 */
    iolink_isdu_dp_page1_invalidate(&ctx->shared.isdu);
    return 0;
}

void iolink_dll_get_pd_length(const iolink_dll_main_t* ctx, uint8_t* IOLINK_NULLABLE pd_in_len,
                              uint8_t* IOLINK_NULLABLE pd_out_len)
{
    if (ctx == NULL) return;
    if (pd_in_len) *pd_in_len = ctx->pd_in_len_current;
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
     * SDCI 会话（SIO 中 int_active() 为 false，因此没有东西会完成它）。 */
    ctx->shared.isr_tx_pending = 0U;
    ctx->shared.isr_tx_active = 0U;
    ctx->shared.isr_tx_has_cks = 0U;
    ctx->shared.isr_tx_idx = 0U;
    ctx->shared.pd_in_captured = 0xFFU;
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
    out_stats->isr_od_overruns = ctx->shared.isr_od_overruns;
    out_stats->isr_plan_misses = ctx->shared.isr_plan_misses;
    out_stats->isr_mailbox_drops = ctx->shared.isr_mailbox_drops;
    out_stats->isr_frames_consumed = ctx->shared.isr_frames_consumed;
    out_stats->isr_frames_handoff = ctx->shared.isr_frames_handoff;
    out_stats->t_pd_discards = ctx->t_pd_discards;
    out_stats->reply_plan_overflows = ctx->reply_plan_overflows;
}

void iolink_dll_reset_error_counters(iolink_dll_main_t* ctx)
{
    if (ctx == NULL) return;

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
    if (ctx == NULL) return;

    /* B.2.20/Table 101：ErrorCount 统计上电或复位以来的错误，
       诊断/状态由重置 SystemCommand 清除。事件
       引擎拥有 DeviceStatus 和 DetailedDeviceStatus；reset() 保留
       绑定的时钟。 */
#if IOLINK_FEATURE_EVENTS
    iolink_events_reset(&ctx->shared.events);
#endif
    iolink_dll_reset_error_counters(ctx);
}

void iolink_dll_set_timing_enforcement(iolink_dll_main_t* ctx, bool enable)
{
    if (ctx != NULL) ctx->enforce_timing = enable;
}

void iolink_dll_set_t_ren_limit_us(iolink_dll_main_t* ctx, uint32_t limit_us)
{
    if (ctx == NULL) return;
    ctx->t_ren_limit_us = limit_us;
    ctx->t_ren_override = (limit_us != 0U);
}

uint8_t dll_req_len_for(const iolink_dll_main_t* ctx, uint8_t mc, uint8_t ckt)
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
    if ((ctx->shared.state == IOLINK_DLL_STATE_ESTAB_COM) && (mc == IOLINK_MC_TRANSITION_COMMAND)) {
        return IOLINK_M_SEQ_HEADER_LEN;
    }
    const uint8_t od_req = ((mc & IOLINK_MC_RW_MASK) == 0U) ? ctx->shared.od_width : 0U;
    return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->shared.pd_out_len_current + od_req);
}

uint8_t iolink_dll_expected_req_len(const iolink_dll_main_t* ctx, uint8_t mc)
{
    if (ctx == NULL) {
        return 0U;
    }
    /* 公共查询问的是“若主站按当前配置发送，这条请求有多长”，
       所以 CKT 取期望的类型位，而不是某个真实帧自报的类型。 */
    return dll_req_len_for(ctx, mc, dll_expected_type_bits(ctx));
}
