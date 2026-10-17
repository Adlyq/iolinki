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
    if (len > sizeof(ctx->isr_tx_payload)) {
        return;
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if (ctx->phy->tx_frame == NULL) {
        return;
    }
    if (ctx->lock != NULL) {
        ctx->lock(ctx->lock_user);
    }
    (void) memcpy(ctx->isr_tx_payload, data, len);
    ctx->isr_tx_len = (uint8_t) len;
    (void) ctx->phy->tx_frame(ctx->phy->user, ctx->isr_tx_payload, len);
    if (ctx->unlock != NULL) {
        ctx->unlock(ctx->lock_user);
    }
#else
    if (ctx->phy->tx_octet == NULL) {
        return;
    }
    if (ctx->lock != NULL) {
        ctx->lock(ctx->lock_user);
    }
    if (ctx->isr_tx_active != 0U) {
        if (ctx->phy->tx_done != NULL) {
            ctx->phy->tx_done(ctx->phy->user);
        }
        ctx->isr_tx_active = 0U;
    }
    (void) memcpy(ctx->isr_tx_payload, data, len);
    ctx->isr_tx_len = (uint8_t) len;
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_has_cks = 0U; /* verbatim: the bytes carry the CKS */
    ctx->isr_tx_active = 1U;
    dll_isr_tx_step(ctx);
    if (ctx->unlock != NULL) {
        ctx->unlock(ctx->lock_user);
    }
#endif
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
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_FRAMING,
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
                od_out[i] = iolink_events_memory_read(&ctx->events, (uint8_t) (addr + i));
            }
        }
        else if (od_len > 0U) {
            iolink_events_memory_write(&ctx->events, addr, od_in[0]);
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
            iolink_isdu_od_write(&ctx->isdu, flowctrl, od_in, od_len);
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
    if (iolink_events_flag(&ctx->events)) {
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
    if (iolink_events_flag(&ctx->events)) {
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
    if (ctx->cyclic_reply_precached &&
        ((ctx->frame_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PROCESS)) {
        /* The ISR pump already transmitted the pre-staged cyclic reply for the
         * process channel at frame end. */
        ctx->fallback_count = 0U;
        ctx->last_response_us = iolink_time_get_us();
        ctx->isr_eager_replied = 0U;
        return;
    }
    if (ctx->isr_eager_replied != 0U) {
        /* The ISR answered this frame (write ACK / FlowCTRL-IDLE read); refresh
         * the staged OD for the next poll and do not send a second reply. */
        if (!write && (resp_od > 0U)) {
            for (uint8_t i = 0U; i < resp_od; i++) {
                ctx->last_od_resp[i] = od_out[i];
            }
            iolink_dll_prepare_cyclic_reply(ctx);
        }
        ctx->isr_eager_replied = 0U;
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
        memcpy(&resp[pos], ctx->pd_in, ctx->pd_in_len_current);
        pos += ctx->pd_in_len_current;
    }

    uint8_t cks = 0x00U;
    if (iolink_events_flag(&ctx->events)) {
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
                iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
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

    if (ctx->phy->get_voltage_mv != NULL) {
        int voltage = ctx->phy->get_voltage_mv(ctx->phy->user);
        /* A negative reading means "measurement unavailable", not a fault
           (phy_generic's template hook returns -1). 18 V to 30 V inclusive is
           the valid range; Table D.1 codes 0x5111/0x5110 report under/overrun. */
        if (voltage >= 0) {
            if (voltage < 18000) {
                ctx->voltage_faults++;
                /* Table D.1 device code 0x5111 "Primary supply voltage underrun". */
                iolink_event_trigger(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN,
                                     IOLINK_EVENT_TYPE_WARNING);
            }
            else if (voltage > 30000) {
                ctx->voltage_faults++;
                /* Table D.1 device code 0x5110 "Primary supply voltage overrun". */
                iolink_event_trigger(&ctx->events, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN,
                                     IOLINK_EVENT_TYPE_WARNING);
            }
        }
    }

    if (ctx->phy->is_short_circuit != NULL) {
        if (ctx->phy->is_short_circuit(ctx->phy->user)) {
            ctx->short_circuits++;
            /* Table D.1 device code 0x7710 "Short circuit". */
            iolink_event_trigger(&ctx->events, IOLINK_EVENTCODE_SHORT_CIRCUIT,
                                 IOLINK_EVENT_TYPE_ERROR);
        }
    }
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

    ctx->pd_in_len_current = ctx->pd_in_len;
    ctx->pd_out_len_current = ctx->pd_out_len;
    ctx->pd_in_len_max = ctx->pd_in_len;
    ctx->pd_out_len_max = ctx->pd_out_len;
    ctx->pd_valid = true;

    ctx->baudrate = IOLINK_BAUDRATE_COM2;
    if (ctx->phy->set_baudrate != NULL) {
        ctx->phy->set_baudrate(ctx->phy->user, IOLINK_BAUDRATE_COM2);
    }

    iolink_events_init(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
    iolink_ds_init(&ctx->ds, NULL);
    ctx->isdu.event_ctx = &ctx->events;
    ctx->isdu.dll_ctx = ctx;
    ctx->isdu.ds_ctx = &ctx->ds;

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
    if (ctx->phy->detect_wakeup != NULL) {
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
static void dll_process_complete_frame(iolink_dll_ctx_t* ctx)
{
    uint64_t now_us_proc = iolink_time_get_us();
    if ((ctx->enforce_timing) && (ctx->min_cycle_time_us > 0U) &&
        (ctx->last_cycle_start_us != 0U)) {
        if (now_us_proc - ctx->last_cycle_start_us < (uint64_t) ctx->min_cycle_time_us) {
            ctx->timing_errors++;
            ctx->t_cycle_violations++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                 IOLINK_EVENT_TYPE_WARNING);
        }
    }
    ctx->last_cycle_start_us = now_us_proc;

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
                if (iolink_events_flag(&ctx->events)) {
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
        ctx->framing_errors++;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_GENERAL,
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
}

void iolink_dll_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    /* Process acyclic ISDU state machine */
    iolink_isdu_process(&ctx->isdu);

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
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
        }
        return;
    }

    if (ctx->phy_mode == IOLINK_PHY_MODE_SIO) {
        if ((ctx->frame_index == 0U) && (ctx->phy->detect_wakeup != NULL)) {
            if (ctx->phy->detect_wakeup(ctx->phy->user) > 0) {
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
                iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
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
        uint8_t n = ctx->isr_frame_len;
        if (n > sizeof(ctx->frame_buf)) {
            n = (uint8_t) sizeof(ctx->frame_buf);
        }
        (void) memcpy(ctx->frame_buf, (const void*) ctx->isr_frame_ptr, n);
        ctx->frame_index = n;
        ctx->req_len = n;
        ctx->isr_frame_ready = 0U;
        dll_process_complete_frame(ctx);
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
                    iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
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
/** @brief Map a pre-staged reply variant to its slot, or -1 if not pre-staged. */
static int dll_variant_slot(iolink_reply_variant_t variant)
{
    switch (variant) {
        case IOLINK_REPLY_CYCLIC_READ:
            return 0;
        case IOLINK_REPLY_CYCLIC_WRITE:
            return 1;
        case IOLINK_REPLY_ACYCLIC_READ:
            return 2;
        default:
            return -1;
    }
}


/* Build one OPERATE reply PAYLOAD (no CKS) into the inactive slot of its double
 * buffer and flip the active index. The CKS octet is appended by the ISR pump
 * while transmitting. */
static void dll_build_variant(iolink_dll_ctx_t* ctx, iolink_reply_variant_t variant)
{
    const int slot = dll_variant_slot(variant);
    if (slot < 0) {
        return;
    }
    const uint8_t nxt = (uint8_t) (1U - ctx->cyclic_stage_active[slot]);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* The DMA transmits the active slot directly; do not rewrite the buffer it
     * is still reading. The rebuild is retried on the next main-pump pass. */
    if ((ctx->isr_tx_busy != 0U) && (slot == (int) ctx->isr_tx_variant) &&
        (nxt == ctx->isr_tx_buffer)) {
        return;
    }
#endif
    uint8_t* out = ctx->cyclic_stage[slot][nxt];
    size_t pos = 0U;

    /* Device READ reply layout (Figure A.9/A.14): [OD...][PD-in...]CKS. The
     * process-channel OD is idle (the master polls it with FlowCTRL IDLE); the
     * OD-channel read carries the staged response. A WRITE reply (Figure A.14)
     * carries no OD: [PD-in...]CKS. */
    if (variant == IOLINK_REPLY_CYCLIC_READ) {
        for (uint8_t i = 0U; i < ctx->od_width; i++) {
            out[pos++] = 0x00U;
        }
    }
    else if (variant == IOLINK_REPLY_ACYCLIC_READ) {
        for (uint8_t i = 0U; i < ctx->od_width; i++) {
            out[pos++] = ctx->last_od_resp[i];
        }
    }
    if (ctx->pd_in_len_current > 0U) {
        (void) memcpy(&out[pos], ctx->pd_in, ctx->pd_in_len_current);
        pos += ctx->pd_in_len_current;
    }

    uint8_t flags = 0x00U;
    if (iolink_events_flag(&ctx->events)) {
        flags |= 0x80U;
    }
    if (!ctx->pd_valid) {
        flags |= 0x40U;
    }

    ctx->cyclic_stage_len[slot][nxt] = (uint8_t) pos;
    ctx->cyclic_stage_flags[slot][nxt] = flags;
    /* Flip the active index last: the ISR pump copies the active slot at frame
     * end, so the payload it reads is always fully written. */
    ctx->cyclic_stage_active[slot] = nxt;
}

void iolink_dll_prepare_cyclic_reply(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || !ctx->cyclic_reply_precached || !dll_phy_has_tx(ctx)) {
        return;
    }

    /* Pre-build every OPERATE variant so the ISR pump can transmit the matching
     * one at frame end without running the main pump. */
    dll_build_variant(ctx, IOLINK_REPLY_CYCLIC_READ);
    dll_build_variant(ctx, IOLINK_REPLY_CYCLIC_WRITE);
    dll_build_variant(ctx, IOLINK_REPLY_ACYCLIC_READ);
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
    uint8_t acc = 0x52U; /* A.1.6 seed */
    for (uint8_t i = 0U; i < len; i++) {
        uint8_t b = frame[i];
        if (i == 1U) {
            b = (uint8_t) (b & IOLINK_MSEQ_TYPE_MASK);
        }
        acc = (uint8_t) (acc ^ b);
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

/* Copy the accumulated RX octets into the mailbox for the main pump. Called from
 * the ISR; drops the frame (returns false) if the previous one has not been
 * consumed yet. */
static bool dll_publish_isr_frame(iolink_dll_ctx_t* ctx, uint8_t len)
{
    if ((ctx->isr_frame_ready != 0U) || (len > sizeof(ctx->isr_frame_buf))) {
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

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
/* Send one step of the active reply: the next payload octet, then the computed
 * CKS (CKS mode), then the frame end. One call per transceiver request. */
static void dll_isr_tx_step(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_tx_idx < ctx->isr_tx_len) {
        const uint8_t b = ctx->isr_tx_payload[ctx->isr_tx_idx];
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
        ctx->isr_tx_active = 0U;
    }
}
#endif /* IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE */

/* Select the pre-staged OPERATE reply payload matching the just-framed request
 * and start transmitting it from the ISR. TX BYTE: the CKS is computed while the
 * octets are sent (one per transceiver request). TX FRAME: the payload and its
 * CKS are assembled in the staging buffer and handed over in one call. A Type-0
 * request has no pre-staged variant; the main pump answers it. */
static void dll_isr_start_reply(iolink_dll_ctx_t* ctx)
{
    const uint8_t first = (ctx->isr_frame_ptr != NULL) ? ctx->isr_frame_ptr[0] : 0U;
    const iolink_reply_variant_t variant = iolink_dll_reply_variant(ctx, first);
    const int slot = dll_variant_slot(variant);
    if ((slot < 0) || !dll_phy_has_tx(ctx)) {
        return;
    }
    const uint8_t active = ctx->cyclic_stage_active[slot];
    const uint8_t len = ctx->cyclic_stage_len[slot][active];
    const uint8_t flags = ctx->cyclic_stage_flags[slot][active];

    /* FlowCTRL IDLE has no ISDU service: answer No Service (0x00) instead of the
     * staged Busy (0x01) that a real START read returns while it executes. */
    const bool idle_od_zero =
        ((first & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) &&
        ((first & IOLINK_MC_RW_MASK) != 0U) &&
        ((first & IOLINK_MC_ADDR_MASK) == IOLINK_FLOWCTRL_IDLE) && (ctx->od_width > 0U) &&
        (len > 0U);

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* Zero-copy: hand the pre-staged slot to the DMA directly instead of copying
     * it into isr_tx_payload. Mark the slot busy so the main pump does not rewrite
     * the octets the DMA is still shifting out; it is cleared when the next frame
     * starts (the transfer is long done by then). */
    {
        uint8_t* base = ctx->cyclic_stage[slot][active];
        if (idle_od_zero) {
            base[0] = 0x00U;
        }
        ctx->isr_tx_busy    = 0U; /* the previous transfer is long done */
        ctx->isr_tx_variant = (uint8_t) slot;
        ctx->isr_tx_buffer  = active;
        ctx->isr_tx_busy    = 1U;
        ctx->isr_tx_len     = (uint8_t) (len + 1U);
        ctx->isr_tx_active  = 0U; /* the PHY owns the frame; tx_done completes it */
        if (len == 0U) {
            base[0] =
                (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (0x52U ^ flags)));
            (void) ctx->phy->tx_frame(ctx->phy->user, base, 1U);
        }
        else {
            base[len] = 0U; /* placeholder, filled after the DMA starts */
            (void) ctx->phy->tx_frame(ctx->phy->user, base, ctx->isr_tx_len);
            uint8_t acc = 0x52U; /* A.1.6 seed */
            for (uint8_t i = 0U; i < len; i++) {
                acc = (uint8_t) (acc ^ base[i]);
            }
            base[len] =
                (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
        }
    }
#else
    for (uint8_t i = 0U; i < len; i++) {
        ctx->isr_tx_payload[i] = ctx->cyclic_stage[slot][active][i];
    }
    if (idle_od_zero) {
        ctx->isr_tx_payload[0] = 0x00U;
    }
    ctx->isr_tx_len = len;
    ctx->isr_tx_flags = flags;
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_acc = 0x52U; /* A.1.6 seed */
    ctx->isr_tx_has_cks = 1U;
    ctx->isr_tx_active = 1U;
    dll_isr_tx_step(ctx);
#endif
}

void iolink_dll_process_isr(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || !dll_phy_has_rx(ctx) || (ctx->phy->int_active == NULL)) {
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
            uint8_t st = 0U;
            uint8_t len = 0U;
            const uint8_t* f = ctx->phy->rx_frame(ctx->phy->user, &st, &len);
            if (f == NULL) {
                break;
            }
            (void) st;
            if ((len >= 2U) && (len <= sizeof(ctx->isr_frame_buf)) &&
                (ctx->isr_frame_ready == 0U)) {
                const bool type12 = ((f[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0);
                const uint8_t ch = (uint8_t) (f[0] & IOLINK_MC_COMM_CHANNEL_MASK);
                const bool isdu = (ch == IOLINK_MC_CHANNEL_ISDU);
                const bool isdu_write = isdu && ((f[0] & IOLINK_MC_RW_MASK) == 0U);
                /* An ISDU read is answered from the ISR only for FlowCTRL IDLE
                 * (the staged OD is the constant "no service" 0x00). A START/
                 * COUNT read needs the segment index and stays on the main pump. */
                const bool isdu_idle_read =
                    isdu && ((f[0] & IOLINK_MC_RW_MASK) != 0U) &&
                    ((f[0] & IOLINK_MC_ADDR_MASK) == IOLINK_FLOWCTRL_IDLE);
                /* A pre-cached (TYPE_2_V) reply is answered from the ISR for the
                 * process channel, an ISDU write ACK and an ISDU FlowCTRL-IDLE
                 * read. Other types/channels are answered by the main pump after
                 * its own validation. An eager reply is never sent to a corrupt
                 * or illegal frame. */
                const bool eager =
                    type12 && (ctx->cyclic_reply_precached != 0U) &&
                    ((ch == IOLINK_MC_CHANNEL_PROCESS) || isdu_write || isdu_idle_read);
                ctx->isr_eager_replied = 0U;
                if (eager && !dll_isr_reply_allowed(ctx, f, len)) {
                    break;
                }
                ctx->isr_frame_len = len;
                ctx->isr_frame_ptr = f;    /* publish pointer (zero-copy) */
                ctx->isr_frame_ready = 1U; /* publish last */
                if (eager) {
                    dll_isr_start_reply(ctx);
                    ctx->isr_eager_replied = 1U;
                }
            }
            break;
        }
#else
        uint8_t data = 0U;
        uint8_t st = 0U;
        int ok = ctx->phy->rx_octet(ctx->phy->user, &data, &st);

        if ((st & IOLINK_PHY_STATUS_CHK) != 0U) {
            /* Frame-end time-out / receive error: Type-0 frames have no length in
             * the header, so this delimits them. Publish whatever was accumulated
             * and reset the transceiver's receive engine (as the ST engine does). */
            if (ctx->isr_rx_cnt != 0U) {
                dll_publish_isr_frame(ctx, ctx->isr_rx_cnt);
            }
            ctx->isr_rx_cnt = 0U;
            ctx->isr_rx_expect = 0U;
            if (ctx->phy->tx_done != NULL) {
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
            /* The CKT type bits (not the length) identify a multi-octet
             * M-sequence; a short Type-2 READ is only 3 octets, so a length
             * heuristic cannot distinguish it from a Type-0 frame. */
            const bool multi =
                ((ctx->isr_rx_buf[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0);
            ctx->isr_rx_expect =
                multi ? iolink_dll_expected_req_len(ctx, (uint8_t) ctx->isr_rx_buf[0]) : 0U;
        }

        if ((ctx->isr_rx_expect != 0U) && (ctx->isr_rx_cnt >= ctx->isr_rx_expect)) {
            const uint8_t ch =
                (uint8_t) (ctx->isr_rx_buf[0] & IOLINK_MC_COMM_CHANNEL_MASK);
            const bool isdu = (ch == IOLINK_MC_CHANNEL_ISDU);
            const bool isdu_write =
                isdu && ((ctx->isr_rx_buf[0] & IOLINK_MC_RW_MASK) == 0U);
            const bool isdu_idle_read =
                isdu && ((ctx->isr_rx_buf[0] & IOLINK_MC_RW_MASK) != 0U) &&
                ((ctx->isr_rx_buf[0] & IOLINK_MC_ADDR_MASK) == IOLINK_FLOWCTRL_IDLE);
            const bool eager =
                (ctx->cyclic_reply_precached != 0U) &&
                ((ch == IOLINK_MC_CHANNEL_PROCESS) || isdu_write || isdu_idle_read);
            ctx->isr_eager_replied = 0U;
            const bool allowed =
                !eager || dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect,
                                                    ctx->isr_rx_acc);
            bool published = false;
            if (allowed) {
                published = dll_publish_isr_frame(ctx, ctx->isr_rx_expect);
            }
            ctx->isr_rx_cnt = 0U;
            ctx->isr_rx_expect = 0U;
            if (allowed && published && eager) {
                dll_isr_start_reply(ctx);
                ctx->isr_eager_replied = 1U;
            }
            break;   /* frame done; the reply drains on later interrupts */
        }
#endif
    }
}


#endif /* IOLINK_PHY_FEATURE_ISR_PUMP */
