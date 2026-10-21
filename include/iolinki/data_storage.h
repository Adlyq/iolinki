/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DATA_STORAGE_H
#define IOLINK_DATA_STORAGE_H

#include "iolinki/config.h"
#include "iolinki/storage.h"

#include "iolinki/protocol.h"
#include "iolinki/params.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file data_storage.h
 * @brief IO-Link Data Storage (DS) for parameter backup and restore
 */

/**
 * @defgroup iolinki_data_storage Data Storage (DS)
 * @brief Parameter image serialization, checksum compare, and upload/download services.
 * @{
 */

/**
 * @brief Maximum size of the serialized Data Storage parameter image.
 *
 * Must hold every DS-backed parameter record. Each record is laid out as
 * [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)].
 */
#ifndef IOLINK_DS_IMAGE_MAX
#define IOLINK_DS_IMAGE_MAX 256U
#endif

/**
 * @brief DS engine states
 */
/**
 * @brief Data Storage (DS) Engine States
 *
 * Manages the transition between idle, uploading, and downloading states.
 */
typedef enum
{
    IOLINK_DS_STATE_IDLE = 0U,         /**< No active DS operation */
    IOLINK_DS_STATE_UPLOAD_REQ = 1U,   /**< Master requested parameter upload */
    IOLINK_DS_STATE_UPLOADING = 2U,    /**< Parameter upload in progress */
    IOLINK_DS_STATE_DOWNLOAD_REQ = 3U, /**< Master requested parameter download */
    IOLINK_DS_STATE_DOWNLOADING = 4U,  /**< Parameter download in progress */
    IOLINK_DS_STATE_LOCKED = 5U        /**< DS operation disabled/locked */
} iolink_ds_state_t;

/**
 * @brief One Data-Storage-backed parameter (index + subindex).
 *
 * The DS parameter set is described by an array of these descriptors. The
 * default set is the three writable identification tags; the application may
 * replace or extend it (e.g. with vendor parameters 0x0040+ / 0x0100+).
 */
typedef struct
{
    uint16_t index;    /**< Parameter index. */
    uint8_t subindex;  /**< Parameter subindex. */
    uint8_t max_len;   /**< Upper bound for the value length (0 = unbounded). */
} iolink_ds_param_desc_t;

/**
 * @brief Read a single DS parameter into a buffer.
 *
 * @param user     Opaque parameter-access user pointer.
 * @param index    Parameter index.
 * @param subindex Parameter subindex.
 * @param out      Buffer receiving the parameter value.
 * @param cap      Capacity of @p out in bytes.
 * @return int Number of bytes read (>= 0), or negative when the parameter is
 *         unavailable/rejected.
 */
typedef int (*iolink_ds_param_read_fn)(void* user, uint16_t index, uint8_t subindex,
                                       uint8_t* out, size_t cap);

/**
 * @brief Write a single DS parameter.
 *
 * @param user     Opaque parameter-access user pointer.
 * @param index    Parameter index.
 * @param subindex Parameter subindex.
 * @param data     Parameter value bytes.
 * @param len      Length of @p data in bytes.
 * @param persist  Whether the write must be committed to non-volatile memory.
 * @return int 0 on success, negative when the parameter is rejected.
 */
typedef int (*iolink_ds_param_write_fn)(void* user, uint16_t index, uint8_t subindex,
                                        const uint8_t* data, size_t len, bool persist);

/**
 * @brief Data Storage Engine Context
 *
 * Tracks the state and checksums for the DS synchronization process.
 */
typedef struct
{
    iolink_ds_state_t state;                /**< Current DS state machine position */
    const iolink_storage_api_t* storage; /**< Bound storage implementation API */
    iolink_params_ctx_t* params_ctx;        /**< Legacy device-local writable parameter context */
    void* param_user;                       /**< Opaque user pointer for the param callbacks */
    iolink_ds_param_read_fn read_param;     /**< Unified parameter read accessor */
    iolink_ds_param_write_fn write_param;   /**< Unified parameter write accessor */
    const iolink_ds_param_desc_t* params;   /**< DS parameter descriptor array */
    size_t param_count;                     /**< Number of entries in @ref params */
    uint32_t current_checksum;              /**< Last calculated local parameter checksum */
    uint32_t master_checksum;               /**< Most recent checksum verified by Master */
    bool ds_upload_flag;                    /**< DS_UPLOAD_REQ pending (State_Property bit 7) */
    uint8_t image[IOLINK_DS_IMAGE_MAX];     /**< Serialized parameter image (DS_Data 0x0003) */
    size_t image_len;                       /**< Valid bytes in @ref image */
} iolink_ds_ctx_t;

/**
 * @brief Initialize the Data Storage engine
 *
 * @param ctx DS context to initialize
 * @param storage Optional storage implementation hooks (can be NULL for RAM-only)
 */
#if IOLINK_FEATURE_DATA_STORAGE
void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage);

/**
 * @brief Bind the writable parameter context used as the DS parameter source.
 *
 * @param ctx DS context
 * @param params_ctx Device-local parameter manager context to serialize/restore
 */
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);

/**
 * @brief Bind a unified parameter accessor and an explicit parameter descriptor set.
 *
 * Replaces the default tag-only set with a caller-provided descriptor array
 * and routes every DS read/write through @p read / @p write. This is how vendor
 * parameters (0x0040+ / 0x0100+) are added to the Data Storage set. When @p params
 * is NULL (or @p count is 0) the descriptor set is left unchanged.
 *
 * The device already binds a default accessor pair that serves standard
 * parameters through the params context and everything else through
 * vendor_service. An application that only needs a different DS index list
 * should call iolink_ds_set_param_desc() instead of replacing the accessors.
 *
 * @param ctx    DS context
 * @param read   Parameter read accessor (may be NULL to keep the current one)
 * @param write  Parameter write accessor (may be NULL to keep the current one)
 * @param user   Opaque pointer passed back to the accessors. Only updated when
 *               @p read or @p write is non-NULL, so passing NULL accessors keeps
 *               the existing user pointer.
 * @param params Descriptor array (may be NULL)
 * @param count  Number of descriptors in @p params
 */
void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx, iolink_ds_param_read_fn read,
                                 iolink_ds_param_write_fn write, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count);

/**
 * @brief Replace only the Data Storage parameter descriptor set.
 *
 * The current read/write accessors and their user pointer are kept. Applications
 * that already serve their parameters through the params context or
 * vendor_service only need to provide the index list here.
 *
 * @param ctx    DS context
 * @param params Descriptor array (must remain valid for the lifetime of the DS)
 * @param count  Number of descriptors in @p params
 */
void iolink_ds_set_param_desc(iolink_ds_ctx_t* ctx, const iolink_ds_param_desc_t* params,
                              size_t count);

/**
 * @brief Calculate a standard 32-bit checksum (Fletcher-32) for a parameter block
 *
 * Used during DS compare operations to detect parity between Master and Device.
 * Table B.10 requires the Parameter_Checksum to be UIntegerT32; 10.4.8 permits a
 * revision indication such as a CRC or checksum signature.
 *
 * @param data Pointer to the parameter data structure
 * @param len Length of the data in bytes
 * @return uint32_t Fletcher-32 checksum (0x00000000-0xFFFFFFFE)
 */
uint32_t iolink_ds_calc_checksum(const uint8_t* data, size_t len);

/**
 * @brief Process Data Storage engine logic
 *
 * Handles state transitions and background synchronization tasks.
 *
 * @param ctx DS context to process
 */
void iolink_ds_process(iolink_ds_ctx_t* ctx);

/**
 * @brief Trigger a DS consistency check with the Master
 *
 * typically triggered by the ISDU engine upon Master comparison requests.
 *
 * @param ctx DS context
 * @param master_checksum The 32-bit checksum provided by the IO-Link Master
 */
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);

/**
 * @brief Start parameter upload to Master (System Command 0x95)
 *
 * @param ctx DS context
 * @return int 0 on success, negative if DS not initialized
 */
int iolink_ds_start_upload(iolink_ds_ctx_t* ctx);

/**
 * @brief Start parameter download from Master (System Command 0x96)
 *
 * @param ctx DS context
 * @return int 0 on success, negative if DS not initialized
 */
int iolink_ds_start_download(iolink_ds_ctx_t* ctx);

/**
 * @brief Abort any in-progress DS upload/download and return to idle.
 *
 * @param ctx DS context
 * @return int 0 on success
 */
int iolink_ds_abort(iolink_ds_ctx_t* ctx);

/**
 * @brief Handle standard DS System Command
 *
 * @param ctx DS context
 * @param cmd System Command (0x05-0x08)
 * @param access_locks Current Access Lock state (Index 0x000C)
 * @return int 0: Success, -1: Busy, -2: Access Denied, -3: Unknown
 */
int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);

/**
 * @brief Handle a DataStorageIndex DS_Command (Table B.10 subindex 01).
 *
 * DS_Command uses a different encoding from the legacy SystemCommand Param*
 * set: 0x01 UploadStart, 0x02 UploadEnd, 0x03 DownloadStart, 0x04 DownloadEnd,
 * 0x05 DS_Break (0x06-0xFF reserved). It drives the same state machine and
 * clears the DS_UPLOAD_FLAG at the end of an upload/download sequence.
 *
 * @param ctx DS context
 * @param command DS_Command octet
 * @param access_locks Current Access Lock state (Index 0x000C)
 * @return int 0: Success, -1: Busy, -2: Access Denied, -3: Unknown/reserved
 */
int iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t command, uint16_t access_locks);

/**
 * @brief Whether a block parameter download is currently buffering writes.
 *
 * @param ctx DS context
 * @return true while in the download-buffering state.
 */
bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx);

/**
 * @brief Stage a parameter write into the download buffer (10.3.2).
 *
 * During a block download (after ParamDownloadStart and before
 * ParamDownloadEnd) parameter writes are buffered into the image rather than
 * applied immediately; ParamDownloadEnd commits them atomically.
 *
 * @param ctx      DS context
 * @param index    Parameter index
 * @param subindex Parameter subindex
 * @param data     Parameter value bytes
 * @param len      Length of @p data in bytes
 * @return int 0 on success, negative when not downloading or the buffer is full.
 */
int iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len);

/**
 * @brief Serialize the device's DS-backed parameters into the image buffer.
 *
 * Reads every parameter in the DS set from the parameter manager and packs it
 * into @ref iolink_ds_ctx_t::image. Updates @ref iolink_ds_ctx_t::current_checksum.
 *
 * @param ctx DS context
 * @return int Number of image bytes built (>= 0), or negative on error.
 */
int iolink_ds_build_image(iolink_ds_ctx_t* ctx);

/**
 * @brief Apply a parameter image received from the Master (restore).
 *
 * Parses the serialized records and writes each parameter back through the
 * parameter manager (persisting to NVM). Updates @ref iolink_ds_ctx_t::current_checksum.
 *
 * @param ctx DS context
 * @param data Serialized image bytes
 * @param len  Number of image bytes
 * @return int 0 on success, negative if the image is malformed or rejected.
 */
int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);

/**
 * @brief Get the current serialized parameter image (building it on demand).
 *
 * @param ctx DS context
 * @param out_len [out] Receives the image length in bytes
 * @return const uint8_t* Pointer to the image buffer, or NULL on error.
 */
const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len);

/**
 * @brief Verify the stored image is consistent with its recorded checksum.
 *
 * @param ctx DS context
 * @return true if checksum(image) == current_checksum, false otherwise.
 */
bool iolink_ds_verify(const iolink_ds_ctx_t* ctx);

/**
 * @brief Read the State_Property octet (Table B.10 subindex 02).
 *
 * Bits 1-2 encode the DS state (Inactive/Upload/Download/locked); bit 7 is the
 * DS_UPLOAD_FLAG (a Data Storage upload request is pending).
 *
 * @param ctx DS context
 * @return uint8_t State_Property value.
 */
uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx);

/**
 * @brief Read the Data_Storage_Size value (Table B.10 subindex 03).
 *
 * @param ctx DS context
 * @return uint32_t Number of octets required to store the parameter set.
 */
uint32_t iolink_ds_size(const iolink_ds_ctx_t* ctx);

/**
 * @brief Read the Parameter_Checksum value (Table B.10 subindex 04).
 *
 * @param ctx DS context
 * @return uint32_t Current parameter-set checksum (Fletcher-32).
 */
uint32_t iolink_ds_checksum(const iolink_ds_ctx_t* ctx);

/**
 * @brief Number of entries in the DS parameter descriptor set.
 *
 * @param ctx DS context
 * @return size_t Descriptor count.
 */
size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx);

/**
 * @brief Copy descriptor @p i into @p out.
 *
 * @param ctx DS context
 * @param i   Descriptor index
 * @param out [out] Receives the descriptor (index + subindex)
 * @return true when @p i is in range, false otherwise.
 */
bool iolink_ds_param_at(const iolink_ds_ctx_t* ctx, size_t i, iolink_ds_param_desc_t* out);

/**
 * @brief Raise a Data Storage upload request (sets DS_UPLOAD_FLAG, 10.4.2).
 *
 * Called when the parameter set changed locally (teach-in, Application Reset,
 * ParamDownloadStore). The master observes bit 7 of State_Property and/or the
 * DS_UPLOAD_REQ event; the flag is cleared at the end of the upload/download.
 *
 * @param ctx DS context
 */
void iolink_ds_request_upload(iolink_ds_ctx_t* ctx);

/**
 * @brief Clear the pending Data Storage upload request (DS_UPLOAD_FLAG = false).
 *
 * @param ctx DS context
 */
void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx);
#endif /* IOLINK_FEATURE_DATA_STORAGE */

/** @} */ /* end of iolinki_data_storage */

#endif  // IOLINK_DATA_STORAGE_H
