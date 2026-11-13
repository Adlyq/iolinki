/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_workspace.c
 * @brief 工作区绑定与 DS 流式序列化的契约。
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/isdu.h"
#include "iolinki/workspace.h"
#include "test_helpers.h"

static iolink_test_env_t g_env;

static void fill_env(void)
{
    test_env_init(&g_env, NULL);
    test_env_reset_params(&g_env);
    (void) iolink_params_ctx_write(test_env_params(&g_env), IOLINK_IDX_APPLICATION_TAG, 0U,
                                   (const uint8_t*) "WS", 2U);
    (void) iolink_params_ctx_write(test_env_params(&g_env), IOLINK_IDX_FUNCTION_TAG, 0U,
                                   (const uint8_t*) "FLOW", 4U);
}

static void device_config_defaults(iolink_test_device_t* dev)
{
    (void) memset(dev, 0, sizeof(*dev));
    dev->cfg.phy = g_phy_mock;
    dev->cfg.time_us = iolink_test_time_us;
#if IOLINK_FEATURE_DATA_STORAGE
    dev->cfg.storage = &g_ds_storage_mock;
#endif
    dev->cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
}

static void test_device_binds_external_workspace(void** state)
{
    (void) state;
    iolink_phy_mock_reset();
    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    static iolink_workspace_t ws;
    iolink_test_device_t dev;
    device_config_defaults(&dev);
    dev.cfg.workspace = &ws;

    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), IOLINK_DEVICE_INIT_OK);
    assert_ptr_equal(iolink_device_get_workspace(&dev.ctx), &ws);

    const iolink_isdu_ctx_t* isdu = &dev.ctx.dll.main.shared.isdu;
    assert_ptr_equal(isdu->buffer, ws.isdu.payload);
    assert_ptr_equal(isdu->response_buf, ws.isdu.response);
    assert_ptr_equal(isdu->req_buf, ws.isdu.request);
    assert_ptr_equal(isdu->resp_frame, ws.isdu.frame);

#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_ptr_equal(ds->image, ws.ds_stage);
    assert_int_equal(ds->image_size, sizeof(ws.ds_stage));
#endif
}

static void test_device_falls_back_to_embedded_scratch(void** state)
{
    (void) state;
    iolink_phy_mock_reset();
    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    iolink_test_device_t dev;
    device_config_defaults(&dev);
    dev.cfg.workspace = NULL;

#if IOLINK_WORKSPACE_EMBEDDED
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), IOLINK_DEVICE_INIT_OK);
    assert_null(iolink_device_get_workspace(&dev.ctx));
    const iolink_isdu_ctx_t* isdu = &dev.ctx.dll.main.shared.isdu;
    assert_ptr_equal(isdu->buffer, isdu->scratch.payload);
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_ptr_equal(ds->image, ds->scratch);
#endif
#else
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), IOLINK_DEVICE_INIT_ERR_WORKSPACE);
#endif
}

static void test_ds_serialize_matches_stored_image(void** state)
{
    (void) state;
    fill_env();
    iolink_ds_mock_reset();

    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_env);

    uint8_t serialized[IOLINK_DS_IMAGE_MAX];
    const int n = iolink_ds_serialize(&ds, serialized, sizeof(serialized));
    assert_true(n > 0);
    assert_int_equal((size_t) n, ds.image_len);
    assert_int_equal(ds.current_checksum, iolink_ds_calc_checksum(serialized, (size_t) n));

    /* 同一份参数经 build_image 写入暂存缓冲，内容必须逐字节一致。
     * 外置工作区形态没有嵌入暂存缓冲，由 test_workspace_external 覆盖。 */
#if IOLINK_WORKSPACE_EMBEDDED
    assert_int_equal(iolink_ds_build_image(&ds), n);
    assert_memory_equal(ds.image, serialized, (size_t) n);
#endif

    /* 容量不足必须被拒绝，而不是截断。 */
    assert_int_equal(iolink_ds_serialize(&ds, serialized, 3U), -1);

    /* 差一字节与恰好放下：拒绝时不得污染镜像状态。 */
    const size_t before_len = ds.image_len;
    const uint32_t before_checksum = ds.current_checksum;
    assert_int_equal(iolink_ds_serialize(&ds, serialized, (size_t) n), n);
    assert_int_equal(iolink_ds_serialize(&ds, serialized, (size_t) n - 1U), -1);
    assert_int_equal(ds.image_len, before_len);
    assert_int_equal(ds.current_checksum, before_checksum);
}

static void test_ds_refresh_without_image_buffer(void** state)
{
    (void) state;
    fill_env();
    iolink_ds_mock_reset();

    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_env);

    uint8_t serialized[IOLINK_DS_IMAGE_MAX];
    const int expected = iolink_ds_serialize(&ds, serialized, sizeof(serialized));
    assert_true(expected > 0);

    /* 模拟 `IOLINK_WORKSPACE_EMBEDDED=0` 且未绑定工作区：只流式计算。 */
    ds.image = NULL;
    ds.image_size = 0U;
    ds.image_valid = false;
    iolink_ds_refresh(&ds);

    assert_int_equal((int) ds.image_len, expected);
    assert_int_equal(ds.current_checksum, iolink_ds_calc_checksum(serialized, (size_t) expected));
    size_t len = 123U;
    assert_null(iolink_ds_get_image(&ds, &len));
    assert_int_equal(len, 0U);
}

static void test_bind_null_is_ignored(void** state)
{
    (void) state;
    /* 防御性空指针契约：绑定函数不得解引用。 */
    iolink_isdu_bind_workspace(NULL, NULL);
    iolink_ds_bind_workspace(NULL, NULL);
    assert_null(iolink_device_get_workspace(NULL));
    assert_int_equal(iolink_device_init(NULL, NULL), IOLINK_DEVICE_INIT_ERR_NULL);
}

static void test_ds_null_api_contracts(void** state)
{
    (void) state;
    assert_int_equal(iolink_ds_serialize(NULL, NULL, 0U), -1);
    assert_int_equal(iolink_ds_build_image(NULL), -1);
    iolink_ds_refresh(NULL);
    assert_null(iolink_ds_get_image(NULL, NULL));
    assert_int_equal(iolink_ds_param_count(NULL), 0U);
    iolink_ds_bind_sources(NULL, NULL, NULL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_binds_external_workspace),
        cmocka_unit_test(test_device_falls_back_to_embedded_scratch),
        cmocka_unit_test(test_ds_serialize_matches_stored_image),
        cmocka_unit_test(test_ds_refresh_without_image_buffer),
        cmocka_unit_test(test_bind_null_is_ignored),
        cmocka_unit_test(test_ds_null_api_contracts),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
