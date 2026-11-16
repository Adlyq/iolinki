/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file fuzz_od.c
 * @brief libFuzzer target: object-dictionary resolution and value codecs.
 *
 * The input is `index` (big endian), `subindex`, a length and value octets. The
 * harness resolves the entry against the standard table plus a small application
 * table, then runs the codec, default, read, write and restore paths on it.
 *
 * What the target asserts (in addition to the sanitizers):
 *  - a lookup either misses or returns an entry that really belongs to one of
 *    the two tables (`iolink_od_at()` / the application array);
 *  - `iolink_od_at()` is NULL exactly past the end of the standard table;
 *  - codec and default output never exceed `IOLINK_OD_MAX_SIZE`;
 *  - the application storage is only touched through its declared width.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "iolinki/od.h"
#include "iolinki/protocol.h"

#define FUZZ_MAX_VALUE 12U
#define FUZZ_MAX_OUT IOLINK_OD_MAX_SIZE

static uint8_t g_scalar;
static uint8_t g_u16[2];
static uint8_t g_block[8];
static uint8_t g_block_len;

static const iolink_od_entry_t k_entries[] = {
    {
        .index = 0x0040U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U8,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = &g_scalar, .str_len = NULL},
        .range_min = 0U,
        .range_max = 255U,
        .scalar_default = 0U,
        .name = "fuzz.scalar",
    },
    {
        .index = 0x0041U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U16,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = g_u16, .str_len = NULL},
        .range_min = 0U,
        .range_max = 65535U,
        .scalar_default = 0U,
        .name = "fuzz.u16",
    },
    {
        .index = 0x0042U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_OCTET_STRING,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = g_block, .str_len = &g_block_len},
        .block_len = sizeof(g_block),
        .name = "fuzz.block",
    },
};

static const iolink_od_app_t k_app = {.entries = k_entries, .count = 3U};

/** @brief Is @p entry one of the standard table's entries? */
static bool in_standard_table(const iolink_od_entry_t* entry)
{
    const size_t count = iolink_od_count();
    for (size_t i = 0U; i < count; i++) {
        if (iolink_od_at(i) == entry) {
            return true;
        }
    }
    return false;
}

/** @brief Is @p entry one of the application table's entries? */
static bool in_app_table(const iolink_od_entry_t* entry)
{
    return (entry >= &k_entries[0]) && (entry < &k_entries[3]);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 4U) {
        return 0;
    }

    const uint16_t index = (uint16_t) (((uint16_t) data[0] << 8) | (uint16_t) data[1]);
    const uint8_t subindex = data[2];
    size_t len = data[3];
    if (len > FUZZ_MAX_VALUE) {
        len = FUZZ_MAX_VALUE;
    }
    if (len > (size - 4U)) {
        len = size - 4U;
    }
    const uint8_t* value = data + 4U;

    /* The standard table boundary is exact. */
    const size_t std_count = iolink_od_count();
    if ((std_count > 0U) && (iolink_od_at(std_count - 1U) == NULL)) {
        abort();
    }
    if (iolink_od_at(std_count) != NULL) {
        abort();
    }

    const iolink_od_entry_t* entry = iolink_od_find(&k_app, index, subindex);
    if (entry != NULL) {
        if (!in_standard_table(entry) && !in_app_table(entry)) {
            /* A lookup must never invent an entry. */
            abort();
        }
    }

    (void) iolink_od_is_ds(&k_app, index, subindex);
    (void) iolink_od_ds_count_app(&k_app);
    (void) iolink_od_ds_at_app(&k_app, (size_t) index);

    if (entry == NULL) {
        /* Most 16-bit indices miss both tables. Fall back to a deterministic
         * entry so every input also drives a codec/read/write path instead of
         * returning after the lookup. */
        const size_t std_count = iolink_od_count();
        if (std_count > 0U) {
            entry = iolink_od_at((size_t) data[0] % std_count);
        }
        else {
            entry = &k_entries[0];
        }
    }
    if (entry == NULL) {
        return 0;
    }

    uint8_t out[FUZZ_MAX_OUT];
    uint32_t decoded = 0U;

    (void) iolink_od_check_value(entry, NULL, value, len);
    (void) iolink_od_decode(entry, value, len, &decoded);
    (void) iolink_od_encode(entry, decoded, out, sizeof(out));

    if (iolink_od_default(entry, out, sizeof(out)).len > (int) IOLINK_OD_MAX_SIZE) {
        abort();
    }

    iolink_od_ctx_t ctx = {.app = &k_app, .self = NULL};
    (void) iolink_od_read_value(&ctx, entry, subindex, out, sizeof(out));
    (void) iolink_od_write_value(&ctx, entry, subindex, value, len);
    (void) iolink_od_restore_value(&ctx, entry, subindex, value, len);

    if (g_block_len > sizeof(g_block)) {
        /* The OctetString length variable must never leave its buffer. */
        abort();
    }
    return 0;
}
