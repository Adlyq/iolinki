/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_MAIN_H
#define IOLINK_DLL_MAIN_H

#include "iolinki/internal/dll_shared.h"
#include "iolinki/data_storage.h"

#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN

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

    volatile uint32_t master_last_activity_ms; /**< 最后一次帧结束 / RX 超时的时间戳 */

    uint8_t master_lost_cycles; /**< 判定 master_lost 前可错过的周期数；0 = 禁用 */

    volatile uint8_t master_lost_count; /**< 已错过的周期数，饱和于 0xFF */

    volatile bool master_lost; /**< 主站通信丢失（应用层提示） */

    bool log_locked; /**< 日志汇点已由初始化配置固定：之后再改写会与数据 ISR
                          的日志读取竞争。 */

    uint32_t master_lost_activity_ms; /**< 上次武装 MASTER_LOST 时的活动时间戳
                                           （用于识别新活动边沿） */

    volatile bool back_to_box_pending; /**< 已请求 Back-to-box；正在倒计帧数 */

    volatile uint8_t back_to_box_cycles; /**< 自请求 Back-to-box 以来已服务的
                                             MasterCycle 数（只由主上下文推进） */

    uint8_t isdu_pub_gen; /**< ISDU 应答代数（每份新应答翻转） */

    uint8_t isdu_pub_state; /**< 上次看到的 ISDU 状态（检测新应答边沿） */

    iolink_frame_rec_t isr_frame_slot[IOLINK_MBX_SLOTS]; /**< 邮箱环存储 */

    volatile uint8_t frame_replied_by_isr; /**< 当前处理帧对应记录中 eager 标志的
                                       主上下文快照。 */

    uint32_t pd_in_skips; /**< 跳过的 PD-in 发布：所有空闲缓冲区均已被租用 */

    iolink_dll_state_t last_notified_state; /**< 已交付给 state_cb 的状态
                                                  （仅主上下文） */

    uint8_t isr_tx_next; /**< 主泵下一个填充的槽位（0/1） */

    uint32_t min_cycle_time_us; /**< 最小周期时间，单位为微秒 */

    bool enforce_timing; /**< 启用定时检查（t_ren / t_cycle） */

    /* SIO 模式 */
    iolink_phy_mode_t phy_mode; /**< 当前工作模式（SDCI 与 SIO） */

    /* 统一帧组装：主站请求为 MC + CKT + PD-out + OD，因此
       按编译期最大值确定尺寸（原为固定 48，一旦
       OD 宽度可达 IOLINK_OD_MAX_SIZE 就显得过小）。 */
    uint8_t frame_buf[IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U];

    uint8_t frame_index; /**< 组装中的当前字节索引 */

    uint8_t req_len; /**< 当前帧类型的期望长度 */

    /* 截止时间表。所有软件超时只在这里保存绝对截止时间（us），
     * 主泵另存一个 next_deadline 用于单点比较。表项语义、规范条款与
     * 同刻优先级见 include/iolinki/dll.h 的 iolink_timer_id_t 与
     * iolink_dll_timer_contract()。 */
    iolink_timer_t timers[IOLINK_TIMER_COUNT]; /**< 静态截止时间表 */

    uint64_t next_deadline; /**< 所有 DEADLINE 类 timer 的最早截止时间（0 = 无） */

    uint32_t t_byte_limit_us; /**< 字节间超时限制，单位为微秒 */

    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /**< 输出 PD 缓冲区（主站 -> 设备） */

    /* 错误计数器与统计 */
    uint32_t crc_errors; /**< 累计 CRC 错误数 */

    uint32_t idle_timeouts; /**< 1 s 空闲链路诊断（主站已暂停） */

    uint32_t timing_errors; /**< 累计定时违规次数 */

    uint32_t t_ren_violations; /**< t_ren 违规次数 */

    uint32_t t_cycle_violations; /**< t_cycle 违规次数 */

    uint32_t t_byte_violations; /**< 字节间定时违规次数 */

    uint32_t t_pd_violations; /**< t_pd 违规次数 */

    uint32_t t_pd_discards; /**< 在 t_PD 保护窗口内丢弃的邮箱帧数 */

    uint32_t reply_plan_overflows; /**< 因计划槽位耗尽而未发布的计划条目数 */

    uint32_t invalid_frames; /**< 判为无效的已收帧：CRC 校验失败与协议错误转
                                  STARTUP 各计一次（不是重传次数） */

    uint32_t voltage_faults; /**< 累计电压故障数（走 get_faults() 时按故障
                                  边沿计数，走旧路径时按轮询周期计数） */

    uint32_t overtemp_faults; /**< 累计过温故障数（走 get_faults() 时按故障
                                   边沿计数，走旧路径时为 0） */

    uint32_t short_circuits; /**< 累计短路次数（走 get_faults() 时按故障
                                  边沿计数，走旧路径时按轮询周期计数） */

    /* 定时统计 */
    uint64_t last_response_us; /**< 最后应答（或 ISR 完全消费）的帧的
                                    微秒时间戳 */

    uint8_t isr_consumed_seq_seen; /**< 已消费的 ISR 消费帧序号
                                        （与 shared.i_isr_consumed_seq 对应） */

#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t ds; /**< 数据存储引擎 */
#endif

    iolink_dll_state_cb_t IOLINK_NULLABLE state_cb; /**< 可选的状态变更钩子（由核心层设置） */

    void* IOLINK_OPAQUE IOLINK_NULLABLE
        state_cb_user; /**< 传给 state_cb 的调用方上下文（DLL 不解释） */
};

/**
 * @name 截止时间表内部操作（库私有，dll_ 前缀）
 *
 * 仅供 src/dll*.c（含 dll_isr*.c）与单元测试使用，不属于公开 iolink_ API。
 * `deadline == 0` 表示未装载；装载与取消都只改表和 next_deadline。
 * @{
 */
void dll_timer_arm(iolink_dll_main_t* ctx, iolink_timer_id_t id, uint64_t deadline_us);
void dll_timer_cancel(iolink_dll_main_t* ctx, iolink_timer_id_t id);
bool dll_timer_armed(const iolink_dll_main_t* ctx, iolink_timer_id_t id);
uint64_t dll_timer_deadline(const iolink_dll_main_t* ctx, iolink_timer_id_t id);

/** @brief 回绕安全判定：@p now_us 已到达（>=）@p deadline_us。 */
bool dll_time_reached(uint64_t now_us, uint64_t deadline_us);

/** @brief 回绕安全判定：@p now_us 已越过（>）@p deadline_us。 */
bool dll_time_passed(uint64_t now_us, uint64_t deadline_us);

/** @brief 该 timer 已装载且已到期。 */
bool dll_timer_due(const iolink_dll_main_t* ctx, iolink_timer_id_t id, uint64_t now_us);
/** @} */

IOLINK_ASSUME_NONNULL_END
#endif /* IOLINK_DLL_MAIN_H */
