/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_H
#define IOLINK_PHY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iolinki/config.h"

/**
 * @file phy.h
 * @brief IO-Link 物理层（PHY）抽象接口
 */

/**
 * @defgroup iolinki_phy 物理层（PHY）接口
 * @brief 与硬件无关的收发器契约、运行模式和波特率。
 * @{
 */

/**
 * @brief IO-Link 运行模式
 */
typedef enum
{
    IOLINK_PHY_MODE_INACTIVE = 0U, /**< PHY 接口已禁用 */
    IOLINK_PHY_MODE_SIO = 1U,      /**< 标准 I/O 模式（数字输入/输出） */
    IOLINK_PHY_MODE_SDCI = 2U      /**< 数字通信模式（IO-Link 交互） */
} iolink_phy_mode_t;

/**
 * @brief IO-Link 通信波特率（COMx）
 */
typedef enum
{
    IOLINK_BAUDRATE_COM1 = 0U, /**< 4.8 kbit/s */
    IOLINK_BAUDRATE_COM2 = 1U, /**< 38.4 kbit/s */
    IOLINK_BAUDRATE_COM3 = 2U  /**< 230.4 kbit/s */
} iolink_baudrate_t;

/**
 * @brief iolink_phy_api_t.get_faults() 返回的物理故障标志。
 *
 * PHY 上报物理状态；设备决定如何
 * 在总线上报告它。DLL 轮询该位掩码，并把每个 0->1 边沿作为
 * 映射的 Table D.1 事件上报（参见该钩子自身的说明）。
 */
#define IOLINK_PHY_FAULT_UNDERVOLTAGE (1U << 0)  /**< L+ 电源低于容差 */
#define IOLINK_PHY_FAULT_OVERTEMP (1U << 1)      /**< 收发器过温 */
#define IOLINK_PHY_FAULT_SHORT_CIRCUIT (1U << 2) /**< C/Q（或 L+）短路 */
#define IOLINK_PHY_FAULT_OVERVOLTAGE (1U << 3)   /**< L+ 电源高于容差 */
/** 所有已定义的物理故障标志。 */
#define IOLINK_PHY_FAULT_ALL                                                                      \
    (IOLINK_PHY_FAULT_UNDERVOLTAGE | IOLINK_PHY_FAULT_OVERTEMP | IOLINK_PHY_FAULT_SHORT_CIRCUIT | \
     IOLINK_PHY_FAULT_OVERVOLTAGE)

/**
 * @brief 原始字节钩子返回的规范化状态位
 *        （iolink_phy_api_t.rx_octet()）。
 *
 * 原始字节 PHY 把自身收发器状态转换为这些位，以便
 * 库的组帧引擎保持与设备无关。库只需要
 * 帧结束/错误指示：在单字节收发器上，即
 * 主站帧结束时（字节计数
 * 超时）或接收错误时产生的“数据 + 校验”条件。
 */
#define IOLINK_PHY_STATUS_CHK (1U << 0)        /**< 帧结束超时 / 接收错误 */
#define IOLINK_PHY_STATUS_DATA_READY (1U << 1) /**< 有已接收的字节可用 */

/**
 * @brief 物理层（PHY）API 结构
 *
 * 本接口定义协议栈与硬件相关
 * 收发器驱动（例如用于 MAX14827、L6362 或虚拟 TTY）之间的契约。
 */
typedef struct
{
    void* user; /**< 回传给每个回调的不透明驱动上下文。 */

    /**
     * @brief 初始化收发器硬件
     * @return 成功返回 0，硬件失败返回负错误码
     */
    int (*init)(void* user);

    /**
     * @brief 设置 PHY 运行模式（SDCI 与 SIO）
     * @param mode 目标模式
     */
    void (*set_mode)(void* user, iolink_phy_mode_t mode);

    /**
     * @brief 设置通信波特率
     * @param baudrate 目标 COMx 速率
     */
    void (*set_baudrate)(void* user, iolink_baudrate_t baudrate);

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /**
     * @brief 读取一个完整接收帧（RX FRAME 模式）。
     *
     * PHY 拥有该帧；返回的指针在下一次
     * rx_frame() 调用前有效（零拷贝：库直接读取，不在
     * ISR 中复制）。PHY 在电气层界定并校验帧的完整性
     * （例如 USART IDLE 加上配置的帧结束规则）。
     * 它不得把帧头或其他截断前缀作为完整
     * 帧返回。字节数通过 @p len 上报，PHY 状态字节
     * （IOLINK_PHY_STATUS_*）通过 @p status 上报；协议校验和与 M 序列
     * 校验仍由库负责。
     * @return 指向该帧的指针；没有完整帧就绪时为 NULL。
     */
    const uint8_t* (*rx_frame)(void* user, uint8_t* status, uint8_t* len);
#else
    /**
     * @brief 读取一个接收字节及其收发器状态（RX BYTE 模式）。
     *
     * 对于单字节收发器（例如单字节 SPI 收发器），
     * 库负责 M 序列组帧：它在此拉取接收字节，并
     * 通过 tx_octet()/tx_done() 送入应答字节。@p status 是 PHY 状态
     * 字节（参见 IOLINK_PHY_STATUS_*）。
     * @return 若读出字节到 @p data 则返回 1，若无待处理数据返回 0，
     *         传输错误时返回负值。
     */
    int (*rx_octet)(void* user, uint8_t* data, uint8_t* status);
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /**
     * @brief 发送一个完整应答帧（TX FRAME 模式）。
     *
     * 库把完整应答（有效载荷 + 已追加的 CKS）交给
     * PHY 在一次操作中发送（例如 DMA 传输）。PHY 负责
     * 传输结束，并在自身的完成
     * 中断中释放线路，因此该模式没有 tx_done() 钩子。@p data 必须保持有效
     * 直到传输完成。
     * @return 成功返回 0，出错返回负值。
     */
    int (*tx_frame)(void* user, const uint8_t* data, size_t len);

    /**
     * @brief PHY 仍占用 C/Q 线路时返回非零值（可选）。
     *
     * 只读且非阻塞；可从数据 ISR 和主
     * 上下文调用。库在启动 ISR 应答前检查它（线路忙时
     * 会把应答推迟到主循环泵送，而不是拆掉该帧），并
     * 从主循环泵送中轮询它以释放 PD-in 应答缓冲区。
     * NULL 保持旧有行为：传输结束不可观测，因此
     * 应答缓冲区只在下一次捕获之后才复用。
     * @return 传输进行中返回 1，线路空闲时返回 0。
     */
    uint8_t (*tx_busy)(void* user);
#else
    /**
     * @brief 写入一个应答字节（TX BYTE 模式）。
     *
     * 库在收发器请求时逐字节送入应答
     * （写入第一个字节即启动该帧）；它在发送过程中累加报文
     * 校验和，并在 tx_done() 之前发出最后的 CKS 字节。收发器
     * 与数据中断的仲裁由 PHY 负责。
     * @return 本次访问的规范化 PHY 状态字节（IOLINK_PHY_STATUS_*）。
     */
    int (*tx_octet)(void* user, uint8_t data);

    /**
     * @brief 结束/完成当前设备发送（仅 TX BYTE）。
     *
     * 由库在最后一个字节之后调用（发送帧结束/
     * 释放线路），并由 RX BYTE 间隔处理程序调用，以释放
     * 收发器的接收引擎。TX FRAME 构建不声明该成员：
     * PHY 自行结束传输。原名为 tx_end()。
     */
    void (*tx_done)(void* user);
#endif

    /**
     * @brief 收发器数据中断线的当前电平。
     * @return 有效时为 1，否则为 0。用于门控 ISR 泵送并
     *         排空背靠背突发；在 FRAME 模式下还会报告
     *         RX 帧完整/TX 就绪。
     */
    uint8_t (*int_active)(void* user);

    /* --- 可选协议栈定时器（与 ST 的 ISR 泵送对齐） ---------------------------
     * response_timer_* 实现 t_A 单次定时（13 个位时间）：在有效的
     * 请求字节上启动（同时停止周期节拍），并在
     * 应答开始或超时时停止。stack_timer_start 驱动周期性节拍，
     * 该节拍授权主上下文服务窗口。两者均为可选；当
     * 为 NULL 时，库保持旧有的软件时间戳行为。
     * stack_timer_stop 已声明，但当前 src/ 未调用它。 */
    void (*response_timer_start)(void* user);
    void (*response_timer_stop)(void* user);
    void (*stack_timer_start)(void* user);
    void (*stack_timer_stop)(void* user);

    /**
     * @brief 请求 PHY 立即运行数据中断泵送（可选）。
     *
     * ISR 双泵传输从中断上下文发送应答。当
     * 主上下文处理程序已暂存应答时，库调用该钩子，使
     * PHY 请求其数据中断（例如挂起 USART IRQ），而
     * 实际的 TX 钩子只从 ISR 中调用。NULL 保持旧有的
     * 直接发送行为（由调用方发送）。
     */
    void (*request_service)(void* user);

    /* 可选的诊断/支持函数（可为 NULL） */

#if IOLINK_PHY_FEATURE_WAKEUP
    /**
     * @brief 检测唤醒脉冲（C/Q 线路上的 80µs 脉冲）
     *
     * 能力标志：实现该钩子的 PHY 不接管 SIO
     * 握手，让 DLL 驱动 SIO/SDCI 切换。当它为 NULL 时，
     * PHY 自行接管 SIO/唤醒握手，因此 DLL 必须停留在 SDCI
     * （切换到 SIO 会锁存且无法返回：NULL 的 PHY 从不上报
     * 唤醒）。
     * @return 当前窗口内检测到唤醒脉冲返回 1，否则返回 0
     */
    int (*detect_wakeup)(void* user);
#endif

#if IOLINK_PHY_FEATURE_SIO_CQ
    /**
     * @brief 手动设置 C/Q 线路状态（用于 SIO 推挽）
     *
     * 为可移植性而声明；DLL 当前没有调用点（PHY
     * 负责 SIO 线路状态）。
     * @param state 0 为低电平，1 为高电平
     */
    void (*set_cq_line)(void* user, uint8_t state);
#endif

    /**
     * @brief 可选：当前活跃的物理故障，为
     *        IOLINK_PHY_FAULT_* 标志的位掩码（无故障时为 0）。
     *
     * 这是诊断通道：PHY 描述物理上
     * 出了什么问题，无需了解 IO-Link EventCodes。DLL
     * 从 iolink_dll_process() 轮询它；0->1 边沿会触发映射的事件
     * （UNDERVOLTAGE -> 0x5111 Warning，OVERVOLTAGE -> 0x5110 Warning，
     * OVERTEMP -> 0x4000 Error，SHORT_CIRCUIT -> 0x7710 Error）一次，以及
     * 1->0 边沿会清除它，使事件引擎能够发出“disappears”。该钩子为
     * NULL 时不做物理诊断。
     * @return 活跃故障标志。
     */
    uint8_t (*get_faults)(void* user);
} iolink_phy_api_t;

/** @} */ /* iolinki_phy 结束 */

#endif  // IOLINK_PHY_H
