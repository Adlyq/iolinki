/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_STORAGE_H
#define IOLINK_STORAGE_H

#include "iolinki/config.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @file storage.h
 * @brief Unified non-volatile storage backend and persistent-region layout.
 *
 * The application supplies exactly one iolink_storage_api_t through
 * iolink_device_config_t::storage. The stack addresses it with a fixed,
 * compile-time region layout (relative offsets); the application adds its own
 * physical base (e.g. an EEPROM address) inside the callbacks.
 */

/** @brief Commit policy for persistent writes. */
typedef enum
{
    IOLINK_PERSIST_NONE = 0U,  /**< RAM only. */
    IOLINK_PERSIST_SYNC = 1U,  /**< Commit before the call returns. */
    IOLINK_PERSIST_ASYNC = 2U  /**< Queue the commit; the callback owns the buffer copy. */
} iolink_persist_t;

/**
 * @brief Unified non-volatile storage backend.
 *
 * All offsets are relative to the storage area owned by the application. The
 * callbacks must be non-blocking for the ASYNC policy, or complete the access
 * synchronously for SYNC. @p user is the application pointer from the config.
 */
typedef struct
{
    void* user; /**< Opaque application pointer. */
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
    int (*erase)(void* user, uint32_t offset, size_t len);
} iolink_storage_api_t;

/* --- Persistent region layout (relative offsets) -------------------------
 *
 * PARAMS | DS image | DS upload flag
 *
 * The layout is fixed at compile time so an application can size its NVM area
 * with a static assertion:
 *     _Static_assert(sizeof(nvm_region) >= IOLINK_STORAGE_TOTAL_SIZE, "...");
 *
 * Versioning rules:
 *  - IOLINK_STORAGE_MAGIC identifies a region written by this stack. A change
 *    of the record layout (fields, order, sizes) MUST change the magic so an
 *    old image cannot be misinterpreted by the new field positions.
 *  - IOLINK_STORAGE_VERSION identifies the layout within one magic and must be
 *    bumped on any layout change that keeps the magic. A mismatch discards the
 *    record (no migration callback yet).
 */

#define IOLINK_STORAGE_MAGIC 0x494F4C32UL /* "IOL2": layout carries a version */
#define IOLINK_STORAGE_VERSION 1U         /* bump on record-layout change */

/**
 * @brief Fletcher-32 checksum for a persistent record.
 *
 * Detects bit flips and torn writes that magic/version cannot see. Sums are
 * reduced modulo 65535 so the result fits a uint32_t.
 */
static inline uint32_t iolink_storage_checksum(const uint8_t* data, size_t len)
{
    uint32_t sum1 = 0U;
    uint32_t sum2 = 0U;
    if (data == NULL) {
        return 0U;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (sum1 + (uint32_t) data[i]) % 65535U;
        sum2 = (sum2 + sum1) % 65535U;
    }
    return (sum2 << 16U) | sum1;
}

/** @brief Persistent parameter record (three identification tags). */
typedef struct
{
    uint32_t magic;          /**< IOLINK_STORAGE_MAGIC. */
    uint16_t version;        /**< IOLINK_STORAGE_VERSION. */
    uint16_t reserved;       /**< Alignment / future use. */
    char application_tag[33]; /**< ApplicationSpecificTag, NUL-terminated. */
    char function_tag[33];    /**< FunctionTag, NUL-terminated. */
    char location_tag[33];    /**< LocationTag, NUL-terminated. */
    uint32_t checksum;        /**< Fletcher-32 over the preceding fields. */
} iolink_params_nvm_t;

#define IOLINK_STORAGE_ALIGN4(x) (((x) + 3U) & ~3U)

#define IOLINK_STORAGE_PARAMS_BASE 0U
#define IOLINK_STORAGE_PARAMS_SIZE IOLINK_STORAGE_ALIGN4((uint32_t) sizeof(iolink_params_nvm_t))
#define IOLINK_STORAGE_DS_BASE IOLINK_STORAGE_PARAMS_SIZE
/** DS header: magic(4) + version(2) + length(2) + checksum(4), then the image. */
#define IOLINK_STORAGE_DS_HDR_SIZE 12U
#define IOLINK_STORAGE_DS_SIZE (IOLINK_STORAGE_DS_HDR_SIZE + (uint32_t) IOLINK_DS_IMAGE_MAX)
#define IOLINK_STORAGE_FLAG_BASE (IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_SIZE)
#define IOLINK_STORAGE_FLAG_SIZE 4U
#define IOLINK_STORAGE_TOTAL_SIZE \
    IOLINK_STORAGE_ALIGN4(IOLINK_STORAGE_FLAG_BASE + IOLINK_STORAGE_FLAG_SIZE)

/* The DS region must follow the parameter record without overlap. */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(IOLINK_STORAGE_PARAMS_SIZE >= sizeof(iolink_params_nvm_t),
               "params region smaller than its record");
_Static_assert(IOLINK_STORAGE_TOTAL_SIZE >= IOLINK_STORAGE_FLAG_BASE + IOLINK_STORAGE_FLAG_SIZE,
               "storage region layout overflow");
#endif

#endif /* IOLINK_STORAGE_H */
