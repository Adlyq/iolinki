/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_H
#define IOLINK_PHY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "iolinki/config.h"

/**
 * @file phy.h
 * @brief IO-Link Physical Layer (PHY) Abstraction Interface
 */

/**
 * @defgroup iolinki_phy Physical Layer (PHY) Interface
 * @brief Hardware-agnostic transceiver contract, operating modes and baudrates.
 * @{
 */

/**
 * @brief IO-Link Operating Modes
 */
typedef enum
{
    IOLINK_PHY_MODE_INACTIVE = 0U, /**< PHY interface disabled */
    IOLINK_PHY_MODE_SIO = 1U,      /**< Standard I/O mode (Digital Input/Output) */
    IOLINK_PHY_MODE_SDCI = 2U      /**< Digital Communication mode (IO-Link exchange) */
} iolink_phy_mode_t;

/**
 * @brief IO-Link Communication Baudrates (COMx)
 */
typedef enum
{
    IOLINK_BAUDRATE_COM1 = 0U, /**< 4.8 kbit/s */
    IOLINK_BAUDRATE_COM2 = 1U, /**< 38.4 kbit/s */
    IOLINK_BAUDRATE_COM3 = 2U  /**< 230.4 kbit/s */
} iolink_baudrate_t;

/**
 * @brief Physical fault flags returned by iolink_phy_api_t.get_faults().
 *
 * A PHY reports the physical condition; the device decides how it is reported
 * on the wire (see iolink_dll_fault_map_t in dll.h).
 */
#define IOLINK_PHY_FAULT_UNDERVOLTAGE  (1U << 0) /**< L+ supply below tolerance */
#define IOLINK_PHY_FAULT_OVERTEMP      (1U << 1) /**< Transceiver over-temperature */
#define IOLINK_PHY_FAULT_SHORT_CIRCUIT (1U << 2) /**< C/Q (or L+) short circuit */
/** All defined physical fault flags. */
#define IOLINK_PHY_FAULT_ALL                                                             \
    (IOLINK_PHY_FAULT_UNDERVOLTAGE | IOLINK_PHY_FAULT_OVERTEMP |                         \
     IOLINK_PHY_FAULT_SHORT_CIRCUIT)

/**
 * @brief Normalized status bits returned by the raw-octet hooks
 *        (iolink_phy_api_t.rx_octet()).
 *
 * A raw-octet PHY translates its own transceiver status into these bits so the
 * library framing engine stays device-agnostic. The library only needs the
 * frame-end/error indication: on a single-octet transceiver that is the
 * "data + check" condition raised at the end of a master frame (octet-count
 * time-out) or on a receive error.
 */
#define IOLINK_PHY_STATUS_CHK        (1U << 0) /**< Frame-end time-out / receive error */
#define IOLINK_PHY_STATUS_DATA_READY (1U << 1) /**< A received octet is available */

/** Valid L+ window for the legacy iolink_phy_api_t.get_voltage_mv() hook, in mV. */
#define IOLINK_PHY_VOLTAGE_MIN_MV (18000)
#define IOLINK_PHY_VOLTAGE_MAX_MV (30000)

/**
 * @brief Physical Layer (PHY) API Structure
 *
 * This interface defines the contract between the stack and the hardware-specific
 * transceiver driver (e.g. for MAX14827, L6362, or a Virtual TTY).
 */
typedef struct
{
    void* user; /**< Opaque driver context passed back to every callback. */

    /**
     * @brief Initialize transceiver hardware
     * @return 0 on success, negative error code on hardware failure
     */
    int (*init)(void* user);

    /**
     * @brief Set PHY operating mode (SDCI vs SIO)
     * @param mode Target mode
     */
    void (*set_mode)(void* user, iolink_phy_mode_t mode);

    /**
     * @brief Set communication baudrate
     * @param baudrate Target COMx speed
     */
    void (*set_baudrate)(void* user, iolink_baudrate_t baudrate);

#if IOLINK_PHY_FEATURE_STREAMING
    /**
     * @brief Send a buffer of data over the line
     * @param data Pointer to source buffer
     * @param len Number of bytes to transmit
     * @return Number of bytes actually sent, or negative on error
     */
    int (*send)(void* user, const uint8_t* data, size_t len);
#endif

    /**
     * @brief Read one received octet and its transceiver status (raw-octet PHY).
     *
     * For a single-octet transceiver (e.g. a single-octet SPI transceiver)
     * the library owns M-sequence framing: it pulls received octets here and
     * feeds reply octets through tx_octet()/tx_end(). @p status is a PHY status
     * octet (see IOLINK_PHY_STATUS_*). Optional: NULL for a streaming PHY that
     * uses recv_byte()/send().
     * @return 1 if an octet was read into @p data, 0 if none was pending,
     *         negative on transport error.
     */
    int (*rx_octet)(void* user, uint8_t* data, uint8_t* status);

    /**
     * @brief Write one reply octet (raw-octet PHY).
     *
     * The library feeds the reply octet by octet as the transceiver requests it
     * (writing the first octet starts the frame); it accumulates the message
     * checksum while sending and emits the final CKS octet before tx_end(). The
     * SPI arbitration with the data interrupt is the PHY's responsibility.
     * @return Normalized PHY status octet for the access (IOLINK_PHY_STATUS_*).
     */
    int (*tx_octet)(void* user, uint8_t data);

    /** @brief Terminate the current device transmission (write the frame end). */
    void (*tx_end)(void* user);

    /**
     * @brief Current level of the transceiver data-interrupt line.
     * @return 1 while asserted, 0 otherwise. Used to gate the ISR pump and to
     *         drain a back-to-back burst.
     */
    uint8_t (*int_active)(void* user);

#if IOLINK_PHY_FEATURE_STREAMING
    /**
     * @brief Non-blocking receive for a single byte
     * @param byte Pointer to store received byte
     * @return 1 if byte available and read, 0 if nothing received, negative on error
     */
    int (*recv_byte)(void* user, uint8_t* byte);
#endif

    /* Optional Diagnostic/Support Functions (can be NULL) */

#if IOLINK_PHY_FEATURE_WAKEUP
    /**
     * @brief Detect wake-up pulse (80µs pulse on C/Q line)
     *
     * Capability flag: a PHY that implements this owns nothing of the SIO
     * handshake and lets the DLL drive SIO/SDCI transitions. When it is NULL the
     * PHY owns the SIO/wake-up handshake itself, so the DLL must stay in SDCI
     * (switching to SIO would latch with no way back: a NULL PHY never reports a
     * wake-up).
     * @return 1 if wake-up pulse detected during current window, 0 otherwise
     */
    int (*detect_wakeup)(void* user);
#endif

#if IOLINK_PHY_FEATURE_SIO_CQ
    /**
     * @brief Manually set C/Q line state (for SIO push-pull)
     * @param state 0 for Low, 1 for High
     */
    void (*set_cq_line)(void* user, uint8_t state);
#endif

    /**
     * @brief Optional: physical faults currently active, as a bitmask of
     *        IOLINK_PHY_FAULT_* flags (0 when none).
     *
     * This is the preferred diagnostics channel: a PHY describes *what* is
     * physically wrong and needs no knowledge of IO-Link EventCodes. The DLL
     * watches the 0->1 edges and reports each fault through the device's
     * fault -> EventCode mapping (iolink_dll_fault_map_t), so the codes stay
     * device-specific data instead of being hard-coded in the stack.
     * @return Active fault flags.
     */
    uint8_t (*get_faults)(void* user);

#if IOLINK_PHY_FEATURE_LEGACY_DIAG
    /**
     * @brief Legacy alternative to get_faults(): measured L+ supply voltage.
     *
     * Only consulted when get_faults() is NULL. A *negative* return means
     * "measurement unavailable" and is not a fault; a non-negative reading
     * outside [IOLINK_PHY_VOLTAGE_MIN_MV, IOLINK_PHY_VOLTAGE_MAX_MV] raises
     * IOLINK_PHY_FAULT_UNDERVOLTAGE.
     * @return Voltage in millivolts, negative if measurement unavailable
     */
    int (*get_voltage_mv)(void* user);

    /**
     * @brief Legacy alternative to get_faults(): hardware fault condition.
     *
     * Only consulted when get_faults() is NULL. The documented contract is
     * "short circuit or overtemperature detected", i.e. the cause is not
     * distinguishable, so it raises IOLINK_PHY_FAULT_SHORT_CIRCUIT.
     * @return true if a fault is detected
     */
    bool (*is_short_circuit)(void* user);
#endif /* IOLINK_PHY_FEATURE_LEGACY_DIAG */
} iolink_phy_api_t;

/** @} */ /* end of iolinki_phy */

#endif  // IOLINK_PHY_H
