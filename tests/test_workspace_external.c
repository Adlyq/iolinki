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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/isdu.h"
#include "iolinki/workspace.h"
#include "dll_access.h"

/* 默认构建需要真实的临界区实现（test_helpers.c 也提供同一对空实现）。 */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static iolink_workspace_t g_workspace;

static uint64_t test_time_us(iolink_app_t* self)
{
    (void) self;
    return 1000U;
}

static const iolink_device_app_t g_app = {
    .self = NULL,
    .time_us = test_time_us,
};

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

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static int test_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    (void) data;
    *status = 0U;
    return 0;
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
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
#endif

static uint8_t test_int_active(void* user)
{
    (void) user;
    return 0U;
}

static void make_config(iolink_device_config_t* config)
{
    (void) memset(config, 0, sizeof(*config));
    (void) memset(&g_workspace, 0, sizeof(g_workspace));

    config->phy.init = test_phy_init;
    config->phy.set_mode = test_set_mode;
    config->phy.set_baudrate = test_set_baudrate;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /* 该 fixture 只验证工作区绑定，不驱动收发；FRAME 构建下不提供帧钩子。 */
    config->phy.rx_frame = NULL;
#else
    config->phy.rx_octet = test_rx_octet;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    config->phy.tx_frame = NULL;
#else
    config->phy.tx_octet = test_tx_octet;
    config->phy.tx_done = test_tx_done;
#endif
    config->phy.int_active = test_int_active;
    config->app = &g_app;
    config->stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    config->stack.od_width = 1U;
}

static void test_init_requires_workspace(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    make_config(&config);

    /* 外置形态下缺少工作区必须被拒绝，而不是退化为空指针写入。 */
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_WORKSPACE);
}

static void test_binds_external_workspace(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    make_config(&config);

    /* 通过 config.workspace 提供后初始化成功，并回读同一个指针。 */
    config.workspace = &g_workspace;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_OK);
    assert_ptr_equal(iolink_device_get_workspace(&ctx), &g_workspace);

    iolink_isdu_ctx_t* isdu = test_dll_isdu(&ctx);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&ctx);

    /* 绑定后所有缓冲都指向工作区。 */
    assert_ptr_equal(isdu->buffer, g_workspace.isdu.payload);
    assert_ptr_equal(isdu->response_buf, g_workspace.isdu.response);
    assert_ptr_equal(isdu->req_buf, g_workspace.isdu.request);
    assert_ptr_equal(isdu->resp_frame, g_workspace.isdu.frame);
    assert_ptr_equal(isdu->resp_store[0], g_workspace.isdu.frame);
    assert_ptr_equal(isdu->resp_store[1], g_workspace.isdu.frame);
    assert_ptr_equal(ds->image, g_workspace.ds_stage);
    assert_int_equal(ds->image_size, sizeof(g_workspace.ds_stage));
}

static void test_unbind_clears_pointers(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    make_config(&config);
    config.workspace = &g_workspace;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_OK);

    iolink_isdu_ctx_t* isdu = test_dll_isdu(&ctx);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&ctx);

    /* 解绑必须清零全部指针与容量：不得保留旧工作区地址。 */
    iolink_isdu_bind_workspace(isdu, NULL);
    iolink_ds_bind_workspace(ds, NULL);

    assert_null(iolink_device_get_workspace(&ctx));
    assert_null(isdu->buffer);
    assert_int_equal(isdu->buffer_size, 0U);
    assert_null(isdu->response_buf);
    assert_int_equal(isdu->response_size, 0U);
    assert_null(isdu->req_buf);
    assert_int_equal(isdu->req_size, 0U);
    assert_null(isdu->resp_frame);
    assert_null(isdu->resp_store[0]);
    assert_null(isdu->resp_store[1]);
    assert_null(isdu->resp_buf);
    assert_null(ds->image);
    assert_int_equal(ds->image_size, 0U);
    assert_int_equal(ds->image_len, 0U);

    /* 解绑后访问返回合理结果而不是解引用旧指针。 */
    const uint8_t service = 0xB5U;
    iolink_isdu_od_write(isdu, IOLINK_FLOWCTRL_START, &service, 1);
    iolink_isdu_process(isdu);
    uint8_t snapshot[8] = {0U};
    assert_int_equal(iolink_isdu_snapshot_response(isdu, NULL, snapshot, sizeof(snapshot)), 0U);

    assert_null(iolink_ds_get_image(ds, NULL));
    const int built = iolink_ds_build_image(ds);
    assert_true(built >= 0);
    assert_true(built <= (int) IOLINK_DS_IMAGE_MAX);
    assert_null(iolink_ds_get_image(ds, NULL));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_requires_workspace),
        cmocka_unit_test(test_binds_external_workspace),
        cmocka_unit_test(test_unbind_clears_pointers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
