/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file data_storage.c
 * @brief IO-Link Data Storage (DS) parameter server.
 * @ingroup iolinki_data_storage
 *
 * The Master keeps an opaque backup of a device's parameters so a replacement
 * unit can be restored automatically. The device serializes its DS-backed
 * parameters into an image (DS_Data, index 0x0003) for upload, and applies an
 * image received from the Master on download. Each record is laid out as:
 *   [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)]
 */

#include "iolinki/config.h"
#include "iolinki/data_storage.h"
#include "iolinki/params.h"
#include "iolinki/utils.h"
#include <string.h>

#if IOLINK_FEATURE_DATA_STORAGE

/** @brief Persist DS_UPLOAD_FLAG into the flag region (10.4.2). */
static void ds_storage_save_flag(iolink_ds_ctx_t* ctx, uint8_t flag)
{
    if ((ctx != NULL) && (ctx->storage != NULL) && (ctx->storage->write != NULL)) {
        uint8_t value = (flag != 0U) ? 1U : 0U;
        (void) ctx->storage->write(ctx->storage->user, IOLINK_STORAGE_FLAG_BASE, &value, 1U);
    }
}

static void ds_storage_load_flag(iolink_ds_ctx_t* ctx)
{
    if ((ctx != NULL) && (ctx->storage != NULL) && (ctx->storage->read != NULL)) {
        uint8_t value = 0U;
        if (ctx->storage->read(ctx->storage->user, IOLINK_STORAGE_FLAG_BASE, &value, 1U) == 0) {
            /* Only an explicit 1 requests an upload; erased (0xFF) means "none". */
            ctx->ds_upload_flag = (value == 1U);
        }
    }
}

/** @brief Persist the DS image (magic + version + length + payload). */
static void ds_storage_save_image(iolink_ds_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->storage == NULL) || (ctx->storage->write == NULL)) {
        return;
    }
    uint8_t header[IOLINK_STORAGE_DS_HDR_SIZE];
    const uint32_t checksum = iolink_storage_checksum(ctx->image, ctx->image_len);
    header[0] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 24);
    header[1] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 16);
    header[2] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 8);
    header[3] = (uint8_t) IOLINK_STORAGE_MAGIC;
    header[4] = (uint8_t) (IOLINK_STORAGE_VERSION & 0xFFU);
    header[5] = (uint8_t) (IOLINK_STORAGE_VERSION >> 8);
    header[6] = (uint8_t) (ctx->image_len >> 8);
    header[7] = (uint8_t) (ctx->image_len & 0xFFU);
    header[8] = (uint8_t) (checksum >> 24);
    header[9] = (uint8_t) (checksum >> 16);
    header[10] = (uint8_t) (checksum >> 8);
    header[11] = (uint8_t) checksum;
    (void) ctx->storage->write(ctx->storage->user, IOLINK_STORAGE_DS_BASE, header,
                               sizeof(header));
    if (ctx->image_len > 0U) {
        (void) ctx->storage->write(ctx->storage->user,
                                   IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_HDR_SIZE,
                                   ctx->image, ctx->image_len);
    }
}

static void ds_storage_load_image(iolink_ds_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->storage == NULL) || (ctx->storage->read == NULL)) {
        return;
    }
    uint8_t header[IOLINK_STORAGE_DS_HDR_SIZE];
    if (ctx->storage->read(ctx->storage->user, IOLINK_STORAGE_DS_BASE, header, sizeof(header)) !=
        0) {
        return;
    }
    const uint32_t magic = ((uint32_t) header[0] << 24) | ((uint32_t) header[1] << 16) |
                           ((uint32_t) header[2] << 8) | (uint32_t) header[3];
    const uint16_t version = (uint16_t) (((uint16_t) header[5] << 8) | (uint16_t) header[4]);
    const size_t len = ((size_t) header[6] << 8) | (size_t) header[7];
    const uint32_t checksum = ((uint32_t) header[8] << 24) | ((uint32_t) header[9] << 16) |
                              ((uint32_t) header[10] << 8) | (uint32_t) header[11];
    if ((magic != IOLINK_STORAGE_MAGIC) || (version != (uint16_t) IOLINK_STORAGE_VERSION)) {
        return; /* Uninitialised or a different layout. */
    }
    if ((len == 0U) || (len > IOLINK_DS_IMAGE_MAX)) {
        return;
    }
    if (ctx->storage->read(ctx->storage->user,
                           IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_HDR_SIZE, ctx->image,
                           len) == 0) {
        if (checksum != iolink_storage_checksum(ctx->image, len)) {
            return; /* Corrupt (bit flip / torn write). */
        }
        ctx->image_len = len;
        ctx->current_checksum = iolink_ds_calc_checksum(ctx->image, len);
    }
}

/* Parameters included in the Data Storage set by default (V1.1.5 device
   identification parameters that are writable and therefore worth backing up).
   The application replaces or extends this via iolink_ds_bind_param_access(). */
static const iolink_ds_param_desc_t k_default_ds_params[] = {
    {IOLINK_IDX_APPLICATION_TAG, 0U, 0U},
    {IOLINK_IDX_FUNCTION_TAG, 0U, 0U},
    {IOLINK_IDX_LOCATION_TAG, 0U, 0U},
};

#define DS_PARAM_COUNT (sizeof(k_default_ds_params) / sizeof(k_default_ds_params[0]))
#define DS_RECORD_HEADER 4U /* index(2) + subindex(1) + length(1) */

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_ds_ctx_t))) {
        return;
    }
    ctx->storage = storage;
    ctx->state = IOLINK_DS_STATE_IDLE;
    /* Default parameter set: the three standard tags, served through the
       legacy params context (or a caller-bound accessor). */
    ctx->params = k_default_ds_params;
    ctx->param_count = DS_PARAM_COUNT;
    ds_storage_load_image(ctx);
    ds_storage_load_flag(ctx);
}

void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx)
{
    if (ctx != NULL) {
        ctx->params_ctx = params_ctx;
    }
}

void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx, iolink_ds_param_read_fn read,
                                 iolink_ds_param_write_fn write, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count)
{
    if (ctx == NULL) {
        return;
    }
    if (read != NULL) {
        ctx->read_param = read;
    }
    if (write != NULL) {
        ctx->write_param = write;
    }
    /* Only take the caller's user pointer together with an accessor; changing
       just the descriptor set must not clobber the device's bound user. */
    if ((read != NULL) || (write != NULL)) {
        ctx->param_user = user;
    }
    if ((params != NULL) && (count > 0U)) {
        ctx->params = params;
        ctx->param_count = count;
    }
}

void iolink_ds_set_param_desc(iolink_ds_ctx_t* ctx, const iolink_ds_param_desc_t* params,
                              size_t count)
{
    if ((ctx == NULL) || (params == NULL) || (count == 0U)) {
        return;
    }
    ctx->params = params;
    ctx->param_count = count;
}

/** @brief Read a parameter via the bound accessor, the params context, or the
 * legacy global set. */
static int ds_params_get(const iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                         uint8_t* buffer, size_t max_len)
{
    if ((ctx != NULL) && (ctx->read_param != NULL)) {
        return ctx->read_param(ctx->param_user, index, subindex, buffer, max_len);
    }
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_get(ctx->params_ctx, index, subindex, buffer, max_len);
    }
    return iolink_params_get(index, subindex, buffer, max_len);
}

/** @brief Write a parameter via the bound accessor, the params context, or the
 * legacy global set. */
static int ds_params_set(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                         const uint8_t* data, size_t len, bool persist)
{
    if ((ctx != NULL) && (ctx->write_param != NULL)) {
        return ctx->write_param(ctx->param_user, index, subindex, data, len, persist);
    }
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_set(ctx->params_ctx, index, subindex, data, len, persist);
    }
    return iolink_params_set(index, subindex, data, len, persist);
}

uint32_t iolink_ds_calc_checksum(const uint8_t* data, size_t len)
{
    /* Fletcher-32 over the serialized image (Table B.10 Parameter_Checksum is
       UIntegerT32). The Master treats the DS blob and its checksum opaquely (DS
       is device-specific), so a stable device-local checksum is all that is
       required for consistency comparison. */
    uint32_t sum1 = 0U;
    uint32_t sum2 = 0U;
    if (!iolink_buf_is_valid(data, len)) {
        return 0U;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (sum1 + (uint32_t) data[i]) % 65535U;
        sum2 = (sum2 + sum1) % 65535U;
    }
    return (sum2 << 16U) | sum1;
}

int iolink_ds_build_image(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    size_t pos = 0U;
    for (size_t i = 0U; i < ctx->param_count; ++i) {
        const uint16_t index = ctx->params[i].index;
        const uint8_t subindex = ctx->params[i].subindex;

        if ((pos + DS_RECORD_HEADER) > IOLINK_DS_IMAGE_MAX) {
            return -1; /* Image does not fit */
        }
        /* Read directly into the image tail; an optional per-parameter max_len
           bounds the value. */
        size_t cap = (size_t) IOLINK_DS_IMAGE_MAX - pos - DS_RECORD_HEADER;
        if ((ctx->params[i].max_len != 0U) && (cap > (size_t) ctx->params[i].max_len)) {
            cap = (size_t) ctx->params[i].max_len;
        }
        int n = ds_params_get(ctx, index, subindex, &ctx->image[pos + DS_RECORD_HEADER], cap);
        if (n < 0) {
            n = 0; /* Parameter not present -> empty record */
        }
        else if ((size_t) n > cap) {
            n = (int) cap;
        }

        ctx->image[pos] = (uint8_t) (index >> 8);
        ctx->image[pos + 1U] = (uint8_t) (index & 0xFFU);
        ctx->image[pos + 2U] = subindex;
        ctx->image[pos + 3U] = (uint8_t) n;
        pos += DS_RECORD_HEADER + (size_t) n;
    }

    ctx->image_len = pos;
    ctx->current_checksum = iolink_ds_calc_checksum(ctx->image, pos);
    return (int) pos;
}

/** @brief True when the index/subindex belongs to the configured DS set. */
static bool ds_param_known(const iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    if (ctx == NULL) {
        return false;
    }
    for (size_t i = 0U; i < ctx->param_count; ++i) {
        if ((ctx->params[i].index == index) && (ctx->params[i].subindex == subindex)) {
            return true;
        }
    }
    return false;
}

int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || !iolink_buf_is_valid(data, len) || (len > IOLINK_DS_IMAGE_MAX)) {
        return -1;
    }

    /* Validate the full record structure before applying anything, so a
       truncated or corrupt image never partially overwrites parameters. */
    size_t pos = 0U;
    while (pos < len) {
        if ((pos + DS_RECORD_HEADER) > len) {
            return -1; /* Truncated header */
        }
        size_t rlen = (size_t) data[pos + 3U];
        if ((pos + DS_RECORD_HEADER + rlen) > len) {
            return -1; /* Truncated data */
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* Apply each record. Unknown indices are skipped (a replacement device may
       not implement every backed-up parameter) rather than failing the restore.
       A *known* parameter that rejects the value makes the set inconsistent. */
    pos = 0U;
    while (pos < len) {
        uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
        uint8_t subindex = data[pos + 2U];
        size_t rlen = (size_t) data[pos + 3U];
        if (ds_param_known(ctx, index, subindex)) {
            if (ds_params_set(ctx, index, subindex, &data[pos + DS_RECORD_HEADER], rlen, true) !=
                0) {
                return -2; /* Known parameter rejected the value */
            }
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* Recovery: re-serialize from the device's ACTUAL parameters so the stored
       image and checksum reflect true device state rather than the raw download.
       If the image could not be applied faithfully (e.g. unknown indices were
       skipped), the resulting checksum differs from the Master's, which makes the
       next iolink_ds_check() re-request a download until the two sides agree. */
    if (iolink_ds_build_image(ctx) < 0) {
        return -1;
    }
    ds_storage_save_image(ctx);
    return 0;
}

bool iolink_ds_verify(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return false;
    }
    return iolink_ds_calc_checksum(ctx->image, ctx->image_len) == ctx->current_checksum;
}

const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len)
{
    if (ctx == NULL) {
        return NULL;
    }
    if (ctx->image_len == 0U) {
        if (iolink_ds_build_image(ctx) < 0) {
            return NULL;
        }
    }
    if (out_len != NULL) {
        *out_len = ctx->image_len;
    }
    return ctx->image;
}

void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum)
{
    if (ctx == NULL) {
        return;
    }

    ctx->master_checksum = master_checksum;

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return;
    }

    if (master_checksum == 0U) {
        /* Master has no data -> Upload request */
        ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    }
    else if (master_checksum != ctx->current_checksum) {
        /* Checksum mismatch -> Download request (Update device) */
        ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
}

void iolink_ds_process(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    switch (ctx->state) {
        case IOLINK_DS_STATE_UPLOAD_REQ:
            /* Master has no data -> snapshot the current parameters for upload. */
            (void) iolink_ds_build_image(ctx);
            ctx->state = IOLINK_DS_STATE_UPLOADING;
            break;

        case IOLINK_DS_STATE_UPLOADING:
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;

        case IOLINK_DS_STATE_DOWNLOAD_REQ:
            /* Master indicated a mismatch -> Device will receive parameters. */
            ctx->state = IOLINK_DS_STATE_DOWNLOADING;
            break;

        case IOLINK_DS_STATE_DOWNLOADING:
            /* Commit a staged image if one was written to DS_Data (0x0003). */
            if (ctx->image_len > 0U) {
                (void) iolink_ds_apply_image(ctx, ctx->image, ctx->image_len);
            }
            else {
                ctx->current_checksum = ctx->master_checksum;
            }
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;

        default:
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;
    }
}

int iolink_ds_start_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return -1; /* Busy */
    }

    (void) iolink_ds_build_image(ctx);
    ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    return 0;
}

int iolink_ds_start_download(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return -1; /* Busy */
    }

    ctx->image_len = 0U; /* Clear any prior staged image */
    ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    return 0;
}

int iolink_ds_abort(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    /* Abort any active DS operation */
    ctx->state = IOLINK_DS_STATE_IDLE;
    return 0;
}

int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks)
{
    if (ctx == NULL) {
        return -1;
    }

    /* Check Access Locks for Download Commands (Write to Device) */
    if ((cmd == IOLINK_CMD_PARAM_DOWNLOAD_START) || (cmd == IOLINK_CMD_PARAM_DOWNLOAD_END)) {
        if ((access_locks & IOLINK_LOCK_DS) != 0U) {
            /* DS Logic is locked */
            return -2; /* Signal Access Denied (user should map to ISDU error) */
        }
    }

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_START: /* 0x01 */
            /* Master wants to read parameters (Upload) */
            if (ctx->state != IOLINK_DS_STATE_IDLE) {
                return -1; /* Busy */
            }
            (void) iolink_ds_build_image(ctx);
            ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_UPLOAD_END: /* 0x02 */
            /* Finish upload */
            if (ctx->state == IOLINK_DS_STATE_UPLOADING) {
                ctx->state = IOLINK_DS_STATE_IDLE;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_START: /* 0x03 */
            /* Master wants to write parameters (Download): buffer the writes
               until ParamDownloadEnd commits them atomically (10.3.2). */
            if (ctx->state != IOLINK_DS_STATE_IDLE) {
                return -1; /* Busy */
            }
            ctx->image_len = 0U; /* Clear any prior staged image */
            ctx->state = IOLINK_DS_STATE_DOWNLOADING;
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_END: /* 0x04 */
            /* Commit the buffered parameter image atomically. */
            if (ctx->state == IOLINK_DS_STATE_DOWNLOADING) {
                int rc = 0;
                if (ctx->image_len > 0U) {
                    rc = iolink_ds_apply_image(ctx, ctx->image, ctx->image_len);
                }
                ctx->state = IOLINK_DS_STATE_IDLE;
                if (rc == -1) {
                    return -5; /* PAR_SETINVALID (0x8040): malformed image */
                }
                if (rc == -2) {
                    return -4; /* PAR_SETINCONSIST (0x8041): parameter rejected */
                }
            }
            break;

        case IOLINK_CMD_PARAM_BREAK: /* 0x06 / ParamBreak (Table B.9) */
            return iolink_ds_abort(ctx);

        default:
            return -3; /* Unknown command */
    }

    return 0;
}

int iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t command, uint16_t access_locks)
{
    uint8_t param_cmd;

    if (ctx == NULL) {
        return -1;
    }

    /* Map the Table B.10 DS_Command encoding to the legacy Param* encoding that
       iolink_ds_handle_command() understands (DS_Command 0x05 = DS_Break). */
    switch (command) {
        case 0x01U: /* DS_UploadStart */
            param_cmd = IOLINK_CMD_PARAM_UPLOAD_START;
            break;
        case 0x02U: /* DS_UploadEnd */
            param_cmd = IOLINK_CMD_PARAM_UPLOAD_END;
            break;
        case 0x03U: /* DS_DownloadStart */
            param_cmd = IOLINK_CMD_PARAM_DOWNLOAD_START;
            break;
        case 0x04U: /* DS_DownloadEnd */
            param_cmd = IOLINK_CMD_PARAM_DOWNLOAD_END;
            break;
        case 0x05U: /* DS_Break */
            param_cmd = IOLINK_CMD_PARAM_BREAK;
            break;
        default:
            return -3; /* Reserved */
    }

    int ret = iolink_ds_handle_command(ctx, param_cmd, access_locks);

    /* The DS_UPLOAD_FLAG is reset at the end of each upload/download sequence
       (Table B.10). */
    if ((ret == 0) && ((command == 0x02U) || (command == 0x04U))) {
        ctx->ds_upload_flag = false;
    }
    return ret;
}

bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx)
{
    return (ctx != NULL) && (ctx->state == IOLINK_DS_STATE_DOWNLOADING);
}

int iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len)
{
    if (!iolink_ds_downloading(ctx)) {
        return -1;
    }
    if ((len > 0U) && !iolink_buf_is_valid(data, len)) {
        return -1;
    }
    if ((ctx->image_len + DS_RECORD_HEADER + len) > IOLINK_DS_IMAGE_MAX) {
        return -1; /* Buffer full */
    }

    const size_t pos = ctx->image_len;
    ctx->image[pos] = (uint8_t) (index >> 8);
    ctx->image[pos + 1U] = (uint8_t) (index & 0xFFU);
    ctx->image[pos + 2U] = subindex;
    ctx->image[pos + 3U] = (uint8_t) len;
    if (len > 0U) {
        (void) memcpy(&ctx->image[pos + DS_RECORD_HEADER], data, len);
    }
    ctx->image_len = pos + DS_RECORD_HEADER + len;
    return 0;
}

uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx)
{
    uint8_t property = 0U;

    if (ctx == NULL) {
        return 0U;
    }
    switch (ctx->state) {
        case IOLINK_DS_STATE_UPLOAD_REQ:
        case IOLINK_DS_STATE_UPLOADING:
            property = 0x02U; /* 0b01 Upload */
            break;
        case IOLINK_DS_STATE_DOWNLOAD_REQ:
        case IOLINK_DS_STATE_DOWNLOADING:
            property = 0x04U; /* 0b10 Download */
            break;
        case IOLINK_DS_STATE_LOCKED:
            property = 0x06U; /* 0b11 Data Storage locked */
            break;
        default:
            property = 0x00U; /* 0b00 Inactive */
            break;
    }
    if (ctx->ds_upload_flag) {
        property |= 0x80U; /* DS_UPLOAD_FLAG */
    }
    return property;
}

uint32_t iolink_ds_size(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return (uint32_t) ctx->image_len;
}

uint32_t iolink_ds_checksum(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return ctx->current_checksum;
}

size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return ctx->param_count;
}

bool iolink_ds_param_at(const iolink_ds_ctx_t* ctx, size_t i, iolink_ds_param_desc_t* out)
{
    if ((ctx == NULL) || (out == NULL) || (i >= ctx->param_count)) {
        return false;
    }
    *out = ctx->params[i];
    return true;
}

void iolink_ds_request_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx != NULL) {
        ctx->ds_upload_flag = true;
        ds_storage_save_flag(ctx, 1U);
        (void) iolink_ds_build_image(ctx);
        ds_storage_save_image(ctx);
    }
}

void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx != NULL) {
        ctx->ds_upload_flag = false;
        ds_storage_save_flag(ctx, 0U);
    }
}

#endif /* IOLINK_FEATURE_DATA_STORAGE */
