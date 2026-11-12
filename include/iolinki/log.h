/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_LOG_H
#define IOLINK_LOG_H

#include <stddef.h>
#include <stdint.h>

/**
 * @file log.h
 * @brief 协议栈日志打印点与运行时 sink。
 *
 * 库在设备、DLL、ISDU、数据存储和事件路径上提供打印点，但不实现
 * 输出：每条日志都经过上下文中的一个 ::iolink_log_sink_t。sink 的
 * 回调由应用提供，负责格式化（`vsnprintf`）、级别过滤后的输出以及
 * 时间戳等策略；库只做运行时级别比较和参数转发，不引入 stdio。
 *
 * sink 默认全零（静默）：`write == NULL` 或 `level == SILENT` 时
 * 不产生任何调用。应用通过 ::iolink_device_config_t::log_write /
 * log_level 在初始化时配置，或在运行时用
 * iolink_device_set_log() 更换。
 */

/**
 * @defgroup iolinki_log 日志
 * @brief 运行时可配置的日志打印点。
 * @{
 */

/** @brief 日志级别；数值越大越详细。 */
typedef enum
{
    IOLINK_LOG_LEVEL_SILENT = 0, /**< 关闭输出 */
    IOLINK_LOG_LEVEL_ERROR = 1,  /**< 不可恢复或链路级故障 */
    IOLINK_LOG_LEVEL_WARN = 2,   /**< 可疑但可恢复的状态 */
    IOLINK_LOG_LEVEL_INFO = 3,   /**< 状态迁移与生命周期事件 */
    IOLINK_LOG_LEVEL_DEBUG = 4   /**< 协议细节（索引、长度、计数器） */
} iolink_log_level_t;

/**
 * @brief 日志输出回调。
 *
 * @param user  ::iolink_device_config_t::user（或直接 DLL 用户的
 *               ::iolink_log_sink_t::user）。
 * @param level 该条日志的级别，已通过 sink 的运行时阈值。
 * @param format printf 风格格式串；库保证它与后续参数匹配。
 *
 * 回调应自行格式化并输出。它在主服务上下文中被调用，必须非阻塞；
 * 启用 IOLINK_LOG_ENABLE_ISR 后它也会从数据 ISR 调用，此时必须
 * 满足 ISR 安全要求。
 */
typedef void (*iolink_log_write_fn)(void* user, iolink_log_level_t level, const char* format, ...);

/**
 * @brief 每个上下文携带的日志汇点。
 *
 * 全零表示静默。`level` 是运行时阈值：只转发级别数值不大于它的
 * 日志。结构体按值嵌入 DLL/ISDU/DS/事件上下文，由设备层在初始化
 * 和 iolink_device_set_log() 时统一写入。
 */
typedef struct
{
    iolink_log_write_fn write; /**< 输出回调；NULL 表示静默 */
    void* user;                /**< 传给 @ref write 的不透明指针 */
    iolink_log_level_t level;  /**< 运行时阈值（0 = 静默） */
} iolink_log_sink_t;

/**
 * @brief 初始化一个日志汇点。
 *
 * 供直接使用 DLL/ISDU 等子上下文（不经由 iolink_device_init()）的
 * 集成代码使用；设备层会自动完成同样的绑定。
 */
static inline void iolink_log_sink_init(iolink_log_sink_t* sink, iolink_log_write_fn write,
                                        void* user, iolink_log_level_t level)
{
    if (sink == NULL) {
        return;
    }
    sink->write = write;
    sink->user = user;
    sink->level = level;
}

/**
 * @brief 低级转发宏：运行时检查 sink 与阈值后调用回调。
 *
 * 传参表达式只会在实际输出时求值；`sink` 可以为 NULL。
 */
#define IOLINK_LOG_WRITE(sink, lvl, ...)                                         \
    do {                                                                         \
        const iolink_log_sink_t* iolink_log_sink_ = (sink);                      \
        if ((iolink_log_sink_ != NULL) && (iolink_log_sink_->write != NULL) &&   \
            ((lvl) <= iolink_log_sink_->level)) {                                \
            iolink_log_sink_->write(iolink_log_sink_->user, (lvl), __VA_ARGS__); \
        }                                                                        \
    } while (0)

/** @brief 输出 ERROR 级日志。 */
#define IOLINK_LOG_ERROR(sink, ...) IOLINK_LOG_WRITE((sink), IOLINK_LOG_LEVEL_ERROR, __VA_ARGS__)
/** @brief 输出 WARN 级日志。 */
#define IOLINK_LOG_WARN(sink, ...) IOLINK_LOG_WRITE((sink), IOLINK_LOG_LEVEL_WARN, __VA_ARGS__)
/** @brief 输出 INFO 级日志。 */
#define IOLINK_LOG_INFO(sink, ...) IOLINK_LOG_WRITE((sink), IOLINK_LOG_LEVEL_INFO, __VA_ARGS__)
/** @brief 输出 DEBUG 级日志。 */
#define IOLINK_LOG_DEBUG(sink, ...) IOLINK_LOG_WRITE((sink), IOLINK_LOG_LEVEL_DEBUG, __VA_ARGS__)

/**
 * @brief 启用数据 ISR 路径上的日志（默认 0）。
 *
 * ISR 打印点使用独立的 IOLINK_LOG_ISR_* 系列：即使主宏已配置，它们
 * 也只在置 1 时转发。这样可以在不改变主服务日志的前提下单独评估
 * ISR 日志的时序影响。
 */
#ifndef IOLINK_LOG_ENABLE_ISR
#define IOLINK_LOG_ENABLE_ISR 0
#endif

#if IOLINK_LOG_ENABLE_ISR
#define IOLINK_LOG_ISR_ERROR(sink, ...) IOLINK_LOG_ERROR((sink), __VA_ARGS__)
#define IOLINK_LOG_ISR_WARN(sink, ...) IOLINK_LOG_WARN((sink), __VA_ARGS__)
#define IOLINK_LOG_ISR_INFO(sink, ...) IOLINK_LOG_INFO((sink), __VA_ARGS__)
#define IOLINK_LOG_ISR_DEBUG(sink, ...) IOLINK_LOG_DEBUG((sink), __VA_ARGS__)
#else
#define IOLINK_LOG_ISR_ERROR(sink, ...) ((void) 0)
#define IOLINK_LOG_ISR_WARN(sink, ...) ((void) 0)
#define IOLINK_LOG_ISR_INFO(sink, ...) ((void) 0)
#define IOLINK_LOG_ISR_DEBUG(sink, ...) ((void) 0)
#endif

/** @} */ /* iolinki_log 结束 */

#endif /* IOLINK_LOG_H */
