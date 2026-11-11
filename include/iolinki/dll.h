/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_H
#define IOLINK_DLL_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/phy.h"
#include "iolinki/config.h"
#include "iolinki/port.h"
#include "iolinki/ring.h"

/**
 * @file dll.h
 * @brief IO-Link Data Link Layer (DLL) Implementation
 */

/**
 * @defgroup iolinki_dll Data Link Layer (DLL)
 * @brief Frame timing, M-sequence exchange, mode/baudrate control and DLL statistics.
 * @{
 */

/**
 * @brief IO-Link DLL State Machine states
 */
typedef enum
{
    IOLINK_DLL_STATE_STARTUP = 0U,       /**< Waiting for wake-up request */
    IOLINK_DLL_STATE_AWAITING_COMM = 1U, /**< Wake-up detected, waiting for first frame */
    IOLINK_DLL_STATE_PREOPERATE = 2U,    /**< Handling ISDU (Type 0) */
    IOLINK_DLL_STATE_ESTAB_COM = 3U,     /**< Establish communication (transition to OPERATE) */
    IOLINK_DLL_STATE_OPERATE = 4U,       /**< Cyclic data exchange active */
    IOLINK_DLL_STATE_FALLBACK = 5U,      /**< Error recovery / fallback */
    IOLINK_DLL_STATE_COMM_DISABLED = 6U  /**< Back-to-box: communication stopped until reset */
} iolink_dll_state_t;

/**
 * @brief Optional hook invoked when the DLL state machine changes state.
 *
 * Notification is level-based: the hook is called from
 * iolink_dll_flush_state_callbacks() with the *current* state when it differs
 * from the last state delivered. The callback therefore never runs in
 * interrupt context, the final state is never missed, and intermediate states
 * that were entered and left between two deliveries are coalesced into the
 * later one, except an ISR-side transition into STARTUP, which is always
 * delivered (Table 45 T11 requires the reset to reach system management).
 * Used by the core to drive application lifecycle callbacks.
 *
 * The hook runs in main context, but it may be invoked from inside
 * iolink_dll_process() (a main-context transition notifies immediately, while
 * the frame dispatch is still on the stack). A hook must therefore not call
 * iolink_dll_* / iolink_device_* re-entrantly.
 */
typedef void (*iolink_dll_state_cb_t)(void* user, iolink_dll_state_t state);

/**
 * @brief Which reply layout an incoming M-sequence requires (Figure A.5/A.14).
 *
 * Derived purely from the request's MC octet plus the current state / configured
 * M-sequence type, so the ISR-side pump can select a pre-built reply image
 * without running any protocol logic itself. The content of each variant is
 * still produced by the main pump.
 */
typedef enum
{
    IOLINK_REPLY_NONE = 0U,    /**< No pre-staged variant (main pump answers) */
    IOLINK_REPLY_CYCLIC_READ,  /**< OPERATE process READ  : payload [OD...][PD-in...] */
    IOLINK_REPLY_CYCLIC_WRITE, /**< OPERATE process WRITE : payload [PD-in...] (no OD) */
    IOLINK_REPLY_ACYCLIC_READ, /**< OPERATE OD READ (ISDU/page/diag): payload
                                    [od_resp...][PD-in...] (Busy until answered) */
    IOLINK_REPLY_TYPE0_OD,     /**< Type-0 READ   : answered by the main pump */
    IOLINK_REPLY_TYPE0_CKS     /**< Type-0 WRITE  : answered by the main pump */
} iolink_reply_variant_t;

/** Largest M-sequence the library frames (request or reply), in octets. */
#define IOLINK_M_SEQ_MAX_LEN (IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U)

/* PD reply send buffer: [OD][PD-in][CKS]. Sized for the runtime PD-in length
 * plus the OD header and the trailing checksum. */
#define IOLINK_PD_REPLY_MAX (IOLINK_OD_MAX_SIZE + IOLINK_PD_IN_MAX_SIZE + 1U)
/* PD-in reply buffers: active, being filled, and held by the ISR.
 *
 * TX BYTE ends the transfer inside the library, which releases the buffer the
 * ISR held (pd_in_captured = 0xFF) at frame end, so two buffers are enough.
 * TX FRAME hands the buffer to a DMA that the library cannot observe unless the
 * PHY provides tx_busy(): with the hook the lease is released as soon as the
 * line is free (two buffers); without it the slot stays leased until the next
 * capture, so a third buffer absorbs the main pump's update. */
#if (IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME) && !IOLINK_PHY_FEATURE_TX_BUSY
#define IOLINK_PD_IN_BUFFERS 3U
#else
#define IOLINK_PD_IN_BUFFERS 2U
#endif
/** Received-frame mailbox depth (data ISR -> main pump). A power of two; 2
 * slots absorb one frame while the main pump is still handling the previous
 * one, so a busy main window no longer drops the next request. */
#define IOLINK_MBX_SLOTS 2U
/** Staged ISDU write depth (data ISR -> main pump); also a power of two. */
#define IOLINK_OD_SLOTS 2U

IOLINK_STATIC_ASSERT((IOLINK_MBX_SLOTS >= 2U) &&
                         ((IOLINK_MBX_SLOTS & (IOLINK_MBX_SLOTS - 1U)) == 0U),
                     "IOLINK_MBX_SLOTS must be a power of two >= 2");
IOLINK_STATIC_ASSERT((IOLINK_OD_SLOTS >= 2U) && ((IOLINK_OD_SLOTS & (IOLINK_OD_SLOTS - 1U)) == 0U),
                     "IOLINK_OD_SLOTS must be a power of two >= 2");

/** Reply staging slots: 0/1 for the main pump (alternating), 2 for the ISR. */
#define IOLINK_TX_SLOTS 3U
/** Slot the data ISR uses for its own immediate replies. */
#define IOLINK_TX_ISR_SLOT 2U
IOLINK_STATIC_ASSERT(IOLINK_TX_SLOTS == IOLINK_TX_ISR_SLOT + 1U,
                     "the ISR reply slot must be the last reply slot");

/**
 * @brief Counters the data ISR also updates.
 *
 * With `IOLINK_STATS_MODE=SPLIT` each context keeps its own copy, so an
 * increment cannot be lost to a preemption between its load and its store; the
 * public statistics sum both sets. With `SHARED` (the default) the ISR writes
 * the same fields as the main pump and one update can be lost.
 */
typedef struct
{
    uint32_t framing_errors; /**< Frames rejected by the ISR framing engine */
    uint32_t timeout_errors; /**< Timer A timeouts seen by the ISR */
    uint32_t isr_tx_dropped; /**< Replies the ISR could not transmit */
} iolink_dll_isr_stats_t;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
/** @brief Counter slot for an increment performed in ISR context. */
#define IOLINK_STAT_ISR(ctx, field) ((ctx)->isr_stat.field)
/** @brief Counter value including the ISR-owned set (read-only). */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->field + (ctx)->isr_stat.field)
#else
/** @brief Counter slot for an increment performed in ISR context. */
#define IOLINK_STAT_ISR(ctx, field) ((ctx)->field)
/** @brief Counter value including the ISR-owned set (read-only). */
#define IOLINK_STAT_TOTAL(ctx, field) ((ctx)->field)
#endif

/**
 * @name ISDU read view packing
 *
 * The main pump publishes the framed-response view as one `IOLINK_ATOMIC_UINT_T`
 * so the data ISR reads a consistent {length, busy, buffer} tuple in one access.
 * The ISR keeps a copy of the last word it adopted at START; any change (a new
 * response, or a Busy transition) breaks the FlowCTRL sequence, which is exactly
 * the behaviour the incremental view had.
 * @{
 */
#define IOLINK_ISDU_PUB_LEN(word) ((uint8_t) ((word) & 0xFFU))
#define IOLINK_ISDU_PUB_BUSY(word) ((uint8_t) (((word) >> 8) & 0x01U))
#define IOLINK_ISDU_PUB_SEL(word) ((uint8_t) (((word) >> 9) & 0x01U))
#define IOLINK_ISDU_PUB_WORD(len, busy, sel)                                                     \
    ((IOLINK_ATOMIC_UINT_T) (uint8_t) (len) | ((IOLINK_ATOMIC_UINT_T) ((busy) ? 1U : 0U) << 8) | \
     ((IOLINK_ATOMIC_UINT_T) ((sel) ? 1U : 0U) << 9))
/** @} */

/** @brief One staged ISDU write segment (data ISR -> main pump). */
typedef struct
{
    uint8_t flowctrl;                 /**< FlowCTRL nibble from the request MC octet */
    uint8_t len;                      /**< OD octets in @ref data */
    uint8_t data[IOLINK_OD_MAX_SIZE]; /**< OD octets */
} iolink_od_rec_t;

/**
 * @brief One received frame in the mailbox ring (data ISR -> main pump).
 *
 * The record carries everything the main pump needs about the frame, so no
 * per-frame state lives outside the ring: the length and whether the ISR
 * already transmitted the reply (eager) travel with the octets.
 */
typedef struct
{
    uint8_t len;                        /**< Frame length in octets */
    uint8_t eager;                      /**< Non-zero when the ISR already replied to this frame */
    uint8_t data[IOLINK_M_SEQ_MAX_LEN]; /**< Frame octets */
} iolink_frame_rec_t;

#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/data_storage.h"

/**
 * @brief Data Link Layer Context
 *
 * Internal state and data storage for the DLL engine.
 */
typedef struct
{
    volatile iolink_dll_state_t state; /**< Current DLL state */
    const iolink_phy_api_t* phy;       /**< Bound PHY API implementation */
    /* Application clock bound from iolink_device_config_t::time_us. The DLL and
     * event engine use it for every timestamp. The data ISR reads it, so the
     * callback must be monotonic and ISR-safe. A bare DLL context without a
     * bound clock reads time as 0. */
    uint64_t (*time_us)(void* user);
    void* time_user;
    volatile uint32_t last_activity_ms; /**< Timestamp of last received byte */
    uint32_t last_valid_frame_ms;       /**< Timestamp of the last checksum/type-valid frame */
    volatile uint32_t master_last_activity_ms; /**< Timestamp of the last frame end / RX timeout */
    uint8_t master_lost_cycles; /**< Missed master cycles before master_lost; 0 = disabled */
    volatile uint8_t master_lost_count;   /**< Missed cycles, saturated at 0xFF */
    volatile bool master_lost;            /**< Master communication lost (application hint) */
    volatile uint8_t isr_master_activity; /**< ISR saw a frame end; main clears it */
    bool wakeup_seen;                     /**< Wake-up detected (if PHY supports it) */
    volatile bool back_to_box_pending;    /**< Back-to-box requested; counting down frames */
    volatile uint8_t back_to_box_cycles;  /**< Frames seen since Back-to-box was requested */

    /* One-shot handshake flags, set by either context and consumed by the main
     * pump; the 64-bit deadlines stay main-owned. Each is set to 1 by either
     * context and cleared to 0 only by the main pump; a set that races the
     * consume is idempotent (the request is applied in this pass or the next
     * one). phy_faults_rearm is consumed inside the critical section. */
    volatile uint8_t isr_dsio_clear;         /**< ISR served the startup probe: clear T_DSIO */
    volatile uint8_t isr_fallback_pending;   /**< ISR accepted FALLBACK: arm T_FBD from main */
    volatile uint8_t fallback_clear_request; /**< A non-FALLBACK command was applied: clear T_FBD */
    volatile uint8_t phy_faults_rearm;       /**< Event engine was reset: re-arm the fault latch */

    /* Configuration */
    uint8_t m_seq_type; /**< Supported M-sequence type */
    uint8_t pd_in_len;  /**< Input Process Data length */
    uint8_t pd_out_len; /**< Output Process Data length */
    uint8_t od_len;     /**< Streaming-path OD length (1 or 2 bytes). */
    uint8_t od_width;   /**< OPERATE OD width m (Table A.10). Whether a frame
                             carries OD is decided per frame from the MC R/W
                             bit (Figure A.14): a WRITE request carries OD
                             after PD-out, a READ reply carries OD before PD-in,
                             and the opposite direction carries none. */
    /* ISR ISDU read view. The main pump publishes {length, busy, buffer} as one
     * atomic word so the ISR never observes a half-updated tuple; the ISR owns
     * the FlowCTRL segment index and adopts a new word at START. */
    volatile IOLINK_ATOMIC_UINT_T isr_isdu_pub; /**< len | busy<<8 | sel<<9 */
    IOLINK_ATOMIC_UINT_T isr_isdu_adopted;      /**< Word adopted by the ISR */
    uint8_t isr_isdu_len;                       /**< Adopted response length (0 = none) */
    uint8_t isr_isdu_sel;                       /**< Adopted response buffer index */
    uint8_t isr_isdu_idx;                       /**< Next response octet (ISR advances) */
    uint8_t isr_isdu_last_flowctrl;             /**< Last accepted FlowCTRL (0xFF=none) */
    uint8_t isr_isdu_seg_idx;                   /**< Offset of the last served segment */

    /* Raw-octet ISR pump state (owned by the library, driven from the PHY data
     * interrupt). Only these fields are touched from ISR context; the reply
     * staging buffer + octet feed are owned by the library (the PHY only does
     * the SPI electrical access). */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    uint8_t isr_rx_buf[IOLINK_M_SEQ_MAX_LEN]; /**< RX octets accumulated */
    uint8_t isr_rx_cnt;                       /**< Octets accumulated this frame */
    uint8_t isr_rx_acc;                       /**< A.1.6 checksum accumulated per octet */
    uint8_t isr_rx_expect;                    /**< Expected length (0 = unknown) */
#endif
    iolink_frame_rec_t isr_frame_slot[IOLINK_MBX_SLOTS]; /**< Mailbox ring storage */
    iolink_ring_t isr_frame_ring;                        /**< Frame mailbox (ISR -> main) */
    volatile uint8_t frame_replied_by_isr; /**< Main-context snapshot of the record's eager flag
                                       for the frame currently being processed. */
    uint32_t isr_mailbox_drops;            /**< Frames dropped because the mailbox was full */
    uint32_t pd_in_skips;       /**< PD-in publishes skipped: every spare buffer leased */
    uint32_t isr_eager_rejects; /**< Frames the eager path refused
                                     (bad checksum/type) */
    /* ISR-side ISDU staging + response snapshot (single-writer model). The ISR
     * stages write OD octets in a ring and reads only the response snapshot;
     * the main pump owns ctx->isdu and refreshes the snapshot when it changes. */
    iolink_od_rec_t isr_od_slot[IOLINK_OD_SLOTS]; /**< Staged write ring storage */
    iolink_ring_t isr_od_ring;                    /**< Staged ISDU writes (ISR -> main) */
    uint32_t isr_od_overruns;                     /**< Staged writes dropped (ring full) */
#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    iolink_dll_isr_stats_t isr_stat; /**< ISR-owned counter set (SPLIT mode) */
#endif
    volatile uint8_t stack_tick_pending;           /**< Timer B tick authorised a
                                                        main-context service window */
    volatile uint8_t in_isr;                       /**< ISR is running the pump */
    volatile uint8_t isr_reentry_pending;          /**< A preempting pump invocation
                                                         was deferred to the outer one */
    volatile uint8_t startup_notification_pending; /**< ISR entered STARTUP; main
                                                        context must report it */
    iolink_dll_state_t last_notified_state;        /**< State already delivered to
                                                         state_cb (main context only) */
    /* Reply transmission, fed one octet per data request. The main pump
     * alternates between slots 0/1 so the in-flight TX-FRAME DMA buffer stays
     * stable while the next reply is prepared; slot 2 is reserved for replies
     * the data ISR generates itself, so no slot counter is shared between the
     * two contexts (the PHY serialises the transfers). */
    uint8_t isr_tx_payload[IOLINK_TX_SLOTS][IOLINK_M_SEQ_MAX_LEN];
    const uint8_t* isr_tx_base;           /**< Active reply source:
                                               pd_in[active] for the long
                                               PD reply, isr_tx_payload
                                               for short/main replies */
    volatile uint8_t isr_tx_slot;         /**< Slot being transmitted */
    uint8_t isr_tx_next;                  /**< Next slot the main pump fills (0/1) */
    volatile uint8_t isr_tx_pending_slot; /**< Slot of a staged main reply */
    volatile uint8_t isr_tx_len;          /**< Payload octets held */
    volatile uint8_t isr_tx_idx;          /**< Next octet index */
    volatile uint8_t isr_tx_flags;        /**< CKS status flags */
    volatile uint8_t isr_tx_acc;          /**< Running checksum accumulator */
    volatile uint8_t isr_tx_has_cks;      /**< 1 = append computed CKS */
    volatile uint8_t isr_tx_active;       /**< 1 while draining */
    volatile uint8_t isr_tx_pending;      /**< 1 = a main-context handler
                                               staged a verbatim reply;
                                               the ISR must transmit it */
    volatile uint8_t isr_tx_dropped;      /**< Replies dropped because an
                                               ISR-only PHY lacks
                                               request_service() */
    bool pd_valid;                        /**< Current PD_In validity status */
    uint8_t master_cycle_time;            /**< DPP1 0x01, the cycle-time octet written by the
                                               Master (Table B.1/B.3); 0 = not yet written */
    uint32_t min_cycle_time_us;           /**< Minimum cycle time in microseconds */
    uint32_t min_cycle_time_app_us;       /**< Application floor for the cycle time */
    bool enforce_timing;                  /**< Enable timing checks (t_ren / t_cycle) */
    uint32_t t_ren_limit_us;              /**< Current t_ren limit in microseconds */
    bool t_ren_override;                  /**< Use overridden t_ren limit if true */
    uint32_t t_pd_delay_us;               /**< Power-on delay (t_pd) in microseconds */

    /* Variable PD Support (for Type 1_V and 2_V) */
    uint8_t pd_in_len_current;  /**< Current runtime PD_In length */
    uint8_t pd_out_len_current; /**< Current runtime PD_Out length */
    uint8_t pd_in_len_max;      /**< Maximum allowed PD_In length */
    uint8_t pd_out_len_max;     /**< Maximum allowed PD_Out length */

    /* SIO Mode */
    iolink_phy_mode_t phy_mode; /**< Current operating mode (SDCI vs SIO) */

    /* Baudrate Support */
    iolink_baudrate_t baudrate; /**< Negotiated baudrate (COM1-COM3) */
    iolink_baudrate_t
        operating_baudrate; /**< Operating COM; IOLINK_BAUDRATE_UNSET if not configured */

    /* Unified Frame Assembly: a master request is MC + CKT + PD-out + OD, so
       size it from the compile-time maxima (was a fixed 48, too small once the
       OD width may reach IOLINK_OD_MAX_SIZE). */
    uint8_t frame_buf[IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U];
    uint8_t frame_index;            /**< Current byte index in assembly */
    uint8_t req_len;                /**< Expected length of current frame type */
    uint64_t last_frame_us;         /**< Microsecond timestamp of last frame start */
    volatile uint32_t last_byte_us; /**< Low 32 bits of the last-byte timestamp; both
                                         contexts compare it with a wrap-safe unsigned
                                         delta (t_byte is milliseconds at most) */
    uint64_t last_cycle_start_us;   /**< Microsecond timestamp of last cycle start */
    uint32_t t_byte_limit_us;       /**< Inter-byte timeout limit in microseconds */
    uint64_t wakeup_deadline_us;    /**< Earliest time to accept frames after wake-up */
    uint64_t t_pd_deadline_us;      /**< Earliest time to accept frames after power-on */
    uint64_t dsio_deadline_ms;      /**< Deadline to fall back to SIO without a valid message */
    uint64_t fallback_deadline_ms;  /**< Deadline to complete a FALLBACK MasterCommand */

    /* Process Data Buffers. The PD-in buffer doubles as the reply send buffer:
     * layout [OD][PD-in][CKS]. The main fills a buffer that is neither active
     * nor held by the ISR, then swaps it in atomically; the ISR captures the
     * active copy at reply start and streams it (zero-copy). The buffer count
     * depends on the TX mode (see IOLINK_PD_IN_BUFFERS): when no buffer is free
     * the main keeps the current PD-in and retries on the next cycle. */
    uint8_t pd_in[IOLINK_PD_IN_BUFFERS][IOLINK_PD_REPLY_MAX]; /**< [OD][PD-in][CKS] reply buffers */
    volatile uint8_t pd_in_active;                            /**< Active PD reply buffer index */
    volatile uint8_t pd_in_captured;        /**< Buffer the ISR is streaming (0xFF = none) */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /**< Output PD buffer (Master -> Device) */

    bool pd_out_seen; /**< A PDout frame has been received (B.2.24) */

    /* Error Counters & Statistics */
    uint32_t crc_errors;             /**< Cumulative CRC error count */
    uint32_t timeout_errors;         /**< Cumulative t_A timeout count */
    uint32_t idle_timeouts;          /**< 1 s idle-link diagnostics (master paused) */
    uint32_t framing_errors;         /**< Cumulative framing error count */
    uint32_t timing_errors;          /**< Cumulative timing violations */
    uint32_t t_ren_violations;       /**< t_ren violations */
    uint32_t t_cycle_violations;     /**< t_cycle violations */
    uint32_t t_byte_violations;      /**< Inter-byte timing violations */
    uint32_t t_pd_violations;        /**< t_pd violations */
    uint8_t retry_count;             /**< Retry counter for current exchange */
    uint32_t total_retries;          /**< Cumulative retry count */
    uint8_t max_retries;             /**< Configured max retries (default 3) */
    uint32_t voltage_faults;         /**< Cumulative voltage fault count (edge per fault
                                          with get_faults(), poll cycle on the legacy path) */
    uint32_t overtemp_faults;        /**< Cumulative over-temperature fault count (edge per
                                          fault with get_faults(), 0 on the legacy path) */
    uint32_t short_circuits;         /**< Cumulative short circuit count (edge per fault with
                                          get_faults(), poll cycle on the legacy path) */
    uint8_t phy_faults_last;         /**< Last get_faults() bitmask (edge detection) */
    volatile uint8_t fallback_count; /**< Consecutive fallback count for SIO transition */
    uint8_t sio_fallback_threshold;  /**< Fallback threshold to enter SIO mode (default 3) */

    /* Timing Statistics */
    uint64_t last_response_us; /**< Microsecond timestamp of last response */
    uint32_t response_time_us; /**< Measured stack response time (t2) */

    /* Sub-modules */
#if IOLINK_FEATURE_EVENTS
    iolink_events_ctx_t events; /**< Diagnostic Events engine */
#endif
    iolink_isdu_ctx_t isdu; /**< ISDU Service engine */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t ds; /**< Data Storage engine */
#endif

    iolink_dll_state_cb_t state_cb; /**< Optional state-change hook (set by core) */
    void* state_cb_user;            /**< User pointer passed to state_cb */
} iolink_dll_ctx_t;

/**
 * @brief PHY transport capability helpers.
 *
 * The library has a single transport path (the ISR dispatch pump). The bound
 * PHY expresses how it moves data through the two ISR-mode compile switches:
 *  - RX      : rx_octet (byte) or rx_frame (whole frame)      [IOLINK_ISR_RX_MODE]
 *  - TX      : tx_octet + tx_done (byte) or tx_frame + tx_busy (whole) [IOLINK_ISR_TX_MODE]
 *  - wake-up : detect_wakeup present (the PHY can observe the C/Q wake-up)
 * These keep the capability checks in one place instead of scattering NULL tests.
 */
static inline bool dll_phy_has_rx(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return (ctx->phy != NULL) && (ctx->phy->rx_frame != NULL);
#else
    return (ctx->phy != NULL) && (ctx->phy->rx_octet != NULL);
#endif
}

static inline bool dll_phy_has_tx(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    return (ctx->phy != NULL) && (ctx->phy->tx_frame != NULL);
#else
    return (ctx->phy != NULL) && (ctx->phy->tx_octet != NULL);
#endif
}

static inline bool dll_phy_has_wakeup(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_PHY_FEATURE_WAKEUP
    return (ctx->phy != NULL) && (ctx->phy->detect_wakeup != NULL);
#else
    (void) ctx;
    return false;
#endif
}

static inline int dll_phy_detect_wakeup(iolink_dll_ctx_t* ctx)
{
#if IOLINK_PHY_FEATURE_WAKEUP
    return ((ctx->phy != NULL) && (ctx->phy->detect_wakeup != NULL))
               ? ctx->phy->detect_wakeup(ctx->phy->user)
               : 0;
#else
    (void) ctx;
    return 0;
#endif
}

/**
 * @brief DLL statistics snapshot
 */
typedef struct
{
    uint32_t crc_errors;         /**< Cumulative CRC error count */
    uint32_t timeout_errors;     /**< Cumulative timeout count */
    uint32_t framing_errors;     /**< Cumulative framing error count */
    uint32_t timing_errors;      /**< Cumulative timing violations */
    uint32_t t_ren_violations;   /**< t_ren violations */
    uint32_t t_cycle_violations; /**< t_cycle violations */
    uint32_t t_byte_violations;  /**< Inter-byte timing violations */
    uint32_t t_pd_violations;    /**< t_pd violations */
    uint32_t total_retries;      /**< Cumulative retry count */
    uint32_t voltage_faults;     /**< Cumulative voltage fault count (edge per fault with
                                     get_faults(), poll cycle on the legacy path) */
    uint32_t overtemp_faults;    /**< Cumulative over-temperature fault count (edge per
                                     fault with get_faults(), 0 on the legacy path) */
    uint32_t short_circuits;     /**< Cumulative short circuit count (edge per fault with
                                     get_faults(), poll cycle on the legacy path) */
    uint32_t isr_tx_dropped;     /**< Replies the transport could not transmit (main and ISR) */
    uint32_t idle_timeouts;      /**< 1 s idle-link diagnostics (not a t_A timeout) */
} iolink_dll_stats_t;

/**
 * @brief Initialize DLL context
 *
 * Sets defaults for state, retries, and resets counters.
 *
 * @param ctx DLL context to initialize
 * @param phy PHY implementation to bind
 * @note The context is zeroed, including the clock. Direct DLL users must set
 *       @c ctx->time_us / @c ctx->time_user after this call;
 *       iolink_device_init() binds them from iolink_device_config_t::time_us.
 */
void iolink_dll_init(iolink_dll_ctx_t* ctx, const iolink_phy_api_t* phy);

/**
 * @brief Process DLL logic
 *
 * Handles byte-level processing and state machine transitions.
 *
 * @param ctx DLL context to process
 */
void iolink_dll_process(iolink_dll_ctx_t* ctx);

/**
 * @brief Expected length (octets) of the master request whose MC octet is @p mc.
 *
 * Pure, ISR-safe and read-only: derived from the DLL state / configured type and
 * the MC octet alone. The library ISR pump uses it to delimit the incoming
 * M-sequence; the main pump reuses it for the same frame.
 * @param ctx DLL context (read only)
 * @param mc  Master-command (first) octet of the request
 * @return Expected request length in octets (2 for a Type-0 message).
 */
uint8_t iolink_dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t mc);

/**
 * @brief Which reply layout the request @p mc requires.
 *
 * Pure, ISR-safe and read-only. The ISR pump selects the reply layout from it;
 * it must be kept consistent with the reply the main pump builds.
 * @param ctx DLL context (read only)
 * @param mc  Master-command (first) octet of the request
 * @return Reply variant to transmit.
 */
iolink_reply_variant_t iolink_dll_reply_variant(const iolink_dll_ctx_t* ctx, uint8_t mc);

/**
 * @brief ISR-side pump for a raw-octet (single-octet transceiver) PHY.
 *
 * Called from the PHY data-interrupt handler. It pulls received octets via
 * phy->rx_octet(), frames the M-sequence (using the pure
 * iolink_dll_expected_req_len()), publishes the complete request to the mailbox
 * consumed by iolink_dll_process(), selects the pre-built reply variant from the
 * request (iolink_dll_reply_variant()) and feeds the reply octets via
 * phy->tx_octet()/tx_done() (TX BYTE) or phy->tx_frame() (TX FRAME). It never
 * runs protocol logic (ISDU / params /
 * events / vendor); those stay in the main pump.
 *
 * Safe to call with no data pending; returns immediately when the INT line is
 * not asserted. No-op for a PHY without rx_octet().
 * @param ctx DLL context
 */
void iolink_dll_process_isr(iolink_dll_ctx_t* ctx);

/**
 * @brief Deliver the current DLL state to the state-change hook.
 *
 * Notification is level-based: when the current state differs from the last
 * one delivered, the hook is called once with the current state. The data ISR
 * only changes the state, so this function is the single place where the
 * application hook can run (never in interrupt context). iolink_dll_process()
 * calls it at the end; the device layer relies on that call, and a bare DLL
 * user may also call it from the main loop.
 *
 * Two states entered and left between two calls are coalesced into the later
 * one; the final state is never lost. A STARTUP transition taken in the data
 * ISR is retained as a pending notification and reported before the current
 * state, so a rapid STARTUP -> PREOPERATE sequence cannot hide the reset.
 * Safe to call when the state has not changed (it returns immediately).
 * @param ctx DLL context
 */
void iolink_dll_flush_state_callbacks(iolink_dll_ctx_t* ctx);

/** @brief Timer A (t_A one-shot) expired without a reply. */
void iolink_dll_on_rx_timeout(iolink_dll_ctx_t* ctx);

/** @brief Timer B tick: authorise one main-context service window. */
void iolink_dll_on_stack_tick(iolink_dll_ctx_t* ctx);

/** @brief Consume the Timer B authorisation (1 = user code may run). */
uint8_t iolink_dll_take_service_window(iolink_dll_ctx_t* ctx);

/**
 * @brief Set current PD lengths for variable types (1_V, 2_V)
 *
 * @param ctx DLL context
 * @param pd_in_len New PD_In length
 * @param pd_out_len New PD_Out length
 * @return int 0 on success, negative on range error
 */
int iolink_dll_set_pd_length(iolink_dll_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/**
 * @brief Get current PD lengths
 *
 * @param ctx DLL context
 * @param[out] pd_in_len Current PD_In length
 * @param[out] pd_out_len Current PD_Out length
 */
void iolink_dll_get_pd_length(const iolink_dll_ctx_t* ctx, uint8_t* pd_in_len, uint8_t* pd_out_len);

/**
 * @brief Request transition to SIO mode (single-wire communication)
 *
 * @param ctx DLL context
 * @return int 0 on success
 */
int iolink_dll_set_sio_mode(iolink_dll_ctx_t* ctx);

/**
 * @brief Request transition to SDCI mode (UART-based exchange)
 *
 * @param ctx DLL context
 * @return int 0 on success
 */
int iolink_dll_set_sdci_mode(iolink_dll_ctx_t* ctx);

/**
 * @brief Get current operating mode
 *
 * @param ctx DLL context
 * @return iolink_phy_mode_t Current mode
 */
iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t* ctx);

/**
 * @brief Request the Back-to-box sequence (10.7.5).
 *
 * The ISDU handler calls this after successfully answering the 0x83
 * SystemCommand. The DLL keeps serving frames for at least 3 MasterCycles,
 * then stops and disables communication (COMM_DISABLED) and the PHY output
 * driver until the next power-on.
 *
 * @param ctx DLL context
 */
void iolink_dll_request_back_to_box(iolink_dll_ctx_t* ctx);

/**
 * @brief Set the communication baudrate
 *
 * @param ctx DLL context
 * @param baudrate Desired baudrate (COM1, COM2, or COM3)
 * @return int 0 on success
 */
int iolink_dll_set_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate);

/** Sentinel for "no operating COM configured" (recovery keeps COM1). */
#define IOLINK_BAUDRATE_UNSET ((iolink_baudrate_t) 0xFF)

/**
 * @brief Set the device's operating COM and apply it now
 *
 * The operating COM is re-asserted by iolink_dll_process() after a recovery
 * drop to COM1 (idle/fallback), matching a device configured for a fixed SDCI
 * baudrate. Pass IOLINK_BAUDRATE_UNSET to stop re-asserting it.
 *
 * @param ctx DLL context
 * @param baudrate Operating baudrate (COM1..COM3)
 * @return int 0 on success, -1 on invalid arguments
 */
int iolink_dll_set_operating_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate);

/**
 * @brief Get current negotiated baudrate
 *
 * @param ctx DLL context
 * @return iolink_baudrate_t Current baudrate
 */
iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t* ctx);

/** Compute the device MinCycleTime (0.1 ms units) from the M-sequence timing. */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_ctx_t* ctx);

/**
 * @brief Get DLL statistics
 *
 * @param ctx DLL context
 * @param[out] out_stats Output stats structure, populated with a counter snapshot
 */
void iolink_dll_get_stats(const iolink_dll_ctx_t* ctx, iolink_dll_stats_t* out_stats);

/**
 * @brief Reset the Device diagnosis and status (Table 101)
 *
 * Clears the counters behind ErrorCount (0x0020) and discards the event queue,
 * the Event flag and the Table 58 event memory, so DeviceStatus (0x0024) and
 * DetailedDeviceStatus (0x0025) start from "0" again. Called for the Device
 * Device reset and Back-to-box. Restore factory settings withdraws the active
 * events instead (Table 101 "Clear"), so it uses
 * iolink_dll_reset_error_counters() together with
 * iolink_events_clear_diagnosis(). An Application reset leaves the diagnosis
 * untouched.
 *
 * @param ctx DLL context
 */
void iolink_dll_reset_diagnostics(iolink_dll_ctx_t* ctx);

/**
 * @brief Reset the ErrorCount counters without touching the event engine.
 *
 * B.2.20/Table 101: ErrorCount counts errors since power-on or reset. Used by
 * Restore factory settings, which resets the counters but withdraws the active
 * Events instead of discarding them.
 *
 * @param ctx DLL context
 */
void iolink_dll_reset_error_counters(iolink_dll_ctx_t* ctx);

/**
 * @brief Enable/disable timing enforcement (t_ren / t_cycle)
 *
 * @param ctx DLL context
 * @param enable true to enable, false to disable
 */
void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t* ctx, bool enable);

/**
 * @brief Override t_ren limit (applies to all baudrates)
 *
 * @param ctx DLL context
 * @param limit_us New t_ren limit in microseconds (0 disables enforcement)
 */
void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t* ctx, uint32_t limit_us);

/** @} */ /* end of iolinki_dll */

#endif  // IOLINK_DLL_H
