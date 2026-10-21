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
#include "iolinki/time_utils.h"
#include <string.h>

/** @brief DLL state-change callback: dispatches to the app startup/preoperate/operate hooks. */
static void device_state_cb(void* user, iolink_dll_state_t state)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if ((ctx == NULL) || (ctx->config == NULL) || (ctx->config->app_callbacks == NULL)) {
        return;
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

/** @brief Copy the user stack configuration into the DLL context and derive lengths/timing. */
static void device_apply_stack_config(iolink_device_ctx_t* ctx)
{
    ctx->dll.m_seq_type = (uint8_t) ctx->stack_config.m_seq_type;
    ctx->dll.pd_in_len = ctx->stack_config.pd_in_len;
    ctx->dll.pd_out_len = ctx->stack_config.pd_out_len;
    ctx->dll.min_cycle_time_app_us = (uint32_t) ctx->stack_config.min_cycle_time * 100U;
    ctx->dll.t_pd_delay_us = ctx->stack_config.t_pd_us;
    if (ctx->dll.t_pd_delay_us > 0U) {
        ctx->dll.t_pd_deadline_us = iolink_time_get_us() + (uint64_t) ctx->dll.t_pd_delay_us;
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

#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* Two-pump transport: the ISR frames requests and transmits the pre-staged
     * cyclic reply, so the DLL needs the OD width and whether the bound PHY can
     * send a staged reply from ISR context. The OD width defaults to upstream's
     * 1/2 octets; the opt-in 8/32-byte extension is enabled via config.od_width
     * (drop that extension when upstream gains native support). */
    ctx->dll.od_width = ctx->stack_config.od_width;
    if (ctx->dll.od_width == 0U) {
        ctx->dll.od_width = ctx->dll.od_len;
    }
    ctx->dll.cyclic_reply_precached =
        (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) && dll_phy_has_tx(&ctx->dll);
#endif

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

/** @brief Validate the stack configuration before touching the context or PHY. */
static int device_config_validate(const iolink_device_config_t* config)
{
    const iolink_config_t* stack;
    uint8_t od;

    if (config == NULL) {
        return IOLINK_DEVICE_INIT_ERR_NULL;
    }
    stack = &config->stack;

    if ((uint32_t) stack->m_seq_type > (uint32_t) IOLINK_M_SEQ_TYPE_2_V) {
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
        od = (stack->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;
    }
    if ((uint32_t) od > (uint32_t) IOLINK_OD_MAX_SIZE) {
        return IOLINK_DEVICE_INIT_ERR_OD_CAPACITY;
    }
    if ((od != 1U) && (od != 2U) && (od != 8U) && (od != 32U)) {
        return IOLINK_DEVICE_INIT_ERR_OD_WIDTH;
    }

    if (stack->baudrate_set && ((uint32_t) stack->baudrate > (uint32_t) IOLINK_BAUDRATE_COM3)) {
        return IOLINK_DEVICE_INIT_ERR_BAUDRATE;
    }

    if ((config->lock == NULL) != (config->unlock == NULL)) {
        return IOLINK_DEVICE_INIT_ERR_LOCK_PAIR;
    }

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
        int err = config->phy.init(config->phy.user);
        if (err != 0) {
            return err;
        }
    }

    iolink_device_info_ctx_init(&ctx->device_info, config->device_info);
    iolink_params_ctx_init(&ctx->params, &ctx->device_info);
    iolink_dll_init(&ctx->dll, &config->phy);
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
#if IOLINK_FEATURE_DATA_STORAGE
    if (config->ds_storage != NULL) {
        iolink_ds_init(&ctx->dll.ds, config->ds_storage);
        ctx->dll.isdu.ds_ctx = &ctx->dll.ds;
    }
    iolink_ds_bind_params(&ctx->dll.ds, &ctx->params);
#else
    ctx->dll.isdu.ds_ctx = NULL;
#endif
    device_apply_stack_config(ctx);
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* Seed the pre-staged cyclic variants so the first ISR reply is complete. */
    if (ctx->dll.cyclic_reply_precached) {
        iolink_dll_prepare_cyclic_reply(&ctx->dll);
    }
#endif

    if ((config->app_callbacks != NULL) && (config->app_callbacks->on_startup != NULL)) {
        config->app_callbacks->on_startup();
    }

    return 0;
}

void iolink_device_process(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* ST-style time partition: when the platform provides the Timer B tick
     * hook, the main service window only runs after a tick authorises it. */
    if ((ctx->dll.phy != NULL) && (ctx->dll.phy->stack_timer_start != NULL) &&
        (iolink_dll_can_run_user_code(&ctx->dll) == 0U)) {
        return;
    }
#endif

    iolink_dll_process(&ctx->dll);

#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* State callbacks triggered from the data ISR are deferred to this window. */
    if (ctx->dll.state_cb_pending != 0U) {
        ctx->dll.state_cb_pending = 0U;
        if (ctx->dll.state_cb != NULL) {
            ctx->dll.state_cb(ctx->dll.state_cb_user, ctx->dll.pending_state);
        }
    }
#endif

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

    if ((ctx->config != NULL) && (ctx->config->app_callbacks != NULL) &&
        (ctx->dll.state == IOLINK_DLL_STATE_OPERATE)) {
        if (ctx->config->app_callbacks->on_pd_output != NULL) {
            ctx->config->app_callbacks->on_pd_output(ctx->dll.pd_out, ctx->dll.pd_out_len_current);
        }
        if (ctx->config->app_callbacks->on_pd_input != NULL) {
            /* Fill the inactive reply buffer, then publish it atomically so the
             * ISR never streams a torn PD-in. */
            const uint8_t inactive = (uint8_t) (ctx->dll.pd_in_active ^ 1U);
            ctx->config->app_callbacks->on_pd_input(&ctx->dll.pd_in[inactive][ctx->dll.od_width],
                                                    ctx->dll.pd_in_len_current);
            ctx->dll.pd_in_active = inactive;
        }
    }
}

int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid)
{
    if ((ctx == NULL) || (data == NULL) || (len > IOLINK_PD_IN_MAX_SIZE)) {
        return -1;
    }

    if ((ctx->config != NULL) && (ctx->config->lock != NULL)) {
        ctx->config->lock(ctx->config->user);
    }
    else {
        iolink_critical_enter();
    }

    /* PD-in lives after the OD header in the reply buffer; fill the inactive
     * copy and publish it with an atomic index swap. */
    const uint8_t inactive = (uint8_t) (ctx->dll.pd_in_active ^ 1U);
    (void) memcpy(&ctx->dll.pd_in[inactive][ctx->dll.od_width], data, len);
    ctx->dll.pd_in_len = (uint8_t) len;
    ctx->dll.pd_in_len_current = (uint8_t) len;
    ctx->dll.pd_valid = valid;
    ctx->dll.pd_in_active = inactive;

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        iolink_critical_exit();
    }

#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* Rebuild the pre-staged cyclic reply variants (double buffered) so the ISR
     * can answer the next cyclic frame without the main loop in the path. */
    if (ctx->dll.cyclic_reply_precached) {
        iolink_dll_prepare_cyclic_reply(&ctx->dll);
    }
#endif

    return 0;
}

#if IOLINK_PHY_FEATURE_ISR_PUMP
void iolink_device_process_isr(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    iolink_dll_process_isr(&ctx->dll);
}
#endif

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
        iolink_critical_enter();
    }

    read_len = (len < ctx->dll.pd_out_len_current) ? (uint8_t) len : ctx->dll.pd_out_len_current;
    (void) memcpy(data, ctx->dll.pd_out, read_len);

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        iolink_critical_exit();
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
    if ((ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_1_V) &&
        (ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_2_V)) {
        return -2;
    }
    if ((pd_in_len > ctx->dll.pd_in_len_max) || (pd_out_len > ctx->dll.pd_out_len_max)) {
        return -1;
    }
    return iolink_dll_set_pd_length(&ctx->dll, pd_in_len, pd_out_len);
}
