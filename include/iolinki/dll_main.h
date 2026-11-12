/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_MAIN_H
#define IOLINK_DLL_MAIN_H

#include "iolinki/dll_shared.h"
#include "iolinki/data_storage.h"

/**
 * @file dll_main.h
 * @brief 主上下文独占的 DLL 状态。
 *
 * 共享区以 @ref iolink_dll_main_t::shared 内嵌，主上下文通过
 * `ctx->shared.<field>` 访问。ISR 独占字段不在本类型中：
 * 主上下文引用它们是编译错误。
 */
struct iolink_dll_main
{
    iolink_dll_shared_t shared;

    uint32_t last_valid_frame_ms; /**< 最后一个校验和/类型有效的帧的时间戳 */

    volatile uint32_t master_last_activity_ms; /**< 最后一次帧结束 / RX 超时的时间戳 */

    uint8_t master_lost_cycles; /**< 判定 master_lost 前可错过的周期数；0 = 禁用 */

    volatile uint8_t master_lost_count; /**< 已错过的周期数，饱和于 0xFF */

    volatile bool master_lost; /**< 主站通信丢失（应用层提示） */

    bool wakeup_seen; /**< 检测到唤醒（若 PHY 支持） */

    volatile bool back_to_box_pending; /**< 已请求 Back-to-box；正在倒计帧数 */

    volatile uint8_t back_to_box_cycles; /**< 自请求 Back-to-box 以来已见的帧数 */

    volatile uint8_t fallback_clear_request; /**< 已应用非 FALLBACK 命令：清除 T_FBD */

    volatile uint8_t phy_faults_rearm; /**< 事件引擎已复位：重新启动故障锁存 */

    uint8_t pd_out_len; /**< 输出过程数据长度 */

    uint8_t od_len; /**< 流式路径的 OD 长度（1 或 2 字节）。 */

    iolink_frame_rec_t isr_frame_slot[IOLINK_MBX_SLOTS]; /**< 邮箱环存储 */

    volatile uint8_t frame_replied_by_isr; /**< 当前处理帧对应记录中 eager 标志的
                                       主上下文快照。 */

    uint32_t pd_in_skips; /**< 跳过的 PD-in 发布：所有空闲缓冲区均已被租用 */

    /* ISR 侧的 ISDU 暂存 + 应答快照（单写者模型）。ISR
     * 将写 OD 字节暂存到环中，只读取应答快照；
     * 主泵拥有 ctx->isdu，并在其变化时刷新快照。 */
    iolink_od_rec_t isr_od_slot[IOLINK_OD_SLOTS]; /**< 暂存写环存储 */

    volatile uint8_t startup_notification_pending; /**< ISR 进入了 STARTUP；主
                                                        上下文必须上报该事件 */

    iolink_dll_state_t last_notified_state; /**< 已交付给 state_cb 的状态
                                                  （仅主上下文） */

    uint8_t isr_tx_next; /**< 主泵下一个填充的槽位（0/1） */

    bool pd_valid; /**< 当前 PD_In 有效状态 */

    uint8_t master_cycle_time; /**< DPP1 0x01，由主站写入的周期时间字节
                                    （Table B.1/B.3）；0 = 尚未写入 */

    uint32_t min_cycle_time_us; /**< 最小周期时间，单位为微秒 */

    uint32_t min_cycle_time_app_us; /**< 周期时间的应用层下限 */

    bool enforce_timing; /**< 启用定时检查（t_ren / t_cycle） */

    uint32_t t_ren_limit_us; /**< 当前 t_ren 限制，单位为微秒 */

    bool t_ren_override; /**< 为 true 时使用覆盖后的 t_ren 限制 */

    uint32_t t_pd_delay_us; /**< 上电延时（t_pd），单位为微秒 */

    /* 可变 PD 支持（用于 Type 1_V 和 2_V） */
    uint8_t pd_in_len_current; /**< 当前运行时 PD_In 长度 */

    uint8_t pd_in_len_max; /**< 允许的 PD_In 最大长度 */

    uint8_t pd_out_len_max; /**< 允许的 PD_Out 最大长度 */

    /* SIO 模式 */
    iolink_phy_mode_t phy_mode; /**< 当前工作模式（SDCI 与 SIO） */

    /* 波特率支持 */
    iolink_baudrate_t baudrate; /**< 协商后的波特率（COM1-COM3） */

    iolink_baudrate_t operating_baudrate; /**< 工作 COM；未配置时为 IOLINK_BAUDRATE_UNSET */

    /* 统一帧组装：主站请求为 MC + CKT + PD-out + OD，因此
       按编译期最大值确定尺寸（原为固定 48，一旦
       OD 宽度可达 IOLINK_OD_MAX_SIZE 就显得过小）。 */
    uint8_t frame_buf[IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U];

    uint8_t frame_index; /**< 组装中的当前字节索引 */

    uint8_t req_len; /**< 当前帧类型的期望长度 */

    uint64_t last_frame_us; /**< 最后帧开始的微秒时间戳 */

    uint64_t last_cycle_start_us; /**< 最后周期开始的微秒时间戳 */

    uint32_t t_byte_limit_us; /**< 字节间超时限制，单位为微秒 */

    uint64_t wakeup_deadline_us; /**< 唤醒后最早可接受帧的时间 */

    uint64_t t_pd_deadline_us; /**< 上电后最早可接受帧的时间 */

    uint64_t dsio_deadline_ms; /**< 无有效报文时回退到 SIO 的截止时间 */

    uint64_t fallback_deadline_ms; /**< 完成 FALLBACK MasterCommand 的截止时间 */

    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /**< 输出 PD 缓冲区（主站 -> 设备） */

    bool pd_out_seen; /**< 已收到 PDout 帧（B.2.24） */

    /* 错误计数器与统计 */
    uint32_t crc_errors; /**< 累计 CRC 错误数 */

    uint32_t idle_timeouts; /**< 1 s 空闲链路诊断（主站已暂停） */

    uint32_t timing_errors; /**< 累计定时违规次数 */

    uint32_t t_ren_violations; /**< t_ren 违规次数 */

    uint32_t t_cycle_violations; /**< t_cycle 违规次数 */

    uint32_t t_byte_violations; /**< 字节间定时违规次数 */

    uint32_t t_pd_violations; /**< t_pd 违规次数 */

    uint8_t retry_count; /**< 当前交换的重试计数器 */

    uint32_t total_retries; /**< 累计重试次数 */

    uint8_t max_retries; /**< 配置的最大重试次数（默认 3） */

    uint32_t voltage_faults; /**< 累计电压故障数（走 get_faults() 时按故障
                                  边沿计数，走旧路径时按轮询周期计数） */

    uint32_t overtemp_faults; /**< 累计过温故障数（走 get_faults() 时按故障
                                   边沿计数，走旧路径时为 0） */

    uint32_t short_circuits; /**< 累计短路次数（走 get_faults() 时按故障
                                  边沿计数，走旧路径时按轮询周期计数） */

    uint8_t phy_faults_last; /**< 最后一次 get_faults() 的位掩码（用于边沿检测） */

    uint8_t sio_fallback_threshold; /**< 进入 SIO 模式的回退阈值（默认 3） */

    /* 定时统计 */
    uint64_t last_response_us; /**< 最后应答的微秒时间戳 */

    uint32_t response_time_us; /**< 实测协议栈应答时间（t2） */

#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t ds; /**< 数据存储引擎 */
#endif

    iolink_dll_state_cb_t state_cb; /**< 可选的状态变更钩子（由核心层设置） */

    void* state_cb_user; /**< 传给 state_cb 的用户指针 */
};

#endif /* IOLINK_DLL_MAIN_H */
