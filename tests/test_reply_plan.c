/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_reply_plan.c
 * @brief 应答计划：主泵发布、ISR 执行、失效与丢弃语义。
 *
 * 这些用例把"主泵发布计划"和"ISR 执行计划"分开驱动，覆盖：
 *  - 命中计划的即时应答（t_A 路径）；
 *  - 合法帧计划未命中：不应答、计数并丢弃，邮箱不接收该帧；
 *  - 非法帧仍进入邮箱，由主泵完成错误/状态处理且不计计划未命中；
 *  - Type-0 IDLE/ABORT 读的"无服务"应答与传输层复位握手；
 *  - ISDU 写重发风暴按 FlowCTRL 位置去重，不消耗新位置；
 *  - 未接管的 COMPLETE 请求被新 START 覆盖时计数，且不得被主泵当作有效请求执行；
 *  - t_PD 保护窗口内丢弃的邮箱帧可观测；
 *  - ISDU 写暂存后的读视图失效（防"写后立刻读"读到旧值）；
 *  - seqlock 发布窗口：主泵正在覆盖计划表时 ISR 不得执行半更新计划；
 *  - 计划携带的 t_A 预算元数据。
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"

#define RX_MAX 16U
#define TX_MAX 32U

static iolink_dll_ctx_t s_dll;
static uint64_t s_now_us;

static uint8_t s_rx_q[RX_MAX];
static size_t s_rx_n;
static size_t s_rx_i;
static uint8_t s_chk_pending;

static uint8_t s_tx_q[TX_MAX];
static size_t s_tx_n;
static uint8_t s_tx_active;

static uint64_t plan_time_us(void* user)
{
    (void) user;
    return s_now_us;
}

static int plan_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (s_rx_i < s_rx_n) {
        *data = s_rx_q[s_rx_i++];
        return 1;
    }
    if (s_chk_pending != 0U) {
        /* Type-0 帧由收发器的帧结束（CHK）定界。 */
        s_chk_pending = 0U;
        *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}

static int plan_tx_octet(void* user, uint8_t data)
{
    (void) user;
    if (!s_tx_active) {
        s_tx_n = 0U;
        s_tx_active = 1U;
    }
    if (s_tx_n < sizeof(s_tx_q)) {
        s_tx_q[s_tx_n++] = data;
    }
    return 0;
}

static void plan_tx_done(void* user)
{
    (void) user;
    s_tx_active = 0U;
}

static uint8_t plan_int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((s_rx_i < s_rx_n) || (s_chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U))
               ? 1U
               : 0U;
}

static const iolink_phy_api_t s_phy = {
    .user = &s_dll,
    .rx_octet = plan_rx_octet,
    .tx_octet = plan_tx_octet,
    .tx_done = plan_tx_done,
    .int_active = plan_int_active,
};

/** @brief 只收不发的 PHY（用于"计划命中但无法执行"的兜底路径）。 */
static const iolink_phy_api_t s_phy_rx_only = {
    .user = &s_dll,
    .rx_octet = plan_rx_octet,
    .int_active = plan_int_active,
};

/** @brief 补全一条 M-sequence：CKT 承载类型位 + A.1.6 CKS。 */
static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void feed(const uint8_t* frame, size_t len, uint8_t chk)
{
    (void) memcpy(s_rx_q, frame, len);
    s_rx_n = len;
    s_rx_i = 0U;
    s_chk_pending = chk;
}

static void drain_tx(void)
{
    while (s_dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&s_dll.main);
    }
}

/** @brief OPERATE / TYPE_2_V：PD-in 1、PD-out 0、OD 1。 */
static void init_operate(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 1U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    s_dll.main.shared.pd_in[0][1] = 0x5AU; /* PD-in 槽位（OD 头之后） */
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief PREOPERATE / TYPE_0：无 PD、OD 1。 */
static void init_preoperate(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief PREOPERATE / TYPE_0，但 PHY 只有接收能力。 */
static void init_preoperate_rx_only(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy_rx_only);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;
    iolink_dll_publish_reply_plans(&s_dll.main);
}

/** @brief Type-2 过程通道读（无 OD、无 PD-out）。 */
static void feed_process_read(void)
{
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK), IOLINK_MSEQ_TYPE_2, frame,
               sizeof(frame));
    feed(frame, sizeof(frame), 0U);
}

/* 命中计划：ISR 直接应答，帧仍带 eager=1 发布给主泵。 */
static int test_plan_hit_executes_reply(void)
{
    init_operate();
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (s_tx_n != 3U) {
        return 1; /* OD 占位 + PD-in 1 + CKS */
    }
    if ((s_tx_q[0] != 0x00U) || (s_tx_q[1] != 0x5AU)) {
        return 2;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 3;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 1U)) {
        return 4;
    }
    return 0;
}

/* 合法帧计划未命中：ISR 不应答、计入一次未命中并丢弃（邮箱保持为空）。
 * 主泵不再接手合法帧，因此后续处理不会补发应答。 */
static int test_valid_miss_is_dropped(void)
{
    init_operate();
    /* 撤回全部计划：发布字为 0 表示"没有计划表"。 */
    s_dll.main.shared.reply_plan_seq = 0U;

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 1; /* ISR 绝不应答计划未命中帧 */
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 2;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 3; /* 合法帧不进邮箱 */
    }

    /* 主泵一轮：不得补发应答，也不得凭空产生邮箱帧。 */
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 4;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 5;
    }

    /* 另一种确实无计划的形态：FALLBACK 状态不发布任何计划切片
     * （T_FBD 由主泵计时），合法 Type-0 帧只能按未命中丢弃。 */
    init_preoperate();
    s_dll.main.shared.state = IOLINK_DLL_STATE_FALLBACK;
    uint8_t write[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               write, sizeof(write));
    s_tx_n = 0U;
    feed(write, sizeof(write), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 6;
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 7;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 8;
    }
    return 0;
}

/* 现场回归：PREOPERATE 的 DPP 页写（地址 1 = MasterCycleTime）必须由
 * ISR 以 CKS 确认，帧仍交给主泵应用写入，且主泵不得重发第二次应答。
 * 诊断写走同一机制。 */
static int test_type0_page_write_is_answered(void)
{
    init_preoperate();

    /* 与现场一致：MC=0x21（页写地址 1），CKT=0x00（Type-0，CKS=0），OD=0x4F。 */
    const uint8_t wr[3] = {0x21U, 0x00U, 0x4FU};
    s_tx_n = 0U;
    feed(wr, sizeof(wr), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 1U) {
        return 1; /* 仅 CKS */
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 1U)) {
        return 3; /* 帧仍进邮箱，由主泵应用写入 */
    }

    /* 主泵应用写入，不重发应答。 */
    s_tx_n = 0U;
    iolink_dll_process(&s_dll.main);
    drain_tx();
    if (s_dll.main.master_cycle_time != 0x4FU) {
        return 4;
    }
    if (s_tx_n != 0U) {
        return 5; /* frame_replied_by_isr 必须抑制主泵的第二次应答 */
    }

    /* 页写使 DPP1 映像失效：主泵再跑一轮重建缓存。 */
    iolink_dll_process(&s_dll.main);

    /* 回读：ISR 页读拿到主泵刚写入的值。 */
    uint8_t rd[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               rd, sizeof(rd));
    s_tx_n = 0U;
    feed(rd, sizeof(rd), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((s_tx_n != 2U) || (s_tx_q[0] != 0x4FU)) {
        return 7;
    }

    /* 诊断写（事件确认）同样由 ISR 确认，帧仍进邮箱由主泵应用。 */
    uint8_t dw[3];
    make_frame(IOLINK_MC_CHANNEL_DIAGNOSIS, IOLINK_MSEQ_TYPE_0, dw, sizeof(dw));
    dw[2] = 0x00U;
    dw[1] = IOLINK_MSEQ_TYPE_0; /* 重算 CKS 前清掉 make_frame 写入的校验位 */
    dw[1] = (uint8_t) (dw[1] | iolink_checksum6(dw, sizeof(dw)));
    s_tx_n = 0U;
    feed(dw, sizeof(dw), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 1U) {
        return 8;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 9;
    }
    return 0;
}

/* 主站重发风暴：同一 FlowCTRL 的重复段不消耗新位置、不丢新段，
 * 且请求保持完整。 */
static int test_isdu_write_retry_storm_is_deduped(void)
{
    init_operate();
    /* 写索引 0x10（只读）0 字节：I-Service 0x14 / index / len / CHKPDU。 */
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};

    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t fc = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) i;
        uint8_t frame[3];
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | fc), IOLINK_MSEQ_TYPE_2, frame,
                   sizeof(frame));
        frame[2] = octets[i];
        frame[1] = IOLINK_MSEQ_TYPE_2;
        frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));

        s_tx_n = 0U;
        feed(frame, sizeof(frame), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
        if (s_tx_n == 0U) {
            return 1; /* 每个有效写段都必须由 ISR 立即 ACK */
        }
        if (i == 1U) {
            /* 在 COUNT1 之后连发 5 次重发（伪造载荷）：全部忽略。 */
            for (uint8_t r = 0U; r < 5U; r++) {
                uint8_t retry[3];
                make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | 1U), IOLINK_MSEQ_TYPE_2, retry,
                           sizeof(retry));
                retry[2] = 0xAAU;
                retry[1] = IOLINK_MSEQ_TYPE_2;
                retry[1] = (uint8_t) (retry[1] | iolink_checksum6(retry, sizeof(retry)));
                s_tx_n = 0U;
                feed(retry, sizeof(retry), 0U);
                iolink_dll_process_isr(&s_dll.main);
                drain_tx();
                if (s_tx_n == 0U) {
                    return 2; /* 重发同样要被 ACK */
                }
            }
        }
    }

    if ((s_dll.main.shared.isdu.req_idx != 4U) || (s_dll.main.shared.isdu.req_buf[1] != 0x10U) ||
        (s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE)) {
        return 3; /* 重发没有推进位置，也没有覆盖已接受的 COUNT1 */
    }

    /* 主泵接管并执行：请求保持有效，产生否定应答。 */
    iolink_dll_process(&s_dll.main);
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_IDLE) ||
        (s_dll.main.shared.isdu.state != ISDU_STATE_RESPONSE_READY)) {
        return 4;
    }
    return 0;
}

/* 上一条请求已收齐但主泵尚未接管时又收到 START：前一条被覆盖并计数。 */
static int test_new_start_overwrites_undrained_request(void)
{
    init_operate();
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};

    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t fc = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) i;
        uint8_t frame[3];
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | fc), IOLINK_MSEQ_TYPE_2, frame,
                   sizeof(frame));
        frame[2] = octets[i];
        frame[1] = IOLINK_MSEQ_TYPE_2;
        frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
        s_tx_n = 0U;
        feed(frame, sizeof(frame), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
    }
    if (s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE) {
        return 1;
    }
    if (s_dll.main.shared.isr_od_overruns != 0U) {
        return 2;
    }

    /* 主泵未接管前又发 START：覆盖前一条并计数。 */
    uint8_t next[3];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START), IOLINK_MSEQ_TYPE_2, next,
               sizeof(next));
    next[2] = 0x93U;
    next[1] = IOLINK_MSEQ_TYPE_2;
    next[1] = (uint8_t) (next[1] | iolink_checksum6(next, sizeof(next)));
    s_tx_n = 0U;
    feed(next, sizeof(next), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n == 0U) {
        return 3;
    }
    if (s_dll.main.shared.isr_od_overruns != 1U) {
        return 4;
    }
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) ||
        (s_dll.main.shared.isdu.req_buf[0] != 0x93U)) {
        return 5;
    }

    /* 相位契约：主泵只能在 COMPLETE 时接管。新请求处于 RECEPTION，
     * iolink_isdu_process() 不得把被覆盖的旧请求当作有效请求执行。 */
    iolink_dll_process(&s_dll.main);
    if ((s_dll.main.shared.isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION) ||
        (s_dll.main.shared.isdu.state != ISDU_STATE_IDLE) ||
        (s_dll.main.shared.isdu.req_buf[0] != 0x93U)) {
        return 6;
    }

    /* 公共统计映射：内部计数必须能通过 iolink_dll_get_stats() 读到。 */
    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&s_dll.main, &stats);
    if (stats.isr_od_overruns != 1U) {
        return 7;
    }
    return 0;
}

/* 非法帧（校验和损坏）：仍以 eager=0 发布到邮箱供主泵处理，
 * 不计计划未命中，也不产生应答。 */
static int test_invalid_frame_is_published(void)
{
    init_operate();

    feed_process_read();
    s_rx_q[1] ^= 0x01U; /* 破坏 CKT 中的 A.1.6 校验和 */
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (s_tx_n != 0U) {
        return 1; /* 非法帧绝不应答 */
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2; /* 非法帧不计计划未命中 */
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 0U)) {
        return 3; /* 主泵必须能看到它 */
    }
    return 0;
}

/* 计划命中但 PHY 无法发送：不是覆盖缺口，帧以 eager=0 交主泵兜底，
 * 且不计入 isr_plan_misses。 */
static int test_plan_hit_without_tx_is_deferred(void)
{
    /* 公共入口对 NULL 上下文是空操作。 */
    iolink_dll_publish_reply_plans(NULL);
    iolink_dll_invalidate_reply_plans(NULL);

    init_preoperate_rx_only();
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    s_tx_n = 0U;
    feed(frame, sizeof(frame), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 1; /* 没有 TX 能力：不得应答 */
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 2; /* 计划命中，只是无法执行 */
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&s_dll.main.shared.isr_frame_ring);
    if ((rec == NULL) || (rec->eager != 0U)) {
        return 3; /* 帧必须交给主泵，且主泵可应答 */
    }
    return 0;
}

/* t_PD 保护窗口内丢弃的邮箱帧必须计数（此前完全静默）。 */
static int test_t_pd_discards_are_counted(void)
{
    init_operate();
    s_dll.main.shared.reply_plan_seq = 0U;

    /* 非法帧仍进邮箱（合法帧计划未命中会被丢弃，无法触发该路径）。 */
    feed_process_read();
    s_rx_q[1] ^= 0x01U;
    iolink_dll_process_isr(&s_dll.main);
    if (iolink_ring_count(&s_dll.main.shared.isr_frame_ring) != 1U) {
        return 1;
    }

    dll_timer_arm(&s_dll.main, IOLINK_TIMER_T_PD, s_now_us + 10000U);
    iolink_dll_process(&s_dll.main); /* t_PD 有效：丢弃窗口内的帧 */
    if (!iolink_ring_is_empty(&s_dll.main.shared.isr_frame_ring)) {
        return 2;
    }
    if (s_dll.main.t_pd_discards != 1U) {
        return 3;
    }

    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&s_dll.main, &stats);
    if (stats.t_pd_discards != 1U) {
        return 4;
    }
    return 0;
}

/* 主泵正在覆盖计划表（发布字为奇数）：不得执行半更新计划。 */
static int test_seqlock_window_is_a_miss(void)
{
    init_operate();
    const IOLINK_ATOMIC_UINT_T stable = s_dll.main.shared.reply_plan_seq;
    s_dll.main.shared.reply_plan_seq = (IOLINK_ATOMIC_UINT_T) (stable + 1U); /* 写入中 */

    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 1;
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 2;
    }
    if (iolink_ring_peek(&s_dll.main.shared.isr_frame_ring) != NULL) {
        return 3; /* 合法帧不发布：计划未命中即丢弃 */
    }

    s_dll.main.shared.reply_plan_seq = stable;
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 3U) {
        return 4;
    }
    return 0;
}

/* ISDU 写暂存后，读视图必须失效：下一次 START 只能返回"无服务"。 */
static int test_isdu_write_invalidates_read_view(void)
{
    init_operate();
    /* 已完成的应答（4 字节）作为当前读视图。 */
    s_dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    s_dll.main.shared.isdu.resp_buf[0] = 0xD4U;
    s_dll.main.shared.isdu.resp_buf[1] = 0x12U;
    s_dll.main.shared.isdu.resp_buf[2] = 0x34U;
    s_dll.main.shared.isdu.resp_buf[3] = 0xF2U;
    s_dll.main.shared.isdu.resp_total = 4U;
    iolink_dll_process(&s_dll.main); /* 发布快照（代数 +1） */
    if (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) != 4U) {
        return 1;
    }
    const uint8_t gen_before = IOLINK_ISDU_PUB_GEN(s_dll.main.shared.isr_isdu_pub);

    /* 先读一次 START：拿到旧应答的首字节（od_width = 1）。 */
    uint8_t frame[3];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    s_tx_n = 0U;
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    /* OPERATE 长应答：[OD][PD-in]CKS。 */
    if ((s_tx_n != 3U) || (s_tx_q[0] != 0xD4U) || (s_tx_q[1] != 0x5AU)) {
        return 2;
    }

    /* 完整的 ISDU 写请求（0x14 0x10 0x00 0x04）：START + 3 个 COUNT。
     * 只发 START/COUNT 才进入写入口；单独的 COUNT 不构成请求。 */
    static const uint8_t octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};
    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t fc = (i == 0U) ? (uint8_t) IOLINK_FLOWCTRL_START : (uint8_t) i;
        uint8_t wr[3];
        make_frame((uint8_t) (IOLINK_MC_CHANNEL_ISDU | fc), IOLINK_MSEQ_TYPE_2, wr, sizeof(wr));
        wr[2] = octets[i];
        wr[1] = IOLINK_MSEQ_TYPE_2;
        wr[1] = (uint8_t) (wr[1] | iolink_checksum6(wr, sizeof(wr)));
        feed(wr, sizeof(wr), 0U);
        iolink_dll_process_isr(&s_dll.main);
        drain_tx();
    }
    if (s_dll.main.shared.isr_isdu_invalidated != 1U) {
        return 3; /* 写已暂存：主泵必须重建读视图 */
    }

    /* 主泵尚未应用该写：START 只能读到"无服务"（0x00）。 */
    s_tx_n = 0U;
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
               IOLINK_MSEQ_TYPE_2, frame, 2U);
    feed(frame, 2U, 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((s_tx_n != 3U) || (s_tx_q[0] != 0x00U)) {
        return 4; /* 不得再返回 0xD4（旧应答） */
    }

    /* 主泵应用写并产出新应答：失效旗标清除，代数翻转。 */
    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isr_isdu_invalidated != 0U) {
        return 5;
    }
    const uint8_t gen_after = IOLINK_ISDU_PUB_GEN(s_dll.main.shared.isr_isdu_pub);
    if ((gen_after == gen_before) && (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) == 4U)) {
        return 6; /* 旧视图被重新发布：代数必须能区分两份应答 */
    }
    return 0;
}

/* 启动探测计划：STARTUP 中的页读同时完成 T1 迁移。 */
static int test_startup_probe_plan(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 1U;
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;
    iolink_dll_process(&s_dll.main); /* 主泵窗口：按当前配置发布计划切片 */

    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    feed(frame, sizeof(frame), 1U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    if (s_dll.main.shared.state != IOLINK_DLL_STATE_PREOPERATE) {
        return 1;
    }
    if (s_dll.main.shared.isr_dsio_clear == 0U) {
        return 2;
    }
    if (s_tx_n != 2U) {
        return 3; /* 页字节 + CKS */
    }
    return 0;
}

/* Type-0 IDLE 读：ISR 回 od_width 个 0x00 + CKS，帧不进邮箱，并置
 * shared.isr_isdu_read_reset；主泵消费后清 0 并复位读传输层。 */
static int test_type0_idle_read_resets_transport(void)
{
    init_preoperate();

    /* 先发布一个 4 字节应答，使"无服务"与旧应答可区分。 */
    s_dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    s_dll.main.shared.isdu.resp_buf[0] = 0xD4U;
    s_dll.main.shared.isdu.resp_buf[1] = 0x12U;
    s_dll.main.shared.isdu.resp_buf[2] = 0x34U;
    s_dll.main.shared.isdu.resp_buf[3] = 0xF2U;
    s_dll.main.shared.isdu.resp_total = 4U;
    iolink_dll_process(&s_dll.main); /* 发布读视图 */
    if (IOLINK_ISDU_PUB_LEN(s_dll.main.shared.isr_isdu_pub) != 4U) {
        return 1;
    }

    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE),
               IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    s_tx_n = 0U;
    feed(frame, sizeof(frame), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();

    /* OD（od_width = 1，全 0）+ CKS。 */
    if ((s_tx_n != 2U) || (s_tx_q[0] != 0x00U)) {
        return 2;
    }
    if (iolink_ring_count(&s_dll.main.shared.isr_frame_ring) != 0U) {
        return 3; /* CONSUME：帧不进邮箱 */
    }
    if (s_dll.main.shared.isr_isdu_read_reset == 0U) {
        return 4;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 5;
    }

    /* IDLE 之后紧跟 COUNT：不得重放旧段，只能回"无服务"（0x00 + CKS）。 */
    uint8_t count[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | 0x01U), IOLINK_MSEQ_TYPE_0,
               count, sizeof(count));
    s_tx_n = 0U;
    feed(count, sizeof(count), 0U);
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if ((s_tx_n != 2U) || (s_tx_q[0] != 0x00U)) {
        return 8; /* 旧段索引必须已被 IDLE 清零 */
    }

    iolink_dll_process(&s_dll.main);
    if (s_dll.main.shared.isr_isdu_read_reset != 0U) {
        return 6;
    }
    if (!iolink_isdu_is_idle(&s_dll.main.shared.isdu)) {
        return 7;
    }
    return 0;
}

/* 计划携带 t_A 预算：COM2 下为 10 * 26 us = 260 us。 */
static int test_plan_deadline_metadata(void)
{
    init_operate();
    if (s_dll.main.shared.reply_plan_total == 0U) {
        return 1;
    }
    for (uint8_t i = 0U; i < s_dll.main.shared.reply_plan_total; i++) {
        if (s_dll.main.shared.reply_plan[i].deadline_us != 260U) {
            return 2;
        }
    }
    return 0;
}

/** @brief OPERATE 切片中 Type-0 页读槽位的动作；未找到返回 0xFF。 */
static uint8_t type0_page_read_action(void)
{
    const uint8_t off = s_dll.main.shared.reply_plan_off[IOLINK_PLAN_SLOT_OPERATE];
    const uint8_t cnt = s_dll.main.shared.reply_plan_cnt[IOLINK_PLAN_SLOT_OPERATE];
    for (uint8_t i = 0U; i < cnt; i++) {
        const iolink_reply_plan_t* plan = &s_dll.main.shared.reply_plan[off + i];
        if ((plan->match_mask == (IOLINK_MC_RW_MASK | IOLINK_MC_COMM_CHANNEL_MASK)) &&
            (plan->match_value == (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE)) &&
            (plan->ckt_mask == IOLINK_MSEQ_TYPE_MASK) && (plan->ckt_value == IOLINK_MSEQ_TYPE_0)) {
            return plan->action;
        }
    }
    return 0xFFU;
}

/** @brief OPERATE Type-0 页读请求（2 字节，无 OD）。 */
static void feed_type0_page_read(void)
{
    uint8_t frame[2];
    make_frame((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x02U), IOLINK_MSEQ_TYPE_0,
               frame, sizeof(frame));
    feed(frame, sizeof(frame), 0U);
}

/* 运行期能力位变化必须使计划失效：键不含能力位，不失效就会继续用旧计划。 */
static int test_capability_change_invalidates_plan(void)
{
    init_operate();
    /* 非兼容模式：OPERATE 中的 Type-0 页读是复位请求（不应答）。 */
    s_dll.main.shared.caps &= (iolink_caps_t) ~IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_invalidate_reply_plans(&s_dll.main);
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 1;
    }
    const IOLINK_ATOMIC_UINT_T seq_before = s_dll.main.shared.reply_plan_seq;
    if ((seq_before == 0U) || ((seq_before & 1U) != 0U)) {
        return 2;
    }

    /* 模拟 iolink_device_set_capabilities()：只改 caps。键不变，
     * 未发失效请求时发布必须提前返回，表内容保持旧语义。 */
    s_dll.main.shared.caps |= IOLINK_CAP_COMPAT_T0_READ;
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_RESET_STARTUP) {
        return 3;
    }
    if (s_dll.main.shared.reply_plan_seq != seq_before) {
        return 4;
    }

    /* 失效：发布字清 0，ISR 把该请求当"计划未命中"丢弃。 */
    iolink_dll_invalidate_reply_plans(&s_dll.main);
    if (s_dll.main.shared.reply_plan_seq != 0U) {
        return 5;
    }
    if (s_dll.main.reply_plan_key != 0U) {
        return 6;
    }
    s_tx_n = 0U;
    feed_type0_page_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 0U) {
        return 7; /* 失效期间没有即时应答 */
    }
    if (s_dll.main.shared.isr_plan_misses != 1U) {
        return 8;
    }

    /* 重发布：计划表随新能力位改变，ISR 恢复即时应答。 */
    iolink_dll_publish_reply_plans(&s_dll.main);
    if (type0_page_read_action() != IOLINK_REPLY_ACT_NONE) {
        return 9; /* 兼容模式：普通页读应答 */
    }
    const IOLINK_ATOMIC_UINT_T seq_after = s_dll.main.shared.reply_plan_seq;
    if ((seq_after == 0U) || ((seq_after & 1U) != 0U)) {
        return 10;
    }
    s_tx_n = 0U;
    feed_type0_page_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 2U) {
        return 11; /* 页字节 + CKS */
    }
    return 0;
}

/* tx_done 只是可选的帧结束通知：只有 tx_octet 的 PHY 仍应置位 CAP_TX 并能发送。 */
static int test_tx_without_done_hook(void)
{
    iolink_phy_api_t phy = s_phy;
    phy.tx_done = NULL; /* 变体：去掉可选完成回调 */

    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &phy);
    s_dll.main.shared.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 1U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 1U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    s_dll.main.shared.pd_in[0][1] = 0x5AU;
    s_now_us = 1000000U;
    s_tx_n = 0U;
    s_tx_active = 0U;

    if (!iolink_caps_has(s_dll.main.shared.caps, IOLINK_CAP_TX)) {
        return 1;
    }
    iolink_dll_publish_reply_plans(&s_dll.main);
    feed_process_read();
    iolink_dll_process_isr(&s_dll.main);
    drain_tx();
    if (s_tx_n != 3U) {
        return 2; /* OD 占位 + PD-in 1 + CKS */
    }
    if ((s_tx_q[0] != 0x00U) || (s_tx_q[1] != 0x5AU)) {
        return 3;
    }
    if (s_dll.main.shared.isr_plan_misses != 0U) {
        return 4;
    }
    return 0;
}

int main(void)
{
    int rc = test_plan_hit_executes_reply();
    if (rc != 0) {
        return 10 + rc;
    }
    rc = test_valid_miss_is_dropped();
    if (rc != 0) {
        return 20 + rc;
    }
    rc = test_isdu_write_retry_storm_is_deduped();
    if (rc != 0) {
        return 30 + rc;
    }
    rc = test_new_start_overwrites_undrained_request();
    if (rc != 0) {
        return 40 + rc;
    }
    rc = test_invalid_frame_is_published();
    if (rc != 0) {
        return 50 + rc;
    }
    rc = test_t_pd_discards_are_counted();
    if (rc != 0) {
        return 60 + rc;
    }
    rc = test_seqlock_window_is_a_miss();
    if (rc != 0) {
        return 70 + rc;
    }
    rc = test_isdu_write_invalidates_read_view();
    if (rc != 0) {
        return 80 + rc;
    }
    rc = test_startup_probe_plan();
    if (rc != 0) {
        return 90 + rc;
    }
    rc = test_type0_idle_read_resets_transport();
    if (rc != 0) {
        return 100 + rc;
    }
    rc = test_plan_deadline_metadata();
    if (rc != 0) {
        return 110 + rc;
    }
    rc = test_capability_change_invalidates_plan();
    if (rc != 0) {
        return 120 + rc;
    }
    rc = test_tx_without_done_hook();
    if (rc != 0) {
        return 130 + rc;
    }
    rc = test_type0_page_write_is_answered();
    if (rc != 0) {
        return 140 + rc;
    }
    rc = test_plan_hit_without_tx_is_deferred();
    if (rc != 0) {
        return 150 + rc;
    }
    return 0;
}
