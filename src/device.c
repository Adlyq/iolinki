/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file device.c
 * @brief 设备应用层：协议栈顶层生命周期与 I/O。
 * @ingroup iolinki_device
 *
 * 将 DLL、ISDU、参数、设备信息与数据存储子上下文
 * 连接在一起，驱动每周期处理，并向应用暴露过程数据
 * 交换与状态访问接口。
 */

#include "iolinki/device.h"
#include "iolinki/port.h"
#include "dll_internal.h"
#include "od_internal.h"
#include <string.h>

/** @brief DLL 状态变化回调：分发到应用的 startup/preoperate/operate 钩子。 */
static void device_state_cb(void* user, iolink_dll_state_t state)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if ((ctx == NULL) || (ctx->config == NULL)) {
        return;
    }

    if (ctx->config->app->on_state_change != NULL) {
        ctx->config->app->on_state_change(ctx->config->app->self, state);
    }

    switch (state) {
        case IOLINK_DLL_STATE_STARTUP:
            if (ctx->config->app->on_startup != NULL) {
                ctx->config->app->on_startup(ctx->config->app->self);
            }
            break;
        case IOLINK_DLL_STATE_PREOPERATE:
            if (ctx->config->app->on_preoperate != NULL) {
                ctx->config->app->on_preoperate(ctx->config->app->self);
            }
            break;
        case IOLINK_DLL_STATE_OPERATE:
            if (ctx->config->app->on_operate != NULL) {
                ctx->config->app->on_operate(ctx->config->app->self);
            }
            break;
        /* 这些状态没有对应的应用回调；枚举项全部列出，新增状态时
         * 必须在这里显式决定它走哪条路。 */
        case IOLINK_DLL_STATE_AWAITING_COMM:
        case IOLINK_DLL_STATE_ESTAB_COM:
        case IOLINK_DLL_STATE_FALLBACK:
        case IOLINK_DLL_STATE_COMM_DISABLED:
        case IOLINK_DLL_STATE_COUNT:
        default:
            break;
    }
}

/** @brief 将用户协议栈配置应用到 DLL 上下文，并推导长度/时序。
 *
 * 配置只有一个来源：调用者在初始化期间提供的
 * @ref iolink_device_config_t 结构。上下文不保留第二份副本。 */
static void device_apply_stack_config(iolink_device_ctx_t* ctx)
{
    const iolink_stack_config_t* stack = ctx->config->stack;

    ctx->dll.main.shared.m_seq_type = (uint8_t) stack->m_seq_type;
    ctx->dll.main.shared.pd_in_len = stack->pd_in_len;
    ctx->dll.main.pd_out_len = stack->pd_out_len;
    ctx->dll.main.min_cycle_time_app_us = (uint32_t) stack->min_cycle_time * 100U;
    ctx->dll.main.master_lost_cycles = stack->master_lost_cycles;
    ctx->dll.main.t_pd_delay_us = stack->t_pd_us;
    if (ctx->dll.main.t_pd_delay_us > 0U) {
        /* 时钟已在上文绑定：直接装载 T_PD 表项，deadline 是唯一事实源。 */
        dll_timer_arm(&ctx->dll.main, IOLINK_TIMER_T_PD,
                      dll_time_us(&ctx->dll.main) + (uint64_t) ctx->dll.main.t_pd_delay_us);
    }

    ctx->dll.main.pd_in_len_current = ctx->dll.main.shared.pd_in_len;
    ctx->dll.main.shared.pd_out_len_current = ctx->dll.main.pd_out_len;
    ctx->dll.main.pd_in_len_max = ctx->dll.main.shared.pd_in_len;
    ctx->dll.main.pd_out_len_max = ctx->dll.main.pd_out_len;

    /* 双泵传输：ISR 组帧请求，并基于共享状态组装应答，因此 DLL 需要
     * OD 宽度来确定应答布局（Table A.10 的 OPERATE 编码跟随该宽度）。
     * 宽度由 config.od_width 显式给出（校验阶段已拒绝 0）。 */
    ctx->dll.main.shared.od_width = stack->od_width;

    /* 在根据波特率推导最小周期时间之前，先采用
     * 配置的操作 COM（及其时序限制）。 */
    if (stack->baudrate_set) {
        (void) iolink_dll_set_operating_baudrate(&ctx->dll.main, stack->baudrate);
    }

    /* 通告/实际提供的最小周期时间由实际的
     * M-sequence（PD/OD 长度与波特率）推导，并受应用下限约束。 */
    ctx->dll.main.min_cycle_time_us =
        (uint32_t) iolink_dll_compute_min_cycle_time(&ctx->dll.main) * 100U;

    /* 时序强制的默认值来自构建档位，栈配置可以显式覆盖。 */
    if (stack->timing_enforce == IOLINK_STACK_TIMING_ENFORCE) {
        iolink_dll_set_timing_enforcement(&ctx->dll.main, true);
    }
    else if (stack->timing_enforce == IOLINK_STACK_TIMING_MEASURE) {
        iolink_dll_set_timing_enforcement(&ctx->dll.main, false);
    }
    else {
        /* DEFAULT：保留 iolink_dll_init() 从能力表取的值。 */
    }
    /* 以上赋值直接写共享区（m_seq_type / PD 长度 / od_width / 应用下限），
     * 因此在这里统一重建一次 DPP1 映像，保证首次 ISR 页读看到的是配置值。 */
    iolink_isdu_dp_page1_invalidate(&ctx->dll.main.shared.isdu);
}

/** @brief 一种固定的 Annex A 布局：PD 输入/输出与可接受的 OD 宽度。 */
typedef struct
{
    uint8_t mseq;
    uint8_t pd_in;
    uint8_t pd_out;
    uint8_t od;
    uint8_t od_alt; /* 第二个可接受的 OD 宽度，无则为 0（此处 od 永不为 0） */
} device_fixed_layout_t;

static const device_fixed_layout_t g_fixed_layouts[] = {
    {IOLINK_M_SEQ_TYPE_1_2, 0U, 0U, 2U, 0U}, {IOLINK_M_SEQ_TYPE_1_V, 0U, 0U, 8U, 32U},
    {IOLINK_M_SEQ_TYPE_2_1, 1U, 0U, 1U, 0U}, {IOLINK_M_SEQ_TYPE_2_2, 2U, 0U, 1U, 0U},
    {IOLINK_M_SEQ_TYPE_2_3, 0U, 1U, 1U, 0U}, {IOLINK_M_SEQ_TYPE_2_4, 0U, 2U, 1U, 0U},
    {IOLINK_M_SEQ_TYPE_2_5, 1U, 1U, 1U, 0U},
};

/** @brief 强制 Annex A 规定的按类型固定的 PD/OD 布局（ISR 传输）。
 *
 * Figure A.7-A.13 规定了每种 M-sequence
 * 类型的固定 PD 与 OD 字节数。通告其中某一类型的设备必须
 * 严格使用该布局，否则主站与设备对帧长度的理解不一致。
 * TYPE_1_1 仅为旧的交错模式，V1.1.5 设备不得
 * 实现它（Table A.10 NOTE 3），因此被拒绝。TYPE_0 与
 * 可变的 TYPE_2_V 保留现有的范围检查。
 */
static int device_validate_fixed_mseq(const iolink_stack_config_t* stack, uint8_t od)
{
    /* TYPE_1_1 仅为旧的交错模式，V1.1.5 设备不得
     * 实现它（Table A.10 NOTE 3）。 */
    if (stack->m_seq_type == IOLINK_M_SEQ_TYPE_1_1) {
        return IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE;
    }
    for (size_t i = 0U; i < sizeof(g_fixed_layouts) / sizeof(g_fixed_layouts[0]); i++) {
        const device_fixed_layout_t* layout = &g_fixed_layouts[i];
        if ((uint32_t) stack->m_seq_type != (uint32_t) layout->mseq) {
            continue;
        }
        const bool od_ok = (od == layout->od) || ((layout->od_alt != 0U) && (od == layout->od_alt));
        if ((stack->pd_in_len == layout->pd_in) && (stack->pd_out_len == layout->pd_out) && od_ok) {
            return IOLINK_DEVICE_INIT_OK;
        }
        return IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT;
    }
    return IOLINK_DEVICE_INIT_OK; /* TYPE_0 与可变的 TYPE_2_V */
}

/** @brief 在触碰上下文或 PHY 之前校验协议栈配置。 */
static int device_config_validate(const iolink_device_config_t* config)
{
    const iolink_stack_config_t* stack;
    uint8_t od;

    if (config == NULL) {
        return IOLINK_DEVICE_INIT_ERR_NULL;
    }

    if ((config->phy == NULL) || (config->stack == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_NULL;
    }
    stack = config->stack;
    if (config->device_info == NULL) {
        return IOLINK_DEVICE_INIT_ERR_DEVICE_INFO;
    }
    if (config->app->time_us == NULL) {
        return IOLINK_DEVICE_INIT_ERR_TIME;
    }

    if ((uint32_t) stack->m_seq_type > (uint32_t) IOLINK_M_SEQ_TYPE_MAX) {
        return IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE;
    }
    if ((uint32_t) stack->pd_in_len > (uint32_t) IOLINK_PD_IN_MAX_SIZE) {
        return IOLINK_DEVICE_INIT_ERR_PD_IN_LEN;
    }
    if ((uint32_t) stack->pd_out_len > (uint32_t) IOLINK_PD_OUT_MAX_SIZE) {
        return IOLINK_DEVICE_INIT_ERR_PD_OUT_LEN;
    }

    /* Table A.10 的 OD 宽度必须显式给出（1/2/8/32），没有推导。 */
    od = stack->od_width;
    if ((uint32_t) od > (uint32_t) IOLINK_OD_MAX_SIZE) {
        return IOLINK_DEVICE_INIT_ERR_OD_CAPACITY;
    }
    if ((od != 1U) && (od != 2U) && (od != 8U) && (od != 32U)) {
        return IOLINK_DEVICE_INIT_ERR_OD_WIDTH;
    }

    /* 宏只能校验常量交叉关系；指针与后端一致性在这里一次走表拒绝。 */
#if IOLINK_FEATURE_VALIDATE
    if (od_validate_app(config->od) != OD_VALIDATE_OK) {
        return IOLINK_DEVICE_INIT_ERR_OD;
    }
#endif

    {
        const int layout_err = device_validate_fixed_mseq(stack, od);
        if (layout_err != IOLINK_DEVICE_INIT_OK) {
            return layout_err;
        }
    }

    if (stack->baudrate_set && ((uint32_t) stack->baudrate > (uint32_t) IOLINK_BAUDRATE_COM3)) {
        return IOLINK_DEVICE_INIT_ERR_BAUDRATE;
    }

#if IOLINK_NEED_CRITICAL
    /* 只有真正与中断串行化的 profile 才需要
     * 真正的临界区；SINGLE profile 从不引用这些钩子。 */
    if (iolink_platform_critical_sections_ready() != 0) {
        return IOLINK_DEVICE_INIT_ERR_CRITICAL;
    }
#endif

    /* 能力的一致性校验。降级后的最大风险不是"位为开但没编译"
     * （resolve 只置位 built 支持的位，unbuilt 能力被静默屏蔽），而是
     * 运行时要求与 PHY 实际钩子不匹配：见下方 REQUIRE_ISR_TX 与 TX_BUSY。 */
    const iolink_caps_t caps = iolink_caps_resolve(config->phy, config->stack, config->app, NULL);

    if (iolink_caps_has(caps, IOLINK_CAP_REQUIRE_ISR_TX) &&
        !iolink_caps_has(caps, IOLINK_CAP_REQUEST_SERVICE)) {
        /* 端口要求所有应答由 ISR 发送，但 PHY 没有 request_service()：
         * 回退路径不可用，应答会全部丢弃。 */
        return IOLINK_DEVICE_INIT_ERR_PHY;
    }

    if ((IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME) && IOLINK_PHY_FEATURE_TX_BUSY &&
        !iolink_caps_has(caps, IOLINK_CAP_TX_BUSY)) {
        /* 该构建承诺基于 tx_busy() 释放租约；NULL 钩子会
         * 退化为永不释放的 PD 输入租约。 */
        return IOLINK_DEVICE_INIT_ERR_PHY;
    }

    return IOLINK_DEVICE_INIT_OK;
}

/** @brief 初始化实现：工作区由 config 提供（见 iolink_device_init）。 */
static int device_init_with_workspace(iolink_device_ctx_t* ctx,
                                      const iolink_device_config_t* config,
                                      iolink_workspace_t* workspace)
{
    int err;

    if ((ctx == NULL) || (config == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_NULL;
    }
    /* 早退路径（钩子缺失、工作区缺失、trait 缺失）同样返回其它码：
     * 先清掉上一次尝试留下的驱动码，getter 的 0 才明确表示"PHY init 没失败"。 */
    ctx->phy_error = 0;
    if (config->app == NULL) {
        return IOLINK_DEVICE_INIT_ERR_APP;
    }

#if !IOLINK_WORKSPACE_EMBEDDED
    if (workspace == NULL) {
        return IOLINK_DEVICE_INIT_ERR_WORKSPACE;
    }
#endif

    err = device_config_validate(config);
    if (err != IOLINK_DEVICE_INIT_OK) {
        iolink_log_sink_t sink;
        const iolink_device_app_t* app = config->app;
        iolink_log_sink_init(&sink, app->log_write, app->self, config->log_level);
        IOLINK_LOG_ERROR(&sink, "device init rejected: err=%d", err);
        return err;
    }

    (void) memset(ctx, 0, sizeof(*ctx));
    ctx->config = config;

    if (config->phy->init != NULL) {
        err = config->phy->init(config->phy->self);
        if (err != 0) {
            iolink_log_sink_t sink;
            /* sink 尚未绑定到各子系统（PHY init 在绑定之前运行）。 */
            iolink_log_sink_init(&sink, config->app->log_write, config->app->self,
                                 config->log_level);
            IOLINK_LOG_ERROR(&sink, "phy init failed: err=%d", err);
            /* 驱动错误码属于另一个域：对外统一为 PHY 失败，原始值留在
             * 上下文里（默认日志是静默的，不能只靠日志）。 */
            ctx->phy_error = err;
            return IOLINK_DEVICE_INIT_ERR_PHY;
        }
    }

    if (iolink_device_info_ctx_init(&ctx->device_info, config->device_info) != 0) {
        return IOLINK_DEVICE_INIT_ERR_DEVICE_INFO; /* 校验阶段已拦截，兜底不回落默认值 */
    }
    iolink_dll_init(&ctx->dll.main, &ctx->dll.isr, config->phy);
    /* 实例能力在绑定 PHY 之后、任何 ISR 可能运行之前解析一次。 */
    ctx->dll.main.shared.caps = iolink_caps_resolve(config->phy, config->stack, config->app, NULL);
    /* ISDU 的传输缓冲：外部工作区或默认内嵌 fallback。 */
    iolink_isdu_bind_workspace(&ctx->dll.main.shared.isdu, workspace);
    /* ISDU 与事件引擎按实例能力位选运行时路径。 */
    iolink_isdu_bind_caps(&ctx->dll.main.shared.isdu, &ctx->dll.main.shared.caps);
    /* 在推导任何截止时间之前绑定应用时钟。设备层
     * 要求该时钟（NULL 已在上文被拒绝）。事件引擎共享
     * 同一时钟，使事件时序与 DLL 一致。 */
    ctx->dll.main.shared.clock.time_us = config->app->time_us;
    ctx->dll.main.shared.clock.self = config->app->self;
#if IOLINK_FEATURE_EVENTS
    ctx->dll.main.shared.events.clock = &ctx->dll.main.shared.clock;
    /* A.6.4 的两个窗口是运行时能力：栈配置非 0 时覆盖构建默认值。 */
    iolink_events_set_window(&ctx->dll.main.shared.events, config->stack->event_min_appear_ms,
                             config->stack->event_min_repeat_ms);
#endif
    /* iolink_dll_init() 在时钟绑定之前已采样了平台时钟。 */
    ctx->dll.main.master_last_activity_ms = dll_time_ms(&ctx->dll.main);
    ctx->dll.main.shared.isdu.direct_param_page2 = ctx->direct_param_page2;
    ctx->dll.main.shared.isdu.device_info = &ctx->device_info;
    ctx->dll.main.shared.isdu.od = config->od;
    ctx->dll.main.shared.isdu.od_self = config->app->self;
    ctx->dll.main.state_cb = device_state_cb;
    ctx->dll.main.state_cb_user = ctx;
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_init(&ctx->dll.main.ds);
    iolink_ds_set_flag_persist(&ctx->dll.main.ds, config->ds_flag_persist, config->app->self);
    iolink_ds_bind_workspace(&ctx->dll.main.ds, workspace);
    ctx->dll.main.shared.isdu.ds_ctx = &ctx->dll.main.ds;
    /* DS 参数访问统一走 OD 表：标准标签经参数上下文，应用参数经表项
       后端。DS 成员由表项 storage 位掩码决定（标准表 + config->od）。 */
    iolink_ds_bind_sources(&ctx->dll.main.ds, config->od, config->app->self);
    /* 按当前参数集刷新 Size/Checksum，使 0x0003 在首次上传前有效。 */
    iolink_ds_refresh(&ctx->dll.main.ds);
#else
    ctx->dll.main.shared.isdu.ds_ctx = NULL;
#endif
    /* 运行时日志 sink：一个配置被复制到所有会打印的子系统。
     * 必须在 iolink_dll_init()/iolink_ds_init() 之后绑定，因为这些
     * 初始化会清零各自的上下文；放在应用配置之前，使初始化期间的
     * 状态/波特率日志也能输出。 */
    iolink_device_set_log(ctx, config->app->log_write, config->log_level);

    device_apply_stack_config(ctx);
    ctx->master_lost_last = ctx->dll.main.master_lost;

    if (config->app->on_startup != NULL) {
        config->app->on_startup(config->app->self);
    }

    IOLINK_LOG_INFO(&ctx->dll.main.shared.log,
                    "device init: m_seq=%u pd_in=%u pd_out=%u od=%u baud=%u",
                    (unsigned) config->stack->m_seq_type, (unsigned) config->stack->pd_in_len,
                    (unsigned) config->stack->pd_out_len, (unsigned) ctx->dll.main.shared.od_width,
                    (unsigned) ctx->dll.main.baudrate);
    return 0;
}

int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config)
{
    if (config == NULL) {
        return IOLINK_DEVICE_INIT_ERR_NULL;
    }
    return device_init_with_workspace(ctx, config, config->workspace);
}

int iolink_device_get_phy_error(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->phy_error : 0;
}

iolink_workspace_t* iolink_device_get_workspace(const iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return NULL;
    }
    return ctx->dll.main.shared.isdu.workspace;
}

void iolink_device_set_log(iolink_device_ctx_t* ctx, iolink_log_write_fn write,
                           iolink_log_level_t level)
{
    if (ctx == NULL) {
        return;
    }
    iolink_app_t* IOLINK_NULLABLE self = (ctx->config != NULL) ? ctx->config->app->self : NULL;

    /* 字段更新必须对数据 ISR 原子：启用 ISR 日志时，中断可能正在
     * 读取同一个 sink。临界区在 SINGLE profile 中展开为空。 */
    IOLINK_CRITICAL_ENTER();
    /* 设备层没有自己的 sink：打印点使用 DLL 的那一份。 */
    iolink_log_sink_init(&ctx->dll.main.shared.log, write, self, level);
    /* 一个 sink 服务全部模块：它们只保存指针，这里统一指向它。 */
    ctx->dll.main.shared.isdu.log = &ctx->dll.main.shared.log;
#if IOLINK_FEATURE_DATA_STORAGE
    ctx->dll.main.ds.log = &ctx->dll.main.shared.log;
#endif
#if IOLINK_FEATURE_EVENTS
    ctx->dll.main.shared.events.log = &ctx->dll.main.shared.log;
#endif
    IOLINK_CRITICAL_EXIT();
}

/** 选择一个 ISR 未在流式发送且非活动的 PD 输入缓冲区。
 *
 * @return 缓冲区索引；当其他所有缓冲区仍被
 *         data ISR 租用时返回 0xFF。调用方随后必须保留当前 PD 输入，并在
 *         后续周期重试：写入已租用的缓冲区会破坏线上的应答。
 */
static uint8_t device_pd_in_free_index(const iolink_dll_main_t* dll)
{
    const uint8_t active = dll->shared.pd_in_active;
    const uint8_t captured = dll->shared.pd_in_captured;
    for (uint8_t i = 0U; i < IOLINK_PD_IN_BUFFERS; i++) {
        if ((i != active) && (i != captured)) {
            return i;
        }
    }
    return 0xFFU;
}

/**
 * @brief ST 风格的时间分区：当平台提供 Timer B tick 钩子时，
 * 主服务窗口仅在某个 tick 授权之后才运行。
 */
static bool device_service_window_ready(iolink_device_ctx_t* ctx)
{
    if ((ctx->dll.main.shared.phy != NULL) &&
        (ctx->dll.main.shared.phy->stack_timer_start != NULL) &&
        (iolink_dll_take_service_window(&ctx->dll.main) == 0U)) {
        return false;
    }
    return true;
}

#if IOLINK_FEATURE_EVENTS
/** @brief 事件引擎是否仍有排队或延迟的工作。 */
static bool device_events_need_service(const iolink_events_ctx_t* events)
{
    if ((events->count > 0U) || (events->pending_count > 0U)) {
        return true;
    }
    /* 某个槽位可能仍持有延迟的消失或重复冷却，
       即使没有任何事件被置位或排队。 */
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (events->pending[i].active) {
            return true;
        }
    }
    return false;
}
#endif

/**
 * @brief 在主循环中服务事件与数据存储状态机。
 *
 * 它们会修改共享的 ISDU/事件状态，因此仅在 ISDU 传输
 * 空闲时运行：这可避免 Event 标志与 DS 状态在
 * ISDU 应答中途发生变化。
 */
static void device_service_events_and_ds(iolink_device_ctx_t* ctx)
{
    if (!iolink_isdu_is_idle(&ctx->dll.main.shared.isdu)) {
        return;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->dll.main.ds.state != IOLINK_DS_STATE_IDLE) {
        iolink_ds_process(&ctx->dll.main.ds);
    }
#endif
#if IOLINK_FEATURE_EVENTS
    if (device_events_need_service(&ctx->dll.main.shared.events)) {
        iolink_events_process(&ctx->dll.main.shared.events);
    }
#endif
#if IOLINK_FEATURE_DATA_STORAGE && IOLINK_FEATURE_EVENTS
    /* DS_UPLOAD_REQ 是通知。仅在 DS 命令的 ISDU
     * 应答被读取之后（ISDU 空闲）才发布它，这样主站不会在该
     * 应答中看到 Event 标志，也不会在 ISDU 传输
     * 进行中插入诊断读取。 */
    if (ctx->dll.main.ds.upload_event_pending) {
        ctx->dll.main.ds.upload_event_pending = false;
        iolink_diag_raise_code(&ctx->dll.main.shared.events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    }
#endif
}

/** @brief 向应用报告主站丢失/主站在线的边沿事件。 */
static void device_notify_master_lost(iolink_device_ctx_t* ctx)
{
    if (ctx->config == NULL) {
        return;
    }
    if (ctx->dll.main.shared.state != IOLINK_DLL_STATE_OPERATE) {
        /* 启动/预操作暂停不是丢失事件。离开 OPERATE 时
           抑制该边沿并忘记任何过期状态。 */
        ctx->master_lost_last = false;
        return;
    }
    const bool lost = ctx->dll.main.master_lost;
    if (lost == ctx->master_lost_last) {
        return;
    }
    ctx->master_lost_last = lost;
    if (lost) {
        if (ctx->config->app->on_master_lost != NULL) {
            ctx->config->app->on_master_lost(ctx->config->app->self);
        }
    }
    else if (ctx->config->app->on_master_online != NULL) {
        ctx->config->app->on_master_online(ctx->config->app->self);
    }
}

/** @brief 将延迟的 ISDU 复位请求递交给应用。 */
static void device_dispatch_resets(iolink_device_ctx_t* ctx)
{
    if (ctx->dll.main.shared.isdu.reset_pending) {
        ctx->dll.main.shared.isdu.reset_pending = false;
        IOLINK_LOG_WARN(&ctx->dll.main.shared.log, "device reset requested by master");
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_DEVICE);
        }
    }
    if (ctx->dll.main.shared.isdu.app_reset_pending) {
        ctx->dll.main.shared.isdu.app_reset_pending = false;
        IOLINK_LOG_WARN(&ctx->dll.main.shared.log, "application reset requested by master");
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_APPLICATION);
        }
    }
}

/** @brief 在 OPERATE 状态下运行应用过程数据回调。 */
static void device_run_app_callbacks(iolink_device_ctx_t* ctx)
{
    if ((ctx->config == NULL) || (ctx->dll.main.shared.state != IOLINK_DLL_STATE_OPERATE)) {
        return;
    }
    if (ctx->config->app->on_pd_output != NULL) {
        ctx->config->app->on_pd_output(ctx->config->app->self, ctx->dll.main.pd_out,
                                       ctx->dll.main.shared.pd_out_len_current);
    }
    if (iolink_caps_has(ctx->dll.main.shared.caps, IOLINK_CAP_PD_IN_CALLBACK)) {
        /* 填写一个 ISR 未在流式发送的缓冲区，然后原子地发布，
         * 使 ISR 永远不会发送撕裂的 PD 输入。回调的返回值
         * 即 PD 有效标志。 */
        const uint8_t inactive = device_pd_in_free_index(&ctx->dll.main);
        if (inactive == 0xFFU) {
            ctx->dll.main.pd_in_skips++; /* 每个备用缓冲区都被租用 */
            return;                      /* 保留当前 PD 输入并在下一周期重试 */
        }
        /* 在临界区之外填写空闲缓冲区：在此期间 ISR
         * 继续发送已捕获的副本。 */
        const bool valid = ctx->config->app->on_pd_input(
            ctx->config->app->self,
            &ctx->dll.main.shared.pd_in[inactive][ctx->dll.main.shared.od_width],
            ctx->dll.main.pd_in_len_current);

        IOLINK_CRITICAL_ENTER();
        ctx->dll.main.shared.pd_in_len = ctx->dll.main.pd_in_len_current;
        ctx->dll.main.pd_valid = valid;
        ctx->dll.main.shared.pd_in_active = inactive;
        IOLINK_CRITICAL_EXIT();
    }
}

void iolink_device_process(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (!device_service_window_ready(ctx)) {
        return;
    }

    iolink_dll_process(&ctx->dll.main);

    device_service_events_and_ds(ctx);

    device_notify_master_lost(ctx);
    device_dispatch_resets(ctx);
    device_run_app_callbacks(ctx);
}

int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid)
{
    if ((ctx == NULL) || (data == NULL) || (len > IOLINK_PD_IN_MAX_SIZE)) {
        return -1;
    }
    /* 回调路径已选中时拒绝轮询调用：两条路径混用会让线上 PD 在
     * 回调输出与调用方数据之间跳变。 */
    if (iolink_caps_has(ctx->dll.main.shared.caps, IOLINK_CAP_PD_IN_CALLBACK)) {
        return IOLINK_DEVICE_INIT_ERR_PD_INPUT;
    }
    /* Annex A 固定类型的 PD 输入长度不可变。TYPE_2_V 可在
     * 配置的最大值内变化；两种情况都不得悄悄改变
     * 应答长度而偏离通告的布局。 */
    if (ctx->dll.main.shared.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        if (len > ctx->dll.main.pd_in_len_max) {
            return -1;
        }
    }
    else if ((ctx->dll.main.shared.m_seq_type != IOLINK_M_SEQ_TYPE_0) &&
             (len != ctx->dll.main.pd_in_len_max)) {
        /* 固定 Annex A 类型：PD 输入宽度不可变。TYPE_0
         * 完全不带 PD，因此通用更新无害且保持宽松。 */
        return -1;
    }

    /* PD 输入位于应答缓冲区中 OD 头之后；填写一个
     * ISR 未在流式发送的缓冲区，并用原子索引交换发布。没有空闲
     * 缓冲区意味着 ISR 仍占用全部缓冲区：保留当前 PD 输入。 */
    const uint8_t inactive = device_pd_in_free_index(&ctx->dll.main);
    if (inactive == 0xFFU) {
        ctx->dll.main.pd_in_skips++; /* 每个备用缓冲区都被租用：稍后重试 */
        return -1;
    }
    /* 在临界区之外填写空闲缓冲区：ISR 只会
     * 捕获 pd_in_active，因此既非活动也未被捕获的缓冲区
     * 不会被它读取。只有发布（长度、有效性、索引）必须
     * 对 ISR 是原子的。 */
    const bool len_changed = (ctx->dll.main.pd_in_len_current != (uint8_t) len);
    (void) memcpy(&ctx->dll.main.shared.pd_in[inactive][ctx->dll.main.shared.od_width], data, len);
    IOLINK_CRITICAL_ENTER();
    ctx->dll.main.shared.pd_in_len = (uint8_t) len;
    ctx->dll.main.pd_in_len_current = (uint8_t) len;
    ctx->dll.main.pd_valid = valid;
    ctx->dll.main.shared.pd_in_active = inactive;
    IOLINK_CRITICAL_EXIT();
    if (len_changed) {
        /* TYPE_2_V 允许运行期改变 PD-in 宽度：DPP1 的 0x05/0x02 必须跟着
         * 失效重建（失效在临界区外执行，整页构建不占用关中断区间）。 */
        iolink_isdu_dp_page1_invalidate(&ctx->dll.main.shared.isdu);
    }

    return 0;
}

void iolink_device_process_isr(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    iolink_dll_process_isr(&ctx->dll.main);
}

int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx, uint8_t* data, size_t len)
{
    uint8_t read_len;

    if ((ctx == NULL) || (data == NULL)) {
        return -1;
    }

    /* pd_out 由同一（主）上下文中的帧分发写入，因此在
     * 单调用方约定下无需加锁。 */
    read_len = (len < ctx->dll.main.shared.pd_out_len_current)
                   ? (uint8_t) len
                   : ctx->dll.main.shared.pd_out_len_current;
    (void) memcpy(data, ctx->dll.main.pd_out, read_len);

    return (int) read_len;
}

void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler)
{
    if (ctx != NULL) {
        ctx->reset_handler = handler;
    }
}

#if IOLINK_FEATURE_EVENTS
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? &ctx->dll.main.shared.events : NULL;
}
#endif
#if IOLINK_FEATURE_DATA_STORAGE
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? &ctx->dll.main.ds : NULL;
}
#endif

iolink_caps_t iolink_device_capabilities(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->dll.main.shared.caps : (iolink_caps_t) 0U;
}

iolink_caps_t iolink_device_set_capabilities(iolink_device_ctx_t* ctx, iolink_caps_t caps)
{
    if ((ctx == NULL) || (ctx->config == NULL)) {
        return (iolink_caps_t) 0U;
    }
    /* 与 PHY vtable、应用回调绑定的位由 resolve() 重新推导，调用方
     * 只能修改运行时开关（兼容位、SystemCommand、访问锁）。 */
    const iolink_caps_t bound_mask = IOLINK_CAP_RX | IOLINK_CAP_TX | IOLINK_CAP_WAKEUP |
                                     IOLINK_CAP_SIO_CQ | IOLINK_CAP_TX_BUSY |
                                     IOLINK_CAP_REQUEST_SERVICE | IOLINK_CAP_PHY_FAULTS |
                                     IOLINK_CAP_REQUIRE_ISR_TX | IOLINK_CAP_PD_IN_CALLBACK;
    const iolink_caps_t bound =
        iolink_caps_resolve(ctx->config->phy, ctx->config->stack, ctx->config->app, NULL);
    const iolink_build_caps_t* build = iolink_caps_build();

    caps = (caps & (iolink_caps_t) ~bound_mask) | (bound & bound_mask);
    if (!iolink_caps_built(build, IOLINK_CAPB_EVENTS)) {
        caps &= (iolink_caps_t) ~IOLINK_CAP_EVENTS;
    }
    if (!iolink_caps_built(build, IOLINK_CAPB_SYSTEM_COMMAND)) {
        caps &= (iolink_caps_t) ~IOLINK_CAP_SYSTEM_COMMAND;
    }
    ctx->dll.main.shared.caps = caps;
    /* 应答计划的内容依赖能力位（例如 IOLINK_CAP_COMPAT_T0_READ），
     * 而发布键不包含能力位；必须先失效，下一轮主泵才会重发布。 */
    iolink_dll_invalidate_reply_plans(&ctx->dll.main);
    return caps;
}

bool iolink_device_master_lost(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) && ctx->dll.main.master_lost;
}

iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->dll.main.shared.state : IOLINK_DLL_STATE_STARTUP;
}

iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? iolink_dll_get_phy_mode(&ctx->dll.main) : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? iolink_dll_get_baudrate(&ctx->dll.main) : IOLINK_BAUDRATE_COM2;
}

void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats)
{
    if (ctx != NULL) {
        iolink_dll_get_stats(&ctx->dll.main, out_stats);
    }
}

void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable)
{
    if (ctx != NULL) {
        iolink_dll_set_timing_enforcement(&ctx->dll.main, enable);
    }
}

void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us)
{
    if (ctx != NULL) {
        iolink_dll_set_t_ren_limit_us(&ctx->dll.main, limit_us);
    }
}

iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? (iolink_m_seq_type_t) ctx->dll.main.shared.m_seq_type
                         : IOLINK_M_SEQ_TYPE_0;
}

uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx)
{
    uint8_t pd_in_len = 0U;

    if (ctx != NULL) {
        iolink_dll_get_pd_length(&ctx->dll.main, &pd_in_len, NULL);
    }

    return pd_in_len;
}

uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx)
{
    uint8_t pd_out_len = 0U;

    if (ctx != NULL) {
        iolink_dll_get_pd_length(&ctx->dll.main, NULL, &pd_out_len);
    }

    return pd_out_len;
}

int iolink_device_set_pd_length(iolink_device_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if (ctx == NULL) {
        return -1;
    }
    /* 固定 Annex A 类型的 PD 布局不可变；只有 TYPE_2_V 可变。 */
    if (ctx->dll.main.shared.m_seq_type != IOLINK_M_SEQ_TYPE_2_V) {
        return -2;
    }
    if ((pd_in_len > ctx->dll.main.pd_in_len_max) || (pd_out_len > ctx->dll.main.pd_out_len_max)) {
        return -1;
    }
    return iolink_dll_set_pd_length(&ctx->dll.main, pd_in_len, pd_out_len);
}
