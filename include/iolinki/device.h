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
#include "iolinki/data_storage.h"
#include "iolinki/device_info.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/params.h"
#include "iolinki/phy.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file device.h
 * @brief IO-Link Device application layer API.
 *
 * Top-level entry point for an IO-Link Device. Ties together the physical
 * layer, data link layer, device identification, parameters and application
 * callbacks behind a single opaque context, and exposes the lifecycle,
 * process-data and introspection functions used by the application.
 */

/* -------------------------------------------------------------------------
 * Integration checklist (two-pump transport)
 *
 * Required calls:
 *  - iolink_device_init() once; the application owns the context object.
 *  - iolink_device_process() periodically from the main loop. This must be the
 *    only main-context caller (no second task may call the API).
 *  - iolink_device_process_isr() from the PHY data interrupt.
 *  - When the PHY provides the optional Timer A/Timer B hooks, forward those
 *    interrupts to iolink_dll_on_rx_timeout() / iolink_dll_on_stack_tick()
 *    (declared in dll.h). Without the Timer B forward the main service window
 *    is gated and iolink_device_process() returns immediately.
 *
 * Serviced automatically by iolink_device_process():
 *  - DLL frame dispatch, ISDU services, timing supervision and diagnostics;
 *  - iolink_events_process() and iolink_ds_process() while the ISDU transport
 *    is idle (device_service_events_and_ds()).
 *
 * Still application-driven:
 *  - iolink_ds_check(): it needs the Master's DS checksum, which the stack
 *    cannot obtain on its own.
 *
 * Normal applications only need this header plus the two ISR hooks above;
 * dll.h is the advanced/porting interface.
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Ownership and lifetime contract
 *
 * Object                    | Owner           | Lifetime / access rule
 * --------------------------+-----------------+--------------------------------
 * iolink_device_config_t    | application     | must outlive the context
 *  .phy (vtable)            | application/PHY | callbacks valid for ctx lifetime
 *  .phy.user                | PHY driver      | passed only to PHY callbacks
 *  .user                    | application     | passed to time_us, vendor_service
 *                           |                 | and the app_callbacks on_* hooks
 *  .app_callbacks           | application     | struct and callback targets valid
 *  .device_info             | application     | static identification data
 *  .storage                 | application     | read/write valid for ctx lifetime
 *  .time_us                 | application     | monotonic, non-blocking, ISR-safe
 * iolink_device_ctx_t       | application     | one instance per link; one main
 *                           |                 | context caller plus the data ISR
 * PD data (pd_input_update) | application     | borrowed for the call; copied
 *                           |                 | into the reply buffer internally
 * PD data (on_pd_input)     | application     | filled buffer borrowed; return value
 *                           |                 | is the PD-valid flag (callback profile)
 * PD buffer (pd_output_read)| caller          | filled by the call; caller owns it
 * vendor_service input      | caller          | borrowed for the call
 * vendor_service output     | caller          | capacity in *output_length; the
 *                           |                 | callback sets the used length
 * rx_frame() return value   | PHY             | borrowed until the next rx_frame()
 * tx_frame() data           | library         | valid until the transfer completes
 * ------------------------------------------------------------------------- */

/**
 * @defgroup iolinki_device Device Application Layer
 * @brief Top-level IO-Link Device context, lifecycle and process-data API.
 * @{
 */

/** @brief Callback returning a monotonic timestamp in microseconds.
 *  @param user Opaque user pointer supplied via ::iolink_device_config_t.
 *  @return Current time in microseconds.
 *  @note Called from both the main loop and the data ISR, so it must be
 *        non-blocking and ISR-safe. */
typedef uint64_t (*iolink_device_time_us_fn)(void* user);

/**
 * @brief Device configuration supplied at initialization.
 *
 * Aggregates the PHY driver, stack settings, application hooks, device
 * identification, data-storage backend and platform integration callbacks.
 */
typedef struct
{
    iolink_phy_api_t phy;                        /**< Physical layer driver API. `phy.user` is the
                                                      driver context passed to the PHY vtable only;
                                                      it is independent of @ref user below. */
    iolink_stack_config_t stack;                 /**< Stack behavior configuration. */
    const iolink_app_callbacks_t* app_callbacks; /**< Application lifecycle/PD callbacks (may be
                                                    NULL unless IOLINK_APP_PD_INPUT_CALLBACK=1,
                                                    which requires on_pd_input). */
    const iolink_device_info_t* device_info;     /**< Static device identification data. */
#if IOLINK_FEATURE_DATA_STORAGE
    const iolink_storage_api_t*
        storage; /**< Unified NVM backend for params, DS image and flags (required). */
#endif
    iolink_device_time_us_fn time_us; /**< Application clock used by the DLL, ISR pump and
                                           event engine; required (NULL is rejected at
                                           initialization). */
    void* user;                       /**< Application context passed to iolink_device_time_us_fn,
                                           vendor_service and every iolink_app_callbacks_t hook. */
    uint8_t (*vendor_service)(
        void* user, uint16_t index, uint8_t subindex, bool write, const uint8_t* input,
        size_t length, uint8_t* output,
        size_t* output_length); /**< Optional vendor ISDU callback; see isdu.h. */
} iolink_device_config_t;

/**
 * @brief Device runtime context.
 *
 * Allocate this object directly, but access it only through the API functions.
 * All fields are private stack state.
 */
typedef struct
{
    /* Private fields. Allocate this object directly, but use API functions. */
    iolink_dll_ctx_t dll;                 /**< Data link layer context. */
    iolink_stack_config_t stack_config;   /**< Active stack configuration. */
    iolink_device_info_ctx_t device_info; /**< Device identification context. */
    iolink_params_ctx_t params;           /**< Parameter manager context. */
    const iolink_device_config_t* config; /**< Pointer to the supplied configuration. */
    iolink_reset_handler_t reset_handler; /**< Application reset handler (may be NULL). */
    bool master_lost_last;                /**< Previous master-lost state (callback edge) */
    uint8_t direct_param_page2[16];       /**< Direct Parameter Page 2 storage. */
} iolink_device_ctx_t;

/**
 * @brief Result codes returned by iolink_device_init().
 *
 * Negative values identify a rejected configuration. A negative value returned
 * by the PHY init callback is propagated unchanged and may fall outside this
 * enumeration.
 */
typedef enum
{
    IOLINK_DEVICE_INIT_OK = 0,               /**< Initialization succeeded */
    IOLINK_DEVICE_INIT_ERR_NULL = -1,        /**< ctx or config is NULL */
    IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE = -2,   /**< m_seq_type is not a valid type */
    IOLINK_DEVICE_INIT_ERR_PD_IN_LEN = -3,   /**< pd_in_len exceeds the buffer */
    IOLINK_DEVICE_INIT_ERR_PD_OUT_LEN = -4,  /**< pd_out_len exceeds the buffer */
    IOLINK_DEVICE_INIT_ERR_OD_WIDTH = -5,    /**< od_width is not 1/2/8/32 */
    IOLINK_DEVICE_INIT_ERR_OD_CAPACITY = -6, /**< od_width exceeds IOLINK_OD_MAX_SIZE */
    IOLINK_DEVICE_INIT_ERR_BAUDRATE = -7,    /**< baudrate_set with an invalid COM */
    IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT =
        -9,                            /**< PD/OD lengths incompatible with the M-sequence type */
    IOLINK_DEVICE_INIT_ERR_TIME = -10, /**< time_us callback is NULL */
    IOLINK_DEVICE_INIT_ERR_CRITICAL = -11, /**< platform critical sections are the weak default */
    IOLINK_DEVICE_INIT_ERR_STORAGE = -12,  /**< storage backend is NULL or incomplete */
    IOLINK_DEVICE_INIT_ERR_PD_INPUT =
        -14, /**< IOLINK_APP_PD_INPUT_CALLBACK=1 without an on_pd_input callback */
    IOLINK_DEVICE_INIT_ERR_PHY = -15, /**< a PHY hook required by the build profile is NULL */
    /* -8 (lock pair) and -13 (RTOS lock) are retired: the stack has a single
     * main-context caller and no longer takes an application thread lock. */
} iolink_device_init_result_t;

/**
 * @brief Initialize a device context from a configuration.
 * @param ctx     Device context to initialize.
 * @param config  Configuration to apply (must remain valid for the ctx lifetime).
 * @return 0 on success, a negative ::iolink_device_init_result_t for an invalid
 *         configuration, or the negative error returned by the PHY init callback.
 */
int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);

/**
 * @brief Run one iteration of the device state machine.
 *
 * Drives the DLL/PHY: services pending frames, timing and application events.
 * Call this periodically from the main loop.
 * @param ctx Device context.
 */
void iolink_device_process(iolink_device_ctx_t* ctx);

/**
 * @brief ISR-side pump for the two-pump transport.
 *
 * Called from the PHY data interrupt: frames the incoming M-sequence and feeds
 * the reply octets, publishing completed requests to a mailbox consumed by
 * iolink_device_process(). Runs no protocol logic.
 */
void iolink_device_process_isr(iolink_device_ctx_t* ctx);

#if !IOLINK_APP_PD_INPUT_CALLBACK
/**
 * @brief Update the input process data (Device -> Master).
 * @param ctx    Device context.
 * @param data   Input process data bytes to publish.
 * @param len    Length of @p data in bytes.
 * @param valid  Whether the process data is currently valid.
 * @return 0 on success, negative on error.
 */
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid);
#else
/**
 * @brief Trap for a polling call in the on_pd_input callback build.
 *
 * IOLINK_APP_PD_INPUT_CALLBACK=1 selected the on_pd_input() callback as the only
 * PD-input source; the polling API does not exist in that build. A call expands
 * to an undeclared identifier, which is a compile error with a readable name.
 */
#define iolink_device_pd_input_update(...) \
    IOLINK_PD_INPUT_UPDATE_IS_DISABLED_USE_ON_PD_INPUT_CALLBACK
#endif

/**
 * @brief Read the latest output process data (Master -> Device).
 * @param ctx        Device context.
 * @param[out] data  Buffer filled with the output process data.
 * @param len        Size of @p data in bytes.
 * @return Number of bytes copied (0 when none), negative on error.
 */
int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx, uint8_t* data, size_t len);

/**
 * @brief Register the handler invoked on Master reset commands.
 * @param ctx      Device context.
 * @param handler  Reset handler callback (may be NULL to clear).
 */
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler);

#if IOLINK_FEATURE_EVENTS
/**
 * @brief Get the event handling context.
 * @param ctx Device context.
 * @return Pointer to the device's events context.
 */
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);
#endif

#if IOLINK_FEATURE_DATA_STORAGE
/**
 * @brief Get the data-storage context.
 * @param ctx Device context.
 * @return Pointer to the device's data-storage context.
 */
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);
#endif

/**
 * @brief Get the current DLL state.
 * @param ctx Device context.
 * @return Current data link layer state.
 */
iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx);

/**
 * @brief Whether the master has been silent for the configured number of cycles.
 * @param ctx Device context.
 * @return true while the master is considered lost (application hint; the DLL
 *         state and baudrate are not changed).
 */
bool iolink_device_master_lost(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the current PHY communication mode.
 * @param ctx Device context.
 * @return Current physical layer mode.
 */
iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the current communication baud rate.
 * @param ctx Device context.
 * @return Active baud rate.
 */
iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx);

/**
 * @brief Copy the DLL statistics counters.
 * @param ctx             Device context.
 * @param[out] out_stats  Filled with the current DLL statistics.
 */
void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats);

/**
 * @brief Enable or disable timing enforcement.
 * @param ctx     Device context.
 * @param enable  True to enforce timing limits, false to only measure.
 */
void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable);

/**
 * @brief Set the response-time (t_ren) limit.
 * @param ctx       Device context.
 * @param limit_us  Response-time limit in microseconds.
 */
void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us);

/**
 * @brief Get the negotiated M-sequence type.
 * @param ctx Device context.
 * @return Active M-sequence type.
 */
iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the input process data length.
 * @param ctx Device context.
 * @return Input process data length in bytes.
 */
uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the output process data length.
 * @param ctx Device context.
 * @return Output process data length in bytes.
 */
uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx);

/**
 * @brief Set the input and output process data lengths.
 * @param ctx         Device context.
 * @param pd_in_len   Input process data length in bytes.
 * @param pd_out_len  Output process data length in bytes.
 * @return 0 on success, negative on error.
 */
int iolink_device_set_pd_length(iolink_device_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/** @} */ /* end of iolinki_device */

#endif  // IOLINK_DEVICE_H
