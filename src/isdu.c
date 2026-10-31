/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/protocol.h"
#include "iolinki/isdu.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <string.h>
#include <stdint.h>

/**
 * @file isdu.c
 * @brief ISDU service engine: acyclic parameter read/write handling.
 * @ingroup iolinki_isdu
 *
 * Implements the ISDU segmentation state machine (byte collection, header
 * parsing, sequence tracking and response segmentation) and dispatches
 * standard indices and system commands to the device parameter, device-info,
 * event, direct-parameter and data-storage handlers.
 *
 * ISDU Header (2 or 3 bytes):
 * [7:4] Service (Read/Write)
 * [3:0] Length (if < 15)
 * [ Index (16-bit) ]
 * [ Subindex (8-bit) ]
 */

/* -------------------------------------------------------------------------
 * File map
 *
 *  - Transport, parsing and shared helpers: chkpdu ... iolink_isdu_od_read
 *    (public: iolink_isdu_init, iolink_isdu_od_write, iolink_isdu_od_read)
 *  - Shared response helpers: isdu_respond_error
 *  - Mandatory indices: isdu_check_mandatory_write ... handle_mandatory_indices
 *  - SystemCommand and Data Storage upload helpers: isdu_clear_ds_upload_req ...
 *    (public: iolink_isdu_execute_system_command)
 *  - Access locks, detailed status, error stats and direct parameters:
 *    handle_access_locks ... handle_direct_parameters
 *  - Data Storage services: isdu_append_ds_index_list ... isdu_reject_ds_access
 *  - Index routing and standard dispatch: isdu_handle_vendor_index ...
 *  - Tail public API: iolink_isdu_process, iolink_isdu_get_response_byte,
 *    iolink_isdu_snapshot_response
 * ------------------------------------------------------------------------- */

/* ... includes ... */

/* Structs moved to header iolink_isdu_ctx_t */

/* -------------------------------------------------------------------------
 * Transport, parsing and shared helpers
 * ------------------------------------------------------------------------- */

/** @brief CHKPDU: XOR of all ISDU octets with the checksum octet read as 0 (A.5.6). */
static uint8_t chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

void iolink_isdu_init(iolink_isdu_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_isdu_ctx_t))) {
        return;
    }
    ctx->state = ISDU_STATE_IDLE;
}

/** @brief Read a parameter via the bound params context, or the legacy global set. */
static int isdu_params_get(const iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           uint8_t* buffer, size_t max_len)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_get(ctx->params_ctx, index, subindex, buffer, max_len);
    }
#if IOLINK_FEATURE_LEGACY_PARAMS
    return iolink_params_get(index, subindex, buffer, max_len);
#else
    (void) index;
    (void) subindex;
    (void) buffer;
    (void) max_len;
    return -1;
#endif
}

/** @brief Write a parameter via the bound params context, or the legacy global set. */
static int isdu_params_set(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           const uint8_t* data, size_t len, bool persist)
{
#if IOLINK_FEATURE_DATA_STORAGE
    /* During a block download the write is buffered for an atomic commit on
       ParamDownloadEnd (10.3.2) rather than applied immediately. */
    if ((ctx != NULL) && (ctx->ds_ctx != NULL) &&
        iolink_ds_downloading((const iolink_ds_ctx_t*) ctx->ds_ctx)) {
        return iolink_ds_stage_param((iolink_ds_ctx_t*) ctx->ds_ctx, index, subindex, data, len);
    }
#endif
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_set(ctx->params_ctx, index, subindex, data, len, persist);
    }
#if IOLINK_FEATURE_LEGACY_PARAMS
    return iolink_params_set(index, subindex, data, len, persist);
#else
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return -1;
#endif
}

#if IOLINK_FEATURE_SYSTEM_COMMAND
/** @brief Factory-reset the bound parameter context, or the legacy global set. */
static void isdu_params_factory_reset(iolink_isdu_ctx_t* ctx)
{
    /* Reset the device's own parameter context when one is bound (as device.c
       does via ctx->params_ctx); otherwise fall back to the legacy global set.
       Using the bound context is required, else a per-device app tag survives a
       Restore Factory Settings command. */
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        iolink_params_ctx_factory_reset(ctx->params_ctx);
    }
#if IOLINK_FEATURE_LEGACY_PARAMS
    else {
        iolink_params_factory_reset();
    }
#endif
}
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */

/** @brief Device-info context bound to this ISDU context, if any. */
static iolink_device_info_ctx_t* isdu_info_ctx(iolink_isdu_ctx_t* ctx)
{
    return ((ctx != NULL) && (ctx->params_ctx != NULL)) ? ctx->params_ctx->device_info : NULL;
}

/** @brief Effective identity for this ISDU context. */
static const iolink_device_info_t* isdu_info(const iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_ctx_t* info_ctx =
        ((ctx != NULL) && (ctx->params_ctx != NULL)) ? ctx->params_ctx->device_info : NULL;
    return (info_ctx != NULL) ? iolink_device_info_ctx_get(info_ctx) : iolink_device_info_get();
}

/** @brief Effective access locks for this ISDU context. */
static uint16_t isdu_access_locks(const iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_ctx_t* info_ctx =
        ((ctx != NULL) && (ctx->params_ctx != NULL)) ? ctx->params_ctx->device_info : NULL;
    return (info_ctx != NULL) ? iolink_device_info_ctx_get_access_locks(info_ctx)
                              : iolink_device_info_get_access_locks();
}

static void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error);

/** @brief Map a parameter-manager error to an ISDU negative response. */
static void isdu_respond_param_error(iolink_isdu_ctx_t* ctx, int ret)
{
    uint8_t error = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    if (ret == -2) {
        error = IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN;
    }
    else if (ret == -3) {
        error = IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN;
    }
    isdu_respond_error(ctx, error);
}

/* handle_standard_commands() is defined below; the transport executes it once a
   full ISDU request has been received. */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx);

/** @brief Reset the ISDU request transport to Idle. */
static void isdu_transport_reset(iolink_isdu_ctx_t* ctx)
{
    ctx->response_negative = false;
    ctx->state = ISDU_STATE_IDLE;
    ctx->req_idx = 0U;
    ctx->req_total = 0U;
    ctx->resp_idx = 0U;
    ctx->resp_total = 0U;
    ctx->buffer_idx = 0U;
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->flowctrl_seen = false;
    ctx->repeat_pending = false;
    ctx->error_code = IOLINK_ISDU_ERROR_NONE;
    ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
    ctx->header.length = 0U;
    ctx->header.index = 0U;
    ctx->header.subindex = 0U;
}

/** @brief Total ISDU octet count declared by the first received octets (A.5.3/A.14).
 *
 * Returns 0 while the ExtLength octet is still missing. */
static size_t isdu_declared_total(const uint8_t* buf, size_t have)
{
    if (have == 0U) {
        return 0U;
    }
    const uint8_t len = (uint8_t) (buf[0] & 0x0FU);
    if (len == 0U) {
        return 1U; /* No Service / Busy: single octet */
    }
    if (len == 1U) {
        if (have < 2U) {
            return 0U; /* ExtLength octet not yet received */
        }
        const uint8_t ext = buf[1];
        if ((ext >= 17U) && (ext <= 238U)) {
            return ext;
        }
        return 0U; /* Reserved / invalid */
    }
    return len;
}

/**
 * @brief Decode a complete request buffer and execute the addressed service.
 *
 * Fills ctx->header and ctx->buffer from the raw octets, then dispatches via
 * handle_standard_commands(); the handler leaves a 2-octet negative marker
 * (response_buf[0]==0x80) or a positive payload in response_buf/response_len.
 */
/** @brief Decode the service nibble into the read/write header type. */
static bool isdu_decode_service(iolink_isdu_ctx_t* ctx, uint8_t service)
{
    if ((service >= 1U) && (service <= 3U)) {
        ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_WRITE;
        return true;
    }
    if ((service >= 9U) && (service <= 11U)) {
        ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
        return true;
    }
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    return false;
}

/**
 * @brief Parse the index/subindex fields of a request.
 *
 * Index format per Table A.12/A.15. Service 0x1/0x9 = 8-bit index, 0x2/0xA =
 * 8-bit index + subindex, 0x3/0xB = 16-bit index + subindex.
 *
 * @return false when the request was truncated and a negative response was
 *         produced.
 */
static bool isdu_parse_index(iolink_isdu_ctx_t* ctx, const uint8_t* buf, size_t* pos,
                             size_t payload_end, uint8_t service)
{
    if ((buf[0] & 0x0FU) == 1U) {
        (*pos)++; /* Skip ExtLength octet */
    }

    const bool two_index = (service == 3U) || (service == 11U);
    const bool has_sub = (service == 2U) || (service == 3U) || (service == 10U) || (service == 11U);

    if (two_index) {
        if ((*pos + 1U) >= payload_end) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return false;
        }
        ctx->header.index = (uint16_t) ((uint16_t) buf[*pos] << 8);
        (*pos)++;
        ctx->header.index |= buf[(*pos)++];
    }
    else {
        if (*pos >= payload_end) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return false;
        }
        ctx->header.index = buf[(*pos)++];
    }

    if (has_sub) {
        if (*pos >= payload_end) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            return false;
        }
        ctx->header.subindex = buf[(*pos)++];
    }
    return true;
}

/** @brief Copy the data payload (everything before CHKPDU) into the buffer. */
static void isdu_copy_payload(iolink_isdu_ctx_t* ctx, const uint8_t* buf, size_t pos, size_t total)
{
    if (pos < total) {
        const size_t data_end = total - 1U; /* exclude CHKPDU */
        if (data_end > pos) {
            size_t n = data_end - pos;
            if (n > sizeof(ctx->buffer)) {
                n = sizeof(ctx->buffer);
            }
            (void) memcpy(ctx->buffer, &buf[pos], n);
            ctx->buffer_idx = n;
        }
    }
    ctx->header.length = (uint8_t) ctx->buffer_idx;
}

static void isdu_execute_request(iolink_isdu_ctx_t* ctx, size_t total)
{
    ctx->response_negative = false;
    const uint8_t* buf = ctx->req_buf;
    const uint8_t service = (uint8_t) (buf[0] >> 4);
    size_t pos = 1U;
    const size_t payload_end = total - 1U; /* CHKPDU cannot supply an index/subindex. */

    ctx->buffer_idx = 0U;
    ctx->response_idx = 0U;
    ctx->header.subindex = 0U;
    ctx->header.length = 0U;

    if (!isdu_decode_service(ctx, service)) {
        return;
    }
    if (!isdu_parse_index(ctx, buf, &pos, payload_end, service)) {
        return;
    }
    isdu_copy_payload(ctx, buf, pos, total);

    /* Parsed: execution is performed by iolink_isdu_process(), keeping the
       transport and the application dispatch separable (and testable). */
    ctx->state = ISDU_STATE_SERVICE_EXECUTE;
}

/** @brief Frame the handler result into ctx->resp_buf (positive or negative, A.5/C.1). */
static void isdu_frame_response(iolink_isdu_ctx_t* ctx)
{
    const bool negative = ctx->response_negative;
    const bool write = (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE);
    size_t pos = 0U;

    if (negative) {
        ctx->resp_buf[pos++] = write ? 0x44U : 0xC4U; /* Write/Read Response (-), Length 4 */
        ctx->resp_buf[pos++] = 0x80U;                 /* ErrorCode (APP_DEV) */
        ctx->resp_buf[pos++] = ctx->response_buf[1];  /* AdditionalCode */
    }
    else if (write) {
        ctx->resp_buf[pos++] = 0x52U; /* Write Response (+) */
    }
    else {
        const size_t data_len = ctx->response_len;
        const size_t direct_total = data_len + 2U; /* I-Service + data + CHKPDU */
        if (direct_total <= 15U) {
            ctx->resp_buf[pos++] = (uint8_t) (0xD0U | (uint8_t) direct_total);
            if (data_len > 0U) {
                (void) memcpy(&ctx->resp_buf[pos], ctx->response_buf, data_len);
                pos += data_len;
            }
        }
        else {
            /* A.5.3 / Figure A.19: I-Service+Length octet, ExtLength, data, CHKPDU = data + 3. */
            const size_t total = data_len + 3U;
            ctx->resp_buf[pos++] = 0xD1U;
            ctx->resp_buf[pos++] = (uint8_t) total;
            if (data_len > 0U) {
                (void) memcpy(&ctx->resp_buf[pos], ctx->response_buf, data_len);
                pos += data_len;
            }
        }
    }

    ctx->resp_buf[pos] = 0x00U;
    ctx->resp_buf[pos] = chkpdu(ctx->resp_buf, pos + 1U);
    pos++;
    ctx->resp_total = pos;
    ctx->resp_idx = 0U;
}

/** @brief Build a Busy response octet (0x01) for a read START not yet answerable. */
static void isdu_emit_busy(iolink_isdu_ctx_t* ctx)
{
    ctx->resp_buf[0] = 0x01U;
    ctx->resp_total = 1U;
    ctx->resp_idx = 0U;
}

void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od,
                          uint8_t od_len)
{
    if (ctx == NULL) {
        return;
    }

    /* Table 52: ABORT (0x1F) rejects the request data and resets the transport
       to Idle regardless of direction. IDLE/IDLE2 and the reserved values
       0x13..0x1E are not writes; they are handled by the read path. */
    if (flowctrl == IOLINK_FLOWCTRL_ABORT) {
        isdu_transport_reset(ctx);
        return;
    }
    if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        return;
    }

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        isdu_transport_reset(ctx);
        ctx->flowctrl_seen = true;
        ctx->last_flowctrl = flowctrl;
        ctx->state = ISDU_STATE_HEADER_INITIAL;
        ctx->req_total = 0U;
    }
    else {
        /* COUNT (0x00..0x0F): must follow a START. A repeated FlowCTRL repeats
           the previous message and its payload is ignored (7.3.6.2). */
        if (!ctx->flowctrl_seen || (ctx->state == ISDU_STATE_IDLE)) {
            ctx->error_code = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
            isdu_transport_reset(ctx);
            return;
        }
        if (flowctrl == ctx->last_flowctrl) {
            ctx->repeat_pending = true;
            return;
        }
        const uint8_t expected = (uint8_t) ((ctx->last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        if (flowctrl != expected) {
            /* Structure violation: drop the request, go to Idle, next read
               answers No Service (0x00) (Table 54 T13). */
            ctx->error_code = IOLINK_ISDU_ERROR_SEGMENTATION;
            isdu_transport_reset(ctx);
            return;
        }
        ctx->last_flowctrl = flowctrl;
        ctx->repeat_pending = false;
    }

    if (ctx->repeat_pending) {
        ctx->repeat_pending = false;
        return;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->req_idx < sizeof(ctx->req_buf)) {
            ctx->req_buf[ctx->req_idx++] = od[i];
        }
        if (ctx->req_total == 0U) {
            ctx->req_total = isdu_declared_total(ctx->req_buf, ctx->req_idx);
        }
        if ((ctx->req_total > 0U) && (ctx->req_idx >= ctx->req_total)) {
            break;
        }
    }

    if ((ctx->req_total > 0U) && (ctx->req_idx >= ctx->req_total)) {
        /* A.5.6: CHKPDU is valid when XOR of all ISDU octets is zero. */
        uint8_t x = 0U;
        for (size_t i = 0U; i < ctx->req_total; i++) {
            x ^= ctx->req_buf[i];
        }
        if (x != 0U) {
            /* A.5.6 / Table 84 (SDCI_TC_0065): an invalid CHKPDU makes the
               request invalid. Discard it, return to Idle and answer the next
               Read request with I-Service 0 ("No Service"). Do not synthesize a
               negative ISDU response for the malformed request. */
            ctx->error_code = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
            isdu_transport_reset(ctx);
            return;
        }
        isdu_execute_request(ctx, ctx->req_total);
        if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            isdu_frame_response(ctx);
        }
    }
}

void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len)
{
    if (ctx == NULL) {
        return;
    }
    for (uint8_t i = 0U; i < od_len; i++) {
        od_out[i] = 0U;
    }

    if (flowctrl == IOLINK_FLOWCTRL_ABORT) {
        isdu_transport_reset(ctx);
        return;
    }
    if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2)) {
        isdu_transport_reset(ctx);
        return;
    }

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        /* Start of response read. If a service is being executed and the
           application has not answered, answer Busy (0x01) (Table A.14). */
        if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
            isdu_emit_busy(ctx);
        }
        else if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            /* restarted read: restart from the first response octet */
            ctx->resp_idx = 0U;
        }
        else {
            /* Idle / no pending service: No Service (0x00). */
            ctx->resp_buf[0] = 0x00U;
            ctx->resp_total = 1U;
            ctx->resp_idx = 0U;
        }
        ctx->last_flowctrl = flowctrl;
        ctx->flowctrl_seen = true;
    }
    else if (flowctrl < IOLINK_FLOWCTRL_START) {
        /* COUNT on a read continues the response sequentially. */
        if (!ctx->flowctrl_seen) {
            return;
        }
        ctx->last_flowctrl = flowctrl;
    }
    else {
        /* Reserved values are not a valid read FlowCTRL. */
        return;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->resp_idx < ctx->resp_total) {
            od_out[i] = ctx->resp_buf[ctx->resp_idx++];
        }
        else {
            od_out[i] = 0x00U;
        }
    }

    if ((ctx->state == ISDU_STATE_RESPONSE_READY) && (ctx->resp_idx >= ctx->resp_total)) {
        isdu_transport_reset(ctx);
    }
}

/* -------------------------------------------------------------------------
 * Shared response helpers
 * ------------------------------------------------------------------------- */

/** @brief Terminate the ISDU service with a two-octet negative response. */
static void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error)
{
    ctx->response_negative = true;
    ctx->response_buf[0] = 0x80U;
    ctx->response_buf[1] = error;
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/* -------------------------------------------------------------------------
 * Mandatory indices
 * ------------------------------------------------------------------------- */

/**
 * @brief Reject a WRITE to a read-only mandatory object.
 *
 * @return true when a negative response was already produced.
 */
static bool isdu_check_mandatory_write(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        return false;
    }
    const uint16_t index = ctx->header.index;
    const bool writable_tag = (index == IOLINK_IDX_APPLICATION_TAG) ||
                              (index == IOLINK_IDX_FUNCTION_TAG) ||
                              (index == IOLINK_IDX_LOCATION_TAG);
    if (writable_tag) {
        /* Table B.2.4: with the Parameter (write) access lock set, every
           read/write parameter except DeviceAccessLocks is write-protected. */
        if ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
            return true;
        }
        return false;
    }

    switch (index) {
        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
        case IOLINK_IDX_VENDOR_NAME:
        case IOLINK_IDX_VENDOR_TEXT:
        case IOLINK_IDX_PRODUCT_NAME:
        case IOLINK_IDX_PRODUCT_ID:
        case IOLINK_IDX_PRODUCT_TEXT:
        case IOLINK_IDX_SERIAL_NUMBER:
        case IOLINK_IDX_HARDWARE_REVISION:
        case IOLINK_IDX_FIRMWARE_REVISION:
        case IOLINK_IDX_PRODUCT_URI:
        case IOLINK_IDX_ERROR_COUNT:
        case IOLINK_IDX_DEVICE_STATUS:
        case IOLINK_IDX_PROCESS_DATA_INPUT:
        case IOLINK_IDX_PROCESS_DATA_OUTPUT:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
            return true;
        default:
            return false; /* unknown index: IDX_NOTAVAIL in the dispatcher */
    }
}

/** @brief B.2.5: 0x000D is an ArrayT of UIntegerT16 (single element here). */
static void isdu_read_profile_characteristic(iolink_isdu_ctx_t* ctx,
                                             const iolink_device_info_t* info)
{
    if (info->profile_characteristic == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    if (ctx->header.subindex > 1U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }
    ctx->response_buf[0] = (uint8_t) (info->profile_characteristic >> 8);
    ctx->response_buf[1] = (uint8_t) (info->profile_characteristic & 0xFF);
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Table B.8/B.2.20: 2-octet error count since reset. */
static void isdu_read_error_count(iolink_isdu_ctx_t* ctx)
{
    uint32_t total = 0U;
    if (ctx->dll_ctx != NULL) {
        iolink_dll_stats_t stats;
        iolink_dll_get_stats((const iolink_dll_ctx_t*) ctx->dll_ctx, &stats);
        uint64_t sum = (uint64_t) stats.crc_errors + (uint64_t) stats.framing_errors +
                       (uint64_t) stats.timeout_errors + (uint64_t) stats.timing_errors;
        total = (sum > 0xFFFFU) ? 0xFFFFU : (uint32_t) sum;
    }
    ctx->response_buf[0] = (uint8_t) (total >> 8);
    ctx->response_buf[1] = (uint8_t) (total & 0xFFU);
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Map an identification index to its device-info string (NULL if none). */
static const char* isdu_identification_string(const iolink_device_info_t* info, uint16_t index)
{
    switch (index) {
        case IOLINK_IDX_VENDOR_NAME:
            return info->vendor_name;
        case IOLINK_IDX_VENDOR_TEXT:
            return info->vendor_text;
        case IOLINK_IDX_PRODUCT_NAME:
            return info->product_name;
        case IOLINK_IDX_PRODUCT_ID:
            return info->product_id;
        case IOLINK_IDX_PRODUCT_TEXT:
            return info->product_text;
        case IOLINK_IDX_SERIAL_NUMBER:
            return info->serial_number;
        case IOLINK_IDX_HARDWARE_REVISION:
            return info->hardware_revision;
        case IOLINK_IDX_FIRMWARE_REVISION:
            return info->firmware_revision;
        case IOLINK_IDX_PRODUCT_URI:
            return info->product_uri;
        default:
            return NULL;
    }
}

/**
 * @brief Read an identification string into the response buffer.
 *
 * @return false when the object has no string and no response was produced
 *         (the transport state is left untouched).
 */
static bool isdu_read_identification_string(iolink_isdu_ctx_t* ctx,
                                            const iolink_device_info_t* info, uint16_t index)
{
    const char* str_data = isdu_identification_string(info, index);
    if (index == IOLINK_IDX_PRODUCT_URI && str_data == NULL) {
        /* Table B.8/B.2.19: optional, conditional on an associated Profile. */
        ctx->response_len = 0U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return true;
    }
    if (str_data == NULL) {
        return false;
    }

    /* Table B.8/B.2.8-B.2.19: per-object StringT fixedLength caps the response.
       ProductURI allows up to 100 octets; SerialNumber 16. */
    size_t limit = 64U;
    if (index == IOLINK_IDX_SERIAL_NUMBER) {
        limit = 16U;
    }
    else if (index == IOLINK_IDX_PRODUCT_URI) {
        limit = 100U;
    }
    size_t len_sz = strlen(str_data);
    if (len_sz > limit) {
        len_sz = limit;
    }
    if (len_sz > sizeof(ctx->response_buf)) {
        len_sz = sizeof(ctx->response_buf);
    }
    (void) memcpy(ctx->response_buf, str_data, len_sz);
    ctx->response_len = (uint8_t) len_sz;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
    return true;
}

/** @brief Read an Application/Function/Location tag from the parameter set. */
static void isdu_tag_read(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    const int res =
        isdu_params_get(ctx, index, 0U, ctx->response_buf, (size_t) IOLINK_ISDU_BUFFER_SIZE);
    if (res >= 0) {
        ctx->response_len = (uint8_t) res;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
    /* On failure leave the state untouched; the transport reset then drops the
       request (same as the original fall-through). */
}

/** @brief Write an Application/Function/Location tag to the parameter set. */
static void isdu_tag_write(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    if (index == IOLINK_IDX_APPLICATION_TAG) {
        if (ctx->buffer_idx > 32U) {
            isdu_respond_param_error(ctx, -2);
            return;
        }
        const int ret = isdu_params_set(ctx, index, 0U, ctx->buffer, ctx->buffer_idx, true);
        if (ret == 0) {
            ctx->response_len = 0U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
        isdu_respond_param_error(ctx, ret);
        return;
    }

    if (isdu_params_set(ctx, index, 0U, ctx->buffer, ctx->buffer_idx, true) == 0) {
        ctx->response_len = 0U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }
    /* Persistent storage rejected the service: terminate with a negative
       response instead of leaving the master polling Busy. */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/** @brief Serve a read or write of a writable identification tag. */
static void isdu_tag_service(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        isdu_tag_write(ctx, index);
    }
    else {
        isdu_tag_read(ctx, index);
    }
}

/** @brief Table B.8/B.2.23-B.2.24: last valid process input/output data. */
static void isdu_read_process_data(iolink_isdu_ctx_t* ctx, bool input)
{
    uint8_t len = 0U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        if (input) {
            const uint8_t* pdi = &dll->pd_in[dll->pd_in_active][dll->od_width];
            len = dll->pd_in_len;
            if (len > IOLINK_PD_IN_MAX_SIZE) {
                len = (uint8_t) IOLINK_PD_IN_MAX_SIZE;
            }
            if (len > 0U) {
                (void) memcpy(ctx->response_buf, pdi, len);
            }
        }
        else {
            len = dll->pd_out_len_current;
            if (len > sizeof(dll->pd_out)) {
                len = (uint8_t) sizeof(dll->pd_out);
            }
            if (len > 0U) {
                (void) memcpy(ctx->response_buf, dll->pd_out, len);
            }
        }
    }
    ctx->response_len = len;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Table B.8/B.2.21: DeviceStatus from the event severity. */
static void isdu_read_device_status(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_EVENTS
    ctx->response_buf[0] =
        iolink_events_get_highest_severity((iolink_events_ctx_t*) ctx->event_ctx);
#else
    ctx->response_buf[0] = 0U;
#endif
    ctx->response_len = 1U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Serve the mandatory identification/status indices (vendor, product, tags, etc.). */
static void handle_mandatory_indices(iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_t* info = isdu_info(ctx);
    if (info == NULL) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    if (isdu_check_mandatory_write(ctx)) {
        return;
    }
    /* B.2.5: 0x000D is an ArrayT; every other object handled here is a
       single-element parameter that only exposes subindex 0. Read-only WRITEs
       were already answered above with WRITE_PROTECTED. */
    if ((ctx->header.subindex != 0U) && (ctx->header.index != IOLINK_IDX_PROFILE_CHARACTERISTIC)) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    const uint16_t index = ctx->header.index;
    switch (index) {
            /* Table B.8: 0x0004-0x000B are reserved. VendorID/DeviceID live in
               Direct Parameter Page 1 (0x07-0x0B), not in an ISDU index. A read of
               0x000A/0x000B therefore falls through to the default IDX_NOTAVAIL. */

        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
            isdu_read_profile_characteristic(ctx, info);
            return;
        case IOLINK_IDX_ERROR_COUNT:
            isdu_read_error_count(ctx);
            return;
        case IOLINK_IDX_VENDOR_NAME:
        case IOLINK_IDX_VENDOR_TEXT:
        case IOLINK_IDX_PRODUCT_NAME:
        case IOLINK_IDX_PRODUCT_ID:
        case IOLINK_IDX_PRODUCT_TEXT:
        case IOLINK_IDX_SERIAL_NUMBER:
        case IOLINK_IDX_HARDWARE_REVISION:
        case IOLINK_IDX_FIRMWARE_REVISION:
        case IOLINK_IDX_PRODUCT_URI:
            (void) isdu_read_identification_string(ctx, info, index);
            return;
        case IOLINK_IDX_APPLICATION_TAG:
        case IOLINK_IDX_FUNCTION_TAG:
        case IOLINK_IDX_LOCATION_TAG:
            isdu_tag_service(ctx, index);
            return;
        case IOLINK_IDX_PROCESS_DATA_INPUT:
            isdu_read_process_data(ctx, true);
            return;
        case IOLINK_IDX_PROCESS_DATA_OUTPUT:
            isdu_read_process_data(ctx, false);
            return;
        case IOLINK_IDX_DEVICE_STATUS:
            isdu_read_device_status(ctx);
            return;

            /* IOLINK_IDX_DETAILED_DEVICE_STATUS (0x0025) is handled earlier in
               handle_standard_commands() via handle_detailed_device_status(). */

        default:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return;
    }
}

/* -------------------------------------------------------------------------
 * SystemCommand and Data Storage upload helpers
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_SYSTEM_COMMAND
/** @brief Clear DS_UPLOAD_FLAG when a reset restored the factory set. */
static void isdu_clear_ds_upload_req(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->ds_ctx != NULL) {
        iolink_ds_clear_upload((iolink_ds_ctx_t*) ctx->ds_ctx);
    }
#else
    (void) ctx;
#endif
}

/** @brief Raise the DS_UPLOAD_REQ event (10.4.2) when a command changed the
 * Data Storage parameter set locally on the Device. */
static void isdu_trigger_ds_upload_req(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->ds_ctx != NULL) {
        /* 10.4.2: raise DS_UPLOAD_FLAG (State_Property bit 7) immediately. The
           corresponding notification is published by the device pump once the
           current ISDU response has been fully read, so the Event flag cannot
           appear inside the DS-command response and make the Master interleave
           a diagnosis read with the ISDU transfer. */
        iolink_ds_request_upload((iolink_ds_ctx_t*) ctx->ds_ctx);
        return;
    }
#endif
#if IOLINK_FEATURE_EVENTS
    if (ctx->event_ctx != NULL) {
        iolink_event_trigger((iolink_events_ctx_t*) ctx->event_ctx,
                             IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
#else
    (void) ctx;
#endif
}
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */

/** @brief Execute a SystemCommand (reset, factory restore, data-storage commands). */
#if IOLINK_FEATURE_DATA_STORAGE
/** @brief Map an iolink_ds_* return code to an ISDU error (Table C.1). */
static uint8_t isdu_ds_result_error(int ret, uint8_t fallback)
{
    switch (ret) {
        case -1:
            return IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL; /* 0x20 */
        case -2:
            return IOLINK_ISDU_ERROR_WRITE_PROTECTED; /* 0x23 */
        case -4:
            return IOLINK_ISDU_ERROR_PARAM_INCONSISTENT; /* 0x41 */
        case -5:
            return IOLINK_ISDU_ERROR_PARAM_SET_INVALID; /* 0x40 */
        case -6:
            return IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL; /* 0x36 */
        default:
            return fallback;
    }
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */

#if IOLINK_FEATURE_SYSTEM_COMMAND
/** @brief 0x80-0x83: device/application reset, factory restore, back-to-box. */
static void isdu_system_reset(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_DEVICE_RESET: /* 0x80 */
            /* Set reset flag for application to handle */
            ctx->reset_pending = true;
            break;

        case IOLINK_CMD_APPLICATION_RESET: /* 0x81 */
            /* Set application reset flag; the parameter set will change, so the
               Master must be prompted to re-upload via DS_UPLOAD_REQ (10.4.2). */
            ctx->app_reset_pending = true;
            isdu_trigger_ds_upload_req(ctx);
            break;

        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82 */
            /* Reset all parameters to factory defaults. Table 177 requires
               DS_UPLOAD_FLAG = 0: the master keeps its stored parameter set
               and re-downloads it, so no upload is requested here. */
            isdu_params_factory_reset(ctx);
            isdu_clear_ds_upload_req(ctx);
            break;

        case IOLINK_CMD_BACK_TO_BOX: /* 0x83 */
            /* Restore the delivery values, then stop and disable communication
               after the response (10.7.5). Table 178 also requires
               DS_UPLOAD_FLAG = 0. */
            isdu_params_factory_reset(ctx);
            isdu_clear_ds_upload_req(ctx);
            if (ctx->dll_ctx != NULL) {
                iolink_dll_request_back_to_box((iolink_dll_ctx_t*) ctx->dll_ctx);
            }
            break;

        default:
            break; /* the dispatcher routes only the four commands here */
    }
}

#if IOLINK_FEATURE_DATA_STORAGE
/**
 * @brief 0x01-0x06 Data Storage commands.
 *
 * @return true when an error response was already produced.
 */
static bool isdu_system_ds(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    if (ctx->ds_ctx == NULL) {
        return false; /* nothing to do: success (empty response) */
    }
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;

    if (cmd == IOLINK_CMD_PARAM_BREAK) { /* 0x06 */
        /* In Idle there is nothing to abort, so this is an unexpected command
           (0324). */
        if (ds->state == IOLINK_DS_STATE_IDLE) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
            return true;
        }
        (void) iolink_ds_abort(ds);
        return false;
    }

    const uint16_t locks = isdu_access_locks(ctx);
    /* ParamDownloadStore finalizes like DownloadEnd and additionally raises
       DS_UPLOAD_REQ (10.4.2). */
    const bool store = (cmd == IOLINK_CMD_PARAM_DOWNLOAD_STORE);
    int ret = 0;
    if (store) {
        /* 0321/0322/0327: ParamDownloadStore is valid in DSIdle_2. Finalize an
           active download, abort an active upload, and always request the
           following upload. */
        if ((locks & IOLINK_LOCK_DS) != 0U) {
            ret = -2;
        }
        else if ((ds->state == IOLINK_DS_STATE_DOWNLOADING) ||
                 (ds->state == IOLINK_DS_STATE_DOWNLOAD_REQ)) {
            ret = iolink_ds_handle_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_END, locks);
        }
        else if ((ds->state == IOLINK_DS_STATE_UPLOADING) ||
                 (ds->state == IOLINK_DS_STATE_UPLOAD_REQ)) {
            (void) iolink_ds_abort(ds);
        }
    }
    else {
        ret = iolink_ds_handle_command(ds, cmd, locks);
    }

    if (ret != 0) {
        isdu_respond_error(ctx, isdu_ds_result_error(ret, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL));
        return true;
    }
    if (store) {
        isdu_trigger_ds_upload_req(ctx);
    }
    return false;
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */

static void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
#if !IOLINK_FEATURE_SYSTEM_COMMAND
    (void) cmd;
    /* SystemCommand support is compiled out. */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
#else
    switch (cmd) {
        case IOLINK_CMD_DEVICE_RESET:             /* 0x80 */
        case IOLINK_CMD_APPLICATION_RESET:        /* 0x81 */
        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82 */
        case IOLINK_CMD_BACK_TO_BOX:              /* 0x83 */
            isdu_system_reset(ctx, cmd);
            break;

            /* Table B.9: 0x84-0x9F are reserved (no SetCommunicationMode command). */

#if IOLINK_FEATURE_DATA_STORAGE
        /* Standard Data Storage Commands (0x01-0x06). */
        case IOLINK_CMD_PARAM_DOWNLOAD_START:
        case IOLINK_CMD_PARAM_DOWNLOAD_END:
        case IOLINK_CMD_PARAM_UPLOAD_START:
        case IOLINK_CMD_PARAM_UPLOAD_END:
        case IOLINK_CMD_PARAM_DOWNLOAD_STORE:
        case IOLINK_CMD_PARAM_BREAK: /* 0x06 */
            if (isdu_system_ds(ctx, cmd)) {
                return;
            }
            break;
#endif /* IOLINK_FEATURE_DATA_STORAGE */

        default:
            /* Unsupported command value: FUNC_NOTAVAIL (Table C.1 / C.2.14). */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
            return;
    }

    /* Success: empty response */
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */
}

void iolink_isdu_execute_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    if (ctx == NULL) {
        return;
    }
    handle_system_command(ctx, cmd);
    /* The SystemCommand on Direct Parameter page 1 provides no positive or
       negative response (7.3.5.3); discard whatever the ISDU handler staged. */
    isdu_transport_reset(ctx);
}

/* -------------------------------------------------------------------------
 * Access locks, detailed status, error stats and direct parameters
 * ------------------------------------------------------------------------- */

/** @brief Read or write the DeviceAccessLocks parameter (index 0x000C). */
static void handle_access_locks(iolink_isdu_ctx_t* ctx)
{
    /* B.2.4: access to DeviceAccessLocks is only permitted via Subindex 0. */
    if (ctx->header.subindex != 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_READ) {
        uint16_t locks = isdu_access_locks(ctx);
        ctx->response_buf[0] = (uint8_t) (locks >> 8);
        ctx->response_buf[1] = (uint8_t) (locks & 0xFF);
        ctx->response_len = 2U;
    }
    else {
        /* Write: Update access locks. Bits 4..15 are reserved and shall be
           rejected with PAR_VALOUTOFRNG (B.2.4 / SDCI_TC_0110). */
        if (ctx->buffer_idx >= 2U) {
            uint16_t new_locks = ((uint16_t) ctx->buffer[0] << 8) | ctx->buffer[1];
            if ((new_locks & (uint16_t) ~IOLINK_ACCESS_LOCKS_IMPLEMENTED) != 0U) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
                return;
            }
            iolink_device_info_ctx_t* info_ctx = isdu_info_ctx(ctx);
            if (info_ctx != NULL) {
                iolink_device_info_ctx_set_access_locks(info_ctx, new_locks);
            }
            else {
                iolink_device_info_set_access_locks(new_locks);
            }
        }
        ctx->response_len = 0U;
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

#if IOLINK_FEATURE_EVENTS
/** @brief Serve DetailedDeviceStatus (Table B.14): ArrayT of 3-octet entries. */
static void handle_detailed_device_status(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    if (ctx->event_ctx == NULL) {
        ctx->response_len = 0U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    IOLINK_CRITICAL_ENTER();
    iolink_events_ctx_t* event_ctx = (iolink_events_ctx_t*) ctx->event_ctx;

    /* Collect the currently active Error/Warning events. B.2.22: only MODE
       "Event appears" entries are listed; a disappeared event drops out, which
       is equivalent to clearing its slot to 0x00/0x0000. */
    uint16_t codes[IOLINK_EVENT_PENDING_MAX];
    iolink_event_type_t types[IOLINK_EVENT_PENDING_MAX];
    uint8_t n = 0U;
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        const iolink_event_pending_t* p = &event_ctx->pending[i];
        if (p->active && (p->type != IOLINK_EVENT_TYPE_NOTIFICATION)) {
            codes[n] = p->code;
            types[n] = p->type;
            n++;
        }
    }

    const uint8_t sub = ctx->header.subindex;
    if ((sub == 0U) || (sub <= n)) {
        const uint8_t start = (sub == 0U) ? 0U : (uint8_t) (sub - 1U);
        const uint8_t end = (sub == 0U) ? n : (uint8_t) (start + 1U);
        uint8_t pos = 0U;
        for (uint8_t i = start; i < end; i++) {
            /* Figure A.24: MODE 11 = appears, TYPE 10 = Warning / 11 = Error,
               INSTANCE 4 = Application. */
            uint8_t qualifier = 0xC0U;
            qualifier |= (types[i] == IOLINK_EVENT_TYPE_ERROR) ? (0x03U << 4) : (0x02U << 4);
            qualifier |= 0x04U;
            ctx->response_buf[pos++] = qualifier;
            ctx->response_buf[pos++] = (uint8_t) (codes[i] >> 8);
            ctx->response_buf[pos++] = (uint8_t) (codes[i] & 0xFFU);
        }
        ctx->response_len = pos;
    }
    else {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
    IOLINK_CRITICAL_EXIT();
}

#endif /* IOLINK_FEATURE_EVENTS */

/** @brief Append a 32-bit value to a buffer in big-endian order, advancing the index. */
static void isdu_write_u32_be(uint8_t* buf, size_t* idx, uint32_t value)
{
    buf[(*idx)++] = (uint8_t) ((value >> 24) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 16) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 8) & 0xFFU);
    buf[(*idx)++] = (uint8_t) (value & 0xFFU);
}

/** @brief Serve the vendor error-statistics index as four big-endian DLL counters. */
static void handle_error_stats(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    if (ctx->header.subindex != 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    if (ctx->dll_ctx == NULL) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }

    iolink_dll_stats_t stats;
    iolink_dll_get_stats((const iolink_dll_ctx_t*) ctx->dll_ctx, &stats);

    size_t idx = 0U;
    isdu_write_u32_be(ctx->response_buf, &idx, stats.crc_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timeout_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.framing_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timing_errors);
    ctx->response_len = (uint8_t) idx;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Return the Direct Parameter page 2 storage (device-specific, addresses 0x10-0x1F). */
static uint8_t* direct_param_page2(iolink_isdu_ctx_t* ctx)
{
    static uint8_t fallback_page2[16] = {0U};

    if ((ctx != NULL) && (ctx->direct_param_page2 != NULL)) {
        return (uint8_t*) ctx->direct_param_page2;
    }
    return fallback_page2;
}

/**
 * @brief Encode a Process Data length (in octets) per IO-Link V1.1.5 Figure B.5.
 *
 * <=2 octets are expressed as bit length (BYTE=0); larger as octets (BYTE=1).
 */
static uint8_t direct_param_encode_pd(uint8_t octets)
{
    if (octets == 0U) {
        return 0x00U;
    }
    if (octets <= 2U) {
        return (uint8_t) (octets * 8U); /* BYTE=0, Length in bits (8 or 16) */
    }
    return (uint8_t) (0x80U | (uint8_t) (octets - 1U)); /* BYTE=1, Length=octets-1 */
}

/**
 * @brief Build the M-sequenceCapability byte (Direct Parameter addr 0x03, Figure B.3).
 *
 * bit0 = ISDU supported, bits1-3 = OPERATE M-sequence code (Table A.10),
 * bits4-5 = PREOPERATE M-sequence code (Table A.8; TYPE_0 = 0 here).
 */
static uint8_t direct_param_mseq_capability(uint8_t m_seq_type, uint8_t od_width)
{
    uint8_t cap = 0x01U; /* ISDU supported */
    /* Table A.10 OPERATE code follows the actual OD width (1/2/8/32). */
    if (od_width == 0U) {
        od_width = iolink_mseq_default_od_width(m_seq_type);
    }
    const uint8_t operate_code = iolink_mseq_operate_code(m_seq_type, od_width);
    cap |= (uint8_t) ((operate_code & 0x07U) << 1);
    /* PREOPERATE M-sequence in this stack is TYPE_0 (code 0) -> bits 4-5 = 0. */
    return cap;
}

/** @brief Populate the 16-octet Direct Parameter page 1 from device-info and DLL state. */
static void build_direct_param_page1(iolink_isdu_ctx_t* ctx, uint8_t* page)
{
    const iolink_device_info_t* info = isdu_info(ctx);
    uint8_t pd_in = 0U;
    uint8_t pd_out = 0U;
    uint8_t m_seq = 0U;
    uint8_t od_width = 0U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        iolink_dll_get_pd_length(dll, &pd_in, &pd_out);
        m_seq = dll->m_seq_type;
        od_width = dll->od_width;
    }

    (void) memset(page, 0, 16);
    /* 0x01 MasterCycleTime is the value the Master wrote during startup. */
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        page[0x01] = dll->master_cycle_time;
    }
    {
        uint16_t min_cyc = (ctx->dll_ctx != NULL)
                               ? (uint16_t) iolink_dll_compute_min_cycle_time(
                                     (const iolink_dll_ctx_t*) ctx->dll_ctx)
                               : ((info != NULL) ? (uint16_t) info->min_cycle_time : 0U);
        /* 0x00 means "no MinCycleTime"; otherwise encode time base | multiplier (Table B.3). */
        page[0x02] = (min_cyc == 0U) ? 0U : iolink_cycle_time_encode(min_cyc);
    }
    page[0x03] = direct_param_mseq_capability(m_seq, od_width); /* M-sequenceCapability */
    /* DPP1 RevisionID is the one-octet protocol revision. The device-info
       field stores the full IODD iolinkRevision (0x0111 for V1.1); the low
       octet is the value transmitted in DPP1 0x04. */
    page[0x04] = (uint8_t) (((info != NULL) ? info->revision_id : 0x0111U) & 0xFFU);
    page[0x05] = direct_param_encode_pd(pd_in);  /* ProcessDataIn */
    page[0x06] = direct_param_encode_pd(pd_out); /* ProcessDataOut */
    if (info != NULL) {
        page[0x07] = (uint8_t) (info->vendor_id >> 8);            /* VendorID MSB */
        page[0x08] = (uint8_t) (info->vendor_id & 0xFFU);         /* VendorID LSB */
        page[0x09] = (uint8_t) ((info->device_id >> 16) & 0xFFU); /* DeviceID octet2 */
        page[0x0A] = (uint8_t) ((info->device_id >> 8) & 0xFFU);  /* DeviceID octet1 */
        page[0x0B] = (uint8_t) (info->device_id & 0xFFU);         /* DeviceID octet0 */
    }
    /* 0x0C-0x0E reserved (0); 0x0F SystemCommand (W) reads 0. */
}

uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr)
{
    uint8_t page[16];

    if ((ctx == NULL) || (addr >= sizeof(page))) {
        return 0U;
    }

    build_direct_param_page1(ctx, page);
    return page[addr];
}

/** @brief Read/write Direct Parameter pages 1 (read-only) and 2 (device-specific). */
static void handle_direct_parameters(iolink_isdu_ctx_t* ctx)
{
    bool page2 = (ctx->header.index == IOLINK_IDX_DIRECT_PARAMETERS_2);
    uint8_t sub = ctx->header.subindex;
    uint8_t* page2_storage = direct_param_page2(ctx);

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        if (!page2) {
            /* Page 1 is read-only. */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        }
        else if ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U) {
            /* Table B.2.4: the Parameter access lock inhibits page-2 writes. */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        }
        else if (sub == 0U) {
            size_t n = ctx->buffer_idx;
            if (n > 16U) {
                n = 16U;
            }
            (void) memcpy(page2_storage, ctx->buffer, n);
            ctx->response_len = 0U;
        }
        else if ((sub <= 16U) && (ctx->buffer_idx >= 1U)) {
            page2_storage[sub - 1U] = ctx->buffer[0];
            ctx->response_len = 0U;
        }
        else {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        }
    }
    else {
        uint8_t page[16];
        if (page2) {
            (void) memcpy(page, page2_storage, sizeof(page));
        }
        else {
            build_direct_param_page1(ctx, page);
        }

        if (sub == 0U) {
            (void) memcpy(ctx->response_buf, page, sizeof(page));
            ctx->response_len = sizeof(page);
        }
        else if (sub <= 16U) {
            ctx->response_buf[0] = page[sub - 1U]; /* Unimplemented bytes already read 0 */
            ctx->response_len = 1U;
        }
        else {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        }
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/* -------------------------------------------------------------------------
 * Data Storage services
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_DATA_STORAGE
/** @brief Serve the Data Storage index: backup (read image) or restore (apply image). */
static size_t isdu_append_ds_index_list(iolink_ds_ctx_t* ds, uint8_t* buf, size_t pos)
{
    const size_t n = iolink_ds_param_count(ds);
    for (size_t i = 0U; i < n; i++) {
        iolink_ds_param_desc_t d;
        if (!iolink_ds_param_at(ds, i, &d)) {
            break;
        }
        if ((pos + 3U) > IOLINK_ISDU_BUFFER_SIZE) {
            break;
        }
        buf[pos++] = (uint8_t) (d.index >> 8);
        buf[pos++] = (uint8_t) (d.index & 0xFFU);
        buf[pos++] = d.subindex;
    }
    if ((pos + 2U) <= IOLINK_ISDU_BUFFER_SIZE) {
        buf[pos++] = 0x00U; /* Termination marker */
        buf[pos++] = 0x00U;
    }
    return pos;
}

/** @brief DS write subindex 0: legacy image restore, or read-only reject. */
static void isdu_ds_write_sub0(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
#if IOLINK_DS_LEGACY_IMAGE_SUB0
    /* Legacy whole-image restore (kept for wire compatibility). */
    const int rc = iolink_ds_apply_image(ds, ctx->buffer, ctx->buffer_idx);
    if (rc != 0) {
        isdu_respond_error(ctx, (rc == -1) ? IOLINK_ISDU_ERROR_PARAM_SET_INVALID
                                           : IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
    }
    else {
        ctx->response_len = 0U; /* Write acknowledged */
    }
#else
    (void) ds;
    /* Table B.10: the whole record is read-only; only DS_Command (subindex 1)
       is writable. */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
#endif
}

/** @brief DS write subindex 1: DS_Command. */
static void isdu_ds_write_command(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
    if (ctx->buffer_idx == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);
        return;
    }
    const uint16_t locks = isdu_access_locks(ctx);
    const int ret = iolink_ds_handle_ds_command(ds, ctx->buffer[0], locks);
    if (ret == 0) {
        ctx->response_len = 0U;
    }
    else {
        isdu_respond_error(ctx, isdu_ds_result_error(ret, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL));
    }
}

/** @brief DS read subindex 0: concatenated record (or legacy image). */
static void isdu_ds_read_record(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
#if IOLINK_DS_LEGACY_IMAGE_SUB0
    /* Legacy whole-image backup read. */
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(ds, &len);
    if ((img == NULL) || (len > IOLINK_ISDU_BUFFER_SIZE)) {
        ctx->response_len = 0U;
    }
    else {
        if (len > 0U) {
            (void) memcpy(ctx->response_buf, img, len);
        }
        ctx->response_len = len;
    }
#else
    /* Table B.10/B.11 (SDCI_TC_0107): sub0 is the concatenated record:
       DS_Command, State_Property, Data_Storage_Size, Parameter_Checksum and
       Index_List. */
    if (!iolink_ds_downloading(ds)) {
        (void) iolink_ds_build_image(ds);
    }
    size_t pos = 0U;
    ctx->response_buf[pos++] = 0x00U; /* DS_Command read: unspecified */
    ctx->response_buf[pos++] = iolink_ds_state_property(ds);
    isdu_write_u32_be(ctx->response_buf, &pos, iolink_ds_size(ds));
    isdu_write_u32_be(ctx->response_buf, &pos, iolink_ds_checksum(ds));
    pos = isdu_append_ds_index_list(ds, ctx->response_buf, pos);
    ctx->response_len = (uint8_t) pos;
#endif
}

/** @brief DS read subindices 1-5. */
static void isdu_ds_read_subindex(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds, uint8_t sub)
{
    switch (sub) {
        case 1U:
            /* DS_Command read returns unspecified values (Table B.10). Test Spec
               0108 requires one octet with a value in 0..5. */
            ctx->response_buf[0] = 0x00U;
            ctx->response_len = 1U;
            break;
        case 2U:
            ctx->response_buf[0] = iolink_ds_state_property(ds);
            ctx->response_len = 1U;
            break;
        case 3U: {
            size_t idx = 0U;
            isdu_write_u32_be(ctx->response_buf, &idx, iolink_ds_size(ds));
            ctx->response_len = (uint8_t) idx;
            break;
        }
        case 4U: {
            /* Table B.10: Parameter_Checksum reflects the current parameter set.
               A direct (non-download) parameter write does not rebuild the DS
               image, so refresh it before reporting the checksum; otherwise the
               value read before ParamDownloadStore differs from the value after
               it. */
            if (!iolink_ds_downloading(ds)) {
                (void) iolink_ds_build_image(ds);
            }
            size_t idx = 0U;
            isdu_write_u32_be(ctx->response_buf, &idx, iolink_ds_checksum(ds));
            ctx->response_len = (uint8_t) idx;
            break;
        }
        case 5U: {
            /* Index_List (Table B.11): [index(2) subindex(1)]*n + 0x0000. */
            const size_t pos = isdu_append_ds_index_list(ds, ctx->response_buf, 0U);
            ctx->response_len = (uint8_t) pos;
            break;
        }
        default:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            break;
    }
}

static void handle_data_storage(iolink_isdu_ctx_t* ctx)
{
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
    const uint8_t sub = ctx->header.subindex;

    if (ds == NULL) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        if (sub == 0U) {
            isdu_ds_write_sub0(ctx, ds);
        }
        else if (sub == 1U) {
            isdu_ds_write_command(ctx, ds);
        }
        else {
            /* Table B.10: subindices 02-05 are read-only. */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        }
    }
    else if (sub == 0U) {
        isdu_ds_read_record(ctx, ds);
    }
    else {
        isdu_ds_read_subindex(ctx, ds, sub);
    }

    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

#endif /* IOLINK_FEATURE_DATA_STORAGE */

#if IOLINK_FEATURE_DATA_STORAGE
/**
 * @brief 10.6.11 / SDCI_TC_0325/0326: parameter writes are locked during an
 * active Block Upload, and parameter reads during an active Block Download.
 * SystemCommand and DataStorageIndex remain accessible.
 *
 * @return true when a negative response was produced.
 */
static bool isdu_reject_ds_access(iolink_isdu_ctx_t* ctx)
{
    const uint16_t index = ctx->header.index;
    if ((ctx->ds_ctx == NULL) || (index == IOLINK_IDX_SYSTEM_COMMAND) ||
        (index == IOLINK_IDX_DATA_STORAGE) || (index == IOLINK_IDX_DIRECT_PARAMETERS_1) ||
        (index == IOLINK_IDX_DIRECT_PARAMETERS_2)) {
        return false;
    }
    const iolink_ds_ctx_t* ds = (const iolink_ds_ctx_t*) ctx->ds_ctx;
    const bool blocked = ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) &&
                          (ds->state == IOLINK_DS_STATE_UPLOADING)) ||
                         ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_READ) &&
                          (ds->state == IOLINK_DS_STATE_DOWNLOADING));
    if (!blocked) {
        return false;
    }
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);
    return true;
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */

/* -------------------------------------------------------------------------
 * Index routing and standard dispatch
 * ------------------------------------------------------------------------- */

/** @brief Serve a device-specific (vendor) index through the application hook. */
static void isdu_handle_vendor_index(iolink_isdu_ctx_t* ctx)
{
    if ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) &&
        ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U)) {
        /* B.2.4: the Parameter lock inhibits every read/write Device parameter
           (reads remain available). */
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    if ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) && (ctx->ds_ctx != NULL) &&
        iolink_ds_downloading((const iolink_ds_ctx_t*) ctx->ds_ctx)) {
        const int ret = iolink_ds_stage_param((iolink_ds_ctx_t*) ctx->ds_ctx, ctx->header.index,
                                              ctx->header.subindex, ctx->buffer, ctx->buffer_idx);
        if (ret == 0) {
            ctx->response_len = 0U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
        else {
            isdu_respond_param_error(ctx, ret);
        }
        return;
    }
#endif
#if IOLINK_ISDU_BUFFER_SIZE > 238U
    size_t length = 235U;
#else
    size_t length = sizeof(ctx->resp_buf) - 3U;
#endif
    const size_t capacity = length;
    const uint8_t error =
        ctx->vendor_service(ctx->vendor_user, ctx->header.index, ctx->header.subindex,
                            ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE, ctx->buffer,
                            ctx->buffer_idx, ctx->response_buf, &length);
    if ((length > capacity) || (error != 0U)) {
        isdu_respond_error(ctx, (length > capacity) ? IOLINK_ISDU_ERROR_IDX_NOT_AVAIL : error);
        return;
    }
    ctx->response_len = (uint8_t) length;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief SystemCommand index: write-only command octet. */
static void isdu_handle_system_command_index(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        /* SystemCommand (Table B.8) is write-only; a read is answered with
           IDX_NOT_ACCESSIBLE (Table C.1). Events are read through the
           Diagnosis-channel event memory (Table 58). */
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
        return;
    }
    if (ctx->buffer_idx == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    handle_system_command(ctx, ctx->buffer[0]);
}

/** @brief Dispatch an executed ISDU request to the handler for its index. */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx)
{
    ctx->response_negative = false;
    /* B.2.27: Preferred Indices 0x0040-0x00FE are device-specific like the
       Extended Index range from 0x0100; both reach the application vendor
       service. 0x00FF itself is reserved. */
    const uint16_t index = ctx->header.index;
#if IOLINK_FEATURE_DATA_STORAGE
    if (isdu_reject_ds_access(ctx)) {
        return;
    }
#endif
    const bool device_specific = ((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U);
    if (device_specific && (ctx->vendor_service != NULL)) {
        isdu_handle_vendor_index(ctx);
    }
    else if (index == IOLINK_IDX_SYSTEM_COMMAND) {
        isdu_handle_system_command_index(ctx);
    }
    else if ((index == IOLINK_IDX_DIRECT_PARAMETERS_1) ||
             (index == IOLINK_IDX_DIRECT_PARAMETERS_2)) {
        handle_direct_parameters(ctx);
    }
#if IOLINK_FEATURE_DATA_STORAGE
    else if (index == IOLINK_IDX_DATA_STORAGE) {
        handle_data_storage(ctx);
    }
#endif
    else if (index == IOLINK_IDX_DEVICE_ACCESS_LOCKS) {
        handle_access_locks(ctx);
    }
#if IOLINK_FEATURE_EVENTS
    else if (index == IOLINK_IDX_DETAILED_DEVICE_STATUS) {
        handle_detailed_device_status(ctx);
    }
#endif
    else if (index == IOLINK_IDX_ERROR_STATS) {
        handle_error_stats(ctx);
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
    else {
        handle_mandatory_indices(ctx);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void iolink_isdu_process(iolink_isdu_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands(ctx);
        if (ctx->state != ISDU_STATE_RESPONSE_READY) {
            isdu_transport_reset(ctx);
        }
        else {
            isdu_frame_response(ctx);
        }
    }
}

int iolink_isdu_get_response_byte(iolink_isdu_ctx_t* ctx, uint8_t* byte)
{
    if ((ctx == NULL) || (byte == NULL)) {
        return 0;
    }
    if (ctx->response_idx >= ctx->response_len) {
        return 0;
    }
    *byte = ctx->response_buf[ctx->response_idx++];
    return 1;
}

uint8_t iolink_isdu_snapshot_response(const iolink_isdu_ctx_t* ctx, bool* busy, uint8_t* dst,
                                      size_t dst_size)
{
    if (busy != NULL) {
        *busy = false;
    }
    if (ctx == NULL) {
        return 0U;
    }
    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        if (busy != NULL) {
            *busy = true;
        }
        return 0U;
    }
    if ((ctx->state != ISDU_STATE_RESPONSE_READY) || (dst == NULL) || (dst_size == 0U)) {
        return 0U;
    }
    size_t n = ctx->resp_total;
    if (n > dst_size) {
        n = dst_size;
    }
    /* isr_isdu_len is a uint8_t; the spec caps framed responses at 241 octets,
       so this clamp is defensive only. */
    if (n > 255U) {
        n = 255U;
    }
    if (n > 0U) {
        (void) memcpy(dst, ctx->resp_buf, n);
    }
    return (uint8_t) n;
}
