/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_workspace_external.c
 * @brief 外部工作区契约（`IOLINK_WORKSPACE_EMBEDDED=0`）。
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

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/isdu.h"
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
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config.stack.od_width = 1U;

    /* 外置形态下缺少工作区必须被拒绝，而不是退化为空指针写入。 */
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_ERR_WORKSPACE);

    /* 通过 config.workspace 提供后初始化成功，并回读同一个指针。 */
    config.workspace = &workspace;
    assert(iolink_device_init(&ctx, &config) == IOLINK_DEVICE_INIT_OK);
    assert(iolink_device_get_workspace(&ctx) == &workspace);

    iolink_isdu_ctx_t* isdu = &ctx.dll.main.shared.isdu;
    iolink_ds_ctx_t* ds = &ctx.dll.main.ds;

    /* 绑定后所有缓冲都指向工作区。 */
    assert(isdu->buffer == workspace.isdu.payload);
    assert(isdu->response_buf == workspace.isdu.response);
    assert(isdu->req_buf == workspace.isdu.request);
    assert(isdu->resp_frame == workspace.isdu.frame);
    assert(isdu->resp_store[0] == workspace.isdu.frame);
    assert(isdu->resp_store[1] == workspace.isdu.frame);
    assert(ds->image == workspace.ds_stage);
    assert(ds->image_size == sizeof(workspace.ds_stage));

    /* 解绑必须清零全部指针与容量：不得保留旧工作区地址。 */
    iolink_isdu_bind_workspace(isdu, NULL);
    iolink_ds_bind_workspace(ds, NULL);

    assert(iolink_device_get_workspace(&ctx) == NULL);
    assert(isdu->buffer == NULL);
    assert(isdu->buffer_size == 0U);
    assert(isdu->response_buf == NULL);
    assert(isdu->response_size == 0U);
    assert(isdu->req_buf == NULL);
    assert(isdu->req_size == 0U);
    assert(isdu->resp_frame == NULL);
    assert(isdu->resp_store[0] == NULL);
    assert(isdu->resp_store[1] == NULL);
    assert(isdu->resp_buf == NULL);
    assert(ds->image == NULL);
    assert(ds->image_size == 0U);
    assert(ds->image_len == 0U);

    /* 解绑后访问返回合理结果而不是解引用旧指针。 */
    const uint8_t service = 0xB5U;
    iolink_isdu_od_write(isdu, IOLINK_FLOWCTRL_START, &service, 1);
    iolink_isdu_process(isdu);
    uint8_t snapshot[8] = {0U};
    assert(iolink_isdu_snapshot_response(isdu, NULL, snapshot, sizeof(snapshot)) == 0U);

    assert(iolink_ds_get_image(ds, NULL) == NULL);
    const int built = iolink_ds_build_image(ds);
    assert(built >= 0);
    assert(built <= (int) IOLINK_DS_IMAGE_MAX);
    assert(iolink_ds_get_image(ds, NULL) == NULL);
    return 0;
}
