/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CONFIG_H
#define IOLINK_CONFIG_H

/**
 * @file config.h
 * @brief iolinki 协议栈的编译期配置。
 *
 * 本文件定义缓冲区大小和队列深度的默认值。
 * 要覆盖这些默认值，可在构建系统中定义相应宏，
 * 或在本文件之前包含的私有配置头文件中定义。
 */

/**
 * @defgroup iolinki_config 编译期配置
 * @brief 可覆盖的缓冲区大小、队列深度与定时默认值。
 * @{
 */

/* -------------------------------------------------------------------------
 * 功能裁剪（编译期）
 *
 * 每个宏取 1（编译进来，默认的完整协议栈）或 0（移除）。项目构建
 * 会把未使用的宏覆盖为 0 以减小镜像。被裁剪掉的
 * 代码若实际被使用会导致编译错误，这正是预期的
 * 防护。参见 docs/feature-trimming.md。
 *
 * IOLINK_MINIMAL_PROFILE 是一个便利开关，会把下面被裁剪的
 * 功能默认关闭；显式的 -D 始终优先（用 #ifndef 保护）。
 * 它不影响 IOLINK_FEATURE_EVENTS（本项目保留该项）。
 * ------------------------------------------------------------------------- */

#ifdef IOLINK_MINIMAL_PROFILE
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

/* -------------------------------------------------------------------------
 * ISR 传输粒度。
 * 这两个开关决定 PHY 在 ISR 路径上如何搬运数据：
 *
 *   RX BYTE  : phy->rx_octet()  - 库逐字节成帧
 *   RX FRAME : phy->rx_frame()  - PHY 交付整帧（零拷贝）
 *   TX BYTE  : phy->tx_octet()+phy->tx_done() - 库逐字节喂送
 *   TX FRAME : phy->tx_frame()  - 库交付整个应答帧
 *
 * 默认是 BYTE/BYTE（单字节收发器，例如 L6364）。带整帧 DMA 的 PHY
 * 在构建中把对应宏设为 FRAME。
 * ------------------------------------------------------------------------- */
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

/* 若不这样限制，异常取值会通过
 * #if 比较静默地选中 FRAME 分支，因此这里只接受两个已定义的模式。 */
#if (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_BYTE) && (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_FRAME)
#error "IOLINK_ISR_RX_MODE must be IOLINK_ISR_RX_BYTE or IOLINK_ISR_RX_FRAME"
#endif
#if (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_BYTE) && (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_FRAME)
#error "IOLINK_ISR_TX_MODE must be IOLINK_ISR_TX_BYTE or IOLINK_ISR_TX_FRAME"
#endif

/**
 * @brief 要求 PHY 为 ISR 泵应答提供 request_service() 钩子。
 *
 * 0（默认）：没有该钩子的 PHY 保留从调用上下文直接发送的旧行为
 * （主机测试）。1：不使用回退；每个应答都由 ISR 上报，
 * 因此板卡必须提供 request_service()。
 */
#ifndef IOLINK_PHY_FEATURE_ISR_TX_ONLY
#define IOLINK_PHY_FEATURE_ISR_TX_ONLY 0
#endif

/**
 * @brief TX FRAME 模式的 PHY 是否提供 tx_busy()。
 *
 * 1：PHY 会上报 C/Q 线路何时重新空闲，因此库在传输结束
 *    后立即释放 PD-in 应答缓冲区，两个缓冲区就够用。
 * 0（默认）：传输结束不可观察，缓冲区会一直保持租约直到
 *    下一次捕获，第三个缓冲区用来吸收主泵的更新。
 * 在 TX FRAME 构建之外被忽略。
 */
#ifndef IOLINK_PHY_FEATURE_TX_BUSY
#define IOLINK_PHY_FEATURE_TX_BUSY 0
#endif

/* -------------------------------------------------------------------------
 * 并发配置。
 *
 * 该配置告诉协议栈，端口必须提供哪些异步原语
 * （参见 include/iolinki/port.h 和 docs/PORTING.md）。它选择
 * 最小集合；绝不增加目标平台无法满足的假设。
 *
 * 协议栈只有一个主上下文调用者：该配置选择数据
 * ISR 与该调用者的关系，而不是有多少任务可以调用 API。
 *
 *   SINGLE : 无异步上下文（主机仿真、协作式循环）。
 *            不需要屏障，也不需要临界区；库完全不会
 *            引用临界区符号。
 *   ISR    : 一个数据 ISR 抢占一个主上下文（默认的双泵
 *            传输）。需要天然原子的发布字、至少一个
 *            编译器屏障以及临界区。
 *   SMP    : 泵可能运行在与主循环不同的核上；
 *            屏障必须是系统范围的。
 * ------------------------------------------------------------------------- */
/** @brief 无异步上下文。 */
#define IOLINK_CONCURRENCY_SINGLE 0
/** @brief 一个数据 ISR 抢占一个主上下文。 */
#define IOLINK_CONCURRENCY_ISR 1
/** @brief 泵可能运行在与主上下文不同的核上。 */
#define IOLINK_CONCURRENCY_SMP 2
/**
 * @brief 已移除的档位，仅用于让旧的构建标志显式报错。
 *
 * 协议栈只有一个主上下文调用者，因此 RTOS 档位
 * 已不存在。该值保持超出范围（且不是有效配置），
 * 从而让过期的 `-DIOLINK_CONCURRENCY=IOLINK_CONCURRENCY_RTOS` 被拒绝，
 * 而不是在预处理器中静默求值为 0（= SINGLE）。
 */
#define IOLINK_CONCURRENCY_RTOS 3

#ifndef IOLINK_CONCURRENCY
#define IOLINK_CONCURRENCY IOLINK_CONCURRENCY_ISR
#endif

/**
 * @brief DLL 统计计数器在各上下文之间如何共享。
 *
 * SHARED（默认）：一组计数器，由两个上下文共同更新。与抢占
 * 竞争的一次自增可能丢失（仅影响诊断）。SPLIT：每个上下文
 * 一组计数器，读取统计时求和，因此不会丢失更新，
 * 也不涉及临界区。
 */
#define IOLINK_STATS_SHARED 0
#define IOLINK_STATS_SPLIT 1

#ifndef IOLINK_STATS_MODE
#define IOLINK_STATS_MODE IOLINK_STATS_SHARED
#endif

/* -------------------------------------------------------------------------
 * 协议兼容性
 * ------------------------------------------------------------------------- */

/**
 * @brief 接受 V1.0 的 STARTUP -> OPERATE DeviceOperate（0x99）捷径。
 *
 * V1.1 测试规范要求非 V1.0 设备在仍处于 STARTUP 时
 * 不得接受 OPERATE 请求。仅为必须与 V1.0 主站
 * 互操作的产品启用。
 * 默认：0（V1.1 严格模式）。
 */
#ifndef IOLINK_COMPAT_V10_STARTUP_OPERATE
#define IOLINK_COMPAT_V10_STARTUP_OPERATE 0
#endif

/**
 * @brief 在 OPERATE 激活时应答 TYPE_0 页读取。
 *
 * Table 57/67 要求此类读取丢弃应答并使设备
 * 回到 STARTUP。某些旧版主站在进入 OPERATE 后会重复
 * 执行 TYPE_0 页读取；仅为这类部署启用。
 * 默认：0（V1.1 严格模式）。
 */
#ifndef IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
#define IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE 0
#endif

/**
 * @brief 在 DataStorageIndex 子索引 0 上保留 V1.1 之前的整镜像载荷。
 *
 * V1.1 Table B.10/B.11 要求子索引 0 为拼接记录
 * （12 + 3n 字节）。仅为仍在读取旧镜像格式的
 * 旧应用启用。
 * 默认：0（V1.1 严格模式）。
 */
#ifndef IOLINK_DS_LEGACY_IMAGE_SUB0
#define IOLINK_DS_LEGACY_IMAGE_SUB0 0
#endif

/* -------------------------------------------------------------------------
 * ISDU 配置
 * ------------------------------------------------------------------------- */

/**
 * @brief ISDU 缓冲区（服务数据）的最大大小。
 * 限制单次 ISDU 读/写事务的最大大小。
 * 默认：256 字节（足以应付标准参数）。
 */
#ifndef IOLINK_ISDU_BUFFER_SIZE
#define IOLINK_ISDU_BUFFER_SIZE 256U
#endif

/**
 * @brief 序列化数据存储参数镜像的最大大小。
 *
 * 必须能容纳所有由 DS 支持的参数记录。每条记录的布局为
 * [Index(2, 大端)][Subindex(1)][Length(1)][Data(Length)]。它同时也是
 * 持久化存储布局中 DS 区域的大小（storage.h）。
 */
#ifndef IOLINK_DS_IMAGE_MAX
#define IOLINK_DS_IMAGE_MAX 256U
#endif

/* -------------------------------------------------------------------------
 * 事件配置
 * ------------------------------------------------------------------------- */

/**
 * @brief 诊断事件队列的大小。
 * 在资源受限的设备上从小值开始。
 * 默认：4
 */
#ifndef IOLINK_EVENT_QUEUE_SIZE
#define IOLINK_EVENT_QUEUE_SIZE 4U
#endif

/**
 * @brief 同时处于活动状态（appears/disappears）的事件最大数量。
 *
 * 每个活动的 Error/Warning 占用一个生命周期槽位。只等待
 * 1 s 重复冷却的槽位会优先被复用。当所有槽位都持有活动
 * 事件时，后续的 Error/Warning 会被丢弃，并由
 * iolink_events_get_dropped_count() 计数。
 * 默认：8
 */
#ifndef IOLINK_EVENT_PENDING_MAX
#define IOLINK_EVENT_PENDING_MAX 8U
#endif

/**
 * @brief “事件出现”与其“事件消失”之间的最小延时。
 *
 * 事件分发器不得在该窗口内发出事件消失
 * （A.6.4 / 附录 A 的分发规则）；更早清除的条件会被保持到
 * 窗口结束。默认：50 ms
 */
#ifndef IOLINK_EVENT_MIN_APPEAR_MS
#define IOLINK_EVENT_MIN_APPEAR_MS 50U
#endif

/**
 * @brief 同一 EventCode/qualifier 两次上报之间的最小间隔。
 *
 * A.6.4：事件分发器调用 AL_Event 服务时，同一 EventCode 与
 * EventQualifier 的频率不得高于每秒一次。
 * 默认：1000 ms
 */
#ifndef IOLINK_EVENT_MIN_REPEAT_MS
#define IOLINK_EVENT_MIN_REPEAT_MS 1000U
#endif

/* -------------------------------------------------------------------------
 * 过程数据配置
 * ------------------------------------------------------------------------- */

/**
 * @brief 输入过程数据（设备 -> 主站）的最大大小，单位为字节。
 * V1.1 规范在标准 M 序列中允许最多 32 字节。
 * 默认：32
 */
#ifndef IOLINK_PD_IN_MAX_SIZE
#define IOLINK_PD_IN_MAX_SIZE 32U
#endif

/**
 * @brief 输出过程数据（主站 -> 设备）的最大大小，单位为字节。
 * V1.1 规范在标准 M 序列中允许最多 32 字节。
 * 默认：32
 */
#ifndef IOLINK_PD_OUT_MAX_SIZE
#define IOLINK_PD_OUT_MAX_SIZE 32U
#endif

/* -------------------------------------------------------------------------
 * 定时配置
 * ------------------------------------------------------------------------- */

/**
 * @brief 默认启用定时强制检查。
 * 置 1 强制检查 t_ren / t_cycle，置 0 仅测量。
 */
#ifndef IOLINK_TIMING_ENFORCE_DEFAULT
#define IOLINK_TIMING_ENFORCE_DEFAULT 0U
#endif

/**
 * @brief 唤醒脉冲时长（T_WU），单位为微秒。
 *
 * 规范 Table 10：唤醒请求是一个电流脉冲；本协议栈把 PHY 使用的
 * 脉冲宽度建模为 80 us。（T_DWU 是独立的 30..50 ms
 * 重试延时，设备侧不建模。）
 */
#ifndef IOLINK_T_WU_US
#define IOLINK_T_WU_US 80U
#endif

/**
 * @brief 应答时间上限（T_REN），单位为微秒。
 *
 * 规范 Table 10：单一取值，最大 500 us，与波特率无关。
 */
#ifndef IOLINK_T_REN_US
#define IOLINK_T_REN_US 500U
#endif

/**
 * @brief 唤醒失败且无有效报文后回退到 SIO（T_DSIO）。
 *
 * 规范 Table 47 T10：60..300 ms，默认 300 ms。
 */
#ifndef IOLINK_T_DSIO_MS
#define IOLINK_T_DSIO_MS 300U
#endif

/* -------------------------------------------------------------------------
 * 按需数据（OD）配置
 * ------------------------------------------------------------------------- */

/**
 * @brief 按需数据宽度 m 的最大值，单位为字节（Table A.10）。
 * TYPE_1_V/TYPE_2_V 允许 1/2/8/32 字节；默认把缓冲区设为规范
 * 最大值，以便任意配置的 od_width 都能容纳。
 * 默认：32 字节。
 */
#ifndef IOLINK_OD_MAX_SIZE
#define IOLINK_OD_MAX_SIZE 32U
#endif

/* -------------------------------------------------------------------------
 * 日志钩子
 *
 * 协议栈只提供打印点，不实现输出。四个宏默认为空，因此默认构建
 * 完全静默：字符串不进入镜像，参数也不求值。
 *
 * 集成时在私有配置头中覆盖这些宏，并通过 `-include` 或构建系统
 * 让它们在所有源文件之前生效，例如：
 *
 *   #define IOLINK_LOG_INFO(...) my_log(LOG_LEVEL_INFO, __VA_ARGS__)
 *
 * 宏必须接受 printf 风格的格式串与变参；库不做格式化。
 * ISR 打印点只使用 IOLINK_LOG_ISR_* 系列，默认关闭；置
 * IOLINK_LOG_ENABLE_ISR 为 1 后才转发到上面的主宏。
 * ------------------------------------------------------------------------- */
#ifndef IOLINK_LOG_ERROR
#define IOLINK_LOG_ERROR(...) ((void) 0)
#endif
#ifndef IOLINK_LOG_WARN
#define IOLINK_LOG_WARN(...) ((void) 0)
#endif
#ifndef IOLINK_LOG_INFO
#define IOLINK_LOG_INFO(...) ((void) 0)
#endif
#ifndef IOLINK_LOG_DEBUG
#define IOLINK_LOG_DEBUG(...) ((void) 0)
#endif

/** @brief 启用数据 ISR 路径上的日志（默认关闭；主宏仍需配置）。 */
#ifndef IOLINK_LOG_ENABLE_ISR
#define IOLINK_LOG_ENABLE_ISR 0
#endif

#if IOLINK_LOG_ENABLE_ISR
#define IOLINK_LOG_ISR_ERROR(...) IOLINK_LOG_ERROR(__VA_ARGS__)
#define IOLINK_LOG_ISR_WARN(...) IOLINK_LOG_WARN(__VA_ARGS__)
#define IOLINK_LOG_ISR_INFO(...) IOLINK_LOG_INFO(__VA_ARGS__)
#define IOLINK_LOG_ISR_DEBUG(...) IOLINK_LOG_DEBUG(__VA_ARGS__)
#else
#define IOLINK_LOG_ISR_ERROR(...) ((void) 0)
#define IOLINK_LOG_ISR_WARN(...) ((void) 0)
#define IOLINK_LOG_ISR_INFO(...) ((void) 0)
#define IOLINK_LOG_ISR_DEBUG(...) ((void) 0)
#endif

/** @} */ /* iolinki_config 结束 */

#endif  // IOLINK_CONFIG_H
