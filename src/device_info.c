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
 * 访问锁），并为调用方提供旧的单例封装，适用于
 * 不自行管理上下文的调用方。
 */

#include "iolinki/device_info.h"
#include <stdbool.h>
#include <string.h>

static const iolink_device_info_t k_default_info = {
    .vendor_name = "iolinki",
    .vendor_text = "Open-Source IO-Link Stack",
    .product_name = "Generic IO-Link Device",
    .product_id = "IOLINK-DEV-001",
    .product_text = "Reference Implementation",
    .serial_number = "0000000001",
    .hardware_revision = "1.0",
    .firmware_revision = "0.1.0",
    .application_tag = "DefaultTag",

    .vendor_id = 0xFFFFU, /* 未分配的供应商标识 */
    .device_id = 0x00000001U,
    .function_id = 0x0000U,
    .profile_characteristic = 0x0000U,

    .min_cycle_time = 10U, /* 1.0ms (10 * 100μs) */
    .revision_id = 0x0111U,
    .device_status = 0x00U, /* 无错误 */
    .detailed_device_status = 0x0000U,

    .access_locks = 0x0000U /* 默认全部未锁定 */
};

void iolink_device_info_ctx_init(iolink_device_info_ctx_t* ctx,
                                 const iolink_device_info_t* configured)
{
    const iolink_device_info_t* source = configured;

    if (ctx == NULL) {
        return;
    }

    if (source == NULL) {
        source = &k_default_info;
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
}

const iolink_device_info_t* iolink_device_info_ctx_get(const iolink_device_info_ctx_t* ctx)
{
    if (ctx == NULL) {
        return NULL;
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
