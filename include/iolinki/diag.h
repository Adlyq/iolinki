/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DIAG_H
#define IOLINK_DIAG_H

#include <stdbool.h>
#include <stdint.h>

#include "iolinki/config.h"
#include "iolinki/port.h"

/**
 * @file diag.h
 * @brief 诊断源注册表：条件 → EventCode / DeviceStatus / DetailedDeviceStatus
 * 的单一派生点。
 *
 * 设计依据见 docs/DIAGNOSTICS_DESIGN.md。三类事实严格分开：
 *
 *  - **ROM 注册表**（`iolink_diag_desc_t`）：每个诊断来源的 EventCode、
 *    规范 EventType、Table D.1 首选 DeviceStatus、来源类型与 PULSE 保持窗口。
 *    同一份元数据供事件内存、DeviceStatus 与 DDS 共用。
 *  - **RAM 条件缓存**（`iolink_conditions_t`）：来源自身的物理/协议事实
 *    （ISR one-shot 脉冲、PULSE 最后发生时间、每个来源的边沿计数）。
 *  - **事件生命周期**：`iolink_events_ctx_t::pending[]` 的 `present`
 *    （条件锁存）与 `reported`（appears 已上线）仍然是唯一权威记录；
 *    本模块只通过 `iolink_event_trigger()` / `iolink_event_clear()` 驱动它。
 *
 * 因此 DeviceStatus、DDS 与 Table 58 事件内存都是同一组条件的投影，不再各自
 * 维护平行状态。`IOLINK_FEATURE_EVENTS=0` 时本模块整体消失（无残留代码），
 * 0x0025 仍返回“索引不可用”，0x0024 仍返回 0。
 */

/* B.2.22：DDS 最多 64 条记录，且每条 3 字节；ISDU 应答缓冲区必须装得下
 * 条件表的全部条目。把容量错误在编译期钉死，而不是在运行时越界。 */
IOLINK_STATIC_ASSERT(IOLINK_EVENT_PENDING_MAX <= 64U,
                     "DetailedDeviceStatus has at most 64 array elements (B.2.22)");
IOLINK_STATIC_ASSERT((3U * IOLINK_EVENT_PENDING_MAX) <= IOLINK_ISDU_BUFFER_SIZE,
                     "DetailedDeviceStatus must fit the ISDU response buffer");

struct iolink_events_ctx;
typedef struct iolink_events_ctx iolink_events_ctx_t;

/**
 * @defgroup iolinki_diag 诊断注册表
 * @brief 诊断来源、条件缓存与派生视图。
 * @{
 */

/**
 * @brief 注册表来源编号（固定顺序，条件缓存按此索引）。
 *
 * PHY 来源是电平源（每轮采样 `get_faults()` 的位）；协议通信来源是
 * 脉冲源（每次检测到一次错误发生即刷新保持窗口）。
 */
typedef enum
{
    IOLINK_DIAG_SRC_PHY_UNDERVOLTAGE = 0, /**< L+ 欠压（0x5111） */
    IOLINK_DIAG_SRC_PHY_OVERVOLTAGE,      /**< L+ 过压（0x5110） */
    IOLINK_DIAG_SRC_PHY_OVERTEMP,         /**< 收发器过温（0x4000） */
    IOLINK_DIAG_SRC_PHY_SHORT_CIRCUIT,    /**< C/Q 短路（0x7710） */
    IOLINK_DIAG_SRC_COMM_CRC,             /**< 校验和错误（0x1801） */
    IOLINK_DIAG_SRC_COMM_TIMEOUT,         /**< 通信超时（0x1802） */
    IOLINK_DIAG_SRC_COMM_FRAMING,         /**< 组帧错误（0x1803） */
    IOLINK_DIAG_SRC_COMM_TIMING,          /**< 时序违规（0x1804） */
    IOLINK_DIAG_SRC_COUNT                 /**< 来源数量（非来源） */
} iolink_diag_src_t;

/** @brief 槽位没有注册表来源（应用动态条目）。 */
#define IOLINK_DIAG_SRC_NONE 0xFFU

/** @brief 注册表没有首选 DeviceStatus（厂商特定代码）。 */
#define IOLINK_DIAG_STATUS_NONE 0xFFU

/** @brief 电平源：`get_faults()` 位，每轮采样一次。 */
#define IOLINK_DIAG_F_LEVEL 0x01U

/** @brief 脉冲源：每次错误发生刷新保持窗口。 */
#define IOLINK_DIAG_F_PULSE 0x02U

/** @brief Notification：单次触发，不进入 DDS，不提升 DeviceStatus。 */
#define IOLINK_DIAG_F_NOTIFICATION 0x04U

/** @brief 条件锁存：电平保持期间只上报一次。 */
#define IOLINK_DIAG_F_LATCHING 0x08U

/**
 * @brief ROM 注册表条目。
 *
 * 只存放规范元数据，不存放运行时状态；整张表是 `const`，可以放在 ROM。
 */
typedef struct
{
    uint16_t code;    /**< Table D.1 EventCode */
    uint16_t hold_ms; /**< PULSE：最后一次发生后的保持时间（0 = 不自动消失） */
    uint8_t type;     /**< 规范 EventType（iolink_event_type_t 取值） */
    uint8_t status;   /**< Table D.1 首选 DeviceStatus（0..4） */
    uint8_t flags;    /**< IOLINK_DIAG_F_* 位组合 */
    uint8_t src;      /**< iolink_diag_src_t 或 IOLINK_DIAG_SRC_NONE */
} iolink_diag_desc_t;

/**
 * @brief 每个注册表来源的 RAM 缓存（主上下文独占，ISR 只写脉冲字节）。
 *
 * 不保存条件本身：条件是否存在由 `pending[].present` 唯一决定。这里仅保存
 * 来源自身的输入事实，使边沿检测、PULSE 保持窗口与统计投影不必各自
 * 复制条件状态。
 */
typedef struct
{
    volatile uint8_t isr_pulse[IOLINK_DIAG_SRC_COUNT]; /**< ISR → 主：单字节 one-shot */
    uint32_t quiet_since_ms[IOLINK_DIAG_SRC_COUNT];    /**< PULSE：最后一次发生时间 */
    uint32_t edges[IOLINK_DIAG_SRC_COUNT];             /**< 每来源 0→1 边沿/脉冲计数 */
} iolink_conditions_t;

/**
 * @brief 一条 DDS 记录（Table B.14：EventQualifier + EventCode）。
 */
typedef struct
{
    uint16_t code;     /**< EventCode */
    uint8_t qualifier; /**< EventQualifier（A.6.4） */
} iolink_diag_dds_entry_t;

#if IOLINK_FEATURE_EVENTS

/**
 * @brief 注册表条目数（Table D.1 覆盖范围）。
 */
uint8_t iolink_diag_registry_size(void);

/**
 * @brief 按序号读取注册表条目（测试与 IODD 导出用）。
 *
 * @param index 0 .. iolink_diag_registry_size()-1
 * @return 条目指针；越界时为 NULL
 */
const iolink_diag_desc_t* iolink_diag_registry_at(uint8_t index);

/**
 * @brief 按 EventCode 查找注册表条目（忽略来源条目的 code 重复）。
 *
 * 系统来源的条目同时按 code 可查；厂商/未知代码返回 NULL。
 *
 * @param code 16 位 EventCode
 * @return 条目指针；未登记时为 NULL
 */
const iolink_diag_desc_t* iolink_diag_desc(uint16_t code);

/**
 * @brief 按来源取得其注册表条目。
 *
 * @param src 来源编号
 * @return 条目指针；越界或未登记时为 NULL
 */
const iolink_diag_desc_t* iolink_diag_src_desc(iolink_diag_src_t src);

/**
 * @brief 来源对应的 EventCode。
 *
 * @param src 来源编号
 * @return EventCode；未登记时为 0
 */
uint16_t iolink_diag_src_code(iolink_diag_src_t src);

/**
 * @brief 规范化 EventType：注册表命中时返回规范值，否则返回 @p fallback。
 *
 * @param code     16 位 EventCode
 * @param fallback 未登记代码使用的类型（兼容入口的调用方输入）
 * @return uint8_t EventType
 */
uint8_t iolink_diag_type_for(uint16_t code, uint8_t fallback);

/**
 * @brief Table D.1 首选 DeviceStatus：未登记时按类型回落到 0/2/4。
 *
 * @param code 16 位 EventCode
 * @param type 该条件的 EventType
 * @return uint8_t DeviceStatus（0..4）
 */
uint8_t iolink_diag_status_for(uint16_t code, uint8_t type);

/**
 * @brief 初始化诊断条件缓存（由 iolink_events_init() 调用）。
 *
 * 注册表是内建 `const` 表，无需运行时绑定；这里只清空 ISR one-shot、
 * PULSE 窗口与边沿计数。
 *
 * @param events 事件上下文（条件权威表）
 */
void iolink_diag_init(iolink_events_ctx_t* events);

/**
 * @brief 清空条件缓存，保留事件生命周期以外的所有时钟/窗口配置。
 *
 * 由 iolink_events_reset() 调用。
 *
 * @param events 事件上下文
 */
void iolink_diag_reset(iolink_events_ctx_t* events);

/**
 * @brief 采样一个电平源。
 *
 * `level=true` 且条件未锁存 → 上升沿（触发一次 appears 并计数）；
 * `level=false` 且条件已锁存 → 下降沿（清除条件，disappears 由事件引擎按
 * A.6.4/10.10.2 的窗口发出）；稳态无动作。
 *
 * @param events 事件上下文
 * @param src    电平来源
 * @param level  当前电平
 * @return 本次采样产生上升沿时返回 true
 */
bool iolink_diag_set_level(iolink_events_ctx_t* events, iolink_diag_src_t src, bool level);

/**
 * @brief 在主上下文报告一个脉冲源事件。
 *
 * 刷新保持窗口并（首次）触发条件。连续脉冲只刷新 `quiet_since_ms`，
 * 不会重复 appears（10.10.2：同一根因只报一次）。
 *
 * @param events 事件上下文
 * @param src    脉冲来源
 */
void iolink_diag_pulse(iolink_events_ctx_t* events, iolink_diag_src_t src);

/**
 * @brief ISR 侧脉冲发布：只写一个 one-shot 字节。
 *
 * 单写者、单字节存储，不做读-改-写；由 iolink_diag_service() 在临界区消费。
 *
 * @param events 事件上下文
 * @param src    脉冲来源
 */
void iolink_diag_pulse_isr(iolink_events_ctx_t* events, iolink_diag_src_t src);

/**
 * @brief 主上下文诊断服务：消费 ISR 脉冲并关闭 PULSE 保持窗口。
 *
 * @param events 事件上下文
 * @param now_ms 当前时间（毫秒，与事件引擎同一时钟）
 */
void iolink_diag_service(iolink_events_ctx_t* events, uint32_t now_ms);

/**
 * @brief 重新武装指定的电平来源。
 *
 * 只清 `present`，不清 `reported`：仍为高电平的来源在下一轮采样产生新的
 * 上升沿；若 disappears 尚未发出，事件引擎按“同一根因只报一次”保持静默。
 * 用于 Restore factory（0x82）等“清除后仍须上报现存故障”的路径。
 *
 * @param events   事件上下文
 * @param src_mask 位 i 对应 @ref iolink_diag_src_t 的 i
 */
void iolink_diag_rearm(iolink_events_ctx_t* events, uint8_t src_mask);

/**
 * @brief Table B.13 DeviceStatus，由条件表派生。
 *
 * 统计 `(present || reported)` 且非 Notification 的条件，取注册表首选状态的
 * 最高优先级（Failure > Functional-Check > Maintenance > Out-of-Spec）。
 * 该过滤条件保证 DDS 仍列出条目时 DeviceStatus 不会提前回 0。
 *
 * @param events 事件上下文
 * @return uint8_t 0..4
 */
uint8_t iolink_diag_device_status(iolink_events_ctx_t* events);

/**
 * @brief Table B.14 DDS 记录快照，由条件表派生。
 *
 * 只输出 `reported` 且非 Notification 的条件（appears 已上线、disappears
 * 未发出）。Qualifier 由共享的 iolink_event_qualifier() 生成。
 *
 * @param events 事件上下文
 * @param out    [out] 记录缓冲区
 * @param max    最多写入的记录数
 * @return uint8_t 实际写入的记录数
 */
uint8_t iolink_diag_dds_snapshot(iolink_events_ctx_t* events, iolink_diag_dds_entry_t* out,
                                 uint8_t max);

/**
 * @brief 应用级条件置位（iolink_event_trigger 的薄封装）。
 *
 * EventType 来自注册表；未登记代码按 iolink_event_classify() 的默认分类。
 * 保留 iolink_event_trigger() 的兼容语义：已置位条件不重复上报，
 * Notification 走单次触发。
 *
 * @param events 事件上下文
 * @param code   16 位 EventCode
 */
void iolink_diag_raise_code(iolink_events_ctx_t* events, uint16_t code);

/**
 * @brief 应用级条件释放（iolink_event_clear 的薄封装）。
 *
 * 只清 `present`；disappears 由事件引擎按 A.6.4/10.10.2 的窗口发出。
 *
 * @param events 事件上下文
 * @param code   16 位 EventCode
 */
void iolink_diag_clear_code(iolink_events_ctx_t* events, uint16_t code);

/**
 * @brief 某来源累计的边沿/脉冲次数（统计投影）。
 *
 * @param events 事件上下文
 * @param src    来源
 * @return uint32_t 计数；越界时为 0
 */
uint32_t iolink_diag_edge_count(const iolink_events_ctx_t* events, iolink_diag_src_t src);

#endif /* IOLINK_FEATURE_EVENTS */

/** @} */ /* end of iolinki_diag */

#endif /* IOLINK_DIAG_H */
