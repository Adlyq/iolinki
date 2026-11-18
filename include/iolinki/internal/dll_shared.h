/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_SHARED_H
#define IOLINK_DLL_SHARED_H

#include "iolinki/capability.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/types.h"

IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file dll_shared.h
 * @brief DLL 中由主上下文与数据 ISR 共同访问的状态。
 *
 * 这里集中存放两个上下文都会引用的字段（帧环、应答发送快照、
 * 配置镜像、子模块上下文）。主上下文与 ISR 翻译单元都包含此头文件；
 * ISR 独占的字段在 dll_isr.h 中，主上下文独占的字段在 dll_main.h 中。
 */

/** @brief 计划槽位的状态切片：ISR 只扫描当前状态的连续切片。 */
typedef enum
{
    IOLINK_PLAN_SLOT_STARTUP = 0,    /**< STARTUP 与 AWAITING_COMM 共用 */
    IOLINK_PLAN_SLOT_PREOPERATE = 1, /**< PREOPERATE */
    IOLINK_PLAN_SLOT_ESTAB_COM = 2,  /**< 与 OPERATE 共用内容（首帧仍交主泵） */
    IOLINK_PLAN_SLOT_OPERATE = 3,    /**< OPERATE */
    IOLINK_PLAN_SLOT_COUNT = 4
} iolink_reply_plan_slot_t;

/**
 * @brief 共享区（主上下文内嵌，ISR 入口只看它）。
 *
 * 完整定义见 dll_shared.h。数据 ISR 的入口与内部泵只接受本类型，
 * 因此 ISR 翻译单元在编译期看不到 @ref iolink_dll_main_t 的字段。
 */
struct iolink_dll_shared
{
    /* =====================================================================
     * 区域 1：init 只读（c_）
     *
     * 初始化闭合（iolink_dll_init / iolink_device_init /
     * iolink_dll_publish_reply_plans）写入，此后主上下文与数据 ISR 都只读。
     * ===================================================================== */

    iolink_dll_isr_t* isr; /**< 组装箱绑定：ISR 独占状态（不透明指针，主上下文无法解引用） */

    const iolink_phy_api_t* phy; /**< 已绑定的 PHY API 实现 */

    /* 实例能力位。iolink_caps_resolve() 在 init 时一次性解析；
     * 主上下文与 ISR 都只读它（位掩码是自然对齐的单字加载）。 */
    iolink_caps_t caps;

    /* 应用时钟，来自 iolink_device_app_t::time_us。DLL 与
     * 事件引擎的每个时间戳都使用它。数据 ISR 会读取它，因此
     * 该回调必须是单调的且 ISR 安全的。未绑定时钟的裸 DLL
     * 上下文读取到的时间为 0。 */
    iolink_clock_t clock;

    uint8_t m_seq_type; /**< 支持的 M 序列类型 */

    uint8_t pd_in_len; /**< 输入过程数据长度（初始化时固定） */

    uint8_t od_width; /**< OPERATE 的 OD 宽度 m（Table A.10）。某帧
                           是否携带 OD 由每帧的 MC R/W 位决定
                           （Figure A.14）：WRITE 请求在 PD-out 之后
                           携带 OD，READ 应答在 PD-in 之前携带 OD，
                           反方向则不携带。 */

    uint8_t pd_out_len_current; /**< 初始化时固定的 PD_Out 长度 */

    uint32_t min_cycle_time_app_us; /**< 周期时间的应用层下限 */

    /**
     * @name 应答计划表（初始化时单次写入 / ISR 只读）
     *
     * 计划内容是 {M-sequence 类型, OD 宽度, 能力位} 的纯函数，三项都只在
     * 初始化期间定义。`reply_plans_ready` 由 `iolink_dll_publish_reply_plans()`
     * 在整张表写完后置 1、写入前清 0：ISR 入口只在它为 1 时进入传输泵，
     * 初始化窗口内到达的帧因此完全不被处理。该标志只保护入口，重建仍要求
     * 数据 ISR 静止（初始化或重新初始化）：没有运行期并发协议，查表也就不
     * 需要序号校验。
     * @{
     */
    volatile IOLINK_ATOMIC_UINT_T reply_plans_ready;         /**< 计划表已完整发布 */
    uint8_t reply_plan_off[IOLINK_PLAN_SLOT_COUNT];          /**< 每状态切片起点 */
    uint8_t reply_plan_cnt[IOLINK_PLAN_SLOT_COUNT];          /**< 每状态切片长度 */
    iolink_reply_plan_t reply_plan[IOLINK_REPLY_PLAN_SLOTS]; /**< 计划槽位 */
    /** @} */

    iolink_log_sink_t log; /**< 日志汇点（默认静默；设备层绑定） */

    /* =====================================================================
     * 区域 2：main -> ISR（m_）
     *
     * 主上下文写、数据 ISR 读。ISR 不得回写这些字段；需要回执时使用
     * 区域 3 的一次性标志。
     * ===================================================================== */

    /** @brief 主泵已应用的 ISDU 写序号（与 `isr_isdu_write_gen` 配对）。 */
    volatile uint32_t isdu_write_gen_applied;

    /* ISR ISDU 读取视图。主泵将 {length, busy, buffer} 作为一个
     * 原子字发布，使 ISR 绝不会观察到半更新的元组；ISR 拥有
     * FlowCTRL 段索引，并在 START 处采纳新的字。 */
    volatile IOLINK_ATOMIC_UINT_T isr_isdu_pub; /**< len | busy<<8 | sel<<9 | gen<<10 */

    /* 应答发送，每次数据请求喂送一个字节。主泵
     * 在槽位 0/1 之间交替，使在途的 TX-FRAME DMA 缓冲区在
     * 准备下一个应答期间保持稳定；槽位 2 保留给
     * 数据 ISR 自行生成的应答，因此两个上下文之间不共享
     * 槽位计数器（PHY 会对传输进行串行化）。 */
    uint8_t isr_tx_payload[IOLINK_TX_SLOTS][IOLINK_M_SEQ_MAX_LEN];

    /* 主上下文独占的暂存元数据：主泵在 isr_tx_pending 发布之前写好，
     * 数据 ISR 只在取走暂存时一次性读入自己独占的流式状态
     * （iolink_dll_isr_t::tx_stream_len / tx_stream_has_cks）。 */
    volatile uint8_t isr_tx_pending_slot; /**< 暂存的主上下文应答所在槽位 */

    volatile uint8_t stage_len; /**< 暂存载荷字节数 */

    volatile uint8_t stage_has_cks; /**< 1 = 发送时追加计算出的 CKS */

    /* 过程数据缓冲区。PD-in 缓冲区同时用作应答发送缓冲区：
     * 布局为 [OD][PD-in][CKS]。主上下文填充一个既非活动
     * 也未被 ISR 持有的缓冲区，然后发布 `pd_pub`；ISR 在应答
     * 开始时捕获同一代的 active 与 valid 并流式发送（零拷贝）。缓冲区数量
     * 取决于 TX 模式（见 IOLINK_PD_IN_BUFFERS）：若无空闲缓冲区，
     * 主上下文保留当前 PD-in 并在下一个周期重试。 */
    uint8_t pd_in[IOLINK_PD_IN_BUFFERS][IOLINK_PD_REPLY_MAX]; /**< [OD][PD-in][CKS] 应答缓冲区 */

    volatile IOLINK_ATOMIC_UINT_T pd_pub; /**< PD-in 发布字 `{active, valid, gen}`
                                              （主上下文单写） */

    uint8_t master_cycle_time; /**< DPP1 0x01，由主站写入的周期时间字节
                                    （Table B.1/B.3）；0 = 尚未写入 */

    /* =====================================================================
     * 区域 3：ISR -> main（i_）
     *
     * 数据 ISR 写、主上下文读。主上下文只允许写字面量 0 作为消费/清除；
     * 其余写操作属于 ISR。
     * ===================================================================== */

    /**
     * @brief ISDU 写失效序号（ISR 单写、自由计数）。
     *
     * ISR 每接受一个有效的写 START/ABORT 就自增一次（先发布写入口相位，
     * 再发布本序号）。主泵在把该写应用或中止、并发布新读视图之后，把
     * `isdu_write_gen_applied` 追到同一个值。两者不等期间 ISR 读路径一律
     * 回 0x00，"写后立刻读"因此不会读到旧应答；主站最多多看到一次
     * "无服务"。序号宽度必须容纳主泵可能错过的全部写实例，不能用 8 位。
     */
    volatile IOLINK_ATOMIC_UINT_T isr_isdu_write_gen;

    volatile uint8_t isr_master_activity; /**< ISR 观察到帧结束；由主上下文清除 */

    /**
     * @brief ISR 完全消费（CONSUME 计划）的帧结束时间戳（ISR -> 主泵）。
     *
     * 这类帧不进邮箱，主泵因此看不到它们；但 T_CYCLE/T_REN 记账与
     * `last_response_us` 仍须覆盖每一个 MasterCycle。ISR 先写
     * `isr_consumed_us`（32 位微秒，单字对齐存储），穿过 `IOLINK_FENCE()`
     * 后再自增 `isr_consumed_seq` 作为发布（release）边沿。主泵按
     * seq -> `IOLINK_FENCE()` -> us -> `IOLINK_FENCE()` -> seq 的顺序做双读
     * 校验（acquire），两次 seq 相同才采纳该时间戳，并在
     * `iolink_dll_main_t::isr_consumed_seq_seen` 记录已消费的序号。
     *
     * 原子性：release/acquire 保证读到新序号的主泵一定读到对应的新时间戳，
     * `IOLINK_CONCURRENCY=SMP` 下（两个内核或两个线程）因此不再依赖"写者
     * 在中断返回后对读者可见"这一单核假设。双读的作用是检测"读取期间 ISR
     * 又交接了一帧"（seq 变化即重试）。8 位序号是自由计数、永不清零；主泵
     * 两次运行之间恰好消费 256 帧时会漏掉一次更新，只影响一次 T_CYCLE 检查。
     */
    volatile uint32_t isr_consumed_us; /**< ISR 消费帧的时间戳低 32 位 */

    volatile uint8_t isr_consumed_seq; /**< 消费帧发布序号（自由计数） */

    /* 一次性握手标志，由任一上下文置位、由主泵消费；
     * 64 位截止时间始终由主上下文拥有。每个标志由任一上下文置 1，
     * 且只能由主泵清 0；与消费竞争的一次置位是幂等的
     * （请求在本次或下一次处理中被应用）。
     * 诊断来源的 ISR 脉冲位于 iolink_conditions_t::isr_pulse。 */
    volatile uint8_t isr_dsio_clear; /**< ISR 已处理启动探测：清除 T_DSIO */

    volatile uint8_t isr_fallback_pending; /**< ISR 已接受 FALLBACK：由主上下文启动 T_FBD */

    /**
     * @brief ISR 已完全服务一个 MasterCycle（ISR -> 主泵，一次性）。
     *
     * CONSUME 帧不进邮箱，主泵看不到它们；但 10.7.5 的 Back-to-box 判定
     * 必须覆盖每一个服务过的周期。`back_to_box_cycles` 归主上下文独占，
     * ISR 不对它做读改写，只置本标志。主泵在
     * `dll_apply_isr_deadline_requests()` 中消费：标志为 1 时清 0，并在
     * `back_to_box_pending` 为真时推进一次。一个主泵周期因此最多推进一次
     * （ISR 在此期间消费多帧时计数偏保守：只会晚停，绝不早停）。
     */
    volatile uint8_t isr_cycle_served;

    /** @brief ISR 触发了 STARTUP：主泵据此作废在途 ISDU 写并复位读传输层。 */
    volatile uint8_t isdu_restart_pending;

    /** @brief ISR 进入了 STARTUP；主上下文必须上报该事件。 */
    volatile uint8_t startup_notification_pending;

    /** @brief ISR 收到 0x97 DeviceStartup：主泵完成协议错误记账与状态迁移。 */
    volatile uint8_t isr_device_startup_requested;

    /**
     * @brief ISDU 读传输层复位请求（ISR -> 主泵，一次性）。
     *
     * ISR 在 ISDU 读的 FlowCTRL 为 IDLE/IDLE2/ABORT（0x11..0x1F）时仍回
     * 0x00（无服务），并置 1 请求主泵复位读传输层。Type-0 下这类读由
     * CONSUME 计划完全消费，主泵看不到该帧，复位只能经此标志传达。
     * 与 isr_dsio_clear 相同的一次性握手：ISR 置位幂等，只有主泵清 0。
     */
    volatile uint8_t isr_isdu_read_reset;

    volatile uint8_t in_isr; /**< ISR 正在运行泵 */

    iolink_ring_t isr_frame_ring; /**< 帧邮箱（ISR -> 主上下文） */

    uint32_t isr_mailbox_drops; /**< 因邮箱已满而丢弃的帧 */

    uint32_t isr_plan_misses; /**< ISR 未命中应答计划而丢弃的合法帧 */

    uint32_t isr_eager_rejects; /**< ISR 早期拒绝的帧（校验失败的 ISDU 写，以及带
                                     TX 能力的 PHY 上非 Type-0 形状的校验失败帧）；
                                     主上下文只经 iolink_dll_get_stats() 读取 */

    uint32_t isr_frames_consumed; /**< 由 ISR 完全处理、不再进入邮箱的帧
                                      （含按规范有意不应答的转换/复位帧） */

    uint32_t isr_frames_handoff; /**< 成功投递邮箱交给主泵的帧 */

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    iolink_dll_isr_stats_t isr_stat; /**< ISR 自有的计数器组（SPLIT 模式） */
#endif

    /* =====================================================================
     * 区域 4：双上下文（b_）
     *
     * 两个上下文都读、都写；写操作必须是单字存储或由文档化的临界区/序号
     * 协议保护。
     * ===================================================================== */

    volatile iolink_dll_state_t state; /**< 当前 DLL 状态 */

    volatile uint32_t last_activity_ms; /**< 最后收到字节的时间戳 */

    volatile uint32_t last_byte_us; /**< 最后字节时间戳的低 32 位；两个
                                         上下文都以抗回绕的无符号差值
                                         比较它（t_byte 至多为毫秒级） */

    volatile uint8_t isr_tx_idx; /**< 下一个字节索引（ISR 流式状态） */

    volatile uint8_t isr_tx_active; /**< 排空期间为 1（ISR 流式状态） */

    volatile uint8_t isr_tx_pending; /**< 1 = 主上下文处理器暂存了
                                          一个原样应答；
                                          ISR 必须发送它 */

    volatile uint8_t isr_tx_dropped; /**< 因仅 ISR 的 PHY 缺少
                                          request_service()，或暂存与在途
                                          应答冲突而丢弃的应答 */

    volatile IOLINK_ATOMIC_UINT_T pd_valid_cmd; /**< PD 有效位命令字
                                                    `{value, base_gen, valid}`（数据 ISR 单写） */

    volatile uint8_t pd_in_captured; /**< ISR 正在流式发送的缓冲区（0xFF = 无）；
                                          TX BYTE 帧末释放，TX FRAME 由 ISR 在下次
                                          进入时按 tx_busy() 释放 */

    volatile uint8_t fallback_count; /**< 连续 CRC 错误计数（诊断脉冲节流） */

    volatile uint8_t fallback_clear_request; /**< 已应用非 FALLBACK 命令：清除 T_FBD */

    iolink_isdu_ctx_t isdu; /**< ISDU 服务引擎 */

    uint32_t framing_errors; /**< 累计成帧错误数（ISR 在 SHARED 统计模式下直接自增） */

    uint32_t timeout_errors; /**< 累计 t_A 超时数（ISR 在 SHARED 统计模式下直接自增） */

    /* 子模块 */
#if IOLINK_FEATURE_EVENTS
    iolink_events_ctx_t events; /**< 诊断事件引擎 */
#endif
};

/**
 * @name PD-in 发布字
 *
 * 主上下文是 @ref iolink_dll_shared_t::pd_pub 的唯一写者：填完应答缓冲区后
 * 一次原子存储发布 `{active, valid, gen}`（release 屏障在 `iolink_atomic_store()`
 * 内）。数据 ISR 只做一次加载，因此它捕获的 active 与随后上报的 valid 取自
 * 同一代；每次发布把 `gen` 前进一格。
 *
 * 位布局：`active[7:0] | valid[8] | gen[23:16]`。
 * @{
 */
#define IOLINK_PD_PUB_ACTIVE(word) ((uint8_t) ((word) & 0xFFU))
#define IOLINK_PD_PUB_VALID(word) ((uint8_t) (((word) >> 8) & 0x01U))
#define IOLINK_PD_PUB_GEN(word) ((uint8_t) (((word) >> 16) & 0xFFU))
#define IOLINK_PD_PUB_WORD(active, valid, gen)           \
    ((IOLINK_ATOMIC_UINT_T) (uint8_t) (active) |         \
     ((IOLINK_ATOMIC_UINT_T) ((valid) ? 1U : 0U) << 8) | \
     ((IOLINK_ATOMIC_UINT_T) (uint8_t) (gen) << 16))
/** @} */

/**
 * @name PD 有效位命令字
 *
 * 数据 ISR 是 @ref iolink_dll_shared_t::pd_valid_cmd 的唯一写者：执行
 * 0x98 `PROCESS_DATA_OPERATE` / 0x99 `DEVICE_OPERATE` 时记录命令值与当时
 * @ref iolink_dll_shared_t::pd_pub 的 `gen`。读侧只在 `base_gen` 仍等于当前
 * gen 时采用命令值，因此该覆盖只影响命令所在的那一代：主上下文下一次发布
 * 即恢复应用给出的有效位。失效的命令由 ISR 自己撤回（见 dll_isr.c）。
 *
 * 位布局：`value[0] | base_gen[15:8] | valid[16]`。
 * @{
 */
#define IOLINK_PD_CMD_VALUE(word) ((uint8_t) ((word) & 0x01U))
#define IOLINK_PD_CMD_BASE_GEN(word) ((uint8_t) (((word) >> 8) & 0xFFU))
#define IOLINK_PD_CMD_VALID(word) ((uint8_t) (((word) >> 16) & 0x01U))
#define IOLINK_PD_CMD_WORD(value, base_gen)       \
    ((IOLINK_ATOMIC_UINT_T) ((value) ? 1U : 0U) | \
     ((IOLINK_ATOMIC_UINT_T) (uint8_t) (base_gen) << 8) | ((IOLINK_ATOMIC_UINT_T) 1U << 16))
/** @} */

/** @brief 读取当前一代的 PD-in 发布字（active 与 valid 同代）。 */
static inline IOLINK_ATOMIC_UINT_T dll_pd_pub_load(const iolink_dll_shared_t* shared)
{
    return iolink_atomic_load(&shared->pd_pub);
}

/**
 * @brief @p pub 这一代的有效位：命令字覆盖它记录的那一代，其余情况取应用值。
 *
 * 两个字段各自单写者，因此读侧不需要 RMW：先加载发布字、再加载命令字，
 * 命令字只有在代号仍然对得上时才生效。
 */
static inline bool dll_pd_valid_at(const iolink_dll_shared_t* shared, IOLINK_ATOMIC_UINT_T pub)
{
    const IOLINK_ATOMIC_UINT_T cmd = iolink_atomic_load(&shared->pd_valid_cmd);
    if ((IOLINK_PD_CMD_VALID(cmd) != 0U) &&
        (IOLINK_PD_CMD_BASE_GEN(cmd) == IOLINK_PD_PUB_GEN(pub))) {
        return IOLINK_PD_CMD_VALUE(cmd) != 0U;
    }
    return IOLINK_PD_PUB_VALID(pub) != 0U;
}

/**
 * @brief 发布一次 PD-in（主上下文单写），`gen` 自动前进一格。
 *
 * 调用方必须在应答缓冲区填完之后调用；屏障由 `iolink_atomic_store()` 提供。
 */
static inline void dll_pd_publish(iolink_dll_shared_t* shared, uint8_t active, bool valid)
{
    const uint8_t gen = (uint8_t) (IOLINK_PD_PUB_GEN(shared->pd_pub) + 1U);
    iolink_atomic_store(&shared->pd_pub, IOLINK_PD_PUB_WORD(active, valid, gen));
}

/**
 * @brief PHY 传输能力辅助函数。
 *
 * 本库只有一条传输路径（ISR 分发泵）。PHY 的实际能力在 init 时由
 * `iolink_caps_resolve()` 解析成 `ctx->caps`（见 capability.h），这里
 * 只做薄委托：能力判定点唯一，且本文件不再出现 vtable 形状相关的
 * `#if`。
 */
static inline bool dll_phy_has_rx(const iolink_dll_shared_t* ctx)
{
    return (ctx != NULL) && iolink_caps_has(ctx->caps, IOLINK_CAP_RX);
}

static inline bool dll_phy_has_tx(const iolink_dll_shared_t* ctx)
{
    return (ctx != NULL) && iolink_caps_has(ctx->caps, IOLINK_CAP_TX);
}

static inline bool dll_phy_has_wakeup(const iolink_dll_shared_t* ctx)
{
    return (ctx != NULL) && iolink_caps_has(ctx->caps, IOLINK_CAP_WAKEUP);
}

static inline int dll_phy_detect_wakeup(iolink_dll_shared_t* ctx)
{
    if (!dll_phy_has_wakeup(ctx)) {
        return 0;
    }
    return ctx->phy->detect_wakeup(ctx->phy->self);
}

IOLINK_ASSUME_NONNULL_END
#endif /* IOLINK_DLL_SHARED_H */
