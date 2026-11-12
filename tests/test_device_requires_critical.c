/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device_requires_critical.c
 * @brief 设备初始化会拒绝库中弱符号的空操作临界区；
 *        本可执行文件有意不链接强覆盖实现。
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"

static int test_storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return -1;
}

static int test_storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

static const iolink_storage_api_t g_test_storage = {.read = test_storage_read,
                                                    .write = test_storage_write};

static uint64_t test_time_us(void* user)
{
    (void) user;
    return 1000000ULL;
}

int main(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    (void) memset(&config, 0, sizeof(config));
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.storage = &g_test_storage;
    config.time_us = test_time_us;

    /* 不含异步上下文的配置文件（SINGLE）从不引用
     * 临界区钩子，因此那里缺少 port 不再是致命错误。 */
#if IOLINK_NEED_CRITICAL
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_CRITICAL);
#else
    assert(iolink_device_init(&ctx, &config) != IOLINK_DEVICE_INIT_ERR_CRITICAL);
#endif
    return 0;
}