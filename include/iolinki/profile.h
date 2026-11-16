/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PROFILE_H
#define IOLINK_PROFILE_H

#include "iolinki/config.h"
#include "iolinki/types.h"

/**
 * @file profile.h
 * @brief 构建档位的链接期一致性断言。
 *
 * 保留为编译期的布局开关（ISR 粒度、并发、统计、DS、事件）会改变
 * `struct` 布局或公开 API 集合。应用与库必须用同一档位编译，否则行为
 * 未定义。CMake 通过 `PUBLIC` 编译定义传播这些开关，本机制是**开发期
 * 安全网**，不是安全边界：
 *
 *  - 库对每个开关定义一个**名字带取值**的符号
 *    （`iolink_profile_events_on` / `iolink_profile_events_off`）；
 *  - 应用在一个 TU 里写一次 `IOLINK_PROFILE_ANCHOR(名字);`，锚点引用
 *    应用侧期望的全部符号；
 *  - 档位不一致时链接失败，错误信息直接打印缺哪个方向的符号
 *    （例如 `undefined reference to 'iolink_profile_events_off'`）。
 *
 * `__attribute__((used))` 只挡编译器删除；`--gc-sections` 仍可能回收
 * 未引用的 `.rodata`，因此锚点放进独立段 `.iolink_profile_anchor`。
 * 嵌入式链接脚本应写 `KEEP(*(.iolink_profile_anchor))`；host 构建没有
 * `--gc-sections`，`used` 已足够。
 */

/** @brief 锚点所在的独立段名（链接脚本用 `KEEP` 保护）。 */
#define IOLINK_PROFILE_ANCHOR_SECTION ".iolink_profile_anchor"

#if defined(__IAR_SYSTEMS_ICC__)
#define IOLINK_PROFILE_ROOT __root
#define IOLINK_PROFILE_SECTION_ATTR
#elif defined(__GNUC__) || defined(__clang__) || defined(__CC_ARM)
#define IOLINK_PROFILE_ROOT
#define IOLINK_PROFILE_SECTION_ATTR __attribute__((section(IOLINK_PROFILE_ANCHOR_SECTION), used))
#else
#define IOLINK_PROFILE_ROOT
#define IOLINK_PROFILE_SECTION_ATTR
#endif

/** @brief 库侧实际定义的档位符号（名字即诊断信息）。 */
#if IOLINK_FEATURE_EVENTS
#define IOLINK_PROFILE_EVENTS_SYM iolink_profile_events_on
#else
#define IOLINK_PROFILE_EVENTS_SYM iolink_profile_events_off
#endif

#if IOLINK_FEATURE_DATA_STORAGE
#define IOLINK_PROFILE_DS_SYM iolink_profile_data_storage_on
#else
#define IOLINK_PROFILE_DS_SYM iolink_profile_data_storage_off
#endif

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
#define IOLINK_PROFILE_ISR_RX_SYM iolink_profile_isr_rx_frame
#else
#define IOLINK_PROFILE_ISR_RX_SYM iolink_profile_isr_rx_byte
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
#define IOLINK_PROFILE_ISR_TX_SYM iolink_profile_isr_tx_frame
#else
#define IOLINK_PROFILE_ISR_TX_SYM iolink_profile_isr_tx_byte
#endif

#if IOLINK_PHY_FEATURE_TX_BUSY
#define IOLINK_PROFILE_TX_BUSY_SYM iolink_profile_tx_busy_on
#else
#define IOLINK_PROFILE_TX_BUSY_SYM iolink_profile_tx_busy_off
#endif

#if IOLINK_CONCURRENCY == IOLINK_CONCURRENCY_SINGLE
#define IOLINK_PROFILE_CONCURRENCY_SYM iolink_profile_concurrency_single
#elif IOLINK_CONCURRENCY == IOLINK_CONCURRENCY_SMP
#define IOLINK_PROFILE_CONCURRENCY_SYM iolink_profile_concurrency_smp
#else
#define IOLINK_PROFILE_CONCURRENCY_SYM iolink_profile_concurrency_isr
#endif

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
#define IOLINK_PROFILE_STATS_SYM iolink_profile_stats_split
#else
#define IOLINK_PROFILE_STATS_SYM iolink_profile_stats_shared
#endif

/* 工作区归属改变 ISDU/DS 上下文布局（内嵌 scratch 数组 vs 纯指针）。 */
#if IOLINK_WORKSPACE_EMBEDDED
#define IOLINK_PROFILE_WORKSPACE_SYM iolink_profile_workspace_embedded_on
#else
#define IOLINK_PROFILE_WORKSPACE_SYM iolink_profile_workspace_embedded_off
#endif

/* PD-in 槽位数改变 iolink_dll_shared_t 的数组维度（2 或 3）。 */
#if IOLINK_PD_IN_BUFFERS == 3U
#define IOLINK_PROFILE_PD_BUFFERS_SYM iolink_profile_pd_in_buffers_3
#else
#define IOLINK_PROFILE_PD_BUFFERS_SYM iolink_profile_pd_in_buffers_2
#endif

/* 库侧符号声明：只有与当前档位匹配的那个真正存在。应用必须与本文件
 * 用同一组宏编译（CMake 的 PUBLIC 定义保证了这一点）。 */
extern const uint8_t iolink_profile_events_on;
extern const uint8_t iolink_profile_events_off;
extern const uint8_t iolink_profile_data_storage_on;
extern const uint8_t iolink_profile_data_storage_off;
extern const uint8_t iolink_profile_isr_rx_byte;
extern const uint8_t iolink_profile_isr_rx_frame;
extern const uint8_t iolink_profile_isr_tx_byte;
extern const uint8_t iolink_profile_isr_tx_frame;
extern const uint8_t iolink_profile_tx_busy_on;
extern const uint8_t iolink_profile_tx_busy_off;
extern const uint8_t iolink_profile_concurrency_single;
extern const uint8_t iolink_profile_concurrency_isr;
extern const uint8_t iolink_profile_concurrency_smp;
extern const uint8_t iolink_profile_stats_shared;
extern const uint8_t iolink_profile_stats_split;
extern const uint8_t iolink_profile_workspace_embedded_on;
extern const uint8_t iolink_profile_workspace_embedded_off;
extern const uint8_t iolink_profile_pd_in_buffers_2;
extern const uint8_t iolink_profile_pd_in_buffers_3;

/**
 * @brief 在应用侧引用全部档位符号，使档位不匹配变成链接错误。
 *
 * 一个应用只写一次（放在任一 `.c` 文件作用域）：
 * @code
 * IOLINK_PROFILE_ANCHOR(iolink_app_profile);
 * @endcode
 *
 * @param name 锚点对象名（仅用于链接映射/调试）
 *
 * 数组元素的类型有意擦除（`IOLINK_OPAQUE`）：每项指向不同的
 * `iolink_profile_*` 符号，只有链接器需要它们，运行期从不解引用。
 */
#define IOLINK_PROFILE_ANCHOR(name)                            \
    IOLINK_PROFILE_ROOT static const void* IOLINK_OPAQUE const \
        name[] IOLINK_PROFILE_SECTION_ATTR = {                 \
            (const void*) &IOLINK_PROFILE_EVENTS_SYM,          \
            (const void*) &IOLINK_PROFILE_DS_SYM,              \
            (const void*) &IOLINK_PROFILE_ISR_RX_SYM,          \
            (const void*) &IOLINK_PROFILE_ISR_TX_SYM,          \
            (const void*) &IOLINK_PROFILE_TX_BUSY_SYM,         \
            (const void*) &IOLINK_PROFILE_CONCURRENCY_SYM,     \
            (const void*) &IOLINK_PROFILE_STATS_SYM,           \
            (const void*) &IOLINK_PROFILE_WORKSPACE_SYM,       \
            (const void*) &IOLINK_PROFILE_PD_BUFFERS_SYM,      \
    }

#endif /* IOLINK_PROFILE_H */
