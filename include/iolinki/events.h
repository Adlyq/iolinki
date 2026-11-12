/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_EVENTS_H
#define IOLINK_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/config.h"
#include "iolinki/port.h"

IOLINK_STATIC_ASSERT(IOLINK_EVENT_QUEUE_SIZE > 0U, "IOLINK_EVENT_QUEUE_SIZE must be > 0");
IOLINK_STATIC_ASSERT(IOLINK_EVENT_PENDING_MAX > 0U, "IOLINK_EVENT_PENDING_MAX must be > 0");

/**
 * @file events.h
 * @brief IO-Link 事件处理
 */

/**
 * @defgroup iolinki_events 事件处理
 * @brief 诊断事件代码、严重级别以及设备事件队列。
 * @{
 */

/**
 * @brief IO-Link 事件严重级别
 */
typedef enum
{
    IOLINK_EVENT_TYPE_NOTIFICATION = 0U, /**< 仅信息，无需操作 */
    IOLINK_EVENT_TYPE_WARNING = 1U,      /**< 潜在问题，操作继续 */
    IOLINK_EVENT_TYPE_ERROR = 2U         /**< 严重故障，运行可能降级 */
} iolink_event_type_t;

/**
 * @brief 事件 MODE（A.6.4 / Figure A.24）
 *
 * Error 和 Warning 事件使用出现/消失（appears/disappears）配对；Notification
 * 事件只上报一次（单次触发）。
 */
typedef enum
{
    IOLINK_EVENT_MODE_APPEARS = 0U,    /**< 事件出现（0b11）。 */
    IOLINK_EVENT_MODE_DISAPPEARS = 1U, /**< 事件消失（0b10）。 */
    IOLINK_EVENT_MODE_SINGLE_SHOT = 2U /**< Notification，仅上报一次。 */
} iolink_event_mode_t;

/*
 * 设备厂商特定的通信事件。
 *
 * IO-Link V1.1.5 规范的 Table D.1 将 0x1800-0x18FF 保留给厂商特定的设备
 * 事件。这些数值与 Table D.2 中的主站/端口代码在数值上重合（例如
 * 0x1805 = "Overtemperature"，0x1806 = 端口上的 "Short circuit at L+"）；
 * EventQualifier 的 SOURCE 位（Table A.18：0 = Device）用于区分这两种
 * 用途。标准化的设备状态具有专用的 Table D.1 代码。
 * 对这些情况请使用下面的 IOLINK_EVENTCODE_* 块，
 * 绝不要使用这些厂商特定的取值。
 */
#define IOLINK_EVENT_COMM_CRC 0x1801U     /**< 通信中的 CRC 错误 */
#define IOLINK_EVENT_COMM_TIMEOUT 0x1802U /**< 通信超时 */
#define IOLINK_EVENT_COMM_FRAMING 0x1803U /**< 通信中的组帧错误 */
#define IOLINK_EVENT_COMM_TIMING 0x1804U  /**< 通信中的时序违规 */

/*
 * 设备的标准 IO-Link EventCode，依据 V1.1.5 Annex D 的 Table D.1。
 * 行尾注释给出规范定义的 EventType。厂商特定的设备事件使用
 * 0x1800-0x18FF 和 0x8CA0-0x8DFF 范围。
 */
#define IOLINK_EVENTCODE_NO_MALFUNCTION 0x0000U             /**< Notification */
#define IOLINK_EVENTCODE_GENERAL_MALFUNCTION 0x1000U        /**< Error */
#define IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD 0x4000U       /**< Error */
#define IOLINK_EVENTCODE_TEMPERATURE_OVERRUN 0x4210U        /**< Warning */
#define IOLINK_EVENTCODE_TEMPERATURE_UNDERRUN 0x4220U       /**< Warning */
#define IOLINK_EVENTCODE_HARDWARE_FAULT 0x5000U             /**< Error */
#define IOLINK_EVENTCODE_COMPONENT_MALFUNCTION 0x5010U      /**< Error */
#define IOLINK_EVENTCODE_NVM_LOSS 0x5011U                   /**< Error */
#define IOLINK_EVENTCODE_BATTERY_LOW 0x5012U                /**< Warning */
#define IOLINK_EVENTCODE_POWER_SUPPLY_FAULT 0x5100U         /**< Error */
#define IOLINK_EVENTCODE_FUSE_BLOWN 0x5101U                 /**< Error */
#define IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN 0x5110U     /**< Warning */
#define IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN 0x5111U    /**< Warning */
#define IOLINK_EVENTCODE_SOFTWARE_FAULT 0x6000U             /**< Error */
#define IOLINK_EVENTCODE_PARAMETER_ERROR 0x6320U            /**< Error */
#define IOLINK_EVENTCODE_PARAMETER_MISSING 0x6321U          /**< Error */
#define IOLINK_EVENTCODE_WIRE_BREAK 0x7700U                 /**< Error */
#define IOLINK_EVENTCODE_SHORT_CIRCUIT 0x7710U              /**< Error */
#define IOLINK_EVENTCODE_GROUND_FAULT 0x7711U               /**< Error */
#define IOLINK_EVENTCODE_APPLICATION_FAULT 0x8C00U          /**< Error */
#define IOLINK_EVENTCODE_SIMULATION_ACTIVE 0x8C01U          /**< Warning */
#define IOLINK_EVENTCODE_PV_RANGE_OVERRUN 0x8C10U           /**< Warning */
#define IOLINK_EVENTCODE_MEASUREMENT_RANGE_EXCEEDED 0x8C20U /**< Error */
#define IOLINK_EVENTCODE_PV_RANGE_UNDERRUN 0x8C30U          /**< Warning */
#define IOLINK_EVENTCODE_MAINTENANCE_CLEANING 0x8C40U       /**< Warning (维护) */
#define IOLINK_EVENTCODE_MAINTENANCE_REFILL 0x8C41U         /**< Warning (维护) */
#define IOLINK_EVENTCODE_MAINTENANCE_WEAR 0x8C42U           /**< Warning (维护) */
#define IOLINK_EVENTCODE_DS_UPLOAD_REQUEST 0xFF91U          /**< Notification (内部 DS) */

/**
 * @brief 事件描述符
 *
 * 表示单个 IO-Link 诊断事件。
 */
typedef struct
{
    uint16_t code;            /**< 16 位 IO-Link EventCode（按规范或设备特定） */
    iolink_event_type_t type; /**< 严重级别 */
    iolink_event_mode_t mode; /**< 出现 / 消失 / 单次触发 */
} iolink_event_t;

/**
 * @brief 单个活动 Error/Warning EventCode 的生命周期跟踪。
 *
 * 实现 A.6.4 的出现/消失配对：一个代码在报告其消失之前只能出现
 * 一次，并且在出现后的 IOLINK_EVENT_MIN_APPEAR_MS 之内不会
 * 发出消失。
 */
typedef struct
{
    uint16_t code;               /**< EventCode */
    iolink_event_type_t type;    /**< 严重级别 */
    uint32_t last_appears_ms;    /**< 最近一次 “event appears” 入队的时间 */
    uint32_t last_disappears_ms; /**< 最近一次 “event disappears” 入队的时间 */
    bool appears_valid;          /**< 该槽位的 last_appears_ms 已设置 */
    bool disappears_valid;       /**< 该槽位的 last_disappears_ms 已设置 */
    bool present;                /**< 事件条件当前处于置位状态 */
    bool reported;               /**< 当前周期已排队 “appears” */
    bool active;                 /**< 槽位已分配（活动事件或重复冷却中） */
} iolink_event_pending_t;

/**
 * @brief 事件引擎上下文
 *
 * 管理待处理事件的内部队列（按严重级别排序），以及诊断通信
 * 通道上暴露的 Table 58 事件存储器。
 */
typedef struct
{
    iolink_event_t queue[IOLINK_EVENT_QUEUE_SIZE]; /**< 按严重级别排序的事件缓冲区 */
    uint8_t count;                                 /**< 队列中当前的事件数量 */

    iolink_event_pending_t pending[IOLINK_EVENT_PENDING_MAX]; /**< 活动的 appears 事件 */
    uint8_t pending_count;                                    /**< 已使用的 pending 槽位 */
    uint32_t dropped_count; /**< 因队列或 pending[] 已满而丢失的事件 */

    /* Table 58 事件存储器：StatusCode(1) + 6 个槽位 * (Qualifier, CodeMSB, CodeLSB)。 */
    uint8_t memory[19];              /**< 事件存储器映像（0x00-0x12） */
    bool frozen;                     /**< Event 标志置位期间存储器被冻结（Table 60） */
    bool flag;                       /**< Event 标志（CKS 第 7 位） */
    uint8_t frozen_slots;            /**< 存储器被冻结时已填充的存储槽位数量。 */
    iolink_event_t frozen_events[6]; /**< 冻结存储器中实际呈现的事件 */
    uint8_t frozen_count;            /**< frozen_events 中有效的条目数 */

    /* 由设备层绑定的应用时钟（config::time_us）。独立使用事件引擎的
     * 用户必须在 iolink_events_init() 之后设置它；缺失的时钟会被
     * 读取为时间 0。 */
    uint64_t (*time_us)(void* user);
    void* time_user;
    iolink_log_sink_t log; /**< 日志汇点（默认静默；设备层绑定） */
} iolink_events_ctx_t;

/**
 * @brief 初始化事件引擎
 *
 * 重置队列和内部计数器。
 *
 * @param ctx 要初始化的事件上下文
 * @note 独立用户必须在此调用之后绑定 @c ctx->time_us / @c ctx->time_user；
 *       iolink_device_init() 会从设备配置中绑定它们。
 */
#if IOLINK_FEATURE_EVENTS
void iolink_events_init(iolink_events_ctx_t* ctx);

/**
 * @brief 触发一个新的诊断事件
 *
 * Error 和 Warning 事件作为 “event appears” 入队一次，并保持活动
 * 状态，直到调用 iolink_event_clear()；Notification 作为单次触发
 * 入队。对已置位的 EventCode 重复触发会被忽略（10.10.2：一个根因
 * 对应一个 Error 或 Warning）。若一次触发在上一次同一代码的
 * “appears” 之后不足 IOLINK_EVENT_MIN_REPEAT_MS 到达，则不会被
 * 立即入队：iolink_events_process() 会在间隔时间满足后尽快将其
 * 入队。
 *
 * 内部队列可容纳 IOLINK_EVENT_QUEUE_SIZE 个条目。队列满时，严重级别
 * 最低的条目会被覆盖；只有严重级别低于所有已入队条目的那些事件才会
 * 被丢弃。当 IOLINK_EVENT_PENDING_MAX 个生命周期槽位全部
 * 在使用中时，新的 Error/Warning 会被丢弃。两条丢失路径都会递增
 * iolink_events_get_dropped_count()。
 *
 * @param ctx 事件上下文
 * @param code 16 位 IO-Link EventCode
 * @param type 严重级别
 */
void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type);

/**
 * @brief 报告此前触发的 Error/Warning 事件已消失。
 *
 * A.6.4：清除当前未置位的代码会被忽略，并且 “event disappears” 会延迟
 * 到自其出现起经过 IOLINK_EVENT_MIN_APPEAR_MS、且自同一代码上一次
 * “event disappears” 起经过 IOLINK_EVENT_MIN_REPEAT_MS 之后才发出
 * （10.10.2）。延迟的消失由 iolink_events_process() 入队。如果出现
 * 尚未入队（代码在 IOLINK_EVENT_MIN_REPEAT_MS 之内被重新触发），
 * 则不会入队任何内容：主站从未看到该事件，
 * 因此无需撤销。
 *
 * @param ctx 事件上下文
 * @param code 已消失的 EventCode
 */
void iolink_event_clear(iolink_events_ctx_t* ctx, uint16_t code);

/**
 * @brief 处理延迟的 A.6.4 生命周期状态转换。
 *
 * 发出延迟的 “event appears” 和 “event disappears”（各自遵守
 * IOLINK_EVENT_MIN_REPEAT_MS 间隔；消失还需遵守
 * IOLINK_EVENT_MIN_APPEAR_MS），并在重复冷却时间结束后释放
 * 生命周期槽位。iolink_device_process() 在每轮循环中、ISDU 传输
 * 空闲时调用它；仅当直接驱动事件上下文时才需自行调用（例如
 * 单元测试）。
 *
 * @param ctx 事件上下文
 */
void iolink_events_process(iolink_events_ctx_t* ctx);

/**
 * @brief 撤销所有活动的 Error/Warning（Table 101 的 “Clear”）。
 *
 * 实现恢复出厂设置的诊断部分：每个已置位的 Error/Warning 都被
 * 释放并以 MODE “event disappears” 上报，以便主站清除其诊断，
 * 同时丢弃已入队的 Notification。消失会立即入队
 * （该重置是有意为之），并且由于条件不再置位，
 * DeviceStatus 会被清除。
 * 调用方仍需重置 ErrorCount 计数器。
 *
 * @param ctx 事件上下文
 */
void iolink_events_clear_diagnosis(iolink_events_ctx_t* ctx);

/**
 * @brief 因队列或 pending[] 已满而丢失的事件数量。
 *
 * 生命周期表可容纳 IOLINK_EVENT_PENDING_MAX 个同时活动的事件，
 * 队列可容纳 IOLINK_EVENT_QUEUE_SIZE 个条目。
 * 上报的事件数超过该容量的设备会丢失多余部分；该计数器使
 * 这种丢失可被观测，并让应用能够据此确定表的容量，
 * 或自行发出诊断。
 *
 * @param ctx 事件上下文
 * @return uint32_t 自初始化/复位以来丢弃的事件数量
 */
uint32_t iolink_events_get_dropped_count(const iolink_events_ctx_t* ctx);

/**
 * @brief 丢弃所有已入队事件并重置生命周期跟踪。
 *
 * A.6.4：当通信中断或被取消时，队列中的事件会被丢弃。
 * 同时清除事件存储器和标志。
 *
 * @param ctx 事件上下文
 */
void iolink_events_reset(iolink_events_ctx_t* ctx);

/**
 * @brief 检查是否有待主站读取的事件
 *
 * @param ctx 事件上下文
 * @return 若队列中有一个或多个事件则为 true
 */
bool iolink_events_pending(const iolink_events_ctx_t* ctx);

/**
 * @brief 读取 CKS 第 7 位中发送的 Event 标志（7.3.8.2）。
 *
 * 当事件被写入事件存储器后该标志置位，并由主站对 StatusCode
 * （事件存储器地址 0）的写入清除。
 *
 * @param ctx 事件上下文
 * @return Event 标志置位期间为 true
 */
bool iolink_events_flag(const iolink_events_ctx_t* ctx);

/**
 * @brief 读取 Table 58 事件存储器（诊断通道）的一个字节。
 *
 * 地址 0 是 StatusCode type 2（Figure A.22）：第 7 位 Event Details = 1，
 * 第 0-5 位每个活动槽位 1..6 对应一位。槽位 n 占用地址 3n-2
 * （EventQualifier）、3n-1（EventCode MSB）和 3n（EventCode LSB）。
 *
 * @param ctx 事件上下文
 * @param addr 事件存储器地址（0x00-0x12）
 * @return uint8_t 存储器字节，地址越界时为 0
 */
uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr);

/**
 * @brief 写入 Table 58 事件存储器（诊断通道）的一个字节。
 *
 * 对地址 0（StatusCode）的写入用于确认事件读取：
 * Event 标志被清除，存储器被释放/标记为无效，
 * 并根据仍留在队列中的任何事件重建（Table 60 T5）。
 * 对其他地址的写入会被忽略。
 *
 * @param ctx 事件上下文
 * @param addr 事件存储器地址
 * @param value 主站写入的值（被忽略，7.3.8.2）
 */
void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value);

/**
 * @brief 从队列中弹出最旧的事件
 *
 * 通常由 ISDU 引擎在响应 Index 2 的读请求时调用。
 *
 * @param ctx 事件上下文
 * @param event [out] 用于存放所弹出事件详情的指针
 * @return 成功弹出事件时为 true，队列为空时为 false
 */
bool iolink_events_pop(iolink_events_ctx_t* ctx, iolink_event_t* event);

/**
 * @brief 查看最旧的事件而不将其从队列中移除
 *
 * 用于 OD 事件内容，以检查下一个事件代码。
 *
 * @param ctx 事件上下文
 * @param event [out] 用于存放事件详情的指针
 * @return 有可用事件时为 true，队列为空时为 false
 */
bool iolink_events_peek(const iolink_events_ctx_t* ctx, iolink_event_t* event);

/**
 * @brief 获取事件队列中当前的最高严重级别
 *
 * 按 Table B.13 映射到 IO-Link Device Status（0=OK，1=Maintenance，
 * 2=Out of Spec，3=Functional-Check，4=Failure）。最低的优先级数值
 * 占优：Failure (4) 高于 Functional-Check (3)，后者高于 Maintenance
 * (1) 和 Out of Spec (2)。
 *
 * @param ctx 事件上下文
 * @return uint8_t 最高严重级别（0-4）
 */
uint8_t iolink_events_get_highest_severity(iolink_events_ctx_t* ctx);

/**
 * @brief 将所有待处理事件复制到缓冲区而不弹出它们
 *
 * @param ctx 事件上下文
 * @param out_events [out] 用于存放事件副本的缓冲区
 * @param max_count 最多复制的事件数量
 * @return uint8_t 已复制的事件数量
 */
uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count);

/**
 * @brief 返回 V1.1.5 规范为某个标准 EventCode 定义的 EventType。
 *
 * 对于标准化代码（Annex D，Table D.1），返回规范定义的
 * notification/warning/error 分类。对于保留或未知代码，
 * 返回 IOLINK_EVENT_TYPE_ERROR 作为安全默认值。
 *
 * @param code 16 位 EventCode
 * @return iolink_event_type_t 规范定义的 EventType
 */
iolink_event_type_t iolink_event_classify(uint16_t code);
#endif /* IOLINK_FEATURE_EVENTS */

/** @} */ /* end of iolinki_events */

#endif  // IOLINK_EVENTS_H
