/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file fuzz_isdu.c
 * @brief libFuzzer target: ISDU reassembly, service execution and ABORT.
 *
 * The input is an ISDU payload (`index`, `subindex`, value octets). The harness
 * feeds it to the engine twice — in one START segment, and one octet at a time
 * with the COUNT sequence in between and the main pump running between the ISR
 * segments — and requires the same outcome: the drained payload, the framed
 * reply published for the DLL, and the busy flag. That equality is the property
 * the ISR write path and the main pump coordinate on.
 *
 * A third pass drives ABORT after the transfer: it must reset the stage without
 * disturbing the buffers or the published reply.
 *
 * What the target asserts (in addition to the sanitizers):
 *  - the response never exceeds the context's response buffer;
 *  - draining yields exactly the length the engine announced;
 *  - one-shot and chunked delivery agree, including the framed reply;
 *  - the reported state stays inside its enumeration.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/od.h"
#include "iolinki/protocol.h"

#define FUZZ_MAX_PAYLOAD 200U /**< fits the uint8_t segment length and the buffer */

typedef struct
{
    uint8_t payload[IOLINK_ISDU_BUFFER_SIZE];
    size_t payload_len;
    uint8_t framed[IOLINK_ISDU_BUFFER_SIZE];
    uint8_t framed_len;
    bool busy;
} run_result_t;

static iolink_isdu_ctx_t g_ctx;
static uint8_t g_param;

/* One application parameter, so the app-table dispatch is reachable too. */
static const iolink_od_entry_t k_entry = {
    .index = 0x0040U,
    .subindex = 0U,
    .max_subindex = 0U,
    .datatype = IOLINK_OD_DT_U8,
    .access = IOLINK_OD_ACCESS_RW,
    .write_semantics = IOLINK_OD_WRITE_VOLATILE,
    .storage = IOLINK_OD_STORAGE_VOLATILE,
    .flags = 0U,
    .handler = IOLINK_OD_HANDLER_NONE,
    .backend = IOLINK_OD_BACKEND_STORAGE,
    .u.storage = {.value = &g_param, .str_len = NULL},
    .block_len = 0U,
    .range_min = 0U,
    .range_max = 255U,
    .scalar_default = 0U,
    .block_default = NULL,
    .name = "fuzz",
    .validate = NULL,
    .on_change = NULL,
};

static const iolink_od_app_t k_app = {.entries = &k_entry, .count = 1U};

static void engine_start(void)
{
    (void) memset(&g_ctx, 0, sizeof(g_ctx));
    g_param = 0U;
    iolink_isdu_init(&g_ctx);
    iolink_isdu_bind_od(&g_ctx, &k_app, NULL);
    iolink_isdu_bind_caps(&g_ctx, NULL);
}

/** @brief One run: deliver @p payload as one segment or octet by octet. */
static void run_isdu(const uint8_t* payload, size_t len, bool chunked, run_result_t* out)
{
    engine_start();

    if (!chunked) {
        iolink_isdu_od_write(&g_ctx, IOLINK_FLOWCTRL_START, payload, (uint8_t) len);
    }
    else if (len > 0U) {
        iolink_isdu_od_write(&g_ctx, IOLINK_FLOWCTRL_START, payload, 1U);
        /* START is 0x10, so the first COUNT segment is 1: the engine expects
         * `(last_flowctrl + 1) & 0x0F`. The main pump runs between segments,
         * exactly as it does between two received frames. */
        for (size_t i = 1U; i < len; i++) {
            iolink_isdu_process(&g_ctx);
            iolink_isdu_od_write(&g_ctx, (uint8_t) (i & IOLINK_FLOWCTRL_COUNT_MASK), payload + i,
                                 1U);
        }
    }

    iolink_isdu_process(&g_ctx);

    if ((g_ctx.response_len > g_ctx.response_size) ||
        ((uint32_t) g_ctx.state > (uint32_t) ISDU_STATE_RESPONSE_READY)) {
        abort();
    }

    /* The framed reply the DLL sends: a write answers here, not in the payload
     * buffer, so comparing payload lengths alone would assert nothing. */
    const uint8_t framed_len =
        iolink_isdu_snapshot_response(&g_ctx, &out->busy, out->framed, sizeof(out->framed));
    out->framed_len = framed_len;

    out->payload_len = 0U;
    uint8_t byte = 0U;
    while (iolink_isdu_get_response_byte(&g_ctx, &byte) != 0) {
        if (out->payload_len < sizeof(out->payload)) {
            out->payload[out->payload_len] = byte;
        }
        out->payload_len++;
    }
    if (out->payload_len != g_ctx.response_len) {
        /* The engine announced more (or fewer) octets than it hands out. */
        abort();
    }
}

/** @brief ABORT must drop a partial transfer without disturbing the buffers. */
static void run_abort(const uint8_t* payload, size_t len)
{
    engine_start();
    if (len > 0U) {
        iolink_isdu_od_write(&g_ctx, IOLINK_FLOWCTRL_START, payload, (uint8_t) len);
    }
    iolink_isdu_od_write(&g_ctx, IOLINK_FLOWCTRL_ABORT, payload, 0U);
    iolink_isdu_process(&g_ctx);
    if ((g_ctx.response_len > g_ctx.response_size) ||
        ((uint32_t) g_ctx.state > (uint32_t) ISDU_STATE_RESPONSE_READY)) {
        abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 3U) {
        return 0;
    }

    const size_t len = (size > FUZZ_MAX_PAYLOAD) ? FUZZ_MAX_PAYLOAD : size;
    static run_result_t one_shot;
    static run_result_t chunked;

    run_isdu(data, len, false, &one_shot);
    run_isdu(data, len, true, &chunked);

    if ((one_shot.payload_len != chunked.payload_len) ||
        (one_shot.framed_len != chunked.framed_len) || (one_shot.busy != chunked.busy)) {
        abort();
    }
    if ((one_shot.payload_len > 0U) &&
        (memcmp(one_shot.payload, chunked.payload, one_shot.payload_len) != 0)) {
        abort();
    }
    if ((one_shot.framed_len > 0U) &&
        (memcmp(one_shot.framed, chunked.framed, one_shot.framed_len) != 0)) {
        abort();
    }

    run_abort(data, len);
    return 0;
}
