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

/**
 * @file dll_isr.c
 * @brief Data Link Layer ISR two-pump transport.
 * @ingroup iolinki_dll
 *
 * Runs from the data interrupt: request framing, eager replies, the ISDU
 * snapshot read path and the staged TX pump. The main pump and the frame
 * dispatch live in dll.c; application callbacks must never run here.
 * `iolink_dll_can_run_user_code()` is the one main-context query kept in this
 * file because it belongs to the same Timer B transport state.
 */

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static void dll_isr_tx_step(iolink_dll_ctx_t* ctx);
#endif

/**
 * @brief Start transmitting a staged reply slot.
 *
 * Shared by dll_send()'s direct-send fallback (a PHY without request_service())
 * and the ISR pending-reply path, so both drive the TX hooks identically.
 */
void dll_isr_start_staged_tx(iolink_dll_ctx_t* ctx, uint8_t slot, uint8_t len)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    if (ctx->phy->tx_frame != NULL) {
        if (ctx->phy->tx_frame(ctx->phy->user, ctx->isr_tx_payload[slot], len) != 0) {
            ctx->isr_tx_dropped++;
        }
    }
#else
    (void) len;
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
        ctx->isr_tx_has_cks = 0U;
        ctx->isr_tx_active = 1U;
        dll_isr_tx_step(ctx);
    }
#endif
}

/* -------------------------------------------------------------------------
 * ISR two-pump transport
 *
 * Everything below runs from the data interrupt. The main pump stages replies
 * and hands ISDU snapshots over here; application callbacks must never run in
 * this section.
 * ------------------------------------------------------------------------- */

/* Conformance gate for the immediate ISR reply: a master request must carry a
 * valid A.1.6 checksum AND a legal M-sequence type for the current state before
 * the device answers it. Mirrors the validation done later by
 * dll_process_rx_frame(); the eager reply must never answer a corrupt/illegal
 * frame that the main pump would have rejected. Read-only and ISR-safe.
 *
 * Legal M-sequence type for the current state (same rule as the main pump):
 * STARTUP/PREOPERATE are Type-0; OPERATE (and ESTAB_COM once the transition is
 * done) expect the configured type. */
static bool dll_isr_type_ok(const iolink_dll_ctx_t* ctx, const uint8_t* frame)
{
    return dll_type_allowed(ctx, frame);
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
/* Copy a completed RX frame into the mailbox for the main pump. Called from the
 * ISR; drops the frame (returns false) if the previous one has not been
 * consumed yet. @p f is the assembled frame (isr_rx_buf for the BYTE path). */
static bool dll_publish_isr_frame(iolink_dll_ctx_t* ctx, const uint8_t* f, uint8_t len)
{
    if (ctx->isr_frame_ready != 0U) {
        ctx->isr_mailbox_drops++;
        return false;
    }
    if ((f == NULL) || (len > sizeof(ctx->isr_frame_buf))) {
        ctx->framing_errors++;
        return false;
    }
    for (uint8_t i = 0U; i < len; i++) {
        ctx->isr_frame_buf[i] = f[i];
    }
    ctx->isr_frame_len = len;
    ctx->isr_frame_ptr = ctx->isr_frame_buf; /* RX BYTE: buffer holds the frame */
    ctx->isr_frame_ready = 1U;               /* publish last */
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
static void dll_isr_isdu_od(iolink_dll_ctx_t* ctx, uint8_t flowctrl, uint8_t* od, uint8_t od_width)
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
        od[n++] = ctx->isr_isdu_resp_buf[idx++];
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
    flags |= dll_pd_status_flags(ctx);
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
    payload[len] = (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
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
/** @brief A frame was served by the ISR: refresh the activity timestamps. */
static void dll_isr_note_activity(iolink_dll_ctx_t* ctx)
{
    ctx->last_activity_ms = dll_time_ms(ctx);
    ctx->last_byte_us = (uint32_t) dll_time_us(ctx);
}

/** @brief Type-0 startup probe (T1): page READ while waiting for the first message. */
static bool dll_isr_type0_startup_probe(iolink_dll_ctx_t* ctx, bool read, uint8_t ch, uint8_t addr,
                                        uint8_t len)
{
    if (!read || (ch != IOLINK_MC_CHANNEL_PAGE) || (len != 2U)) {
        return false;
    }
    dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
    /* The 64-bit T_DSIO deadline is main-owned: request the clear instead of
     * writing it from the ISR (no torn 64-bit read in the main pump). */
    ctx->isr_dsio_clear = 1U;
    const uint8_t od = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
    dll_isr_send_payload(ctx, &od, 1U, dll_isr_status_flags(ctx));
    dll_isr_note_activity(ctx);
    return true;
}

/** @brief Transition command (PREOPERATE -> ESTAB_COM): no response per spec. */
static bool dll_isr_type0_transition(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    if ((ctx->state == IOLINK_DLL_STATE_PREOPERATE) && (len == 2U) &&
        (frame[0] == IOLINK_MC_TRANSITION_COMMAND)) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->fallback_count = 0U;
        if (ctx->phy->stack_timer_start != NULL) {
            ctx->phy->stack_timer_start(ctx->phy->user);
        }
        dll_isr_note_activity(ctx);
        return true;
    }
    return false;
}

/** @brief Type-0 READ: answer page/ISDU/process/diagnosis from the ISR. */
static bool dll_isr_type0_read(iolink_dll_ctx_t* ctx, uint8_t ch, uint8_t addr)
{
#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
    /* Tables 57/67 (SDCI_TC_0038/0049): a TYPE_0 page read while OPERATE is
       active is a reset request. Consume it without a reply and return to
       STARTUP; the next read is served as the startup probe. */
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) && (ch == IOLINK_MC_CHANNEL_PAGE) &&
        (ctx->m_seq_type != IOLINK_M_SEQ_TYPE_0)) {
        ctx->framing_errors++;
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        dll_isr_note_activity(ctx);
        return true;
    }
#endif
    uint8_t od = 0U;
    if (ch == IOLINK_MC_CHANNEL_PAGE) {
        od = iolink_isdu_direct_param_page1_octet(&ctx->isdu, addr);
    }
    else if (ch == IOLINK_MC_CHANNEL_ISDU) {
        /* IDLE/ABORT reset the ISDU transport in the main window; leave those
         * to the main pump. */
        if (addr >= IOLINK_FLOWCTRL_IDLE) {
            return false;
        }
        dll_isr_isdu_od(ctx, addr, &od, 1U);
    }
    else if (ch == IOLINK_MC_CHANNEL_PROCESS) {
        od = 0U;
    }
    else if (ch == IOLINK_MC_CHANNEL_DIAGNOSIS) {
        od = DLL_EVENTS_MEMORY_READ(&ctx->events, addr);
    }
    else {
        return false;
    }
    dll_isr_send_payload(ctx, &od, 1U, dll_isr_status_flags(ctx));
    dll_isr_note_activity(ctx);
    return true;
}

/** @brief Type-0 page WRITE addr 0: Fallback and MasterCommand, CKS-only ack. */
static bool dll_isr_type0_master_command(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len,
                                         uint8_t ch, uint8_t addr)
{
    if (((ctx->state != IOLINK_DLL_STATE_PREOPERATE) && (ctx->state != IOLINK_DLL_STATE_OPERATE)) ||
        (len != 3U) || (ch != IOLINK_MC_CHANNEL_PAGE) || (addr != 0U)) {
        return false;
    }
    if (frame[2] == IOLINK_CMD_FALLBACK) {
        /* A PHY without wake-up detection cannot return from SIO, so the
         * unsupported transition is acked without changing the DL mode. */
        if (dll_phy_has_wakeup(ctx)) {
            /* The 64-bit T_FBD deadline is main-owned: the ISR only requests
             * it, the main pump arms it from the current MinCycleTime. */
            ctx->isr_fallback_pending = 1U;
            dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
        }
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        dll_isr_note_activity(ctx);
        return true;
    }
    if (dll_apply_master_command(ctx, frame[2])) {
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        dll_isr_note_activity(ctx);
        return true;
    }
    return false;
}

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

    if (was_establishing) {
        return dll_isr_type0_startup_probe(ctx, read, ch, addr, len);
    }
    if ((ctx->state != IOLINK_DLL_STATE_PREOPERATE) && (ctx->state != IOLINK_DLL_STATE_OPERATE)) {
        return false;
    }
    if (dll_isr_type0_transition(ctx, frame, len)) {
        return true;
    }
    if (read) {
        return dll_isr_type0_read(ctx, ch, addr);
    }
    if (dll_isr_type0_master_command(ctx, frame, len, ch, addr)) {
        return true;
    }

    /* ISDU write: the ISR already buffered the request octet(s). */
    if ((ch == IOLINK_MC_CHANNEL_ISDU) && (ctx->isdu_isr_consumed != 0U)) {
        ctx->isdu_isr_consumed = 0U; /* the frame is not published to main */
        dll_isr_send_payload(ctx, NULL, 0U, dll_isr_status_flags(ctx));
        dll_isr_note_activity(ctx);
        return true;
    }
    return false;
}

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
    /* Stage the write for the main pump instead of touching ctx->isdu from the
     * ISR: ctx->isdu then has a single writer (the main pump), so the ISDU state
     * machine no longer needs to run with interrupts disabled. A second segment
     * arriving before the drain would overwrite the slot (counted); IO-Link
     * segments are master-paced with at least one cycle time between them. */
    if (ctx->isr_od_staged != 0U) {
        ctx->isr_od_overruns++;
    }
    if (od_n > sizeof(ctx->isr_od_stage)) {
        od_n = (uint8_t) sizeof(ctx->isr_od_stage);
    }
    ctx->isr_od_stage_flowctrl = (uint8_t) (frame[0] & IOLINK_MC_ADDR_MASK);
    ctx->isr_od_stage_len = od_n;
    for (uint8_t i = 0U; i < od_n; i++) {
        ctx->isr_od_stage[i] = frame[od_off + i];
    }
    ctx->isr_od_staged = 1U;
    ctx->isdu_isr_consumed = 1U;
}

/**
 * @brief Fill the reply OD for the captured buffer.
 *
 * Reads come from the ISDU snapshot, the direct-parameter pages or the event
 * memory; a WRITE reply only needs the PD-in tail. Returns false for a channel
 * the ISR must not answer (the main pump owns it).
 */
static bool dll_isr_build_reply_od(iolink_dll_ctx_t* ctx, uint8_t* buf, bool read, uint8_t ch,
                                   uint8_t flowctrl)
{
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
        else if (ch == IOLINK_MC_CHANNEL_PAGE) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = iolink_isdu_direct_param_page1_octet(&ctx->isdu, (uint8_t) (flowctrl + i));
            }
        }
        else if (ch == IOLINK_MC_CHANNEL_DIAGNOSIS) {
            for (uint8_t i = 0U; i < ctx->od_width; i++) {
                buf[i] = DLL_EVENTS_MEMORY_READ(&ctx->events, (uint8_t) (flowctrl + i));
            }
        }
        else {
            return false;
        }
    }
    else if ((ch != IOLINK_MC_CHANNEL_ISDU) && (ch != IOLINK_MC_CHANNEL_PROCESS) &&
             (ch != IOLINK_MC_CHANNEL_PAGE) && (ch != IOLINK_MC_CHANNEL_DIAGNOSIS)) {
        return false;
    }
    return true;
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/** @brief TX FRAME: assemble the reply in place and hand it to the PHY. */
static bool dll_isr_start_reply_frame(iolink_dll_ctx_t* ctx, uint8_t* base, uint16_t out_len,
                                      uint8_t flags)
{
    ctx->isr_tx_len = (uint8_t) out_len;
    ctx->isr_tx_active = 0U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    /* The DMA reads the CKS almost immediately for a 1-2 octet reply (the TDR
     * empties at frame start). It is only safe to compute the CKS after
     * tx_frame() when at least two octets precede it (out_len >= 3): then the
     * DMA must shift the first octet (one full octet time, baud-scaled) before
     * it reads the checksum. A busy PHY (previous reply still on the wire)
     * rejects the transfer instead of tearing it down; the main pump then owns
     * the frame. */
    if (out_len >= 3U) {
        int tx_rc = -1;
        if (ctx->phy->tx_frame != NULL) {
            tx_rc = ctx->phy->tx_frame(ctx->phy->user, base, out_len);
        }
        if (tx_rc != 0) {
            return false;
        }
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        base[out_len - 1U] = (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
    }
    else {
        /* Short reply: fill the CKS before starting the DMA. */
        uint8_t acc = 0x52U;
        for (uint16_t i = 0U; i < (uint16_t) (out_len - 1U); i++) {
            acc = (uint8_t) (acc ^ base[i]);
        }
        base[out_len - 1U] = (uint8_t) (flags | iolink_checksum6_from_acc((uint8_t) (acc ^ flags)));
        if ((ctx->phy->tx_frame == NULL) ||
            (ctx->phy->tx_frame(ctx->phy->user, base, out_len) != 0)) {
            return false;
        }
    }
    return true;
}
#else
/** @brief TX BYTE: stage the reply; the CKS is computed while sending. */
static bool dll_isr_start_reply_byte(iolink_dll_ctx_t* ctx, uint8_t* base, uint16_t out_len,
                                     uint8_t flags)
{
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
}
#endif

/* Select the pre-staged OPERATE reply payload matching the just-framed request
 * and start transmitting it from the ISR. TX BYTE: the CKS is computed while the
 * octets are sent (one per transceiver request). TX FRAME: the payload and its
 * CKS are assembled in the staging buffer and handed over in one call. A Type-0
 * request has no pre-staged variant; the main pump answers it. */
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
     * (the main fills a free copy and publishes it atomically).
     * Layout: [OD x od_width][PD-in][CKS]. Capture the index once, so the main
     * pump never targets this buffer until the next reply. */
    const uint8_t captured = ctx->pd_in_active;
    uint8_t* buf = ctx->pd_in[captured];
    if (!dll_isr_build_reply_od(ctx, buf, read, ch, flowctrl)) {
        return false;
    }

    /* READ: [OD][PD-in]CKS from buf. WRITE: [PD-in]CKS from buf+od_width (the
     * PD-in is always stored after the configured OD header). */
    uint8_t* base = read ? buf : (buf + ctx->od_width);
    const uint16_t total =
        (uint16_t) (((uint16_t) ctx->od_width) + ((uint16_t) ctx->pd_in_len) + 1U);
    const uint16_t out_len = read ? total : (uint16_t) (total - (uint16_t) ctx->od_width);
    if (out_len > (uint16_t) IOLINK_PD_REPLY_MAX) {
        return false;
    }
    /* Publish the captured buffer only once the reply is known to be valid. */
    ctx->pd_in_captured = captured;
    const uint8_t flags = dll_isr_status_flags(ctx);
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    return dll_isr_start_reply_frame(ctx, base, out_len, flags);
#else
    return dll_isr_start_reply_byte(ctx, base, out_len, flags);
#endif
}

/** @brief True for an ISDU write request (its OD is consumed by the engine). */
static bool dll_mc_is_isdu_write(const uint8_t* f)
{
    return ((f[0] & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_ISDU) &&
           ((f[0] & IOLINK_MC_RW_MASK) == 0U);
}

/**
 * @brief Decide whether the ISR must answer this request itself.
 *
 * A pre-cached (TYPE_2_V) reply is answered from the ISR for the process
 * channel, an ISDU write ACK and any ISDU read. Type-0 frames and other
 * types/channels are answered by the main pump after its own validation. A
 * corrupt or illegal frame is never answered eagerly (the caller rejects it).
 */
static bool dll_isr_wants_eager(const iolink_dll_ctx_t* ctx, const uint8_t* f)
{
    const bool type12 = ((f[1] & IOLINK_MSEQ_TYPE_MASK) != IOLINK_MSEQ_TYPE_0);
    const uint8_t ch = (uint8_t) (f[0] & IOLINK_MC_COMM_CHANNEL_MASK);
    const bool isdu = (ch == IOLINK_MC_CHANNEL_ISDU);
    const bool isdu_write = isdu && ((f[0] & IOLINK_MC_RW_MASK) == 0U);
    /* Every ISDU read is answered from the ISR, which owns the response segment
     * index and serves it from the snapshot the main pump refreshes when the
     * service completes. This keeps t_A within 10 bit times (no main-loop
     * round-trip). */
    const bool isdu_read = isdu && ((f[0] & IOLINK_MC_RW_MASK) != 0U);
    return type12 && dll_phy_has_tx(ctx) &&
           ((ch == IOLINK_MC_CHANNEL_PROCESS) || (ch == IOLINK_MC_CHANNEL_PAGE) ||
            (ch == IOLINK_MC_CHANNEL_DIAGNOSIS) || isdu_write || isdu_read);
}

/**
 * @brief Shared frame-complete path for both RX granularities.
 *
 * Consumes an ISDU write, runs the Type-0 fast path and decides the eager
 * reply. The publish/reply ordering is the only mode-specific part: RX BYTE
 * starts the reply before the mailbox copy so the first reply octet never waits
 * for the publish and a full mailbox cannot suppress it; RX FRAME publishes the
 * zero-copy pointer first and stabilises the mailbox after the reply window.
 */
static inline void dll_isr_complete_frame(iolink_dll_ctx_t* ctx, const uint8_t* f, uint8_t len,
                                          bool allowed)
{
    if (dll_mc_is_isdu_write(f)) {
        /* Buffer only a valid request: a corrupt ISDU write must not reach the
         * transport (ST validates before StackBaseOdWrite). */
        if (!allowed) {
            ctx->isr_eager_rejects++;
            return;
        }
        dll_isr_consume_isdu_write(ctx, f, len);
    }
    if (allowed && dll_isr_handle_type0(ctx, f, len)) {
        dll_master_activity(ctx); /* served this cycle from the ISR */
        return;
    }

    const bool eager = dll_isr_wants_eager(ctx, f);
    ctx->isr_eager_replied = 0U;
    if (eager && !allowed) {
        ctx->isr_eager_rejects++; /* main pump validates/falls back */
    }
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    /* Start the eager reply BEFORE the mailbox copy: the first reply octet must
     * not wait for the publish, and a full mailbox must not suppress the reply.
     * The payload is built synchronously from the header and pd_in. */
    if (eager && allowed) {
        ctx->isr_frame_ptr = f;
        ctx->isr_frame_len = len;
        if (dll_isr_start_reply(ctx)) {
            ctx->isr_eager_replied = 1U;
        }
    }
    /* Publish even when the eager validation failed: the main pump owns the
     * error/fallback accounting for an invalid frame. */
    (void) dll_publish_isr_frame(ctx, f, len);
#else
    ctx->isr_frame_len = len;
    ctx->isr_frame_ptr = f;    /* PHY buffer, valid during this ISR */
    ctx->isr_frame_ready = 1U; /* publish last */
    if (eager && allowed) {
        if (dll_isr_start_reply(ctx)) {
            ctx->isr_eager_replied = 1U;
        }
    }
    /* Stabilise the frame for the main pump after the reply window has closed:
     * the PHY's RX DMA ring may wrap while the main copies, and the eager reply
     * must not pay for this in t_A. */
    (void) memcpy(ctx->isr_frame_buf, f, len);
    ctx->isr_frame_ptr = ctx->isr_frame_buf;
#endif
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/** @brief Length sanity check for a PHY-delivered frame (counts framing errors). */
static bool dll_isr_frame_len_ok(iolink_dll_ctx_t* ctx, const uint8_t* f, uint8_t len)
{
    if ((len < 2U) || (len > IOLINK_M_SEQ_MAX_LEN)) {
        ctx->framing_errors++;
        return false;
    }
    const uint8_t expected_len = iolink_dll_expected_req_len(ctx, f[0]);
    if ((expected_len == 0U) || (len != expected_len)) {
        ctx->framing_errors++;
        return false;
    }
    return true;
}

/** @brief RX FRAME: take one PHY-delivered frame and hand it to the shared tail. */
static void dll_isr_drain_rx_frame(iolink_dll_ctx_t* ctx)
{
    /* Do not call rx_frame() while the mailbox still holds the previous
     * zero-copy frame: the PHY may reuse its buffer on the next call. */
    if (ctx->isr_frame_ready != 0U) {
        return;
    }
    uint8_t st = 0U;
    uint8_t len = 0U;
    const uint8_t* f = ctx->phy->rx_frame(ctx->phy->user, &st, &len);
    (void) st;
    if (f == NULL) {
        return;
    }
    if (!dll_isr_frame_len_ok(ctx, f, len)) {
        return;
    }
    const bool allowed = dll_isr_reply_allowed(ctx, f, len);
    if (allowed) {
        /* A validated frame proves the master is polling. The main pump may be
         * busy (e.g. a slow ISDU service) and cannot consume the mailbox in
         * time; without this the master-lost supervision would count a busy
         * main window as a loss. */
        ctx->isr_master_activity = 1U;
        if (ctx->phy->response_timer_start != NULL) {
            /* Arm the t_A watchdog only for a request that will be answered:
             * rejected or spurious frames must not count as t_A timeouts. */
            ctx->phy->response_timer_start(ctx->phy->user);
        }
    }
    dll_isr_complete_frame(ctx, f, len, allowed);
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME */

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/** @brief Discard the frame being assembled (does not touch the TX engine). */
static void dll_isr_rx_byte_reset(iolink_dll_ctx_t* ctx)
{
    ctx->isr_rx_cnt = 0U;
    ctx->isr_rx_expect = 0U;
}

/**
 * @brief RX BYTE: handle the CHK/gap frame end (Timer A) for the current buffer.
 *
 * The gap is a timeout, not the normal frame end. It can only close a short
 * M-sequence (fewer than the two header octets) or a truncated one: the byte
 * engine consumes a frame as soon as its expected length arrives, so a
 * length-complete frame never ends here. Both cases are discarded; only a real
 * truncation is counted as a framing error.
 */
static void dll_isr_rx_byte_gap(iolink_dll_ctx_t* ctx)
{
    const bool incomplete_ms = (ctx->isr_rx_expect != 0U) &&
                               (ctx->isr_rx_cnt >= IOLINK_M_SEQ_HEADER_LEN) &&
                               (ctx->isr_rx_cnt < ctx->isr_rx_expect);
    if (incomplete_ms) {
        ctx->framing_errors++;
    }
    dll_isr_rx_byte_reset(ctx);
    /* Release the transceiver's receive engine unless a reply is in flight
     * (the TX drain calls tx_done). */
    if ((ctx->isr_tx_active == 0U) && (ctx->phy->tx_done != NULL)) {
        ctx->phy->tx_done(ctx->phy->user);
    }
}

/** @brief RX BYTE: seed the checksum and append one received octet. */
static void dll_isr_rx_byte_accumulate(iolink_dll_ctx_t* ctx, uint8_t data)
{
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
}

/**
 * @brief RX BYTE: derive the expected request length once the header is complete.
 *
 * The CKT type bits identify the M-sequence type. Type-0 has no length field but
 * its size is fixed by MC/CKT (read 2, OD write 3); the 13 Tbit gap is only a
 * timeout, never the normal end.
 */
static void dll_isr_rx_byte_derive_expect(iolink_dll_ctx_t* ctx)
{
    if (ctx->isr_rx_cnt != 2U) {
        return;
    }
    const uint8_t mc = (uint8_t) ctx->isr_rx_buf[0];
    const uint8_t ckt = (uint8_t) ctx->isr_rx_buf[1];
    if ((ckt & IOLINK_MSEQ_TYPE_MASK) == IOLINK_MSEQ_TYPE_0) {
        const bool wr = ((mc & IOLINK_MC_RW_MASK) == 0U);
        const uint8_t ch = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);
        ctx->isr_rx_expect =
            (wr && ((ch == IOLINK_MC_CHANNEL_PAGE) || (ch == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                    (ch == IOLINK_MC_CHANNEL_ISDU)))
                ? 3U
                : 2U;
    }
    else {
        ctx->isr_rx_expect = iolink_dll_expected_req_len(ctx, mc);
    }
}

/** @brief RX BYTE: the known length is complete; hand the frame to the shared tail. */
static void dll_isr_rx_byte_complete(iolink_dll_ctx_t* ctx)
{
    /* A length-complete M-sequence proves the master is polling; the main pump
     * may not consume the mailbox before its supervision window elapses (see
     * the RX FRAME branch). */
    ctx->isr_master_activity = 1U;
    const bool allowed =
        dll_isr_reply_allowed_acc(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect, ctx->isr_rx_acc);
    dll_isr_complete_frame(ctx, ctx->isr_rx_buf, ctx->isr_rx_expect, allowed);
    dll_isr_rx_byte_reset(ctx);
}

/** @brief RX BYTE: consume one received octet. Returns true when the drain should stop. */
static bool dll_isr_drain_rx_byte(iolink_dll_ctx_t* ctx)
{
    uint8_t data = 0U;
    uint8_t st = 0U;
    const int ok = ctx->phy->rx_octet(ctx->phy->user, &data, &st);

    if ((st & IOLINK_PHY_STATUS_CHK) != 0U) {
        dll_isr_rx_byte_gap(ctx);
        return true;
    }
    if (ok < 0) {
        /* Transport error: discard the partial frame and count it. */
        ctx->framing_errors++;
        dll_isr_rx_byte_reset(ctx);
        return true;
    }
    if (ok == 0) {
        return true;
    }
    dll_isr_rx_byte_accumulate(ctx, data);
    dll_isr_rx_byte_derive_expect(ctx);
    if ((ctx->isr_rx_expect != 0U) && (ctx->isr_rx_cnt >= ctx->isr_rx_expect)) {
        dll_isr_rx_byte_complete(ctx);
        return true;
    }
    return false;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/**
 * @brief Drain a back-to-back burst, bounded so a wedged line cannot spin forever.
 */
static void dll_isr_drain(iolink_dll_ctx_t* ctx)
{
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
        dll_isr_drain_rx_frame(ctx);
        break;
#else
        if (dll_isr_drain_rx_byte(ctx)) {
            break;
        }
#endif
    }
}

/** @brief A main-context handler staged a verbatim reply; transmit it now. */
static void dll_isr_tx_pending(iolink_dll_ctx_t* ctx)
{
    ctx->isr_tx_pending = 0U;
    if (ctx->phy->response_timer_stop != NULL) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
    dll_isr_start_staged_tx(ctx, ctx->isr_tx_pending_slot, ctx->isr_tx_len);
}

static void dll_isr_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || !dll_phy_has_rx(ctx) || (ctx->phy->int_active == NULL)) {
        return;
    }

    /* Checked before the int_active gate: a software-requested service has no
     * transceiver RX/TX activity of its own to report. */
    if (ctx->isr_tx_pending != 0U) {
        dll_isr_tx_pending(ctx);
        return;
    }

    /* Level re-check: the data interrupt may latch a crosstalk edge; bail out at
     * zero cost unless the transceiver is genuinely requesting service. */
    if (ctx->phy->int_active(ctx->phy->user) == 0U) {
        return;
    }

    dll_isr_drain(ctx);
}

void iolink_dll_process_isr(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->in_isr != 0U) {
        /* A higher-priority interrupt invoked the pump while a lower-priority
         * one is already running it (the pump is bound to several vectors on
         * boards without BASEPRI). The pump state has a single writer, so
         * nesting would corrupt the reply staging and the mailbox: defer and
         * let the outer invocation run once more before it returns. */
        ctx->isr_reentry_pending = 1U;
        return;
    }
    ctx->in_isr = 1U;
    do {
        ctx->isr_reentry_pending = 0U;
        dll_isr_process(ctx);
    } while (ctx->isr_reentry_pending != 0U);
    ctx->in_isr = 0U;
}

void iolink_dll_on_rx_timeout(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->isr_master_activity = 1U;
    ctx->timeout_errors++;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
    ctx->isr_rx_cnt = 0U;
    ctx->isr_rx_expect = 0U;
#endif
    if ((ctx->phy != NULL) && (ctx->phy->response_timer_stop != NULL)) {
        ctx->phy->response_timer_stop(ctx->phy->user);
    }
}

void iolink_dll_on_stack_tick(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    ctx->stack_tick_pending = 1U;
}

uint8_t iolink_dll_can_run_user_code(iolink_dll_ctx_t* ctx)
{
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
}
