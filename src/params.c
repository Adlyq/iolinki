/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file params.c
 * @brief 标签值存储：应用/功能/位置标签的 RAM 读写与出厂复位。
 * @ingroup iolinki_params
 *
 * 持久化不属于本模块：标签表项带 `IOLINK_OD_STORAGE_PERSIST`，
 * 写成功后由 ISDU/DS 层保存统一的参数镜像。
 */

#include "iolinki/config.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include <string.h>

/** @brief 将最多 32 字节复制到以 NUL 结尾的标签缓冲区并标记为有效。 */
static void copy_tag(char* dst, bool* valid, const uint8_t* data, size_t len)
{
    size_t copy_len = (len > 32U) ? 32U : len;

    if (copy_len > 0U) {
        (void) memcpy(dst, data, copy_len);
    }
    dst[copy_len] = '\0';
    *valid = true;
}

/** @brief 返回标签字符串长度，并限制在 32 字节上限内。 */
static size_t bounded_tag_len(const char* tag)
{
    size_t len = strlen(tag);
    if (len > 32U) {
        len = 32U;
    }
    return len;
}

/** @brief 将有界标签字符串复制到输出缓冲区；返回写入的字节数。 */
static int read_tag(const char* tag, uint8_t* buffer, size_t max_len)
{
    size_t len = bounded_tag_len(tag);
    if (len > max_len) {
        len = max_len;
    }
    if (len > 0U) {
        (void) memcpy(buffer, tag, len);
    }
    return (int) len;
}

static void params_ctx_apply_defaults(iolink_params_ctx_t* ctx)
{
    if ((ctx != NULL) && (ctx->device_info != NULL)) {
        const iolink_device_info_t* info = iolink_device_info_ctx_get(ctx->device_info);
        if ((info != NULL) && (info->application_tag != NULL)) {
            copy_tag(ctx->application_tag, &ctx->application_tag_valid,
                     (const uint8_t*) info->application_tag,
                     bounded_tag_len(info->application_tag));
        }
    }
}

void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info)
{
    if (ctx == NULL) {
        return;
    }
    (void) memset(ctx, 0, sizeof(*ctx));
    ctx->device_info = device_info;
    params_ctx_apply_defaults(ctx);
}

iolink_result_t iolink_params_ctx_read(const iolink_params_ctx_t* ctx, uint16_t index,
                                       uint8_t subindex, uint8_t* buffer, size_t max_len)
{
    if ((ctx == NULL) || (buffer == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        const iolink_device_info_t* info = iolink_device_info_ctx_get(ctx->device_info);
        if ((info != NULL) && (info->application_tag != NULL)) {
            return iolink_result_ok((uint16_t) read_tag(info->application_tag, buffer, max_len));
        }
        return iolink_result_ok(0U);
    }
    if ((index == IOLINK_IDX_FUNCTION_TAG) && (subindex == 0U)) {
        if (!ctx->function_tag_valid) {
            return iolink_result_ok(0U);
        }
        return iolink_result_ok((uint16_t) read_tag(ctx->function_tag, buffer, max_len));
    }
    if ((index == IOLINK_IDX_LOCATION_TAG) && (subindex == 0U)) {
        if (!ctx->location_tag_valid) {
            return iolink_result_ok(0U);
        }
        return iolink_result_ok((uint16_t) read_tag(ctx->location_tag, buffer, max_len));
    }
    return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
}

iolink_result_t iolink_params_ctx_write(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                        const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || !iolink_buf_is_valid(data, len)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    iolink_params_ctx_t candidate = *ctx;
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        if (len >= sizeof(candidate.application_tag)) {
            return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
        }
        copy_tag(candidate.application_tag, &candidate.application_tag_valid, data, len);
    }
    else if ((index == IOLINK_IDX_FUNCTION_TAG) && (subindex == 0U)) {
        copy_tag(candidate.function_tag, &candidate.function_tag_valid, data, len);
    }
    else if ((index == IOLINK_IDX_LOCATION_TAG) && (subindex == 0U)) {
        copy_tag(candidate.location_tag, &candidate.location_tag_valid, data, len);
    }
    else {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if ((index == IOLINK_IDX_APPLICATION_TAG) && candidate.device_info != NULL) {
        if (iolink_device_info_ctx_set_application_tag(
                candidate.device_info, candidate.application_tag,
                (uint8_t) strlen(candidate.application_tag)) != 0) {
            return iolink_result_error(IOLINK_RESULT_IO);
        }
    }
    *ctx = candidate;
    return iolink_result_ok(0U);
}

void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->application_tag[0] = '\0';
    ctx->function_tag[0] = '\0';
    ctx->location_tag[0] = '\0';
    ctx->application_tag_valid = true;
    ctx->function_tag_valid = true;
    ctx->location_tag_valid = true;

    if (ctx->device_info != NULL) {
        (void) iolink_device_info_ctx_set_application_tag(ctx->device_info, "", 0U);
    }
}
