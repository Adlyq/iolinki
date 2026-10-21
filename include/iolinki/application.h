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
 * @brief Select the PD-input path.
 *
 * 0: the application calls iolink_device_pd_input_update() from its main loop.
 * 1: the DLL calls on_pd_input() each OPERATE cycle to fill the inactive PD-in
 *    buffer; iolink_device_pd_input_update() is disabled to prevent mixing.
 */
#ifndef IOLINK_APP_PD_INPUT_CALLBACK
#define IOLINK_APP_PD_INPUT_CALLBACK 0
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
 * Any field may be NULL. Lifecycle callbacks fire once when the DLL enters the
 * corresponding state. The PD callbacks fire on
 * every processed OPERATE cycle. @ref on_pd_output reports the output Process
 * Data received from the Master (Master -> Device). When
 * IOLINK_APP_PD_INPUT_CALLBACK is 1, @ref on_pd_input fills the inactive PD-in
 * buffer (Device -> Master) and its return value becomes the PD valid flag.
 */
typedef struct
{
    void (*on_startup)(void);                              /**< Entered STARTUP */
    void (*on_preoperate)(void);                           /**< Entered PREOPERATE */
    void (*on_operate)(void);                              /**< Entered OPERATE */
    bool (*on_pd_input)(uint8_t* data, uint8_t len);       /**< Fill input PD; false = invalid */
    void (*on_pd_output)(uint8_t* data, uint8_t len);      /**< Output PD received */
    void (*on_state_change)(iolink_dll_state_t state);     /**< Any DLL state change */
    void (*on_master_lost)(void);                          /**< Master communication lost */
    void (*on_master_online)(void);                        /**< Master communication resumed */
} iolink_app_callbacks_t;

/** @} */ /* end of iolinki_application */

#endif  // IOLINK_APPLICATION_H
