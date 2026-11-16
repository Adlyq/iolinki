/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file device_info.c
 * @brief 设备标识信息的存储与访问。
 * @ingroup iolinki_device_info
 *
 * 在上下文中保存设备标识参数（供应商标识/产品字符串、ID、
 * 访问锁）。身份按上下文访问，不再提供进程级单例封装。
 */

#include "iolinki/device_info.h"
#include <stdbool.h>
#include <string.h>

int iolink_device_info_ctx_init(iolink_device_info_ctx_t* ctx,
                                const iolink_device_info_t* configured)
{
    const iolink_device_info_t* source = configured;

    if (ctx == NULL) {
        return -1;
    }

    /* 没有默认身份：缺配置就清零，不伪造厂商/产品数据。 */
    (void) memset(ctx, 0, sizeof(*ctx));
    if (source == NULL) {
        return -1;
    }

    ctx->configured = configured;
    ctx->defaults = *source;
    ctx->access_locks = source->access_locks;

    if (source->application_tag != NULL) {
        size_t len = strlen(source->application_tag);
        if (len > 32U) {
            len = 32U;
        }
        (void) memcpy(ctx->application_tag, source->application_tag, len);
        ctx->application_tag[len] = '\0';
    }
    else {
        ctx->application_tag[0] = '\0';
    }

    ctx->defaults.application_tag = ctx->application_tag;
    ctx->defaults.access_locks = ctx->access_locks;
    return 0;
}

const iolink_device_info_t* IOLINK_NULLABLE
iolink_device_info_ctx_get(const iolink_device_info_ctx_t* IOLINK_NULLABLE ctx)
{
    if ((ctx == NULL) || (ctx->configured == NULL)) {
        return NULL; /* 未绑定身份：没有可返回的标识 */
    }
    return &ctx->defaults;
}

int iolink_device_info_ctx_set_application_tag(iolink_device_info_ctx_t* ctx, const char* tag,
                                               uint8_t len)
{
    if ((ctx == NULL) || (tag == NULL)) {
        return -1;
    }
    if (len >= sizeof(ctx->application_tag)) {
        return -1;
    }

    (void) memcpy(ctx->application_tag, tag, len);
    ctx->application_tag[len] = '\0';
    ctx->defaults.application_tag = ctx->application_tag;
    return 0;
}

uint16_t iolink_device_info_ctx_get_access_locks(const iolink_device_info_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return ctx->access_locks;
}

void iolink_device_info_ctx_set_access_locks(iolink_device_info_ctx_t* ctx, uint16_t locks)
{
    if (ctx == NULL) {
        return;
    }
    ctx->access_locks = locks;
    ctx->defaults.access_locks = locks;
}
