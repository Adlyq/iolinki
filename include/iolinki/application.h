/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_APPLICATION_H
#define IOLINK_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/dll.h"

/**
 * @brief Select the PD-input path at compile time.
 *
 * 0: the application calls iolink_device_pd_input_update() from its main loop;
 *    the on_pd_input callback member does not exist (assigning it is a compile
 *    error).
 * 1: the DLL calls on_pd_input() each OPERATE cycle to fill a free PD-in
 *    buffer; iolink_device_pd_input_update() is not declared (calling it is a
 *    compile/link error, see the trap macro in device.h).
 *
 * Mixing the two paths is impossible: the unused symbol/member is removed from
 * the headers instead of failing at run time.
 */
#ifndef IOLINK_APP_PD_INPUT_CALLBACK
#define IOLINK_APP_PD_INPUT_CALLBACK 0
#endif

#if IOLINK_APP_PD_INPUT_CALLBACK
/**
 * @brief Link-time profile marker for the callback profile.
 *
 * Defined only by a library built with IOLINK_APP_PD_INPUT_CALLBACK=1. An
 * application must use the same value (the iolink_app_callbacks_t layout
 * depends on it). Referencing this symbol from an application translation unit
 * (e.g. a file-scope `const void* const probe = &...;`) turns a mismatched
 * profile into a link error instead of undefined behaviour. Applications that
 * consume the library through add_subdirectory() inherit the setting
 * automatically via the PUBLIC compile definition.
 */
extern const uint8_t iolink_app_pd_input_callback_profile;
#endif

/**
 * @file application.h
 * @brief IO-Link Application Layer API for Process Data
 */

/**
 * @defgroup iolinki_application Application Layer Callbacks
 * @brief Application lifecycle and process-data callback hooks.
 * @{
 */

/**
 * @brief Application lifecycle and Process Data callbacks.
 *
 * Any field may be NULL, except that on_pd_input is required (and the only
 * PD-input source) when IOLINK_APP_PD_INPUT_CALLBACK is 1; initialization
 * rejects a missing callback with IOLINK_DEVICE_INIT_ERR_PD_INPUT. Every
 * callback receives the opaque @c user pointer supplied as
 * iolink_device_config_t::user, so the application does not need a global to
 * reach its own state. Lifecycle callbacks fire once when the DLL
 * enters the corresponding state. The PD callbacks fire on every processed
 * OPERATE cycle. @ref on_pd_output reports the output Process Data received
 * from the Master (Master -> Device). When IOLINK_APP_PD_INPUT_CALLBACK is 1,
 * on_pd_input() fills a free PD-in buffer (Device -> Master) and its return
 * value becomes the PD valid flag.
 */
typedef struct
{
    void (*on_startup)(void* user);    /**< Entered STARTUP */
    void (*on_preoperate)(void* user); /**< Entered PREOPERATE */
    void (*on_operate)(void* user);    /**< Entered OPERATE */
#if IOLINK_APP_PD_INPUT_CALLBACK
    bool (*on_pd_input)(void* user, uint8_t* data, uint8_t len); /**< Fill input PD;
                                                                      false = invalid */
#endif
    void (*on_pd_output)(void* user, uint8_t* data, uint8_t len); /**< Output PD received */
    void (*on_state_change)(void* user,
                            iolink_dll_state_t state); /**< Current DLL state on a
                                                            level-based notification.
                                                            Runs in main context, possibly
                                                            inside iolink_dll_process();
                                                            do not re-enter iolink_* here. */
    void (*on_master_lost)(void* user);                /**< Master communication lost */
    void (*on_master_online)(void* user);              /**< Master communication resumed */
} iolink_app_callbacks_t;

/** @} */ /* end of iolinki_application */

#endif  // IOLINK_APPLICATION_H
