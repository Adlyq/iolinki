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
 * guard. See Docs/裁剪与配置.md.
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
#ifndef IOLINK_FEATURE_MASTER_IDENT
#define IOLINK_FEATURE_MASTER_IDENT 0
#endif
#ifndef IOLINK_FEATURE_LEGACY_PARAMS
#define IOLINK_FEATURE_LEGACY_PARAMS 0
#endif
#ifndef IOLINK_PHY_FEATURE_STREAMING
#define IOLINK_PHY_FEATURE_STREAMING 0
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
/** @brief MasterCommand ident subset (0x95/0x96). */
#ifndef IOLINK_FEATURE_MASTER_IDENT
#define IOLINK_FEATURE_MASTER_IDENT 1
#endif
/** @brief Diagnostic Events engine + DDS + CKS event flag + fault polling. */
#ifndef IOLINK_FEATURE_EVENTS
#define IOLINK_FEATURE_EVENTS 1
#endif
/** @brief Legacy global params API + g_legacy_* globals (ctx API preferred). */
#ifndef IOLINK_FEATURE_LEGACY_PARAMS
#define IOLINK_FEATURE_LEGACY_PARAMS 1
#endif
/** @brief PHY streaming transport (send + recv_byte). */
#ifndef IOLINK_PHY_FEATURE_STREAMING
#define IOLINK_PHY_FEATURE_STREAMING 1
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
