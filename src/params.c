/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file params.c
 * @brief 参数管理器：支持 NVM 持久化的可写设备标签。
 * @ingroup iolinki_params
 *
 * 管理应用/功能/位置标签参数，通过平台 NVM 接口加载并
 * 持久化这些参数，并将应用标签同步到设备信息
 * 上下文。同时为无上下文的调用方提供
 * 旧的单例封装。
 */

#include "iolinki/config.h"
#include "iolinki/params.h"
#include "iolinki/platform.h"
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

/** @brief 将有效标签序列化到 NVM 记录并写入存储。 */
static int params_ctx_write_nvm(const iolink_params_ctx_t* ctx)
{
    iolink_params_nvm_t nvm;

    if ((ctx == NULL) || (ctx->storage == NULL) || (ctx->storage->write == NULL)) {
        return 0; /* 无后端：纯 RAM 设备，接受该写入。 */
    }
    (void) memset(&nvm, 0, sizeof(nvm));
    nvm.magic = IOLINK_STORAGE_MAGIC;
    nvm.version = IOLINK_STORAGE_VERSION;
    if (ctx->application_tag_valid) {
        (void) memcpy(nvm.application_tag, ctx->application_tag, sizeof(nvm.application_tag));
    }
    if (ctx->function_tag_valid) {
        (void) memcpy(nvm.function_tag, ctx->function_tag, sizeof(nvm.function_tag));
    }
    if (ctx->location_tag_valid) {
        (void) memcpy(nvm.location_tag, ctx->location_tag, sizeof(nvm.location_tag));
    }
    nvm.checksum =
        iolink_storage_checksum((const uint8_t*) &nvm, offsetof(iolink_params_nvm_t, checksum));
    return ctx->storage->write(ctx->storage->user, IOLINK_STORAGE_PARAMS_BASE,
                               (const uint8_t*) &nvm, sizeof(nvm));
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

static void params_ctx_load(iolink_params_ctx_t* ctx)
{
    iolink_params_nvm_t nvm;

    if ((ctx == NULL) || (ctx->storage == NULL) || (ctx->storage->read == NULL)) {
        return;
    }
    if ((ctx->storage->read(ctx->storage->user, IOLINK_STORAGE_PARAMS_BASE, (uint8_t*) &nvm,
                            sizeof(nvm)) != 0) ||
        (nvm.magic != IOLINK_STORAGE_MAGIC) || (nvm.version != IOLINK_STORAGE_VERSION) ||
        (nvm.checksum !=
         iolink_storage_checksum((const uint8_t*) &nvm, offsetof(iolink_params_nvm_t, checksum)))) {
        return; /* 未初始化、布局不同或已损坏。 */
    }
    copy_tag(ctx->application_tag, &ctx->application_tag_valid,
             (const uint8_t*) nvm.application_tag, bounded_tag_len(nvm.application_tag));
    copy_tag(ctx->function_tag, &ctx->function_tag_valid, (const uint8_t*) nvm.function_tag,
             bounded_tag_len(nvm.function_tag));
    copy_tag(ctx->location_tag, &ctx->location_tag_valid, (const uint8_t*) nvm.location_tag,
             bounded_tag_len(nvm.location_tag));
    if (ctx->device_info != NULL) {
        (void) iolink_device_info_ctx_set_application_tag(ctx->device_info, ctx->application_tag,
                                                          (uint8_t) strlen(ctx->application_tag));
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

void iolink_params_bind_storage(iolink_params_ctx_t* ctx, const iolink_storage_api_t* storage)
{
    if (ctx == NULL) {
        return;
    }
    ctx->storage = storage;
    params_ctx_load(ctx);
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
                                        const uint8_t* data, size_t len, iolink_persist_t persist)
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
    /* 被拒绝的持久化写入不得在 RAM/回读中表现为已接受。 */
    if ((persist != IOLINK_PERSIST_NONE) && (params_ctx_write_nvm(&candidate) != 0)) {
        return iolink_result_error(IOLINK_RESULT_IO);
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

int iolink_params_ctx_get(const iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          uint8_t* buffer, size_t max_len)
{
    const iolink_result_t result = iolink_params_ctx_read(ctx, index, subindex, buffer, max_len);

    return iolink_result_succeeded(result) ? (int) result.len : -1;
}

int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len, iolink_persist_t persist)
{
    return iolink_result_succeeded(
               iolink_params_ctx_write(ctx, index, subindex, data, len, persist))
               ? 0
               : -1;
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
    /* 旧的 void API：即使存储不支持或失败而无法
     * 保留复位结果，也要复位 RAM。集成方不能从该调用推断持久化。 */
    (void) params_ctx_write_nvm(ctx);
}
