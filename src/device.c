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
#include "iolinki/platform.h"
#include "dll_internal.h"
#include <string.h>

#if IOLINK_APP_PD_INPUT_CALLBACK
/* 在 application.h 中声明的 profile 标记：仅存在于回调构建中，使
 * 不匹配的应用在链接时失败，而不会破坏 vtable。 */
const uint8_t iolink_app_pd_input_callback_profile = 1U;
#endif

/** @brief DLL 状态变化回调：分发到应用的 startup/preoperate/operate 钩子。 */
static void device_state_cb(void* user, iolink_dll_state_t state)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if ((ctx == NULL) || (ctx->config == NULL) || (ctx->config->app_callbacks == NULL)) {
        return;
    }

    if (ctx->config->app_callbacks->on_state_change != NULL) {
        ctx->config->app_callbacks->on_state_change(ctx->config->user, state);
    }

    switch (state) {
        case IOLINK_DLL_STATE_STARTUP:
            if (ctx->config->app_callbacks->on_startup != NULL) {
                ctx->config->app_callbacks->on_startup(ctx->config->user);
            }
            break;
        case IOLINK_DLL_STATE_PREOPERATE:
            if (ctx->config->app_callbacks->on_preoperate != NULL) {
                ctx->config->app_callbacks->on_preoperate(ctx->config->user);
            }
            break;
        case IOLINK_DLL_STATE_OPERATE:
            if (ctx->config->app_callbacks->on_operate != NULL) {
                ctx->config->app_callbacks->on_operate(ctx->config->user);
            }
            break;
        default:
            break;
    }
}

#if IOLINK_FEATURE_DATA_STORAGE
/** @brief 统一的 DS 参数读取：标准标签经 params 上下文，
 * 其余全部经厂商服务。 */
static int device_ds_read(void* user, uint16_t index, uint8_t subindex, uint8_t* out, size_t cap)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;
    int n;

    if ((ctx == NULL) || (out == NULL)) {
        return -1;
    }
    n = iolink_params_ctx_get(&ctx->params, index, subindex, out, cap);
    if (n >= 0) {
        return n;
    }
    if ((ctx->config != NULL) && (ctx->config->vendor_service != NULL)) {
        size_t outlen = cap;
        uint8_t err = ctx->config->vendor_service(ctx->config->user, index, subindex, false, NULL,
                                                  0U, out, &outlen);
        if (err == 0U) {
            return (int) outlen;
        }
    }
    return -1;
}

/** @brief 统一的 DS 参数写入：标准标签经 params 上下文，
 * 其余全部经厂商服务。 */
static int device_ds_write(void* user, uint16_t index, uint8_t subindex, const uint8_t* data,
                           size_t len, bool persist)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if (ctx == NULL) {
        return -1;
    }
    if (iolink_params_ctx_set(&ctx->params, index, subindex, data, len, persist) == 0) {
        return 0;
    }
    if ((ctx->config != NULL) && (ctx->config->vendor_service != NULL)) {
        size_t outlen = 0U;
        uint8_t err = ctx->config->vendor_service(ctx->config->user, index, subindex, true, data,
                                                  len, NULL, &outlen);
        if (err == 0U) {
            return 0;
        }
    }
    return -1;
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */

/** @brief 某种 M-sequence 类型的默认 OD 字节数（Table A.10）。
 *
 * TYPE_1_2 携带 2 个 OD 字节（编码 1），TYPE_1_V 为 8 字节（编码 6），
 * TYPE_2_V 默认为 2 个 OD 字节（编码 5）；其余情况默认为 1。
 * TYPE_1_1 在初始化时被 ISR 传输层拒绝；默认值 1 仅作为
 * 安全的占位值。 */
static uint8_t device_default_od_width(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_2:
            return 2U;
        case IOLINK_M_SEQ_TYPE_1_V:
            return 8U;
        case IOLINK_M_SEQ_TYPE_2_V:
            return 2U;
        default:
            return 1U;
    }
}

/** @brief 将用户协议栈配置复制到 DLL 上下文，并推导长度/时序。 */
static void device_apply_stack_config(iolink_device_ctx_t* ctx)
{
    ctx->dll.m_seq_type = (uint8_t) ctx->stack_config.m_seq_type;
    ctx->dll.pd_in_len = ctx->stack_config.pd_in_len;
    ctx->dll.pd_out_len = ctx->stack_config.pd_out_len;
    ctx->dll.min_cycle_time_app_us = (uint32_t) ctx->stack_config.min_cycle_time * 100U;
    ctx->dll.master_lost_cycles = ctx->stack_config.master_lost_cycles;
    ctx->dll.t_pd_delay_us = ctx->stack_config.t_pd_us;
    if (ctx->dll.t_pd_delay_us > 0U) {
        ctx->dll.t_pd_deadline_us = dll_time_us(&ctx->dll) + (uint64_t) ctx->dll.t_pd_delay_us;
    }
    else {
        ctx->dll.t_pd_deadline_us = 0U;
    }

    /* Table A.10：TYPE_2_1/TYPE_2_2 携带一个 On-request Data 字节；TYPE_2_V 是
       两字节变体（OPERATE M-sequence 编码 5）。 */
    ctx->dll.od_len = (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;

    ctx->dll.pd_in_len_current = ctx->dll.pd_in_len;
    ctx->dll.pd_out_len_current = ctx->dll.pd_out_len;
    ctx->dll.pd_in_len_max = ctx->dll.pd_in_len;
    ctx->dll.pd_out_len_max = ctx->dll.pd_out_len;

    /* 双泵传输：ISR 组帧请求，并基于共享状态
     * 组装应答，因此 DLL 需要 OD 宽度来确定应答布局。
     * OD 宽度默认取 Table A.10 的值；config.od_width 选择
     * 8/32 字节扩展。 */
    ctx->dll.od_width = ctx->stack_config.od_width;
    if (ctx->dll.od_width == 0U) {
        ctx->dll.od_width = device_default_od_width((uint8_t) ctx->dll.m_seq_type);
    }
    /* ISR 传输层始终使用 od_width（0/1/2/8/32）；保持 od_len 与之
     * 同步，避免旧的 1/2 假设渗入该构建。 */
    ctx->dll.od_len = ctx->dll.od_width;

    /* 在根据波特率推导最小周期时间之前，先采用
     * 配置的操作 COM（及其时序限制）。 */
    if (ctx->stack_config.baudrate_set) {
        (void) iolink_dll_set_operating_baudrate(&ctx->dll, ctx->stack_config.baudrate);
    }

    /* 通告/实际提供的最小周期时间由实际的
     * M-sequence（PD/OD 长度与波特率）推导，并受应用下限约束。 */
    ctx->dll.min_cycle_time_us = (uint32_t) iolink_dll_compute_min_cycle_time(&ctx->dll) * 100U;
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
    stack = &config->stack;

    if (config->time_us == NULL) {
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

    /* Table A.10 的 OD 宽度；0 表示由 M-sequence 类型推导。 */
    od = stack->od_width;
    if (od == 0U) {
        od = device_default_od_width((uint8_t) stack->m_seq_type);
    }
    if ((uint32_t) od > (uint32_t) IOLINK_OD_MAX_SIZE) {
        return IOLINK_DEVICE_INIT_ERR_OD_CAPACITY;
    }
    if ((od != 1U) && (od != 2U) && (od != 8U) && (od != 32U)) {
        return IOLINK_DEVICE_INIT_ERR_OD_WIDTH;
    }

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

#if IOLINK_FEATURE_DATA_STORAGE
    if ((config->storage == NULL) || (config->storage->read == NULL) ||
        (config->storage->write == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_STORAGE;
    }
#endif

#if IOLINK_APP_PD_INPUT_CALLBACK
    /* 该 profile 中回调是唯一的 PD 输入来源：接受
     * 缺少它的配置会静默地不发布任何过程数据。 */
    if ((config->app_callbacks == NULL) || (config->app_callbacks->on_pd_input == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_PD_INPUT;
    }
#endif

#if (IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME) && IOLINK_PHY_FEATURE_TX_BUSY
    /* 该构建承诺基于 tx_busy() 释放租约；NULL 钩子会
     * 退化为永不释放的 PD 输入租约。 */
    if (config->phy.tx_busy == NULL) {
        return IOLINK_DEVICE_INIT_ERR_PHY;
    }
#endif

    return IOLINK_DEVICE_INIT_OK;
}

int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config)
{
    int err;

    if ((ctx == NULL) || (config == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_NULL;
    }

    err = device_config_validate(config);
    if (err != IOLINK_DEVICE_INIT_OK) {
        IOLINK_LOG_ERROR("device init rejected: err=%d", err);
        return err;
    }

    (void) memset(ctx, 0, sizeof(*ctx));
    (void) memcpy(&ctx->stack_config, &config->stack, sizeof(ctx->stack_config));
    ctx->config = config;

    if (config->phy.init != NULL) {
        err = config->phy.init(config->phy.user);
        if (err != 0) {
            IOLINK_LOG_ERROR("phy init failed: err=%d", err);
            return err;
        }
    }

    iolink_device_info_ctx_init(&ctx->device_info, config->device_info);
    iolink_params_ctx_init(&ctx->params, &ctx->device_info);
    iolink_dll_init(&ctx->dll, &config->phy);
    /* 在推导任何截止时间之前绑定应用时钟。设备层
     * 要求该时钟（NULL 已在上文被拒绝）。事件引擎共享
     * 同一时钟，使事件时序与 DLL 一致。 */
    ctx->dll.time_us = config->time_us;
    ctx->dll.time_user = config->user;
#if IOLINK_FEATURE_EVENTS
    ctx->dll.events.time_us = config->time_us;
    ctx->dll.events.time_user = config->user;
#endif
    /* iolink_dll_init() 在时钟绑定之前已采样了平台时钟。 */
    ctx->dll.master_last_activity_ms = dll_time_ms(&ctx->dll);
    ctx->dll.isdu.direct_param_page2 = ctx->direct_param_page2;
    ctx->dll.isdu.params_ctx = &ctx->params;
    ctx->dll.isdu.vendor_service = config->vendor_service;
    ctx->dll.isdu.vendor_user = config->user;
    ctx->dll.state_cb = device_state_cb;
    ctx->dll.state_cb_user = ctx;
    /* 一个后端同时服务标准参数与数据存储镜像。 */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_params_bind_storage(&ctx->params, config->storage);
    if (config->storage != NULL) {
        iolink_ds_init(&ctx->dll.ds, config->storage);
        ctx->dll.isdu.ds_ctx = &ctx->dll.ds;
    }
    iolink_ds_bind_params(&ctx->dll.ds, &ctx->params);
    /* 将 DS 参数访问路由到设备层：标准标签经
       params 上下文，其余（厂商 0x0040+/0x0100+）经厂商
       服务。保留默认描述符集（三个标签）。 */
    iolink_ds_bind_param_access(&ctx->dll.ds, device_ds_read, device_ds_write, ctx, NULL, 0U);
    /* 启动时若不存在持久化镜像，则建立 DS 镜像/校验和，
       使 Data_Storage_Size 与 Parameter_Checksum 在首次
       上传之前就已有效。 */
    if (ctx->dll.ds.image_len == 0U) {
        (void) iolink_ds_build_image(&ctx->dll.ds);
    }
#else
    iolink_params_bind_storage(&ctx->params, NULL);
    ctx->dll.isdu.ds_ctx = NULL;
#endif
    device_apply_stack_config(ctx);
    ctx->master_lost_last = ctx->dll.master_lost;

    if ((config->app_callbacks != NULL) && (config->app_callbacks->on_startup != NULL)) {
        config->app_callbacks->on_startup(config->user);
    }

    IOLINK_LOG_INFO("device init: m_seq=%u pd_in=%u pd_out=%u od=%u baud=%u",
                    (unsigned) config->stack.m_seq_type, (unsigned) config->stack.pd_in_len,
                    (unsigned) config->stack.pd_out_len, (unsigned) ctx->dll.od_width,
                    (unsigned) ctx->dll.baudrate);
    return 0;
}

/** 选择一个 ISR 未在流式发送且非活动的 PD 输入缓冲区。
 *
 * @return 缓冲区索引；当其他所有缓冲区仍被
 *         data ISR 租用时返回 0xFF。调用方随后必须保留当前 PD 输入，并在
 *         后续周期重试：写入已租用的缓冲区会破坏线上的应答。
 */
static uint8_t device_pd_in_free_index(const iolink_dll_ctx_t* dll)
{
    const uint8_t active = dll->pd_in_active;
    const uint8_t captured = dll->pd_in_captured;
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
    if ((ctx->dll.phy != NULL) && (ctx->dll.phy->stack_timer_start != NULL) &&
        (iolink_dll_take_service_window(&ctx->dll) == 0U)) {
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
    if (!iolink_isdu_is_idle(&ctx->dll.isdu)) {
        return;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->dll.ds.state != IOLINK_DS_STATE_IDLE) {
        iolink_ds_process(&ctx->dll.ds);
    }
#endif
#if IOLINK_FEATURE_EVENTS
    if (device_events_need_service(&ctx->dll.events)) {
        iolink_events_process(&ctx->dll.events);
    }
#endif
#if IOLINK_FEATURE_DATA_STORAGE && IOLINK_FEATURE_EVENTS
    /* DS_UPLOAD_REQ 是通知。仅在 DS 命令的 ISDU
     * 应答被读取之后（ISDU 空闲）才发布它，这样主站不会在该
     * 应答中看到 Event 标志，也不会在 ISDU 传输
     * 进行中插入诊断读取。 */
    if (ctx->dll.ds.upload_event_pending) {
        ctx->dll.ds.upload_event_pending = false;
        iolink_event_trigger(&ctx->dll.events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST,
                             IOLINK_EVENT_TYPE_NOTIFICATION);
    }
#endif
}

/** @brief 向应用报告主站丢失/主站在线的边沿事件。 */
static void device_notify_master_lost(iolink_device_ctx_t* ctx)
{
    if ((ctx->config == NULL) || (ctx->config->app_callbacks == NULL)) {
        return;
    }
    if (ctx->dll.state != IOLINK_DLL_STATE_OPERATE) {
        /* 启动/预操作暂停不是丢失事件。离开 OPERATE 时
           抑制该边沿并忘记任何过期状态。 */
        ctx->master_lost_last = false;
        return;
    }
    const bool lost = ctx->dll.master_lost;
    if (lost == ctx->master_lost_last) {
        return;
    }
    ctx->master_lost_last = lost;
    if (lost) {
        if (ctx->config->app_callbacks->on_master_lost != NULL) {
            ctx->config->app_callbacks->on_master_lost(ctx->config->user);
        }
    }
    else if (ctx->config->app_callbacks->on_master_online != NULL) {
        ctx->config->app_callbacks->on_master_online(ctx->config->user);
    }
}

/** @brief 将延迟的 ISDU 复位请求递交给应用。 */
static void device_dispatch_resets(iolink_device_ctx_t* ctx)
{
    if (ctx->dll.isdu.reset_pending) {
        ctx->dll.isdu.reset_pending = false;
        IOLINK_LOG_WARN("device reset requested by master");
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_DEVICE);
        }
    }
    if (ctx->dll.isdu.app_reset_pending) {
        ctx->dll.isdu.app_reset_pending = false;
        IOLINK_LOG_WARN("application reset requested by master");
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_APPLICATION);
        }
    }
}

/** @brief 在 OPERATE 状态下运行应用过程数据回调。 */
static void device_run_app_callbacks(iolink_device_ctx_t* ctx)
{
    if ((ctx->config == NULL) || (ctx->config->app_callbacks == NULL) ||
        (ctx->dll.state != IOLINK_DLL_STATE_OPERATE)) {
        return;
    }
    if (ctx->config->app_callbacks->on_pd_output != NULL) {
        ctx->config->app_callbacks->on_pd_output(ctx->config->user, ctx->dll.pd_out,
                                                 ctx->dll.pd_out_len_current);
    }
#if IOLINK_APP_PD_INPUT_CALLBACK
    if (ctx->config->app_callbacks->on_pd_input != NULL) {
        /* 填写一个 ISR 未在流式发送的缓冲区，然后原子地发布，
         * 使 ISR 永远不会发送撕裂的 PD 输入。回调的返回值
         * 即 PD 有效标志。 */
        const uint8_t inactive = device_pd_in_free_index(&ctx->dll);
        if (inactive == 0xFFU) {
            ctx->dll.pd_in_skips++; /* 每个备用缓冲区都被租用 */
            return;                 /* 保留当前 PD 输入并在下一周期重试 */
        }
        /* 在临界区之外填写空闲缓冲区：在此期间 ISR
         * 继续发送已捕获的副本。 */
        const bool valid = ctx->config->app_callbacks->on_pd_input(
            ctx->config->user, &ctx->dll.pd_in[inactive][ctx->dll.od_width],
            ctx->dll.pd_in_len_current);

        IOLINK_CRITICAL_ENTER();
        ctx->dll.pd_in_len = ctx->dll.pd_in_len_current;
        ctx->dll.pd_valid = valid;
        ctx->dll.pd_in_active = inactive;
        IOLINK_CRITICAL_EXIT();
    }
#endif
}

void iolink_device_process(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (!device_service_window_ready(ctx)) {
        return;
    }

    iolink_dll_process(&ctx->dll);

    device_service_events_and_ds(ctx);

    device_notify_master_lost(ctx);
    device_dispatch_resets(ctx);
    device_run_app_callbacks(ctx);
}

#if !IOLINK_APP_PD_INPUT_CALLBACK
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid)
{
    if ((ctx == NULL) || (data == NULL) || (len > IOLINK_PD_IN_MAX_SIZE)) {
        return -1;
    }
    /* Annex A 固定类型的 PD 输入长度不可变。TYPE_2_V 可在
     * 配置的最大值内变化；两种情况都不得悄悄改变
     * 应答长度而偏离通告的布局。 */
    if (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        if (len > ctx->dll.pd_in_len_max) {
            return -1;
        }
    }
    else if ((ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_0) && (len != ctx->dll.pd_in_len_max)) {
        /* 固定 Annex A 类型：PD 输入宽度不可变。TYPE_0
         * 完全不带 PD，因此通用更新无害且保持宽松。 */
        return -1;
    }

    /* PD 输入位于应答缓冲区中 OD 头之后；填写一个
     * ISR 未在流式发送的缓冲区，并用原子索引交换发布。没有空闲
     * 缓冲区意味着 ISR 仍占用全部缓冲区：保留当前 PD 输入。 */
    const uint8_t inactive = device_pd_in_free_index(&ctx->dll);
    if (inactive == 0xFFU) {
        ctx->dll.pd_in_skips++; /* 每个备用缓冲区都被租用：稍后重试 */
        return -1;
    }
    /* 在临界区之外填写空闲缓冲区：ISR 只会
     * 捕获 pd_in_active，因此既非活动也未被捕获的缓冲区
     * 不会被它读取。只有发布（长度、有效性、索引）必须
     * 对 ISR 是原子的。 */
    (void) memcpy(&ctx->dll.pd_in[inactive][ctx->dll.od_width], data, len);
    IOLINK_CRITICAL_ENTER();
    ctx->dll.pd_in_len = (uint8_t) len;
    ctx->dll.pd_in_len_current = (uint8_t) len;
    ctx->dll.pd_valid = valid;
    ctx->dll.pd_in_active = inactive;
    IOLINK_CRITICAL_EXIT();

    return 0;
}
#endif /* !IOLINK_APP_PD_INPUT_CALLBACK */

void iolink_device_process_isr(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    iolink_dll_process_isr(&ctx->dll);
}

int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx, uint8_t* data, size_t len)
{
    uint8_t read_len;

    if ((ctx == NULL) || (data == NULL)) {
        return -1;
    }

    /* pd_out 由同一（主）上下文中的帧分发写入，因此在
     * 单调用方约定下无需加锁。 */
    read_len = (len < ctx->dll.pd_out_len_current) ? (uint8_t) len : ctx->dll.pd_out_len_current;
    (void) memcpy(data, ctx->dll.pd_out, read_len);

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
    return (ctx != NULL) ? &ctx->dll.events : NULL;
}
#endif
#if IOLINK_FEATURE_DATA_STORAGE
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? &ctx->dll.ds : NULL;
}
#endif

bool iolink_device_master_lost(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) && ctx->dll.master_lost;
}

iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->dll.state : IOLINK_DLL_STATE_STARTUP;
}

iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? iolink_dll_get_phy_mode(&ctx->dll) : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? iolink_dll_get_baudrate(&ctx->dll) : IOLINK_BAUDRATE_COM2;
}

void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats)
{
    if (ctx != NULL) {
        iolink_dll_get_stats(&ctx->dll, out_stats);
    }
}

void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable)
{
    if (ctx != NULL) {
        iolink_dll_set_timing_enforcement(&ctx->dll, enable);
    }
}

void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us)
{
    if (ctx != NULL) {
        iolink_dll_set_t_ren_limit_us(&ctx->dll, limit_us);
    }
}

iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? (iolink_m_seq_type_t) ctx->dll.m_seq_type : IOLINK_M_SEQ_TYPE_0;
}

uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx)
{
    uint8_t pd_in_len = 0U;

    if (ctx != NULL) {
        iolink_dll_get_pd_length(&ctx->dll, &pd_in_len, NULL);
    }

    return pd_in_len;
}

uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx)
{
    uint8_t pd_out_len = 0U;

    if (ctx != NULL) {
        iolink_dll_get_pd_length(&ctx->dll, NULL, &pd_out_len);
    }

    return pd_out_len;
}

int iolink_device_set_pd_length(iolink_device_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if (ctx == NULL) {
        return -1;
    }
    /* 固定 Annex A 类型的 PD 布局不可变；只有 TYPE_2_V 可变。 */
    if (ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_2_V) {
        return -2;
    }
    if ((pd_in_len > ctx->dll.pd_in_len_max) || (pd_out_len > ctx->dll.pd_out_len_max)) {
        return -1;
    }
    return iolink_dll_set_pd_length(&ctx->dll, pd_in_len, pd_out_len);
}
