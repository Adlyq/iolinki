/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CONFIG_H
#define IOLINK_CONFIG_H

/**
 * @file config.h
 * @brief Compile-time configuration for iolinki stack.
 *
 * This file defines default values for buffer sizes and queue depths.
 * To override these defaults, define the macros in your build system
 * or in a private config header included before this file.
 */

/**
 * @defgroup iolinki_config Compile-time Configuration
 * @brief Overridable buffer sizes, queue depths and timing defaults.
 * @{
 */

/* -------------------------------------------------------------------------
 * PHY / line-state configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Whether the bound PHY owns the C/Q line state (wake-up / SIO<->SDCI).
 *
 * When non-zero the DLL skips the electrical wake-up handshake and runs in SDCI
 * (the PHY is contractually an already-established SDCI link). When zero (the
 * default) the DLL infers this at run time from the PHY: a PHY without a
 * wake-up detector (detect_wakeup == NULL) is treated as owning the line state.
 *
 * Override to 1 in the build system for a PHY such as the L6364 that manages the
 * SIO/wake-up handshake itself.
 * Default: 0 (keep the run-time inference).
 */
#ifndef IOLINK_PHY_OWNS_LINE_STATE
#define IOLINK_PHY_OWNS_LINE_STATE 0
#endif

/* -------------------------------------------------------------------------
 * Feature trimming (compile-time)
 *
 * Each macro is 1 (compiled in, the default full stack) or 0 (removed). The
 * project build overrides the unused ones to 0 for a smaller image. Trimmed
 * code that is actually used results in a compile error, which is the intended
 * guard. See docs/feature-trimming.md.
 *
 * IOLINK_MINIMAL_PROFILE is a convenience switch that defaults the trimmed
 * features below off; an explicit -D always wins (guarded with #ifndef).
 * It does not affect IOLINK_FEATURE_EVENTS (kept by this project).
 * ------------------------------------------------------------------------- */

#ifdef IOLINK_MINIMAL_PROFILE
#ifndef IOLINK_FEATURE_DATA_STORAGE
#define IOLINK_FEATURE_DATA_STORAGE 0
#endif
#ifndef IOLINK_FEATURE_SYSTEM_COMMAND
#define IOLINK_FEATURE_SYSTEM_COMMAND 0
#endif
#ifndef IOLINK_FEATURE_LEGACY_PARAMS
#define IOLINK_FEATURE_LEGACY_PARAMS 0
#endif
#ifndef IOLINK_PHY_FEATURE_WAKEUP
#define IOLINK_PHY_FEATURE_WAKEUP 0
#endif
#ifndef IOLINK_PHY_FEATURE_SIO_CQ
#define IOLINK_PHY_FEATURE_SIO_CQ 0
#endif
#ifndef IOLINK_PHY_FEATURE_LEGACY_DIAG
#define IOLINK_PHY_FEATURE_LEGACY_DIAG 0
#endif
#endif /* IOLINK_MINIMAL_PROFILE */

/** @brief Data Storage subsystem (data_storage.c, ISDU DS service, ds ctx). */
#ifndef IOLINK_FEATURE_DATA_STORAGE
#define IOLINK_FEATURE_DATA_STORAGE 1
#endif
/** @brief ISDU SystemCommand handling (0x01-0x06, 0x80-0x83). */
#ifndef IOLINK_FEATURE_SYSTEM_COMMAND
#define IOLINK_FEATURE_SYSTEM_COMMAND 1
#endif
/** @brief Diagnostic Events engine + DDS + CKS event flag + fault polling. */
#ifndef IOLINK_FEATURE_EVENTS
#define IOLINK_FEATURE_EVENTS 1
#endif
/** @brief Legacy global params API + g_legacy_* globals (ctx API preferred). */
#ifndef IOLINK_FEATURE_LEGACY_PARAMS
#define IOLINK_FEATURE_LEGACY_PARAMS 1
#endif
/** @brief PHY wake-up detection (detect_wakeup). */
#ifndef IOLINK_PHY_FEATURE_WAKEUP
#define IOLINK_PHY_FEATURE_WAKEUP 1
#endif
/** @brief PHY manual C/Q push-pull (set_cq_line). */
#ifndef IOLINK_PHY_FEATURE_SIO_CQ
#define IOLINK_PHY_FEATURE_SIO_CQ 1
#endif
/** @brief PHY legacy diagnostics (get_voltage_mv + is_short_circuit). */
#ifndef IOLINK_PHY_FEATURE_LEGACY_DIAG
#define IOLINK_PHY_FEATURE_LEGACY_DIAG 1
#endif

/* -------------------------------------------------------------------------
 * ISR transport granularity.
 * These two switches select how the PHY moves data on the ISR path:
 *
 *   RX BYTE  : phy->rx_octet()  - library frames octet by octet
 *   RX FRAME : phy->rx_frame()  - PHY delivers a whole frame (zero-copy)
 *   TX BYTE  : phy->tx_octet()+phy->tx_done() - library feeds octet by octet
 *   TX FRAME : phy->tx_frame()  - library hands a whole reply frame
 *
 * Defaults are BYTE/BYTE (single-octet transceivers, e.g. the L6364). A PHY
 * with whole-frame DMA sets the matching macro to FRAME in the build.
 * ------------------------------------------------------------------------- */
#define IOLINK_ISR_RX_BYTE 0
#define IOLINK_ISR_RX_FRAME 1
#define IOLINK_ISR_TX_BYTE 0
#define IOLINK_ISR_TX_FRAME 1

#ifndef IOLINK_ISR_RX_MODE
#define IOLINK_ISR_RX_MODE IOLINK_ISR_RX_BYTE
#endif
#ifndef IOLINK_ISR_TX_MODE
#define IOLINK_ISR_TX_MODE IOLINK_ISR_TX_BYTE
#endif

/**
 * @brief Require the PHY request_service() hook for ISR-pump replies.
 *
 * 0 (default): a PHY without the hook keeps the legacy direct send from the
 * calling context (host tests). 1: no fallback; every reply is reported from
 * the ISR, so the board must provide request_service().
 */
#ifndef IOLINK_PHY_FEATURE_ISR_TX_ONLY
#define IOLINK_PHY_FEATURE_ISR_TX_ONLY 0
#endif

/* -------------------------------------------------------------------------
 * Concurrency profile.
 *
 * The profile tells the stack which asynchronous primitives a port must
 * provide (see include/iolinki/port.h and docs/PORTING.md). It selects the
 * minimum set; it never adds an assumption the target cannot meet.
 *
 *   SINGLE : no asynchronous context (host simulation, cooperative loop).
 *            No barrier and no critical section are required; the library
 *            does not reference the critical-section symbols at all.
 *   ISR    : one data ISR preempts one main context (the default dual-pump
 *            transport). Needs a naturally atomic publish word and at least a
 *            compiler barrier; the critical section is still required while
 *            the mailbox/ISDU staging paths use it.
 *   RTOS   : the main context is spread over tasks; additionally needs the
 *            application thread lock (iolink_device_config_t::lock/unlock).
 *   SMP    : the pump and the main context may run on different cores; needs
 *            a system-wide (SMP) barrier.
 * ------------------------------------------------------------------------- */
/** @brief No asynchronous context. */
#define IOLINK_CONCURRENCY_SINGLE 0
/** @brief One data ISR preempts one main context. */
#define IOLINK_CONCURRENCY_ISR 1
/** @brief Main context spread over RTOS tasks. */
#define IOLINK_CONCURRENCY_RTOS 2
/** @brief Separate cores for the pump and the main context. */
#define IOLINK_CONCURRENCY_SMP 3

#ifndef IOLINK_CONCURRENCY
#define IOLINK_CONCURRENCY IOLINK_CONCURRENCY_ISR
#endif

/**
 * @brief How the DLL statistics counters are shared between contexts.
 *
 * SHARED (default): one counter set, updated by both contexts. An increment
 * that races a preemption can be lost (diagnostics only). SPLIT: one counter
 * set per context, summed when the statistics are read, so no update is lost
 * and no critical section is involved.
 */
#define IOLINK_STATS_SHARED 0
#define IOLINK_STATS_SPLIT 1

#ifndef IOLINK_STATS_MODE
#define IOLINK_STATS_MODE IOLINK_STATS_SHARED
#endif

/* -------------------------------------------------------------------------
 * Protocol compatibility
 * ------------------------------------------------------------------------- */

/**
 * @brief Accept the V1.0 STARTUP -> OPERATE DeviceOperate (0x99) shortcut.
 *
 * The V1.1 Test Specification requires a non-V1.0 Device not to accept an
 * OPERATE request while still in STARTUP. Enable only for a product that must
 * interoperate with a V1.0 Master.
 * Default: 0 (V1.1 strict).
 */
#ifndef IOLINK_COMPAT_V10_STARTUP_OPERATE
#define IOLINK_COMPAT_V10_STARTUP_OPERATE 0
#endif

/**
 * @brief Answer a TYPE_0 page read while OPERATE is active.
 *
 * Table 57/67 require such a read to drop the response and return the Device
 * to STARTUP. Some legacy Masters repeat TYPE_0 page reads after entering
 * OPERATE; enable only for those deployments.
 * Default: 0 (V1.1 strict).
 */
#ifndef IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
#define IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE 0
#endif

/**
 * @brief Keep the pre-V1.1 whole-image payload on DataStorageIndex subindex 0.
 *
 * V1.1 Table B.10/B.11 require subindex 0 to be the concatenated record
 * (12 + 3n octets). Enable only for a legacy application that still reads the
 * old image format.
 * Default: 0 (V1.1 strict).
 */
#ifndef IOLINK_DS_LEGACY_IMAGE_SUB0
#define IOLINK_DS_LEGACY_IMAGE_SUB0 0
#endif

/* -------------------------------------------------------------------------
 * ISDU Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Max size of ISDU buffer (Service Data).
 * Limits the maximum size of a single ISDU read/write transaction.
 * Default: 256 bytes (sufficient for standard params).
 */
#ifndef IOLINK_ISDU_BUFFER_SIZE
#define IOLINK_ISDU_BUFFER_SIZE 256U
#endif

/**
 * @brief Maximum size of the serialized Data Storage parameter image.
 *
 * Must hold every DS-backed parameter record. Each record is laid out as
 * [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)]. This is also
 * the size of the DS region in the persistent storage layout (storage.h).
 */
#ifndef IOLINK_DS_IMAGE_MAX
#define IOLINK_DS_IMAGE_MAX 256U
#endif

/* -------------------------------------------------------------------------
 * Events Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Size of the diagnostic Event Queue.
 * Start small for resource constrained devices.
 * Default: 4
 */
#ifndef IOLINK_EVENT_QUEUE_SIZE
#define IOLINK_EVENT_QUEUE_SIZE 4U
#endif

/**
 * @brief Minimum delay between an "Event appears" and its "Event disappears".
 *
 * The event dispatcher must not issue a disappearance within this window
 * (A.6.4 / Annex A dispatch rules); a condition that clears sooner is held until
 * the window has elapsed. Default: 50 ms
 */
#ifndef IOLINK_EVENT_MIN_APPEAR_MS
#define IOLINK_EVENT_MIN_APPEAR_MS 50U
#endif

/**
 * @brief Minimum interval between two reports of the same EventCode/qualifier.
 *
 * A.6.4: the Event Dispatcher shall not invoke the AL_Event service with the
 * same EventCode and EventQualifier more often than once per second.
 * Default: 1000 ms
 */
#ifndef IOLINK_EVENT_MIN_REPEAT_MS
#define IOLINK_EVENT_MIN_REPEAT_MS 1000U
#endif

/* -------------------------------------------------------------------------
 * Process Data Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Max Input Process Data (Device -> Master) size in bytes.
 * V1.1 Spec supports up to 32 bytes allowed in standard M-sequences.
 * Default: 32
 */
#ifndef IOLINK_PD_IN_MAX_SIZE
#define IOLINK_PD_IN_MAX_SIZE 32U
#endif

/**
 * @brief Max Output Process Data (Master -> Device) size in bytes.
 * V1.1 Spec supports up to 32 bytes allowed in standard M-sequences.
 * Default: 32
 */
#ifndef IOLINK_PD_OUT_MAX_SIZE
#define IOLINK_PD_OUT_MAX_SIZE 32U
#endif

/* -------------------------------------------------------------------------
 * Timing Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Enable timing enforcement by default.
 * Set to 1 to enforce t_ren / t_cycle, 0 to only measure.
 */
#ifndef IOLINK_TIMING_ENFORCE_DEFAULT
#define IOLINK_TIMING_ENFORCE_DEFAULT 0U
#endif

/**
 * @brief Wake-up pulse duration (T_WU) in microseconds.
 *
 * Spec Table 10: the wake-up request is a current pulse; this stack models the
 * pulse width used by the PHY as 80 us. (T_DWU is a separate 30..50 ms
 * retry delay and is not modelled by the device.)
 */
#ifndef IOLINK_T_WU_US
#define IOLINK_T_WU_US 80U
#endif

/**
 * @brief Response time limit (T_REN) in microseconds.
 *
 * Spec Table 10: a single value of at most 500 us, independent of the baudrate.
 */
#ifndef IOLINK_T_REN_US
#define IOLINK_T_REN_US 500U
#endif

/**
 * @brief OPERATE supervision window in milliseconds.
 *
 * If no checksum/type-valid frame arrives for this long while the DLL is in
 * OPERATE, the master is considered gone: the device returns to STARTUP/SIO and
 * refreshes the PHY, so a subsequent master restart (WURQ) is handled cleanly
 * instead of leaving the device stuck showing "connected".
 */
#ifndef IOLINK_OPERATE_TIMEOUT_MS
#define IOLINK_OPERATE_TIMEOUT_MS 200U
#endif

/**
 * @brief Fallback to SIO after a failed wake-up without a valid message (T_DSIO).
 *
 * Spec Table 47 T10: 60..300 ms, default 300 ms.
 */
#ifndef IOLINK_T_DSIO_MS
#define IOLINK_T_DSIO_MS 300U
#endif

/* -------------------------------------------------------------------------
 * On-Request Data (OD) Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Maximum On-Request Data width m in bytes (Table A.10).
 * TYPE_1_V/TYPE_2_V allow 1/2/8/32 octets; default the buffer to the spec
 * maximum so any configured od_width fits.
 * Default: 32 bytes.
 */
#ifndef IOLINK_OD_MAX_SIZE
#define IOLINK_OD_MAX_SIZE 32U
#endif

/**
 * @brief OD Event Mode: Single (0) or Multiple (1) event mode.
 * Single mode: Only one event transmitted per OD cycle.
 * Multiple mode: Multiple events can be queued in OD.
 * Default: 0 (Single event mode)
 */
#ifndef IOLINK_OD_EVENT_MODE
#define IOLINK_OD_EVENT_MODE 0U
#endif

/** @} */ /* end of iolinki_config */

#endif  // IOLINK_CONFIG_H
