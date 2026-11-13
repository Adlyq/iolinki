/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ds_app_od.c
 * @brief 应用 OD 表参与数据存储（DS）集合的端到端契约。
 *
 * 覆盖：
 *  - `config->od` 被设备层绑定到 DS，`IOLINK_OD_STORAGE_DS` 应用项进入
 *    描述符集合（标准表在前、应用表按表序）；
 *  - 下载态写入应用 DS 参数被暂存，ParamDownloadEnd 后才生效；
 *  - `iolink_ds_serialize()` 输出应用项，VOLATILE 项不进入 DS；
 *  - `iolink_ds_apply_image()` 能恢复应用参数；
 *  - 恢复前的整份预校验：非法记录让整次恢复失败，先前记录不生效。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/isdu.h"
#include "iolinki/od.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

#define APP_DS_IDX 0x0180U
#define APP_VOLATILE_IDX 0x0181U

typedef struct
{
    uint16_t ds_value;
    uint16_t volatile_value;
} app_state_t;

static app_state_t g_app_state;
/* 设备配置保存的是指针：应用表必须比设备上下文存活更久。 */
static iolink_od_app_t g_app_od;

static iolink_result_t app_od_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                   size_t cap)
{
    const app_state_t* state = user;
    const uint32_t value = (entry->index == APP_DS_IDX) ? state->ds_value : state->volatile_value;

    return iolink_od_encode(entry, value, out, cap);
}

static iolink_result_t app_od_write(void* user, const iolink_od_entry_t* entry, uint8_t subindex,
                                    const uint8_t* data, size_t len)
{
    app_state_t* state = user;
    uint32_t value = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &value);

    (void) subindex;
    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    if (entry->index == APP_DS_IDX) {
        state->ds_value = (uint16_t) value;
    }
    else if (entry->index == APP_VOLATILE_IDX) {
        state->volatile_value = (uint16_t) value;
    }
    else {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    return iolink_result_ok(0U);
}

/* 0x0180 属于 DS；0x0181 只是普通可写参数。 */
static const iolink_od_entry_t k_app_entries[] = {
    IOLINK_OD_SCALAR(APP_DS_IDX, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_STORE,
                     IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND, 0U, 0xFFFFU, 0U,
                     "AppStoredParameter"),
    IOLINK_OD_SCALAR(APP_VOLATILE_IDX, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                     IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                     IOLINK_OD_HANDLER_BACKEND, 0U, 0xFFFFU, 0U, "AppVolatileParameter"),
};

static void app_od_init(iolink_od_app_t* app)
{
    app->entries = k_app_entries;
    app->count = sizeof(k_app_entries) / sizeof(k_app_entries[0]);
    app->read = app_od_read;
    app->write = app_od_write;
    app->user = &g_app_state;
}

static int ds_app_setup(void** state)
{
    (void) state;
    return 0;
}

static int ds_app_teardown(void** state)
{
    (void) state;
    return 0;
}

/** @brief 通过 ISDU 写一个参数并返回应答体长度（成功时为 0）。 */
static int app_write_object(iolink_device_ctx_t* ctx, uint16_t index, const uint8_t* data,
                            uint8_t len, uint8_t* out, size_t out_size)
{
    iolink_isdu_ctx_t* isdu = &ctx->dll.main.shared.isdu;

    assert_int_equal(isdu_send_write_request(isdu, index, 0U, data, len), 1);
    iolink_isdu_process(isdu);
    return isdu_collect_response(isdu, out, out_size);
}

/** @brief 在序列化映像中定位一个记录。 */
static bool image_find_record(const uint8_t* image, size_t len, uint16_t index, uint8_t subindex,
                              const uint8_t** data, size_t* data_len)
{
    size_t pos = 0U;

    while (pos + 4U <= len) {
        const uint16_t rec_index = (uint16_t) (((uint16_t) image[pos] << 8) | image[pos + 1U]);
        const uint8_t rec_sub = image[pos + 2U];
        const size_t rec_len = image[pos + 3U];
        if (pos + 4U + rec_len > len) {
            return false;
        }
        if ((rec_index == index) && (rec_sub == subindex)) {
            if (data != NULL) {
                *data = &image[pos + 4U];
            }
            if (data_len != NULL) {
                *data_len = rec_len;
            }
            return true;
        }
        pos += 4U + rec_len;
    }
    return false;
}

static void init_device_with_app(iolink_test_device_t* dev)
{
    (void) memset(&g_app_state, 0, sizeof(g_app_state));
    g_app_state.ds_value = 0x1111U;
    g_app_state.volatile_value = 0x2222U;
    app_od_init(&g_app_od);

    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init_with_od(dev, NULL, NULL, &g_app_od), 0);
}

/* 应用 DS 项进入描述符集合，VOLATILE 项不进入。 */
static void test_app_ds_descriptor_set(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    init_device_with_app(&dev);

    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_non_null(ds);
    assert_int_equal(iolink_od_ds_count(), 3U);
    assert_int_equal(iolink_od_ds_count_app(&g_app_od), 1U);
    assert_int_equal(iolink_ds_param_count(ds), 4U);

    /* 标准表在前（标签），应用表按表序追加。 */
    iolink_ds_param_desc_t desc;
    assert_true(iolink_ds_param_at(ds, 0U, &desc));
    assert_int_equal(desc.index, IOLINK_IDX_APPLICATION_TAG);
    assert_true(iolink_ds_param_at(ds, 3U, &desc));
    assert_int_equal(desc.index, APP_DS_IDX);
    assert_int_equal(desc.subindex, 0U);
    assert_false(iolink_ds_param_at(ds, 4U, &desc));

    assert_true(iolink_od_is_ds(&g_app_od, APP_DS_IDX, 0U));
    assert_false(iolink_od_is_ds(&g_app_od, APP_VOLATILE_IDX, 0U));
}

/* 下载态写入被暂存；VOLATILE 参数立即生效且不进镜像。 */
static void test_app_ds_download_stages_then_commits(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    uint8_t resp[8] = {0U};

    init_device_with_app(&dev);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_non_null(ds);

    assert_int_equal(iolink_ds_handle_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    assert_true(iolink_ds_downloading(ds));
    const size_t staged_before = ds->image_len;

    const uint8_t stored[2] = {0xABU, 0xCDU};
    assert_int_equal(app_write_object(&dev.ctx, APP_DS_IDX, stored, 2U, resp, sizeof(resp)), 0);
    /* 暂存期间参数值不变，但镜像已增长。 */
    assert_int_equal(g_app_state.ds_value, 0x1111U);
    assert_true(ds->image_len > staged_before);

    const uint8_t volatile_value[2] = {0x33U, 0x44U};
    const size_t image_after_ds = ds->image_len;
    assert_int_equal(
        app_write_object(&dev.ctx, APP_VOLATILE_IDX, volatile_value, 2U, resp, sizeof(resp)), 0);
    assert_int_equal(g_app_state.volatile_value, 0x3344U);
    assert_int_equal(ds->image_len, image_after_ds);

    assert_int_equal(iolink_ds_handle_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), 0);
    assert_int_equal(g_app_state.ds_value, 0xABCDU);

    /* 序列化映像包含应用 DS 记录，且不包含 VOLATILE 参数。 */
    uint8_t image[IOLINK_DS_IMAGE_MAX];
    const int serialized = iolink_ds_serialize(ds, image, sizeof(image));
    assert_true(serialized > 0);
    const uint8_t* data = NULL;
    size_t data_len = 0U;
    assert_true(image_find_record(image, (size_t) serialized, APP_DS_IDX, 0U, &data, &data_len));
    assert_int_equal(data_len, 2U);
    assert_memory_equal(data, stored, 2U);
    assert_false(image_find_record(image, (size_t) serialized, APP_VOLATILE_IDX, 0U, NULL, NULL));
}

/* apply_image 能恢复应用 DS 参数。 */
static void test_app_ds_apply_image_restores(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    size_t backup_len = 0U;

    init_device_with_app(&dev);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_non_null(ds);

    g_app_state.ds_value = 0xABCDU;
    assert_true(iolink_ds_build_image(ds) > 0);
    const uint8_t* image = iolink_ds_get_image(ds, &backup_len);
    assert_non_null(image);
    assert_true(backup_len <= sizeof(backup));
    (void) memcpy(backup, image, backup_len);

    g_app_state.ds_value = 0x0001U;
    assert_int_equal(iolink_ds_apply_image(ds, backup, backup_len), 0);
    assert_int_equal(g_app_state.ds_value, 0xABCDU);
}

/* 非法记录让整次恢复失败，且先前记录不生效。 */
static void test_app_ds_rejects_whole_image(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    uint8_t resp[8] = {0U};

    init_device_with_app(&dev);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_non_null(ds);

    const uint8_t old_tag[3] = {'O', 'L', 'D'};
    assert_int_equal(
        app_write_object(&dev.ctx, IOLINK_IDX_FUNCTION_TAG, old_tag, 3U, resp, sizeof(resp)), 0);

    /* 第一条合法（FunctionTag = "OK"），第二条 ApplicationTag 超过 32 字节。
       预校验必须在写入任何参数之前拒绝整份镜像。 */
    uint8_t bad[4U + 2U + 4U + 40U];
    size_t pos = 0U;
    bad[pos++] = 0x00U;
    bad[pos++] = 0x19U; /* FunctionTag */
    bad[pos++] = 0x00U;
    bad[pos++] = 0x02U;
    bad[pos++] = 'O';
    bad[pos++] = 'K';
    bad[pos++] = 0x00U;
    bad[pos++] = 0x18U; /* ApplicationTag */
    bad[pos++] = 0x00U;
    bad[pos++] = 40U;
    (void) memset(&bad[pos], 'A', 40U);
    pos += 40U;

    uint8_t tag[40] = {0U};
    uint8_t before_app_tag[40] = {0U};
    const iolink_result_t before_app = iolink_params_ctx_read(
        &dev.ctx.params, IOLINK_IDX_APPLICATION_TAG, 0U, before_app_tag, sizeof(before_app_tag));
    assert_int_equal(before_app.code, 0);
    const size_t before_app_len = before_app.len;

    assert_int_equal(iolink_ds_apply_image(ds, bad, pos), -2);

    const iolink_result_t kept_fun =
        iolink_params_ctx_read(&dev.ctx.params, IOLINK_IDX_FUNCTION_TAG, 0U, tag, sizeof(tag));
    assert_int_equal(kept_fun.code, 0);
    assert_int_equal(kept_fun.len, 3);
    assert_memory_equal(tag, "OLD", 3U);
    (void) memset(tag, 0, sizeof(tag));
    const iolink_result_t kept_app =
        iolink_params_ctx_read(&dev.ctx.params, IOLINK_IDX_APPLICATION_TAG, 0U, tag, sizeof(tag));
    assert_int_equal(kept_app.code, 0);
    assert_int_equal(kept_app.len, before_app_len);
    assert_memory_equal(tag, before_app_tag, before_app_len);

    /* 合法镜像仍能完整恢复。 */
    const uint8_t new_tag[3] = {'N', 'E', 'W'};
    assert_int_equal(
        app_write_object(&dev.ctx, IOLINK_IDX_FUNCTION_TAG, new_tag, 3U, resp, sizeof(resp)), 0);
    assert_true(iolink_ds_build_image(ds) > 0);
    size_t image_len = 0U;
    const uint8_t* image = iolink_ds_get_image(ds, &image_len);
    assert_non_null(image);
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    assert_true(image_len <= sizeof(backup));
    (void) memcpy(backup, image, image_len);

    const uint8_t reset_tag[3] = {'O', 'L', 'D'};
    assert_int_equal(
        app_write_object(&dev.ctx, IOLINK_IDX_FUNCTION_TAG, reset_tag, 3U, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_ds_apply_image(ds, backup, image_len), 0);
    (void) memset(tag, 0, sizeof(tag));
    const iolink_result_t restored_fun =
        iolink_params_ctx_read(&dev.ctx.params, IOLINK_IDX_FUNCTION_TAG, 0U, tag, sizeof(tag));
    assert_int_equal(restored_fun.code, 0);
    assert_int_equal(restored_fun.len, 3);
    assert_memory_equal(tag, "NEW", 3U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_app_ds_descriptor_set, ds_app_setup, ds_app_teardown),
        cmocka_unit_test_setup_teardown(test_app_ds_download_stages_then_commits, ds_app_setup,
                                        ds_app_teardown),
        cmocka_unit_test_setup_teardown(test_app_ds_apply_image_restores, ds_app_setup,
                                        ds_app_teardown),
        cmocka_unit_test_setup_teardown(test_app_ds_rejects_whole_image, ds_app_setup,
                                        ds_app_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
