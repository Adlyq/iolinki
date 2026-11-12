/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_H
#define IOLINK_DLL_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/phy.h"
#include "iolinki/config.h"
#include "iolinki/port.h"
#include "iolinki/ring.h"

/**
 * @file dll.h
 * @brief IO-Link 数据链路层（DLL）实现
 */

/**
 * @defgroup iolinki_dll 数据链路层（DLL）
 * @brief 帧定时、M 序列交换、模式与波特率控制以及 DLL 统计。
 * @{
 */

/**
 * @brief IO-Link DLL 状态机状态
 */
typedef enum
{
    IOLINK_DLL_STATE_STARTUP = 0U,       /**< 等待唤醒请求 */
    IOLINK_DLL_STATE_AWAITING_COMM = 1U, /**< 已检测到唤醒，等待首帧 */
    IOLINK_DLL_STATE_PREOPERATE = 2U,    /**< 处理 ISDU（Type 0） */
    IOLINK_DLL_STATE_ESTAB_COM = 3U,     /**< 建立通信（转入 OPERATE） */
    IOLINK_DLL_STATE_OPERATE = 4U,       /**< 循环数据交换已激活 */
    IOLINK_DLL_STATE_FALLBACK = 5U,      /**< 错误恢复 / 回退 */
    IOLINK_DLL_STATE_COMM_DISABLED = 6U  /**< Back-to-box：通信停止直至复位 */
} iolink_dll_state_t;

/**
 * @brief DLL 状态机状态改变时调用的可选钩子。
 *
 * 通知基于电平：钩子由
 * iolink_dll_flush_state_callbacks() 以 *当前* 状态调用，条件是它
 * 与上次交付的状态不同。因此回调绝不会运行在
 * 中断上下文中，最终状态绝不会被漏掉，而中间状态
 * 若在两次交付之间进入又离开，会被合并进
 * 后一个状态；例外是 ISR 侧进入 STARTUP 的迁移，它总会
 * 被交付（Table 45 T11 要求该复位能到达系统管理）。
 * 由核心层用于驱动应用生命周期回调。
 *
 * 该钩子在主上下文中运行，但也可能从
 * iolink_dll_process() 内部被调用（主上下文的状态迁移会立即通知，
 * 而此时帧分发仍在栈上）。因此钩子不得对
 * iolink_dll_* / iolink_device_* 做重入调用。
 */
typedef void (*iolink_dll_state_cb_t)(void* user, iolink_dll_state_t state);

/** 本库成帧的最大 M 序列长度（请求或应答），以字节计。 */
#define IOLINK_M_SEQ_MAX_LEN (IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U)

/* PD 应答发送缓冲区：[OD][PD-in][CKS]。按运行时 PD-in 长度
 * 加上 OD 头与尾部校验和来确定尺寸。 */
#define IOLINK_PD_REPLY_MAX (IOLINK_OD_MAX_SIZE + IOLINK_PD_IN_MAX_SIZE + 1U)
/* PD-in 应答缓冲区：活动的、正在填充的，以及被 ISR 持有的。
 *
 * TX BYTE 在库内部结束传输，会在帧结束时释放 ISR 持有的
 * 缓冲区（pd_in_captured = 0xFF），因此两个缓冲区就够用。
 * TX FRAME 把缓冲区交给 DMA，除非 PHY 提供 tx_busy()，否则库无法
 * 观察到传输状态：提供该钩子时，一旦线路空闲就释放租约
 * （两个缓冲区）；不提供时该槽位会一直保持租约直到下一次
 * 捕获，因此用第三个缓冲区吸收主泵的更新。 */
#if (IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME) && !IOLINK_PHY_FEATURE_TX_BUSY
#define IOLINK_PD_IN_BUFFERS 3U
#else
#define IOLINK_PD_IN_BUFFERS 2U
#endif
/** 接收帧邮箱深度（数据 ISR -> 主泵）。取 2 的幂；2
 * 个槽位可在主泵仍在处理上一帧时吸收一帧，
 * 因此繁忙的主循环窗口不再丢弃下一个请求。 */
#define IOLINK_MBX_SLOTS 2U
/** 暂存 ISDU 写入深度（数据 ISR -> 主泵）；同样取 2 的幂。 */
#define IOLINK_OD_SLOTS 2U

IOLINK_STATIC_ASSERT((IOLINK_MBX_SLOTS >= 2U) &&
                         ((IOLINK_MBX_SLOTS & (IOLINK_MBX_SLOTS - 1U)) == 0U),
                     "IOLINK_MBX_SLOTS must be a power of two >= 2");
IOLINK_STATIC_ASSERT((IOLINK_OD_SLOTS >= 2U) && ((IOLINK_OD_SLOTS & (IOLINK_OD_SLOTS - 1U)) == 0U),
                     "IOLINK_OD_SLOTS must be a power of two >= 2");

/** 应答暂存槽位：0/1 供主泵使用（交替），2 供 ISR 使用。 */
#define IOLINK_TX_SLOTS 3U
/** 数据 ISR 用于自身立即应答的槽位。 */
#define IOLINK_TX_ISR_SLOT 2U
IOLINK_STATIC_ASSERT(IOLINK_TX_SLOTS == IOLINK_TX_ISR_SLOT + 1U,
                     "the ISR reply slot must be the last reply slot");

/**
 * @brief 数据 ISR 也会更新的计数器。
 *
 * 在 `IOLINK_STATS_MODE=SPLIT` 下，各上下文各自保留一份副本，因此
 * 自增操作不会在其读取与写回之间被抢占而丢失；
 * 对外的统计会对两组计数求和。在 `SHARED`（默认）下，ISR 写入
 * 与主泵相同的字段，可能丢失一次更新。
 */
typedef struct
{
    uint32_t framing_errors; /**< 被 ISR 成帧引擎拒绝的帧 */
    uint32_t timeout_errors; /**< ISR 观察到的 Timer A 超时 */
    uint32_t isr_tx_dropped; /**< ISR 无法发送的应答 */
} iolink_dll_isr_stats_t;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
/** @brief 在 ISR 上下文中执行自增的计数器槽位。 */
#define IOLINK_STAT_ISR(ctx, field) ((ctx)->isr_stat.field)
/** @brief 包含 ISR 自有计数的计数值（只读）。 */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->field + (ctx)->isr_stat.field)
#else
/** @brief 在 ISR 上下文中执行自增的计数器槽位。 */
#define IOLINK_STAT_ISR(ctx, field) ((ctx)->field)
/** @brief 包含 ISR 自有计数的计数值（只读）。 */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->field)
#endif

/**
 * @name ISDU 读取视图打包
 *
 * 主泵将已成帧的应答视图作为一个 `IOLINK_ATOMIC_UINT_T` 发布，
 * 使数据 ISR 能在一次访问中读到一致的 {length, busy, buffer} 元组。
 * ISR 保留它在 START 处采纳的最后一个字的副本；任何变化（新的
 * 应答，或 Busy 迁移）都会打断 FlowCTRL 序列，这正是
 * 增量视图原本的行为。
 * @{
 */
#define IOLINK_ISDU_PUB_LEN(word) ((uint8_t) ((word) & 0xFFU))
#define IOLINK_ISDU_PUB_BUSY(word) ((uint8_t) (((word) >> 8) & 0x01U))
#define IOLINK_ISDU_PUB_SEL(word) ((uint8_t) (((word) >> 9) & 0x01U))
#define IOLINK_ISDU_PUB_WORD(len, busy, sel)                                                     \
    ((IOLINK_ATOMIC_UINT_T) (uint8_t) (len) | ((IOLINK_ATOMIC_UINT_T) ((busy) ? 1U : 0U) << 8) | \
     ((IOLINK_ATOMIC_UINT_T) ((sel) ? 1U : 0U) << 9))
/** @} */

/** @brief 一个暂存的 ISDU 写段（数据 ISR -> 主泵）。 */
typedef struct
{
    uint8_t flowctrl;                 /**< 请求 MC 字节中的 FlowCTRL 半字节 */
    uint8_t len;                      /**< @ref data 中的 OD 字节 */
    uint8_t data[IOLINK_OD_MAX_SIZE]; /**< OD 字节 */
} iolink_od_rec_t;

/**
 * @brief 邮箱环中的一个接收帧（数据 ISR -> 主泵）。
 *
 * 该记录携带主泵处理该帧所需的全部信息，因此没有
 * 任何逐帧状态存在于环之外：长度以及 ISR
 * 是否已发送应答（eager）都随字节一同传递。
 */
typedef struct
{
    uint8_t len;                        /**< 帧长度，以字节计 */
    uint8_t eager;                      /**< 非零表示 ISR 已应答该帧 */
    uint8_t data[IOLINK_M_SEQ_MAX_LEN]; /**< 帧字节 */
} iolink_frame_rec_t;

#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/data_storage.h"

/**
 * @brief 数据链路层上下文
 *
 * DLL 引擎的内部状态与数据存储。
 */
typedef struct
{
    volatile iolink_dll_state_t state; /**< 当前 DLL 状态 */
    const iolink_phy_api_t* phy;       /**< 已绑定的 PHY API 实现 */
    /* 应用时钟，来自 iolink_device_config_t::time_us。DLL 与
     * 事件引擎的每个时间戳都使用它。数据 ISR 会读取它，因此
     * 该回调必须是单调的且 ISR 安全的。未绑定时钟的裸 DLL
     * 上下文读取到的时间为 0。 */
    uint64_t (*time_us)(void* user);
    void* time_user;
    volatile uint32_t last_activity_ms;        /**< 最后收到字节的时间戳 */
    uint32_t last_valid_frame_ms;              /**< 最后一个校验和/类型有效的帧的时间戳 */
    volatile uint32_t master_last_activity_ms; /**< 最后一次帧结束 / RX 超时的时间戳 */
    uint8_t master_lost_cycles;                /**< 判定 master_lost 前可错过的周期数；0 = 禁用 */
    volatile uint8_t master_lost_count;        /**< 已错过的周期数，饱和于 0xFF */
    volatile bool master_lost;                 /**< 主站通信丢失（应用层提示） */
    volatile uint8_t isr_master_activity;      /**< ISR 观察到帧结束；由主上下文清除 */
    bool wakeup_seen;                          /**< 检测到唤醒（若 PHY 支持） */
    volatile bool back_to_box_pending;         /**< 已请求 Back-to-box；正在倒计帧数 */
    volatile uint8_t back_to_box_cycles;       /**< 自请求 Back-to-box 以来已见的帧数 */

    /* 一次性握手标志，由任一上下文置位、由主泵消费；
     * 64 位截止时间始终由主上下文拥有。每个标志由任一上下文置 1，
     * 且只能由主泵清 0；与消费竞争的一次置位是幂等的
     * （请求在本次或下一次处理中被应用）。
     * phy_faults_rearm 在临界区内被消费。 */
    volatile uint8_t isr_dsio_clear;         /**< ISR 已处理启动探测：清除 T_DSIO */
    volatile uint8_t isr_fallback_pending;   /**< ISR 已接受 FALLBACK：由主上下文启动 T_FBD */
    volatile uint8_t fallback_clear_request; /**< 已应用非 FALLBACK 命令：清除 T_FBD */
    volatile uint8_t phy_faults_rearm;       /**< 事件引擎已复位：重新启动故障锁存 */

    /* 配置 */
    uint8_t m_seq_type; /**< 支持的 M 序列类型 */
    uint8_t pd_in_len;  /**< 输入过程数据长度 */
    uint8_t pd_out_len; /**< 输出过程数据长度 */
    uint8_t od_len;     /**< 流式路径的 OD 长度（1 或 2 字节）。 */
    uint8_t od_width;   /**< OPERATE 的 OD 宽度 m（Table A.10）。某帧
                             是否携带 OD 由每帧的 MC R/W 位决定
                             （Figure A.14）：WRITE 请求在 PD-out 之后
                             携带 OD，READ 应答在 PD-in 之前携带 OD，
                             反方向则不携带。 */
    /* ISR ISDU 读取视图。主泵将 {length, busy, buffer} 作为一个
     * 原子字发布，使 ISR 绝不会观察到半更新的元组；ISR 拥有
     * FlowCTRL 段索引，并在 START 处采纳新的字。 */
    volatile IOLINK_ATOMIC_UINT_T isr_isdu_pub; /**< len | busy<<8 | sel<<9 */
    IOLINK_ATOMIC_UINT_T isr_isdu_adopted;      /**< 由 ISR 采纳的字 */
    uint8_t isr_isdu_len;                       /**< 采纳的应答长度（0 = 无） */
    uint8_t isr_isdu_sel;                       /**< 采纳的应答缓冲区索引 */
    uint8_t isr_isdu_idx;                       /**< 下一个应答字节（由 ISR 推进） */
    uint8_t isr_isdu_last_flowctrl;             /**< 最后接受的 FlowCTRL（0xFF = 无） */
    uint8_t isr_isdu_seg_idx;                   /**< 最后服务的段偏移 */

    /* 原始字节 ISR 泵状态（由库拥有，由 PHY 数据
     * 中断驱动）。ISR 上下文只触碰这些字段；应答
     * 暂存缓冲区与字节喂送由库拥有（PHY 只负责
     * SPI 电气访问）。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    uint8_t isr_rx_buf[IOLINK_M_SEQ_MAX_LEN]; /**< 已累积的 RX 字节 */
    uint8_t isr_rx_cnt;                       /**< 本帧已累积的字节 */
    uint8_t isr_rx_acc;                       /**< 按 A.1.6 逐字节累积的校验和 */
    uint8_t isr_rx_expect;                    /**< 期望长度（0 = 未知） */
#endif
    iolink_frame_rec_t isr_frame_slot[IOLINK_MBX_SLOTS]; /**< 邮箱环存储 */
    iolink_ring_t isr_frame_ring;                        /**< 帧邮箱（ISR -> 主上下文） */
    volatile uint8_t frame_replied_by_isr;               /**< 当前处理帧对应记录中 eager 标志的
                                                     主上下文快照。 */
    uint32_t isr_mailbox_drops;                          /**< 因邮箱已满而丢弃的帧 */
    uint32_t pd_in_skips;       /**< 跳过的 PD-in 发布：所有空闲缓冲区均已被租用 */
    uint32_t isr_eager_rejects; /**< eager 路径拒绝的帧
                                     （校验和/类型错误） */
    /* ISR 侧的 ISDU 暂存 + 应答快照（单写者模型）。ISR
     * 将写 OD 字节暂存到环中，只读取应答快照；
     * 主泵拥有 ctx->isdu，并在其变化时刷新快照。 */
    iolink_od_rec_t isr_od_slot[IOLINK_OD_SLOTS]; /**< 暂存写环存储 */
    iolink_ring_t isr_od_ring;                    /**< 暂存的 ISDU 写入（ISR -> 主上下文） */
    uint32_t isr_od_overruns;                     /**< 因环满而丢弃的暂存写入 */
#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    iolink_dll_isr_stats_t isr_stat; /**< ISR 自有的计数器组（SPLIT 模式） */
#endif
    volatile uint8_t stack_tick_pending;           /**< Timer B 节拍已授权一个
                                                        主上下文服务窗口 */
    volatile uint8_t in_isr;                       /**< ISR 正在运行泵 */
    volatile uint8_t isr_reentry_pending;          /**< 一个抢占的泵调用
                                                         已延后到外层调用 */
    volatile uint8_t startup_notification_pending; /**< ISR 进入了 STARTUP；主
                                                        上下文必须上报该事件 */
    iolink_dll_state_t last_notified_state;        /**< 已交付给 state_cb 的状态
                                                         （仅主上下文） */
    /* 应答发送，每次数据请求喂送一个字节。主泵
     * 在槽位 0/1 之间交替，使在途的 TX-FRAME DMA 缓冲区在
     * 准备下一个应答期间保持稳定；槽位 2 保留给
     * 数据 ISR 自行生成的应答，因此两个上下文之间不共享
     * 槽位计数器（PHY 会对传输进行串行化）。 */
    uint8_t isr_tx_payload[IOLINK_TX_SLOTS][IOLINK_M_SEQ_MAX_LEN];
    const uint8_t* isr_tx_base;           /**< 当前应答来源：
                                               长 PD 应答为 pd_in[active]，
                                               短应答/主上下文应答为
                                               isr_tx_payload */
    volatile uint8_t isr_tx_slot;         /**< 正在发送的槽位 */
    uint8_t isr_tx_next;                  /**< 主泵下一个填充的槽位（0/1） */
    volatile uint8_t isr_tx_pending_slot; /**< 暂存的主上下文应答所在槽位 */
    volatile uint8_t isr_tx_len;          /**< 持有的载荷字节数 */
    volatile uint8_t isr_tx_idx;          /**< 下一个字节索引 */
    volatile uint8_t isr_tx_flags;        /**< CKS 状态标志 */
    volatile uint8_t isr_tx_acc;          /**< 运行中的校验和累加器 */
    volatile uint8_t isr_tx_has_cks;      /**< 1 = 追加计算出的 CKS */
    volatile uint8_t isr_tx_active;       /**< 排空期间为 1 */
    volatile uint8_t isr_tx_pending;      /**< 1 = 主上下文处理器暂存了
                                               一个原样应答；
                                               ISR 必须发送它 */
    volatile uint8_t isr_tx_dropped;      /**< 因仅 ISR 的 PHY 缺少
                                               request_service() 而已丢弃的
                                               应答 */
    bool pd_valid;                        /**< 当前 PD_In 有效状态 */
    uint8_t master_cycle_time;            /**< DPP1 0x01，由主站写入的周期时间字节
                                               （Table B.1/B.3）；0 = 尚未写入 */
    uint32_t min_cycle_time_us;           /**< 最小周期时间，单位为微秒 */
    uint32_t min_cycle_time_app_us;       /**< 周期时间的应用层下限 */
    bool enforce_timing;                  /**< 启用定时检查（t_ren / t_cycle） */
    uint32_t t_ren_limit_us;              /**< 当前 t_ren 限制，单位为微秒 */
    bool t_ren_override;                  /**< 为 true 时使用覆盖后的 t_ren 限制 */
    uint32_t t_pd_delay_us;               /**< 上电延时（t_pd），单位为微秒 */

    /* 可变 PD 支持（用于 Type 1_V 和 2_V） */
    uint8_t pd_in_len_current;  /**< 当前运行时 PD_In 长度 */
    uint8_t pd_out_len_current; /**< 当前运行时 PD_Out 长度 */
    uint8_t pd_in_len_max;      /**< 允许的 PD_In 最大长度 */
    uint8_t pd_out_len_max;     /**< 允许的 PD_Out 最大长度 */

    /* SIO 模式 */
    iolink_phy_mode_t phy_mode; /**< 当前工作模式（SDCI 与 SIO） */

    /* 波特率支持 */
    iolink_baudrate_t baudrate;           /**< 协商后的波特率（COM1-COM3） */
    iolink_baudrate_t operating_baudrate; /**< 工作 COM；未配置时为 IOLINK_BAUDRATE_UNSET */

    /* 统一帧组装：主站请求为 MC + CKT + PD-out + OD，因此
       按编译期最大值确定尺寸（原为固定 48，一旦
       OD 宽度可达 IOLINK_OD_MAX_SIZE 就显得过小）。 */
    uint8_t frame_buf[IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U];
    uint8_t frame_index;            /**< 组装中的当前字节索引 */
    uint8_t req_len;                /**< 当前帧类型的期望长度 */
    uint64_t last_frame_us;         /**< 最后帧开始的微秒时间戳 */
    volatile uint32_t last_byte_us; /**< 最后字节时间戳的低 32 位；两个
                                         上下文都以抗回绕的无符号差值
                                         比较它（t_byte 至多为毫秒级） */
    uint64_t last_cycle_start_us;   /**< 最后周期开始的微秒时间戳 */
    uint32_t t_byte_limit_us;       /**< 字节间超时限制，单位为微秒 */
    uint64_t wakeup_deadline_us;    /**< 唤醒后最早可接受帧的时间 */
    uint64_t t_pd_deadline_us;      /**< 上电后最早可接受帧的时间 */
    uint64_t dsio_deadline_ms;      /**< 无有效报文时回退到 SIO 的截止时间 */
    uint64_t fallback_deadline_ms;  /**< 完成 FALLBACK MasterCommand 的截止时间 */

    /* 过程数据缓冲区。PD-in 缓冲区同时用作应答发送缓冲区：
     * 布局为 [OD][PD-in][CKS]。主上下文填充一个既非活动
     * 也未被 ISR 持有的缓冲区，然后原子地切换；ISR 在应答
     * 开始时捕获活动副本并流式发送（零拷贝）。缓冲区数量
     * 取决于 TX 模式（见 IOLINK_PD_IN_BUFFERS）：若无空闲缓冲区，
     * 主上下文保留当前 PD-in 并在下一个周期重试。 */
    uint8_t pd_in[IOLINK_PD_IN_BUFFERS][IOLINK_PD_REPLY_MAX]; /**< [OD][PD-in][CKS] 应答缓冲区 */
    volatile uint8_t pd_in_active;                            /**< 活动 PD 应答缓冲区索引 */
    volatile uint8_t pd_in_captured;        /**< ISR 正在流式发送的缓冲区（0xFF = 无） */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /**< 输出 PD 缓冲区（主站 -> 设备） */

    bool pd_out_seen; /**< 已收到 PDout 帧（B.2.24） */

    /* 错误计数器与统计 */
    uint32_t crc_errors;             /**< 累计 CRC 错误数 */
    uint32_t timeout_errors;         /**< 累计 t_A 超时数 */
    uint32_t idle_timeouts;          /**< 1 s 空闲链路诊断（主站已暂停） */
    uint32_t framing_errors;         /**< 累计成帧错误数 */
    uint32_t timing_errors;          /**< 累计定时违规次数 */
    uint32_t t_ren_violations;       /**< t_ren 违规次数 */
    uint32_t t_cycle_violations;     /**< t_cycle 违规次数 */
    uint32_t t_byte_violations;      /**< 字节间定时违规次数 */
    uint32_t t_pd_violations;        /**< t_pd 违规次数 */
    uint8_t retry_count;             /**< 当前交换的重试计数器 */
    uint32_t total_retries;          /**< 累计重试次数 */
    uint8_t max_retries;             /**< 配置的最大重试次数（默认 3） */
    uint32_t voltage_faults;         /**< 累计电压故障数（走 get_faults() 时按故障
                                          边沿计数，走旧路径时按轮询周期计数） */
    uint32_t overtemp_faults;        /**< 累计过温故障数（走 get_faults() 时按故障
                                          边沿计数，走旧路径时为 0） */
    uint32_t short_circuits;         /**< 累计短路次数（走 get_faults() 时按故障
                                          边沿计数，走旧路径时按轮询周期计数） */
    uint8_t phy_faults_last;         /**< 最后一次 get_faults() 的位掩码（用于边沿检测） */
    volatile uint8_t fallback_count; /**< 为切换到 SIO 的连续回退计数 */
    uint8_t sio_fallback_threshold;  /**< 进入 SIO 模式的回退阈值（默认 3） */

    /* 定时统计 */
    uint64_t last_response_us; /**< 最后应答的微秒时间戳 */
    uint32_t response_time_us; /**< 实测协议栈应答时间（t2） */

    /* 子模块 */
#if IOLINK_FEATURE_EVENTS
    iolink_events_ctx_t events; /**< 诊断事件引擎 */
#endif
    iolink_isdu_ctx_t isdu; /**< ISDU 服务引擎 */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t ds; /**< 数据存储引擎 */
#endif

    iolink_dll_state_cb_t state_cb; /**< 可选的状态变更钩子（由核心层设置） */
    void* state_cb_user;            /**< 传给 state_cb 的用户指针 */
    iolink_log_sink_t log;          /**< 日志汇点（默认静默；设备层绑定） */
} iolink_dll_ctx_t;

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
static inline bool dll_phy_has_rx(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return (ctx->phy != NULL) && (ctx->phy->rx_frame != NULL);
#else
    return (ctx->phy != NULL) && (ctx->phy->rx_octet != NULL);
#endif
}

static inline bool dll_phy_has_tx(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    return (ctx->phy != NULL) && (ctx->phy->tx_frame != NULL);
#else
    return (ctx->phy != NULL) && (ctx->phy->tx_octet != NULL);
#endif
}

static inline bool dll_phy_has_wakeup(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_PHY_FEATURE_WAKEUP
    return (ctx->phy != NULL) && (ctx->phy->detect_wakeup != NULL);
#else
    (void) ctx;
    return false;
#endif
}

static inline int dll_phy_detect_wakeup(iolink_dll_ctx_t* ctx)
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

/**
 * @brief DLL 统计快照
 */
typedef struct
{
    uint32_t crc_errors;         /**< 累计 CRC 错误数 */
    uint32_t timeout_errors;     /**< 累计超时数 */
    uint32_t framing_errors;     /**< 累计成帧错误数 */
    uint32_t timing_errors;      /**< 累计定时违规次数 */
    uint32_t t_ren_violations;   /**< t_ren 违规次数 */
    uint32_t t_cycle_violations; /**< t_cycle 违规次数 */
    uint32_t t_byte_violations;  /**< 字节间定时违规次数 */
    uint32_t t_pd_violations;    /**< t_pd 违规次数 */
    uint32_t total_retries;      /**< 累计重试次数 */
    uint32_t voltage_faults;     /**< 累计电压故障数（走 get_faults() 时按故障
                                     边沿计数，走旧路径时按轮询周期计数） */
    uint32_t overtemp_faults;    /**< 累计过温故障数（走 get_faults() 时按故障
                                     边沿计数，走旧路径时为 0） */
    uint32_t short_circuits;     /**< 累计短路次数（走 get_faults() 时按故障
                                     边沿计数，走旧路径时按轮询周期计数） */
    uint32_t isr_tx_dropped;     /**< 传输层无法发送的应答（主上下文与 ISR） */
    uint32_t idle_timeouts;      /**< 1 s 空闲链路诊断（不是 t_A 超时） */
} iolink_dll_stats_t;

/**
 * @brief 初始化 DLL 上下文
 *
 * 设置状态、重试的默认值，并复位计数器。
 *
 * @param ctx 要初始化的 DLL 上下文
 * @param phy 要绑定的 PHY 实现
 * @note 上下文会被清零，包括时钟。直接使用 DLL 的用户必须在此调用之后
 *       设置 @c ctx->time_us / @c ctx->time_user；
 *       iolink_device_init() 会从 iolink_device_config_t::time_us 绑定它们。
 */
void iolink_dll_init(iolink_dll_ctx_t* ctx, const iolink_phy_api_t* phy);

/**
 * @brief 处理 DLL 逻辑
 *
 * 处理字节级数据并执行状态机迁移。
 *
 * @param ctx 要处理的 DLL 上下文
 */
void iolink_dll_process(iolink_dll_ctx_t* ctx);

/**
 * @brief MC 字节为 @p mc 的主站请求的期望长度（字节）。
 *
 * 纯函数、ISR 安全且只读：仅由 DLL 状态 / 配置的类型和
 * MC 字节推导得出。库的 ISR 泵用它界定输入的
 * M 序列；主泵对同一帧复用该结果。
 * @param ctx DLL 上下文（只读）
 * @param mc  请求的主站命令（第一个）字节
 * @return 期望的请求长度，以字节计（Type-0 报文为 2）。
 */
uint8_t iolink_dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t mc);

/**
 * @brief 面向原始字节（单字节收发器）PHY 的 ISR 侧泵。
 *
 * 由 PHY 数据中断处理程序调用。它通过
 * phy->rx_octet() 拉取接收到的字节，对 M 序列成帧（使用纯函数
 * iolink_dll_expected_req_len()），把完整请求发布到由
 * iolink_dll_process() 消费的邮箱，从共享状态组装应答并
 * 通过 phy->tx_octet()/tx_done()（TX BYTE）或 phy->tx_frame()
 * （TX FRAME）喂送应答字节。它从不
 * 执行协议逻辑（ISDU / 参数 /
 * 事件 / 厂商相关部分）；这些仍留在主泵中。
 *
 * 无待处理数据时调用是安全的；当 INT 线未被
 * 置为有效时立即返回。对没有 rx_octet() 的 PHY 为空操作。
 * @param ctx DLL 上下文
 */
void iolink_dll_process_isr(iolink_dll_ctx_t* ctx);

/**
 * @brief 把当前 DLL 状态交付给状态变更钩子。
 *
 * 通知基于电平：当当前状态与上次
 * 交付的状态不同时，会用当前状态调用一次钩子。数据 ISR
 * 只改变状态，因此该函数是应用钩子唯一可能运行的
 * 位置（绝不在中断上下文中）。iolink_dll_process()
 * 在结束时调用它；设备层依赖这次调用，裸 DLL
 * 用户也可以从主循环中调用它。
 *
 * 两次调用之间进入并离开的两个状态会被合并到后一个
 * 状态；最终状态绝不会丢失。在数据 ISR 中发生的 STARTUP
 * 迁移会作为待处理通知保留下来，并在当前状态之前上报，
 * 因此快速的 STARTUP -> PREOPERATE 序列无法掩盖该复位。
 * 状态未改变时调用是安全的（会立即返回）。
 * @param ctx DLL 上下文
 */
void iolink_dll_flush_state_callbacks(iolink_dll_ctx_t* ctx);

/** @brief Timer A（t_A 单次定时）在无应答的情况下到期。 */
void iolink_dll_on_rx_timeout(iolink_dll_ctx_t* ctx);

/** @brief Timer B 节拍：授权一个主上下文服务窗口。 */
void iolink_dll_on_stack_tick(iolink_dll_ctx_t* ctx);

/** @brief 消费 Timer B 的授权（1 = 用户代码可以运行）。 */
uint8_t iolink_dll_take_service_window(iolink_dll_ctx_t* ctx);

/**
 * @brief 为可变类型（1_V、2_V）设置当前 PD 长度
 *
 * @param ctx DLL 上下文
 * @param pd_in_len 新的 PD_In 长度
 * @param pd_out_len 新的 PD_Out 长度
 * @return int 成功返回 0，范围错误返回负值
 */
int iolink_dll_set_pd_length(iolink_dll_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/**
 * @brief 获取当前 PD 长度
 *
 * @param ctx DLL 上下文
 * @param[out] pd_in_len 当前 PD_In 长度
 * @param[out] pd_out_len 当前 PD_Out 长度
 */
void iolink_dll_get_pd_length(const iolink_dll_ctx_t* ctx, uint8_t* pd_in_len, uint8_t* pd_out_len);

/**
 * @brief 请求切换到 SIO 模式（单线通信）
 *
 * @param ctx DLL 上下文
 * @return int 成功返回 0
 */
int iolink_dll_set_sio_mode(iolink_dll_ctx_t* ctx);

/**
 * @brief 请求切换到 SDCI 模式（基于 UART 的交换）
 *
 * @param ctx DLL 上下文
 * @return int 成功返回 0
 */
int iolink_dll_set_sdci_mode(iolink_dll_ctx_t* ctx);

/**
 * @brief 获取当前工作模式
 *
 * @param ctx DLL 上下文
 * @return iolink_phy_mode_t 当前模式
 */
iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t* ctx);

/**
 * @brief 请求执行 Back-to-box 序列（10.7.5）。
 *
 * ISDU 处理程序在成功应答 0x83
 * SystemCommand 后调用它。DLL 至少会继续服务帧 3 个 MasterCycle，
 * 随后停止并禁用通信（COMM_DISABLED）以及 PHY 输出
 * 驱动器，直到下一次上电。
 *
 * @param ctx DLL 上下文
 */
void iolink_dll_request_back_to_box(iolink_dll_ctx_t* ctx);

/**
 * @brief 设置通信波特率
 *
 * @param ctx DLL 上下文
 * @param baudrate 期望的波特率（COM1、COM2 或 COM3）
 * @return int 成功返回 0
 */
int iolink_dll_set_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate);

/** “未配置工作 COM”的哨兵值（恢复过程会保留 COM1）。 */
#define IOLINK_BAUDRATE_UNSET ((iolink_baudrate_t) 0xFF)

/**
 * @brief 设置设备的工作 COM 并立即应用
 *
 * 工作 COM 会由 iolink_dll_process() 在恢复过程
 * 掉到 COM1（空闲/回退）之后重新断言，与配置为固定 SDCI
 * 波特率的设备一致。传入 IOLINK_BAUDRATE_UNSET 可停止重复断言。
 *
 * @param ctx DLL 上下文
 * @param baudrate 工作波特率（COM1..COM3）
 * @return int 成功返回 0，参数无效返回 -1
 */
int iolink_dll_set_operating_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate);

/**
 * @brief 获取当前协商后的波特率
 *
 * @param ctx DLL 上下文
 * @return iolink_baudrate_t 当前波特率
 */
iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t* ctx);

/** 根据 M 序列定时计算设备的 MinCycleTime（单位 0.1 ms）。 */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_ctx_t* ctx);

/**
 * @brief 获取 DLL 统计
 *
 * @param ctx DLL 上下文
 * @param[out] out_stats 输出统计结构，填入计数器的快照
 */
void iolink_dll_get_stats(const iolink_dll_ctx_t* ctx, iolink_dll_stats_t* out_stats);

/**
 * @brief 复位设备诊断与状态（Table 101）
 *
 * 清除构成 ErrorCount（0x0020）的计数器，并丢弃事件队列、
 * Event 标志和 Table 58 事件存储，使 DeviceStatus（0x0024）和
 * DetailedDeviceStatus（0x0025）再次从 "0" 开始。用于设备
 * 复位和 Back-to-box。恢复出厂设置则改为撤销活动
 * 事件（Table 101 的 "Clear"），因此它使用
 * iolink_dll_reset_error_counters() 以及
 * iolink_events_clear_diagnosis()。应用复位不影响
 * 诊断信息。
 *
 * @param ctx DLL 上下文
 */
void iolink_dll_reset_diagnostics(iolink_dll_ctx_t* ctx);

/**
 * @brief 复位 ErrorCount 计数器，不改动事件引擎。
 *
 * B.2.20/Table 101：ErrorCount 统计自上次上电或复位以来的错误。
 * 由恢复出厂设置使用，它复位计数器，但撤销活动
 * 事件而不是丢弃它们。
 *
 * @param ctx DLL 上下文
 */
void iolink_dll_reset_error_counters(iolink_dll_ctx_t* ctx);

/**
 * @brief 启用/禁用定时强制检查（t_ren / t_cycle）
 *
 * @param ctx DLL 上下文
 * @param enable true 表示启用，false 表示禁用
 */
void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t* ctx, bool enable);

/**
 * @brief 覆盖 t_ren 限制（对所有波特率生效）
 *
 * @param ctx DLL 上下文
 * @param limit_us 新的 t_ren 限制，单位为微秒（0 表示禁用强制检查）
 */
void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t* ctx, uint32_t limit_us);

/** @} */ /* iolinki_dll 结束 */

#endif  // IOLINK_DLL_H
