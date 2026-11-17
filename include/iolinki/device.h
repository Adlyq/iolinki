/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DEVICE_H
#define IOLINK_DEVICE_H

#include "iolinki/config.h"

#include "iolinki/application.h"
#include "iolinki/capability.h"
#include "iolinki/data_storage.h"
#include "iolinki/device_info.h"
#include "iolinki/internal/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/od.h"
#include "iolinki/phy.h"
#include "iolinki/workspace.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file device.h
 * @brief IO-Link 设备应用层 API。
 *
 * IO-Link 设备的顶层入口点。它把物理层、数据链路层、
 * 设备标识、参数和应用回调整合在单个不透明上下文之后，
 * 并提供应用所使用的生命周期、过程数据及自省
 * （introspection）函数。
 */

/* -------------------------------------------------------------------------
 * 集成检查清单（双泵传输）
 *
 * 必需的调用：
 *  - iolink_device_init() 调用一次；应用拥有该上下文对象。
 *  - 在主循环中周期性调用 iolink_device_process()。它必须是
 *    唯一的主上下文调用者（不允许第二个任务调用该 API）。
 *  - 从 PHY 数据中断中调用 iolink_device_process_isr()。
 *  - 当 PHY 提供可选的 Timer A/Timer B 钩子时，将这些中断
 *    转发给 iolink_dll_on_rx_timeout() / iolink_dll_on_stack_tick()
 *    （在 dll.h 中声明）。没有 Timer B 转发时，主服务窗口
 *    会被门控，iolink_device_process() 会立即返回。
 *
 * 由 iolink_device_process() 自动服务：
 *  - DLL 帧分发、ISDU 服务、时序监视和诊断；
 *  - ISDU 传输空闲时的 iolink_events_process() 和 iolink_ds_process()
 *    （device_service_events_and_ds()）。
 *
 * 仍由应用驱动：
 *  - iolink_ds_check()：它需要主站的 DS 校验和，协议栈
 *    无法自行获取。
 *
 * 普通应用只需要此头文件以及上面的两个 ISR 钩子；
 * dll.h 是高级/移植接口。
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * 所有权与生命周期约定
 *
 * 对象                       | 所有者          | 生命周期 / 访问规则
 * ---------------------------+-----------------+---------------------------------------------
 * iolink_device_config_t     | application     | 必须比上下文存活更久
 *  .phy (vtable)             | application/PHY | 指针；vtable 在 ctx 生命周期内有效
 *  .phy->self                | PHY driver      | 仅传给 PHY 回调；驱动初始化时写入
 *  .stack                    | application     | 指针；初始化时读入上下文，须存活更久
 *  .app                      | application     | trait（self + 回调）保持有效；
 *                            |                 | self 传给 time/log/OD/on_* 全部钩子
 *  .app->time_us             | application     | 单调递增、非阻塞、ISR 安全
 *  .app->log_write           | application     | 主服务上下文调用；启用
 *                            |                 | IOLINK_LOG_ENABLE_ISR 时须 ISR 安全
 *  .device_info              | application     | 指针，必须提供；静态标识数据
 * iolink_device_ctx_t        | application     | 每个链路一个实例；一个主上下文
 *                            |                 | 调用者加数据 ISR
 * PD data (pd_input_update)  | application     | 调用期间借用；内部复制到
 *                            |                 | 应答缓冲区
 * PD data (on_pd_input)      | application     | 借用已填充的缓冲区；返回值是
 *                            |                 | PD 有效标志（回调 profile）
 * PD buffer (pd_output_read) | caller          | 由调用填充；调用方拥有
 * rx_frame() return value    | PHY             | 借用至下一次 rx_frame()
 * tx_frame() data            | library         | 有效至传输完成
 * ------------------------------------------------------------------------- */

/**
 * @defgroup iolinki_device 设备应用层
 * @brief 顶层 IO-Link 设备上下文、生命周期和过程数据 API。
 * @{
 */

/** @brief 返回以微秒为单位的单调时间戳的回调。
 *  @param self 应用上下文（`iolink_device_app_t::self`）。
 *  @return 当前时间，单位为微秒。
 *  @note 主循环和数据 ISR 都会调用它，因此它必须是非阻塞
 *        且 ISR 安全的。 */
typedef uint64_t (*iolink_device_time_us_fn)(iolink_app_t* IOLINK_NULLABLE self);

/**
 * @brief 初始化时提供的设备配置。
 *
 * 汇总 PHY 驱动、协议栈设置、应用钩子、设备标识、
 * 数据存储后端和平台集成回调。
 */
typedef struct
{
    const iolink_phy_api_t* IOLINK_NONNULL
        phy; /**< 物理层驱动 API（通常取自 `iolink_phy_*_get()`），必须
   比上下文存活更久：帧处理期间仍读它。`phy->self` 由提供 vtable 的一方写入自己的
   上下文（内置驱动在 `iolink_phy_*_init()` 里写），
   应用不覆盖它（覆盖会让驱动把错误指针转回自己的
   状态）。为 NULL 时初始化返回
   `IOLINK_DEVICE_INIT_ERR_NULL`。 */
    const iolink_stack_config_t* IOLINK_NONNULL stack; /**< 协议栈行为配置；必须比上下文存活更久。为
                                             NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_NULL`。 */
    const iolink_device_app_t* IOLINK_NONNULL
        app; /**< 应用 trait：实例指针与生命周期/PD/时钟/日志回调
   的唯一来源，必须比上下文存活更久（帧处理与
   应用回调期间仍读它）。`app` 为
   NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_APP`；
   `app->time_us` 为 NULL 时返回
   `IOLINK_DEVICE_INIT_ERR_TIME`。 */
    const iolink_device_info_t* IOLINK_NONNULL
        device_info; /**< 静态设备标识数据，必须提供（设备没有默认身份）；
           为 NULL 时初始化返回
           `IOLINK_DEVICE_INIT_ERR_DEVICE_INFO`。 */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_flag_persist_fn IOLINK_NULLABLE ds_flag_persist; /**< 持久化 DS_UPLOAD_FLAG（NULL =
                                                    RAM-only）； 回调收到 `app->self`。 */
#endif
    iolink_log_level_t log_level;                  /**< 运行时日志阈值；IOLINK_LOG_LEVEL_SILENT（0）
                                                        表示不输出。 */
    const iolink_od_app_t* IOLINK_NULLABLE od;     /**< 应用对象字典（可选；设备专用索引与参数值的
                                            唯一扩展入口）。 */
    iolink_workspace_t* IOLINK_NULLABLE workspace; /**< 可选的传输 scratch 工作区。NULL 表示使用
                                        上下文内嵌的 fallback 存储；当
                                        `IOLINK_WORKSPACE_EMBEDDED=0` 时必须提供。 */
} iolink_device_config_t;

/**
 * @brief 设备运行时上下文。
 *
 * 直接分配此对象，但只能通过 API 函数访问它。
 * 所有字段都是协议栈私有的状态。
 */
typedef struct
{
    /* 私有字段。直接分配此对象，但请使用 API 函数。 */
    iolink_dll_ctx_t dll;                 /**< 数据链路层上下文。 */
    iolink_device_info_ctx_t device_info; /**< 设备标识上下文。 */
    const iolink_device_config_t* config; /**< 指向所提供配置的指针（唯一配置来源）。 */
    iolink_reset_handler_t IOLINK_NULLABLE reset_handler; /**< 应用重置处理函数（可以为 NULL）。 */
    bool master_lost_last;                                /**< 上一次的主站丢失状态（回调边沿） */
    int phy_error;                                        /**< 最近一次 PHY init 失败的原始驱动码
                                                               （0 = 无失败；日志关闭时仍可读） */
    uint8_t direct_param_page2[16];                       /**< Direct Parameter Page 2 存储。 */
} iolink_device_ctx_t;

/**
 * @brief iolink_device_init() 返回的结果代码。
 *
 * 负值表示初始化被拒绝。PHY 的 `init` 回调失败时统一返回
 * `IOLINK_DEVICE_INIT_ERR_PHY`，驱动自己的错误码不会混进本枚举的取值域，
 * 而是由 iolink_device_get_phy_error() 提供（默认日志是静默的）。
 */
typedef enum
{
    IOLINK_DEVICE_INIT_OK = 0,               /**< 初始化成功 */
    IOLINK_DEVICE_INIT_ERR_NULL = -1,        /**< ctx、config、phy 或 stack 为 NULL */
    IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE = -2,   /**< m_seq_type 不是有效类型 */
    IOLINK_DEVICE_INIT_ERR_PD_IN_LEN = -3,   /**< pd_in_len 超出缓冲区 */
    IOLINK_DEVICE_INIT_ERR_PD_OUT_LEN = -4,  /**< pd_out_len 超出缓冲区 */
    IOLINK_DEVICE_INIT_ERR_OD_WIDTH = -5,    /**< od_width 不是 1/2/8/32 */
    IOLINK_DEVICE_INIT_ERR_OD_CAPACITY = -6, /**< od_width 超出 IOLINK_OD_MAX_SIZE */
    IOLINK_DEVICE_INIT_ERR_BAUDRATE = -7,    /**< baudrate_set 使用了无效的 COM */
    IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT = -8, /**< PD/OD 长度与 M-sequence 类型不兼容 */
    IOLINK_DEVICE_INIT_ERR_TIME = -9,        /**< time_us 回调为 NULL */
    IOLINK_DEVICE_INIT_ERR_CRITICAL = -10,   /**< 平台临界区为弱默认实现 */
    IOLINK_DEVICE_INIT_ERR_PD_INPUT =
        -11, /**< PD 回调路径与轮询调用混用（能力位已置位时调用轮询 API） */
    IOLINK_DEVICE_INIT_ERR_PHY =
        -12, /**< PHY 能力组合不可满足（例如要求 ISR 发送却没有 `request_service`，
                  或构建承诺 `tx_busy` 却缺少该钩子），或 PHY `init` 失败
                  （原始驱动码见 iolink_device_get_phy_error()） */
    IOLINK_DEVICE_INIT_ERR_WORKSPACE = -13, /**< `IOLINK_WORKSPACE_EMBEDDED=0` 但没有提供工作区 */
    IOLINK_DEVICE_INIT_ERR_OD = -14,        /**< 应用对象字典表存在未定义/自相矛盾的配置 */
    IOLINK_DEVICE_INIT_ERR_APP = -15,       /**< 未提供应用 trait（`config->app == NULL`） */
    IOLINK_DEVICE_INIT_ERR_DEVICE_INFO =
        -16, /**< 未提供设备身份（`config->device_info == NULL`） */
} iolink_device_init_result_t;

/**
 * @brief 根据配置初始化设备上下文。
 * @param ctx     要初始化的设备上下文。
 * @param config  要应用的配置（在 ctx 生命周期内必须保持有效）。
 * @return 成功时为 0；失败时为负的 ::iolink_device_init_result_t 值。
 *         PHY `init` 失败统一映射为 `IOLINK_DEVICE_INIT_ERR_PHY`；驱动
 *         自己的错误码用 iolink_device_get_phy_error() 读取。
 */
int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);

/**
 * @brief 返回设备当前绑定的工作区。
 *
 * @param ctx 设备上下文。
 * @return 外部工作区指针；使用内嵌 fallback 存储时返回 NULL。
 */
iolink_workspace_t* IOLINK_NULLABLE iolink_device_get_workspace(const iolink_device_ctx_t* ctx);

/**
 * @brief 返回最近一次 `iolink_device_init()` 中 PHY `init` 回调的原始错误码。
 *
 * 初始化失败时返回负的 `iolink_device_init_result_t`，PHY 驱动自己的负值
 * 不会混进那个枚举；需要区分"哪一步失败"时读这里（0 表示 PHY 未失败）。
 * @param ctx 设备上下文（初始化尝试用的那个）
 * @return 驱动返回的原始值，或 0
 */
int iolink_device_get_phy_error(const iolink_device_ctx_t* ctx);

/**
 * @brief 在运行时更换日志回调或阈值。
 *
 * 更新 DLL、ISDU、DS 和事件子系统共用的汇点；`self` 仍取
 * 应用 trait 的 `self` 字段。传入 `write == NULL` 或
 * `level == IOLINK_LOG_LEVEL_SILENT` 可立即静默。
 *
 * @param ctx   由 iolink_device_init() 初始化的设备上下文。
 * @param write 新的输出回调，可为 NULL。
 * @param level 新的运行时阈值。
 */
void iolink_device_set_log(iolink_device_ctx_t* ctx, iolink_log_write_fn IOLINK_NULLABLE write,
                           iolink_log_level_t level);

/**
 * @brief 运行设备状态机的一次迭代。
 *
 * 驱动 DLL/PHY：处理待处理的帧、时序和应用事件。
 * 请从主循环中周期性调用它。
 * @param ctx 设备上下文。
 */
void iolink_device_process(iolink_device_ctx_t* ctx);

/**
 * @brief 双泵传输的 ISR 侧泵。
 *
 * 从 PHY 数据中断中调用：对进入的 M-sequence 进行组帧，并供给
 * 回复字节，将已完成的请求发布到由
 * iolink_device_process() 消费的邮箱。不运行任何协议逻辑。
 */
void iolink_device_process_isr(iolink_device_ctx_t* ctx);

/**
 * @brief 更新输入过程数据（设备 -> 主站）。
 *
 * 轮询 API 与 `on_pd_input` 回调在同一次构建中共存，按应用是否
 * 提供回调在初始化时选择路径。已选择回调路径（能力位
 * `IOLINK_CAP_PD_IN_CALLBACK` 置位）时，本函数返回
 * `IOLINK_DEVICE_INIT_ERR_PD_INPUT`，而不是静默覆盖回调的输出。
 *
 * @note PD 长度是初始化属性（`iolink_stack_config_t::pd_in_len`），运行期
 * 不可改变：DPP1 的 0x05/0x06 是只读通告，`@p len` 必须等于该配置长度，
 * 否则返回 -1。成功调用只更新缓冲区、有效性与活动索引，
 * 不修改 DPP1、MinCycleTime 或应答布局。
 *
 * @param ctx    设备上下文。
 * @param data   要发布的输入过程数据字节。
 * @param len    @p data 的长度（字节），必须等于配置的 PD-in 长度。
 * @param valid  过程数据当前是否有效。
 * @return 成功时为 0；参数/长度不符或没有空闲缓冲区时返回负值。
 */
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid);

/**
 * @brief 取本实例的能力位。
 * @param ctx 设备上下文。
 * @return 实例能力位掩码；ctx 为 NULL 时为 0。
 */
iolink_caps_t iolink_device_capabilities(const iolink_device_ctx_t* ctx);

/**
 * @brief 读取最新的输出过程数据（主站 -> 设备）。
 * @param ctx        设备上下文。
 * @param[out] data  填充输出过程数据的缓冲区。
 * @param len        @p data 的大小（字节）。
 * @return 已复制的字节数（没有时为 0），出错时为负值。
 */
int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx, uint8_t* data, size_t len);

/**
 * @brief 注册在主站重置命令时调用的处理函数。
 * @param ctx      设备上下文。
 * @param handler  重置处理函数回调（可以为 NULL 以清除）。
 */
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx,
                                     iolink_reset_handler_t IOLINK_NULLABLE handler);

#if IOLINK_FEATURE_EVENTS
/**
 * @brief 获取事件处理上下文。
 * @param ctx 设备上下文。
 * @return 指向设备事件上下文的指针。
 */
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);
#endif

#if IOLINK_FEATURE_DATA_STORAGE
/**
 * @brief 获取数据存储上下文。
 * @param ctx 设备上下文。
 * @return 指向设备数据存储上下文的指针。
 */
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);
#endif

/**
 * @brief 获取当前 DLL 状态。
 * @param ctx 设备上下文。
 * @return 当前数据链路层状态。
 */
iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx);

/**
 * @brief 主站是否已静默达到配置的周期数。
 * @param ctx 设备上下文。
 * @return 在主站被视为丢失期间为 true（应用提示；DLL
 *         状态和波特率不会改变）。
 */
bool iolink_device_master_lost(const iolink_device_ctx_t* ctx);

/**
 * @brief 获取当前 PHY 通信模式。
 * @param ctx 设备上下文。
 * @return 当前物理层模式。
 */
iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx);

/**
 * @brief 获取当前通信波特率。
 * @param ctx 设备上下文。
 * @return 活动波特率。
 */
iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx);

/**
 * @brief 复制 DLL 统计计数器。
 * @param ctx             设备上下文。
 * @param[out] out_stats  填充当前 DLL 统计信息。
 */
void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats);

/**
 * @brief 启用或禁用时序强制检查。
 * @param ctx     设备上下文。
 * @param enable  为 true 时强制时序限制，为 false 时仅测量。
 */
void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable);

/**
 * @brief 设置响应时间（t_ren）限制。
 * @param ctx       设备上下文。
 * @param limit_us  响应时间限制，单位为微秒。
 */
void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us);

/**
 * @brief 获取协商后的 M-sequence 类型。
 * @param ctx 设备上下文。
 * @return 活动的 M-sequence 类型。
 */
iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx);

/**
 * @brief 获取输入过程数据长度。
 * @param ctx 设备上下文。
 * @return 输入过程数据长度（字节）。
 */
uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx);

/**
 * @brief 获取输出过程数据长度。
 * @param ctx 设备上下文。
 * @return 输出过程数据长度（字节）。
 */
uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx);

/** @} */ /* end of iolinki_device */

IOLINK_ASSUME_NONNULL_END
#endif  // IOLINK_DEVICE_H
