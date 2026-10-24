/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file phy_generic.c
 * @brief Template/skeleton generic UART PHY adapter.
 * @ingroup iolinki_phy_generic
 *
 * Provides a non-functional reference PHY implementation whose hooks are meant
 * to be filled in with target-specific UART/GPIO/transceiver code.
 */

#include "iolinki/phy_generic.h"

/** @brief Template PHY init hook (returns failure until implemented). */
static int generic_init(void* user)
{
    (void) user;
    /* Template: initialize UART/GPIO/transceiver here. */
    return -1;
}

/** @brief Template hook to configure the transceiver for SIO/SDCI mode. */
static void generic_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
    /* Template: configure transceiver for SIO/SDCI. */
}

/** @brief Template hook to configure UART speed for COM1/2/3. */
static void generic_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
    /* Template: configure UART speed for COM1/2/3. */
}

#if IOLINK_PHY_FEATURE_WAKEUP
/** @brief Template hook to detect the 80us wake-up pulse on the C/Q line. */
static int generic_detect_wakeup(void* user)
{
    (void) user;
    /* Template: detect 80us wake-up pulse on C/Q line. */
    return 0;
}
#endif

#if IOLINK_PHY_FEATURE_SIO_CQ
/** @brief Template hook to drive the C/Q line high/low in SIO mode. */
static void generic_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    (void) state;
    /* Template: drive C/Q line high/low in SIO mode. */
}
#endif

#if IOLINK_PHY_FEATURE_LEGACY_DIAG
/** @brief Template hook to read supply voltage in mV (negative if unavailable). */
static int generic_get_voltage_mv(void* user)
{
    (void) user;
    /* Template: return supply voltage in mV, or negative if unavailable. */
    return -1;
}

/** @brief Template hook returning true when a short-circuit fault is detected. */
static bool generic_is_short_circuit(void* user)
{
    (void) user;
    /* Template: return true if fault detected. */
    return false;
}
#endif

static const iolink_phy_api_t g_phy_generic = {.init = generic_init,
                                               .set_mode = generic_set_mode,
                                               .set_baudrate = generic_set_baudrate,
#if IOLINK_PHY_FEATURE_WAKEUP
                                               .detect_wakeup = generic_detect_wakeup,
#endif
#if IOLINK_PHY_FEATURE_SIO_CQ
                                               .set_cq_line = generic_set_cq_line,
#endif
#if IOLINK_PHY_FEATURE_LEGACY_DIAG
                                               .get_voltage_mv = generic_get_voltage_mv,
                                               .is_short_circuit = generic_is_short_circuit
#endif
};

const iolink_phy_api_t* iolink_phy_generic_get(void)
{
    return &g_phy_generic;
}
