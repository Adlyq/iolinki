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
 * @brief Return the expected request length for a master frame (Figure A.14).
 *
 * The request is MC + CKT + PD-out + [OD]; the checksum is carried in the CKT
 * octet (A.1.6), so there is no trailing checksum octet. A Type-0 message
 * carries no PD. For the multi-octet types the OD octets are present only on a
 * WRITE access (MC bit 7 = 0); a READ request (MC bit 7 = 1) carries none.
 */
static uint8_t dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t type_bits, uint8_t mc)
{
    if (type_bits == IOLINK_MSEQ_TYPE_0) {
        return 2U;
    }
    const uint8_t od_req = ((mc & IOLINK_MC_RW_MASK) == 0U) ? ctx->od_width : 0U;
    return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current + od_req);
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

/** @brief Return the inter-byte timeout in us for the active baudrate, or 0 when
 *  the library ISR pump delimits frames (rx_octet PHY). */
static uint32_t dll_get_t_byte_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    /* A raw-octet PHY (single-octet transceiver) is framed by the library ISR
     * pump itself (octet count + CKT type, or the transceiver's DAT/CHK frame-end
     * time-out), so the main-loop DLL must not second-guess it with a polled
     * inter-octet timer: the main loop is slower than t_byte and would reset the
     * frame mid-assembly. The byte-wise fallback keeps the t_byte delimiting. */
    if (dll_phy_is_raw_octet(ctx)) {
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

static void dll_isr_tx_step(iolink_dll_ctx_t* ctx);
static void dll_build_variant(iolink_dll_ctx_t* ctx, iolink_reply_variant_t variant);

/** @brief Send a main-context reply (Type-0 in STARTUP/PREOPERATE, or a
 *         streaming PHY).
 *
 * For a raw-octet PHY the library stages @p data verbatim (the CKS is already
 * included) and starts the transmission; the ISR pump feeds the remaining octets
 * as the transceiver requests them. The application lock keeps the setup atomic
 * with respect to the data interrupt (the library does not know the EXTI). For a
 * streaming PHY, send() transmits the bytes directly. */
static void dll_send(iolink_dll_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (data == NULL) || (len == 0U) ||
        (len > sizeof(ctx->isr_tx_payload))) {
        return;
    }
    if (dll_phy_is_raw_octet(ctx)) {
        if (ctx->phy->tx_octet == NULL) {
            return;
        }
        if (ctx->lock != NULL) {
            ctx->lock(ctx->lock_user);
        }
        if (ctx->isr_tx_active != 0U) {
            ctx->phy->tx_end(ctx->phy->user); /* abort a stale transmission */
            ctx->isr_tx_active = 0U;
        }
        (void) memcpy(ctx->isr_tx_payload, data, len);
        ctx->isr_tx_len = (uint8_t) len;
        ctx->isr_tx_idx = 0U;
        ctx->isr_tx_has_cks = 0U; /* verbatim: the bytes carry the CKS */
        ctx->isr_tx_active = 1U;
        dll_isr_tx_step(ctx); /* writes the first octet */
        if (ctx->unlock != NULL) {
            ctx->unlock(ctx->lock_user);
        }
    }
    else if (ctx->phy->send != NULL) {
        ctx->phy->send(ctx->phy->user, data, len);
    }
}

/** @brief Return true while the power-down/PD guard window (t_PD) is still active. */
static bool dll_t_pd_active(const iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->t_pd_deadline_us == 0U)) {
        return false;
    }
    return iolink_time_get_us() < ctx->t_pd_deadline_us;
}

/** @brief Drain all pending RX data from the PHY; returns true if any was seen. */
static bool dll_drain_rx(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return false;
    }
    bool saw_data = false;
    if (dll_phy_is_raw_octet(ctx)) {
        /* Raw-octet PHY: the ISR pump fills the mailbox; draining it just drops
         * a frame that arrived while the t_pd guard was active. */
        if (ctx->isr_frame_ready != 0U) {
            ctx->isr_frame_ready = 0U;
            saw_data = true;
        }
        return saw_data;
    }
    if (ctx->phy->recv_byte != NULL) {
        uint8_t byte = 0U;
        while (ctx->phy->recv_byte(ctx->phy->user, &byte) > 0) {
            saw_data = true;
        }
    }
    return saw_data;
}

/** @brief Whether the bound PHY owns the C/Q line state (wake-up / SIO<->SDCI).
 *
 * Compile-time override IOLINK_PHY_OWNS_LINE_STATE (config.h); otherwise inferred
 * from the PHY: a PHY without a wake-up detector cannot leave SIO, so it is an
 * already-established SDCI link. */
static inline bool dll_phy_owns_line_state(const iolink_dll_ctx_t* ctx)
{
#if IOLINK_PHY_OWNS_LINE_STATE
    (void) ctx;
    return true;
#else
    return !dll_phy_has_wakeup(ctx);
#endif
}

/** @brief Handle a communication failure: count retries and revert toward SIO/STARTUP. */
static void dll_recover_to_idle(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* A PHY that owns the line state stays in SDCI; switching it to SIO would
     * latch it with no way to leave (it never reports a wake-up). */
    if (dll_phy_owns_line_state(ctx)) {
        iolink_dll_set_sdci_mode(ctx);
    }
    else {
        iolink_dll_set_sio_mode(ctx);
    }
}

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
        dll_recover_to_idle(ctx);
        iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->fallback_count = 0U;
        ctx->frame_index = 0U;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_FRAMING,
                             IOLINK_EVENT_TYPE_WARNING);
    }
    else if (ctx->state != IOLINK_DLL_STATE_OPERATE && ctx->state != IOLINK_DLL_STATE_ESTAB_COM) {
        iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    }
}

/**
 * @brief Arm the FALLBACK transition (MasterCommand 0x5A, Table B.2/43).
 *
 * The device shall switch to SIO after T_FBD = 3 MasterCycleTimes and within at
 * most 500 ms (Table 43). The Master's actual cycle time (DPP1 0x01, Table B.3)
 * is authoritative once written; fall back to the device MinCycleTime before
 * then. The deadline is enforced in iolink_dll_process(). No reply is sent here:
 * the caller builds it (Type-0 -> CKS only, OPERATE Type-2 -> [PD-in]CKS), so
 * the command works in every state via dll_apply_master_command(). */
static void dll_arm_fallback(iolink_dll_ctx_t* ctx)
{
    uint32_t cycle_us = iolink_cycle_time_to_us(ctx->master_cycle_time);
    if (cycle_us == 0U) {
        cycle_us = ctx->min_cycle_time_us;
    }
    uint32_t t_fbd_ms = 500U;
    if (cycle_us > 0U) {
        t_fbd_ms = (3U * cycle_us) / 1000U;
        if (t_fbd_ms > 500U) {
            t_fbd_ms = 500U;
        }
    }
    ctx->fallback_deadline_ms = iolink_time_get_us() / 1000U + t_fbd_ms;
    dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
}

/**
 * @brief Apply a MasterCommand written to Direct Parameter address 0 (Table B.2).
 *
 * State mapping (Table B.2 / Figure 54): 0x5A -> FALLBACK (arm T_FBD), 0x97 ->
 * STARTUP, 0x9A -> PREOPERATE, 0x98 -> ESTAB_COM with PD valid, 0x99 -> ESTAB_COM
 * with PD invalid, 0x95/0x96 -> acknowledged without state change. Returns true
 * for a known command. The reply is built by the caller (Figure A.5 Type-0
 * CKS-only, or the OPERATE Type-2 [PD-in]CKS when the command arrives inside the
 * cyclic frame).
 */
static bool dll_apply_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_DEVICE_STARTUP: /* 0x97 */
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            break;
        case IOLINK_CMD_DEVICE_PREOPERATE: /* 0x9A */
            dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
            break;
        case IOLINK_CMD_PROCESS_DATA_OPERATE: /* 0x98: PD output valid */
            ctx->pd_valid = true;
            dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
            break;
        case IOLINK_CMD_DEVICE_OPERATE: /* 0x99: PD output invalid/missing */
            ctx->pd_valid = false;
            dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
            break;
        case IOLINK_CMD_FALLBACK: /* 0x5A: arm the SIO transition (Table 43) */
            dll_arm_fallback(ctx);
            break;
        case IOLINK_CMD_MASTER_IDENT: /* 0x95 */
        case IOLINK_CMD_DEVICE_IDENT: /* 0x96 */
            break; /* acknowledged, no state change */
        default:
            return false; /* not a MasterCommand */
    }
    ctx->fallback_count = 0U;
    return true;
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
           octet at the address; unimplemented addresses return 0 (A.1.2). A
           write to address 0 carries a MasterCommand (Table B.2); address 1 is
           MasterCycleTime (Table B.1, R/W). */
        const uint8_t addr = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            if (od_len > 0U) {
                od_out[0] = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
            }
        }
        else if (od_len > 0U) {
            if (addr == 0x00U) {
                (void) dll_apply_master_command(ctx, od_in[0]);
            }
            else if (addr == 0x01U) {
                ctx->master_cycle_time = od_in[0];
            }
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
    /* Type-1/2 master message: MC | CKT | PD-out | [OD]; the checksum is in CKT.
       Figure A.14: the OD octets are present only on a WRITE (MC bit 7 = 0). */
    const uint8_t mc = ctx->frame_buf[0];
    const bool write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    uint8_t req_od = write ? ctx->od_width : 0U;
    uint8_t resp_od = write ? 0U : ctx->od_width;
    if (req_od > IOLINK_OD_MAX_SIZE) {
        req_od = IOLINK_OD_MAX_SIZE;
    }
    if (resp_od > IOLINK_OD_MAX_SIZE) {
        resp_od = IOLINK_OD_MAX_SIZE;
    }

    uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    uint16_t od_offset = (uint16_t) (pd_offset + ctx->pd_out_len_current);

    if (ctx->pd_out_len_current > 0U) {
        memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->pd_out_len_current);
    }

    uint8_t od_in[IOLINK_OD_MAX_SIZE];
    uint8_t od_out[IOLINK_OD_MAX_SIZE];
    (void) memset(od_in, 0, sizeof(od_in));
    (void) memset(od_out, 0, sizeof(od_out));
    if (req_od > 0U) {
        memcpy(od_in, &ctx->frame_buf[od_offset], req_od);
    }

    /* dll_dispatch_od interprets od_len as the request width on a WRITE and as
     * the reply width on a READ, because for a READ the OD octets carry the
     * device response (an ISDU response, a Direct-Parameter octet, or an event
     * memory octet). Pass req_od for a WRITE and resp_od for a READ so an
     * acyclic ISDU / DPP read in OPERATE is actually served (7.3.6.2, A.1.2). */
    const uint8_t dispatch_od = write ? req_od : resp_od;
    if (!dll_dispatch_od(ctx, mc, od_in, dispatch_od, od_out)) {
        for (uint16_t i = 0; i < dispatch_od; i++) {
            od_out[i] = 0U;
        }
    }

    if (ctx->cyclic_reply_precached) {
        /* OPERATE with a raw-octet PHY: the library ISR pump transmits the whole
         * reply at frame end, so the main pump never sends here. A process-channel
         * request uses the pre-built cyclic payload; a non-cyclic OD read uses the
         * staged response (refreshed below); a non-cyclic write reply is [PD-in]
         * (the cyclic-write payload). */
        if (!write && ((mc & IOLINK_MC_COMM_CHANNEL_MASK) != IOLINK_MC_CHANNEL_PROCESS)) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                ctx->last_od_resp[i] = od_out[i];
            }
            dll_build_variant(ctx, IOLINK_REPLY_ACYCLIC_READ);
        }
        ctx->fallback_count = 0U;
        ctx->last_response_us = iolink_time_get_us();
        return;
    }

    /* Device reply layout (Figure A.9/A.10/A.13/A.14): the On-request Data octets
       come FIRST, then the Process Data In octets, then the CKS octet:
           [OD...][PD-In...]CKS
       This mirrors the master message, which places its OD after the PD-out. The
       CKS carries the Event flag (bit 7), the PD-validity flag (bit 6, 1 = invalid)
       and the 6-bit message checksum (bits 0-5); there is no leading status octet.
       A WRITE reply carries no OD (Figure A.14). */
    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + IOLINK_OD_MAX_SIZE + 1];
    uint16_t pos = 0U;
    for (uint8_t i = 0U; i < resp_od; i++) {
        resp[pos] = od_out[i];
        pos++;
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

    if (dll_phy_is_raw_octet(ctx) || (ctx->phy->send != NULL)) {
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

/** @brief Library default fault -> IO-Link event mapping.
 *
 * Keeps the historical codes so an existing device is unaffected; a device that
 * declares its own events (its IODD) overrides this with
 * iolink_dll_set_fault_map().
 *
 * The codes are the standardized *device* EventCodes of Table D.1 (the
 * 0x18xx range is vendor-specific and 0x1805/0x1806 are the master/port codes
 * "PHY overtemperature" / "Short circuit at L+", so they must not be used for
 * device faults). A device that declares its own IODD overrides this with
 * iolink_dll_set_fault_map(). */
static const iolink_dll_fault_map_t g_default_fault_map = {
    .undervoltage = {IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, IOLINK_EVENT_TYPE_WARNING}, /* 0x5111 */
    .overtemp = {IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD, IOLINK_EVENT_TYPE_ERROR},          /* 0x4000 */
    .short_circuit = {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR},            /* 0x7710 */
};

/** @brief Announce fault *transitions* as IO-Link events.
 *
 * Implements the dispatcher rules of A.6.4: an "appears" is issued once when a
 * fault starts and is not repeated while it persists (the Event flag already
 * tells the master to re-read), and the matching "disappears" is issued when the
 * fault ends, but never within IOLINK_EVENT_MIN_APPEAR_MS of the "appears" - a
 * shorter glitch keeps the fault announced until the window has elapsed. */
static void dll_dispatch_fault_events(iolink_dll_ctx_t* ctx, uint8_t faults, uint32_t now_ms)
{
    const iolink_dll_fault_event_t* const map[3] = {&ctx->fault_map.undervoltage,
                                                    &ctx->fault_map.overtemp,
                                                    &ctx->fault_map.short_circuit};
    for (uint8_t i = 0U; i < 3U; i++) {
        uint8_t bit = (uint8_t) (1U << i);
        bool present = (faults & bit) != 0U;
        bool reported = (ctx->faults_reported & bit) != 0U;

        if (present && !reported) {
            ctx->faults_reported |= bit;
            ctx->fault_appears_ms[i] = now_ms;
            if (map[i]->code != 0U) {
                iolink_event_trigger_mode(&ctx->events, map[i]->code, map[i]->type,
                                          IOLINK_EVENT_MODE_APPEARS);
            }
        }
        else if (!present && reported) {
            if ((uint32_t) (now_ms - ctx->fault_appears_ms[i]) < IOLINK_EVENT_MIN_APPEAR_MS) {
                continue; /* too soon: retry on a later poll */
            }
            ctx->faults_reported &= (uint8_t) ~bit;
            if (map[i]->code != 0U) {
                iolink_event_trigger_mode(&ctx->events, map[i]->code, map[i]->type,
                                          IOLINK_EVENT_MODE_DISAPPEARS);
            }
        }
        else {
            /* present && reported : already announced (de-duplicated).
               !present && !reported : nothing to do. */
        }
    }
}

/** @brief Poll the PHY diagnostics: count faults every cycle, report the edges.
 *
 * A PHY that implements get_faults() states the physical condition directly; the
 * legacy get_voltage_mv() / is_short_circuit() hooks are used otherwise, so
 * existing PHYs keep working. The counters accumulate while a fault is present;
 * how the fault is reported on the wire is the device's fault map. */
static void dll_poll_diagnostics(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    uint8_t faults = 0U;
    if (ctx->phy->get_faults != NULL) {
        faults = (uint8_t) (ctx->phy->get_faults(ctx->phy->user) & IOLINK_PHY_FAULT_ALL);
    }
    else {
        if (ctx->phy->get_voltage_mv != NULL) {
            int voltage_mv = ctx->phy->get_voltage_mv(ctx->phy->user);
            /* A negative reading means "measurement unavailable", not a fault. */
            if ((voltage_mv >= 0) && ((voltage_mv < IOLINK_PHY_VOLTAGE_MIN_MV) ||
                                      (voltage_mv > IOLINK_PHY_VOLTAGE_MAX_MV))) {
                faults |= IOLINK_PHY_FAULT_UNDERVOLTAGE;
            }
        }
        if ((ctx->phy->is_short_circuit != NULL) &&
            ctx->phy->is_short_circuit(ctx->phy->user)) {
            faults |= IOLINK_PHY_FAULT_SHORT_CIRCUIT;
        }
    }

    if ((faults & IOLINK_PHY_FAULT_UNDERVOLTAGE) != 0U) {
        ctx->voltage_faults++;
    }
    if ((faults & IOLINK_PHY_FAULT_OVERTEMP) != 0U) {
        ctx->overtemp_faults++;
    }
    if ((faults & IOLINK_PHY_FAULT_SHORT_CIRCUIT) != 0U) {
        ctx->short_circuits++;
    }

    dll_dispatch_fault_events(ctx, faults, iolink_time_get_ms());
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

    /* The OD width is derived from the M-sequence type once it is known:
     * device_apply_stack_config() sets it from the stack configuration. A bare
     * init leaves it at 0. */
    ctx->od_width = 0U;

    ctx->pd_in_len_current = ctx->pd_in_len;
    ctx->pd_out_len_current = ctx->pd_out_len;
    ctx->pd_in_len_max = ctx->pd_in_len;
    ctx->pd_out_len_max = ctx->pd_out_len;
    ctx->pd_valid = true;

    ctx->baudrate = IOLINK_BAUDRATE_COM2;
    ctx->operating_baudrate = IOLINK_BAUDRATE_COM2;
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
    ctx->fault_map = g_default_fault_map;

    /* OPERATE acyclic OD response starts as Busy (0x01, Table A.14) until the
     * main pump answers the first OD read. */
    ctx->last_od_resp[0] = 0x01U;

    /*
     * Initial line mode depends on whether the PHY can observe the electrical
     * C/Q wake-up pulse:
     *
     *  - PHYs that can (detect_wakeup != NULL, e.g. a transceiver front-end or
     *    the virtual PHY) start in SIO and switch to SDCI when a wake-up is
     *    detected, matching the IO-Link startup handshake.
     *
     *  - PHYs that cannot (detect_wakeup == NULL, e.g. a plain UART sitting
     *    behind a transceiver that has already established COM, or a PHY that
     *    declares IOLINK_PHY_OWNS_LINE_STATE) would otherwise sit in SIO forever
     *    and never service M-sequences. Such a link is, by contract, an
     *    already-established SDCI link, so start in SDCI and accept M-sequences
     *    immediately.
     */
    if (dll_phy_owns_line_state(ctx)) {
        iolink_dll_set_sdci_mode(ctx);
    }
    else {
        iolink_dll_set_sio_mode(ctx);
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
    if ((ctx == NULL) || !ctx->cyclic_reply_precached || (ctx->phy == NULL) ||
        (ctx->phy->tx_octet == NULL)) {
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
        return dll_expected_req_len(ctx, type_bits, mc_byte);
    }
    return 2U;
}

/* Derive the expected request length for the M-sequence whose first octet
 * (the MC byte) is now in ctx->frame_buf[0]. */
static void dll_rx_begin(iolink_dll_ctx_t* ctx)
{
    ctx->req_len = dll_compute_req_len(ctx, ctx->frame_buf[0]);
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

/* Dispatch a complete M-sequence held in ctx->frame_buf (frame_index octets),
 * with req_len already derived by dll_rx_begin(). Leaves frame_index 0. */
static void dll_process_rx_frame(iolink_dll_ctx_t* ctx)
{
    if ((ctx->frame_index == 0U) || (ctx->frame_index < ctx->req_len)) {
        return;
    }

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
        ctx->last_valid_frame_ms = iolink_time_get_ms();   /* heartbeat */
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
            else if (ctx->req_len == 3U) {
                /* Any 3-octet Type-0 OD write in PREOPERATE is a
                   MasterCommand (page address 0), MasterCycleTime, ISDU or
                   diagnosis traffic (7.3.6 allows ISDU before OPERATE). It is a
                   TYPE_0 frame: one OD octet at offset 2 and a CKS-only reply
                   (Figure A.5). dll_dispatch_od applies the MasterCommand. */
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
            if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND) {
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

    /* OPERATE supervision: if the master stops delivering valid frames (stops
       talking, or its frames no longer pass CRC/type), drop back to STARTUP/SIO
       and refresh the PHY so the device does not stay stuck "connected" while
       the master has actually gone offline / is restarting the port. */
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) && (ctx->last_valid_frame_ms != 0U) &&
        ((uint32_t)(now_ms - ctx->last_valid_frame_ms) > IOLINK_OPERATE_TIMEOUT_MS)) {
        ctx->last_valid_frame_ms = 0U;
        ctx->timeout_errors++;
        iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
        dll_recover_to_idle(ctx);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->frame_index = 0U;
    }

    if ((ctx->last_activity_ms != 0U) && (now_ms - ctx->last_activity_ms > 1000U)) {
        ctx->last_activity_ms = 0U; /* Prevent repeated resets */
        ctx->timeout_errors++;      /* Link went idle past the inactivity window */
        if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
            iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
            dll_recover_to_idle(ctx);
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
            iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
            dll_recover_to_idle(ctx);
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
            iolink_dll_set_baudrate(ctx, ctx->operating_baudrate);
            dll_recover_to_idle(ctx);
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
        if ((ctx->frame_index == 0U) && dll_phy_has_wakeup(ctx)) {
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
                /* Inter-octet gap longer than t_byte on a byte-wise PHY: the
                 * partial M-sequence is discarded (the next octet starts a fresh
                 * frame) and, as upstream, the link is treated as broken. Only
                 * reached when the PHY does not delimit frames itself
                 * (t_byte_limit_us is 0 for a raw-octet PHY). */
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

    if (dll_phy_is_raw_octet(ctx)) {
        /* Raw-octet PHY (single-octet transceiver): the library ISR pump has
         * already framed the M-sequence and published it to the mailbox. Consume
         * one frame here and run the protocol on it. */
        if (ctx->isr_frame_ready != 0U) {
            uint8_t flen = ctx->isr_frame_len;
            ctx->isr_frame_ready = 0U;
            if (flen > sizeof(ctx->frame_buf)) {
                ctx->framing_errors++;
            }
            else {
                (void) memcpy(ctx->frame_buf, (const void*) ctx->isr_frame_buf, flen);
                ctx->frame_index = flen;
                ctx->last_activity_ms = iolink_time_get_ms();
                ctx->last_byte_us = iolink_time_get_us();
                dll_rx_begin(ctx);
                dll_process_rx_frame(ctx);
            }
        }
    }
    else if (ctx->phy->recv_byte != NULL) {
        /* Byte-wise PHY (upstream behaviour): reconstruct the M-sequence octet by
         * octet; the t_byte time-out delimits and resynchronises the frame. */
        uint8_t byte;
        for (;;) {
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
            if ((ctx->frame_index > 0U) && (ctx->enforce_timing) &&
                (ctx->t_byte_limit_us > 0U)) {
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
                dll_rx_begin(ctx);
            }
            else if (ctx->frame_index < sizeof(ctx->frame_buf)) {
                ctx->frame_buf[ctx->frame_index] = byte;
                ctx->frame_index++;
            }
            else {
                ctx->frame_index = 0U;
                ctx->framing_errors++;
            }

            dll_process_rx_frame(ctx);
        }
    }
}

/* Copy the accumulated RX octets into the mailbox for the main pump. Called from
 * the ISR; drops the frame if the previous one has not been consumed yet. */
static void dll_publish_isr_frame(iolink_dll_ctx_t* ctx, uint8_t len)
{
    if ((ctx->isr_frame_ready != 0U) || (len > sizeof(ctx->isr_frame_buf))) {
        return;
    }
    for (uint8_t i = 0U; i < len; i++) {
        ctx->isr_frame_buf[i] = ctx->isr_rx_buf[i];
    }
    ctx->isr_frame_len = len;
    ctx->isr_frame_ready = 1U;   /* publish last */
}

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
        if (ctx->phy->tx_end != NULL) {
            ctx->phy->tx_end(ctx->phy->user);
        }
        ctx->isr_tx_active = 0U;
    }
}

/* Select the pre-staged OPERATE reply payload matching the just-framed request
 * and start transmitting it from the ISR (the CKS is computed as the octets are
 * sent). A Type-0 request has no pre-staged variant; the main pump answers it. */
static void dll_isr_start_reply(iolink_dll_ctx_t* ctx)
{
    const iolink_reply_variant_t variant =
        iolink_dll_reply_variant(ctx, (uint8_t) ctx->isr_frame_buf[0]);
    const int slot = dll_variant_slot(variant);
    if ((slot < 0) || (ctx->phy->tx_octet == NULL)) {
        return;
    }
    const uint8_t active = ctx->cyclic_stage_active[slot];
    const uint8_t len = ctx->cyclic_stage_len[slot][active];

    for (uint8_t i = 0U; i < len; i++) {
        ctx->isr_tx_payload[i] = ctx->cyclic_stage[slot][active][i];
    }
    ctx->isr_tx_len = len;
    ctx->isr_tx_flags = ctx->cyclic_stage_flags[slot][active];
    ctx->isr_tx_idx = 0U;
    ctx->isr_tx_acc = 0x52U; /* A.1.6 seed */
    ctx->isr_tx_has_cks = 1U;
    ctx->isr_tx_active = 1U;
    dll_isr_tx_step(ctx); /* write the first octet (or the CKS if no payload) */
}

void iolink_dll_process_isr(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || !dll_phy_is_raw_octet(ctx) || (ctx->phy->int_active == NULL)) {
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
        /* Reply draining: the transceiver requests the next reply octet. Feed
         * exactly one and yield (the next request arrives as another interrupt). */
        if (ctx->isr_tx_active != 0U) {
            dll_isr_tx_step(ctx);
            break;
        }

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
            if (ctx->phy->tx_end != NULL) {
                ctx->phy->tx_end(ctx->phy->user);
            }
            break;
        }

        if (ok <= 0) {
            break;
        }

        if (ctx->isr_rx_cnt < sizeof(ctx->isr_rx_buf)) {
            ctx->isr_rx_buf[ctx->isr_rx_cnt] = data;
            ctx->isr_rx_cnt++;
        }

        /* The second octet (CKT) completes the header: derive the request length
         * for the multi-octet (Type-1/2) types. A Type-0 request (2..3 octets) is
         * left to the CHK frame-end indication above, matching the byte engine. */
        if (ctx->isr_rx_cnt == 2U) {
            const uint8_t exp = iolink_dll_expected_req_len(ctx, (uint8_t) ctx->isr_rx_buf[0]);
            ctx->isr_rx_expect = (exp > 3U) ? exp : 0U;
        }

        if ((ctx->isr_rx_expect != 0U) && (ctx->isr_rx_cnt >= ctx->isr_rx_expect)) {
            dll_publish_isr_frame(ctx, ctx->isr_rx_expect);
            ctx->isr_rx_cnt = 0U;
            ctx->isr_rx_expect = 0U;
            dll_isr_start_reply(ctx);
            break;   /* frame done; the reply drains on later interrupts */
        }
    }
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
    out_stats->overtemp_faults = ctx->overtemp_faults;
    out_stats->short_circuits = ctx->short_circuits;
}

void iolink_dll_set_fault_map(iolink_dll_ctx_t* ctx, const iolink_dll_fault_map_t* map)
{
    if (ctx == NULL) {
        return;
    }
    ctx->fault_map = (map != NULL) ? *map : g_default_fault_map;
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
