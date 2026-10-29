/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_INTERNAL_H
#define IOLINK_DLL_INTERNAL_H

#include <stdint.h>
#include "iolinki/dll.h"

/**
 * @file dll_internal.h
 * @brief Internal DLL definitions and structures
 */

#include "iolinki/protocol.h"

#define IOLINK_M_SEQ_TYPE1_LEN(pd_len)                         \
    (4 + (pd_len)) /* MC(8) + CKT(8) + PD(var) + OD(8) + CK(8) \
                    */
#define IOLINK_M_SEQ_TYPE2_LEN(pd_len, od_len) \
    (3 + (pd_len) + (od_len)) /* MC(8) + CKT(8) + PD(var) + OD(var) + CK(8) */

/** @brief Decoded 2-octet Type-0 M-sequence (master command + checksum). */
typedef struct
{
    uint8_t master_command; /**< Master command octet (MC). */
    uint8_t ck;             /**< Checksum octet (CK). */
} iolink_m_seq_type0_t;

/** @brief On-request data (OD) octet paired with a validity flag. */
typedef struct
{
    uint8_t od_data; /**< On-request data octet. */
    uint8_t valid;   /**< Non-zero when @ref od_data holds a valid value. */
} iolink_od_req_t;

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

/**
 * @name Cross-TU DLL declarations
 *
 * dll.c provides the state/timing helpers used by the ISR, and dll_isr.c
 * provides the staged-TX entry used by dll_send(). See docs/CODE_FLOW.md for
 * the main/ISR context contract.
 * @{
 */
void dll_set_state(iolink_dll_ctx_t* ctx, iolink_dll_state_t new_state);
bool dll_apply_master_command(iolink_dll_ctx_t* ctx, uint8_t cmd);
bool dll_type_allowed(const iolink_dll_ctx_t* ctx, const uint8_t* frame);
uint8_t dll_pd_status_flags(const iolink_dll_ctx_t* ctx);
void dll_master_activity(iolink_dll_ctx_t* ctx);
void dll_isr_start_staged_tx(iolink_dll_ctx_t* ctx, uint8_t slot, uint8_t len);
/** @} */

#endif  // IOLINK_DLL_INTERNAL_H
