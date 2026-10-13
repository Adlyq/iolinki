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

    /**
     * @brief Send a buffer of data over the line
     * @param data Pointer to source buffer
     * @param len Number of bytes to transmit
     * @return Number of bytes actually sent, or negative on error
     */
    int (*send)(void* user, const uint8_t* data, size_t len);

    /**
     * @brief Pre-stage the deterministic cyclic reply image.
     *
     * When the cyclic reply is deterministic (cyclic_reply_precached), the DLL
     * builds the whole M-sequence reply image (On-request Data + PD-in + CKS) and
     * hands it over here; the PHY transmits it verbatim at the next master-frame
     * end without interpreting its contents. The buffer stays valid until the next
     * stage_reply() call (the DLL double buffers it), so the PHY may retain the
     * pointer. Optional: when NULL the DLL sends the cyclic reply itself via
     * send().
     */
    void (*stage_reply)(void* user, const uint8_t* img, size_t len);

    /**
     * @brief Non-blocking receive for a single byte
     * @param byte Pointer to store received byte
     * @return 1 if byte available and read, 0 if nothing received, negative on error
     */
    int (*recv_byte)(void* user, uint8_t* byte);

    /**
     * @brief Non-blocking receive of a complete, frame-delimited M-sequence.
     *
     * For single-octet transceivers the PHY interrupts delimit the frame (octet
     * count + CKT type / frame-end time-out), so the stack receives a whole
     * M-sequence at once instead of reconstructing it from bytes with a t_byte
     * timer. @p len is in/out: input is the buffer capacity, output is the
     * received frame length. Optional: may be NULL (fall back to recv_byte).
     * @return 1 if a complete frame was copied, 0 if no frame available
     */
    int (*recv_frame)(void* user, uint8_t* buf, uint8_t* len);

    /**
     * @brief Declare the expected cyclic (Type-1/Type-2) frame length in octets.
     *
     * The DLL owns the M-sequence length (it derives it from the configured PD and
     * OD widths); the PHY needs it only to delimit the incoming cyclic frame
     * (Type-0 frames are delimited by the frame-end time-out instead). Keeping a
     * single source avoids deriving the same length twice. Optional: may be NULL.
     * @param len  MC + CKT + PD-out + OD octets of the cyclic frame
     */
    void (*set_cyclic_frame_len)(void* user, uint8_t len);

    /* Optional Diagnostic/Support Functions (can be NULL) */

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

    /**
     * @brief Manually set C/Q line state (for SIO push-pull)
     * @param state 0 for Low, 1 for High
     */
    void (*set_cq_line)(void* user, uint8_t state);

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
} iolink_phy_api_t;

/** @} */ /* end of iolinki_phy */

#endif  // IOLINK_PHY_H
