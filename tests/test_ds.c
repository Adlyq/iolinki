/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ds.c
 * @brief 数据存储（DS）的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "iolinki/params.h"
#include "iolinki/device_info.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static iolink_test_env_t g_test_env;

#include "conformance_test.h"

static void test_ds_checksum(void** state)
{
    (void) state;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint32_t cs1 = iolink_ds_calc_checksum(data, sizeof(data));
    uint32_t cs2 = iolink_ds_calc_checksum(data, sizeof(data));
    assert_int_equal(cs1, cs2);

    data[0] = 0xAA;
    uint32_t cs3 = iolink_ds_calc_checksum(data, sizeof(data));
    assert_int_not_equal(cs1, cs3);
}

static void test_ds_storage_integration(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);
    ds.params_ctx = test_env_params(&g_test_env);

    uint8_t write_data[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t read_buf[4] = {0};

    assert_int_equal(ds.storage->write(ds.storage->user, 0, write_data, 4), 0);
    assert_int_equal(ds.storage->read(ds.storage->user, 0, read_buf, 4), 0);
    assert_memory_equal(read_buf, write_data, 4);
}

static void test_ds_state_transitions(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0077");
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);

    /* 1. 触发 Download（主站数据更新） */
    iolink_ds_check(&ds, 0xABCD);
    iolink_ds_process(&ds); /* Req -> Downloading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    iolink_ds_process(&ds); /* 保持 Downloading 直到 DownloadEnd */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. 触发 Upload（主站为 0x0000——空） */
    iolink_ds_check(&ds, 0x0000);
    iolink_ds_process(&ds); /* Req -> Uploading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    iolink_ds_process(&ds); /* 保持 Uploading 直到 UploadEnd */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_UPLOAD_END, 0), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

/* 当 ISDU 传输空闲时，设备泵会服务 DS 状态机：
 * 无需手动调用 iolink_ds_process()。 */
static void test_ds_auto_processed_by_device_pump(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    /* 繁忙的 ISDU 传输会阻塞 DS 服务。HEADER_INITIAL 是
     * iolink_isdu_process() 不会改变的状态，因此它在
     * 泵调用期间保持繁忙。 */
    assert_int_equal(
        iolink_ds_handle_param_command(&dev.ctx.dll.main.ds, IOLINK_CMD_PARAM_UPLOAD_START, 0U), 0);
    dev.ctx.dll.main.shared.isdu.state = ISDU_STATE_HEADER_INITIAL;
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.main.ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    /* ISDU 空闲后，泵会推进 DS 状态机。 */
    dev.ctx.dll.main.shared.isdu.state = ISDU_STATE_IDLE;
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.main.ds.state, IOLINK_DS_STATE_UPLOADING);
}

static void test_ds_commands_locked(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0156");
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);

    /* 1. 在锁定状态下尝试 Download Start（写） */
    int ret = iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, -2); /* 访问被拒绝 */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. 在锁定状态下尝试 Upload Start（读）——通常应通过？
       规范：访问锁通常适用于参数写入（Index 2）。
       Data Storage 上传是从设备读取，因此或许允许？
       我们实现的代码只对 Download（写）检查锁定。
    */
    ret = iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_UPLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, 0); /* 成功 */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
}

static void test_ds_commands_unlocked(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);

    /* 1. Download Start（写）未锁定 */
    int ret = iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    /* 2. 结束 Download */
    ret = iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static void test_ds_param_round_trip(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);

    /* 配置已知的参数值。 */
    const char* app = "APP-TAG-1";
    const char* fun = "FLOW";
    const char* loc = "TANK-7";
    assert_int_equal(
        iolink_params_ctx_write(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U,
                                (const uint8_t*) app, strlen(app))
            .code,
        0);
    assert_int_equal(iolink_params_ctx_write(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG,
                                             0U, (const uint8_t*) fun, strlen(fun))
                         .code,
                     0);
    assert_int_equal(iolink_params_ctx_write(test_env_params(&g_test_env), IOLINK_IDX_LOCATION_TAG,
                                             0U, (const uint8_t*) loc, strlen(loc))
                         .code,
                     0);

    /* 上传：将当前参数序列化到 DS 镜像中（备份）。 */
    int n = iolink_ds_build_image(&ds);
    assert_true(n > 0);
    size_t backup_len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &backup_len);
    assert_non_null(img);
    assert_int_equal((int) backup_len, n);
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    memcpy(backup, img, backup_len);
    uint32_t backup_cs = ds.current_checksum;
    assert_int_not_equal(backup_cs, 0);

    /* 清除设备参数。 */
    iolink_params_ctx_factory_reset(test_env_params(&g_test_env));
    uint8_t buf[40];
    const iolink_result_t cleared_app = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    const iolink_result_t cleared_fun = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(cleared_app.code, 0);
    assert_int_equal(cleared_app.len, 0);
    assert_int_equal(cleared_fun.code, 0);
    assert_int_equal(cleared_fun.len, 0);

    /* 下载：应用备份镜像（恢复）。 */
    assert_int_equal(iolink_ds_apply_image(&ds, backup, backup_len), 0);
    assert_int_equal(ds.current_checksum, backup_cs);

    /* 参数被逐字节恢复。 */
    const iolink_result_t app_read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(app_read.code, 0);
    assert_int_equal(app_read.len, strlen(app));
    assert_memory_equal(buf, app, strlen(app));
    const iolink_result_t fun_read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(fun_read.code, 0);
    assert_int_equal(fun_read.len, strlen(fun));
    assert_memory_equal(buf, fun, strlen(fun));
    const iolink_result_t loc_read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_LOCATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(loc_read.code, 0);
    assert_int_equal(loc_read.len, strlen(loc));
    assert_memory_equal(buf, loc, strlen(loc));
}

static void test_ds_bound_params_restore_device_context_only(void** state)
{
    (void) state;
    iolink_device_info_ctx_t info_a;
    iolink_device_info_ctx_t info_b;
    iolink_params_ctx_t params_a;
    iolink_params_ctx_t params_b;
    iolink_ds_ctx_t ds_a;
    iolink_ds_ctx_t ds_b;
    static const uint8_t tag_a[] = "CtxA";
    static const uint8_t tag_b[] = "CtxB";
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    uint8_t out[16] = {0U};

    iolink_device_info_ctx_init(&info_a, NULL);
    iolink_device_info_ctx_init(&info_b, NULL);
    iolink_params_ctx_init(&params_a, &info_a);
    iolink_params_ctx_init(&params_b, &info_b);
    iolink_ds_init(&ds_a, NULL);
    iolink_ds_init(&ds_b, NULL);
    iolink_ds_bind_sources(&ds_a, NULL, &params_a);
    iolink_ds_bind_sources(&ds_b, NULL, &params_b);

    assert_int_equal(iolink_params_ctx_write(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, tag_a,
                                             sizeof(tag_a) - 1U)
                         .code,
                     0);
    assert_int_equal(iolink_params_ctx_write(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, tag_b,
                                             sizeof(tag_b) - 1U)
                         .code,
                     0);

    assert_true(iolink_ds_build_image(&ds_a) > 0);
    assert_true(ds_a.image_len <= sizeof(backup));
    memcpy(backup, ds_a.image, ds_a.image_len);

    iolink_params_ctx_factory_reset(&params_a);
    assert_int_equal(iolink_ds_apply_image(&ds_a, backup, ds_a.image_len), 0);

    const iolink_result_t read_a =
        iolink_params_ctx_read(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out));
    assert_int_equal(read_a.code, 0);
    assert_int_equal(read_a.len, sizeof(tag_a) - 1U);
    assert_memory_equal(out, tag_a, sizeof(tag_a) - 1U);

    memset(out, 0, sizeof(out));
    const iolink_result_t read_b =
        iolink_params_ctx_read(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out));
    assert_int_equal(read_b.code, 0);
    assert_int_equal(read_b.len, sizeof(tag_b) - 1U);
    assert_memory_equal(out, tag_b, sizeof(tag_b) - 1U);
}

static void test_ds_apply_rejects_truncated(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    /* 头部声称有 5 字节值，但后面只有 2 个数据字节 -> 被拒绝。 */
    uint8_t bad[] = {0x00, 0x18, 0x00, 0x05, 0xAA, 0xBB};
    assert_int_equal(iolink_ds_apply_image(&ds, bad, sizeof(bad)), -1);
}

/* 由静态缓冲区支撑的厂商参数（0x0050）：进入 DS 集合的路径是表项的
 * storage 标志，值由应用 OD 后端提供。 */
#define VENDOR_PARAM_IDX 0x0050U
static uint8_t g_vendor_param[8];
static size_t g_vendor_param_len;

static iolink_result_t vendor_od_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                      size_t cap)
{
    (void) user;
    if (entry->index != VENDOR_PARAM_IDX) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    const size_t n = (g_vendor_param_len < cap) ? g_vendor_param_len : cap;
    (void) memcpy(out, g_vendor_param, n);
    return iolink_result_ok((uint16_t) n);
}

static iolink_result_t vendor_od_write(void* user, const iolink_od_entry_t* entry, uint8_t subindex,
                                       const uint8_t* data, size_t len)
{
    (void) user;
    (void) subindex;
    if (entry->index != VENDOR_PARAM_IDX) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if (len > sizeof(g_vendor_param)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    (void) memcpy(g_vendor_param, data, len);
    g_vendor_param_len = len;
    return iolink_result_ok(0U);
}

static const iolink_od_entry_t k_vendor_entries[] = {
    IOLINK_OD_BYTES_HANDLER(VENDOR_PARAM_IDX, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                            IOLINK_OD_STORAGE_DS, 8U, "VendorParam", vendor_od_read,
                            vendor_od_write),
};

static iolink_od_app_t k_vendor_app = {
    .entries = k_vendor_entries,
    .count = sizeof(k_vendor_entries) / sizeof(k_vendor_entries[0]),
    .user = NULL,
};

static void test_ds_vendor_param_set_round_trip(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const uint8_t vend[] = {0xDEU, 0xADU, 0xBEU, 0xEFU};

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_sources(&ds, &k_vendor_app, test_env_params(&g_test_env));

    assert_int_equal(
        iolink_params_ctx_write(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U,
                                (const uint8_t*) "TAG", 3U)
            .code,
        0);
    g_vendor_param_len = sizeof(vend);
    (void) memcpy(g_vendor_param, vend, sizeof(vend));

    /* 上传：镜像现在必须同时包含标准标签与厂商记录。 */
    assert_true(iolink_ds_build_image(&ds) > 0);
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &len);
    assert_non_null(img);
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    assert_true(len <= sizeof(backup));
    (void) memcpy(backup, img, len);

    /* 清除两个来源并从镜像恢复。 */
    g_vendor_param_len = 0U;
    (void) memset(g_vendor_param, 0, sizeof(g_vendor_param));
    iolink_params_ctx_factory_reset(test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_apply_image(&ds, backup, len), 0);

    assert_int_equal(g_vendor_param_len, sizeof(vend));
    assert_memory_equal(g_vendor_param, vend, sizeof(vend));
    uint8_t tag[8] = {0U};
    const iolink_result_t tag_read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, tag, sizeof(tag));
    assert_int_equal(tag_read.code, 0);
    assert_int_equal(tag_read.len, 3);
    assert_memory_equal(tag, "TAG", 3U);
}

static void test_ds_download_recovery(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);

    /* 构造一个镜像，包含已知参数（ApplicationTag 0x18 = "X"）以及
       设备无法存储的未知索引（0x9999）。 */
    uint8_t img[16];
    size_t p = 0U;
    img[p++] = 0x00;
    img[p++] = 0x18;
    img[p++] = 0x00;
    img[p++] = 0x01;
    img[p++] = 'X';
    img[p++] = 0x99;
    img[p++] = 0x99;
    img[p++] = 0x00;
    img[p++] = 0x01;
    img[p++] = 0xAB;
    uint32_t downloaded_cs = iolink_ds_calc_checksum(img, p);

    assert_int_equal(iolink_ds_apply_image(&ds, img, p), 0);

    /* 已知参数已被应用。 */
    uint8_t buf[8];
    const iolink_result_t applied = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(applied.code, 0);
    assert_int_equal(applied.len, 1);
    assert_int_equal(buf[0], 'X');

    /* 设备校验和反映实际状态（未知索引被丢弃），因此
       它与主站下载镜像的校验和不同。 */
    assert_int_not_equal(ds.current_checksum, downloaded_cs);

    /* 针对过时主站校验和的一致性检查会重新请求下载。 */
    iolink_ds_check(&ds, downloaded_cs);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
}

static iolink_result_t rejecting_od_write(void* user, const iolink_od_entry_t* entry,
                                          uint8_t subindex, const uint8_t* data, size_t len)
{
    (void) user;
    (void) entry;
    (void) subindex;
    (void) data;
    (void) len;
    return iolink_result_error(IOLINK_RESULT_INVALID);
}

static const iolink_od_entry_t k_reject_entries[] = {
    IOLINK_OD_BYTES_HANDLER(VENDOR_PARAM_IDX, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                            IOLINK_OD_STORAGE_DS, 8U, "Rejected", NULL, rejecting_od_write),
};

static iolink_od_app_t k_reject_app = {
    .entries = k_reject_entries,
    .count = sizeof(k_reject_entries) / sizeof(k_reject_entries[0]),
    .user = NULL,
};

/* ParamDownloadEnd 将格式错误的镜像（PAR_SETINVALID 0x8040）与
 * 设备拒绝的已知参数（PAR_SETINCONSIST 0x8041）区分开。 */
static void test_ds_download_end_error_classification(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;

    /* 格式错误的暂存镜像 -> -1 -> PAR_SETINVALID。 */
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t bad[] = {0x00U, 0x18U, 0x00U, 0x05U, 0xAAU}; /* 声称 5 字节，实际为 1 */
    (void) memcpy(ds.image, bad, sizeof(bad));
    ds.image_len = sizeof(bad);
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -5);

    /* 已知参数被拒绝 -> -2 -> PAR_SETINCONSIST。 */
    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_sources(&ds, &k_reject_app, test_env_params(&g_test_env));
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t img[] = {0x00U, 0x50U, 0x00U, 0x03U, 'A', 'B', 'C'};
    (void) memcpy(ds.image, img, sizeof(img));
    ds.image_len = sizeof(img);
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -4);
}

/* 损坏的参数镜像（位翻转/写撕裂）必须在加载时被丢弃。 */
static void test_ds_params_image_checksum_rejects_corruption(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, NULL, test_env_params(&g_test_env));

    const uint8_t tag[] = "A";
    assert_int_equal(iolink_params_ctx_write(test_env_params(&g_test_env),
                                             IOLINK_IDX_APPLICATION_TAG, 0U, tag, 1U)
                         .code,
                     0);
    assert_int_equal(iolink_ds_save_persistent(&ds), 0);

    /* 翻转参数镜像中的第一个记录数据字节（不是索引，避免变成未知索引
       而被自然跳过）。 */
    uint8_t* buf = iolink_ds_mock_get_buf();
    buf[IOLINK_STORAGE_PARAMS_BASE + IOLINK_STORAGE_HDR_SIZE + 4U] ^= 0xFFU;

    /* 重新加载：校验和不匹配时必须丢弃该镜像（不应用任何值）。 */
    test_env_reset_params(&g_test_env);
    uint8_t baseline[16] = {0U};
    const iolink_result_t before = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, baseline, sizeof(baseline));
    assert_int_equal(before.code, 0);

    iolink_ds_ctx_t reloaded;
    iolink_ds_init(&reloaded, &g_ds_storage_mock);
    iolink_ds_bind_sources(&reloaded, NULL, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_load_persistent(&reloaded), -1);

    uint8_t out[8] = {0U};
    const iolink_result_t read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out));
    assert_int_equal(read.code, 0);
    assert_int_equal(read.len, before.len);
    assert_memory_equal(out, baseline, before.len);
}

/* 下载暂存期间写 PERSIST-only 参数不得覆盖暂存镜像（两者共用 ds_stage）。 */
static void test_ds_download_survives_persist_only_write(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_isdu_ctx_t isdu;
    static uint16_t local_value;
    static const iolink_od_entry_t local_entries[] = {
        IOLINK_OD_SCALAR_STORAGE(0x01A0U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                 IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_PERSIST, 0U, 0xFFFFU, 0U,
                                 "LocalOnly", &local_value),
    };
    static const iolink_od_app_t local_app = {
        .entries = local_entries,
        .count = sizeof(local_entries) / sizeof(local_entries[0]),
        .user = NULL,
    };
    uint8_t response[4] = {0U};

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, &local_app, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);

    const uint8_t staged[] = {'A'};
    assert_int_equal(iolink_ds_stage_param(&ds, IOLINK_IDX_APPLICATION_TAG, 0U, staged, 1U).code,
                     0);
    const size_t staged_len = ds.image_len;
    uint8_t snapshot[IOLINK_DS_IMAGE_MAX];
    assert_true(staged_len <= sizeof(snapshot));
    (void) memcpy(snapshot, ds.image, staged_len);

    iolink_isdu_init(&isdu);
    isdu.ds_ctx = &ds;
    isdu.od = &local_app;
    isdu.params_ctx = test_env_params(&g_test_env);
    const uint8_t wire[2] = {0x12U, 0x34U};
    assert_int_equal(isdu_send_write_request(&isdu, 0x01A0U, 0U, wire, sizeof(wire)), 1);
    iolink_isdu_process(&isdu);
    assert_int_equal(isdu_collect_response(&isdu, response, sizeof(response)), 0);
    assert_int_equal(local_value, 0x1234U);

    /* 暂存镜像必须原样保留，下载可以在下一帧继续。 */
    assert_int_equal(ds.image_len, staged_len);
    assert_memory_equal(ds.image, snapshot, staged_len);

    /* DownloadEnd 统一保存：新上下文恢复暂存的标签与 PERSIST-only 参数。 */
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    iolink_device_info_ctx_t info2;
    iolink_params_ctx_t params2;
    iolink_ds_ctx_t ds2;
    iolink_device_info_ctx_init(&info2, NULL);
    iolink_params_ctx_init(&params2, &info2);
    local_value = 0U;
    iolink_ds_init(&ds2, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds2, &local_app, &params2);
    assert_int_equal(iolink_ds_load_persistent(&ds2), 0);
    assert_int_equal(local_value, 0x1234U);
    uint8_t tag_out[4] = {0U};
    const iolink_result_t tag_read =
        iolink_params_ctx_read(&params2, IOLINK_IDX_APPLICATION_TAG, 0U, tag_out, sizeof(tag_out));
    assert_int_equal(tag_read.len, 1U);
    assert_int_equal(tag_out[0], 'A');
}

/* 表项的 block_len 限制存入镜像的值。 */
static void test_ds_entry_block_len_bounds_value(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_od_entry_t bounded_entries[] = {
        IOLINK_OD_BYTES_HANDLER(VENDOR_PARAM_IDX, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                                IOLINK_OD_STORAGE_DS, 4U, "Bounded", vendor_od_read,
                                vendor_od_write),
    };
    static iolink_od_app_t bounded_app = {
        .entries = bounded_entries,
        .count = sizeof(bounded_entries) / sizeof(bounded_entries[0]),
        .user = NULL,
    };

    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_sources(&ds, &bounded_app, test_env_params(&g_test_env));
    g_vendor_param_len = 8U;
    (void) memcpy(g_vendor_param, "ABCDEFGH", 8U);

    assert_true(iolink_ds_build_image(&ds) > 0);
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &len);
    assert_non_null(img);

    /* 集合里标准标签在前：定位厂商记录，不假设固定偏移。 */
    size_t pos = 0U;
    bool found = false;
    while ((pos + 4U) <= len) {
        const uint16_t index = (uint16_t) (((uint16_t) img[pos] << 8) | (uint16_t) img[pos + 1U]);
        const size_t record_len = (size_t) img[pos + 3U];
        if (index == VENDOR_PARAM_IDX) {
            assert_int_equal(img[pos + 3U], 4U); /* 长度字节被限制为 block_len */
            assert_memory_equal(&img[pos + 4U], "ABCD", 4U);
            found = true;
            break;
        }
        pos += 4U + record_len;
    }
    assert_true(found);
}

/* ------------------------------------------------------------------------- */
/* 数据存储一致性用例 0077-0081、0374 的共享 fixture。                       */
/* ------------------------------------------------------------------------- */

typedef struct
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
} ds_fixture_t;

static void ds_fixture_init(ds_fixture_t* f, const iolink_storage_api_t* storage)
{
    iolink_device_info_ctx_init(&f->info, NULL);
    iolink_params_ctx_init(&f->params, &f->info);
    iolink_ds_init(&f->ds, storage);
    iolink_ds_bind_sources(&f->ds, NULL, &f->params);
    iolink_events_init(&f->events);
}

static void ds_fixture_set_tags(ds_fixture_t* f, const char* app, const char* fun, const char* loc)
{
    assert_int_equal(iolink_params_ctx_write(&f->params, IOLINK_IDX_APPLICATION_TAG, 0U,
                                             (const uint8_t*) app, strlen(app))
                         .code,
                     0);
    assert_int_equal(iolink_params_ctx_write(&f->params, IOLINK_IDX_FUNCTION_TAG, 0U,
                                             (const uint8_t*) fun, strlen(fun))
                         .code,
                     0);
    assert_int_equal(iolink_params_ctx_write(&f->params, IOLINK_IDX_LOCATION_TAG, 0U,
                                             (const uint8_t*) loc, strlen(loc))
                         .code,
                     0);
}

static int ds_fixture_isdu_read(ds_fixture_t* f, uint16_t index, uint8_t sub, uint8_t* buf,
                                size_t cap)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    ctx.ds_ctx = &f->ds;
    ctx.params_ctx = &f->params;
    ctx.event_ctx = &f->events;
    assert_int_equal(isdu_send_read_request(&ctx, index, sub), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, buf, cap);
}

static int ds_fixture_isdu_write(ds_fixture_t* f, uint16_t index, uint8_t sub, const uint8_t* data,
                                 uint8_t len, uint8_t* buf, size_t cap)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    ctx.ds_ctx = &f->ds;
    ctx.params_ctx = &f->params;
    ctx.event_ctx = &f->events;
    assert_int_equal(isdu_send_write_request(&ctx, index, sub, data, len), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, buf, cap);
}

static int ds_fixture_command(ds_fixture_t* f, uint8_t cmd, uint8_t* buf, size_t cap)
{
    return ds_fixture_isdu_write(f, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U, buf, cap);
}

static int ds_fixture_ds_command(ds_fixture_t* f, uint8_t cmd, uint8_t* buf, size_t cap)
{
    return ds_fixture_isdu_write(f, IOLINK_IDX_DATA_STORAGE, 1U, &cmd, 1U, buf, cap);
}

static uint32_t ds_fixture_read_u32(ds_fixture_t* f, uint8_t sub)
{
    uint8_t r[4] = {0U};
    assert_int_equal(ds_fixture_isdu_read(f, IOLINK_IDX_DATA_STORAGE, sub, r, sizeof(r)), 4);
    return ((uint32_t) r[0] << 24) | ((uint32_t) r[1] << 16) | ((uint32_t) r[2] << 8) |
           (uint32_t) r[3];
}

static void ds_fixture_assert_tag(ds_fixture_t* f, uint16_t index, const char* expected)
{
    uint8_t buf[40] = {0U};
    const size_t expected_len = strlen(expected);
    const iolink_result_t read_result =
        iolink_params_ctx_read(&f->params, index, 0U, buf, sizeof(buf));
    assert_int_equal(read_result.code, 0);
    assert_int_equal(read_result.len, expected_len);
    assert_memory_equal(buf, expected, expected_len);
}

static bool ds_image_find(const uint8_t* img, size_t len, uint16_t index, uint8_t sub,
                          const uint8_t** value, size_t* value_len)
{
    size_t pos = 0U;
    while ((pos + 4U) <= len) {
        const uint16_t idx = (uint16_t) (((uint16_t) img[pos] << 8) | (uint16_t) img[pos + 1U]);
        const uint8_t s = img[pos + 2U];
        const size_t rlen = (size_t) img[pos + 3U];
        if ((pos + 4U + rlen) > len) {
            return false;
        }
        if ((idx == index) && (s == sub)) {
            *value = &img[pos + 4U];
            *value_len = rlen;
            return true;
        }
        pos += 4U + rlen;
    }
    return false;
}

static void ds_assert_image_record(const uint8_t* img, size_t len, uint16_t index,
                                   const char* expected)
{
    const uint8_t* value = NULL;
    size_t value_len = 0U;
    assert_true(ds_image_find(img, len, index, 0U, &value, &value_len));
    assert_int_equal((int) value_len, (int) strlen(expected));
    assert_memory_equal(value, expected, strlen(expected));
}

/* 期望帧用例：DataStorageIndex 子索引 0 的旧式整镜像载荷是运行时
 * 兼容位。严格路径读拼接记录（10+3n 字节），兼容路径直接返回整镜像；
 * 写操作在严格路径被 WRITE_PROTECTED 拒绝，兼容路径被接受。
 * 这条用例取代了已退出差分门的 ds_legacy_sub0 配置。 */
static int ds_fixture_read_sub0_with_caps(ds_fixture_t* f, const iolink_caps_t* caps, uint8_t* buf,
                                          size_t cap)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    ctx.ds_ctx = &f->ds;
    ctx.params_ctx = &f->params;
    ctx.event_ctx = &f->events;
    iolink_isdu_bind_caps(&ctx, caps);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, buf, cap);
}

static int ds_fixture_write_sub0_with_caps(ds_fixture_t* f, const iolink_caps_t* caps,
                                           const uint8_t* data, uint8_t len, uint8_t* buf,
                                           size_t cap)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    ctx.ds_ctx = &f->ds;
    ctx.params_ctx = &f->params;
    ctx.event_ctx = &f->events;
    iolink_isdu_bind_caps(&ctx, caps);
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0U, data, len), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, buf, cap);
}

static void test_ds_legacy_sub0_is_runtime_compat(void** state)
{
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "APP1", "FLOW", "TANK");
    assert_true(iolink_ds_build_image(&f.ds) > 0);
    assert_true(f.ds.image_len > 0U);

    /* 严格 V1.1：sub0 读是拼接记录，写被写保护拒绝。 */
    iolink_caps_t strict = IOLINK_CAP_SYSTEM_COMMAND;
    uint8_t record[256] = {0U};
    const int record_len = ds_fixture_read_sub0_with_caps(&f, &strict, record, sizeof(record));
    assert_true(record_len > 10);
    assert_int_equal(record[0], 0x00U); /* DS_Command 读取 */
    assert_true(record_len != (int) f.ds.image_len);

    uint8_t err[2] = {0U};
    const int err_len = ds_fixture_write_sub0_with_caps(&f, &strict, f.ds.image,
                                                        (uint8_t) f.ds.image_len, err, sizeof(err));
    assert_int_equal(err_len, 2);
    assert_int_equal(err[0], 0x80U);
    assert_int_equal(err[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    /* 兼容位打开：读回整镜像字节，写被接受（空应答）。 */
    iolink_caps_t legacy = IOLINK_CAP_SYSTEM_COMMAND | IOLINK_CAP_DS_LEGACY_SUB0;
    uint8_t image[256] = {0U};
    const int image_len = ds_fixture_read_sub0_with_caps(&f, &legacy, image, sizeof(image));
    assert_int_equal(image_len, (int) f.ds.image_len);
    assert_memory_equal(image, f.ds.image, f.ds.image_len);

    uint8_t ack[2] = {0U};
    assert_int_equal(ds_fixture_write_sub0_with_caps(&f, &legacy, f.ds.image,
                                                     (uint8_t) f.ds.image_len, ack, sizeof(ack)),
                     0);
}

/* 0077：通过 Data Storage Index（Index_List）完成完整上传，校验
 * 大小、校验和与序列化记录。 */
static void test_ds_0077_full_upload_via_index_list(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0077");
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "APP1", "FLOW", "TANK");
    iolink_ds_clear_upload(&f.ds);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);

    uint8_t resp[2] = {0U};
    assert_int_equal(ds_fixture_ds_command(&f, 0x01U, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);

    /* Index_List 枚举完整的 DS 集合以及 0x0000 终止符。 */
    uint8_t list[32] = {0U};
    const int list_len = ds_fixture_isdu_read(&f, IOLINK_IDX_DATA_STORAGE, 5U, list, sizeof(list));
    assert_int_equal(list_len, 11);
    static const uint8_t expected_list[] = {0x00U, 0x18U, 0x00U, 0x00U, 0x19U, 0x00U,
                                            0x00U, 0x1AU, 0x00U, 0x00U, 0x00U};
    assert_memory_equal(list, expected_list, sizeof(expected_list));

    /* 子索引 0 是拼接的 DataStorageIndex 记录：
       DS_Command(1) + State_Property(1) + Size(4) + Checksum(4) + Index_List。 */
    uint8_t record[64] = {0U};
    const int record_len =
        ds_fixture_isdu_read(&f, IOLINK_IDX_DATA_STORAGE, 0U, record, sizeof(record));
    assert_int_equal(record_len, 10 + list_len);
    assert_int_equal(record[0], 0x00U); /* DS_Command 读取：未指定 */
    assert_int_equal(record[1], 0x02U); /* State_Property：上传 */
    const uint32_t size = ((uint32_t) record[2] << 24) | ((uint32_t) record[3] << 16) |
                          ((uint32_t) record[4] << 8) | (uint32_t) record[5];
    const uint32_t checksum = ((uint32_t) record[6] << 24) | ((uint32_t) record[7] << 16) |
                              ((uint32_t) record[8] << 8) | (uint32_t) record[9];
    assert_int_equal(size, f.ds.image_len);
    assert_int_equal(checksum, iolink_ds_calc_checksum(f.ds.image, f.ds.image_len));
    assert_memory_equal(&record[10], expected_list, sizeof(expected_list));
    assert_int_equal(ds_fixture_read_u32(&f, 3U), size);
    assert_int_equal(ds_fixture_read_u32(&f, 4U), checksum);

    /* 遍历每个 Index_List 条目，将实时值与其记录比较。 */
    for (int i = 0; i < (list_len - 2); i += 3) {
        const uint16_t index = (uint16_t) (((uint16_t) list[i] << 8) | (uint16_t) list[i + 1]);
        const uint8_t sub = list[i + 2];
        uint8_t value[33] = {0U};
        const int value_len = ds_fixture_isdu_read(&f, index, sub, value, sizeof(value));
        assert_true(value_len >= 0);
        const uint8_t* rec = NULL;
        size_t rec_len = 0U;
        assert_true(ds_image_find(f.ds.image, f.ds.image_len, index, sub, &rec, &rec_len));
        assert_int_equal((int) rec_len, value_len);
        assert_memory_equal(rec, value, (size_t) value_len);
    }
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "APP1");

    const uint32_t checksum_before_end = ds_fixture_read_u32(&f, 4U);
    assert_int_equal(ds_fixture_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
    assert_int_equal(ds_fixture_read_u32(&f, 4U), checksum_before_end);
}

/* 0078：通过 ParamDownloadStore 从集合 1 切换到集合 2，然后上传集合 2 并验证
 * Parameter_Checksum 随之改变。 */
static void test_ds_0078_upload_set2_after_store(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0078");
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "S1A", "S1B", "S1C");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;
    const uint32_t checksum_set1 = ds_fixture_read_u32(&f, 4U);

    uint8_t resp[2] = {0U};
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)),
                     0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);
    assert_int_equal(ds_fixture_isdu_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U,
                                           (const uint8_t*) "S2A", 3U, resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_fixture_isdu_write(&f, IOLINK_IDX_FUNCTION_TAG, 0U, (const uint8_t*) "S2B",
                                           3U, resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_fixture_isdu_write(&f, IOLINK_IDX_LOCATION_TAG, 0U, (const uint8_t*) "S2C",
                                           3U, resp, sizeof(resp)),
                     0);
    /* 块写入被暂存；在存储之前集合 1 保持生效。 */
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "S1A");
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)),
                     0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_true(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "S2A");
    const uint32_t checksum_set2 = ds_fixture_read_u32(&f, 4U);
    assert_int_not_equal(checksum_set2, checksum_set1);

    /* 上传集合 2 并验证镜像现在携带它。 */
    assert_int_equal(ds_fixture_ds_command(&f, 0x01U, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_true(f.ds.image_len > 0U);
    ds_assert_image_record(f.ds.image, f.ds.image_len, IOLINK_IDX_APPLICATION_TAG, "S2A");
    ds_assert_image_record(f.ds.image, f.ds.image_len, IOLINK_IDX_FUNCTION_TAG, "S2B");
    ds_assert_image_record(f.ds.image, f.ds.image_len, IOLINK_IDX_LOCATION_TAG, "S2C");
    assert_int_equal(ds_fixture_read_u32(&f, 3U), (uint32_t) f.ds.image_len);
    assert_int_equal(ds_fixture_read_u32(&f, 4U), checksum_set2);
    assert_int_equal(ds_fixture_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 0079：普通参数变更不得置起 DS_UPLOAD_REQ（无写入/
 * 超时路径），而未写入任何参数的 ParamDownloadStore 仍会置起。 */
static void test_ds_0079_no_write_store_timeout(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0079");
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "OLD", "OLD", "OLD");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;

    /* 在块下载之外写入集合 2。DS 状态必须保持空闲，且
       不得有上传通知挂起（1 s 超时已到）。 */
    ds_fixture_set_tags(&f, "NEW", "NEW", "NEW");
    iolink_ds_process(&f.ds);
    assert_false(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);

    /* 不写入任何参数的 ParamDownloadStart + ParamDownloadStore。 */
    uint8_t resp[2] = {0U};
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)),
                     0);
    assert_true(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "NEW");
}

/* 0080：本地参数变更（等价于示教/菜单操作）加上库的
 * 钩子必须请求并完成新集合的 DS 上传。 */
static void test_ds_0080_local_change_upload(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0080");
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "OLD", "OLD", "OLD");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;

    ds_fixture_set_tags(&f, "LOCAL", "TEACH", "PANEL");
    iolink_ds_request_upload(&f.ds);
    assert_true(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);

    uint8_t resp[2] = {0U};
    assert_int_equal(ds_fixture_ds_command(&f, 0x01U, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_true(f.ds.image_len > 0U);
    ds_assert_image_record(f.ds.image, f.ds.image_len, IOLINK_IDX_APPLICATION_TAG, "LOCAL");
    ds_assert_image_record(f.ds.image, f.ds.image_len, IOLINK_IDX_FUNCTION_TAG, "TEACH");
    ds_assert_image_record(f.ds.image, f.ds.image_len, IOLINK_IDX_LOCATION_TAG, "PANEL");
    assert_int_equal(ds_fixture_read_u32(&f, 4U),
                     iolink_ds_calc_checksum(f.ds.image, f.ds.image_len));
    assert_int_equal(ds_fixture_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 0081：ParamBreak 在三个不同时间点中止上传，且不应用
 * 数据、不改变参数校验和。 */
static void test_ds_0081_upload_abort_points(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0081");
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "SET1", "SET1", "SET1");
    iolink_ds_clear_upload(&f.ds);
    const uint32_t checksum = ds_fixture_read_u32(&f, 4U);
    uint8_t resp[2] = {0U};
    uint8_t img[IOLINK_DS_IMAGE_MAX] = {0U};

    for (int point = 0; point < 3; point++) {
        assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)),
                         0);
        if (point >= 1) {
            iolink_ds_process(&f.ds);
            assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
        }
        else {
            assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
        }
        if (point == 2) {
            /* 在上传数据被读取一次之后中止。 */
            assert_true(ds_fixture_isdu_read(&f, IOLINK_IDX_DATA_STORAGE, 0U, img, sizeof(img)) >
                        0);
        }
        assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_BREAK, resp, sizeof(resp)), 0);
        assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
        assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
        assert_int_equal(ds_fixture_read_u32(&f, 4U), checksum);
        ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "SET1");
    }
}

/* 0374：DS_UPLOAD_FLAG 是非易失的；DS_UploadEnd 会清除并持久化它。
 * 带存储的重新初始化用于模拟库级别的断电重启。 */
static void test_ds_0374_upload_flag_power_cycle(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0374");
    (void) state;
    iolink_ds_mock_reset();

    ds_fixture_t f;
    ds_fixture_init(&f, &g_ds_storage_mock);
    ds_fixture_set_tags(&f, "PC", "PC", "PC");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;

    /* 块下载之后的 ParamDownloadStore 会置起并持久化该标志。 */
    uint8_t resp[2] = {0U};
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_fixture_isdu_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U,
                                           (const uint8_t*) "PC2", 3U, resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)),
                     0);
    assert_true(f.ds.upload_event_pending);
    assert_true(f.ds.ds_upload_flag);

    /* 断电重启：新上下文从存储中读回该标志。 */
    ds_fixture_init(&f, &g_ds_storage_mock);
    assert_true(f.ds.ds_upload_flag);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);

    /* DS_UploadStart/UploadEnd 清除并持久化该标志。 */
    assert_int_equal(ds_fixture_ds_command(&f, 0x01U, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_fixture_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_false(f.ds.ds_upload_flag);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);

    /* 再次断电重启仍能看到已清除的标志。 */
    ds_fixture_init(&f, &g_ds_storage_mock);
    assert_false(f.ds.ds_upload_flag);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 持久化错误路径：无后端、写失败、坏记录、恢复/复位分支。 */
static int failing_write(void* user, uint32_t offset, const uint8_t* data, size_t len)
{
    (void) user;
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}

static int failing_read(void* user, uint32_t offset, uint8_t* data, size_t len)
{
    (void) user;
    (void) offset;
    (void) data;
    (void) len;
    return -1;
}

static const iolink_storage_api_t g_failing_storage = {
    .user = NULL, .read = failing_read, .write = failing_write};

static uint16_t g_reset_scalar;
static uint8_t g_reset_bytes[4];
static uint8_t g_reset_len;
static const uint8_t g_reset_default[] = {'A', 'B', 'C', 'D'};

/* 把一组记录写进 mock NVM（头 + payload，校验和覆盖 payload）。 */
static void ds_mock_write_image(const uint8_t* payload, size_t len)
{
    uint8_t* nvm = iolink_ds_mock_get_buf();
    const uint32_t checksum = iolink_storage_checksum(payload, len);

    nvm[0] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 24);
    nvm[1] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 16);
    nvm[2] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 8);
    nvm[3] = (uint8_t) IOLINK_STORAGE_MAGIC;
    nvm[4] = (uint8_t) (IOLINK_STORAGE_VERSION & 0xFFU);
    nvm[5] = (uint8_t) (IOLINK_STORAGE_VERSION >> 8);
    nvm[6] = (uint8_t) (len >> 8);
    nvm[7] = (uint8_t) (len & 0xFFU);
    nvm[8] = (uint8_t) (checksum >> 24);
    nvm[9] = (uint8_t) (checksum >> 16);
    nvm[10] = (uint8_t) (checksum >> 8);
    nvm[11] = (uint8_t) checksum;
    if (len > 0U) {
        (void) memcpy(&nvm[IOLINK_STORAGE_HDR_SIZE], payload, len);
    }
}

static void test_ds_persistence_error_paths(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_od_entry_t reset_entries[] = {
        IOLINK_OD_SCALAR_STORAGE_EX(0x01A1U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                    IOLINK_OD_WRITE_STORE,
                                    IOLINK_OD_STORAGE_DS | IOLINK_OD_STORAGE_PERSIST, 0U, 0xFFFFU,
                                    7U, "ResetScalar", &g_reset_scalar, NULL, NULL),
        {.index = 0x01A2U,
         .datatype = IOLINK_OD_DT_OCTET_STRING,
         .access = IOLINK_OD_ACCESS_RW,
         .write_semantics = IOLINK_OD_WRITE_VOLATILE,
         .storage = IOLINK_OD_STORAGE_DS | IOLINK_OD_STORAGE_PERSIST,
         .handler = IOLINK_OD_HANDLER_NONE,
         .backend = IOLINK_OD_BACKEND_STORAGE,
         .u.storage = {.value = g_reset_bytes, .str_len = &g_reset_len},
         .block_len = sizeof(g_reset_bytes),
         .block_default = g_reset_default,
         .name = "ResetBytes"},
    };
    static const iolink_od_app_t reset_app = {
        .entries = reset_entries,
        .count = sizeof(reset_entries) / sizeof(reset_entries[0]),
        .user = NULL,
    };

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_mock_reset();

    assert_int_equal(iolink_ds_save_persistent(NULL), -1);
    assert_int_equal(iolink_ds_load_persistent(NULL), -1);
    assert_int_equal(iolink_ds_factory_reset(NULL), -1);

    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_sources(&ds, NULL, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_save_persistent(&ds), -1); /* 无后端 */
    assert_int_equal(iolink_ds_load_persistent(&ds), -1);
    assert_int_equal(iolink_ds_factory_reset(&ds), 0); /* RAM-only：复位成功但不落盘 */

    iolink_ds_init(&ds, &g_failing_storage);
    iolink_ds_bind_sources(&ds, NULL, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_save_persistent(&ds), -1); /* 写失败 */
    assert_int_equal(iolink_ds_load_persistent(&ds), -1); /* 读失败 */
    assert_int_equal(iolink_ds_factory_reset(&ds), -1);

    /* 控制组：完整且校验正确的记录必须能加载。 */
    iolink_ds_mock_reset();
    const uint8_t good[] = {0x00U, 0x18U, 0x00U, 0x01U, 'A'};
    ds_mock_write_image(good, sizeof(good));
    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, NULL, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_load_persistent(&ds), 0);
    uint8_t applied[4] = {0U};
    const iolink_result_t applied_read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, applied, sizeof(applied));
    assert_int_equal(applied_read.len, 1U);
    assert_int_equal(applied[0], 'A');

    /* 记录长度越界：头/校验和正确，但记录声称 5 字节而只有 2 字节。 */
    test_env_reset_params(&g_test_env);
    uint8_t baseline[16] = {0U};
    const iolink_result_t baseline_read = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, baseline, sizeof(baseline));
    assert_int_equal(baseline_read.code, 0);
    iolink_ds_mock_reset();
    const uint8_t truncated[] = {0x00U, 0x18U, 0x00U, 0x05U, 0xAAU, 0xBBU};
    ds_mock_write_image(truncated, sizeof(truncated));

    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, NULL, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_load_persistent(&ds), -1);
    uint8_t tag_out[16] = {0U};
    const iolink_result_t after = iolink_params_ctx_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, tag_out, sizeof(tag_out));
    assert_int_equal(after.len, baseline_read.len);
    assert_memory_equal(tag_out, baseline, baseline_read.len);

    /* 应用写失败：值已应用，但保存镜像失败时 apply 返回 -1。 */
    iolink_ds_init(&ds, &g_failing_storage);
    iolink_ds_bind_sources(&ds, NULL, test_env_params(&g_test_env));
    const uint8_t image[] = {0x00U, 0x18U, 0x00U, 0x01U, 'X'};
    assert_int_equal(iolink_ds_apply_image(&ds, image, sizeof(image)), -1);
    assert_int_equal(
        iolink_params_ctx_read(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U,
                               tag_out, sizeof(tag_out))
            .len,
        1U);
    assert_int_equal(tag_out[0], 'X');

    /* 出厂复位：标量写入默认值，字节串写入 block_default，并重写镜像。 */
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, &reset_app, test_env_params(&g_test_env));
    g_reset_scalar = 0U;
    g_reset_len = 0U;
    (void) memset(g_reset_bytes, 0, sizeof(g_reset_bytes));
    assert_int_equal(iolink_ds_factory_reset(&ds), 0);
    assert_int_equal(g_reset_scalar, 7U);
    assert_int_equal(g_reset_len, sizeof(g_reset_default));
    assert_memory_equal(g_reset_bytes, g_reset_default, sizeof(g_reset_default));

    /* 复位后的镜像必须可重新加载（证明已落盘，而不是只改 RAM）。 */
    g_reset_scalar = 0U;
    g_reset_len = 0U;
    (void) memset(g_reset_bytes, 0, sizeof(g_reset_bytes));
    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, &reset_app, test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_load_persistent(&ds), 0);
    assert_int_equal(g_reset_scalar, 7U);
    assert_int_equal(g_reset_len, sizeof(g_reset_default));
    assert_memory_equal(g_reset_bytes, g_reset_default, sizeof(g_reset_default));
}

static void* g_validate_user;

static iolink_result_t reject_validate(void* user, const iolink_od_entry_t* entry,
                                       const uint8_t* data, size_t len)
{
    g_validate_user = user;
    (void) entry;
    (void) data;
    (void) len;
    return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
}

/* 恢复预校验复用表项 validate：第一条合法记录也必须在写入前被拦下。 */
static void test_ds_apply_precheck_rejects_before_write(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static int marker;
    static uint16_t value;
    static const iolink_od_entry_t entries[] = {
        IOLINK_OD_SCALAR_STORAGE_EX(0x01B0U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                    IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, 0U, 0xFFFFU, 0U,
                                    "Checked", &value, reject_validate, NULL),
    };
    static const iolink_od_app_t app = {
        .entries = entries,
        .count = sizeof(entries) / sizeof(entries[0]),
        .user = &marker,
    };
    /* 合法 FunctionTag 在前，被 validate 拒绝的 0x01B0 在后。 */
    const uint8_t image[] = {0x00U, 0x19U, 0x00U, 0x01U, 'X',  0x01U,
                             0xB0U, 0x00U, 0x02U, 0x12U, 0x34U};

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);
    iolink_ds_bind_sources(&ds, &app, test_env_params(&g_test_env));

    g_validate_user = NULL;
    assert_int_equal(iolink_ds_apply_image(&ds, image, sizeof(image)), -2);
    assert_int_equal(value, 0U);
    assert_ptr_equal(g_validate_user, &marker);
    /* 预校验在写入任何参数之前：前面的合法记录也不得生效。 */
    uint8_t out[4] = {0U};
    assert_int_equal(iolink_params_ctx_read(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG,
                                            0U, out, sizeof(out))
                         .len,
                     0U);
}

/* 设备启动时恢复参数镜像（device.c 的 load_persistent 调用点）。 */
static void test_ds_device_boot_restores_params(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_test_device_t reboot;

    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_non_null(ds);

    const uint8_t tag[] = "BOOT";
    assert_int_equal(iolink_params_ctx_write(&dev.ctx.params, IOLINK_IDX_APPLICATION_TAG, 0U, tag,
                                             sizeof(tag) - 1U)
                         .code,
                     0);
    assert_int_equal(iolink_ds_save_persistent(ds), 0);
    iolink_ds_refresh(ds);
    const uint32_t saved_checksum = iolink_ds_checksum(ds);

    /* 重启：新设备上下文从同一 NVM 恢复。 */
    assert_int_equal(iolink_test_device_init(&reboot, NULL, NULL), 0);
    uint8_t out[8] = {0U};
    const iolink_result_t restored = iolink_params_ctx_read(
        &reboot.ctx.params, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out));
    assert_int_equal(restored.len, sizeof(tag) - 1U);
    assert_memory_equal(out, tag, sizeof(tag) - 1U);
    assert_int_equal(iolink_ds_checksum(iolink_device_get_ds_ctx(&reboot.ctx)), saved_checksum);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ds_checksum),
        cmocka_unit_test(test_ds_storage_integration),
        cmocka_unit_test(test_ds_state_transitions),
        cmocka_unit_test(test_ds_auto_processed_by_device_pump),
        cmocka_unit_test(test_ds_commands_locked),
        cmocka_unit_test(test_ds_commands_unlocked),
        cmocka_unit_test(test_ds_param_round_trip),
        cmocka_unit_test(test_ds_bound_params_restore_device_context_only),
        cmocka_unit_test(test_ds_apply_rejects_truncated),
        cmocka_unit_test(test_ds_vendor_param_set_round_trip),
        cmocka_unit_test(test_ds_params_image_checksum_rejects_corruption),
        cmocka_unit_test(test_ds_entry_block_len_bounds_value),
        cmocka_unit_test(test_ds_download_end_error_classification),
        cmocka_unit_test(test_ds_download_recovery),
        cmocka_unit_test(test_ds_download_survives_persist_only_write),
        cmocka_unit_test(test_ds_persistence_error_paths),
        cmocka_unit_test(test_ds_apply_precheck_rejects_before_write),
        cmocka_unit_test(test_ds_device_boot_restores_params),
        cmocka_unit_test(test_ds_legacy_sub0_is_runtime_compat),
        cmocka_unit_test(test_ds_0077_full_upload_via_index_list),
        cmocka_unit_test(test_ds_0078_upload_set2_after_store),
        cmocka_unit_test(test_ds_0079_no_write_store_timeout),
        cmocka_unit_test(test_ds_0080_local_change_upload),
        cmocka_unit_test(test_ds_0081_upload_abort_points),
        cmocka_unit_test(test_ds_0374_upload_flag_power_cycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
