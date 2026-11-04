/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include "iolinki/platform.h"
#include "dll_internal.h"
#include <string.h>

#include <stdio.h>

/**
 * @file dll.c
 * @brief Data Link Layer (DLL): M-sequence state machine and framing.
 * @ingroup iolinki_dll
 *
 * Drives the IO-Link device DLL: wake-up detection, mode/baudrate management,
 * per-frame reception and CRC validation, the STARTUP -> PREOPERATE ->
 * OPERATE state machine, timing enforcement, fallback handling, and dispatch
 * of Type-0/Type-1/Type-2 M-sequences into the ISDU engine.
 */

/* -------------------------------------------------------------------------
 * File map
 *
 *  - State transitions and callbacks: dll_queue_state ... dll_set_state
 *  - M-sequence typing and transmission: dll_mseq_type_bits ... dll_send
 *  - Timing, master commands and operate handlers: dll_get_t_ren_limit_us ...
 *    dll_handle_operate_type1_2
 *  - Diagnostics polling: dll_poll_diagnostics
 *  - Lifecycle and ISDU snapshot: iolink_dll_init ...
 *  - Main pump: dll_frame_drop_if_stopped ... iolink_dll_process
 *  - Public configuration and query API: iolink_dll_get_state ...
 *
 * The ISR two-pump transport lives in dll_isr.c; see docs/CODE_FLOW.md for the
 * main/ISR context contract.
 * ------------------------------------------------------------------------- */

#define DLL_LOG(...)

/* -------------------------------------------------------------------------
 * State transitions and callbacks
 * ------------------------------------------------------------------------- */

/**
 * @brief Centralised state transition.
 *
 * Updates the protocol state immediately. When the transition happens in main
 * context the application hook is notified right away (level check, no queue);
 * a transition taken in the data ISR only sets the state and is delivered by
 * the next main-context flush. Two ISR transitions before that flush are
 * coalesced into the later state.
 */
void dll_set_state(iolink_dll_ctx_t* ctx, iolink_dll_state_t new_state)
{
    if (ctx->state == new_state) {
        return;
    }
    /* Single aligned store: the data ISR may read the state at any time, and
     * both contexts may transition it (the ISR fast path does so for t_A).
     * The state-dependent bookkeeping below is main-owned/hint-only. */
    ctx->state = new_state;
    if (new_state == IOLINK_DLL_STATE_OPERATE) {
        /* Master-loss supervision is meaningful only inside OPERATE; the
         * startup sequence may legitimately pause for several cycles. */
        ctx->master_last_activity_ms = dll_time_ms(ctx);
        ctx->master_lost_count = 0U;
        ctx->master_lost = false;
    }
    if (new_state == IOLINK_DLL_STATE_STARTUP) {
        /* A.6.4: queued events are discarded when communication is
           interrupted or cancelled. iolink_events_reset() takes its own short
           critical section, so no outer one is needed. */
#if IOLINK_FEATURE_EVENTS
        iolink_events_reset(&ctx->events);
#endif
    }
    if (ctx->in_isr == 0U) {
        /* Main context: deliver now, so a transition sequence that happens
         * inside one frame dispatch is still reported state by state. */
        iolink_dll_flush_state_callbacks(ctx);
    }
}

void iolink_dll_flush_state_callbacks(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->state_cb == NULL)) {
        return;
    }
    /* Level-based notification: deliver the current state once. Never called
     * from interrupt context, so the application hook stays out of the ISR. */
    const iolink_dll_state_t state = ctx->state;
    if (state == ctx->last_notified_state) {
        return;
    }
    ctx->last_notified_state = state;
    ctx->state_cb(ctx->state_cb_user, state);
}

/* -------------------------------------------------------------------------
 * M-sequence typing and transmission
 * ------------------------------------------------------------------------- */

/** @brief Return the CKT M-sequence type bits (A.1.3, Table A.3) for a configured type. */
static uint8_t dll_mseq_type_bits(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            return IOLINK_MSEQ_TYPE_1;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_3:
        case IOLINK_M_SEQ_TYPE_2_4:
        case IOLINK_M_SEQ_TYPE_2_5:
        case IOLINK_M_SEQ_TYPE_2_V:
            return IOLINK_MSEQ_TYPE_2;
        default:
            return IOLINK_MSEQ_TYPE_0;
    }
}

/**
 * @brief Is this request's CKT type legal in the current DLL state?
 *
 * STARTUP/PREOPERATE use Type-0. OPERATE/ESTAB_COM use the configured
 * M-sequence type, with one exception: ST/Table 47 allow a Type-0 Direct
 * Parameter page access (the startup probe, MasterCommand page writes such as
 * DevicePreoperate 0x9A/DeviceOperate 0x99/PDOperate 0x98, and DPP parameter
 * writes) while the link is already in OPERATE. The master uses those
 * periodically, so treating them as an illegal type would flap the link down
 * to STARTUP every time the master re-sends its page-write sequence.
 */
bool dll_type_allowed(const iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    const uint8_t rx_type = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    const uint8_t ch = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    uint8_t exp_type = IOLINK_MSEQ_TYPE_0;

    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                                                     (frame[0] != IOLINK_MC_TRANSITION_COMMAND))) {
        if ((ch == IOLINK_MC_CHANNEL_PAGE) && (rx_type == IOLINK_MSEQ_TYPE_0)) {
            return true;
        }
        exp_type = dll_mseq_type_bits(ctx->m_seq_type);
    }
    return rx_type == exp_type;
}

/**
 * @brief Return the CKT type bits the device expects for the current state.
 *
 * In STARTUP and PREOPERATE every message is Type 0 (Table A.3, CKT bits 00);
 * in OPERATE the configured M-sequence type is expected.
 */
static uint8_t dll_expected_type_bits(const iolink_dll_ctx_t* ctx)
{
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || (ctx->state == IOLINK_DLL_STATE_ESTAB_COM)) {
        return dll_mseq_type_bits(ctx->m_seq_type);
    }
    return IOLINK_MSEQ_TYPE_0;
}

/**
 * @brief Return the expected request length for the given M-sequence type bits.
 *
 * The request is MC + CKT + PD-out width + OD width; the checksum is carried in
 * the CKT octet (A.1.6), so there is no trailing checksum octet. A Type-0
 * message carries no PD and a single OD octet written only on a write access.
 */

static void dll_isdu_sync_snapshot(iolink_dll_ctx_t* ctx);

/**
 * @brief Emit a device reply through the ISR transport.
 *
 * Hand the octets to the raw-octet/frame TX hooks; the ISR feeds them (TX BYTE)
 * or the PHY transmits the whole frame (TX FRAME). The reply octets (including
 * the CKS) are built by the shared handlers, so the frame layout is the same
 * either way.
 *
 * The stack has a single main-context caller, so staging needs no lock: the
 * main pump owns slots 0/1; the data ISR owns its own slot. The ISR reads the
 * staged reply only after isr_tx_pending is published (data-first ordering plus
 * a compiler barrier). Only the legacy direct-send fallback drives the PHY from
 * this context and therefore excludes the data interrupt.
 */
static void dll_send(iolink_dll_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (data == NULL) || (len == 0U)) {
        return;
    }
    if (len > sizeof(ctx->isr_tx_payload[0])) {
        return;
    }
    /* Main-context slots only: the ISR has its own slot, so this counter has a
     * single writer and needs no read-modify-write atomic. */
    const uint8_t slot = ctx->isr_tx_next;
    ctx->isr_tx_next = (uint8_t) (ctx->isr_tx_next ^ 1U);
    (void) memcpy(ctx->isr_tx_payload[slot], data, len);
    ctx->isr_tx_pending_slot = slot;
    ctx->isr_tx_len = (uint8_t) len;
    ctx->isr_tx_has_cks = 0U;
    IOLINK_FENCE();           /* staged octets before the publish */
    ctx->isr_tx_pending = 1U; /* publish last */

    if (ctx->phy->request_service != NULL) {
        /* Poke the PHY after the publish and outside any interrupt-off window:
         * the hook is port code (it may do an SPI access or take a lock). A
         * natural data interrupt that runs in between simply drains the staged
         * reply, and the extra service request then finds nothing. */
        ctx->phy->request_service(ctx->phy->user);
        return;
    }

#if !IOLINK_PHY_FEATURE_ISR_TX_ONLY
    /* Legacy fallback for PHYs without a service hook (streaming/host tests).
     * It drives the TX hooks from this context, so the data interrupt must not
     * interleave with the transfer. */
    IOLINK_CRITICAL_ENTER();
    ctx->isr_tx_pending = 0U;
    dll_isr_start_staged_tx(ctx, slot, (uint8_t) len);
    IOLINK_CRITICAL_EXIT();
#else
    /* ISR-only PHY without request_service(): the reply cannot be transmitted.
     * Count it so a misconfiguration is observable instead of silent. */
    ctx->isr_tx_pending = 0U;
    ctx->isr_tx_dropped++;
#endif /* !IOLINK_PHY_FEATURE_ISR_TX_ONLY */
}

/* -------------------------------------------------------------------------
 * Timing, master commands and operate handlers
 * ------------------------------------------------------------------------- */

/** @brief Return the response-time (t_REN) limit in us (C6/Table 10: 500 us). */
static uint32_t dll_get_t_ren_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    if (ctx->t_ren_override) {
        return ctx->t_ren_limit_us;
    }
    /* Table 10: a single T_REN value at most 500 us, independent of baudrate. */
    return IOLINK_T_REN_US;
}

/** @brief Return the active baudrate in Hz. */
static uint32_t dll_get_baud_hz(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 38400U;
    }
    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            return 4800U;
        case IOLINK_BAUDRATE_COM2:
            return 38400U;
        case IOLINK_BAUDRATE_COM3:
            return 230400U;
        default:
            return 38400U;
    }
}

/** @brief Return the inter-byte timeout (16 bit-times) in us for the active baudrate. */
static uint32_t dll_get_t_byte_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    uint32_t t_bit_us;
    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            t_bit_us = 208U;
            break;
        case IOLINK_BAUDRATE_COM2:
            t_bit_us = 26U;
            break;
        case IOLINK_BAUDRATE_COM3:
            t_bit_us = 4U;
            break;
        default:
            t_bit_us = 26U;
            break;
    }
    return t_bit_us * 16U;
}

/** @brief Return true while the power-down/PD guard window (t_PD) is still active. */
static bool dll_t_pd_active(const iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->t_pd_deadline_us == 0U)) {
        return false;
    }
    return dll_time_us(ctx) < ctx->t_pd_deadline_us;
}

/** @brief Drain all pending RX bytes from the PHY; returns true if any were seen. */
static bool dll_drain_rx(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return false;
    }
    /* The ISR pump fills the mailbox; draining it discards a frame that arrived
     * while the t_pd guard window was active. */
    if (!iolink_ring_is_empty(&ctx->isr_frame_ring)) {
        while (iolink_ring_peek(&ctx->isr_frame_ring) != NULL) {
            iolink_ring_release(&ctx->isr_frame_ring);
        }
        return true;
    }
    return false;
}

/** @brief ST rc=3: a protocol error drops the frame and demotes to STARTUP.
 *
 * The ST 6362 library only returns to SIO for the Fallback MasterCommand
 * (T8/T9) or a T_DSIO wake-up timeout (T10). Framing/CRC errors never leave
 * the current mode on their own. */
static void dll_protocol_error_to_startup(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->fallback_count = 0U;
    ctx->total_retries++;
    ctx->frame_index = 0U;
    if (ctx->state != IOLINK_DLL_STATE_STARTUP) {
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    }
}

/** @brief True for a page-channel Type-0 WRITE of MasterCommand FALLBACK (Table B.2). */
static bool dll_is_fallback_command(const iolink_dll_ctx_t* ctx)
{
    uint8_t mc = ctx->frame_buf[0];
    return ((mc & IOLINK_MC_RW_MASK) == 0U) &&
           ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
           ((mc & IOLINK_MC_ADDR_MASK) == 0x00U) && (ctx->frame_buf[2] == IOLINK_CMD_FALLBACK);
}

/**
 * @brief Handle a FALLBACK MasterCommand (0x5A, Table B.2) from the master.
 *
 * The device shall switch to SIO after 3 MasterCycleTimes and within at most
 * 500 ms (T_FBD, Table 43). The deadline is armed here and enforced in
 * iolink_dll_process(); the reply is a Type-0 write CKS-only response. */
static void dll_handle_fallback_command(iolink_dll_ctx_t* ctx)
{
    /* Without a wake-up detector the PHY cannot leave SIO again, so the
     * FALLBACK transition is not supported: ack the Type-0 write without
     * changing the DL mode. */
    if (dll_phy_has_wakeup(ctx)) {
        uint32_t t_fbd_ms = 500U;
        if (ctx->min_cycle_time_us > 0U) {
            t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
            if (t_fbd_ms > 500U) {
                t_fbd_ms = 500U;
            }
        }
        ctx->fallback_deadline_ms = dll_time_us(ctx) / 1000U + t_fbd_ms;
        dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
    }

    uint8_t resp[1];
    resp[0] = 0x00U;
    resp[0] = iolink_checksum6(resp, 1U);
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
}

/**
 * @brief Apply a MasterCommand (Table B.2) to the DL state, ST-style.
 *
 * Returns false for FALLBACK (0x5A), which needs the T_FBD deadline and is
 * handled by dll_handle_fallback_command(). Every other command is acked with
 * a CKS-only reply by the caller.
 */
bool dll_apply_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_FALLBACK:
            return false;
        case IOLINK_CMD_DEVICE_STARTUP:
            dll_protocol_error_to_startup(ctx);
            return true;
        case IOLINK_CMD_DEVICE_PREOPERATE:
            ctx->fallback_clear_pending = 1U;
            dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
            return true;
        case IOLINK_CMD_PROCESS_DATA_OPERATE:
            ctx->fallback_clear_pending = 1U;
            ctx->pd_valid = true;
            dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
            return true;
        case IOLINK_CMD_DEVICE_OPERATE:
            ctx->fallback_clear_pending = 1U;
            ctx->pd_valid = false;
            /* DeviceOperate from PREOPERATE establishes communication
               (ESTAB_COM); the following PDOperate 0x98 moves to OPERATE.
               In OPERATE/ESTAB it is just an idempotent acknowledgement. */
            if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
                dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
            }
            else {
                dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
            }
            return true;
        case IOLINK_CMD_MASTER_IDENT:
        case IOLINK_CMD_DEVICE_IDENT:
        default:
            return true;
    }
}

/** @brief Main-context MasterCommand page write: apply the state and send CKS. */
/* CKS PD-status bit: bit 6 is 0 for "Process Data valid".
 * A Device with no input PD cannot have invalid input PD, so it always reports
 * valid; otherwise report the current pd_valid state. */
uint8_t dll_pd_status_flags(const iolink_dll_ctx_t* ctx)
{
    if ((ctx->pd_in_len_current == 0U) && (ctx->pd_out_len_current > 0U)) {
        return 0U;
    }
    return ctx->pd_valid ? 0U : 0x40U;
}

static void dll_handle_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd)
{
    if (cmd == IOLINK_CMD_FALLBACK) {
        dll_handle_fallback_command(ctx);
        return;
    }

    (void) dll_apply_master_command(ctx, cmd);

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
}

/** @brief Handle a PREOPERATE master command: advance to ESTAB_COM on the transition command. */
static void dll_handle_preoperate(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t ck)
{
    (void) ck;
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->fallback_count = 0U;
        /* No response to transition command per spec */
    }
}

/**
 * @brief Answer a startup Type-0 read on the page communication channel.
 *
 * Spec startup transition T1: the master reads a Direct Parameter page octet
 * (address in the MC address field, e.g. 0x02 = MinCycleTime). Reply with a
 * 2-octet Type-0 frame carrying that octet, rather than feeding the MC into the
 * ISDU engine.
 */
static void dll_handle_page_channel_read(iolink_dll_ctx_t* ctx, uint8_t mc)
{
    uint8_t resp[2];
    resp[0] =
        iolink_isdu_direct_param_page1_octet(&ctx->isdu, (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
    resp[1] = 0x00U;
    resp[1] = iolink_checksum6(resp, 2U);
    dll_send(ctx, resp, 2);
    ctx->last_response_us = dll_time_us(ctx);
}

/**
 * @brief Dispatch one OD message by its communication channel (C2, 7.3.5.3).
 *
 * Diagnosis channel: event memory (Table 58). ISDU channel: the ISDU
 * transport. The page and process channels are handled by the callers (page by
 * the startup probe and the legacy ISDU path until Task 4). Returns true when
 * the channel was handled here.
 */
static bool dll_dispatch_od(iolink_dll_ctx_t* ctx, uint8_t mc, const uint8_t* od_in, uint8_t od_len,
                            uint8_t* od_out)
{
    const uint8_t channel = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);

    if (channel == IOLINK_MC_CHANNEL_PAGE) {
        /* 7.3.5.3: Direct Parameter page read/write. A read returns the page
           octet at the address; unimplemented addresses return 0, writes are
           ignored (A.1.2). MasterCommand at address 0 is handled by the
           PREOPERATE branch. */
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x01U)) {
            ctx->master_cycle_time = od_in[0];
        }
        if (((mc & IOLINK_MC_RW_MASK) == 0U) && (od_len > 0U) &&
            ((mc & IOLINK_MC_ADDR_MASK) == 0x0FU)) {
            /* 7.3.5.3: SystemCommand via Direct Parameter page 1 (no response). */
            iolink_isdu_execute_system_command(&ctx->isdu, od_in[0]);
        }
        if (((mc & IOLINK_MC_RW_MASK) != 0U) && (od_len > 0U)) {
            od_out[0] = iolink_isdu_direct_param_page1_octet(&ctx->isdu,
                                                             (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
        }
        return true;
    }

    if (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) {
        /* 7.3.8 Table 58: event memory is served by the diagnosis channel.
           A read returns the memory octet(s) at the address; a write to
           address 0 confirms the event readout and clears the Event flag. */
        const uint8_t addr = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
#if !IOLINK_FEATURE_EVENTS
        (void) addr;
#endif
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            for (uint8_t i = 0U; i < od_len; i++) {
                od_out[i] = DLL_EVENTS_MEMORY_READ(&ctx->events, (uint8_t) (addr + i));
            }
        }
        else if (od_len > 0U) {
            DLL_EVENTS_MEMORY_WRITE(&ctx->events, addr, od_in[0]);
        }
        return true;
    }

    if (channel == IOLINK_MC_CHANNEL_ISDU) {
        /* C3/Table 52: FlowCTRL lives in the MC address bits. A write carries
           request octets; a read fetches the framed response (or Busy). The ISR
           stages every valid ISDU write and the main pump drains it at the top
           of iolink_dll_process(), so a write is never applied twice here. */
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            const uint8_t flowctrl = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
            iolink_isdu_od_read(&ctx->isdu, flowctrl, od_out, od_len);
        }
        return true;
    }

    (void) od_in;
    (void) ctx;
    return false;
}

/** @brief Process a 2-octet Type-0 request through ISDU and send the OD response. */
static void dll_handle_operate_type0(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t cks)
{
    (void) cks;
    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &cks, 1U, &od_resp);

    uint8_t resp[2];
    uint8_t ck_flags = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        ck_flags |= 0x80U;
    }
    ck_flags |= dll_pd_status_flags(ctx);

    /* Figure A.5: a Type-0 WRITE is answered by the CKS only, on every
       communication channel; every Type-0 READ is answered by OD followed by
       the CKS. */
    const bool type0_write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    if (type0_write) {
        resp[0] = ck_flags;
        resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
        dll_send(ctx, resp, 1);
    }
    else {
        resp[0] = od_resp;
        resp[1] = ck_flags;
        resp[1] = (uint8_t) (resp[1] | iolink_checksum6(resp, 2U));
        dll_send(ctx, resp, 2);
    }
    ctx->last_response_us = dll_time_us(ctx);
}

/** @brief Process a 3-octet Type-0 OD WRITE (MC, CKT, OD) and reply with CKS only (Figure A.5). */
static void dll_handle_type0_od_write(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t od_in)
{
    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &od_in, 1U, &od_resp);

    uint8_t resp[1];
    resp[0] = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        resp[0] |= 0x80U;
    }
    resp[0] |= dll_pd_status_flags(ctx);
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = dll_time_us(ctx);
}

/** @brief Process a Type-1/Type-2 OPERATE frame (PD+OD) and build the response, enforcing t_REN. */
/**
 * @brief Handle an eager ISDU read that only needs the transport reset.
 *
 * An OPERATE ISDU read is answered by the ISR from the response snapshot; the
 * main pump must not advance the ISDU segment index again. Only an IDLE/ABORT
 * read needs the transport reset (and the snapshot refresh).
 *
 * @return true when the frame was fully handled.
 */
static bool dll_handle_eager_isdu_read(iolink_dll_ctx_t* ctx, bool write)
{
    if (!ctx->frame_eager ||
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) != IOLINK_MC_CHANNEL_ISDU) || write) {
        return false;
    }
    const uint8_t flowctrl = (uint8_t) (ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK);
    if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2) ||
        (flowctrl == IOLINK_FLOWCTRL_ABORT)) {
        uint8_t dummy = 0U;
        iolink_isdu_od_read(&ctx->isdu, flowctrl, &dummy, 0U); /* reset only */
        dll_isdu_sync_snapshot(ctx);
    }
    ctx->frame_eager = 0U;
    ctx->last_response_us = dll_time_us(ctx);
    return true;
}

/** @brief Dispatch the request OD; zero the reply OD when the channel refuses it. */
static void dll_dispatch_od_reply(iolink_dll_ctx_t* ctx, bool write, uint16_t od_offset,
                                  uint8_t* od_out)
{
    const uint8_t od_w = ctx->od_width; /* 1/2/8/32 from the opt-in patch */
    const uint8_t req_od = write ? od_w : 0U;

    uint8_t od_in[IOLINK_OD_MAX_SIZE];
    (void) memset(od_in, 0, sizeof(od_in));
    (void) memset(od_out, 0, IOLINK_OD_MAX_SIZE);
    if (req_od > 0U) {
        (void) memcpy(od_in, &ctx->frame_buf[od_offset], req_od);
    }

    const uint8_t dispatch_od = write ? req_od : od_w;
    if (!dll_dispatch_od(ctx, ctx->frame_buf[0], od_in, dispatch_od, od_out)) {
        for (uint16_t i = 0; i < dispatch_od; i++) {
            od_out[i] = 0U;
        }
    }
}

/** @brief Build and transmit the [OD][PD-in]CKS reply for a Type-1/2 frame. */
static void dll_send_od_reply(iolink_dll_ctx_t* ctx, bool write, const uint8_t* od_out)
{
    const uint8_t resp_od = write ? 0U : ctx->od_width;

    /* Figures A.9/A.10/A.13/A.14: the reply to a READ M-sequence is
       [OD octets][PD-in octets] CKS; the reply to a WRITE M-sequence carries no
       OD and is [PD-in octets] CKS. CKS carries the Event flag (bit 7), the
       PD-validity flag (bit 6, 1 = invalid) and the 6-bit message checksum
       (bits 0-5); there is no leading status octet (A.1.5). */
    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + IOLINK_OD_MAX_SIZE + 1];
    uint16_t pos = 0U;
    if (!write && (resp_od > 0U)) {
        (void) memcpy(&resp[pos], od_out, resp_od);
        pos += resp_od;
    }
    if (ctx->pd_in_len > 0U) {
        (void) memcpy(&resp[pos], &ctx->pd_in[ctx->pd_in_active][ctx->od_width], ctx->pd_in_len);
        pos += ctx->pd_in_len;
    }

    uint8_t cks = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        cks |= 0x80U;
    }
    cks |= dll_pd_status_flags(ctx);
    resp[pos] = cks;
    resp[pos] = (uint8_t) (resp[pos] | iolink_checksum6(resp, pos + 1U));
    pos++;

    if (dll_phy_has_tx(ctx)) {
        dll_send(ctx, resp, pos);
        ctx->fallback_count = 0U;
        const uint32_t end_tx_us = (uint32_t) dll_time_us(ctx);
        ctx->response_time_us = end_tx_us - (uint32_t) ctx->last_cycle_start_us;

        if (ctx->enforce_timing) {
            const uint32_t limit = dll_get_t_ren_limit_us(ctx);
            if ((limit > 0U) && (ctx->response_time_us > limit)) {
                ctx->timing_errors++;
                ctx->t_ren_violations++;
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                  IOLINK_EVENT_TYPE_WARNING);
            }
        }
    }
    ctx->last_response_us = dll_time_us(ctx);
}

static void dll_handle_operate_type1_2(iolink_dll_ctx_t* ctx)
{
    /* Type-1/2 master message: MC | CKT | PD | OD; the checksum is in CKT. */
    const uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    const uint16_t od_offset = (uint16_t) (pd_offset + ctx->pd_out_len_current);

    if (ctx->pd_out_len_current > 0U) {
        (void) memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->pd_out_len_current);
        ctx->pd_out_seen = true; /* B.2.24: last PDout written to the application */
    }

    const bool write = ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U);
    if (dll_handle_eager_isdu_read(ctx, write)) {
        return;
    }

    uint8_t od_out[IOLINK_OD_MAX_SIZE];
    dll_dispatch_od_reply(ctx, write, od_offset, od_out);

    if (ctx->frame_eager &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PROCESS)) {
        /* The ISR pump already transmitted the pre-staged cyclic reply for the
         * process channel at frame end. */
        ctx->fallback_count = 0U;
        ctx->last_response_us = dll_time_us(ctx);
        ctx->frame_eager = 0U;
        return;
    }
    if (ctx->frame_eager != 0U) {
        /* The ISR already acknowledged this ISDU write; do not send a second
         * reply. ISDU reads are handled before dll_dispatch_od(). */
        ctx->frame_eager = 0U;
        ctx->last_response_us = dll_time_us(ctx);
        return;
    }

    dll_send_od_reply(ctx, write, od_out);
}

/* -------------------------------------------------------------------------
 * Diagnostics polling
 * ------------------------------------------------------------------------- */

/** @brief Poll PHY voltage and short-circuit status and raise events on faults. */
static void dll_poll_diagnostics(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

#if IOLINK_PHY_FEATURE_LEGACY_DIAG
    if (ctx->phy->get_voltage_mv != NULL) {
        int voltage = ctx->phy->get_voltage_mv(ctx->phy->user);
        /* A negative reading means "measurement unavailable", not a fault
           (phy_generic's template hook returns -1). 18 V to 30 V inclusive is
           the valid range; Table D.1 codes 0x5111/0x5110 report under/overrun. */
        if (voltage >= 0) {
            if (voltage < 18000) {
                ctx->voltage_faults++;
                /* Table D.1 device code 0x5111 "Primary supply voltage underrun". */
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN,
                                  IOLINK_EVENT_TYPE_WARNING);
            }
            else if (voltage > 30000) {
                ctx->voltage_faults++;
                /* Table D.1 device code 0x5110 "Primary supply voltage overrun". */
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN,
                                  IOLINK_EVENT_TYPE_WARNING);
            }
        }
    }

    if (ctx->phy->is_short_circuit != NULL) {
        if (ctx->phy->is_short_circuit(ctx->phy->user)) {
            ctx->short_circuits++;
            /* Table D.1 device code 0x7710 "Short circuit". */
            DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENTCODE_SHORT_CIRCUIT,
                              IOLINK_EVENT_TYPE_ERROR);
        }
    }
#endif /* IOLINK_PHY_FEATURE_LEGACY_DIAG */
}

/* -------------------------------------------------------------------------
 * Lifecycle and ISDU snapshot
 * ------------------------------------------------------------------------- */

void iolink_dll_init(iolink_dll_ctx_t* ctx, const iolink_phy_api_t* phy)
{
    if ((phy == NULL) || (!iolink_ctx_zero(ctx, sizeof(iolink_dll_ctx_t)))) {
        return;
    }
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->last_notified_state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    ctx->enforce_timing = (IOLINK_TIMING_ENFORCE_DEFAULT != 0U);
    ctx->sio_fallback_threshold = 3U;
    ctx->operating_baudrate = IOLINK_BAUDRATE_UNSET;
    ctx->master_last_activity_ms = dll_time_ms(ctx);
    ctx->master_lost_cycles = 0U;
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;

    /* Table A.10: TYPE_2_1/TYPE_2_2 carry one On-request Data octet; TYPE_2_V is
       the two-octet variant (OPERATE M-sequence code 5). */
    ctx->od_len = (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;
    ctx->isr_isdu_last_flowctrl = 0xFFU; /* no START seen yet */

    ctx->pd_in_len_current = ctx->pd_in_len;
    ctx->pd_out_len_current = ctx->pd_out_len;
    ctx->pd_in_len_max = ctx->pd_in_len;
    ctx->pd_out_len_max = ctx->pd_out_len;
    ctx->pd_valid = true;
    ctx->pd_in_captured = 0xFFU; /* no reply in flight */

    /* Frame mailbox: the ISR publishes into the ring, the main pump consumes. */
    iolink_ring_init(&ctx->isr_frame_ring, (uint8_t*) ctx->isr_frame_slot, IOLINK_MBX_SLOTS,
                     (uint8_t) sizeof(iolink_frame_rec_t));
    /* Staged ISDU write segments (ISR -> main). */
    iolink_ring_init(&ctx->isr_od_ring, (uint8_t*) ctx->isr_od_slot, IOLINK_OD_SLOTS,
                     (uint8_t) sizeof(iolink_od_rec_t));

    ctx->baudrate = IOLINK_BAUDRATE_COM2;
    if (ctx->phy->set_baudrate != NULL) {
        ctx->phy->set_baudrate(ctx->phy->user, IOLINK_BAUDRATE_COM2);
    }

    DLL_EVENTS_INIT(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_init(&ctx->ds, NULL);
#endif
#if IOLINK_FEATURE_EVENTS
    ctx->isdu.event_ctx = &ctx->events;
#else
    ctx->isdu.event_ctx = NULL;
#endif
    ctx->isdu.dll_ctx = ctx;
#if IOLINK_FEATURE_DATA_STORAGE
    ctx->isdu.ds_ctx = &ctx->ds;
#else
    ctx->isdu.ds_ctx = NULL;
#endif

    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);

    /*
     * Initial line mode depends on whether the PHY can observe the electrical
     * C/Q wake-up pulse:
     *
     *  - PHYs that can (detect_wakeup != NULL, e.g. a transceiver front-end or
     *    the virtual PHY) start in SIO and switch to SDCI when a wake-up is
     *    detected, matching the IO-Link startup handshake.
     *
     *  - PHYs that cannot (detect_wakeup == NULL, e.g. a plain UART sitting
     *    behind a transceiver that has already established COM) would otherwise
     *    sit in SIO forever and never service M-sequences. Such a link is, by
     *    contract, an already-established SDCI link, so start in SDCI and accept
     *    M-sequences immediately.
     */
    if (dll_phy_has_wakeup(ctx)) {
        iolink_dll_set_sio_mode(ctx);
    }
    else {
        iolink_dll_set_sdci_mode(ctx);
    }
}

/** @brief Validate and dispatch one complete request held in ctx->frame_buf.
 *
 * Shared by the streaming RX loop and the ISR-pump mailbox: both assemble a
 * complete M-sequence first, then run this exact validation + dispatch.
 */
/**
 * @brief Publish the framed ISDU response view for the ISR read pump.
 *
 * Single-writer handoff: ctx->isdu (and the double-buffered response) is owned
 * by the main pump; the ISR reads the last published buffer. {length, busy,
 * buffer} travel as one atomic word, so no critical section and no copy are
 * needed, and the ISR always reads a complete, stable response.
 */
static void dll_isdu_sync_snapshot(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    const bool busy =
        (ctx->isdu.state == ISDU_STATE_SERVICE_EXECUTE) || (ctx->isdu.state == ISDU_STATE_BUSY);
    uint8_t n = 0U;
    uint8_t sel = ctx->isdu.resp_pub;
    if (ctx->isdu.state == ISDU_STATE_RESPONSE_READY) {
        n = (ctx->isdu.resp_total > 255U) ? 255U : (uint8_t) ctx->isdu.resp_total;
        sel = ctx->isdu.resp_sel;
        /* The completed response becomes the published one; the engine will
         * build the next response in the other buffer. */
        ctx->isdu.resp_pub = sel;
    }
    const IOLINK_ATOMIC_UINT_T word = IOLINK_ISDU_PUB_WORD(n, busy, sel);
    if (word != ctx->isr_isdu_pub) {
        /* Publish last: the response octets are already in resp_store[sel]. */
        iolink_atomic_store(&ctx->isr_isdu_pub, word);
    }
}

/** @brief Decode a DPP1 MasterCycleTime/MinCycleTime octet to 100 us ticks (ST Table B.3). */
static uint16_t dll_decode_cycle_time_ticks(uint8_t value)
{
    if (value <= 3U) {
        return 0U;
    }
    if (value < 0x40U) {
        return (uint16_t) value;
    }
    if ((value & 0xC0U) == 0x40U) {
        return (uint16_t) (((uint16_t) (value & 0x3FU) * 4U) + 0x40U);
    }
    if ((value & 0xC0U) == 0x80U) {
        return (uint16_t) (((uint16_t) (value & 0x3FU) * 0x10U) + 0x140U);
    }
    return 0U;
}

/** @brief Master polling period in microseconds (DPP1 MasterCycleTime, else MinCycleTime). */
static uint32_t dll_master_cycle_us(const iolink_dll_ctx_t* ctx)
{
    uint16_t ticks = dll_decode_cycle_time_ticks(ctx->master_cycle_time);
    if (ticks == 0U) {
        ticks = (uint16_t) ((ctx->min_cycle_time_us + 99U) / 100U);
    }
    if (ticks == 0U) {
        ticks = 20U; /* 2 ms fallback */
    }
    return (uint32_t) ticks * 100U;
}

/** @brief Refresh the master-activity timestamp.
 *
 * Used when the data ISR sees a frame but the Back-to-box cycle count must
 * still be advanced exactly once, by whoever actually served the request. */
static void dll_master_activity_ts(iolink_dll_ctx_t* ctx)
{
    ctx->master_last_activity_ms = dll_time_ms(ctx);
    ctx->master_lost_count = 0U;
    ctx->master_lost = false;
}

/** @brief ST-style master activity: frame end / RX timeout restarts the counter. */
void dll_master_activity(iolink_dll_ctx_t* ctx)
{
    dll_master_activity_ts(ctx);

    /* 10.7.5: count the frames served after Back-to-box so the device keeps
       answering for at least 3 MasterCycles before it stops (see
       dll_process_complete_frame, which performs the actual stop). Counted
       exactly once per served cycle: from the ISR for a request it answers
       itself, otherwise from dll_process_complete_frame. */
    if (ctx->back_to_box_pending) {
        ctx->back_to_box_cycles++;
    }
}

void iolink_dll_request_back_to_box(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->back_to_box_pending = true;
    ctx->back_to_box_cycles = 0U;
}

/* -------------------------------------------------------------------------
 * Main pump: frame validation, dispatch and timing supervision
 * ------------------------------------------------------------------------- */

/** @brief Result of the A.1.6 checksum and CKT type validation. */
typedef enum
{
    DLL_FRAME_INVALID,    /**< Checksum mismatch: drop, keep the DL mode. */
    DLL_FRAME_TYPE_ERROR, /**< Valid checksum, illegal M-sequence type. */
    DLL_FRAME_VALID       /**< Valid request; dispatch by state. */
} dll_frame_result_t;

/**
 * @brief Handle the two states that accept no more frames.
 *
 * @return true when the frame was dropped (the caller must not run the common
 *         tail cleanup).
 */
static bool dll_frame_drop_if_stopped(iolink_dll_ctx_t* ctx)
{
    if (ctx->back_to_box_pending && (ctx->back_to_box_cycles >= 3U)) {
        /* 10.7.5: after at least 3 MasterCycles the device stops and disables
           communication and the output driver until the next power-on. */
        ctx->back_to_box_pending = false;
        dll_set_state(ctx, IOLINK_DLL_STATE_COMM_DISABLED);
        if ((ctx->phy != NULL) && (ctx->phy->set_mode != NULL)) {
            ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_INACTIVE);
        }
        ctx->frame_index = 0U;
        return true;
    }
    if (ctx->state == IOLINK_DLL_STATE_COMM_DISABLED) {
        /* Communication stopped until the next power-on (10.7.5). */
        ctx->frame_index = 0U;
        return true;
    }
    return false;
}

/** @brief T_CYCLE supervision: count a too-short MasterCycle and restart it. */
static void dll_check_cycle_time(iolink_dll_ctx_t* ctx)
{
    const uint64_t now_us = dll_time_us(ctx);
    if ((ctx->enforce_timing) && (ctx->min_cycle_time_us > 0U) &&
        (ctx->last_cycle_start_us != 0U) &&
        (now_us - ctx->last_cycle_start_us < (uint64_t) ctx->min_cycle_time_us)) {
        ctx->timing_errors++;
        ctx->t_cycle_violations++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
    }
    ctx->last_cycle_start_us = now_us;
}

/**
 * @brief Validate the A.1.6 checksum and the CKT M-sequence type.
 *
 * A.1.6/Figure A.2: the checksum lives in the CKT octet (second octet); its
 * bits 0-5 are zeroed and the type bits (6-7) are kept before the checksum is
 * computed. C5/Table 47: the type must be legal for the current state.
 */
static dll_frame_result_t dll_frame_validate(iolink_dll_ctx_t* ctx)
{
    uint8_t crc_buf[sizeof(ctx->frame_buf)];
    (void) memcpy(crc_buf, ctx->frame_buf, ctx->req_len);
    const uint8_t expected_ck = (uint8_t) (crc_buf[1] & 0x3FU);
    crc_buf[1] = (uint8_t) (crc_buf[1] & IOLINK_MSEQ_TYPE_MASK);
    const bool crc_ok = (iolink_checksum6(crc_buf, ctx->req_len) == expected_ck);
    const bool type_ok = dll_type_allowed(ctx, ctx->frame_buf);

    if (crc_ok && type_ok) {
        return DLL_FRAME_VALID;
    }
    if (crc_ok) {
        return DLL_FRAME_TYPE_ERROR;
    }
    return DLL_FRAME_INVALID;
}

/**
 * @brief Account for an invalid or type-illegal frame.
 *
 * A checksum failure keeps the DL mode (ST rc=2 semantics). A valid checksum
 * with an illegal type is a protocol error outside OPERATE/ESTAB and returns to
 * STARTUP; inside OPERATE/ESTAB it is treated as a framing artifact from an
 * overlapping PD stream and only counted.
 */
static void dll_handle_frame_error(iolink_dll_ctx_t* ctx, dll_frame_result_t result)
{
    if (result == DLL_FRAME_TYPE_ERROR) {
        ctx->frame_eager = 0U;
        ctx->framing_errors++;
        ctx->frame_index = 0U;
        if ((ctx->state == IOLINK_DLL_STATE_OPERATE) ||
            (ctx->state == IOLINK_DLL_STATE_ESTAB_COM)) {
            return;
        }
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_FRAMING, IOLINK_EVENT_TYPE_ERROR);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        return;
    }

    /* Checksum failure: ST rc=2 means "discard this frame and go back to
     * listening"; the DLL mode is left untouched. Do not demote to SIO here: a
     * single truncated/collided frame (e.g. the master starting its next
     * M-sequence while our reply is still on C/Q) must not take the link down.
     * SIO is reserved for the Fallback MasterCommand and the T_DSIO timeout. */
    ctx->crc_errors++;
    ctx->framing_errors++;
    ctx->total_retries++;
    /* Keep the diagnostic event (ST has no counter, but the application uses it
     * for the event flag) without changing the DL mode. */
    ctx->fallback_count++;
    if (ctx->fallback_count >= ctx->sio_fallback_threshold) {
        ctx->fallback_count = 0U;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_WARNING);
    }
}

#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
/**
 * @brief Reject an OPERATE request received during startup.
 *
 * Table 53 (SDCI_TC_0035): only a V1.0 Device reacts to an OPERATE request
 * received during STARTUP. A V1.1 Device must not answer it and must not
 * advance to PREOPERATE/ESTAB_COM.
 *
 * @return true when the frame was rejected (skip the tail cleanup).
 */
static bool dll_reject_startup_operate(iolink_dll_ctx_t* ctx, bool was_establishing)
{
    if (was_establishing && (ctx->req_len == 3U) &&
        ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U) &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
        ((ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK) == 0x00U) &&
        ((ctx->frame_buf[2] == IOLINK_CMD_DEVICE_OPERATE) ||
         (ctx->frame_buf[2] == IOLINK_CMD_PROCESS_DATA_OPERATE))) {
        ctx->framing_errors++;
        ctx->frame_index = 0U;
        return true;
    }
    return false;
}
#endif /* !IOLINK_COMPAT_V10_STARTUP_OPERATE */

/**
 * @brief PREOPERATE: intercept the first message after wake-up.
 *
 * Transition T1: a Type-0 READ of a Direct Parameter page octet on the page
 * channel is the spec startup probe. Answer from the direct-parameter source
 * rather than feeding the MC into ISDU. Later PREOPERATE Type-0 frames are ISDU
 * traffic and are not intercepted.
 */
static bool dll_handle_startup_probe(iolink_dll_ctx_t* ctx, bool was_establishing, uint8_t mc)
{
    const bool page_read =
        ((mc & IOLINK_MC_RW_MASK) != 0U) && ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == 0x20U);
    if (was_establishing && (ctx->req_len == 2U) && page_read) {
        dll_handle_page_channel_read(ctx, mc);
        return true;
    }
    return false;
}

/** @brief PREOPERATE dispatch: Type-0 only, plus FALLBACK/MasterCommand. */
static void dll_dispatch_preoperate(iolink_dll_ctx_t* ctx, bool was_establishing)
{
    const uint8_t mc = ctx->frame_buf[0];
    if (dll_handle_startup_probe(ctx, was_establishing, mc)) {
        return;
    }
    if (ctx->req_len == 2U) {
        if (mc == IOLINK_MC_TRANSITION_COMMAND) {
            dll_handle_preoperate(ctx, mc, ctx->frame_buf[1]);
        }
        else {
            dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
        }
    }
    else if ((ctx->req_len == 3U) && dll_is_fallback_command(ctx)) {
        /* Table B.2/43: FALLBACK MasterCommand 0x5A. */
        dll_handle_fallback_command(ctx);
    }
    else if ((ctx->req_len == 3U) && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
             ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
             ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
        /* Page-channel Type-0 WRITE of a MasterCommand (Table B.2):
           0x97 DeviceStartup, 0x98 PDOperate, 0x99 DeviceOperate,
           0x9A DevicePreoperate, 0x95/0x96 ident, 0x5A Fallback.
           ST applies the mode change and answers with CKS only. */
        dll_handle_master_command(ctx, ctx->frame_buf[2]);
    }
    else if (ctx->req_len == 3U) {
        /* Any other 3-octet Type-0 OD write in PREOPERATE is ISDU or diagnosis
           traffic (7.3.6 allows ISDU before OPERATE). It is a TYPE_0 frame: one
           OD octet at offset 2 and a CKS-only reply (Figure A.5), never the
           configured OPERATE PD widths. */
        dll_handle_type0_od_write(ctx, ctx->frame_buf[0], ctx->frame_buf[2]);
    }
}

/** @brief ESTAB_COM dispatch: first valid non-transition frame enters OPERATE. */
static void dll_dispatch_estab_com(iolink_dll_ctx_t* ctx)
{
    const uint8_t mc = ctx->frame_buf[0];
    const bool type0 = ((ctx->frame_buf[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0);
    const bool page_write = type0 && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
                            ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE);
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        dll_handle_preoperate(ctx, mc, ctx->frame_buf[1]);
    }
    else if (page_write && ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
        /* MasterCommand before the first cyclic frame: apply it instead of
           mistaking a Type-0 page write for Type-2 PD. */
        dll_handle_master_command(ctx, ctx->frame_buf[2]);
    }
    else {
        /* The first valid non-transition frame establishes communication and
           moves to OPERATE, Type 0 included. */
        dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
        if (page_write) {
            dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
        }
        else if (type0 && (ctx->req_len == 2U)) {
            dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
        }
        else if (type0) {
            /* Type-0 OD write (ISDU/diagnosis): one OD octet at 2. */
            dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
        }
        else {
            /* A fixed TYPE_1_x/TYPE_2_x READ is only MC + CKT (req_len == 2), so
               req_len cannot distinguish it from Type-0: the CKT type bits are
               authoritative. */
            dll_handle_operate_type1_2(ctx);
        }
    }
}

/** @brief OPERATE dispatch: every channel is valid, transitions are forbidden. */
static bool dll_dispatch_operate(iolink_dll_ctx_t* ctx)
{
    const uint8_t mc = ctx->frame_buf[0];
    const bool type0 = ((ctx->frame_buf[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0);
    const bool page_write = type0 && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
                            ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE);
    if ((ctx->req_len == 3U) && (dll_is_fallback_command(ctx))) {
        /* Table B.2/43, Table 47 T9: OPERATE -> SIO after T_FBD. */
        dll_handle_fallback_command(ctx);
    }
    else if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        /* Transition command is illegal in OPERATE. Report it but keep the DL
           mode: the master never sends it in a healthy link, and ST rc=2/rc=3
           recovery here would only add churn. */
        ctx->framing_errors++;
    }
    else if (type0 && (ctx->req_len == 2U)) {
#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
        /* Tables 57/67 (SDCI_TC_0038/0049): a TYPE_0 page READ while OPERATE is
           active is a reset request. Do not answer it; go back to STARTUP so the
           next read is handled as the startup probe. TYPE_0 page writes keep
           their normal behaviour. */
        if (((mc & IOLINK_MC_RW_MASK) != 0U) &&
            ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
            (ctx->m_seq_type != IOLINK_M_SEQ_TYPE_0)) {
            ctx->framing_errors++;
            ctx->frame_index = 0U;
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            return false; /* the reset path skips the common tail cleanup */
        }
#endif
        dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
    }
    else if (page_write && ((mc & IOLINK_MC_ADDR_MASK) == 0x00U)) {
        /* MasterCommand page write in OPERATE (0x9A/0x98/0x99/...). ST handles
           these in every DL mode and only changes the state, never treats them
           as an illegal M-sequence. */
        dll_handle_master_command(ctx, ctx->frame_buf[2]);
    }
    else if (page_write) {
        /* DPP parameter write (e.g. MasterCycleTime): CKS-only ack. */
        dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
    }
    else if (type0) {
        /* Type-0 OD write on ISDU/diagnosis: CKS-only ack. */
        dll_handle_type0_od_write(ctx, mc, ctx->frame_buf[2]);
    }
    else {
        /* Fixed TYPE_1_x/TYPE_2_x READ may be only MC + CKT (req_len == 2);
           disambiguate by the CKT type bits, not the length. */
        dll_handle_operate_type1_2(ctx);
    }
    return true;
}

/**
 * @brief Dispatch a validated frame by DL state.
 *
 * @return false when the frame was rejected without state progress and the
 *         caller must skip the common tail cleanup.
 */
static bool dll_handle_valid_frame(iolink_dll_ctx_t* ctx)
{
    ctx->last_valid_frame_ms = dll_time_ms(ctx);
    const bool was_establishing =
        (ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) || (ctx->state == IOLINK_DLL_STATE_STARTUP);
#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
    if (dll_reject_startup_operate(ctx, was_establishing)) {
        return false;
    }
#endif
    if (was_establishing) {
        dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
        ctx->dsio_deadline_ms = 0U; /* valid message cancels T_DSIO */
    }

    if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
        dll_dispatch_preoperate(ctx, was_establishing);
    }
    else if (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) {
        dll_dispatch_estab_com(ctx);
    }
    else if (ctx->state == IOLINK_DLL_STATE_OPERATE) {
        return dll_dispatch_operate(ctx);
    }
    return true;
}

/** @brief Clear the per-frame bookkeeping and refresh the ISR ISDU snapshot. */
static void dll_finish_frame(iolink_dll_ctx_t* ctx)
{
    ctx->frame_index = 0U;
    ctx->frame_eager = 0U;
    /* Reflect any ISDU state change (e.g. a completed write request) for the
     * ISR read pump before the next master frame. */
    dll_isdu_sync_snapshot(ctx);
}

static void dll_process_complete_frame(iolink_dll_ctx_t* ctx)
{
    if (dll_frame_drop_if_stopped(ctx)) {
        return;
    }
    dll_check_cycle_time(ctx);

    /* A one-octet "frame" is not an M-sequence: it is the wake-up byte the
     * transceiver forwards in SIO (or line noise). Ignore it instead of failing
     * the checksum, which would read the absent second octet and drive a
     * spurious fallback. */
    if (ctx->req_len < IOLINK_M_SEQ_HEADER_LEN) {
        ctx->frame_index = 0U;
        return;
    }

    /* ST restarts its callback timer at frame end; a completed frame (even a
       bad one) proves the link is still being driven. */
    dll_master_activity(ctx);

    const dll_frame_result_t result = dll_frame_validate(ctx);
    if (result == DLL_FRAME_VALID) {
        if (!dll_handle_valid_frame(ctx)) {
            return;
        }
    }
    else {
        dll_handle_frame_error(ctx, result);
        if (result == DLL_FRAME_TYPE_ERROR) {
            return; /* the type-error path clears its own bookkeeping */
        }
    }

    dll_finish_frame(ctx);
}

/** Re-assert the configured operating COM after a recovery drop to COM1. */
static void dll_restore_operating_baudrate(iolink_dll_ctx_t* ctx)
{
    /* Do not reconfigure the UART in the middle of a reply: wait for the
     * staged main reply (isr_tx_pending) or the TX BYTE octet feed
     * (isr_tx_active) to drain. TX FRAME transfers are owned by the PHY and are
     * not visible here: should the baudrate change while one is on the wire, the
     * PHY aborts it in its critical section and the master retries the request. */
    if ((ctx->isr_tx_active != 0U) || (ctx->isr_tx_pending != 0U)) {
        return;
    }
    if ((ctx->operating_baudrate != IOLINK_BAUDRATE_UNSET) &&
        (ctx->baudrate != ctx->operating_baudrate)) {
        (void) iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
    }
}

/** @brief Consume the ISR master-activity flag (timestamp only, no cycle count). */
static void dll_take_isr_master_activity(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_master_activity != 0U) {
        ctx->isr_master_activity = 0U;
        /* Timestamp only: the Back-to-box cycle count is advanced exactly once
         * per served request (here for an ISR-answered frame, or in
         * dll_process_complete_frame for a main-answered one). */
        dll_master_activity_ts(ctx);
    }
}

/**
 * @brief Apply deadline requests the data ISR only flags.
 *
 * The two 64-bit deadlines stay main-owned so no 64-bit value is ever written
 * from interrupt context (a torn read would fire a spurious T_DSIO/T_FBD).
 * The ISR sets a flag; the main pump arms or clears the deadline here, before
 * any deadline check in the same pass.
 */
static void dll_apply_isr_deadline_requests(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_fallback_pending != 0U) {
        ctx->isr_fallback_pending = 0U;
        /* Only arm while the DL mode is still FALLBACK: a later command may
         * have left it, in which case a T_FBD deadline would be spurious. */
        if (ctx->state == IOLINK_DLL_STATE_FALLBACK) {
            /* Table 47 T8/T9: T_FBD is 3 MasterCycleTimes, capped at 500 ms.
             * The reference point is this main-context pass, one pass later
             * than the frame end; that only stretches T_FBD (never shortens
             * it) by at most one main-loop latency. */
            uint32_t t_fbd_ms = 500U;
            if (ctx->min_cycle_time_us > 0U) {
                t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
                if (t_fbd_ms > 500U) {
                    t_fbd_ms = 500U;
                }
            }
            ctx->fallback_deadline_ms = dll_time_us(ctx) / 1000U + t_fbd_ms;
        }
    }
    if (ctx->fallback_clear_pending != 0U) {
        ctx->fallback_clear_pending = 0U;
        /* A command other than FALLBACK was applied: drop any pending T_FBD. */
        if (ctx->state != IOLINK_DLL_STATE_FALLBACK) {
            ctx->fallback_deadline_ms = 0U;
        }
    }
    if (ctx->isr_dsio_clear != 0U) {
        ctx->isr_dsio_clear = 0U;
        ctx->dsio_deadline_ms = 0U;
    }
}

/**
 * @brief Hand an ISR-staged ISDU write to the transport.
 *
 * The ISR stages whole segments in a ring; the main pump owns the transport
 * and the response buffer. Each iteration copies one record out of the ring,
 * releases its slot (so the ISR may reuse it) and then runs the (possibly
 * slow) transport with interrupts enabled. No critical section is involved.
 */
static void dll_drain_staged_isdu_write(iolink_dll_ctx_t* ctx)
{
    for (;;) {
        const iolink_od_rec_t* rec = (const iolink_od_rec_t*) iolink_ring_peek(&ctx->isr_od_ring);
        if (rec == NULL) {
            return;
        }
        uint8_t staged_fc = rec->flowctrl;
        uint8_t staged_len = rec->len;
        if (staged_len > IOLINK_OD_MAX_SIZE) {
            staged_len = (uint8_t) IOLINK_OD_MAX_SIZE;
        }
        uint8_t staged_od[IOLINK_OD_MAX_SIZE];
        for (uint8_t i = 0U; i < staged_len; i++) {
            staged_od[i] = rec->data[i];
        }
        iolink_ring_release(&ctx->isr_od_ring);
        if (staged_len > 0U) {
            iolink_isdu_od_write(&ctx->isdu, staged_fc, staged_od, staged_len);
        }
    }
}

/** @brief ST-style master-lost supervision from completed frames/RX timeouts. */
static void dll_update_master_lost(iolink_dll_ctx_t* ctx, uint32_t now_ms)
{
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) && (ctx->master_lost_cycles != 0U)) {
        uint32_t cycle_ms = (dll_master_cycle_us(ctx) + 999U) / 1000U;
        if (cycle_ms == 0U) {
            cycle_ms = 1U;
        }
        uint32_t elapsed = now_ms - ctx->master_last_activity_ms;
        uint32_t missed = elapsed / cycle_ms;
        if (missed > 0xFFU) {
            missed = 0xFFU;
        }
        ctx->master_lost_count = (uint8_t) missed;
        ctx->master_lost = (ctx->master_lost_count >= ctx->master_lost_cycles);
    }
    else {
        ctx->master_lost_count = 0U;
        ctx->master_lost = false;
    }
}

/**
 * @brief 1 s idle-link diagnostic counter.
 *
 * This is not the ST-style master-lost hint; it does not leave the current DL
 * mode, so a late master can resume on the existing mode/baudrate.
 */
static void dll_check_idle_timeout(iolink_dll_ctx_t* ctx, uint32_t now_ms)
{
    if ((ctx->last_activity_ms != 0U) && (now_ms - ctx->last_activity_ms > 1000U)) {
        /* A silent link is not a response-time violation: count it separately so
         * timeout_errors stays a genuine t_A metric. */
        ctx->idle_timeouts++;
        ctx->last_activity_ms = now_ms;
    }
}

/**
 * @brief Table 47 T10: a wake-up without a valid message within T_DSIO returns
 * the device to SIO. Only applies while still waiting for the first message.
 */
static void dll_check_dsio_deadline(iolink_dll_ctx_t* ctx, uint64_t now_ms64)
{
    const uint64_t dsio_deadline = ctx->dsio_deadline_ms;
    if ((ctx->phy_mode != IOLINK_PHY_MODE_SIO) && (dsio_deadline != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
         (ctx->state == IOLINK_DLL_STATE_STARTUP))) {
        if (now_ms64 > dsio_deadline) {
            ctx->dsio_deadline_ms = 0U;
            ctx->wakeup_seen = false;
            ctx->timeout_errors++;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }
}

/**
 * @brief Table 47 T8/T9 + Table 43: after a FALLBACK MasterCommand the device
 * switches to SIO after T_FBD (3 MasterCycleTimes, at most 500 ms). The reply
 * was already sent when the command was handled.
 */
static void dll_check_fallback_deadline(iolink_dll_ctx_t* ctx, uint64_t now_ms64)
{
    const uint64_t fallback_deadline = ctx->fallback_deadline_ms;
    if ((fallback_deadline != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_FALLBACK) || (ctx->state == IOLINK_DLL_STATE_OPERATE) ||
         (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) ||
         (ctx->state == IOLINK_DLL_STATE_PREOPERATE))) {
        if (now_ms64 >= fallback_deadline) {
            ctx->fallback_deadline_ms = 0U;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }
}

/**
 * @brief Enforce the t_PD guard window.
 *
 * @return true when the guard is active and the caller must return without
 *         processing a mailbox frame.
 */
static bool dll_check_t_pd(iolink_dll_ctx_t* ctx)
{
    if (!dll_t_pd_active(ctx)) {
        return false;
    }
    if (dll_drain_rx(ctx)) {
        ctx->timing_errors++;
        ctx->t_pd_violations++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
    }
    return true;
}

/**
 * @brief SIO: detect a C/Q wake-up and start the communication window.
 *
 * @return true when the DLL is (or stays) in SIO mode and the caller must
 *         return.
 */
static bool dll_handle_sio_wakeup(iolink_dll_ctx_t* ctx)
{
    if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
        return false;
    }
    if ((ctx->frame_index == 0U) && dll_phy_has_wakeup(ctx)) {
        if (dll_phy_detect_wakeup(ctx) > 0) {
            ctx->wakeup_seen = true;
            dll_set_state(ctx, IOLINK_DLL_STATE_AWAITING_COMM);
            ctx->wakeup_deadline_us = dll_time_us(ctx) + IOLINK_T_WU_US;
            /* Table 47 T10: without a valid message the device returns to SIO
               within T_DSIO (60..300 ms, default 300 ms). */
            ctx->dsio_deadline_ms = dll_time_us(ctx) / 1000U + IOLINK_T_DSIO_MS;
            iolink_dll_set_sdci_mode(ctx);
            /* A wake-up starts a fresh communication-establishment window.
               Reset the inactivity timer so a re-wake after a prior exchange
               is not immediately killed by the stale activity timestamp. */
            ctx->last_activity_ms = dll_time_ms(ctx);
            dll_master_activity(ctx); /* ST StackOnWakeUp restarts Timer B */
        }
    }
    return true;
}

/** @brief ST Timer A: a byte gap inside a frame aborts it and re-listens. */
static void dll_check_t_byte(iolink_dll_ctx_t* ctx)
{
    if ((ctx->frame_index > 0U) && (ctx->enforce_timing) && (ctx->t_byte_limit_us > 0U)) {
        /* The ISR writes the low 32 bits of the byte timestamp; an unsigned
         * delta is wrap-safe for the millisecond-scale t_byte window. */
        const uint32_t last_byte_us = ctx->last_byte_us;
        if (last_byte_us != 0U) {
            const uint32_t now_us = (uint32_t) dll_time_us(ctx);
            if ((uint32_t) (now_us - last_byte_us) > ctx->t_byte_limit_us) {
                ctx->timing_errors++;
                ctx->t_byte_violations++;
                ctx->framing_errors++;
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                  IOLINK_EVENT_TYPE_WARNING);
                /* Abort the current frame and re-listen; the DL mode is not
                   changed. */
                ctx->frame_index = 0U;
            }
        }
    }
}

/** @brief T_WU: keep waiting for the first message inside the wake-up window. */
static bool dll_wakeup_window_active(iolink_dll_ctx_t* ctx)
{
    if ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) && (ctx->enforce_timing)) {
        if ((ctx->wakeup_deadline_us != 0U) && (dll_time_us(ctx) < ctx->wakeup_deadline_us)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Move an ISR-published frame into the main context and dispatch it.
 *
 * The frame arrives through the SPSC mailbox ring: copy the oldest record,
 * release its slot, then run the validation/dispatch. No critical section is
 * needed (the ISR never writes the slot the consumer is reading), and the
 * record carries its own length and eager flag.
 */
static void dll_consume_isr_mailbox(iolink_dll_ctx_t* ctx)
{
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx->isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    uint8_t n = rec->len;
    if (n > sizeof(ctx->frame_buf)) {
        n = (uint8_t) sizeof(ctx->frame_buf);
    }
    (void) memcpy(ctx->frame_buf, rec->data, n);
    ctx->frame_index = n;
    ctx->req_len = n;
    /* The record carries the eager flag with its octets, so no cross-frame
     * snapshot is needed. */
    ctx->frame_eager = rec->eager;
    /* Release after the copy: the ISR may then reuse this slot, while the other
     * one stays intact. No critical section is required, because the ISR never
     * writes the slot at 'out'. */
    iolink_ring_release(&ctx->isr_frame_ring);
    /* A mailbox frame is received traffic: refresh the inactivity and byte-gap
       timestamps exactly like the streaming RX path. Without this the 1 s idle
       supervision resets a healthy ISR link. */
    ctx->last_activity_ms = dll_time_ms(ctx);
    ctx->last_byte_us = (uint32_t) dll_time_us(ctx);
    dll_process_complete_frame(ctx);
}

/** @brief Main-pump body; the public entry point delivers the state afterwards. */
static void dll_process_body(iolink_dll_ctx_t* ctx)
{
    dll_take_isr_master_activity(ctx);
    dll_apply_isr_deadline_requests(ctx);

    /* A captured PD-in buffer is released by the TX BYTE pump when the reply
     * completes. A TX FRAME PHY can report the transfer end through tx_busy();
     * without that hook the library cannot distinguish "still on the wire" from
     * "transceiver stalled", so it keeps the lease until the next capture and
     * pd_in_skips surfaces the stall. The section keeps the data ISR from
     * capturing a new buffer between the poll and the release. */
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if ((ctx->phy != NULL) && (ctx->phy->tx_busy != NULL)) {
        IOLINK_CRITICAL_ENTER();
        if ((ctx->pd_in_captured != 0xFFU) && (ctx->phy->tx_busy(ctx->phy->user) == 0U)) {
            ctx->pd_in_captured = 0xFFU;
        }
        IOLINK_CRITICAL_EXIT();
    }
#endif

    /* Restore the operating COM before the SIO/power-on early returns: a
     * recovery drop to COM1 must not leave the UART on the wrong baudrate. */
    dll_restore_operating_baudrate(ctx);

    dll_drain_staged_isdu_write(ctx);
    iolink_isdu_process(&ctx->isdu);
    /* The service may have completed here: refresh the ISR response snapshot. */
    dll_isdu_sync_snapshot(ctx);
    dll_poll_diagnostics(ctx);
    /* A frame may have completed in the ISR during the steps above. */
    dll_take_isr_master_activity(ctx);

    /* Sampled after the late activity refresh so the elapsed-time subtraction
     * below cannot see a timestamp newer than now_ms. One clock sample keeps
     * the millisecond and microsecond views consistent. */
    const uint64_t now_ms64 = dll_time_us(ctx) / 1000U;
    const uint32_t now_ms = (uint32_t) now_ms64;

    dll_update_master_lost(ctx, now_ms);
    dll_check_idle_timeout(ctx, now_ms);
    dll_check_dsio_deadline(ctx, now_ms64);
    dll_check_fallback_deadline(ctx, now_ms64);

    if (dll_check_t_pd(ctx)) {
        return;
    }
    if (dll_handle_sio_wakeup(ctx)) {
        return;
    }
    dll_check_t_byte(ctx);
    if (dll_wakeup_window_active(ctx)) {
        return;
    }

    dll_consume_isr_mailbox(ctx);

    /* Undo a same-call recovery drop to COM1 (SDCI path reaches here). */
    dll_restore_operating_baudrate(ctx);
}

void iolink_dll_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }
    dll_process_body(ctx);
    /* Level-based application notification: the single place the state hook can
     * run. Delivery does not depend on which path the body took. */
    iolink_dll_flush_state_callbacks(ctx);
}

/* -------------------------------------------------------------------------
 * Public configuration and query API
 * ------------------------------------------------------------------------- */

iolink_dll_state_t iolink_dll_get_state(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->state : IOLINK_DLL_STATE_STARTUP;
}

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->phy_mode : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->baudrate : IOLINK_BAUDRATE_COM2;
}

/** @brief Compute the device MinCycleTime from the M-sequence timing (Table B.3).
 *
 * Uses the ST ministry formula: mseq_us = (reply_octets*14 + request_octets*12
 * + 6) * 1e6 / baud. The worst case of a PD write and a PD/ISDU read is taken,
 * rounded up to 0.1 ms units, and clamped to the application floor. */
uint8_t iolink_dll_compute_min_cycle_time(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    const uint32_t baud = dll_get_baud_hz(ctx);
    const uint32_t pdi = ctx->pd_in_len_current;
    const uint32_t pdo = ctx->pd_out_len_current;
    const uint32_t od = ctx->od_width;
    /* Write request: MC+CKT + PD-out + OD; reply: PD-in + CKS.
     * Read request: MC+CKT + PD-out (no OD); reply: OD + PD-in + CKS.
     * This holds for every Annex A type: the fixed types simply have 0 octets
     * in the fields they do not carry (see A.6-A.13). */
    const uint32_t t_write = ((pdi + 1U) * 14U + (2U + pdo + od) * 12U + 6U) * 1000000U / baud;
    const uint32_t t_read = ((od + pdi + 1U) * 14U + (2U + pdo) * 12U + 6U) * 1000000U / baud;
    const uint32_t t = (t_write > t_read) ? t_write : t_read;
    uint32_t units = (t + 99U) / 100U; /* ceil to 0.1 ms */
    const uint32_t app_units = (ctx->min_cycle_time_app_us + 99U) / 100U;
    if (units < app_units) {
        units = app_units;
    }
    if (units < 4U) {
        units = 4U; /* Table B.3 minimum is 0.4 ms. */
    }
    return (units > 255U) ? 255U : (uint8_t) units;
}

int iolink_dll_set_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) return -1;
    const bool changing = (ctx->baudrate != baudrate);
    ctx->baudrate = baudrate;
    if (ctx->phy->set_baudrate != NULL) ctx->phy->set_baudrate(ctx->phy->user, baudrate);
    if (changing) {
        /* The PHY aborts an in-flight reply when the UART is reconfigured;
         * drop the matching ISR TX state so it cannot resume the stale frame
         * on the next request. */
        ctx->isr_tx_pending = 0U;
        ctx->isr_tx_active = 0U;
        ctx->isr_tx_has_cks = 0U;
        ctx->isr_tx_idx = 0U;
        ctx->pd_in_captured = 0xFFU;
    }
    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);
    ctx->min_cycle_time_us = (uint32_t) iolink_dll_compute_min_cycle_time(ctx) * 100U;
    return 0;
}

int iolink_dll_set_operating_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) {
        return -1;
    }
    if ((uint8_t) baudrate > (uint8_t) IOLINK_BAUDRATE_COM3) {
        return -1;
    }
    ctx->operating_baudrate = baudrate;
    return iolink_dll_set_baudrate(ctx, baudrate);
}

int iolink_dll_set_pd_length(iolink_dll_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if ((ctx == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
        (pd_out_len > IOLINK_PD_OUT_MAX_SIZE))
        return -1;
    /* The data ISR reads pd_out_len_current to frame a request; publish the
     * PD-in/PD-out pair atomically so it cannot observe a half update. */
    IOLINK_CRITICAL_ENTER();
    ctx->pd_in_len_current = pd_in_len;
    ctx->pd_out_len_current = pd_out_len;
    IOLINK_CRITICAL_EXIT();
    return 0;
}

void iolink_dll_get_pd_length(const iolink_dll_ctx_t* ctx, uint8_t* pd_in_len, uint8_t* pd_out_len)
{
    if (ctx == NULL) return;
    if (pd_in_len) *pd_in_len = ctx->pd_in_len_current;
    if (pd_out_len) *pd_out_len = ctx->pd_out_len_current;
}

int iolink_dll_set_sio_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;
    /* The SIO transition aborts any in-flight reply in the PHY; drop the
     * matching ISR TX state so a stale active index cannot leak into the next
     * SDCI session (int_active() is false in SIO, so nothing would finish it). */
    ctx->isr_tx_pending = 0U;
    ctx->isr_tx_active = 0U;
    ctx->isr_tx_has_cks = 0U;
    ctx->isr_tx_idx = 0U;
    ctx->pd_in_captured = 0xFFU;
    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_SDCI);
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    return 0;
}

void iolink_dll_get_stats(const iolink_dll_ctx_t* ctx, iolink_dll_stats_t* out_stats)
{
    if ((ctx == NULL) || (out_stats == NULL)) return;
    out_stats->crc_errors = ctx->crc_errors;
    out_stats->timeout_errors = IOLINK_STAT_TOTAL(ctx, timeout_errors);
    out_stats->idle_timeouts = ctx->idle_timeouts;
    out_stats->framing_errors = IOLINK_STAT_TOTAL(ctx, framing_errors);
    out_stats->timing_errors = ctx->timing_errors;
    out_stats->t_ren_violations = ctx->t_ren_violations;
    out_stats->t_cycle_violations = ctx->t_cycle_violations;
    out_stats->t_byte_violations = ctx->t_byte_violations;
    out_stats->t_pd_violations = ctx->t_pd_violations;
    out_stats->total_retries = ctx->total_retries;
    out_stats->voltage_faults = ctx->voltage_faults;
    out_stats->overtemp_faults = ctx->overtemp_faults;
    out_stats->short_circuits = ctx->short_circuits;
    out_stats->isr_tx_dropped = IOLINK_STAT_TOTAL(ctx, isr_tx_dropped);
}

void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t* ctx, bool enable)
{
    if (ctx != NULL) ctx->enforce_timing = enable;
}

void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t* ctx, uint32_t limit_us)
{
    if (ctx == NULL) return;
    ctx->t_ren_limit_us = limit_us;
    ctx->t_ren_override = (limit_us != 0U);
}

void iolink_dll_prepare_cyclic_reply(iolink_dll_ctx_t* ctx)
{
    (void) ctx; /* Replies are generated directly from the shared state now. */
}

/* Derive the expected request length for the M-sequence whose first octet
 * (the MC byte) is @p mc_byte, from the current DLL state. Pure / read-only so
 * the ISR pump can call it directly; shared by both RX paths. */
static uint8_t dll_compute_req_len(const iolink_dll_ctx_t* ctx, uint8_t mc_byte)
{
    const uint8_t type0_channel = (uint8_t) (mc_byte & IOLINK_MC_COMM_CHANNEL_MASK);
    const bool type0_od_write = (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0) &&
                                ((mc_byte & IOLINK_MC_RW_MASK) == 0U) &&
                                ((type0_channel == IOLINK_MC_CHANNEL_PAGE) ||
                                 (type0_channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                                 (type0_channel == IOLINK_MC_CHANNEL_ISDU));
    if (type0_od_write) {
        /* Figure A.5: a Type-0 write carries exactly ONE OD octet
           whatever OD width the OPERATE M-sequence uses. */
        return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + 1U);
    }
    /* STARTUP/PREOPERATE expect Type 0; ESTAB_COM and OPERATE use the configured
       type (C5/Table 47). The transition command is the sole Type-0 exchange
       once a multi type is configured. */
    uint8_t type_bits = dll_expected_type_bits(ctx);
    bool transition_cmd =
        (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) && (mc_byte == IOLINK_MC_TRANSITION_COMMAND);
    if (!transition_cmd && (type_bits != IOLINK_MSEQ_TYPE_0)) {
        const uint8_t od_req = ((mc_byte & IOLINK_MC_RW_MASK) == 0U) ? ctx->od_width : 0U;
        return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current + od_req);
    }
    return 2U;
}

uint8_t iolink_dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t mc)
{
    if (ctx == NULL) {
        return 0U;
    }
    return dll_compute_req_len(ctx, mc);
}

iolink_reply_variant_t iolink_dll_reply_variant(const iolink_dll_ctx_t* ctx, uint8_t mc)
{
    if (ctx == NULL) {
        return IOLINK_REPLY_NONE;
    }
    const bool write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    if (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0) {
        return write ? IOLINK_REPLY_TYPE0_CKS : IOLINK_REPLY_TYPE0_OD;
    }
    if ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PROCESS) {
        return write ? IOLINK_REPLY_CYCLIC_WRITE : IOLINK_REPLY_CYCLIC_READ;
    }
    /* Non-cyclic OD channel (ISDU / page / diagnosis). A WRITE reply carries no
     * OD, so it reuses the cyclic-write payload [PD-in]. A READ reply carries the
     * staged OD response (Busy until the main pump has answered). */
    return write ? IOLINK_REPLY_CYCLIC_WRITE : IOLINK_REPLY_ACYCLIC_READ;
}
