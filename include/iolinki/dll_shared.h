/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_SHARED_H
#define IOLINK_DLL_SHARED_H

#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"

/**
 * @file dll_shared.h
 * @brief DLL 中由主上下文与数据 ISR 共同访问的状态。
 *
 * 这里集中存放两个上下文都会引用的字段（帧环、应答发送快照、
 * 配置镜像、子模块上下文）。dll.c 与 dll_isr.c 都包含此头文件；
 * ISR 独占的字段在 dll_isr.h 中，主上下文独占的字段在 dll_main.h 中。
 */

/**
 * @brief 主上下文与数据 ISR 共享的 DLL 状态。
 */
typedef struct iolink_dll_shared
{
    iolink_dll_isr_t* isr; /**< 组装箱绑定：ISR 独占状态（不透明指针，主上下文无法解引用） */

    volatile iolink_dll_state_t state; /**< 当前 DLL 状态 */

    const iolink_phy_api_t* phy; /**< 已绑定的 PHY API 实现 */

    /* 应用时钟，来自 iolink_device_config_t::time_us。DLL 与
     * 事件引擎的每个时间戳都使用它。数据 ISR 会读取它，因此
     * 该回调必须是单调的且 ISR 安全的。未绑定时钟的裸 DLL
     * 上下文读取到的时间为 0。 */
    uint64_t (*time_us)(void* user);

    void* time_user;

    volatile uint32_t last_activity_ms; /**< 最后收到字节的时间戳 */

    volatile uint8_t isr_master_activity; /**< ISR 观察到帧结束；由主上下文清除 */

    /* 一次性握手标志，由任一上下文置位、由主泵消费；
     * 64 位截止时间始终由主上下文拥有。每个标志由任一上下文置 1，
     * 且只能由主泵清 0；与消费竞争的一次置位是幂等的
     * （请求在本次或下一次处理中被应用）。
     * phy_faults_rearm 在临界区内被消费。 */
    volatile uint8_t isr_dsio_clear; /**< ISR 已处理启动探测：清除 T_DSIO */

    volatile uint8_t isr_fallback_pending; /**< ISR 已接受 FALLBACK：由主上下文启动 T_FBD */

    /* 配置 */
    uint8_t m_seq_type; /**< 支持的 M 序列类型 */

    uint8_t pd_in_len; /**< 输入过程数据长度 */

    uint8_t od_width; /**< OPERATE 的 OD 宽度 m（Table A.10）。某帧
                           是否携带 OD 由每帧的 MC R/W 位决定
                           （Figure A.14）：WRITE 请求在 PD-out 之后
                           携带 OD，READ 应答在 PD-in 之前携带 OD，
                           反方向则不携带。 */

    /* ISR ISDU 读取视图。主泵将 {length, busy, buffer} 作为一个
     * 原子字发布，使 ISR 绝不会观察到半更新的元组；ISR 拥有
     * FlowCTRL 段索引，并在 START 处采纳新的字。 */
    volatile IOLINK_ATOMIC_UINT_T isr_isdu_pub; /**< len | busy<<8 | sel<<9 */

    uint8_t isr_isdu_last_flowctrl; /**< 最后接受的 FlowCTRL（0xFF = 无） */

    iolink_ring_t isr_frame_ring; /**< 帧邮箱（ISR -> 主上下文） */

    uint32_t isr_mailbox_drops; /**< 因邮箱已满而丢弃的帧 */

    iolink_ring_t isr_od_ring; /**< 暂存的 ISDU 写入（ISR -> 主上下文） */

    uint32_t isr_od_overruns; /**< 因环满而丢弃的暂存写入 */

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    iolink_dll_isr_stats_t isr_stat; /**< ISR 自有的计数器组（SPLIT 模式） */
#endif

    volatile uint8_t in_isr; /**< ISR 正在运行泵 */

    /* 应答发送，每次数据请求喂送一个字节。主泵
     * 在槽位 0/1 之间交替，使在途的 TX-FRAME DMA 缓冲区在
     * 准备下一个应答期间保持稳定；槽位 2 保留给
     * 数据 ISR 自行生成的应答，因此两个上下文之间不共享
     * 槽位计数器（PHY 会对传输进行串行化）。 */
    uint8_t isr_tx_payload[IOLINK_TX_SLOTS][IOLINK_M_SEQ_MAX_LEN];

    volatile uint8_t isr_tx_pending_slot; /**< 暂存的主上下文应答所在槽位 */

    volatile uint8_t isr_tx_len; /**< 持有的载荷字节数 */

    volatile uint8_t isr_tx_idx; /**< 下一个字节索引 */

    volatile uint8_t isr_tx_has_cks; /**< 1 = 追加计算出的 CKS */

    volatile uint8_t isr_tx_active; /**< 排空期间为 1 */

    volatile uint8_t isr_tx_pending; /**< 1 = 主上下文处理器暂存了
                                          一个原样应答；
                                          ISR 必须发送它 */

    volatile uint8_t isr_tx_dropped; /**< 因仅 ISR 的 PHY 缺少
                                          request_service() 而已丢弃的
                                          应答 */

    uint8_t pd_out_len_current; /**< 当前运行时 PD_Out 长度 */

    volatile uint32_t last_byte_us; /**< 最后字节时间戳的低 32 位；两个
                                         上下文都以抗回绕的无符号差值
                                         比较它（t_byte 至多为毫秒级） */

    /* 过程数据缓冲区。PD-in 缓冲区同时用作应答发送缓冲区：
     * 布局为 [OD][PD-in][CKS]。主上下文填充一个既非活动
     * 也未被 ISR 持有的缓冲区，然后原子地切换；ISR 在应答
     * 开始时捕获活动副本并流式发送（零拷贝）。缓冲区数量
     * 取决于 TX 模式（见 IOLINK_PD_IN_BUFFERS）：若无空闲缓冲区，
     * 主上下文保留当前 PD-in 并在下一个周期重试。 */
    uint8_t pd_in[IOLINK_PD_IN_BUFFERS][IOLINK_PD_REPLY_MAX]; /**< [OD][PD-in][CKS] 应答缓冲区 */

    volatile uint8_t pd_in_active; /**< 活动 PD 应答缓冲区索引 */

    volatile uint8_t pd_in_captured; /**< ISR 正在流式发送的缓冲区（0xFF = 无） */

    volatile uint8_t fallback_count; /**< 为切换到 SIO 的连续回退计数 */

    /* 子模块 */
#if IOLINK_FEATURE_EVENTS
    iolink_events_ctx_t events; /**< 诊断事件引擎 */
#endif

    iolink_isdu_ctx_t isdu; /**< ISDU 服务引擎 */

    uint32_t framing_errors; /**< 累计成帧错误数（ISR 在 SHARED 统计模式下直接自增） */

    uint32_t timeout_errors; /**< 累计 t_A 超时数（ISR 在 SHARED 统计模式下直接自增） */

    iolink_log_sink_t log; /**< 日志汇点（默认静默；设备层绑定） */
} iolink_dll_shared_t;

/**
 * @brief PHY 传输能力辅助函数。
 *
 * 本库只有一条传输路径（ISR 分发泵）。绑定的
 * PHY 通过两个 ISR 模式编译开关表达它如何搬运数据：
 *  - RX      : rx_octet（字节）或 rx_frame（整帧）      [IOLINK_ISR_RX_MODE]
 *  - TX      : tx_octet + tx_done（字节）或 tx_frame + tx_busy（整帧） [IOLINK_ISR_TX_MODE]
 *  - wake-up : 存在 detect_wakeup（PHY 能观察到 C/Q 唤醒）
 * 这些辅助函数把能力检查集中在一处，而不是分散 NULL 判断。
 */
static inline bool dll_phy_has_rx(const iolink_dll_shared_t* ctx)
{
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return (ctx->phy != NULL) && (ctx->phy->rx_frame != NULL);
#else
    return (ctx->phy != NULL) && (ctx->phy->rx_octet != NULL);
#endif
}

static inline bool dll_phy_has_tx(const iolink_dll_shared_t* ctx)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    return (ctx->phy != NULL) && (ctx->phy->tx_frame != NULL);
#else
    return (ctx->phy != NULL) && (ctx->phy->tx_octet != NULL);
#endif
}

static inline bool dll_phy_has_wakeup(const iolink_dll_shared_t* ctx)
{
#if IOLINK_PHY_FEATURE_WAKEUP
    return (ctx->phy != NULL) && (ctx->phy->detect_wakeup != NULL);
#else
    (void) ctx;
    return false;
#endif
}

static inline int dll_phy_detect_wakeup(iolink_dll_shared_t* ctx)
{
#if IOLINK_PHY_FEATURE_WAKEUP
    return ((ctx->phy != NULL) && (ctx->phy->detect_wakeup != NULL))
               ? ctx->phy->detect_wakeup(ctx->phy->user)
               : 0;
#else
    (void) ctx;
    return 0;
#endif
}

#endif /* IOLINK_DLL_SHARED_H */
