/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file device.c
 * @brief Device application layer: top-level stack lifecycle and I/O.
 * @ingroup iolinki_device
 *
 * Wires together the DLL, ISDU, parameter, device-info and data-storage
 * sub-contexts, drives the per-cycle processing, and exposes process-data
 * exchange and status accessors to the application.
 */

#include "iolinki/device.h"
#include "iolinki/platform.h"
#include "dll_internal.h"
#include <string.h>

/** @brief DLL state-change callback: dispatches to the app startup/preoperate/operate hooks. */
static void device_state_cb(void* user, iolink_dll_state_t state)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if ((ctx == NULL) || (ctx->config == NULL) || (ctx->config->app_callbacks == NULL)) {
        return;
    }

    if (ctx->config->app_callbacks->on_state_change != NULL) {
        ctx->config->app_callbacks->on_state_change(state);
    }

    switch (state) {
        case IOLINK_DLL_STATE_STARTUP:
            if (ctx->config->app_callbacks->on_startup != NULL) {
                ctx->config->app_callbacks->on_startup();
            }
            break;
        case IOLINK_DLL_STATE_PREOPERATE:
            if (ctx->config->app_callbacks->on_preoperate != NULL) {
                ctx->config->app_callbacks->on_preoperate();
            }
            break;
        case IOLINK_DLL_STATE_OPERATE:
            if (ctx->config->app_callbacks->on_operate != NULL) {
                ctx->config->app_callbacks->on_operate();
            }
            break;
        default:
            break;
    }
}

#if IOLINK_FEATURE_DATA_STORAGE
/** @brief Unified DS parameter read: standard tags via the params context,
 * everything else via the vendor service. */
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

/** @brief Unified DS parameter write: standard tags via the params context,
 * everything else via the vendor service. */
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

/** @brief Default OD octet count for an M-sequence type (Table A.10).
 *
 * TYPE_1_2 carries 2 OD octets (code 1), TYPE_1_V 8 octets (code 6) and
 * TYPE_2_V defaults to 2 OD octets (code 5); everything else defaults to 1.
 * TYPE_1_1 is rejected at init by the ISR transport; the default of 1 is only
 * a safe placeholder. */
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

/** @brief Copy the user stack configuration into the DLL context and derive lengths/timing. */
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

    /* Table A.10: TYPE_2_1/TYPE_2_2 carry one On-request Data octet; TYPE_2_V is
       the two-octet variant (OPERATE M-sequence code 5). */
    ctx->dll.od_len = (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;

    ctx->dll.pd_in_len_current = ctx->dll.pd_in_len;
    ctx->dll.pd_out_len_current = ctx->dll.pd_out_len;
    ctx->dll.pd_in_len_max = ctx->dll.pd_in_len;
    ctx->dll.pd_out_len_max = ctx->dll.pd_out_len;

    /* Two-pump transport: the ISR frames requests and transmits the pre-staged
     * cyclic reply, so the DLL needs the OD width and whether the bound PHY can
     * send a staged reply from ISR context. The OD width defaults to the
     * Table A.10 value; config.od_width selects the 8/32-byte extension. */
    ctx->dll.od_width = ctx->stack_config.od_width;
    if (ctx->dll.od_width == 0U) {
        ctx->dll.od_width = device_default_od_width((uint8_t) ctx->dll.m_seq_type);
    }
    /* The ISR transport uses od_width (0/1/2/8/32) throughout; keep od_len in
     * sync so no legacy 1/2 assumption leaks into this build. */
    ctx->dll.od_len = ctx->dll.od_width;
    ctx->dll.cyclic_reply_precached =
        (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) && dll_phy_has_tx(&ctx->dll);

    /* Adopt the configured operating COM (and its timing limits) before the
     * minimum cycle time is derived from the baudrate. */
    if (ctx->stack_config.baudrate_set) {
        (void) iolink_dll_set_operating_baudrate(&ctx->dll, ctx->stack_config.baudrate);
    }

    /* The advertised/served minimum cycle time is derived from the actual
     * M-sequence (PD/OD lengths and baud), clamped by the app floor. */
    ctx->dll.min_cycle_time_us = (uint32_t) iolink_dll_compute_min_cycle_time(&ctx->dll) * 100U;
}

size_t iolink_device_ctx_size(void)
{
    return sizeof(iolink_device_ctx_t);
}

/** @brief One fixed Annex A layout: PD in/out and the accepted OD width(s). */
typedef struct
{
    uint8_t mseq;
    uint8_t pd_in;
    uint8_t pd_out;
    uint8_t od;
    uint8_t od_alt; /* second accepted OD width, 0 when none (od is never 0 here) */
} device_fixed_layout_t;

static const device_fixed_layout_t g_fixed_layouts[] = {
    {IOLINK_M_SEQ_TYPE_1_2, 0U, 0U, 2U, 0U}, {IOLINK_M_SEQ_TYPE_1_V, 0U, 0U, 8U, 32U},
    {IOLINK_M_SEQ_TYPE_2_1, 1U, 0U, 1U, 0U}, {IOLINK_M_SEQ_TYPE_2_2, 2U, 0U, 1U, 0U},
    {IOLINK_M_SEQ_TYPE_2_3, 0U, 1U, 1U, 0U}, {IOLINK_M_SEQ_TYPE_2_4, 0U, 2U, 1U, 0U},
    {IOLINK_M_SEQ_TYPE_2_5, 1U, 1U, 1U, 0U},
};

/** @brief Enforce the fixed per-type PD/OD layout of Annex A (ISR transport).
 *
 * Figures A.7-A.13 define a fixed number of PD and OD octets per M-sequence
 * type. A device that advertises one of these types must use exactly that
 * layout, otherwise the master and device disagree about the frame length.
 * TYPE_1_1 is legacy interleave only and shall not be implemented by a
 * V1.1.5 device (Table A.10 NOTE 3), so it is rejected. TYPE_0 and the
 * variable TYPE_2_V keep the existing range checks.
 */
static int device_validate_fixed_mseq(const iolink_config_t* stack, uint8_t od)
{
    /* TYPE_1_1 is legacy interleave only and shall not be implemented by a
     * V1.1.5 device (Table A.10 NOTE 3). */
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
    return IOLINK_DEVICE_INIT_OK; /* TYPE_0 and the variable TYPE_2_V */
}

/** @brief Validate the stack configuration before touching the context or PHY. */
static int device_config_validate(const iolink_device_config_t* config)
{
    const iolink_config_t* stack;
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

    /* Table A.10 OD widths; 0 derives from the M-sequence type. */
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

    if ((config->lock == NULL) != (config->unlock == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_LOCK_PAIR;
    }

#if IOLINK_NEED_CRITICAL
    /* Only a profile that actually serialises against an interrupt needs a
     * real critical section; a SINGLE profile never references the hooks. */
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
        return err;
    }

    (void) memset(ctx, 0, sizeof(*ctx));
    (void) memcpy(&ctx->stack_config, &config->stack, sizeof(ctx->stack_config));
    ctx->config = config;

    if (config->phy.init != NULL) {
        err = config->phy.init(config->phy.user);
        if (err != 0) {
            return err;
        }
    }

    iolink_device_info_ctx_init(&ctx->device_info, config->device_info);
    iolink_params_ctx_init(&ctx->params, &ctx->device_info);
    iolink_dll_init(&ctx->dll, &config->phy);
    /* Bind the application clock before any deadline is derived. The device
     * layer requires it (NULL is rejected above). The event engine shares the
     * same clock so event timing matches the DLL. */
    ctx->dll.time_us = config->time_us;
    ctx->dll.time_user = config->user;
#if IOLINK_FEATURE_EVENTS
    ctx->dll.events.time_us = config->time_us;
    ctx->dll.events.time_user = config->user;
#endif
    /* iolink_dll_init() sampled the platform clock before it was bound. */
    ctx->dll.master_last_activity_ms = dll_time_ms(&ctx->dll);
    /* Main-context reply staging vs the data-interrupt pump needs the
     * application lock; without it dll_send() and the ISDU snapshot copy would
     * not be atomic with respect to the ISR. */
    ctx->dll.lock = config->lock;
    ctx->dll.unlock = config->unlock;
    ctx->dll.lock_user = config->user;
    ctx->dll.isdu.direct_param_page2 = ctx->direct_param_page2;
    ctx->dll.isdu.params_ctx = &ctx->params;
    ctx->dll.isdu.vendor_service = config->vendor_service;
    ctx->dll.isdu.vendor_user = config->user;
    ctx->dll.state_cb = device_state_cb;
    ctx->dll.state_cb_user = ctx;
    /* One backend serves the standard parameters and the Data Storage image. */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_params_bind_storage(&ctx->params, config->storage);
    if (config->storage != NULL) {
        iolink_ds_init(&ctx->dll.ds, config->storage);
        ctx->dll.isdu.ds_ctx = &ctx->dll.ds;
    }
    iolink_ds_bind_params(&ctx->dll.ds, &ctx->params);
    /* Route DS parameter access through the device: standard tags via the
       params context, everything else (vendor 0x0040+/0x0100+) via the vendor
       service. The default descriptor set (three tags) is kept. */
    iolink_ds_bind_param_access(&ctx->dll.ds, device_ds_read, device_ds_write, ctx, NULL, 0U);
    /* Establish the DS image/checksum at boot when no persisted image exists,
       so Data_Storage_Size and Parameter_Checksum are valid before the first
       upload. */
    if (ctx->dll.ds.image_len == 0U) {
        (void) iolink_ds_build_image(&ctx->dll.ds);
    }
#else
    iolink_params_bind_storage(&ctx->params, NULL);
    ctx->dll.isdu.ds_ctx = NULL;
#endif
    device_apply_stack_config(ctx);
    ctx->master_lost_last = ctx->dll.master_lost;
    /* Seed the pre-staged cyclic variants so the first ISR reply is complete. */
    if (ctx->dll.cyclic_reply_precached) {
        iolink_dll_prepare_cyclic_reply(&ctx->dll);
    }

    if ((config->app_callbacks != NULL) && (config->app_callbacks->on_startup != NULL)) {
        config->app_callbacks->on_startup();
    }

    return 0;
}

/** Pick a PD-in buffer the ISR is not streaming and that is not active.
 *
 * @return Buffer index, or 0xFF when every other buffer is still leased by the
 *         data ISR. The caller must then keep the current PD-in and retry on a
 *         later cycle: writing a leased buffer would tear the reply on the wire.
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
 * @brief ST-style time partition: when the platform provides the Timer B tick
 * hook, the main service window only runs after a tick authorises it.
 */
static bool device_service_window_ready(iolink_device_ctx_t* ctx)
{
    if ((ctx->dll.phy != NULL) && (ctx->dll.phy->stack_timer_start != NULL) &&
        (iolink_dll_can_run_user_code(&ctx->dll) == 0U)) {
        return false;
    }
    return true;
}

/**
 * @brief Service the event and Data Storage state machines from the main loop.
 *
 * They mutate shared ISDU/event state, so only run them while the ISDU transport
 * is idle: this keeps the Event flag and the DS state from changing in the
 * middle of an ISDU response.
 */
static void device_service_events_and_ds(iolink_device_ctx_t* ctx)
{
    if (ctx->dll.isdu.state != ISDU_STATE_IDLE) {
        return;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->dll.ds.state != IOLINK_DS_STATE_IDLE) {
        iolink_ds_process(&ctx->dll.ds);
    }
#endif
#if IOLINK_FEATURE_EVENTS
    if ((ctx->dll.events.count > 0U) || (ctx->dll.events.pending_count > 0U)) {
        iolink_events_process(&ctx->dll.events);
    }
#endif
#if IOLINK_FEATURE_DATA_STORAGE && IOLINK_FEATURE_EVENTS
    /* DS_UPLOAD_REQ is a notification. Publish it only after the DS-command ISDU
     * response has been read (ISDU idle), so the Master does not see the Event
     * flag inside that response and interleave a diagnosis read with the
     * ongoing ISDU transfer. */
    if (ctx->dll.ds.upload_event_pending) {
        ctx->dll.ds.upload_event_pending = false;
        iolink_event_trigger(&ctx->dll.events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST,
                             IOLINK_EVENT_TYPE_NOTIFICATION);
    }
#endif
}

/** @brief Report master-lost / master-online edges to the application. */
static void device_notify_master_lost(iolink_device_ctx_t* ctx)
{
    if ((ctx->config == NULL) || (ctx->config->app_callbacks == NULL)) {
        return;
    }
    if (ctx->dll.state != IOLINK_DLL_STATE_OPERATE) {
        /* Startup/preoperate pauses are not loss events. Suppress the edge and
           forget any stale state when leaving OPERATE. */
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
            ctx->config->app_callbacks->on_master_lost();
        }
    }
    else if (ctx->config->app_callbacks->on_master_online != NULL) {
        ctx->config->app_callbacks->on_master_online();
    }
}

/** @brief Deliver deferred ISDU reset requests to the application. */
static void device_dispatch_resets(iolink_device_ctx_t* ctx)
{
    if (ctx->dll.isdu.reset_pending) {
        ctx->dll.isdu.reset_pending = false;
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_DEVICE);
        }
    }
    if (ctx->dll.isdu.app_reset_pending) {
        ctx->dll.isdu.app_reset_pending = false;
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_APPLICATION);
        }
    }
}

/** @brief Run the application process-data callbacks while in OPERATE. */
static void device_run_app_callbacks(iolink_device_ctx_t* ctx)
{
    if ((ctx->config == NULL) || (ctx->config->app_callbacks == NULL) ||
        (ctx->dll.state != IOLINK_DLL_STATE_OPERATE)) {
        return;
    }
    if (ctx->config->app_callbacks->on_pd_output != NULL) {
        ctx->config->app_callbacks->on_pd_output(ctx->dll.pd_out, ctx->dll.pd_out_len_current);
    }
#if IOLINK_APP_PD_INPUT_CALLBACK
    if (ctx->config->app_callbacks->on_pd_input != NULL) {
        /* Fill a buffer the ISR is not streaming, then publish it atomically so
         * the ISR never streams a torn PD-in. The callback return value is the
         * PD valid flag; the pre-cached cyclic reply must be rebuilt. */
        const uint8_t inactive = device_pd_in_free_index(&ctx->dll);
        if (inactive == 0xFFU) {
            return; /* every spare buffer is leased: keep the current PD-in */
        }
        /* Fill the free buffer outside the critical section: the ISR keeps
         * streaming the captured copy meanwhile. */
        const bool valid = ctx->config->app_callbacks->on_pd_input(
            &ctx->dll.pd_in[inactive][ctx->dll.od_width], ctx->dll.pd_in_len_current);

        if (ctx->config->lock != NULL) {
            ctx->config->lock(ctx->config->user);
        }
        else {
            IOLINK_CRITICAL_ENTER();
        }
        ctx->dll.pd_in_len = ctx->dll.pd_in_len_current;
        ctx->dll.pd_valid = valid;
        ctx->dll.pd_in_active = inactive;
        if (ctx->config->unlock != NULL) {
            ctx->config->unlock(ctx->config->user);
        }
        else {
            IOLINK_CRITICAL_EXIT();
        }

        if (ctx->dll.cyclic_reply_precached) {
            iolink_dll_prepare_cyclic_reply(&ctx->dll);
        }
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

    /* State callbacks triggered from the data ISR are deferred to this window,
     * in transition order (a burst of frames cannot overwrite an earlier one). */
    iolink_dll_flush_state_callbacks(&ctx->dll);

    device_notify_master_lost(ctx);
    device_dispatch_resets(ctx);
    device_run_app_callbacks(ctx);
}

int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid)
{
#if IOLINK_APP_PD_INPUT_CALLBACK
    /* The callback path is selected; mixing both paths would publish PD-in
     * twice per cycle and race the pre-cached cyclic reply. */
    (void) ctx;
    (void) data;
    (void) len;
    (void) valid;
    return -1;
#else
    if ((ctx == NULL) || (data == NULL) || (len > IOLINK_PD_IN_MAX_SIZE)) {
        return -1;
    }
    /* Annex A fixed types have an immutable PD-in length. TYPE_2_V may vary
     * within the configured maximum; both cases must not silently change the
     * reply length away from the advertised layout. */
    if (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        if (len > ctx->dll.pd_in_len_max) {
            return -1;
        }
    }
    else if ((ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_0) && (len != ctx->dll.pd_in_len_max)) {
        /* Fixed Annex A type: the PD-in width is immutable. TYPE_0 carries no
         * PD at all, so a generic update is harmless and stays permissive. */
        return -1;
    }

    /* PD-in lives after the OD header in the reply buffer; fill a buffer the
     * ISR is not streaming and publish it with an atomic index swap. No spare
     * buffer means the ISR still holds them all: keep the current PD-in. */
    const uint8_t inactive = device_pd_in_free_index(&ctx->dll);
    if (inactive == 0xFFU) {
        return -1;
    }
    if ((ctx->config != NULL) && (ctx->config->lock != NULL)) {
        ctx->config->lock(ctx->config->user);
    }
    else {
        IOLINK_CRITICAL_ENTER();
    }

    (void) memcpy(&ctx->dll.pd_in[inactive][ctx->dll.od_width], data, len);
    ctx->dll.pd_in_len = (uint8_t) len;
    ctx->dll.pd_in_len_current = (uint8_t) len;
    ctx->dll.pd_valid = valid;
    ctx->dll.pd_in_active = inactive;

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        IOLINK_CRITICAL_EXIT();
    }

    /* Rebuild the pre-staged cyclic reply variants (double buffered) so the ISR
     * can answer the next cyclic frame without the main loop in the path. */
    if (ctx->dll.cyclic_reply_precached) {
        iolink_dll_prepare_cyclic_reply(&ctx->dll);
    }

    return 0;
#endif /* IOLINK_APP_PD_INPUT_CALLBACK */
}

void iolink_device_process_isr(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    iolink_dll_process_isr(&ctx->dll);
}

int iolink_device_pd_output_read(iolink_device_ctx_t* ctx, uint8_t* data, size_t len)
{
    uint8_t read_len;

    if ((ctx == NULL) || (data == NULL)) {
        return -1;
    }

    if ((ctx->config != NULL) && (ctx->config->lock != NULL)) {
        ctx->config->lock(ctx->config->user);
    }
    else {
        IOLINK_CRITICAL_ENTER();
    }

    read_len = (len < ctx->dll.pd_out_len_current) ? (uint8_t) len : ctx->dll.pd_out_len_current;
    (void) memcpy(data, ctx->dll.pd_out, read_len);

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        IOLINK_CRITICAL_EXIT();
    }

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
    /* Fixed Annex A types have an immutable PD layout; only TYPE_2_V varies. */
    if (ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_2_V) {
        return -2;
    }
    if ((pd_in_len > ctx->dll.pd_in_len_max) || (pd_out_len > ctx->dll.pd_out_len_max)) {
        return -1;
    }
    return iolink_dll_set_pd_length(&ctx->dll, pd_in_len, pd_out_len);
}
