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
#define IOLINK_STAT_ISR(ctx, field) ((ctx)->shared.isr_stat.field)
/** @brief 包含 ISR 自有计数的计数值（只读）。 */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->shared.field + (ctx)->shared.isr_stat.field)
#else
/** @brief 在 ISR 上下文中执行自增的计数器槽位。 */
#define IOLINK_STAT_ISR(ctx, field) ((ctx)->shared.field)
/** @brief 包含 ISR 自有计数的计数值（只读）。 */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->shared.field)
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

/**
 * @brief DLL 主上下文状态（含共享区）。
 *
 * 完整定义见 dll_main.h。主上下文只包含本类型；数据 ISR 的
 * 独占字段在 @ref iolink_dll_isr_t 中，dll.c 看不到它们。
 */
typedef struct iolink_dll_main iolink_dll_main_t;

/** @brief 数据 ISR 独占状态。完整定义见 dll_isr.h。 */
typedef struct iolink_dll_isr iolink_dll_isr_t;

/** @brief 单次分配容器（主上下文 + 数据 ISR）。定义见 dll_ctx.h。 */
typedef struct iolink_dll_ctx iolink_dll_ctx_t;

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
 * 清零主上下文与 ISR 状态、设置状态/重试默认值并复位计数器，
 * 同时把 @p isr 绑定到主上下文的共享区（@c ctx->shared.isr）。
 *
 * @param ctx 主上下文（通常为 @c iolink_dll_ctx_t::main）
 * @param isr ISR 独占状态（通常为 @c iolink_dll_ctx_t::isr）
 * @param phy 要绑定的 PHY 实现
 * @note 两个对象都会被清零，包括时钟。直接使用 DLL 的用户必须在此调用之后
 *       设置 @c ctx->shared.time_us / @c ctx->shared.time_user；
 *       iolink_device_init() 会从 iolink_device_config_t::time_us 绑定它们。
 */
void iolink_dll_init(iolink_dll_main_t* ctx, iolink_dll_isr_t* isr, const iolink_phy_api_t* phy);

/**
 * @brief 处理 DLL 逻辑
 *
 * 处理字节级数据并执行状态机迁移。
 *
 * @param ctx 要处理的 DLL 上下文
 */
void iolink_dll_process(iolink_dll_main_t* ctx);

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
uint8_t iolink_dll_expected_req_len(const iolink_dll_main_t* ctx, uint8_t mc);

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
void iolink_dll_process_isr(iolink_dll_main_t* ctx);

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
void iolink_dll_flush_state_callbacks(iolink_dll_main_t* ctx);

/** @brief Timer A（t_A 单次定时）在无应答的情况下到期。 */
void iolink_dll_on_rx_timeout(iolink_dll_main_t* ctx);

/** @brief Timer B 节拍：授权一个主上下文服务窗口。 */
void iolink_dll_on_stack_tick(iolink_dll_main_t* ctx);

/** @brief 消费 Timer B 的授权（1 = 用户代码可以运行）。 */
uint8_t iolink_dll_take_service_window(iolink_dll_main_t* ctx);

/**
 * @brief 为可变类型（1_V、2_V）设置当前 PD 长度
 *
 * @param ctx DLL 上下文
 * @param pd_in_len 新的 PD_In 长度
 * @param pd_out_len 新的 PD_Out 长度
 * @return int 成功返回 0，范围错误返回负值
 */
int iolink_dll_set_pd_length(iolink_dll_main_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/**
 * @brief 获取当前 PD 长度
 *
 * @param ctx DLL 上下文
 * @param[out] pd_in_len 当前 PD_In 长度
 * @param[out] pd_out_len 当前 PD_Out 长度
 */
void iolink_dll_get_pd_length(const iolink_dll_main_t* ctx, uint8_t* pd_in_len,
                              uint8_t* pd_out_len);

/**
 * @brief 请求切换到 SIO 模式（单线通信）
 *
 * @param ctx DLL 上下文
 * @return int 成功返回 0
 */
int iolink_dll_set_sio_mode(iolink_dll_main_t* ctx);

/**
 * @brief 请求切换到 SDCI 模式（基于 UART 的交换）
 *
 * @param ctx DLL 上下文
 * @return int 成功返回 0
 */
int iolink_dll_set_sdci_mode(iolink_dll_main_t* ctx);

/**
 * @brief 获取当前工作模式
 *
 * @param ctx DLL 上下文
 * @return iolink_phy_mode_t 当前模式
 */
iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_main_t* ctx);

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
void iolink_dll_request_back_to_box(iolink_dll_main_t* ctx);

/**
 * @brief 设置通信波特率
 *
 * @param ctx DLL 上下文
 * @param baudrate 期望的波特率（COM1、COM2 或 COM3）
 * @return int 成功返回 0
 */
int iolink_dll_set_baudrate(iolink_dll_main_t* ctx, iolink_baudrate_t baudrate);

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
int iolink_dll_set_operating_baudrate(iolink_dll_main_t* ctx, iolink_baudrate_t baudrate);

/**
 * @brief 获取当前协商后的波特率
 *
 * @param ctx DLL 上下文
 * @return iolink_baudrate_t 当前波特率
 */
iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_main_t* ctx);

/** 根据 M 序列定时计算设备的 MinCycleTime（单位 0.1 ms）。 */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_main_t* ctx);

/**
 * @brief 获取 DLL 统计
 *
 * @param ctx DLL 上下文
 * @param[out] out_stats 输出统计结构，填入计数器的快照
 */
void iolink_dll_get_stats(const iolink_dll_main_t* ctx, iolink_dll_stats_t* out_stats);

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
void iolink_dll_reset_diagnostics(iolink_dll_main_t* ctx);

/**
 * @brief 复位 ErrorCount 计数器，不改动事件引擎。
 *
 * B.2.20/Table 101：ErrorCount 统计自上次上电或复位以来的错误。
 * 由恢复出厂设置使用，它复位计数器，但撤销活动
 * 事件而不是丢弃它们。
 *
 * @param ctx DLL 上下文
 */
void iolink_dll_reset_error_counters(iolink_dll_main_t* ctx);

/**
 * @brief 启用/禁用定时强制检查（t_ren / t_cycle）
 *
 * @param ctx DLL 上下文
 * @param enable true 表示启用，false 表示禁用
 */
void iolink_dll_set_timing_enforcement(iolink_dll_main_t* ctx, bool enable);

/**
 * @brief 覆盖 t_ren 限制（对所有波特率生效）
 *
 * @param ctx DLL 上下文
 * @param limit_us 新的 t_ren 限制，单位为微秒（0 表示禁用强制检查）
 */
void iolink_dll_set_t_ren_limit_us(iolink_dll_main_t* ctx, uint32_t limit_us);

/** @} */ /* iolinki_dll 结束 */

#endif  // IOLINK_DLL_H
