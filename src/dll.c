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
#include "iolinki/time_utils.h"
#include "iolinki/utils.h"
#include "iolinki/platform.h"
#include <string.h>

/* The event engine is internal to the DLL/ISDU. When it is compiled out, the
 * helpers below collapse to constants so no disabled interface is referenced. */
#if IOLINK_FEATURE_EVENTS
#define DLL_EVENTS_INIT(ctx) iolink_events_init(ctx)
#define DLL_EVENT_TRIGGER(ctx, code, type) iolink_event_trigger((ctx), (code), (type))
#define DLL_EVENTS_FLAG(ctx) iolink_events_flag(ctx)
#define DLL_EVENTS_MEMORY_READ(ctx, addr) iolink_events_memory_read((ctx), (addr))
#define DLL_EVENTS_MEMORY_WRITE(ctx, addr, value) iolink_events_memory_write((ctx), (addr), (value))
#else
#define DLL_EVENTS_INIT(ctx) ((void) 0)
#define DLL_EVENT_TRIGGER(ctx, code, type) ((void) 0)
#define DLL_EVENTS_FLAG(ctx) false
#define DLL_EVENTS_MEMORY_READ(ctx, addr) 0U
#define DLL_EVENTS_MEMORY_WRITE(ctx, addr, value) ((void) 0)
#endif
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

#define DLL_LOG(...)

/**
 * @brief Centralised state transition.
 *
 * Updates the state and notifies the optional state-change hook on an actual
 * change, so no transition is ever missed.
 */
static void dll_set_state(iolink_dll_ctx_t* ctx, iolink_dll_state_t new_state)
{
    if (ctx->state != new_state) {
        ctx->state = new_state;
        if (ctx->state_cb != NULL) {
#if IOLINK_PHY_FEATURE_ISR_PUMP
            if (ctx->in_isr != 0U) {
                /* Application callbacks must not run in the data ISR. */
                ctx->pending_state = new_state;
                ctx->state_cb_pending = 1U;
                return;
            }
#endif
            ctx->state_cb(ctx->state_cb_user, new_state);
        }
    }
}

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
        case IOLINK_M_SEQ_TYPE_2_V:
            return IOLINK_MSEQ_TYPE_2;
        default:
            return IOLINK_MSEQ_TYPE_0;
    }
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
#if !IOLINK_PHY_FEATURE_ISR_PUMP
static uint8_t dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t type_bits)
{
    if (type_bits == IOLINK_MSEQ_TYPE_0) {
        return 2U;
    }
    return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current + ctx->od_len);
}
#endif

#if IOLINK_PHY_FEATURE_ISR_PUMP
static void dll_isr_tx_step(iolink_dll_ctx_t* ctx);
static void dll_isdu_sync_snapshot(iolink_dll_ctx_t* ctx);
#endif

/**
 * @brief Emit a device reply through the active transport.
 *
 * Streaming (default): phy->send(), exactly as upstream.
 * Opt-in ISR pump: hand the octets to the raw-octet/frame TX hooks; the ISR
 * feeds them (TX BYTE) or the PHY transmits the whole frame (TX FRAME).
 * The reply octets (including the CKS) are built by the shared handlers either
 * way, so both transports use the same frame layout.
 */
static void dll_send(iolink_dll_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (data == NULL) || (len == 0U)) {
        return;
    }
#if IOLINK_PHY_FEATURE_ISR_PUMP
    if (len > sizeof(ctx->isr_tx_payload[0])) {
        return;
    }
    /* Stage the reply (verbatim: the octets already carry the CKS) and let the
     * ISR report it. The TX hooks are never driven from this (main) context. */
    if (ctx->lock != NULL) {
        ctx->lock(ctx->lock_user);
    }
    const uint8_t slot = ctx->isr_tx_next;
    ctx->isr_tx_next ^= 1U;
    (void) memcpy(ctx->isr_tx_payload[slot], data, len);
    ctx->isr_tx_pending_slot = slot;
    ctx->isr_tx_len = (uint8_t) len;
    ctx->isr_tx_has_cks = 0U;
    ctx->isr_tx_pending = 1U; /* publish last */

    if (ctx->phy->request_service != NULL) {
        /* Ask the PHY to run the data-interrupt pump now. Kept inside the
         * critical section so the pend cannot race a natural data interrupt. */
        ctx->phy->request_service(ctx->phy->user);
        if (ctx->unlock != NULL) {
            ctx->unlock(ctx->lock_user);
        }
        return;
    }

#if !IOLINK_PHY_FEATURE_ISR_TX_ONLY
    /* Legacy fallback for PHYs without a service hook (streaming/host tests). */
    ctx->isr_tx_pending = 0U;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if (ctx->phy->tx_frame != NULL) {
        (void) ctx->phy->tx_frame(ctx->phy->user, ctx->isr_tx_payload[slot], len);
    }
#else
    if (ctx->phy->tx_octet != NULL) {
        if (ctx->isr_tx_active != 0U) {
            if (ctx->phy->tx_done != NULL) {
                ctx->phy->tx_done(ctx->phy->user);
            }
            ctx->isr_tx_active = 0U;
        }
        ctx->isr_tx_base = ctx->isr_tx_payload[slot];
        ctx->isr_tx_slot = slot;
        ctx->isr_tx_idx = 0U;
        ctx->isr_tx_active = 1U;
        dll_isr_tx_step(ctx);
    }
#endif
#else
    /* ISR-only PHY without request_service(): the reply cannot be transmitted.
     * Count it so a misconfiguration is observable instead of silent. */
    ctx->isr_tx_pending = 0U;
    ctx->isr_tx_dropped++;
#endif /* !IOLINK_PHY_FEATURE_ISR_TX_ONLY */
    if (ctx->unlock != NULL) {
        ctx->unlock(ctx->lock_user);
    }
#else
    (void) ctx->phy->send(ctx->phy->user, data, len);
#endif
}

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
    return iolink_time_get_us() < ctx->t_pd_deadline_us;
}

/** @brief Drain all pending RX bytes from the PHY; returns true if any were seen. */
static bool dll_drain_rx(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return false;
    }
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* The ISR pump fills the mailbox; draining it discards a frame that arrived
     * while the t_pd guard window was active. */
    if (ctx->isr_frame_ready != 0U) {
        ctx->isr_frame_ready = 0U;
        return true;
    }
    return false;
#else
    if (ctx->phy->recv_byte == NULL) {
        return false;
    }
    bool saw_byte = false;
    uint8_t byte = 0U;
    while (ctx->phy->recv_byte(ctx->phy->user, &byte) > 0) {
        saw_byte = true;
    }
    return saw_byte;
#endif
}

/** @brief Handle a communication failure: count retries and revert toward SIO/STARTUP. */
static void dll_enter_fallback(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->fallback_count++;
    ctx->total_retries++;

    if (ctx->fallback_count >= ctx->sio_fallback_threshold) {
        /* Communication could not be sustained: enter FALLBACK, revert the PHY
           to SIO at COM1, then settle back to STARTUP to await a new wake-up. */
        dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
        iolink_dll_set_sio_mode(ctx);
        iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->fallback_count = 0U;
        ctx->frame_index = 0U;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_FRAMING,
                             IOLINK_EVENT_TYPE_WARNING);
    }
    else if (ctx->state != IOLINK_DLL_STATE_OPERATE && ctx->state != IOLINK_DLL_STATE_ESTAB_COM) {
        iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
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
    uint32_t t_fbd_ms = 500U;
    if (ctx->min_cycle_time_us > 0U) {
        t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
        if (t_fbd_ms > 500U) {
            t_fbd_ms = 500U;
        }
    }
    ctx->fallback_deadline_ms = iolink_time_get_us() / 1000U + t_fbd_ms;
    dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);

    uint8_t resp[1];
    resp[0] = 0x00U;
    resp[0] = iolink_checksum6(resp, 1U);
    dll_send(ctx, resp, 1);
    ctx->last_response_us = iolink_time_get_us();
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
    ctx->last_response_us = iolink_time_get_us();
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
           request octets; a read fetches the framed response (or Busy). */
        const uint8_t flowctrl = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            iolink_isdu_od_read(&ctx->isdu, flowctrl, od_out, od_len);
        }
        else {
#if IOLINK_PHY_FEATURE_ISR_PUMP
            if (ctx->isdu_isr_consumed != 0U) {
                ctx->isdu_isr_consumed = 0U; /* already buffered by the ISR */
            }
            else
#endif
            {
                iolink_isdu_od_write(&ctx->isdu, flowctrl, od_in, od_len);
            }
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
    if (!ctx->pd_valid) {
        ck_flags |= 0x40U;
    }

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
    ctx->last_response_us = iolink_time_get_us();
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
    if (!ctx->pd_valid) {
        resp[0] |= 0x40U;
    }
    resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
    dll_send(ctx, resp, 1);
    ctx->last_response_us = iolink_time_get_us();
}

/** @brief Process a Type-1/Type-2 OPERATE frame (PD+OD) and build the response, enforcing t_REN. */
static void dll_handle_operate_type1_2(iolink_dll_ctx_t* ctx)
{
    /* Type-1/2 master message: MC | CKT | PD | OD; the checksum is in CKT. */
    uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    uint16_t od_offset = (uint16_t) (pd_offset + ctx->pd_out_len_current);

    if (ctx->pd_out_len_current > 0U) {
        memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->pd_out_len_current);
    }

    const bool write = ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U);
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* An OPERATE ISDU read is answered by the ISR from the response snapshot;
     * the main pump must not advance the ISDU segment index again. Only an
     * IDLE/ABORT read needs the transport reset (and the snapshot refresh). */
    if (ctx->frame_eager &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) && !write) {
        const uint8_t flowctrl = (uint8_t) (ctx->frame_buf[0] & IOLINK_MC_ADDR_MASK);
        if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2) ||
            (flowctrl == IOLINK_FLOWCTRL_ABORT)) {
            uint8_t dummy = 0U;
            iolink_isdu_od_read(&ctx->isdu, flowctrl, &dummy, 0U); /* reset only */
            dll_isdu_sync_snapshot(ctx);
        }
        ctx->frame_eager = 0U;
        ctx->last_response_us = iolink_time_get_us();
        return;
    }
    const uint8_t od_w = ctx->od_width; /* 1/2/8/32 from the opt-in patch */
#else
    const uint8_t od_w = ctx->od_len; /* upstream streaming width (1/2) */
#endif
    const uint8_t req_od = write ? od_w : 0U;
    const uint8_t resp_od = write ? 0U : od_w;

    uint8_t od_in[IOLINK_OD_MAX_SIZE];
    uint8_t od_out[IOLINK_OD_MAX_SIZE];
    (void) memset(od_in, 0, sizeof(od_in));
    (void) memset(od_out, 0, sizeof(od_out));
    if (req_od > 0U) {
        memcpy(od_in, &ctx->frame_buf[od_offset], req_od);
    }

    const uint8_t dispatch_od = write ? req_od : resp_od;
    if (!dll_dispatch_od(ctx, ctx->frame_buf[0], od_in, dispatch_od, od_out)) {
        for (uint16_t i = 0; i < dispatch_od; i++) {
            od_out[i] = 0U;
        }
    }

#if IOLINK_PHY_FEATURE_ISR_PUMP
    if (ctx->frame_eager &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PROCESS)) {
        /* The ISR pump already transmitted the pre-staged cyclic reply for the
         * process channel at frame end. */
        ctx->fallback_count = 0U;
        ctx->last_response_us = iolink_time_get_us();
        ctx->frame_eager = 0U;
        return;
    }
    if (ctx->frame_eager != 0U) {
        /* The ISR already acknowledged this ISDU write; do not send a second
         * reply. ISDU reads are handled before dll_dispatch_od(). */
        ctx->frame_eager = 0U;
        ctx->last_response_us = iolink_time_get_us();
        return;
    }
#endif

    /* Figures A.9/A.10/A.13/A.14: the reply to a READ M-sequence is
       [OD octets][PD-in octets] CKS; the reply to a WRITE M-sequence carries no
       OD and is [PD-in octets] CKS. CKS carries the Event flag (bit 7), the
       PD-validity flag (bit 6, 1 = invalid) and the 6-bit message checksum
       (bits 0-5); there is no leading status octet (A.1.5). */

    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + IOLINK_OD_MAX_SIZE + 1];
    uint16_t pos = 0U;
    if (!write && (resp_od > 0U)) {
        memcpy(&resp[pos], od_out, resp_od);
        pos += resp_od;
    }
    if (ctx->pd_in_len_current > 0U) {
        memcpy(&resp[pos], &ctx->pd_in[ctx->pd_in_active][ctx->od_width],
               ctx->pd_in_len_current);
        pos += ctx->pd_in_len_current;
    }

    uint8_t cks = 0x00U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        cks |= 0x80U;
    }
    if (!ctx->pd_valid) {
        cks |= 0x40U;
    }
    resp[pos] = cks;
    resp[pos] = (uint8_t) (resp[pos] | iolink_checksum6(resp, pos + 1U));
    pos++;

#if IOLINK_PHY_FEATURE_ISR_PUMP
    if (dll_phy_has_tx(ctx)) {
#else
    if (ctx->phy->send != NULL) {
#endif
        dll_send(ctx, resp, pos);
        ctx->fallback_count = 0U;
        uint32_t end_tx_us = (uint32_t) iolink_time_get_us();
        ctx->response_time_us = end_tx_us - (uint32_t) ctx->last_cycle_start_us;

        if (ctx->enforce_timing) {
            uint32_t limit = dll_get_t_ren_limit_us(ctx);
            if ((limit > 0U) && (ctx->response_time_us > limit)) {
                ctx->timing_errors++;
                ctx->t_ren_violations++;
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                     IOLINK_EVENT_TYPE_WARNING);
            }
        }
    }
    ctx->last_response_us = iolink_time_get_us();
}

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

void iolink_dll_init(iolink_dll_ctx_t* ctx, const iolink_phy_api_t* phy)
{
    if ((phy == NULL) || (!iolink_ctx_zero(ctx, sizeof(iolink_dll_ctx_t)))) {
        return;
    }
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->phy = phy;
    ctx->enforce_timing = (IOLINK_TIMING_ENFORCE_DEFAULT != 0U);
    ctx->sio_fallback_threshold = 3U;

    /* Table A.10: TYPE_2_1/TYPE_2_2 carry one On-request Data octet; TYPE_2_V is
       the two-octet variant (OPERATE M-sequence code 5). */
    ctx->od_len = (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) ? 2U : 1U;
#if IOLINK_PHY_FEATURE_ISR_PUMP
    ctx->isr_isdu_last_flowctrl = 0xFFU; /* no START seen yet */
#endif

    ctx->pd_in_len_current = ctx->pd_in_len;
    ctx->pd_out_len_current = ctx->pd_out_len;
    ctx->pd_in_len_max = ctx->pd_in_len;
    ctx->pd_out_len_max = ctx->pd_out_len;
    ctx->pd_valid = true;

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
#if IOLINK_PHY_FEATURE_ISR_PUMP
/** @brief Pre-stage the framed ISDU response for the ISR read pump. */
static void dll_isdu_sync_snapshot(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* The ISR reads the framed response directly from ctx->isdu.resp_buf; keep
     * only the lightweight Busy/length view in sync. Shared single-writer: the
     * main pump is inside a critical section while this runs. */
    const bool busy = (ctx->isdu.state == ISDU_STATE_SERVICE_EXECUTE) ||
                      (ctx->isdu.state == ISDU_STATE_BUSY);
    uint8_t n = 0U;
    if (ctx->isdu.state == ISDU_STATE_RESPONSE_READY) {
        n = (ctx->isdu.resp_total > 255U) ? 255U : (uint8_t) ctx->isdu.resp_total;
    }
    const uint8_t next_busy = busy ? 1U : 0U;
    const bool changed = (ctx->isr_isdu_len != n) || (ctx->isr_isdu_busy != next_busy);
    ctx->isr_isdu_busy = next_busy;
    ctx->isr_isdu_len = n;
    if (changed) {
        /* A new response (or Busy transition) invalidates the FlowCTRL sequence;
         * the Master must restart with START. */
        ctx->isr_isdu_idx = 0U;
        ctx->isr_isdu_seg_idx = 0U;
        ctx->isr_isdu_last_flowctrl = 0xFFU;
    }
    /* Do not rewind isr_isdu_idx here: the ISR advances it across COUNT reads
     * and only a START read restarts the response. Resetting it on every main
     * loop pass would replay the first segment of a multi-segment read. */
}
#endif

static void dll_process_complete_frame(iolink_dll_ctx_t* ctx)
{
    uint64_t now_us_proc = iolink_time_get_us();
    if ((ctx->enforce_timing) && (ctx->min_cycle_time_us > 0U) &&
        (ctx->last_cycle_start_us != 0U)) {
        if (now_us_proc - ctx->last_cycle_start_us < (uint64_t) ctx->min_cycle_time_us) {
            ctx->timing_errors++;
            ctx->t_cycle_violations++;
            DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                 IOLINK_EVENT_TYPE_WARNING);
        }
    }
    ctx->last_cycle_start_us = now_us_proc;

    /* A one-octet "frame" is not an M-sequence: it is the wake-up byte the
     * transceiver forwards in SIO (or line noise). Ignore it instead of
     * failing the checksum, which would read the absent second octet and
     * drive a spurious fallback. */
    if (ctx->req_len < IOLINK_M_SEQ_HEADER_LEN) {
        ctx->frame_index = 0U;
        return;
    }

    /* A.1.6/Figure A.2: the master message carries the checksum in the
       CKT octet (the second octet); its bits 0-5 are zeroed before the
       A.1.6 checksum is computed over every message octet. The CKT is
       also where the M-sequence type (bits 6-7) lives. */
    uint8_t crc_buf[sizeof(ctx->frame_buf)];
    (void) memcpy(crc_buf, ctx->frame_buf, ctx->req_len);
    const uint8_t expected_ck = (uint8_t) (crc_buf[1] & 0x3FU);
    crc_buf[1] = (uint8_t) (crc_buf[1] & IOLINK_MSEQ_TYPE_MASK);
    bool crc_ok = (iolink_checksum6(crc_buf, ctx->req_len) == expected_ck);

    /* C5/Table 47: the CKT type bits must match the M-sequence type in
       force. STARTUP and PREOPERATE are Type-0 only; in OPERATE the
       configured type is expected. An illegal type is an illegal
       M-sequence: report it and return to STARTUP (Table 47 T8/T11). */
    uint8_t rx_type = (uint8_t) (ctx->frame_buf[1] & IOLINK_MSEQ_TYPE_MASK);
    uint8_t exp_type = IOLINK_MSEQ_TYPE_0;
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) ||
        ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
         (ctx->frame_buf[0] != IOLINK_MC_TRANSITION_COMMAND))) {
        exp_type = dll_mseq_type_bits(ctx->m_seq_type);
    }
    bool type_ok = (rx_type == exp_type);

    if (crc_ok && type_ok) {
        bool was_establishing = (ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
                                (ctx->state == IOLINK_DLL_STATE_STARTUP);
        if (was_establishing) {
            dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
            ctx->dsio_deadline_ms = 0U; /* valid message cancels T_DSIO */
        }

        if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
            uint8_t mc = ctx->frame_buf[0];
            bool page_read = ((mc & IOLINK_MC_RW_MASK) != 0U) &&
                             ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == 0x20U);
            if (was_establishing && (ctx->req_len == 2U) && page_read) {
                /* First message after wake-up is the spec startup probe
                   (transition T1): a Type-0 READ of a Direct Parameter page
                   octet on the page channel. Answer from the direct-parameter
                   source rather than feeding the MC into ISDU. Later PREOPERATE
                   Type-0 frames are ISDU traffic and are not intercepted. */
                dll_handle_page_channel_read(ctx, mc);
            }
            else if (ctx->req_len == 2U) {
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
                     ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == 0x20U) &&
                     ((mc & IOLINK_MC_ADDR_MASK) == 0x00U) &&
                     (ctx->frame_buf[2] == IOLINK_CMD_DEVICE_OPERATE)) {
                /* Spec DeviceOperate: page-channel Type-0 WRITE of
                   MasterCommand 0x99 to Direct Parameter address 0x00.
                   Figure A.5: a Type-0 write reply is the CKS only. */
                dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
                ctx->fallback_count = 0U;

                uint8_t resp[1];
                uint8_t cks = 0x00U;
                if (DLL_EVENTS_FLAG(&ctx->events)) {
                    cks |= 0x80U;
                }
                if (!ctx->pd_valid) {
                    cks |= 0x40U;
                }
                resp[0] = cks;
                resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
                dll_send(ctx, resp, 1);
                ctx->last_response_us = iolink_time_get_us();
            }
            else if (ctx->req_len == 3U) {
                /* Any other 3-octet Type-0 OD write in PREOPERATE is
                   ISDU or diagnosis traffic (7.3.6 allows ISDU before
                   OPERATE). It is a TYPE_0 frame: one OD octet at
                   offset 2 and a CKS-only reply (Figure A.5), never the
                   configured OPERATE PD widths. */
                dll_handle_type0_od_write(ctx, ctx->frame_buf[0], ctx->frame_buf[2]);
            }
        }
        else if (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) {
            if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND) {
                dll_handle_preoperate(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
            }
            else {
                /* The first valid non-transition frame establishes
                   communication and moves to OPERATE, Type 0 included. */
                dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
                if (ctx->req_len == 2U) {
                    dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                }
                else {
                    dll_handle_operate_type1_2(ctx);
                }
            }
        }
        else if (ctx->state == IOLINK_DLL_STATE_OPERATE) {
            /* Transitions are forbidden; every communication channel is
               valid in OPERATE (page/diagnosis/ISDU/process). */
            if ((ctx->req_len == 3U) && (dll_is_fallback_command(ctx))) {
                /* Table B.2/43, Table 47 T9: OPERATE -> SIO after T_FBD. */
                dll_handle_fallback_command(ctx);
            }
            else if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND) {
                ctx->framing_errors++;
                dll_enter_fallback(ctx);
            }
            else if (ctx->req_len == 2U) {
                dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
            }
            else {
                dll_handle_operate_type1_2(ctx);
            }
        }
    }
    else if (crc_ok && !type_ok) {
        /* Illegal M-sequence type: report it and go to STARTUP (T11). */
        ctx->frame_eager = 0U;
        ctx->isdu_isr_consumed = 0U;
        ctx->framing_errors++;
        DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_GENERAL,
                             IOLINK_EVENT_TYPE_ERROR);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->frame_index = 0U;
        return;
    }
    else {
        ctx->crc_errors++;
        ctx->framing_errors++;
        dll_enter_fallback(ctx);
    }
    ctx->frame_index = 0U;
    ctx->frame_eager = 0U;
    ctx->isdu_isr_consumed = 0U;
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* Reflect any ISDU state change (e.g. a completed write request) for the
     * ISR read pump before the next master frame. */
    dll_isdu_sync_snapshot(ctx);
#endif
}

void iolink_dll_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    /* Process acyclic ISDU state machine. In the two-pump transport the ISR
     * buffers ISDU write octets; the critical section keeps the execute step
     * from being preempted mid-update (ST's IRQ-off main window). */
#if IOLINK_PHY_FEATURE_ISR_PUMP
    iolink_critical_enter();
#endif
    iolink_isdu_process(&ctx->isdu);
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* The service may have completed here: refresh the ISR response snapshot. */
    dll_isdu_sync_snapshot(ctx);
    iolink_critical_exit();
#endif

    dll_poll_diagnostics(ctx);

    uint32_t now_ms = iolink_time_get_ms();
    uint64_t now_ms64 = iolink_time_get_us() / 1000U;
    if ((ctx->last_activity_ms != 0U) && (now_ms - ctx->last_activity_ms > 1000U)) {
        ctx->last_activity_ms = 0U; /* Prevent repeated resets */
        ctx->timeout_errors++;      /* Link went idle past the inactivity window */
        if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
        }
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->frame_index = 0U;
    }

    /* Table 47 T10: a wake-up without a valid message within T_DSIO returns the
       device to SIO. Only applies while still waiting for the first message. */
    if ((ctx->phy_mode != IOLINK_PHY_MODE_SIO) && (ctx->dsio_deadline_ms != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
         (ctx->state == IOLINK_DLL_STATE_STARTUP))) {
        if (now_ms64 > ctx->dsio_deadline_ms) {
            ctx->dsio_deadline_ms = 0U;
            ctx->wakeup_seen = false;
            ctx->timeout_errors++;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }

    /* Table 47 T8/T9 + Table 43: after a FALLBACK MasterCommand the device
       switches to SIO after T_FBD (3 MasterCycleTimes, at most 500 ms) at the
       latest. The reply was already sent when the command was handled. */
    if ((ctx->fallback_deadline_ms != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_FALLBACK) || (ctx->state == IOLINK_DLL_STATE_OPERATE) ||
         (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) ||
         (ctx->state == IOLINK_DLL_STATE_PREOPERATE))) {
        if (now_ms64 >= ctx->fallback_deadline_ms) {
            ctx->fallback_deadline_ms = 0U;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }

    if (dll_t_pd_active(ctx)) {
        if (dll_drain_rx(ctx)) {
            ctx->timing_errors++;
            ctx->t_pd_violations++;
            DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
        }
        return;
    }

    if (ctx->phy_mode == IOLINK_PHY_MODE_SIO) {
        if ((ctx->frame_index == 0U) && dll_phy_has_wakeup(ctx)) {
            if (dll_phy_detect_wakeup(ctx) > 0) {
                ctx->wakeup_seen = true;
                dll_set_state(ctx, IOLINK_DLL_STATE_AWAITING_COMM);
                ctx->wakeup_deadline_us = iolink_time_get_us() + IOLINK_T_WU_US;
                /* Table 47 T10: without a valid message the device returns to
                   SIO within T_DSIO (60..300 ms, default 300 ms). */
                ctx->dsio_deadline_ms = iolink_time_get_us() / 1000U + IOLINK_T_DSIO_MS;
                iolink_dll_set_sdci_mode(ctx);
                /* A wake-up starts a fresh communication-establishment window.
                   Reset the inactivity timer so a re-wake after a prior exchange
                   is not immediately killed by the stale activity timestamp. */
                ctx->last_activity_ms = iolink_time_get_ms();
            }
        }
        return;
    }

    if ((ctx->frame_index > 0U) && (ctx->enforce_timing) && (ctx->t_byte_limit_us > 0U)) {
        if (ctx->last_byte_us != 0U) {
            uint64_t now_us = iolink_time_get_us();
            if (now_us - ctx->last_byte_us > (uint64_t) ctx->t_byte_limit_us) {
                ctx->timing_errors++;
                ctx->t_byte_violations++;
                ctx->framing_errors++;
                DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                     IOLINK_EVENT_TYPE_WARNING);
                ctx->frame_index = 0U;
                dll_enter_fallback(ctx);
            }
        }
    }

    if ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) && (ctx->enforce_timing)) {
        if ((ctx->wakeup_deadline_us != 0U) && (iolink_time_get_us() < ctx->wakeup_deadline_us)) {
            return;
        }
    }

#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* The ISR pump assembles one complete request and publishes it to the
     * mailbox; run the shared validation/dispatch here in the main context. */
    if (ctx->isr_frame_ready != 0U) {
        /* Serialize the consume/dispatch against the ISR: the ISR buffers ISDU
         * writes and may publish the next frame once the slot is released. */
        iolink_critical_enter();
        uint8_t n = ctx->isr_frame_len;
        if (n > sizeof(ctx->frame_buf)) {
            n = (uint8_t) sizeof(ctx->frame_buf);
        }
        (void) memcpy(ctx->frame_buf, (const void*) ctx->isr_frame_ptr, n);
        ctx->frame_index = n;
        ctx->req_len = n;
        /* Snapshot/clear the eager flag for *this* frame before releasing the
         * mailbox; a new ISR frame may then set isr_eager_replied without being
         * clobbered by this frame's processing. */
        ctx->frame_eager = ctx->isr_eager_replied;
        ctx->isr_eager_replied = 0U;
        ctx->isr_frame_ready = 0U;
        /* A mailbox frame is received traffic: refresh the inactivity and
           byte-gap timestamps exactly like the streaming RX path. Without
           this the 1 s idle supervision resets a healthy ISR link. */
        ctx->last_activity_ms = iolink_time_get_ms();
        ctx->last_byte_us = iolink_time_get_us();
        dll_process_complete_frame(ctx);
        iolink_critical_exit();
    }
#else
    uint8_t byte;
    while (ctx->phy->recv_byte != NULL) {
        int received = ctx->phy->recv_byte(ctx->phy->user, &byte);
        if (received <= 0) {
            if (received < 0) {
                /* A transport loss invalidates any already consumed prefix. */
                ctx->frame_index = 0U;
                ctx->last_byte_us = 0U;
                ctx->framing_errors++;
            }
            break;
        }
        uint64_t now_us = iolink_time_get_us();
        ctx->last_activity_ms = iolink_time_get_ms();
        if ((ctx->frame_index > 0U) && (ctx->enforce_timing) && (ctx->t_byte_limit_us > 0U)) {
            if (ctx->last_byte_us != 0U) {
                if (now_us - ctx->last_byte_us > (uint64_t) ctx->t_byte_limit_us) {
                    ctx->timing_errors++;
                    ctx->t_byte_violations++;
                    ctx->framing_errors++;
                    DLL_EVENT_TRIGGER(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                         IOLINK_EVENT_TYPE_WARNING);
                    ctx->frame_index = 0U;
                }
            }
        }
        ctx->last_byte_us = now_us;

        if (ctx->frame_index == 0U) {
            ctx->frame_buf[0] = byte;
            ctx->frame_index = 1U;
            ctx->last_frame_us = now_us;

            /* A Type-0 WRITE on an OD-carrying channel (page, diagnosis, ISDU)
               carries one OD octet after the CKT: MC + CKT + OD. This covers
               DeviceOperate/FALLBACK MasterCommands and every ISDU/event write.
               Type-0 reads carry no OD input; their reply is OD + CKS (A.1.5). */
            const uint8_t type0_channel = (uint8_t) (byte & IOLINK_MC_COMM_CHANNEL_MASK);
            const bool type0_od_write = (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0) &&
                                        ((byte & IOLINK_MC_RW_MASK) == 0U) &&
                                        ((type0_channel == IOLINK_MC_CHANNEL_PAGE) ||
                                         (type0_channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                                         (type0_channel == IOLINK_MC_CHANNEL_ISDU));
            if (type0_od_write) {
                /* Figure A.5: a Type-0 write carries exactly ONE OD octet
                   whatever OD width the OPERATE M-sequence uses. */
                ctx->req_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + 1U);
            }
            else {
                /* STARTUP/PREOPERATE expect Type 0; ESTAB_COM and OPERATE use
                   the configured type (C5/Table 47). The transition command is
                   the sole Type-0 exchange once a multi type is configured.
                   The M-sequence length is independent of the baudrate. */
                uint8_t type_bits = dll_expected_type_bits(ctx);
                bool transition_cmd = (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                                      (byte == IOLINK_MC_TRANSITION_COMMAND);
                if (!transition_cmd && (type_bits != IOLINK_MSEQ_TYPE_0)) {
                    ctx->req_len = dll_expected_req_len(ctx, type_bits);
                }
                else {
                    ctx->req_len = 2U;
                }
            }
        }
        else {
            if (ctx->frame_index < sizeof(ctx->frame_buf)) {
                ctx->frame_buf[ctx->frame_index++] = byte;
            }
            else {
                ctx->frame_index = 0U;
                ctx->framing_errors++;
            }
        }

        if ((ctx->frame_index > 0U) && (ctx->frame_index >= ctx->req_len)) {
            dll_process_complete_frame(ctx);
        }
    }
#endif /* IOLINK_PHY_FEATURE_ISR_PUMP */
}

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

int iolink_dll_set_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) return -1;
    ctx->baudrate = baudrate;
    if (ctx->phy->set_baudrate != NULL) ctx->phy->set_baudrate(ctx->phy->user, baudrate);
    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);
    return 0;
}

int iolink_dll_set_pd_length(iolink_dll_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if ((ctx == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
        (pd_out_len > IOLINK_PD_OUT_MAX_SIZE))
        return -1;
    ctx->pd_in_len_current = pd_in_len;
    ctx->pd_out_len_current = pd_out_len;
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
#if IOLINK_PHY_FEATURE_ISR_PUMP
    /* The SIO transition aborts any in-flight reply in the PHY; drop the
     * matching ISR TX state so a stale active index cannot leak into the next
     * SDCI session (int_active() is false in SIO, so nothing would finish it). */
    ctx->isr_tx_pending = 0U;
    ctx->isr_tx_active = 0U;
    ctx->isr_tx_has_cks = 0U;
    ctx->isr_tx_idx = 0U;
#endif
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
    out_stats->timeout_errors = ctx->timeout_errors;
    out_stats->framing_errors = ctx->framing_errors;
    out_stats->timing_errors = ctx->timing_errors;
    out_stats->t_ren_violations = ctx->t_ren_violations;
    out_stats->t_cycle_violations = ctx->t_cycle_violations;
    out_stats->t_byte_violations = ctx->t_byte_violations;
    out_stats->t_pd_violations = ctx->t_pd_violations;
    out_stats->total_retries = ctx->total_retries;
    out_stats->voltage_faults = ctx->voltage_faults;
    out_stats->short_circuits = ctx->short_circuits;
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

#if IOLINK_PHY_FEATURE_ISR_PUMP
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
    bool transition_cmd = (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                          (mc_byte == IOLINK_MC_TRANSITION_COMMAND);
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

/* Conformance gate for the immediate ISR reply: a master request must carry a
 * valid A.1.6 checksum AND a legal M-sequence type for the current state before
 * the device answers it. Mirrors the validation done later by
 * dll_process_rx_frame(); the eager reply must never answer a corrupt/illegal
 * frame that the main pump would have rejected. Read-only and ISR-safe. */
/* Legal M-sequence type for the current state (same rule as the main pump):
 * STARTUP/PREOPERATE are Type-0; OPERATE (and ESTAB_COM once the transition is
 * done) expect the configured type. */
static bool dll_isr_type_ok(const iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    const uint8_t rx_type = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    uint8_t exp_type = IOLINK_MSEQ_TYPE_0;
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) ||
        ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
         (frame[0] != IOLINK_MC_TRANSITION_COMMAND))) {
        exp_type = dll_mseq_type_bits(ctx->m_seq_type);
    }
    return rx_type == exp_type;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* Validate the request checksum by rescanning the frame (used by the FRAME path,
 * where the PHY hands over a whole buffer). */
static bool dll_isr_reply_allowed(const iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((ctx == NULL) || (frame == NULL) || (len < 2U)) {
        return false;
    }
    const uint8_t expected = (uint8_t) (frame[1] & 0x3FU);
    /* A.1.6 seed; octet 1 (CKT) is masked to its type bits. Handle it outside
     * the loop so the remaining octets run a tight XOR loop. */
    uint8_t acc = (uint8_t) (0x52U ^ frame[0]);
    acc = (uint8_t) (acc ^ (frame[1] & IOLINK_MSEQ_TYPE_MASK));
    for (uint8_t i = 2U; i < len; i++) {
        acc = (uint8_t) (acc ^ frame[i]);
    }
    return (iolink_checksum6_from_acc(acc) == expected) && dll_isr_type_ok(ctx, frame);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/* Validate the request checksum from the accumulator built per received octet
 * (BYTE path): no second pass over the frame in the ISR. */
static bool dll_isr_reply_allowed_acc(const iolink_dll_ctx_t* ctx, const uint8_t* frame,
                                      uint8_t len, uint8_t acc)
{
    if ((ctx == NULL) || (frame == NULL) || (len < 2U)) {
        return false;
    }
    const uint8_t expected = (uint8_t) (frame[1] & 0x3FU);
    return (iolink_checksum6_from_acc(acc) == expected) && dll_isr_type_ok(ctx, frame);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/* Copy the accumulated RX octets into the mailbox for the main pump. Called from
 * the ISR; drops the frame (returns false) if the previous one has not been
 * consumed yet. */
static bool dll_publish_isr_frame(iolink_dll_ctx_t* ctx, uint8_t len)
{
    if (ctx->isr_frame_ready != 0U) {
        ctx->isr_mailbox_drops++;
        return false;
    }
    if (len > sizeof(ctx->isr_frame_buf)) {
        ctx->framing_errors++;
        return false;
    }
    for (uint8_t i = 0U; i < len; i++) {
        ctx->isr_frame_buf[i] = ctx->isr_rx_buf[i];
    }
    ctx->isr_frame_len = len;
    ctx->isr_frame_ptr = ctx->isr_frame_buf;   /* RX BYTE: buffer holds the frame */
    ctx->isr_frame_ready = 1U;                 /* publish last */
    return true;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
/* Send one step of the active reply: the next payload octet, then the computed
 * CKS (CKS mode), then the frame end. One call per transceiver request. */
static void dll_isr_tx_step(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_tx_idx < ctx->isr_tx_len) {
        const uint8_t b = ctx->isr_tx_base[ctx->isr_tx_idx];
        (void) ctx->phy->tx_octet(ctx->phy->user, b);
        if (ctx->isr_tx_has_cks != 0U) {
            ctx->isr_tx_acc = (uint8_t) (ctx->isr_tx_acc ^ b);
        }
        ctx->isr_tx_idx++;
    }
    else if ((ctx->isr_tx_has_cks != 0U) && (ctx->isr_tx_idx == ctx->isr_tx_len)) {
        /* Payload done: emit CKS = flags | checksum6(acc ^ flags). */
        const uint8_t acc = (uint8_t) (ctx->isr_tx_acc ^ ctx->isr_tx_flags);
        const uint8_t cks = (uint8_t) (ctx->isr_tx_flags | iolink_checksum6_from_acc(acc));
        (void) ctx->phy->tx_octet(ctx->phy->user, cks);
        ctx->isr_tx_idx++;
    }
    else {
        if (ctx->phy->tx_done != NULL) {
            ctx->phy->tx_done(ctx->phy->user);
        }
        if (ctx->phy->stack_timer_start != NULL) {
            ctx->phy->stack_timer_start(ctx->phy->user); /* frame end -> Timer B */
        }
        ctx->isr_tx_active = 0U;
    }
}
#endif /* IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE */

/* Fill the OD octets of an ISDU read reply from the response snapshot. */
static void dll_isr_isdu_od(iolink_dll_ctx_t* ctx, uint8_t flowctrl, uint8_t* od,
                            uint8_t od_width)
{
    if (od_width == 0U) {
        return;
    }
    for (uint8_t i = 0U; i < od_width; i++) {
        od[i] = 0x00U;
    }

    bool repeat = false;
    if (flowctrl == IOLINK_FLOWCTRL_START) {
        if (ctx->isr_isdu_busy != 0U) {
            od[0] = 0x01U; /* Busy */
            return;
        }
        if (ctx->isr_isdu_len == 0U) {
            od[0] = 0x00U; /* No Service */
            return;
        }
        ctx->isr_isdu_idx = 0U; /* START restarts the response */
        ctx->isr_isdu_seg_idx = 0U;
        ctx->isr_isdu_last_flowctrl = IOLINK_FLOWCTRL_START;
    }
    else if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        od[0] = 0x00U; /* IDLE / IDLE2 / ABORT / reserved: No Service */
        return;
    }
    else {
        /* COUNT (0x00..0x0F). A COUNT without START is not valid; a repeated
         * COUNT repeats the previous segment; any other value must be +1. */
        if (ctx->isr_isdu_last_flowctrl == 0xFFU) {
            od[0] = 0x00U; /* No Service */
            return;
        }
        if (flowctrl == ctx->isr_isdu_last_flowctrl) {
            repeat = true;
        }
        else {
            const uint8_t expected =
                (uint8_t) ((ctx->isr_isdu_last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
            if (flowctrl != expected) {
                od[0] = 0x00U; /* segmentation error -> No Service */
                ctx->isr_isdu_last_flowctrl = 0xFFU;
                return;
            }
            ctx->isr_isdu_last_flowctrl = flowctrl;
        }
    }

    /* START (ready), a new COUNT, or a repeat: serve one od_width segment.
     * A repeat re-sends the previous segment without advancing the index. */
    uint8_t idx = repeat ? ctx->isr_isdu_seg_idx : ctx->isr_isdu_idx;
    const uint8_t len = ctx->isr_isdu_len;
    uint8_t n = 0U;
    while ((n < od_width) && (idx < len)) {
        od[n++] = ctx->isdu.resp_buf[idx++];
    }
    if (!repeat) {
        ctx->isr_isdu_seg_idx = ctx->isr_isdu_idx;
        ctx->isr_isdu_idx = idx;
    }
}

/* CKS status flags: Event (bit7) + PD invalid (bit6). ISR-safe (plain reads). */
static uint8_t dll_isr_status_flags(const iolink_dll_ctx_t* ctx)
{
    uint8_t flags = 0U;
    if (DLL_EVENTS_FLAG(&ctx->events)) {
        flags |= 0x80U;
    }
    if (!ctx->pd_valid) {
        flags |= 0x40U;
    }
    return flags;
}

/* Send a short reply from the ISR (payload + CKS). Used by the Type-0 fast path;
 * the payload buffer is the library's own staging buffer, so it stays valid for
 * a TX FRAME transfer. */
/* Transmit a reply whose payload is already staged in ctx->isr_tx_payload.
 * Appends the A.1.6 CKS (flags | checksum6(acc ^ flags)) in the same buffer. */
static uint8_t dll_isr_tx_alloc(iolink_dll_ctx_t* ctx)
{
    const uint8_t slot = ctx->isr_tx_next;
    ctx->isr_tx_next ^= 1U;
    return slot;
}

static void dll_isr_send_staged(iolink_dll_ctx_t* ctx, uint8_t slot, uint8_t len, uint8_t flags)
{
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user); /* reply starts -> stop t_A */
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* Short reply: the DMA reads the CKS as soon as the first octet moves to
     * the shift register (the TDR empties immediately at frame start), so the
     * CKS must be in the buffer BEFORE tx_frame(). Only the long PD reply has
     * enough distance (>= 28 octets) to compute it after the DMA start. */
    uint8_t* payload = ctx->isr_tx_payload[slot];
    uint8_t acc = 0x52U;
    for (uint8_t i = 0U; i < len; i++) {
        acc = (uint8_t) (acc ^ payload[i]);
    }
    payload[len] =
        (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
    ctx->isr_tx_len = (uint8_t) (len + 1U);
    ctx->isr_tx_active = 0U;
    if (ctx->phy->tx_frame != NULL) {
        (void) ctx->phy->tx_frame(ctx->phy->user, payload, ctx->isr_tx_len);
    }
#else
    ctx->isr_tx_base = ctx->isr_tx_payload[slot];
    ctx->isr_tx_slot = slot;
    ctx->isr_tx_len = len;
    ctx->isr_tx_flags = flags;
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_acc = 0x52U;
    ctx->isr_tx_has_cks = 1U;
    ctx->isr_tx_active = 1U;
    dll_isr_tx_step(ctx);
#endif
}

static void dll_isr_send_payload(iolink_dll_ctx_t* ctx, const uint8_t* payload, uint8_t len,
                                 uint8_t flags)
{
    const uint8_t slot = dll_isr_tx_alloc(ctx);
    if (payload != NULL) {
        for (uint8_t i = 0U; i < len; i++) {
            ctx->isr_tx_payload[slot][i] = payload[i];
        }
    }
    dll_isr_send_staged(ctx, slot, len, flags);
}

/* Phase 3 fast path: answer Type-0 frames that do not change the DLL state.
 * PREOPERATE/OPERATE only; ISDU reads/writes, page reads and process reads.
 * Page/diagnosis writes (MasterCommand/Event handling) and the startup probe
 * still go through the main state machine. */
static bool dll_isr_handle_type0(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((frame == NULL) || (len < 2U) || !dll_phy_has_tx(ctx)) {
        return false;
    }
    if ((frame[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0) {
        return false;
    }

    const bool read = ((frame[0] & IOLINK_MC_RW_MASK) != 0U);
    const uint8_t ch = (uint8_t) (frame[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    const uint8_t addr = (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK);
    const bool was_establishing =
        (ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) || (ctx->state == IOLINK_DLL_STATE_STARTUP);

    /* Startup probe: Type-0 page READ while waiting for the first message.
     * T1: transition to PREOPERATE and return the requested DPP1 octet. */
    if (was_establishing) {
        if (!read || (ch != IOLINK_MC_CHANNEL_PAGE) || (len != 2U)) {
            return false;
        }
        dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
        ctx->dsio_deadline_ms = 0U;
        const uint8_t od = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
        dll_isr_send_payload(ctx, &od, 1U, dll_isr_status_flags(ctx));
        ctx->last_activity_ms = iolink_time_get_ms();
        ctx->last_byte_us = iolink_time_get_us();
        return true;
    }

    if ((ctx->state != IOLINK_DLL_STATE_PREOPERATE) && (ctx->state != IOLINK_DLL_STATE_OPERATE)) {
        return false;
    }

    /* Transition command (PREOPERATE -> ESTAB_COM): no response per spec. */
    if ((ctx->state == IOLINK_DLL_STATE_PREOPERATE) && (len == 2U) &&
        (frame[0] == IOLINK_MC_TRANSITION_COMMAND)) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->fallback_count = 0U;
        if (ctx->phy->stack_timer_start != NULL) {
            ctx->phy->stack_timer_start(ctx->phy->user);
        }
        ctx->last_activity_ms = iolink_time_get_ms();
        ctx->last_byte_us = iolink_time_get_us();
        return true;
    }

    if (read) {
        uint8_t od = 0U;
        if (ch == IOLINK_MC_CHANNEL_PAGE) {
            od = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
        }
        else if (ch == IOLINK_MC_CHANNEL_ISDU) {
            /* IDLE/ABORT reset the ISDU transport in the main window; leave
             * those to the main pump. */
            if (addr >= IOLINK_FLOWCTRL_IDLE) {
                return false;
            }
            dll_isr_isdu_od(ctx, addr, &od, 1U);
        }
        else if (ch == IOLINK_MC_CHANNEL_PROCESS) {
            od = 0U;
        }
        else {
            return false; /* diagnosis memory: main context */
        }
        dll_isr_send_payload(ctx, &od, 1U, dll_isr_status_flags(ctx));
        ctx->last_activity_ms = iolink_time_get_ms();
        ctx->last_byte_us = iolink_time_get_us();
        return true;
    }

    /* DeviceOperate / Fallback MasterCommands (page WRITE addr 0, Type-0). */
    if ((ctx->state == IOLINK_DLL_STATE_PREOPERATE) && (len == 3U) &&
        (ch == IOLINK_MC_CHANNEL_PAGE) && (addr == 0U)) {
        if (frame[2] == IOLINK_CMD_DEVICE_OPERATE) {
            dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
            ctx->fallback_count = 0U;
            dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
            ctx->last_activity_ms = iolink_time_get_ms();
            ctx->last_byte_us = iolink_time_get_us();
            return true;
        }
        if (frame[2] == IOLINK_CMD_FALLBACK) {
            uint32_t t_fbd_ms = 500U;
            if (ctx->min_cycle_time_us > 0U) {
                t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
                if (t_fbd_ms > 500U) {
                    t_fbd_ms = 500U;
                }
            }
            ctx->fallback_deadline_ms = iolink_time_get_us() / 1000U + t_fbd_ms;
            dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
            dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
            ctx->last_activity_ms = iolink_time_get_ms();
            ctx->last_byte_us = iolink_time_get_us();
            return true;
        }
    }

    /* ISDU write: the ISR already buffered the request octet(s). */
    if ((ch == IOLINK_MC_CHANNEL_ISDU) && (ctx->isdu_isr_consumed != 0U)) {
        ctx->isdu_isr_consumed = 0U; /* the frame is not published to main */
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        ctx->last_activity_ms = iolink_time_get_ms();
        ctx->last_byte_us = iolink_time_get_us();
        return true;
    }
    return false;
}

/* Select the pre-staged OPERATE reply payload matching the just-framed request
 * and start transmitting it from the ISR. TX BYTE: the CKS is computed while the
 * octets are sent (one per transceiver request). TX FRAME: the payload and its
 * CKS are assembled in the staging buffer and handed over in one call. A Type-0
 * request has no pre-staged variant; the main pump answers it. */
/* Buffer an ISDU write request directly in the ISR. Only the transport is
 * performed here (byte collection + FlowCTRL + CHKPDU check); the application
 * dispatch stays in iolink_isdu_process() (main window), matching ST's
 * StackBaseOdWrite vs StackISDUProcess split. */
static void dll_isr_consume_isdu_write(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((frame == NULL) || (len < 2U)) {
        return;
    }
    if ((frame[0] & IOLINK_MC_COMM_CHANNEL_MASK) != IOLINK_MC_CHANNEL_ISDU) {
        return;
    }
    if ((frame[0] & IOLINK_MC_RW_MASK) != 0U) {
        return; /* READ */
    }
    uint8_t od_off;
    uint8_t od_n;
    if ((frame[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0) {
        od_off = 2U; /* Type-0 write: MC + CKT + OD */
        od_n = 1U;
    }
    else {
        od_off = (uint8_t) (2U + ctx->pd_out_len_current);
        od_n = ctx->od_width;
    }
    if (((uint16_t) od_off + (uint16_t) od_n) > (uint16_t) len) {
        return;
    }
    iolink_isdu_od_write(&ctx->isdu, (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK),
                         &frame[od_off], od_n);
    ctx->isdu_isr_consumed = 1U;
}

static bool dll_isr_start_reply(iolink_dll_ctx_t* ctx)
{
    if ((ctx->isr_frame_ptr == NULL) || !dll_phy_has_tx(ctx)) {
        return false;
    }
    const uint8_t first = ctx->isr_frame_ptr[0];
    if (first == IOLINK_MC_TRANSITION_COMMAND) {
        return false; /* invalid in OPERATE; the main pump handles it */
    }
    const bool read = ((first & IOLINK_MC_RW_MASK) != 0U);
    const uint8_t ch = (uint8_t) (first & IOLINK_MC_COMM_CHANNEL_MASK);
    const uint8_t flowctrl = (uint8_t) (first & IOLINK_MC_ADDR_MASK);

    /* Zero-copy reply: the PD-in already lives in the active reply buffer
     * (the main fills the inactive copy and publishes it atomically).
     * Layout: [OD x od_width][PD-in][CKS]. Capture the index once so a swap
     * during the transfer cannot affect this reply. */
    const uint8_t captured = ctx->pd_in_active;
    uint8_t* buf = ctx->pd_in[captured];
    if (read) {
        if (ch == IOLINK_MC_CHANNEL_ISDU) {
            uint8_t od[IOLINK_OD_MAX_SIZE];
            dll_isr_isdu_od(ctx, flowctrl, od, ctx->od_width);
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = od[i];
            }
        }
        else if (ch == IOLINK_MC_CHANNEL_PROCESS) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = 0x00U;
            }
        }
        else {
            return false;
        }
    }
    else if ((ch != IOLINK_MC_CHANNEL_ISDU) && (ch != IOLINK_MC_CHANNEL_PROCESS)) {
        return false;
    }

    /* READ: [OD][PD-in]CKS from buf. WRITE: [PD-in]CKS from buf+od_width (the
     * PD-in is always stored after the configured OD header). */
    const uint8_t* base = read ? buf : (buf + ctx->od_width);
    const uint16_t total =
        (uint16_t) ctx->od_width + (uint16_t) ctx->pd_in_len_current + 1U;
    const uint16_t out_len =
        read ? total : (uint16_t) (total - (uint16_t) ctx->od_width);
    if (out_len > (uint16_t) IOLINK_PD_REPLY_MAX) {
        return false;
    }
    const uint8_t flags = dll_isr_status_flags(ctx);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    ctx->isr_tx_len = (uint8_t) out_len;
    ctx->isr_tx_active = 0U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    /* The DMA reads the CKS almost immediately for a 1-2 octet reply (the TDR
     * empties at frame start). It is only safe to compute the CKS after
     * tx_frame() when at least two octets precede it (out_len >= 3): then the
     * DMA must shift the first octet (one full octet time, baud-scaled) before
     * it reads the checksum. */
    if (out_len >= 3U) {
        if (ctx->phy->tx_frame != NULL) {
            (void) ctx->phy->tx_frame(ctx->phy->user, base, out_len);
        }
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        ((uint8_t*) base)[out_len - 1U] =
            (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
    }
    else {
        /* Short reply: fill the CKS before starting the DMA. */
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        ((uint8_t*) base)[out_len - 1U] =
            (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
        if (ctx->phy->tx_frame != NULL) {
            (void) ctx->phy->tx_frame(ctx->phy->user, base, out_len);
        }
    }
    return true;
#else
    ctx->isr_tx_base = base;
    ctx->isr_tx_len = (uint8_t) (out_len - 1U);
    ctx->isr_tx_flags = flags;
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_acc = 0x52U;
    ctx->isr_tx_has_cks = 1U;
    ctx->isr_tx_active = 1U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    dll_isr_tx_step(ctx);
    return true;
#endif
}

static void dll_isr_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || !dll_phy_has_rx(ctx) || (ctx->phy->int_active == NULL)) {
        return;
    }

    /* A main-context handler staged a verbatim reply; transmit it now. Checked
     * before the int_active gate: a software-requested service has no
     * transceiver RX/TX activity of its own to report. */
    if (ctx->isr_tx_pending != 0U) {
        ctx->isr_tx_pending = 0U;
        if (ctx->phy->response_timer_stop != NULL) {
            ctx->phy->response_timer_stop(ctx->phy->user);
        }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        if (ctx->phy->tx_frame != NULL) {
            (void) ctx->phy->tx_frame(ctx->phy->user,
                                      ctx->isr_tx_payload[ctx->isr_tx_pending_slot],
                                      ctx->isr_tx_len);
        }
#else
        if (ctx->isr_tx_active != 0U) {
            if (ctx->phy->tx_done != NULL) {
                ctx->phy->tx_done(ctx->phy->user);
            }
            ctx->isr_tx_active = 0U;
        }
        ctx->isr_tx_base = ctx->isr_tx_payload[ctx->isr_tx_pending_slot];
        ctx->isr_tx_slot = ctx->isr_tx_pending_slot;
        ctx->isr_tx_idx = 0U;
        ctx->isr_tx_has_cks = 0U;
        ctx->isr_tx_active = 1U;
        dll_isr_tx_step(ctx);
#endif
        return;
    }

    /* Level re-check: the data interrupt may latch a crosstalk edge; bail out at
     * zero cost unless the transceiver is genuinely requesting service. */
    if (ctx->phy->int_active(ctx->phy->user) == 0U) {
        return;
    }

    /* Drain a back-to-back burst, bounded so a wedged line cannot spin forever. */
    for (uint32_t guard = 0U; (guard < 64U) && (ctx->phy->int_active(ctx->phy->user) != 0U);
         guard++) {
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        /* Reply draining (TX BYTE): the transceiver requests the next reply octet.
         * Feed exactly one and yield (the next request arrives as another IRQ). */
        if (ctx->isr_tx_active != 0U) {
            dll_isr_tx_step(ctx);
            break;
        }
#endif

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
        /* RX FRAME: the PHY delimits and delivers whole frames (zero-copy). */
        {
            /* Do not call rx_frame() while the mailbox still holds the previous
             * zero-copy frame: the PHY may reuse its buffer on the next call. */
            if (ctx->isr_frame_ready != 0U) {
                break;
            }
            uint8_t st = 0U;
            uint8_t len = 0U;
            const uint8_t* f = ctx->phy->rx_frame(ctx->phy->user, &st, &len);
            if (f == NULL) {
                break;
            }
            (void) st;
            if (ctx->phy->response_timer_start != NULL) {
                ctx->phy->response_timer_start(ctx->phy->user);
            }
            if ((len < 2U) || (len > IOLINK_M_SEQ_MAX_LEN)) {
                ctx->framing_errors++;
                break;
            }
            const uint8_t expected_len = iolink_dll_expected_req_len(ctx, f[0]);
            if ((expected_len == 0U) || (len != expected_len)) {
                ctx->framing_errors++;
                break;
            }
            const bool allowed = dll_isr_reply_allowed(ctx, f, len);
            {
                const bool isdu_write =
                    (((f[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) &&
                     ((f[0] & IOLINK_MC_RW_MASK) == 0U));
                if (isdu_write) {
                    if (!allowed) {
                        ctx->isr_eager_rejects++;
                        break;
                    }
                    dll_isr_consume_isdu_write(ctx, f, len);
                }
            }
            if (allowed && dll_isr_handle_type0(ctx, f, len)) {
                break;
            }
            {
                const bool type12 = ((f[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0);
                const uint8_t ch = (uint8_t) (f[0] & IOLINK_MC_COMM_CHANNEL_MASK);
                const bool isdu = (ch == IOLINK_MC_CHANNEL_ISDU);
                const bool isdu_write = isdu && ((f[0] & IOLINK_MC_RW_MASK) == 0U);
                /* Every ISDU read is answered from the ISR, which owns the
                 * response segment index and serves it from the snapshot the
                 * main pump refreshes when the service completes. This keeps
                 * t_A within 10 bit times (no main-loop round-trip). */
                const bool isdu_read = isdu && ((f[0] & IOLINK_MC_RW_MASK) != 0U);
                /* A pre-cached (TYPE_2_V) reply is answered from the ISR for the
                 * process channel, an ISDU write ACK and any ISDU read. Other
                 * types/channels are answered by the main pump after its own
                 * validation. An eager reply is never sent to a corrupt or
                 * illegal frame. */
                const bool eager = type12 && dll_phy_has_tx(ctx) &&
                                   ((ch == IOLINK_MC_CHANNEL_PROCESS) || isdu_write || isdu_read);
                ctx->isr_eager_replied = 0U;
                bool eager_ok = eager;
                if (eager && !allowed) {
                    ctx->isr_eager_rejects++;
                    eager_ok = false; /* main pump validates/falls back */
                }
                ctx->isr_frame_len = len;
                ctx->isr_frame_ptr = f;    /* publish pointer (zero-copy) */
                ctx->isr_frame_ready = 1U; /* publish last */
                if (eager_ok) {
                    if (dll_isr_start_reply(ctx)) {
                        ctx->isr_eager_replied = 1U;
                    }
                }
            }
            break;
        }
#else
        uint8_t data = 0U;
        uint8_t st = 0U;
        int ok = ctx->phy->rx_octet(ctx->phy->user, &data, &st);

        if ((st & IOLINK_PHY_STATUS_CHK) != 0U) {
            /* The gap/CHK is a timeout (Timer A), not the normal frame end.
             * A frame that did not reach its known length is truncated or
             * lost: drop it instead of feeding a partial M-sequence to the
             * stack. Completed frames already reset isr_rx_cnt. */
            const bool incomplete_ms = (ctx->isr_rx_expect != 0U) &&
                                       (ctx->isr_rx_cnt >= IOLINK_M_SEQ_HEADER_LEN) &&
                                       (ctx->isr_rx_cnt < ctx->isr_rx_expect);
            if ((ctx->isr_rx_cnt < IOLINK_M_SEQ_HEADER_LEN) || incomplete_ms) {
                if (incomplete_ms) {
                    ctx->framing_errors++;
                }
                ctx->isr_rx_cnt = 0U;
                ctx->isr_rx_expect = 0U;
                if ((ctx->isr_tx_active == 0U) && (ctx->phy->tx_done != NULL)) {
                    ctx->phy->tx_done(ctx->phy->user);
                }
                break;
            }
            bool drop = false;
            if (ctx->isr_rx_cnt != 0U) {
                const bool isdu_write =
                    (((ctx->isr_rx_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) &&
                     ((ctx->isr_rx_buf[0] & IOLINK_MC_RW_MASK) == 0U));
                if (isdu_write) {
                    /* Buffer only a valid request: a corrupt ISDU write must not
                     * reach the transport (ST validates before StackBaseOdWrite). */
                    if (dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_cnt,
                                                  ctx->isr_rx_acc)) {
                        dll_isr_consume_isdu_write(ctx, ctx->isr_rx_buf, ctx->isr_rx_cnt);
                    }
                    else {
                        ctx->isr_eager_rejects++;
                        drop = true;
                    }
                }
                if (!drop) {
                    bool handled = false;
                    if (dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_cnt,
                                                  ctx->isr_rx_acc)) {
                        handled = dll_isr_handle_type0(ctx, ctx->isr_rx_buf, ctx->isr_rx_cnt);
                    }
                    if (!handled) {
                        dll_publish_isr_frame(ctx, ctx->isr_rx_cnt);
                    }
                }
            }
            ctx->isr_rx_cnt = 0U;
            ctx->isr_rx_expect = 0U;
            /* Release the transceiver's receive engine unless the Type-0 fast
             * path already started a reply (the TX drain calls tx_done). */
            if ((ctx->isr_tx_active == 0U) && (ctx->phy->tx_done != NULL)) {
                ctx->phy->tx_done(ctx->phy->user);
            }
            break;
        }

        if (ok < 0) {
            /* Transport error: discard the partial frame and count it. */
            ctx->framing_errors++;
            ctx->isr_rx_cnt = 0U;
            ctx->isr_rx_expect = 0U;
            break;
        }
        if (ok == 0) {
            break;
        }

        if ((ctx->isr_rx_cnt == 0U) && (ctx->phy->response_timer_start != NULL)) {
            ctx->phy->response_timer_start(ctx->phy->user); /* first octet -> Timer A */
        }
        if (ctx->isr_rx_cnt == 0U) {
            ctx->isr_rx_acc = 0x52U; /* A.1.6 seed (start of frame) */
        }
        if (ctx->isr_rx_cnt < sizeof(ctx->isr_rx_buf)) {
            uint8_t b = data;
            if (ctx->isr_rx_cnt == 1U) {
                b = (uint8_t) (b & IOLINK_MSEQ_TYPE_MASK); /* CKT type bits only */
            }
            ctx->isr_rx_acc = (uint8_t) (ctx->isr_rx_acc ^ b);
            ctx->isr_rx_buf[ctx->isr_rx_cnt] = data;
            ctx->isr_rx_cnt++;
        }

        /* The second octet (CKT) completes the header: derive the request length
         * for the multi-octet (Type-1/2) types. A Type-0 request (2..3 octets) is
         * left to the CHK frame-end indication above, matching the byte engine. */
        if (ctx->isr_rx_cnt == 2U) {
            /* The CKT type bits identify the M-sequence type. Type-0 has no
             * length field but its size is fixed by MC/CKT (read 2, OD write
             * 3); the 13 Tbit gap is only a timeout, never the normal end. */
            const uint8_t mc = (uint8_t) ctx->isr_rx_buf[0];
            const uint8_t ckt = (uint8_t) ctx->isr_rx_buf[1];
            if ((ckt & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0) {
                const bool wr = ((mc & IOLINK_MC_RW_MASK) == 0U);
                const uint8_t ch = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);
                ctx->isr_rx_expect =
                    (wr && ((ch == IOLINK_MC_CHANNEL_PAGE) ||
                            (ch == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                            (ch == IOLINK_MC_CHANNEL_ISDU))) ? 3U : 2U;
            }
            else {
                ctx->isr_rx_expect = iolink_dll_expected_req_len(ctx, mc);
            }
        }

        if ((ctx->isr_rx_expect != 0U) && (ctx->isr_rx_cnt >= ctx->isr_rx_expect)) {
            const uint8_t ch =
                (uint8_t) (ctx->isr_rx_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK);
            const bool isdu = (ch == IOLINK_MC_CHANNEL_ISDU);
            const bool isdu_write =
                isdu && ((ctx->isr_rx_buf[0] & IOLINK_MC_RW_MASK) == 0U);
            const bool isdu_read = isdu && ((ctx->isr_rx_buf[0] & IOLINK_MC_RW_MASK) != 0U);
            if (isdu_write) {
                if (dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect,
                                              ctx->isr_rx_acc)) {
                    dll_isr_consume_isdu_write(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect);
                }
                else {
                    ctx->isr_eager_rejects++;
                    ctx->isr_rx_cnt = 0U;
                    ctx->isr_rx_expect = 0U;
                    break;
                }
            }
            /* Type-0 fast path: reply from the ISR as soon as the known length
             * is complete (no 13 Tbit gap wait). Gate on the received CKT type
             * so Type-1/2 frames pay nothing extra. */
            const bool type0 =
                ((ctx->isr_rx_buf[1] & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0);
            if (type0 &&
                dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect,
                                          ctx->isr_rx_acc) &&
                dll_isr_handle_type0(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect)) {
                ctx->isr_rx_cnt = 0U;
                ctx->isr_rx_expect = 0U;
                break;
            }
            /* Type-1/2 only: a Type-0 frame the fast path did not handle
             * (startup/transition/parameter writes) belongs to the main pump. */
            const bool eager = !type0 && dll_phy_has_tx(ctx) &&
                               ((ch == IOLINK_MC_CHANNEL_PROCESS) || isdu_write || isdu_read);
            ctx->isr_eager_replied = 0U;
            const bool allowed =
                !eager || dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect,
                                                    ctx->isr_rx_acc);
            if (eager && !allowed) {
                ctx->isr_eager_rejects++;
            }
            /* Start the eager reply BEFORE the mailbox copy: the first reply
             * octet must not wait for the 30-byte publish, and a full mailbox
             * must not suppress the reply. The reply payload is built
             * synchronously from isr_rx_buf (header) and pd_in. */
            if (allowed && eager) {
                ctx->isr_frame_ptr = ctx->isr_rx_buf;
                ctx->isr_frame_len = ctx->isr_rx_expect;
                if (dll_isr_start_reply(ctx)) {
                    ctx->isr_eager_replied = 1U;
                }
            }
            /* Publish even when the eager validation failed: the main pump owns
             * the error/fallback accounting for an invalid frame. */
            (void) dll_publish_isr_frame(ctx, ctx->isr_rx_expect);
            ctx->isr_rx_cnt = 0U;
            ctx->isr_rx_expect = 0U;
            break;   /* frame done; the reply drains on later interrupts */
        }
#endif
    }
}

void iolink_dll_process_isr(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->in_isr = 1U;
    dll_isr_process(ctx);
    ctx->in_isr = 0U;
}

#endif /* IOLINK_PHY_FEATURE_ISR_PUMP */

void iolink_dll_on_rx_timeout(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
#if IOLINK_PHY_FEATURE_ISR_PUMP
    ctx->timeout_errors++;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    ctx->isr_rx_cnt = 0U;
    ctx->isr_rx_expect = 0U;
#endif
    if ((ctx->phy != NULL) && (ctx->phy->response_timer_stop != NULL)) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
#endif
}

void iolink_dll_on_stack_tick(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
#if IOLINK_PHY_FEATURE_ISR_PUMP
    ctx->stack_tick_pending = 1U;
#endif
}

uint8_t iolink_dll_can_run_user_code(iolink_dll_ctx_t* ctx)
{
#if IOLINK_PHY_FEATURE_ISR_PUMP
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return 0U;
    }
    if (ctx->phy->stack_timer_start == NULL) {
        return 1U; /* no hardware window: legacy free-running main */
    }
    if (ctx->stack_tick_pending == 0U) {
        return 0U;
    }
    ctx->stack_tick_pending = 0U;
    return 1U;
#else
    (void) ctx;
    return 1U;
#endif
}
