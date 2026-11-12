/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PORT_H
#define IOLINK_PORT_H

#include <stdint.h>
#include <stddef.h>

#include "iolinki/config.h"

/**
 * @file port.h
 * @brief 协议栈与端口之间关于并发原语的声明式约定。
 *
 * 协议栈只需要三类异步原语。
 * 每一类都按强度分级，因此端口只实现所选并发配置
 * 所要求的那一级，不多实现：
 *
 * - **原子宽度**（`IOLINK_ATOMIC_UINT_T`，默认 `uint32_t`）：自然
 *   对齐的值在一次访问中完成读写（单拷贝原子）。
 *   不假定存在读-改-写。
 * - **屏障**（`IOLINK_FENCE()`）：把数据写入排在发布字存储之前，
 *   并把发布字加载排在它所发布的数据之前。
 * - **互斥**（`IOLINK_CRITICAL_ENTER/EXIT()`）：排除数据中断，
 *   可嵌套，可从 ISR 调用，持续时间有界。
 *
 * 协议栈只有单一主上下文调用者，因此约定中不存在线程锁：
 * 上述 CPU 级临界区是库所获取的唯一互斥手段。端口若让
 * 临界区同时串行化任务（例如 RTOS 调度器锁），
 * 也不改变这一点：
 * API 仍必须从同一个任务进入。
 *
 * 并发配置（`IOLINK_CONCURRENCY`）选择需要哪些原语；
 * 库据此推导出 `IOLINK_NEED_*`，缺少某个原语
 * 会导致编译错误（对临界区而言，则是在设备初始化时返回
 * `IOLINK_DEVICE_INIT_ERR_CRITICAL`）。不需要临界区的配置
 * 完全不会引用临界区符号，
 * 因此裸机端口可以省略它们。
 *
 * 协议栈从不对共享状态使用读-改-写原子操作（比较交换、
 * 原子自增），因为若干受支持内核（ARMv6-M、
 * ARMv8-M Baseline、RV32I）没有原子 RMW 指令。共享状态
 * 改用单写者加发布字的方式。
 *
 * 完整约定与各平台映射参见 docs/PORTING.md。
 */

/**
 * @defgroup iolinki_port 并发原语
 * @brief 端口按需提供的分级异步原语。
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name 互斥
 *
 * 仅当 `IOLINK_NEED_CRITICAL` 非零时需要。实现
 * 必须可嵌套，并可从中断上下文调用；它应映射为
 * PRIMASK/BASEPRI 保存/恢复（Cortex-M）、IRQ 锁（RTOS）或等效手段。
 * @{
 */
void iolink_critical_enter(void);
void iolink_critical_exit(void);

/**
 * @brief 探测平台是否提供了真正的临界区钩子。
 *
 * 调用一次 iolink_critical_enter()/iolink_critical_exit()，并报告是否
 * 使用了 src/platform.c 中的弱定义空实现默认值。iolink_device_init()
 * 仅在 `IOLINK_NEED_CRITICAL` 非零时查询此函数。
 *
 * @return 两个钩子都被覆盖时为 0，运行了弱定义默认实现时为 -1。
 */
int iolink_platform_critical_sections_ready(void);
/** @} */

#ifdef __cplusplus
}
#endif

/* -------------------------------------------------------------------------
 * 能力声明（端口在包含本文件之前，通过构建系统或
 * IOLINK_PORT_CONFIG_FILE 覆盖这些定义）
 * ------------------------------------------------------------------------- */

/** @brief 可选的端口配置头文件，在默认值之前包含。 */
#ifdef IOLINK_PORT_CONFIG_FILE
/* cppcheck 2.7 无法求值宏形式的 include，即使宏未定义也会报错；
   端口定义该宏时，此 include 是合法的。 */
// cppcheck-suppress preprocessorErrorDirective
#include IOLINK_PORT_CONFIG_FILE
#endif

/**
 * @brief 自然原子的无符号整数类型（对齐时为单拷贝原子）。
 *
 * 默认为 `uint32_t`。8/16 位目标用最宽的自然原子
 * 无符号类型覆盖它。ISR 并发配置至少要求
 * 32 位；更窄的目标应保持 `SINGLE` 配置。
 */
#ifndef IOLINK_ATOMIC_UINT_T
#define IOLINK_ATOMIC_UINT_T uint32_t
#endif

/** @brief 屏障等级：仅编译器、设备/DMA 可见、SMP/系统范围。 */
#define IOLINK_FENCE_COMPILER 0
#define IOLINK_FENCE_DEVICE 1
#define IOLINK_FENCE_SMP 2

/**
 * @brief 端口声明的屏障等级。
 *
 * `COMPILER` 是可移植默认值：当两个上下文是同一内核上的
 * 主循环与 ISR，且没有其他总线主站读取已发布数据时，它已足够。
 * 端口的 PHY/DMA 若在 CPU 写入的同时读取已发布的缓冲区，
 * 则声明 `DEVICE` 并提供 `IOLINK_FENCE_DEVICE_IMPL()`。
 * 上下文为两个内核（或两个线程）的端口声明 `SMP`，
 * 并提供 `IOLINK_FENCE_SMP_IMPL()`。
 */
#ifndef IOLINK_FENCE_LEVEL
#define IOLINK_FENCE_LEVEL IOLINK_FENCE_COMPILER
#endif

/* -------------------------------------------------------------------------
 * 需求推导与校验
 * ------------------------------------------------------------------------- */

/* IOLINK_CONCURRENCY_SINGLE/_ISR/_SMP 的取值来自 config.h。 */
#if (IOLINK_CONCURRENCY < IOLINK_CONCURRENCY_SINGLE) || \
    (IOLINK_CONCURRENCY > IOLINK_CONCURRENCY_SMP)
#error "IOLINK_CONCURRENCY must be IOLINK_CONCURRENCY_SINGLE/ISR/SMP (the RTOS tier was removed)"
#endif

#if (IOLINK_STATS_MODE != IOLINK_STATS_SHARED) && (IOLINK_STATS_MODE != IOLINK_STATS_SPLIT)
#error "IOLINK_STATS_MODE must be IOLINK_STATS_SHARED or IOLINK_STATS_SPLIT"
#endif

/** @brief 上下文之间需要屏障时非零。 */
#define IOLINK_NEED_FENCE (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR)

/**
 * @brief 协议栈调用平台临界区时非零。
 *
 * 批量交接是无锁的（帧邮箱与暂存写环形缓冲区、
 * ISDU 应答发布字、PD-in 缓冲区租约以及应答暂存
 * 槽位）。平台临界区仍然保护事件引擎、
 * PD-in 索引交换以及待处理长度对，因此每个异步配置
 * 都需要它。唯一的其他调用者是 `dll_send()` 中的
 * 遗留直发回退路径：没有 `request_service()` 的 PHY
 * （由 `IOLINK_PHY_FEATURE_ISR_TX_ONLY` 编译裁除）在主上下文中、
 * 于该临界区内驱动。应答本身的暂存不需要临界区：槽位归
 * 主上下文所有，并最后发布。只有 `SINGLE` 在没有临界区的
 * 情况下运行。状态迁移不需要临界区：状态是一次对齐
 * 存储，其应用通知是基于电平的。
 *
 * `IOLINK_STATS_MODE=SPLIT` 消除了最后一处计数器竞争（两个
 * 上下文递增同一字段），但不改变上述要求。
 */
#define IOLINK_NEED_CRITICAL (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR)

#if (IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_SMP) && (IOLINK_FENCE_LEVEL < IOLINK_FENCE_SMP)
#error "IOLINK_CONCURRENCY=SMP requires IOLINK_FENCE_LEVEL=IOLINK_FENCE_SMP"
#endif

#if (IOLINK_FENCE_LEVEL < IOLINK_FENCE_COMPILER) || (IOLINK_FENCE_LEVEL > IOLINK_FENCE_SMP)
#error "IOLINK_FENCE_LEVEL must be COMPILER, DEVICE or SMP"
#endif

/* -------------------------------------------------------------------------
 * 屏障
 * ------------------------------------------------------------------------- */

/** @brief 阻止编译器跨越此点移动内存操作。 */
#if defined(__CC_ARM)
/* ARM Compiler 5 在 --gnu 模式下定义 __GNUC__，却没有 GCC 风格的内联
 * asm。__schedule_barrier() 阻止重排，__memory_changed()
 * 强制重新加载缓存的内存；两者合起来等价于 GNU 形式中
 * "memory" clobber 的效果。 */
#define IOLINK_COMPILER_BARRIER() \
    do {                          \
        __schedule_barrier();     \
        __memory_changed();       \
    } while (0)
#elif defined(__GNUC__) || defined(__clang__)
#define IOLINK_COMPILER_BARRIER() __asm__ __volatile__("" ::: "memory")
#else
#define IOLINK_COMPILER_BARRIER() ((void) 0)
#endif

#if IOLINK_NEED_FENCE
#if (IOLINK_FENCE_LEVEL == IOLINK_FENCE_COMPILER)
#define IOLINK_FENCE() IOLINK_COMPILER_BARRIER()
#elif (IOLINK_FENCE_LEVEL == IOLINK_FENCE_DEVICE)
#ifndef IOLINK_FENCE_DEVICE_IMPL
#error "IOLINK_FENCE_LEVEL=DEVICE requires the port to define IOLINK_FENCE_DEVICE_IMPL()"
#endif
#define IOLINK_FENCE() IOLINK_FENCE_DEVICE_IMPL()
#else
#ifndef IOLINK_FENCE_SMP_IMPL
#error "IOLINK_FENCE_LEVEL=SMP requires the port to define IOLINK_FENCE_SMP_IMPL()"
#endif
#define IOLINK_FENCE() IOLINK_FENCE_SMP_IMPL()
#endif
#else
#define IOLINK_FENCE() ((void) 0)
#endif

/* -------------------------------------------------------------------------
 * 发布字
 * ------------------------------------------------------------------------- */

/**
 * @brief 以获取语义读取发布字。
 *
 * 该字必须自然对齐（当结构体把它放在自身大小的整数倍处时，
 * `IOLINK_ATOMIC_UINT_T` 成员即满足）。每个上下文都是它
 * 所发布字的唯一写者；读者从不写入，因此
 * 一次普通加载加上已配置的屏障就足够。
 */
static inline IOLINK_ATOMIC_UINT_T iolink_atomic_load(const volatile IOLINK_ATOMIC_UINT_T* word)
{
    const IOLINK_ATOMIC_UINT_T value = *word;
    IOLINK_FENCE();
    return value;
}

/**
 * @brief 以释放语义写入发布字。
 *
 * 该字所指的全部数据必须在此调用之前写完；
 * 屏障阻止编译器把这些写入下沉到发布之后。
 */
static inline void iolink_atomic_store(volatile IOLINK_ATOMIC_UINT_T* word,
                                       IOLINK_ATOMIC_UINT_T value)
{
    IOLINK_FENCE();
    *word = value;
}

/**
 * @brief 与数据中断串行化（无需临界区时为空操作）。
 *
 * 配置需要时展开为平台钩子，否则展开为空，
 * 这样无锁或单上下文的配置完全不会
 * 引用这些符号。
 */
#if IOLINK_NEED_CRITICAL
#define IOLINK_CRITICAL_ENTER() iolink_critical_enter()
#define IOLINK_CRITICAL_EXIT() iolink_critical_exit()
#else
#define IOLINK_CRITICAL_ENTER() ((void) 0)
#define IOLINK_CRITICAL_EXIT() ((void) 0)
#endif

/**
 * @brief 同样适用于 C99 模式的编译期断言。
 *
 * C99 下 `-Wpedantic` 会拒绝 C11 的 `_Static_assert`，而交付
 * 矩阵使用的正是 C99，因此回退为数组大小惯用法。
 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define IOLINK_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#else
#define IOLINK_STATIC_ASSERT_JOIN_(a, b) a##b
#define IOLINK_STATIC_ASSERT_JOIN(a, b) IOLINK_STATIC_ASSERT_JOIN_(a, b)
#define IOLINK_STATIC_ASSERT(cond, msg) \
    typedef char IOLINK_STATIC_ASSERT_JOIN(iolink_static_assert_, __LINE__)[(cond) ? 1 : -1]
#endif

#if IOLINK_CONCURRENCY >= IOLINK_CONCURRENCY_ISR
IOLINK_STATIC_ASSERT(sizeof(IOLINK_ATOMIC_UINT_T) >= 4,
                     "the ISR concurrency profile needs a 32-bit naturally atomic type");
#endif

/** @} */ /* end of iolinki_port */

#endif  // IOLINK_PORT_H
