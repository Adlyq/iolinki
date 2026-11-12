/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_workspace_external.c
 * @brief P8 外部工作区契约（`IOLINK_WORKSPACE_EMBEDDED=0`）。
 *
 * 该二进制链接一份以 `IOLINK_WORKSPACE_EMBEDDED=0` 编译的协议栈：
 *  - 未提供工作区时 init 必须返回 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`；
 *  - 显式提供工作区后 init 成功，且 `iolink_device_get_workspace()`
 *    返回同一个指针。
 *
 * 默认构建（内嵌 scratch）由 `tests/test_workspace.c` 覆盖；这里只验证
 * 外置形态，二者不会互相替代。
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/storage.h"
#include "iolinki/workspace.h"

/* 默认构建需要真实的临界区实现（test_helpers.c 也提供同一对空实现）。 */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t test_time_us(void* user)
{
    (void) user;
    return 1000U;
}

static int test_phy_init(void* user)
{
    (void) user;
    return 0;
}

static void test_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void test_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static int test_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    (void) data;
    *status = 0U;
    return 0;
}

static int test_tx_octet(void* user, uint8_t data)
{
    (void) user;
    (void) data;
    return 0;
}

static void test_tx_done(void* user)
{
    (void) user;
}

static uint8_t test_int_active(void* user)
{
    (void) user;
    return 0U;
}

static int storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) memset(buf, 0, len);
    return 0;
}

static int storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

static const iolink_storage_api_t g_storage = {
    .user = NULL, .read = storage_read, .write = storage_write};

int main(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    static iolink_workspace_t workspace;

    (void) memset(&config, 0, sizeof(config));
    (void) memset(&workspace, 0, sizeof(workspace));

    config.phy.init = test_phy_init;
    config.phy.set_mode = test_set_mode;
    config.phy.set_baudrate = test_set_baudrate;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    config.phy.rx_frame = NULL; /* 本 fixture 只构建 BYTE/BYTE。 */
#else
    config.phy.rx_octet = test_rx_octet;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    config.phy.tx_frame = NULL; /* 本 fixture 只构建 BYTE/BYTE。 */
#else
    config.phy.tx_octet = test_tx_octet;
    config.phy.tx_done = test_tx_done;
#endif
    config.phy.int_active = test_int_active;
    config.time_us = test_time_us;
    config.storage = &g_storage;
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.stack.od_width = 1U;

    /* 外置形态下缺少工作区必须被拒绝，而不是退化为空指针写入。 */
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_WORKSPACE);

    /* 显式提供工作区后初始化成功，并回读同一个指针。 */
    assert(iolink_device_init_with_workspace(&ctx, &config, &workspace) == IOLINK_DEVICE_INIT_OK);
    assert(iolink_device_get_workspace(&ctx) == &workspace);
    return 0;
}
