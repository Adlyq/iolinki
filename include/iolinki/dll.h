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
    IOLINK_DLL_STATE_FALLBACK = 5U       /**< Error recovery / fallback */
} iolink_dll_state_t;

/**
 * @brief Optional hook invoked when the DLL state machine changes state.
 *
 * Fired from within iolink_dll_process() at the actual transition, so no
 * transient state is missed. Used by the core to drive application lifecycle
 * callbacks.
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

/** Number of pre-staged OPERATE reply payload variants (see below). */
#define IOLINK_REPLY_VARIANT_COUNT 3U
/** Largest M-sequence the library frames (request or reply), in octets. */
#define IOLINK_M_SEQ_MAX_LEN (IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U)

/* PD reply send buffer: [OD][PD-in][CKS]. Sized for the runtime PD-in length
 * plus the OD header and the trailing checksum. */
#define IOLINK_PD_REPLY_MAX (IOLINK_OD_MAX_SIZE + IOLINK_PD_IN_MAX_SIZE + 1U)

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
    iolink_dll_state_t state;    /**< Current DLL state */
    const iolink_phy_api_t* phy; /**< Bound PHY API implementation */
    /* Optional application critical-section hooks. Used by dll_send() to start a
     * main-context (Type-0) transmission atomically with respect to the data
     * interrupt. The library does not know the platform's lock mechanism. */
    void (*lock)(void* user);
    void (*unlock)(void* user);
    void* lock_user;
    uint32_t last_activity_ms;   /**< Timestamp of last received byte */
    uint32_t last_valid_frame_ms;/**< Timestamp of the last checksum/type-valid frame */
    bool wakeup_seen;            /**< Wake-up detected (if PHY supports it) */

    /* Configuration */
    uint8_t m_seq_type;         /**< Supported M-sequence type */
    uint8_t pd_in_len;          /**< Input Process Data length */
    uint8_t pd_out_len;         /**< Output Process Data length */
    uint8_t od_len;             /**< Streaming-path OD length (1 or 2 bytes). */
    uint8_t od_width;           /**< OPERATE OD width m (Table A.10). Whether a frame
                                     carries OD is decided per frame from the MC R/W
                                     bit (Figure A.14): a WRITE request carries OD
                                     after PD-out, a READ reply carries OD before PD-in,
                                     and the opposite direction carries none. */
    bool cyclic_reply_precached;/**< Legacy flag (kept for source compatibility):
                                     replies are now generated directly from the
                                     shared state by the ISR pump. */
    /* ISR ISDU read view: the ISR reads ctx->isdu.resp_buf directly and owns the
     * FlowCTRL segment index; the main pump only refreshes the Busy/length view. */
    volatile uint8_t isr_isdu_len;  /**< Framed response length (0 = none) */
    volatile uint8_t isr_isdu_idx;  /**< Next response octet (ISR advances) */
    volatile uint8_t isr_isdu_busy; /**< 1 = service executing -> answer Busy 0x01 */
    volatile uint8_t isr_isdu_last_flowctrl; /**< Last accepted FlowCTRL (0xFF=none) */
    volatile uint8_t isr_isdu_seg_idx;       /**< Offset of the last served segment */

    /* Raw-octet ISR pump state (owned by the library, driven from the PHY data
     * interrupt). Only these fields are touched from ISR context; the reply
     * staging buffer + octet feed are owned by the library (the PHY only does
     * the SPI electrical access). */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    uint8_t isr_rx_buf[IOLINK_M_SEQ_MAX_LEN];    /**< RX octets accumulated */
    uint8_t isr_rx_cnt;                          /**< Octets accumulated this frame */
    uint8_t isr_rx_acc;                          /**< A.1.6 checksum accumulated per octet */
    uint8_t isr_rx_expect;                       /**< Expected length (0 = unknown) */
    uint8_t isr_frame_buf[IOLINK_M_SEQ_MAX_LEN]; /**< Mailbox storage (RX BYTE) */
#endif
    volatile const uint8_t* isr_frame_ptr;       /**< Mailbox: frame pointer
                                                      (RX BYTE: isr_frame_buf;
                                                       RX FRAME: PHY-owned buffer) */
    volatile uint8_t isr_frame_len;              /**< Mailbox length */
    volatile uint8_t isr_frame_ready;            /**< Mailbox ready flag */
    volatile uint8_t isr_eager_replied;          /**< 1 if the ISR already sent the
                                                      reply for the published frame;
                                                      the main pump must stay silent */
    volatile uint8_t frame_eager;                /**< Main-context snapshot of
                                                      isr_eager_replied for the frame
                                                      currently being processed
                                                      (avoids cross-frame races). */
    uint32_t isr_mailbox_drops;                  /**< Frames dropped because the
                                                      single mailbox slot was busy */
    uint32_t isr_eager_rejects;                  /**< Frames the eager path refused
                                                      (bad checksum/type) */
    volatile uint8_t isdu_isr_consumed;          /**< ISR already buffered this
                                                      ISDU write request */
    /* ISR-side ISDU staging + response snapshot (single-writer model). The ISR
     * stages write OD octets and reads only the response snapshot; the main
     * pump owns ctx->isdu and refreshes the snapshot under the short critical
     * section. This keeps iolink_isdu_process() out of the IRQ-off window. */
    uint8_t isr_od_stage_flowctrl;               /**< Staged write FlowCTRL */
    uint8_t isr_od_stage_len;                    /**< Staged write OD octets */
    uint8_t isr_od_stage[IOLINK_OD_MAX_SIZE];    /**< Staged write OD payload */
    volatile uint8_t isr_od_staged;              /**< 1 = a write is staged */
    uint32_t isr_od_overruns;                    /**< Staged writes overwritten */
    uint8_t isr_isdu_resp_buf[IOLINK_ISDU_BUFFER_SIZE]; /**< ISDU response snapshot */
    uint8_t isr_isdu_resp_valid;                 /**< Snapshot already copied for
                                                      the current response */
    volatile uint8_t stack_tick_pending;         /**< Timer B tick authorised a
                                                      main-context service window */
    volatile uint8_t in_isr;                     /**< ISR is running the pump */
    volatile uint8_t state_cb_pending;           /**< State callback deferred to main */
    iolink_dll_state_t pending_state;            /**< Deferred state value */
    /* Reply transmission, fed one octet per data request. Two payload slots
     * keep the in-flight TX-FRAME DMA buffer stable while the next reply is
     * prepared (the PHY serialises the transfers). */
    uint8_t isr_tx_payload[2][IOLINK_M_SEQ_MAX_LEN];
    const uint8_t* isr_tx_base;                  /**< Active reply source:
                                                      pd_in[active] for the long
                                                      PD reply, isr_tx_payload
                                                      for short/main replies */
    volatile uint8_t isr_tx_slot;                /**< Slot being transmitted */
    volatile uint8_t isr_tx_next;                /**< Next slot to fill */
    volatile uint8_t isr_tx_pending_slot;        /**< Slot of a staged main reply */
    volatile uint8_t isr_tx_len;                 /**< Payload octets held */
    volatile uint8_t isr_tx_idx;                 /**< Next octet index */
    volatile uint8_t isr_tx_flags;               /**< CKS status flags */
    volatile uint8_t isr_tx_acc;                 /**< Running checksum accumulator */
    volatile uint8_t isr_tx_has_cks;             /**< 1 = append computed CKS */
    volatile uint8_t isr_tx_active;              /**< 1 while draining */
    volatile uint8_t isr_tx_pending;             /**< 1 = a main-context handler
                                                      staged a verbatim reply;
                                                      the ISR must transmit it */
    volatile uint8_t isr_tx_dropped;             /**< Replies dropped because an
                                                      ISR-only PHY lacks
                                                      request_service() */
    bool pd_valid;              /**< Current PD_In validity status */
    uint8_t master_cycle_time;  /**< DPP1 0x01, the cycle-time octet written by the
                                     Master (Table B.1/B.3); 0 = not yet written */
    uint32_t min_cycle_time_us; /**< Minimum cycle time in microseconds */
    uint32_t min_cycle_time_app_us; /**< Application floor for the cycle time */
    bool enforce_timing;        /**< Enable timing checks (t_ren / t_cycle) */
    uint32_t t_ren_limit_us;    /**< Current t_ren limit in microseconds */
    bool t_ren_override;        /**< Use overridden t_ren limit if true */
    uint32_t t_pd_delay_us;     /**< Power-on delay (t_pd) in microseconds */

    /* Variable PD Support (for Type 1_V and 2_V) */
    uint8_t pd_in_len_current;  /**< Current runtime PD_In length */
    uint8_t pd_out_len_current; /**< Current runtime PD_Out length */
    uint8_t pd_in_len_max;      /**< Maximum allowed PD_In length */
    uint8_t pd_out_len_max;     /**< Maximum allowed PD_Out length */

    /* SIO Mode */
    iolink_phy_mode_t phy_mode; /**< Current operating mode (SDCI vs SIO) */

    /* Baudrate Support */
    iolink_baudrate_t baudrate; /**< Negotiated baudrate (COM1-COM3) */
    iolink_baudrate_t operating_baudrate; /**< Operating COM; IOLINK_BAUDRATE_UNSET if not configured */

    /* Unified Frame Assembly: a master request is MC + CKT + PD-out + OD, so
       size it from the compile-time maxima (was a fixed 48, too small once the
       OD width may reach IOLINK_OD_MAX_SIZE). */
    uint8_t frame_buf[IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U];
    uint8_t frame_index;           /**< Current byte index in assembly */
    uint8_t req_len;               /**< Expected length of current frame type */
    uint64_t last_frame_us;        /**< Microsecond timestamp of last frame start */
    uint64_t last_byte_us;         /**< Microsecond timestamp of last received byte */
    uint64_t last_cycle_start_us;  /**< Microsecond timestamp of last cycle start */
    uint32_t t_byte_limit_us;      /**< Inter-byte timeout limit in microseconds */
    uint64_t wakeup_deadline_us;   /**< Earliest time to accept frames after wake-up */
    uint64_t t_pd_deadline_us;     /**< Earliest time to accept frames after power-on */
    uint64_t dsio_deadline_ms;     /**< Deadline to fall back to SIO without a valid message */
    uint64_t fallback_deadline_ms; /**< Deadline to complete a FALLBACK MasterCommand */

    /* Process Data Buffers. The PD-in buffer doubles as the reply send buffer:
     * layout [OD][PD-in][CKS]. The main fills the inactive copy and swaps
     * atomically; the ISR streams the active copy (zero-copy). */
    uint8_t pd_in[2][IOLINK_PD_REPLY_MAX];  /**< [OD][PD-in][CKS] reply buffers */
    volatile uint8_t pd_in_active;          /**< Active PD reply buffer index */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /**< Output PD buffer (Master -> Device) */

    /* Error Counters & Statistics */
    uint32_t crc_errors;            /**< Cumulative CRC error count */
    uint32_t timeout_errors;        /**< Cumulative timeout count */
    uint32_t framing_errors;        /**< Cumulative framing error count */
    uint32_t timing_errors;         /**< Cumulative timing violations */
    uint32_t t_ren_violations;      /**< t_ren violations */
    uint32_t t_cycle_violations;    /**< t_cycle violations */
    uint32_t t_byte_violations;     /**< Inter-byte timing violations */
    uint32_t t_pd_violations;       /**< t_pd violations */
    uint8_t retry_count;            /**< Retry counter for current exchange */
    uint32_t total_retries;         /**< Cumulative retry count */
    uint8_t max_retries;            /**< Configured max retries (default 3) */
    uint32_t voltage_faults;        /**< Cumulative voltage fault count */
    uint32_t overtemp_faults;       /**< Cumulative over-temperature fault count */
    uint32_t short_circuits;        /**< Cumulative short circuit count */
    uint8_t fallback_count;         /**< Consecutive fallback count for SIO transition */
    uint8_t sio_fallback_threshold; /**< Fallback threshold to enter SIO mode (default 3) */

    /* Timing Statistics */
    uint64_t last_response_us; /**< Microsecond timestamp of last response */
    uint32_t response_time_us; /**< Measured stack response time (t2) */

    /* Sub-modules */
#if IOLINK_FEATURE_EVENTS
    iolink_events_ctx_t events; /**< Diagnostic Events engine */
#endif
    iolink_isdu_ctx_t isdu;     /**< ISDU Service engine */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t ds;         /**< Data Storage engine */
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
 *  - TX      : tx_octet + tx_done (byte) or tx_frame (whole)  [IOLINK_ISR_TX_MODE]
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
    uint32_t voltage_faults;     /**< Cumulative voltage fault count */
    uint32_t overtemp_faults;    /**< Cumulative over-temperature fault count */
    uint32_t short_circuits;     /**< Cumulative short circuit count */
} iolink_dll_stats_t;

/**
 * @brief Initialize DLL context
 *
 * Sets defaults for state, retries, and resets counters.
 *
 * @param ctx DLL context to initialize
 * @param phy PHY implementation to bind
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
 * Pure, ISR-safe and read-only. The ISR pump selects the matching pre-staged
 * reply variant from it; it must be kept consistent with the reply the main pump
 * builds in iolink_dll_prepare_cyclic_reply().
 * @param ctx DLL context (read only)
 * @param mc  Master-command (first) octet of the request
 * @return Reply variant to transmit.
 */
iolink_reply_variant_t iolink_dll_reply_variant(const iolink_dll_ctx_t* ctx, uint8_t mc);

/**
 * @brief Build and pre-stage the deterministic cyclic reply variants.
 *
 * Only meaningful when cyclic_reply_precached is set (raw-octet PHY): builds
 * both direction variants from the current PD-in, validity and event state into
 * the inactive slot of each double buffer (read: [OD...][PD-in...]CKS, Figure
 * A.9/A.14; write: [PD-in...]CKS, Figure A.14) and swaps the active index
 * atomically. The ISR pump selects the matching one at frame end. Call it
 * whenever the PD-in image changes (e.g. once per main-loop cycle).
 * @param ctx DLL context
 */
void iolink_dll_prepare_cyclic_reply(iolink_dll_ctx_t* ctx);

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

/** @brief Timer A (t_A one-shot) expired without a reply. */
void iolink_dll_on_rx_timeout(iolink_dll_ctx_t* ctx);

/** @brief Timer B tick: authorise one main-context service window. */
void iolink_dll_on_stack_tick(iolink_dll_ctx_t* ctx);

/** @brief Consume the Timer B authorisation (1 = user code may run). */
uint8_t iolink_dll_can_run_user_code(iolink_dll_ctx_t* ctx);

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
