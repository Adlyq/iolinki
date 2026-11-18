/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_H
#define IOLINK_DLL_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/phy.h"
#include "iolinki/config.h"
#include "iolinki/port.h"
#include "iolinki/internal/ring.h"
#include "iolinki/types.h"

IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file dll.h
 * @brief IO-Link 数据链路层（DLL）实现 —— 高级/底层接口
 *
 * 应用通常只包含 `iolink/device.h`：设备层把 DLL、ISDU、OD、Data Storage
 * 与事件组合成一个上下文，并在 `iolink_device_process()` 里完成调度。
 * 本头文件面向需要自定义主循环、直接驱动帧泵或读取 DLL 统计的集成；
 * 其中的上下文类型由 `iolink/internal/` 提供完整布局，不要直接依赖。
 */

/**
 * @defgroup iolinki_dll 数据链路层（DLL）
 * @brief 帧定时、M 序列交换、模式控制以及 DLL 统计。
 * @{
 */

/**
 * @brief IO-Link DLL 状态机状态
 */
typedef enum
{
    IOLINK_DLL_STATE_STARTUP = 0U,       /**< 等待唤醒请求 */
    IOLINK_DLL_STATE_AWAITING_COMM = 1U, /**< 已检测到唤醒，等待首帧 */
    IOLINK_DLL_STATE_PREOPERATE = 2U,    /**< 处理 ISDU（Type 0） */
    IOLINK_DLL_STATE_ESTAB_COM = 3U,     /**< 建立通信（转入 OPERATE） */
    IOLINK_DLL_STATE_OPERATE = 4U,       /**< 循环数据交换已激活 */
    IOLINK_DLL_STATE_FALLBACK = 5U,      /**< 错误恢复 / 回退 */
    IOLINK_DLL_STATE_COMM_DISABLED = 6U, /**< Back-to-box：通信停止直至复位 */
    IOLINK_DLL_STATE_COUNT = 7U          /**< 状态数量（状态机表的行维度） */
} iolink_dll_state_t;

/**
 * @brief DLL 状态机状态改变时调用的可选钩子。
 *
 * 通知基于电平：钩子由
 * iolink_dll_flush_state_callbacks() 以 *当前* 状态调用，条件是它
 * 与上次交付的状态不同。因此回调绝不会运行在
 * 中断上下文中，最终状态绝不会被漏掉，而中间状态
 * 若在两次交付之间进入又离开，会被合并进
 * 后一个状态；例外是 ISR 侧进入 STARTUP 的迁移，它总会
 * 被交付（Table 45 T11 要求该复位能到达系统管理）。
 * 由核心层用于驱动应用生命周期回调。
 *
 * 该钩子在主上下文中运行，但也可能从
 * iolink_dll_process() 内部被调用（主上下文的状态迁移会立即通知，
 * 而此时帧分发仍在栈上）。因此钩子不得对
 * iolink_dll_* / iolink_device_* 做重入调用。
 */
/** @brief 状态变更钩子。
 *
 * @p user 是调用方自己的上下文：DLL 只保存并原样回传，不解释其内容
 * （DLL 层与上层的设备类型无编译期依赖）。 */
typedef void (*iolink_dll_state_cb_t)(void* IOLINK_OPAQUE IOLINK_NULLABLE user,
                                      iolink_dll_state_t state);

/** 本库成帧的最大 M 序列长度（请求或应答），以字节计。 */
#define IOLINK_M_SEQ_MAX_LEN (IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U)

/* PD 应答发送缓冲区：[OD][PD-in][CKS]。按配置的 PD-in 长度
 * 加上 OD 头与尾部校验和来确定尺寸。 */
#define IOLINK_PD_REPLY_MAX (IOLINK_OD_MAX_SIZE + IOLINK_PD_IN_MAX_SIZE + 1U)

/** 应答暂存槽位：0/1 供主泵使用（交替），2 供 ISR 使用。 */
#define IOLINK_TX_ISR_SLOT 2U
#define IOLINK_TX_SLOTS (IOLINK_TX_ISR_SLOT + 1U)
IOLINK_STATIC_ASSERT(IOLINK_TX_SLOTS == IOLINK_TX_ISR_SLOT + 1U,
                     "the ISR reply slot must be the last reply slot");

/**
 * @brief 数据 ISR 也会更新的计数器。
 *
 * 在 `IOLINK_STATS_MODE=SPLIT` 下，各上下文各自保留一份副本，因此
 * 自增操作不会在其读取与写回之间被抢占而丢失；
 * 对外的统计会对两组计数求和。在 `SHARED`（默认）下，ISR 写入
 * 与主泵相同的字段，可能丢失一次更新。
 */
typedef struct
{
    uint32_t framing_errors; /**< 被 ISR 成帧引擎拒绝的帧 */
    uint32_t timeout_errors; /**< ISR 观察到的 Timer A 超时 */
    uint32_t isr_tx_dropped; /**< ISR 无法发送的应答 */
} iolink_dll_isr_stats_t;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
/** @brief 在 ISR 上下文中执行自增的计数器槽位（参数是共享区）。 */
#define IOLINK_STAT_ISR(shared, field) ((shared)->isr_stat.field)
/** @brief 包含 ISR 自有计数的计数值（只读，参数是主上下文）。 */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->shared.field + (ctx)->shared.isr_stat.field)
#else
/** @brief 在 ISR 上下文中执行自增的计数器槽位（参数是共享区）。 */
#define IOLINK_STAT_ISR(shared, field) ((shared)->field)
/** @brief 包含 ISR 自有计数的计数值（只读，参数是主上下文）。 */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->shared.field)
#endif

/**
 * @name ISDU 读取视图打包
 *
 * 主泵将已成帧的应答视图作为一个 `IOLINK_ATOMIC_UINT_T` 发布，
 * 使数据 ISR 能在一次访问中读到一致的 {length, busy, buffer} 元组。
 * ISR 保留它在 START 处采纳的最后一个字的副本；任何变化（新的
 * 应答，或 Busy 迁移）都会打断 FlowCTRL 序列，这正是
 * 增量视图原本的行为。
 *
 * 位 10 是应答代数（generation）：主泵每产生一份*新*应答就翻转它，
 * 即使新应答的长度与缓冲区选择与上一份完全相同。ISR 因此能识别
 * "写后立刻读"（同一缓冲、同一长度、内容已变），不会继续使用
 * 失效前采纳的段索引。
 * @{
 */
#define IOLINK_ISDU_PUB_LEN(word) ((uint8_t) ((word) & 0xFFU))
#define IOLINK_ISDU_PUB_BUSY(word) ((uint8_t) (((word) >> 8) & 0x01U))
#define IOLINK_ISDU_PUB_SEL(word) ((uint8_t) (((word) >> 9) & 0x01U))
#define IOLINK_ISDU_PUB_GEN(word) ((uint8_t) (((word) >> 10) & 0x01U))
#define IOLINK_ISDU_PUB_WORD_GEN(len, busy, sel, gen)                                            \
    ((IOLINK_ATOMIC_UINT_T) (uint8_t) (len) | ((IOLINK_ATOMIC_UINT_T) ((busy) ? 1U : 0U) << 8) | \
     ((IOLINK_ATOMIC_UINT_T) ((sel) ? 1U : 0U) << 9) |                                           \
     ((IOLINK_ATOMIC_UINT_T) ((gen) ? 1U : 0U) << 10))
#define IOLINK_ISDU_PUB_WORD(len, busy, sel) IOLINK_ISDU_PUB_WORD_GEN(len, busy, sel, 0U)
/** @} */

/**
 * @name 应答计划
 *
 * 主泵是 M-sequence 内容语义的唯一所有者：它把"哪种请求、用哪段
 * 字节、多长、以什么方式应答"编译成一张计划表。表内容只取决于
 * {M-sequence 类型, OD 宽度, 能力位}，三项都在初始化时定义，因此计划表
 * 在 `iolink_dll_init()` / `iolink_device_init()` 内编译一次后不再改变；
 * 数据 ISR 只做 查计划 -> 取源字节 -> 追加 CKS -> 喂字节，不再解读请求内容。
 *
 * 发布只有一个"就绪"标志 `reply_plans_ready`：编译前撤下、写完后置位，
 * ISR 入口只在置位时进入传输泵，因此初始化窗口内到达的帧不会被处理。
 * 该标志只保护**入口**：已经在泵内的调用不受它保护，所以重建必须满足
 * "数据 ISR 静止"这一前提（初始化/重新初始化时中断尚未启用）。正因为
 * 重建不会与查表并发，查表不需要任何序号校验。
 *
 * 计划未命中的语义：ISR 不应答、不解释内容，关闭 t_A 看门狗并**丢弃**
 * 该帧。合法帧不再交给主泵——计划表是"哪些请求可被服务"的唯一出处，
 * 主泵只消费不立即需要应答的数据（校验失败的帧，以及由计划应答但
 * 仍需要主泵副作用的帧）。因此计划表直接决定设备的服务范围。
 * @{
 */

/** @brief 计划命中后的应答来源。 */
typedef enum
{
    IOLINK_REPLY_SRC_NONE = 0U,     /**< 无载荷：仅 CKS 的确认 */
    IOLINK_REPLY_SRC_TEMPLATE = 1U, /**< plan->data 处的静态模板 */
    IOLINK_REPLY_SRC_PD_IN = 2U,    /**< 活动 PD-in 槽位（当前由 PD_TAIL 标志表达） */
    IOLINK_REPLY_SRC_ISDU_SEG = 3U, /**< 已暂存的 ISDU 应答段 */
    IOLINK_REPLY_SRC_PAGE = 4U,     /**< Direct Parameter page 1 字节 */
    IOLINK_REPLY_SRC_EVENT_MEM = 5U /**< Table 58 事件存储器字节 */
} iolink_reply_source_t;

/** @brief 计划允许的、应在应答前应用的状态动作。 */
typedef enum
{
    IOLINK_REPLY_ACT_NONE = 0U,             /**< 无副作用 */
    IOLINK_REPLY_ACT_ENTER_PREOPERATE = 1U, /**< 启动探测被接受 */
    IOLINK_REPLY_ACT_ENTER_ESTAB_COM = 2U,  /**< 转换命令（不应答） */
    IOLINK_REPLY_ACT_MASTER_COMMAND = 3U,   /**< 应用请求 OD 字节中的 MasterCommand */
    IOLINK_REPLY_ACT_RESET_STARTUP = 4U     /**< OPERATE 中的 Type-0 页读：复位到 STARTUP */
} iolink_reply_action_t;

/** @brief 应答在列表尾追加活动 PD-in 字节（Type-1/2 长应答）。 */
#define IOLINK_REPLY_FLAG_PD_TAIL 0x01U
/** @brief 该计划只在 ISDU 写已成功暂存时有效。 */
#define IOLINK_REPLY_FLAG_STAGED 0x02U
/** @brief 不应答：关闭 t_A 窗口（计划自己消费该帧）。 */
#define IOLINK_REPLY_FLAG_CLOSE_WINDOW 0x04U
/** @brief 关闭 t_A 窗口后启动 Timer B（转换命令的时序语义）。 */
#define IOLINK_REPLY_FLAG_RESTART_TIMER_B 0x10U
/** @brief 该帧由 ISR 完全消费，不再发布到主泵邮箱。 */
#define IOLINK_REPLY_FLAG_CONSUME 0x08U
/** @brief ISR 不应答但帧仍交主泵：接受/拒绝依赖帧内容（如启动期 MasterCommand）。 */
#define IOLINK_REPLY_FLAG_DEFER 0x20U

/**
 * @brief 一条应答计划：请求指纹 + 应答来源。
 *
 * 匹配规则（全部满足才命中）：
 *  - `(mc & match_mask) == match_value`
 *  - `((mc & IOLINK_MC_ADDR_MASK)) <= addr_max`
 *  - `req_len == 0` 或 `req_len == 实际请求长度`
 *  - `ckt_mask == 0` 或 `(ckt & ckt_mask) == ckt_value`
 */
typedef struct
{
    const uint8_t* IOLINK_NULLABLE data; /**< @ref IOLINK_REPLY_SRC_TEMPLATE 的模板字节 */
    uint8_t match_mask;                  /**< MC 字节参与匹配的位 */
    uint8_t match_value;                 /**< MC 字节匹配值 */
    uint8_t ckt_mask;                    /**< CKT 类型位掩码（0 = 不比较类型） */
    uint8_t ckt_value;                   /**< CKT 类型位匹配值 */
    uint8_t addr_max;                    /**< MC 地址字段上界（0x1F = 不限制） */
    uint8_t req_len;                     /**< 精确请求长度，0 = 不限制 */
    uint8_t len;                         /**< 载荷字节数（不含 CKS） */
    uint8_t flags;                       /**< @ref IOLINK_REPLY_FLAG_PD_TAIL 等 */
    uint8_t source;                      /**< @ref iolink_reply_source_t */
    uint8_t action;                      /**< @ref iolink_reply_action_t */
    uint8_t state; /**< 编译该计划时的 DLL 状态（仅记录/调试；匹配范围由切片决定） */
} iolink_reply_plan_t;
/** @} */

/**
 * @brief 邮箱环中的一个接收帧（数据 ISR -> 主泵）。
 *
 * 该记录携带主泵处理该帧所需的全部信息，因此没有
 * 任何逐帧状态存在于环之外：长度、ISR 是否已发送应答（eager）
 * 随字节一同传递。
 */
typedef struct
{
    uint8_t len;                        /**< 帧长度，以字节计 */
    uint8_t eager;                      /**< 非零表示 ISR 已应答该帧 */
    uint8_t data[IOLINK_M_SEQ_MAX_LEN]; /**< 帧字节 */
} iolink_frame_rec_t;

/**
 * @brief DLL 主上下文状态（含共享区）。
 *
 * 完整定义见 dll_main.h。主上下文只包含本类型；数据 ISR 的
 * 独占字段在 @ref iolink_dll_isr_t 中，主上下文翻译单元看不到它们。
 */
typedef struct iolink_dll_main iolink_dll_main_t;

/** @brief 主上下文与数据 ISR 共享的 DLL 状态。完整定义见 dll_shared.h。 */
typedef struct iolink_dll_shared iolink_dll_shared_t;

/** @brief 数据 ISR 独占状态。完整定义见 dll_isr.h。 */
typedef struct iolink_dll_isr iolink_dll_isr_t;

/** @brief 单次分配容器（主上下文 + 数据 ISR）。定义见 dll_ctx.h。 */
typedef struct iolink_dll_ctx iolink_dll_ctx_t;

/**
 * @brief DLL 统计快照
 */
typedef struct
{
    uint32_t crc_errors;           /**< 累计 CRC 错误数 */
    uint32_t timeout_errors;       /**< 累计超时数 */
    uint32_t framing_errors;       /**< 累计成帧错误数 */
    uint32_t timing_errors;        /**< 累计定时违规次数 */
    uint32_t t_ren_violations;     /**< t_ren 违规次数 */
    uint32_t t_cycle_violations;   /**< t_cycle 违规次数 */
    uint32_t t_byte_violations;    /**< 字节间定时违规次数 */
    uint32_t t_pd_violations;      /**< t_pd 违规次数 */
    uint32_t invalid_frames;       /**< 判为无效的已收帧：CRC 校验失败（与 `crc_errors` 同步
                                        自增）与协议错误转 STARTUP 时各计一次。不是重传次数
                                        ——库内没有重发路径 */
    uint32_t voltage_faults;       /**< 累计电压故障数（走 get_faults() 时按故障
                                       边沿计数，走旧路径时按轮询周期计数） */
    uint32_t overtemp_faults;      /**< 累计过温故障数（走 get_faults() 时按故障
                                       边沿计数，走旧路径时为 0） */
    uint32_t short_circuits;       /**< 累计短路次数（走 get_faults() 时按故障
                                       边沿计数，走旧路径时按轮询周期计数） */
    uint32_t isr_tx_dropped;       /**< 传输层无法发送的应答（主上下文与 ISR） */
    uint32_t idle_timeouts;        /**< 1 s 空闲链路诊断（不是 t_A 超时） */
    uint32_t isr_od_overruns;      /**< 已收齐的写请求被新 START 覆盖的次数
                                        （ISDU 引擎的 `req_overruns`） */
    uint32_t isr_plan_misses;      /**< 未命中应答计划而被丢弃的合法帧 */
    uint32_t isr_eager_rejects;    /**< ISR 早期拒绝的帧：校验失败的 ISDU 写，以及
                                        带 TX 能力的 PHY 上非 Type-0 形状的校验失败帧 */
    uint32_t isr_mailbox_drops;    /**< 因帧邮箱满而丢弃的帧 */
    uint32_t isr_frames_consumed;  /**< 由 ISR 完全处理、不再进入邮箱的帧 */
    uint32_t isr_frames_handoff;   /**< 成功投递邮箱交给主泵的帧 */
    uint32_t t_pd_discards;        /**< 在 t_PD 保护窗口内丢弃的邮箱帧 */
    uint32_t reply_plan_overflows; /**< 当前计划表中因槽位耗尽而未发布的条目数 */
    uint32_t pd_in_skips;          /**< 全部 PD-in 缓冲区都被数据 ISR 租用时跳过的发布
                                        次数（回调与轮询路径共用该计数，下一周期重试） */
} iolink_dll_stats_t;

/**
 * @name 截止时间表
 *
 * DLL 的所有软件超时收敛为一张静态表：每个 timer 只保存一个绝对
 * 截止时间（微秒，来自唯一时钟源 @c shared.c_clock / dll_time_us()），
 * 主上下文再维护一个 @c next_deadline。主泵正常路径只把当前时间与
 * @c next_deadline 比较一次；表项装载/取消是边沿事件（收帧、唤醒、
 * 主站活动），不随 timer 数量线性扫描。
 *
 * 每个 timer 有一条 @ref iolink_timer_contract_t 契约（名称 + 规范
 * 条款 + 运行语义），由 @ref iolink_dll_timer_contract() 导出，测试与
 * 文档共用同一份事实源。同刻到期的执行顺序 = 枚举顺序（索引小者先
 * 执行），与定时器表引入前 `dll_process_body()` 中逐项检查的顺序一致。
 *
 * `deadline == 0` 表示该 timer 未装载。所有比较都走同一处回绕安全
 * 判定（`now - deadline` 的符号位）；表内不再混用 us/ms 或 32/64 位。
 * WINDOW 类 timer 到期后仍保留绝对时间（表示"窗口已关闭"），由下一次
 * 边沿事件重装；只有 DEADLINE 类进入 @c next_deadline。
 * @{
 */

/** @brief 截止时间表的 timer 标识；顺序即同刻到期的执行优先级。 */
typedef enum
{
    IOLINK_TIMER_MASTER_LOST = 0, /**< 主站丢失（N 个 MasterCycle） */
    IOLINK_TIMER_IDLE,            /**< 1 s 空闲链路诊断 */
    IOLINK_TIMER_T_DSIO,          /**< T_DSIO：唤醒后无有效报文回 SIO */
    IOLINK_TIMER_T_FBD,           /**< T_FBD：FALLBACK 后切换 SIO */
    IOLINK_TIMER_T_PD,            /**< t_PD：上电/PD 保护窗口 */
    IOLINK_TIMER_T_WU,            /**< T_WU：唤醒后首帧窗口 */
    IOLINK_TIMER_T_BYTE,          /**< t_byte：帧内字节间超时 */
    IOLINK_TIMER_T_REN,           /**< t_REN：应答时间上限（边沿检查） */
    IOLINK_TIMER_T_CYCLE,         /**< T_CYCLE：最小周期时间（边沿检查） */
    IOLINK_TIMER_T_A,             /**< t_A：应答起始预算（PHY 定时器为准） */
    IOLINK_TIMER_COUNT            /**< 表项数量 */
} iolink_timer_id_t;

/** @brief 截止时间表条目；@c deadline 为 0 表示未装载。 */
typedef struct
{
    uint64_t deadline; /**< 绝对截止时间，单位 us（0 = 未装载） */
    uint8_t id;        /**< 表项标识，等于其在表中的索引 */
} iolink_timer_t;

/** @brief timer 的运行语义。 */
typedef enum
{
    IOLINK_TIMER_KIND_DEADLINE = 0U, /**< 到期由主泵分发（进 @c next_deadline） */
    IOLINK_TIMER_KIND_WINDOW = 1U    /**< 只由边沿事件比较（不进 @c next_deadline） */
} iolink_timer_kind_t;

/** @brief timer 的时序契约（名称 + 规范条款 + 运行语义）。 */
typedef struct
{
    const char* name;         /**< 稳定的 timer 名（日志/导出用） */
    const char* clause;       /**< 规范条款号或"实现相关"说明 */
    iolink_timer_id_t id;     /**< 表项标识 */
    iolink_timer_kind_t kind; /**< @ref iolink_timer_kind_t */
    uint8_t phase;            /**< 分发阶段：0 = 模式切换之前，1 = 门控之后 */
} iolink_timer_contract_t;

/** @brief 取时序契约表（只读，进程生命周期内稳定）；@p count 可传 NULL。 */
const iolink_timer_contract_t* iolink_dll_timer_contract(uint8_t* IOLINK_NULLABLE count);
/** @} */

/**
 * @brief 初始化 DLL 上下文
 *
 * 清零主上下文与 ISR 状态、设置状态/重试默认值并复位计数器，
 * 同时把 @p isr 绑定到主上下文的共享区（@c ctx->shared.c_isr）。
 *
 * @param ctx 主上下文（通常为 @c iolink_dll_ctx_t::main）
 * @param isr ISR 独占状态（通常为 @c iolink_dll_ctx_t::isr）
 * @param phy 要绑定的 PHY 实现
 * @note 本函数与 `iolink_dll_publish_reply_plans()` / `iolink_isdu_bind_workspace()`
 *       有同一前置条件：主上下文调用，数据泵静止。三者都不会自行串行化，
 *       区别只在能否在运行期识别误调：另外两个入口在 `shared.in_isr != 0`
 *       时显式失败，而本函数要清零的正是 `shared.in_isr` 所在的对象，无法在
 *       清零之前判定调用上下文——误从数据 ISR 调用会直接破坏正在运行的泵。
 *       任一参数为 NULL 时本函数直接返回、不修改任何对象；
 *       `iolink_dll_publish_reply_plans()` 因此用 `shared.c_isr` 报告未初始化。
 * @note 两个对象都会被清零，包括时钟。直接使用 DLL 的用户必须在此调用之后
 *       设置 @c ctx->shared.c_clock；此外还必须在数据 ISR 静止时设置
 *       `shared.c_m_seq_type` / `shared.c_od_width` / `shared.c_pd_in_len` /
 *       `shared.c_pd_out_len_current` / 能力位，并调用一次
 *       `iolink_dll_publish_reply_plans()`：应答计划和 PD 长度都只在初始化时
 *       定义，本函数发布的是默认 `{Type-0, od=0, PD=0}` 表，之后
 *       `iolink_dll_process()` 不会再重发布。直接使用 DLL 时还应设置
 *       `ctx->min_cycle_time_us`（`iolink_dll_compute_min_cycle_time(ctx) * 100`），
 *       否则 T_CYCLE 强制检查与 T_FBD 会退化。iolink_device_init() 会从
 *       `stack.min_cycle_time` 完成这一步。ISR/SMP 档还要求具备 TX 能力的
 *       PHY 提供 `request_service()`：暂存应答只能由数据 ISR 发送，本函数
 *       不校验能力位，`iolink_device_init()` 会拒绝缺少该钩子的配置。
 */
void iolink_dll_init(iolink_dll_main_t* ctx, iolink_dll_isr_t* isr, const iolink_phy_api_t* phy);

/**
 * @brief 处理 DLL 逻辑
 *
 * 处理字节级数据并执行状态机迁移。应答计划不在这里重编译：计划表是
 * {M-sequence 类型, OD 宽度, 能力位} 的纯函数，三项都只在初始化期间定义，
 * 各状态的差异由计划表的**状态切片**表达（见
 * `iolink_dll_publish_reply_plans()`）。
 *
 * @param ctx 要处理的 DLL 上下文
 */
void iolink_dll_process(iolink_dll_main_t* ctx);

/** @brief iolink_dll_publish_reply_plans() 的结果码。 */
typedef enum
{
    IOLINK_DLL_PUBLISH_OK = 0,          /**< 计划表已完整发布 */
    IOLINK_DLL_PUBLISH_ERR_NULL = -1,   /**< ctx 为 NULL */
    IOLINK_DLL_PUBLISH_ERR_UNINIT = -2, /**< 上下文未经 iolink_dll_init() 初始化 */
    IOLINK_DLL_PUBLISH_ERR_ISR = -3     /**< 在数据 ISR 内调用（`shared.in_isr != 0`） */
} iolink_dll_publish_result_t;

/**
 * @brief 编译并发布应答计划表（主上下文调用，数据 ISR 静止）。
 *
 * 计划内容是 {M-sequence 类型, OD 宽度, 能力位} 的纯函数。三项都只能在
 * 初始化时定义：**能力位不支持运行期修改**，M-sequence 类型与 OD 宽度来自
 * `iolink_stack_config_t`。`iolink_dll_init()` 与 `iolink_device_init()` 各调用
 * 一次，之后不再重编译。
 *
 * 发布协议只有一个"就绪"标志：进入函数时把 `reply_plans_ready` 撤到 0，
 * 整张表写完后原子置 1；数据 ISR 的入口只在它为 1 时进入传输泵。因此
 * 调用本函数期间**开始**的帧不会被处理（也不会计为任何错误）。
 *
 * 标志只保护 ISR 入口，不保护已经在传输泵内的调用：在数据 ISR 可能运行
 * 时重建计划表仍会与查表并发。因此本函数和 `iolink_dll_init()` /
 * `iolink_isdu_bind_workspace()` 一样要求数据泵静止，并且和后者一样在
 * 运行期拒绝误从数据 ISR 的调用：返回 ::IOLINK_DLL_PUBLISH_ERR_ISR 并记录
 * 一条 ISR 错误，此时不写任何计划槽、也不改动 `reply_plans_ready`。
 * `iolink_dll_init()` / `iolink_device_init()` 在中断启用前完成这次编译。
 * 因为没有运行期并发协议，ISR 查表不需要任何序号校验。
 *
 * @param ctx DLL 上下文
 * @return ::IOLINK_DLL_PUBLISH_OK（0）表示表已发布；否则为
 *         ::IOLINK_DLL_PUBLISH_ERR_NULL、::IOLINK_DLL_PUBLISH_ERR_UNINIT
 *         （`iolink_dll_init()` 未成功完成）或 ::IOLINK_DLL_PUBLISH_ERR_ISR，
 *         失败时上下文保持不变。
 */
int iolink_dll_publish_reply_plans(iolink_dll_main_t* ctx);

/**
 * @brief 面向原始字节（单字节收发器）PHY 的 ISR 侧泵。
 *
 * 由 PHY 数据中断处理程序调用。它通过
 * phy->rx_octet() 拉取接收到的字节，对 M 序列成帧（使用内部的
 * 期望长度推导），按应答计划直接应答，并把仍需主泵
 * 处理的帧（校验失败，或由计划应答但需要副作用的帧）发布到由
 * iolink_dll_process() 消费的邮箱，从共享状态组装应答并
 * 通过 phy->tx_octet()/tx_done()（TX BYTE）或 phy->tx_frame()
 * （TX FRAME）喂送应答字节。合法帧计划未命中时不应答并丢弃。
 * 它从不执行协议逻辑（ISDU / 参数 /
 * 事件 / 厂商相关部分）；这些仍留在主泵中。
 *
 * 无待处理数据时调用是安全的；当 INT 线未被
 * 置为有效时立即返回。对没有 rx_octet() 的 PHY 为空操作。计划表尚未
 * 完整发布（初始化窗口）时同样直接返回，不改任何状态、不计数；此时也
 * **不排空 PHY 的 RX**，因此窗口内到达的帧及其硬件状态由端口按自己的
 * 策略处理（不保证缓存）。
 * @param shared DLL 共享区（主上下文内嵌，`&ctx->shared`）
 */
void iolink_dll_process_isr(iolink_dll_shared_t* shared);

/**
 * @brief 把当前 DLL 状态交付给状态变更钩子。
 *
 * 通知基于电平：当当前状态与上次
 * 交付的状态不同时，会用当前状态调用一次钩子。数据 ISR
 * 只改变状态，因此该函数是应用钩子唯一可能运行的
 * 位置（绝不在中断上下文中）。iolink_dll_process()
 * 在结束时调用它；设备层依赖这次调用，裸 DLL
 * 用户也可以从主循环中调用它。
 *
 * 两次调用之间进入并离开的两个状态会被合并到后一个
 * 状态；最终状态绝不会丢失。在数据 ISR 中发生的 STARTUP
 * 迁移会作为待处理通知保留下来，并在当前状态之前上报，
 * 因此快速的 STARTUP -> PREOPERATE 序列无法掩盖该复位。
 * 状态未改变时调用是安全的（会立即返回）。
 * @param ctx DLL 上下文
 */
void iolink_dll_flush_state_callbacks(iolink_dll_main_t* ctx);

/** @brief Timer A（t_A 单次定时）在无应答的情况下到期。
 * @param shared DLL 共享区（`&ctx->shared`） */
void iolink_dll_on_rx_timeout(iolink_dll_shared_t* shared);

/** @brief Timer B 节拍：授权一个主上下文服务窗口。
 * @param shared DLL 共享区（`&ctx->shared`） */
void iolink_dll_on_stack_tick(iolink_dll_shared_t* shared);

/** @brief 消费 Timer B 的授权（1 = 用户代码可以运行）。 */
uint8_t iolink_dll_take_service_window(iolink_dll_main_t* ctx);

/**
 * @brief 获取当前 PD 长度（初始化时由 `shared.c_pd_in_len` /
 *        `shared.c_pd_out_len_current` 固定）
 *
 * @param ctx DLL 上下文
 * @param[out] pd_in_len 当前 PD_In 长度；可传 NULL 表示不需要该值
 * @param[out] pd_out_len 当前 PD_Out 长度；可传 NULL 表示不需要该值
 */
void iolink_dll_get_pd_length(const iolink_dll_main_t* ctx, uint8_t* IOLINK_NULLABLE pd_in_len,
                              uint8_t* IOLINK_NULLABLE pd_out_len);

/**
 * @brief 获取当前工作模式
 *
 * @param ctx DLL 上下文
 * @return iolink_phy_mode_t 当前模式
 */
iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_main_t* ctx);

/**
 * @brief 获取当前协商后的波特率（编译期 `IOLINK_COM`）
 *
 * @return iolink_baudrate_t 当前波特率
 */
iolink_baudrate_t iolink_dll_get_baudrate(void);

/** 根据 M 序列定时计算设备的 MinCycleTime（单位 0.1 ms）。 */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_main_t* ctx);

/**
 * @brief 获取 DLL 统计
 *
 * @param ctx DLL 上下文
 * @param[out] out_stats 输出统计结构，填入计数器的快照
 */
void iolink_dll_get_stats(const iolink_dll_main_t* ctx, iolink_dll_stats_t* out_stats);

/** @} */ /* iolinki_dll 结束 */

IOLINK_ASSUME_NONNULL_END

#endif  // IOLINK_DLL_H
