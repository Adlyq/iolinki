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
 * | `IOLINK_MBX_SLOTS`        | 4    | 帧邮箱深度（2 的幂，≥ 2）       |
 * | `IOLINK_REPLY_PLAN_SLOTS` | 48   | 应答计划槽位上限（按状态切片）   |
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
 * @brief 接收帧邮箱深度（数据 ISR -> 主泵），单位槽。
 *
 * 必须为 2 的幂且至少为 2（SPSC 环用掩码取模）。槽位吸收主泵处理
 * 上一帧期间到达的请求；环满时新帧被丢弃并计入 `isr_mailbox_drops`。
 * 默认：4
 */
#ifndef IOLINK_MBX_SLOTS
#define IOLINK_MBX_SLOTS 4U
#endif

/**
 * @brief 应答计划槽位上限。
 *
 * 计划表是主泵为 ISR 编译的请求指纹集合；槽位耗尽时多余计划被丢弃
 * 并计入 `reply_plan_overflows`，对应请求按"计划未命中"丢弃。默认：48
 */
#ifndef IOLINK_REPLY_PLAN_SLOTS
#define IOLINK_REPLY_PLAN_SLOTS 48U
#endif

/* 环使用 uint8_t 槽数与掩码取模，因此上限为 128（256 会截断为 0）。 */
#if (IOLINK_MBX_SLOTS < 2) || (IOLINK_MBX_SLOTS > 128) || \
    ((IOLINK_MBX_SLOTS & (IOLINK_MBX_SLOTS - 1)) != 0)
#error "IOLINK_MBX_SLOTS must be a power of two in [2, 128]"
#endif
#if (IOLINK_REPLY_PLAN_SLOTS < 1) || (IOLINK_REPLY_PLAN_SLOTS > 255)
#error "IOLINK_REPLY_PLAN_SLOTS must be in [1, 255]"
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
 * @brief 是否在上下文内嵌默认的传输 scratch 缓冲。
 *
 * 1（默认）：`iolink_isdu_ctx_t` / `iolink_ds_ctx_t` 各自内嵌 fallback
 * 存储，`iolink_isdu_init()` 等既有入口无需额外初始化即可使用。
 * 0：上下文只保留缓冲指针，调用方必须提供
 * `iolink_workspace_t`（设备层要求 `iolink_device_config_t::workspace`
 * 非 NULL）。该模式把 ISDU 与 DS 的 scratch 全部移出上下文，是
 * 小 MCU 的裁剪路径。
 *
 * 该开关只影响内存归属，不改变协议行为。
 */
#ifndef IOLINK_WORKSPACE_EMBEDDED
#define IOLINK_WORKSPACE_EMBEDDED 1
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
 * @brief 协议错误类 PULSE 诊断源的保持窗口，单位毫秒。
 *
 * 每次检测到 CRC/超时/组帧/时序错误时刷新窗口；窗口结束才发出
 * disappears，避免一次会话内的连续错误产生事件风暴（10.10.2）。
 * 0 表示不自动撤销（需要显式清除）。默认：1000
 */
#ifndef IOLINK_DIAG_PULSE_HOLD_MS
#define IOLINK_DIAG_PULSE_HOLD_MS 1000U
#endif

/**
 * @brief 时序强制检查的构建默认值（运行时数据）。
 *
 * 1：默认强制检查 t_ren / t_cycle；0：默认仅测量并计数。默认：0
 *
 * 该宏只提供 `iolink_build_caps_t::timing_enforce_default` 的初值；
 * 每个实例可用 `iolink_stack_config_t::timing_enforce` 覆盖。
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
/**
 * @brief 设备初始化期的配置自检（总开关）。
 *
 * 打开时，@ref iolink_device_init 在绑定任何子系统之前检查应用配置：
 *  - 对象字典表项的自相矛盾（缺后端指针、只读项带写钩子、`PERSIST` 未配
 *    `STORE`、可写字节串无长度上限等）；
 *  - 由对象字典派生的 PERSIST 集合能否放进 `IOLINK_DS_IMAGE_MAX` 的参数
 *    镜像缓冲。
 *
 * 命中时返回 ::IOLINK_DEVICE_INIT_ERR_OD 或
 * ::IOLINK_DEVICE_INIT_ERR_PERSIST_CAPACITY，而不是拖到首次 ISDU/DS 访问才
 * 失败。常量之间的同类检查已经在表项宏里于编译期完成。本开关只影响
 * 初始化期自检，协议期数据校验始终生效；表稳定后可以关掉以省下这段
 * `.text`。
 */
#ifndef IOLINK_FEATURE_VALIDATE
#define IOLINK_FEATURE_VALIDATE 1
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
 * @brief 要求 PHY 为 ISR 泵应答提供 request_service() 钩子（运行时能力）。
 *
 * 该宏只提供 `IOLINK_STACK_FLAG_REQUIRE_ISR_TX` 的构建默认值。回退路径
 * 常驻；"只有 ISR 能发"是运行时能力位 `IOLINK_CAP_REQUIRE_ISR_TX`，
 * 由 `iolink_device_init()` 校验（缺 request_service() 时拒绝初始化）。
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

/* --- 3.4 协议兼容 [运行时能力，宏只提供构建默认值] ---------------------- */

/*
 * 这三个开关现在是 `iolink_stack_config_t::compat_flags`
 * （IOLINK_STACK_COMPAT_*）的构建默认值，实例可用运行时配置覆盖
 * （按位或）。判定点只有一个：实例能力位 IOLINK_CAP_COMPAT_*。
 */

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
 * 3.5 PD-in 缓冲区数量（派生，勿覆盖）
 *
 * 由 ISR 发送粒度与 TX_BUSY 钩子决定；放在 config.h 以保证只包含
 * config.h 的翻译单元（profile 档位符号、组合探针）也能看到它。
 * ========================================================================= */

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

/* =========================================================================
 * 4. 日志接口（运行时 sink）
 *
 * 打印点定义在 iolinki/log.h；输出由应用通过 iolink_log_sink_t 提供，默认静默。
 * 应用在 iolink_device_config_t 中配置，或运行时调用 iolink_device_set_log()。
 * 编译期开关（ISR 日志等）在 log.h 内。
 * ========================================================================= */

/** @} */ /* iolinki_config 结束 */

#endif  // IOLINK_CONFIG_H
