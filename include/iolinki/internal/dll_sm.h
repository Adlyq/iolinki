/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_SM_H
#define IOLINK_DLL_SM_H

#include "iolinki/dll.h"

#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file dll_sm.h
 * @brief DLL 状态机骨架表：state × event -> action。
 *
 * 表只描述"在哪个状态、收到哪类已校验帧、先进入哪个状态、调用哪个
 * 动作函数"。内容语义仍在动作函数里（表管骨架 + 函数管动作），因为
 * 纯表装不下长度/通道/兼容开关这类连续条件。
 *
 * @ref DLL_SM_ROWS 是唯一事实源：`src/dll_sm.c` 用指定初始化器把它展开成
 * `g_dll_sm[state][event]` 常量表；`tools/gen_dll_statemachine.py`
 * 解析同一份宏行，生成 mermaid、CSV 与覆盖测试。新增/移动迁移时只改
 * 这里，脚本的 `--check` 会验证 7×7 网格无空洞、无重复。
 */

/** @brief 已校验帧的事件分类（互斥，按 dll_sm_classify() 的顺序判定）。 */
typedef enum
{
    DLL_EV_FALLBACK_CMD = 0, /**< FALLBACK MasterCommand（0x5A，3 字节） */
    DLL_EV_PAGE_READ_T0,     /**< Type-0 页通道读（启动探测 / 复位请求） */
    DLL_EV_PAGE_WRITE_ADDR0, /**< Type-0 页通道写地址 0（MasterCommand） */
    DLL_EV_PAGE_WRITE,       /**< Type-0 页通道写其他地址（DPP 参数） */
    DLL_EV_OD_WRITE_T0,      /**< Type-0 诊断/ISDU 通道写（仅 CKS 应答） */
    DLL_EV_T0_SHORT,         /**< Type-0 非页通道读（OD/CKS 应答） */
    DLL_EV_PD_EXCHANGE,      /**< Type-1/2 过程数据交换 */
    DLL_EV_COUNT             /**< 事件数量 */
} iolink_dll_event_t;

/** @brief 进入状态字段的哨兵：保持当前状态。 */
#define DLL_SM_STATE_KEEP 0xFFU

/** @brief 一个表项的动作函数：返回 false 表示跳过帧尾清理。 */
typedef bool (*iolink_dll_sm_action_t)(iolink_dll_main_t* ctx);

/** @brief state × event 表项：先迁移状态（可选），再执行动作。 */
typedef struct
{
    iolink_dll_sm_action_t action; /**< 动作函数（非 NULL） */
    uint8_t enter_state;           /**< 迁移目标，或 @ref DLL_SM_STATE_KEEP */
} iolink_dll_sm_cell_t;

/**
 * @brief 状态机行的唯一事实源。
 *
 * 参数：状态、事件、动作函数、进入状态（@ref DLL_SM_STATE_KEEP = 不变）、
 * 规范条款/说明（只被生成脚本读取，C 展开时丢弃）。
 */
/* clang-format off */
#define DLL_SM_ROWS(X)                                                                          \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_FALLBACK_CMD, dll_sm_fallback,                            \
      IOLINK_DLL_STATE_PREOPERATE, "Table B.2/43：FALLBACK 命令，T_FBD 由主泵计时")             \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_PAGE_READ_T0, dll_sm_page_probe,                          \
      IOLINK_DLL_STATE_PREOPERATE, "T1 / Table 47：启动探测页读，应答 DPP1 字节")               \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_master_command,                  \
      IOLINK_DLL_STATE_PREOPERATE, "Table B.2：MasterCommand 页写")                             \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_PAGE_WRITE, dll_sm_od_write_t0,                           \
      IOLINK_DLL_STATE_PREOPERATE, "7.3.5.3：DPP 参数写，仅 CKS")                               \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_OD_WRITE_T0, dll_sm_od_write_t0,                          \
      IOLINK_DLL_STATE_PREOPERATE, "Figure A.5：Type-0 OD 写，仅 CKS")                          \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_T0_SHORT, dll_sm_type0_read,                               \
      IOLINK_DLL_STATE_PREOPERATE, "7.3.6：OPERATE 前的 Type-0 ISDU/诊断读")                    \
    X(IOLINK_DLL_STATE_STARTUP, DLL_EV_PD_EXCHANGE, dll_sm_none,                                 \
      IOLINK_DLL_STATE_PREOPERATE, "不可达：校验前类型门控只放行 Type-0")                       \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_FALLBACK_CMD, dll_sm_fallback,                      \
      IOLINK_DLL_STATE_PREOPERATE, "Table B.2/43：FALLBACK 命令，T_FBD 由主泵计时")             \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_PAGE_READ_T0, dll_sm_page_probe,                    \
      IOLINK_DLL_STATE_PREOPERATE, "T1 / Table 47：启动探测页读，应答 DPP1 字节")               \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_master_command,            \
      IOLINK_DLL_STATE_PREOPERATE, "Table B.2：MasterCommand 页写")                             \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_PAGE_WRITE, dll_sm_od_write_t0,                     \
      IOLINK_DLL_STATE_PREOPERATE, "7.3.5.3：DPP 参数写，仅 CKS")                               \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_OD_WRITE_T0, dll_sm_od_write_t0,                    \
      IOLINK_DLL_STATE_PREOPERATE, "Figure A.5：Type-0 OD 写，仅 CKS")                          \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_T0_SHORT, dll_sm_type0_read,                         \
      IOLINK_DLL_STATE_PREOPERATE, "7.3.6：OPERATE 前的 Type-0 ISDU/诊断读")                    \
    X(IOLINK_DLL_STATE_AWAITING_COMM, DLL_EV_PD_EXCHANGE, dll_sm_none,                           \
      IOLINK_DLL_STATE_PREOPERATE, "不可达：校验前类型门控只放行 Type-0")                       \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_FALLBACK_CMD, dll_sm_fallback,                         \
      DLL_SM_STATE_KEEP, "Table B.2/43：FALLBACK 命令，T_FBD 由主泵计时")                       \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_PAGE_READ_T0, dll_sm_type0_read,                       \
      DLL_SM_STATE_KEEP, "7.3.6：PREOPERATE 的 Type-0 读是 ISDU/诊断流量")                      \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_master_command,               \
      DLL_SM_STATE_KEEP, "Table B.2：MasterCommand 页写")                                       \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_PAGE_WRITE, dll_sm_od_write_t0,                        \
      DLL_SM_STATE_KEEP, "7.3.5.3：DPP 参数写，仅 CKS")                                         \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_OD_WRITE_T0, dll_sm_od_write_t0,                       \
      DLL_SM_STATE_KEEP, "Figure A.5：Type-0 OD 写，仅 CKS")                                    \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_T0_SHORT, dll_sm_type0_read,                            \
      DLL_SM_STATE_KEEP, "7.3.6：Type-0 ISDU/诊断读")                                           \
    X(IOLINK_DLL_STATE_PREOPERATE, DLL_EV_PD_EXCHANGE, dll_sm_none,                              \
      DLL_SM_STATE_KEEP, "不可达：校验前类型门控只放行 Type-0")                                 \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_FALLBACK_CMD, dll_sm_master_command,                    \
      DLL_SM_STATE_KEEP, "Table B.2：ESTAB_COM 中 MasterCommand 立即应用")                      \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_PAGE_READ_T0, dll_sm_type0_read,                        \
      IOLINK_DLL_STATE_OPERATE, "C5：首个周期帧进入 OPERATE")                                   \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_master_command,                \
      DLL_SM_STATE_KEEP, "Table B.2：MasterCommand 不推进状态")                                 \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_PAGE_WRITE, dll_sm_od_write_t0,                         \
      IOLINK_DLL_STATE_OPERATE, "C5：首个周期帧进入 OPERATE")                                   \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_OD_WRITE_T0, dll_sm_od_write_t0,                        \
      IOLINK_DLL_STATE_OPERATE, "C5：首个周期帧进入 OPERATE")                                   \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_T0_SHORT, dll_sm_type0_read,                             \
      IOLINK_DLL_STATE_OPERATE, "C5：首个周期帧进入 OPERATE")                                   \
    X(IOLINK_DLL_STATE_ESTAB_COM, DLL_EV_PD_EXCHANGE, dll_sm_pd_exchange,                        \
      IOLINK_DLL_STATE_OPERATE, "C5：首个周期帧建立通信并进入 OPERATE")                         \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_FALLBACK_CMD, dll_sm_fallback,                            \
      DLL_SM_STATE_KEEP, "Table 47 T9：FALLBACK 后由 T_FBD 切到 SIO")                           \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_PAGE_READ_T0, dll_sm_operate_page_read,                   \
      DLL_SM_STATE_KEEP, "Table 57/67：Type-0 页读是复位请求（兼容开关除外）")                  \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_master_command,                  \
      DLL_SM_STATE_KEEP, "Table B.2：MasterCommand 页写")                                       \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_PAGE_WRITE, dll_sm_od_write_t0,                           \
      DLL_SM_STATE_KEEP, "7.3.5.3：DPP 参数写，仅 CKS")                                         \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_OD_WRITE_T0, dll_sm_od_write_t0,                          \
      DLL_SM_STATE_KEEP, "Figure A.5：Type-0 OD 写，仅 CKS")                                    \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_T0_SHORT, dll_sm_type0_read,                               \
      DLL_SM_STATE_KEEP, "7.3.6：Type-0 ISDU/诊断读")                                           \
    X(IOLINK_DLL_STATE_OPERATE, DLL_EV_PD_EXCHANGE, dll_sm_pd_exchange,                          \
      DLL_SM_STATE_KEEP, "C5：周期数据交换")                                                    \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_FALLBACK_CMD, dll_sm_none, DLL_SM_STATE_KEEP,            \
      "Table 47 T9：FALLBACK 期间不应答，等待 T_FBD")                                            \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_PAGE_READ_T0, dll_sm_none, DLL_SM_STATE_KEEP,            \
      "Table 47 T9：FALLBACK 期间不应答，等待 T_FBD")                                            \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_none, DLL_SM_STATE_KEEP,        \
      "Table 47 T9：FALLBACK 期间不应答，等待 T_FBD")                                            \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_PAGE_WRITE, dll_sm_none, DLL_SM_STATE_KEEP,              \
      "Table 47 T9：FALLBACK 期间不应答，等待 T_FBD")                                            \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_OD_WRITE_T0, dll_sm_none, DLL_SM_STATE_KEEP,             \
      "Table 47 T9：FALLBACK 期间不应答，等待 T_FBD")                                            \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_T0_SHORT, dll_sm_none, DLL_SM_STATE_KEEP,                 \
      "Table 47 T9：FALLBACK 期间不应答，等待 T_FBD")                                            \
    X(IOLINK_DLL_STATE_FALLBACK, DLL_EV_PD_EXCHANGE, dll_sm_none, DLL_SM_STATE_KEEP,             \
      "不可达：校验前类型门控只放行 Type-0")                                                     \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_FALLBACK_CMD, dll_sm_none, DLL_SM_STATE_KEEP,       \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")                                          \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_PAGE_READ_T0, dll_sm_none, DLL_SM_STATE_KEEP,       \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")                                          \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_PAGE_WRITE_ADDR0, dll_sm_none, DLL_SM_STATE_KEEP,   \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")                                          \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_PAGE_WRITE, dll_sm_none, DLL_SM_STATE_KEEP,         \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")                                          \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_OD_WRITE_T0, dll_sm_none, DLL_SM_STATE_KEEP,        \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")                                          \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_T0_SHORT, dll_sm_none, DLL_SM_STATE_KEEP,            \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")                                          \
    X(IOLINK_DLL_STATE_COMM_DISABLED, DLL_EV_PD_EXCHANGE, dll_sm_none, DLL_SM_STATE_KEEP,        \
      "10.7.5：Back-to-box 后停止服务，帧在校验前丢弃")
/* clang-format on */

/**
 * @brief 帧事件之外的迁移（初始化、唤醒、定时器、管理命令）。
 *
 * 这些迁移由主上下文与 ISR 里的显式调用触发，不在 `g_dll_sm` 网格内；本表只是
 * 它们的声明，`tools/gen_dll_statemachine.py` 用它生成
 * `docs/CODE_FLOW.md` 的迁移表并在 `--check` 时校验，避免文档与代码漂移。
 * C 侧不展开该宏。
 *
 * 参数：迁移、触发条件、代码位置。
 */
/* clang-format off */
#define DLL_SM_ADMIN_ROWS(X)                                                                     \
    X("初始化 → STARTUP", "`iolink_dll_init()`", "`dll_set_state(STARTUP)`")                     \
    X("STARTUP → AWAITING_COMM", "SIO + `detect_wakeup()>0`", "`dll_handle_sio_wakeup()`")       \
    X("STARTUP/AWAITING → PREOPERATE", "首个有效帧",                                              \
      "`g_dll_sm` 表项的 `enter_state` / `dll_isr_plan_action()`（ENTER_PREOPERATE）")            \
    X("PREOPERATE → ESTAB_COM", "0x99 DeviceOperate",                                            \
      "`dll_sm_master_command()` → `dll_apply_master_command()`")                                \
    X("PREOPERATE → OPERATE", "0x98 PDOperate", "`dll_apply_master_command()`")                  \
    X("ESTAB_COM → OPERATE", "首个周期帧（Type-1/2 或任一 Type-0 周期帧）",                       \
      "`g_dll_sm` 表项的 `enter_state`")                                                          \
    X("→ PREOPERATE", "0x9A DevicePreoperate", "`dll_apply_master_command()`")                   \
    X("→ FALLBACK", "0x5A Fallback（需唤醒能力）",                                                \
      "`dll_handle_fallback_command()` / `dll_isr_plan_action()`")                               \
    X("FALLBACK/OPERATE/ESTAB/PREOP → STARTUP", "T_FBD 到期，切 SIO（UART 速率不变）",        \
      "`dll_timer_expire_fbd()`")                                                                \
    X("AWAITING/STARTUP → STARTUP", "T_DSIO 到期，切 SIO（UART 速率不变）",                      \
      "`dll_timer_expire_dsio()`")                                                               \
    X("任意 → STARTUP", "0x97 DeviceStartup 或非 OPERATE/ESTAB 的类型错误",                      \
      "`dll_protocol_error_to_startup()` / `dll_handle_frame_error()`")                          \
    X("任意（Back-to-box 挂起后） → COMM_DISABLED", "SystemCommand 0x83 之后又服务 ≥3 帧",      \
      "`dll_frame_drop_if_stopped()`")                                                           \
    X("OPERATE → STARTUP", "Type-0 页读且未开 IOLINK_CAP_COMPAT_T0_READ（复位请求）",             \
      "`dll_sm_operate_page_read()` / `dll_isr_plan_action()`（RESET_STARTUP）")
/* clang-format on */

/**
 * @brief 查询表项（测试/生成物使用；运行时由 dll_sm.c 直接访问常量表）。
 * @return 命中的表项，越界时返回 NULL。
 */
const iolink_dll_sm_cell_t* IOLINK_NULLABLE iolink_dll_sm_cell(iolink_dll_state_t state,
                                                               iolink_dll_event_t event);

IOLINK_ASSUME_NONNULL_END
#endif /* IOLINK_DLL_SM_H */
