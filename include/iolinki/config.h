/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CONFIG_H
#define IOLINK_CONFIG_H

#include "iolinki/log.h"

/**
 * @file config.h
 * @brief iolinki 协议栈的编译期默认值。
 *
 * 本文件是**默认值的唯一权威出处**；原理与取舍见 docs/feature-trimming.md
 * 和 docs/PORTING.md，本文只列默认值与相互关系。
 *
 * 三类宏，按标记区分：
 *  - `[可覆盖]` 可以在构建系统中 `-D` 覆盖（`#ifndef` 保护）。
 *  - `[规范固定]` 取值来自 IO-Link 规范；应用不应修改，仅测试可覆盖。
 *  - `[内部]` 选择器/派生常量；**请勿覆盖**，改了会改变语义。
 *
 * 常用配置速查（完整清单见下方各节）：
 *
 * | 宏                        | 默认 | 作用                             |
 * | :------------------------ | :--- | :------------------------------- |
 * | `IOLINK_PD_IN_MAX_SIZE`   | 32   | PD-in 缓冲上限（字节）           |
 * | `IOLINK_PD_OUT_MAX_SIZE`  | 32   | PD-out 缓冲上限（字节）          |
 * | `IOLINK_OD_MAX_SIZE`      | 32   | On-request Data 缓冲上限（字节） |
 * | `IOLINK_ISDU_BUFFER_SIZE` | 256  | 单次 ISDU 事务上限（字节）       |
 * | `IOLINK_DS_IMAGE_MAX`     | 256  | Data Storage 镜像上限（字节）    |
 * | `IOLINK_EVENT_QUEUE_SIZE` | 4    | 事件队列深度                     |
 * | `IOLINK_EVENT_PENDING_MAX`| 8    | 同时活动的事件数上限             |
 * | `IOLINK_TIMING_ENFORCE_DEFAULT` | 0 | 1=强制检查 t_ren/t_cycle，0=仅测量 |
 *
 * 运行时配置（PD 长度、最小周期、日志 sink 等）不在这里，见
 * `iolink_stack_config_t` / `iolink_device_config_t`。
 */

/**
 * @defgroup iolinki_config 编译期配置
 * @brief 可覆盖的缓冲区大小、队列深度、时序默认值与移植档位。
 * @{
 */

/* =========================================================================
 * 1. 缓冲与队列 [可覆盖]
 *
 * 这些是应用最常调整的旋钮。改小省 RAM，但运行时的 pd_in_len/pd_out_len/
 * od_width（见 iolink_stack_config_t）必须不超过这里的上限。
 * ========================================================================= */

/**
 * @brief 输入过程数据（设备 -> 主站）缓冲上限，单位字节。
 *
 * 规范允许标准 M-sequence 最多 32 字节。运行时 `stack.pd_in_len` 不得超过该值。
 * 默认：32
 */
#ifndef IOLINK_PD_IN_MAX_SIZE
#define IOLINK_PD_IN_MAX_SIZE 32U
#endif

/**
 * @brief 输出过程数据（主站 -> 设备）缓冲上限，单位字节。
 *
 * 运行时 `stack.pd_out_len` 不得超过该值。默认：32
 */
#ifndef IOLINK_PD_OUT_MAX_SIZE
#define IOLINK_PD_OUT_MAX_SIZE 32U
#endif

/**
 * @brief On-request Data 缓冲上限，单位字节（Table A.10）。
 *
 * TYPE_1_V/TYPE_2_V 允许 1/2/8/32 字节；缓冲区按规范最大值设置，
 * 运行时 `stack.od_width` 不得超过该值。默认：32
 */
#ifndef IOLINK_OD_MAX_SIZE
#define IOLINK_OD_MAX_SIZE 32U
#endif

/**
 * @brief 单次 ISDU 读/写事务的最大长度，单位字节。
 *
 * 标准参数通常不超过 256 字节。默认：256
 */
#ifndef IOLINK_ISDU_BUFFER_SIZE
#define IOLINK_ISDU_BUFFER_SIZE 256U
#endif

/**
 * @brief Data Storage 序列化镜像的最大长度，单位字节。
 *
 * 记录布局为 [Index(2,大端)][Subindex(1)][Length(1)][Data(Length)]；
 * 该值同时决定 storage.h 中 DS 分区的大小。默认：256
 */
#ifndef IOLINK_DS_IMAGE_MAX
#define IOLINK_DS_IMAGE_MAX 256U
#endif

/**
 * @brief 诊断事件队列深度。
 *
 * 资源受限设备可从更小的值开始。默认：4
 */
#ifndef IOLINK_EVENT_QUEUE_SIZE
#define IOLINK_EVENT_QUEUE_SIZE 4U
#endif

/**
 * @brief 同时处于活动状态（appears/disappears）的事件数上限。
 *
 * 每个活动的 Error/Warning 占一个生命周期槽位；只等待重复冷却的槽位会先被复用。
 * 槽位耗尽时新事件被丢弃并计入 `iolink_events_get_dropped_count()`。默认：8
 */
#ifndef IOLINK_EVENT_PENDING_MAX
#define IOLINK_EVENT_PENDING_MAX 8U
#endif

/**
 * @brief "事件出现"到"事件消失"的最小间隔，单位毫秒（A.6.4）。
 *
 * 该窗口内不发出 disappears；更早清除的条件会被保持到窗口结束。默认：50
 */
#ifndef IOLINK_EVENT_MIN_APPEAR_MS
#define IOLINK_EVENT_MIN_APPEAR_MS 50U
#endif

/**
 * @brief 同一 EventCode/qualifier 两次上报的最小间隔，单位毫秒（A.6.4）。
 *
 * 同一 EventCode 的上报频率不得高于每秒一次。默认：1000
 */
#ifndef IOLINK_EVENT_MIN_REPEAT_MS
#define IOLINK_EVENT_MIN_REPEAT_MS 1000U
#endif

/**
 * @brief 默认启用定时强制检查。
 *
 * 1：强制检查 t_ren / t_cycle；0：仅测量并计数。默认：0
 */
#ifndef IOLINK_TIMING_ENFORCE_DEFAULT
#define IOLINK_TIMING_ENFORCE_DEFAULT 0U
#endif

/* =========================================================================
 * 2. 规范固定值 [规范固定]
 *
 * 取值来自 IO-Link 规范；应用不应修改。保留 `#ifndef` 只为测试可以注入
 * 边界值。规范条款见 docs/SPEC_CONFORMANCE.md。
 * ========================================================================= */

/**
 * @brief 唤醒脉冲宽度（T_WU），单位微秒（Table 10，80 us）。
 */
#ifndef IOLINK_T_WU_US
#define IOLINK_T_WU_US 80U
#endif

/**
 * @brief 应答时间上限（T_REN），单位微秒（Table 10，最大 500 us）。
 */
#ifndef IOLINK_T_REN_US
#define IOLINK_T_REN_US 500U
#endif

/**
 * @brief 唤醒失败且无有效报文后回退到 SIO（T_DSIO），单位毫秒（Table 47，60..300 ms）。
 */
#ifndef IOLINK_T_DSIO_MS
#define IOLINK_T_DSIO_MS 300U
#endif

/* =========================================================================
 * 3. 构建与移植档位 [可覆盖，需配合 PHY/端口]
 *
 * 这里的开关改变编译进镜像的功能与对端口的要求；普通应用通常保持默认。
 * 原理与组合见 docs/feature-trimming.md、docs/PORTING.md。
 * ========================================================================= */

/* --- 3.1 功能裁剪 [可覆盖] --------------------------------------------- */

/**
 * @brief 便利档位：一次关闭下面多个可选特性（保留事件引擎）。
 *
 * 显式的 `-DIOLINK_FEATURE_*=1` 始终优先（`#ifndef` 保护）。必须定义为非 0
 * （例如 `-DIOLINK_MINIMAL_PROFILE=1`）；定义为 0 表示关闭。
 * CMake 用户请用 `-DIOLINK_MINIMAL_PROFILE=ON`（由 CMake 转成特性开关）。
 */
#if defined(IOLINK_MINIMAL_PROFILE) && (IOLINK_MINIMAL_PROFILE + 0)
#ifndef IOLINK_FEATURE_DATA_STORAGE
#define IOLINK_FEATURE_DATA_STORAGE 0
#endif
#ifndef IOLINK_FEATURE_SYSTEM_COMMAND
#define IOLINK_FEATURE_SYSTEM_COMMAND 0
#endif
#ifndef IOLINK_FEATURE_LEGACY_PARAMS
#define IOLINK_FEATURE_LEGACY_PARAMS 0
#endif
#ifndef IOLINK_PHY_FEATURE_WAKEUP
#define IOLINK_PHY_FEATURE_WAKEUP 0
#endif
#ifndef IOLINK_PHY_FEATURE_SIO_CQ
#define IOLINK_PHY_FEATURE_SIO_CQ 0
#endif
#ifndef IOLINK_PHY_FEATURE_LEGACY_DIAG
#define IOLINK_PHY_FEATURE_LEGACY_DIAG 0
#endif
#endif /* IOLINK_MINIMAL_PROFILE */

/** @brief 数据存储子系统（data_storage.c、ISDU DS 服务、ds ctx）。 */
#ifndef IOLINK_FEATURE_DATA_STORAGE
#define IOLINK_FEATURE_DATA_STORAGE 1
#endif
/** @brief ISDU SystemCommand 处理（0x01-0x06、0x80-0x83）。 */
#ifndef IOLINK_FEATURE_SYSTEM_COMMAND
#define IOLINK_FEATURE_SYSTEM_COMMAND 1
#endif
/** @brief 诊断事件引擎 + DDS + CKS 事件标志 + 故障轮询。 */
#ifndef IOLINK_FEATURE_EVENTS
#define IOLINK_FEATURE_EVENTS 1
#endif
/** @brief 旧版全局参数 API + g_legacy_* 全局变量（推荐使用 ctx API）。 */
#ifndef IOLINK_FEATURE_LEGACY_PARAMS
#define IOLINK_FEATURE_LEGACY_PARAMS 1
#endif
/** @brief PHY 唤醒检测（detect_wakeup）。 */
#ifndef IOLINK_PHY_FEATURE_WAKEUP
#define IOLINK_PHY_FEATURE_WAKEUP 1
#endif
/** @brief PHY 手动 C/Q 推挽（set_cq_line）。 */
#ifndef IOLINK_PHY_FEATURE_SIO_CQ
#define IOLINK_PHY_FEATURE_SIO_CQ 1
#endif
/** @brief PHY 旧版诊断（get_voltage_mv + is_short_circuit）。 */
#ifndef IOLINK_PHY_FEATURE_LEGACY_DIAG
#define IOLINK_PHY_FEATURE_LEGACY_DIAG 1
#endif

/* --- 3.2 ISR 传输粒度 [可覆盖] ----------------------------------------- */

/*
 *   RX BYTE  : phy->rx_octet()  - 库逐字节成帧
 *   RX FRAME : phy->rx_frame()  - PHY 交付整帧（零拷贝）
 *   TX BYTE  : phy->tx_octet()+phy->tx_done() - 库逐字节喂送
 *   TX FRAME : phy->tx_frame()  - 库交付整个应答帧
 *
 * 默认 BYTE/BYTE（单字节收发器，例如 L6364）；整帧 DMA 的 PHY 设为 FRAME。
 */

/* [内部] 选择器取值，勿覆盖。 */
#define IOLINK_ISR_RX_BYTE 0
#define IOLINK_ISR_RX_FRAME 1
#define IOLINK_ISR_TX_BYTE 0
#define IOLINK_ISR_TX_FRAME 1

#ifndef IOLINK_ISR_RX_MODE
#define IOLINK_ISR_RX_MODE IOLINK_ISR_RX_BYTE
#endif
#ifndef IOLINK_ISR_TX_MODE
#define IOLINK_ISR_TX_MODE IOLINK_ISR_TX_BYTE
#endif

/* 非法取值会通过 #if 比较静默落到某个分支，因此在此显式拒绝。 */
#if (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_BYTE) && (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_FRAME)
#error "IOLINK_ISR_RX_MODE must be IOLINK_ISR_RX_BYTE or IOLINK_ISR_RX_FRAME"
#endif
#if (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_BYTE) && (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_FRAME)
#error "IOLINK_ISR_TX_MODE must be IOLINK_ISR_TX_BYTE or IOLINK_ISR_TX_FRAME"
#endif

/**
 * @brief 要求 PHY 为 ISR 泵应答提供 request_service() 钩子。
 *
 * 0（默认）：没有该钩子的 PHY 保留从调用上下文直接发送的回退路径（主机测试）。
 * 1：不使用回退，每个应答都由 ISR 上报，板卡必须提供 request_service()。
 */
#ifndef IOLINK_PHY_FEATURE_ISR_TX_ONLY
#define IOLINK_PHY_FEATURE_ISR_TX_ONLY 0
#endif

/**
 * @brief TX FRAME 模式的 PHY 是否提供 tx_busy()。
 *
 * 1：PHY 上报线路何时空闲，应答缓冲可在传输结束后立即释放（两个缓冲足够）。
 * 0（默认）：传输结束不可观察，缓冲保持租约到下次捕获（需要三个缓冲）。
 * 仅在 TX FRAME 构建中生效。
 */
#ifndef IOLINK_PHY_FEATURE_TX_BUSY
#define IOLINK_PHY_FEATURE_TX_BUSY 0
#endif

/* --- 3.3 并发与统计 [可覆盖] ------------------------------------------- */

/*
 * 档位描述"数据 ISR 与唯一主上下文"的关系，而不是允许多任务调用 API
 * （库始终只有一个主上下文调用者）。端口需要提供的最小原语见 port.h。
 *   SINGLE : 无异步上下文（主机仿真、协作式循环）；库不引用临界区符号。
 *   ISR    : 一个数据 ISR 抢占主上下文（默认双泵）；需要原子发布字、
 *            COMPILER 屏障与临界区。
 *   SMP    : 泵可能在不同核上；屏障必须是系统范围。
 */

/* [内部] 选择器取值，勿覆盖。 */
#define IOLINK_CONCURRENCY_SINGLE 0
#define IOLINK_CONCURRENCY_ISR 1
#define IOLINK_CONCURRENCY_SMP 2
/**
 * @brief 已移除的档位，仅用于让旧的构建标志显式报错。
 *
 * 该值保持越界，使过期的 `-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_RTOS`
 * 被 port.h 拒绝，而不是静默求值为 0（= SINGLE）。
 */
#define IOLINK_CONCURRENCY_RTOS 3

#ifndef IOLINK_CONCURRENCY
#define IOLINK_CONCURRENCY IOLINK_CONCURRENCY_ISR
#endif

/**
 * @brief DLL 统计计数器在多上下文间的共享方式。
 *
 * SHARED（默认）：一组计数器，两个上下文共同更新；竞争可能丢一次自增（仅诊断）。
 * SPLIT：每个上下文一组，读取时求和，不会丢更新。
 */
/* [内部] 选择器取值，勿覆盖。 */
#define IOLINK_STATS_SHARED 0
#define IOLINK_STATS_SPLIT 1

#ifndef IOLINK_STATS_MODE
#define IOLINK_STATS_MODE IOLINK_STATS_SHARED
#endif

/* --- 3.4 协议兼容 [可覆盖，默认严格 V1.1] ------------------------------ */

/**
 * @brief 接受 V1.0 的 STARTUP -> OPERATE DeviceOperate（0x99）捷径。
 *
 * 仅为必须与 V1.0 主站互操作的产品启用。默认：0（严格 V1.1）
 */
#ifndef IOLINK_COMPAT_V10_STARTUP_OPERATE
#define IOLINK_COMPAT_V10_STARTUP_OPERATE 0
#endif

/**
 * @brief 在 OPERATE 激活时应答 TYPE_0 页读取。
 *
 * 规范要求丢弃应答并回到 STARTUP；仅为会在 OPERATE 重复页读的旧主站启用。
 * 默认：0（严格 V1.1）
 */
#ifndef IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
#define IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE 0
#endif

/**
 * @brief 在 DataStorageIndex 子索引 0 上保留 V1.1 之前的整镜像载荷。
 *
 * 规范要求子索引 0 为拼接记录（12 + 3n 字节）；仅为旧应用启用。默认：0
 */
#ifndef IOLINK_DS_LEGACY_IMAGE_SUB0
#define IOLINK_DS_LEGACY_IMAGE_SUB0 0
#endif

/* =========================================================================
 * 4. 日志接口（运行时 sink）
 *
 * 打印点定义在 iolinki/log.h；输出由应用通过 iolink_log_sink_t 提供，默认静默。
 * 应用在 iolink_device_config_t 中配置，或运行时调用 iolink_device_set_log()。
 * 编译期开关（ISR 日志等）在 log.h 内。
 * ========================================================================= */

/** @} */ /* iolinki_config 结束 */

#endif  // IOLINK_CONFIG_H
