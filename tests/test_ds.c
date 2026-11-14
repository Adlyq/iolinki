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

static void test_ds_state_transitions(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0077");
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));

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
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));

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
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));

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
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));

    /* 配置已知的参数值。 */
    const char* app = "APP-TAG-1";
    const char* fun = "FLOW";
    const char* loc = "TANK-7";
    assert_int_equal(test_params_write(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U,
                                       (const uint8_t*) app, strlen(app))
                         .code,
                     0);
    assert_int_equal(test_params_write(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U,
                                       (const uint8_t*) fun, strlen(fun))
                         .code,
                     0);
    assert_int_equal(test_params_write(test_env_params(&g_test_env), IOLINK_IDX_LOCATION_TAG, 0U,
                                       (const uint8_t*) loc, strlen(loc))
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
    test_params_factory_reset(test_env_params(&g_test_env));
    uint8_t buf[40];
    const iolink_result_t cleared_app = test_params_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    const iolink_result_t cleared_fun = test_params_read(
        test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(cleared_app.code, 0);
    assert_int_equal(cleared_app.len, 0);
    assert_int_equal(cleared_fun.code, 0);
    assert_int_equal(cleared_fun.len, 0);

    /* 下载：应用备份镜像（恢复）。 */
    assert_int_equal(iolink_ds_apply_image(&ds, backup, backup_len), 0);
    assert_int_equal(ds.current_checksum, backup_cs);

    /* 参数被逐字节恢复。 */
    const iolink_result_t app_read = test_params_read(
        test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(app_read.code, 0);
    assert_int_equal(app_read.len, strlen(app));
    assert_memory_equal(buf, app, strlen(app));
    const iolink_result_t fun_read = test_params_read(
        test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(fun_read.code, 0);
    assert_int_equal(fun_read.len, strlen(fun));
    assert_memory_equal(buf, fun, strlen(fun));
    const iolink_result_t loc_read = test_params_read(
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
    iolink_test_params_t params_a;
    iolink_test_params_t params_b;
    iolink_ds_ctx_t ds_a;
    iolink_ds_ctx_t ds_b;
    static const uint8_t tag_a[] = "CtxA";
    static const uint8_t tag_b[] = "CtxB";
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    uint8_t out[16] = {0U};

    iolink_device_info_ctx_init(&info_a, NULL);
    iolink_device_info_ctx_init(&info_b, NULL);
    test_params_init(&params_a, &info_a);
    test_params_init(&params_b, &info_b);
    iolink_ds_init(&ds_a);
    iolink_ds_init(&ds_b);
    iolink_ds_bind_sources(&ds_a, test_params_od(&params_a));
    iolink_ds_bind_sources(&ds_b, test_params_od(&params_b));

    assert_int_equal(
        test_params_write(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, tag_a, sizeof(tag_a) - 1U)
            .code,
        0);
    assert_int_equal(
        test_params_write(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, tag_b, sizeof(tag_b) - 1U)
            .code,
        0);

    assert_true(iolink_ds_build_image(&ds_a) > 0);
    assert_true(ds_a.image_len <= sizeof(backup));
    memcpy(backup, ds_a.image, ds_a.image_len);

    test_params_factory_reset(&params_a);
    assert_int_equal(iolink_ds_apply_image(&ds_a, backup, ds_a.image_len), 0);

    const iolink_result_t read_a =
        test_params_read(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out));
    assert_int_equal(read_a.code, 0);
    assert_int_equal(read_a.len, sizeof(tag_a) - 1U);
    assert_memory_equal(out, tag_a, sizeof(tag_a) - 1U);

    memset(out, 0, sizeof(out));
    const iolink_result_t read_b =
        test_params_read(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out));
    assert_int_equal(read_b.code, 0);
    assert_int_equal(read_b.len, sizeof(tag_b) - 1U);
    assert_memory_equal(out, tag_b, sizeof(tag_b) - 1U);
}

static void test_ds_apply_rejects_truncated(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));
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
    iolink_od_entry_t merged_storage[64];
    iolink_od_app_t merged;
    static const uint8_t vend[] = {0xDEU, 0xADU, 0xBEU, 0xEFU};

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_init(&ds);
    merged = test_od_with_tags(&k_vendor_app, test_params_od(test_env_params(&g_test_env)),
                               merged_storage, 64U);
    iolink_ds_bind_sources(&ds, &merged);

    assert_int_equal(test_params_write(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U,
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
    test_params_factory_reset(test_env_params(&g_test_env));
    assert_int_equal(iolink_ds_apply_image(&ds, backup, len), 0);

    assert_int_equal(g_vendor_param_len, sizeof(vend));
    assert_memory_equal(g_vendor_param, vend, sizeof(vend));
    uint8_t tag[8] = {0U};
    const iolink_result_t tag_read = test_params_read(
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
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));

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
    const iolink_result_t applied = test_params_read(
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
    iolink_od_entry_t merged_storage[64];
    iolink_od_app_t merged;

    /* 格式错误的暂存镜像 -> -1 -> PAR_SETINVALID。 */
    iolink_ds_init(&ds);
    iolink_ds_bind_sources(&ds, test_params_od(test_env_params(&g_test_env)));
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t bad[] = {0x00U, 0x18U, 0x00U, 0x05U, 0xAAU}; /* 声称 5 字节，实际为 1 */
    (void) memcpy(ds.image, bad, sizeof(bad));
    ds.image_len = sizeof(bad);
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -5);

    /* 已知参数被拒绝 -> -2 -> PAR_SETINCONSIST。 */
    iolink_ds_init(&ds);
    merged = test_od_with_tags(&k_reject_app, test_params_od(test_env_params(&g_test_env)),
                               merged_storage, 64U);
    iolink_ds_bind_sources(&ds, &merged);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t img[] = {0x00U, 0x50U, 0x00U, 0x03U, 'A', 'B', 'C'};
    (void) memcpy(ds.image, img, sizeof(img));
    ds.image_len = sizeof(img);
    assert_int_equal(iolink_ds_handle_param_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -4);
}

/* 表项的 block_len 限制存入镜像的值。 */
static void test_ds_entry_block_len_bounds_value(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_od_entry_t merged_storage[64];
    iolink_od_app_t merged;
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

    iolink_ds_init(&ds);
    merged = test_od_with_tags(&bounded_app, test_params_od(test_env_params(&g_test_env)),
                               merged_storage, 64U);
    iolink_ds_bind_sources(&ds, &merged);
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
/* 数据存储一致性用例 0077-0081 的共享 fixture。                             */
/* ------------------------------------------------------------------------- */

typedef struct
{
    iolink_device_info_ctx_t info;
    iolink_test_params_t params;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
} ds_fixture_t;

static void ds_fixture_init(ds_fixture_t* f)
{
    iolink_device_info_ctx_init(&f->info, NULL);
    test_params_init(&f->params, &f->info);
    iolink_ds_init(&f->ds);
    iolink_ds_bind_sources(&f->ds, test_params_od(&f->params));
    iolink_events_init(&f->events);
}

static void ds_fixture_set_tags(ds_fixture_t* f, const char* app, const char* fun, const char* loc)
{
    assert_int_equal(test_params_write(&f->params, IOLINK_IDX_APPLICATION_TAG, 0U,
                                       (const uint8_t*) app, strlen(app))
                         .code,
                     0);
    assert_int_equal(test_params_write(&f->params, IOLINK_IDX_FUNCTION_TAG, 0U,
                                       (const uint8_t*) fun, strlen(fun))
                         .code,
                     0);
    assert_int_equal(test_params_write(&f->params, IOLINK_IDX_LOCATION_TAG, 0U,
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
    ctx.od = test_params_od(&f->params);
    ctx.device_info = &f->info;
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
    ctx.od = test_params_od(&f->params);
    ctx.device_info = &f->info;
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
    const iolink_result_t read_result = test_params_read(&f->params, index, 0U, buf, sizeof(buf));
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
    ctx.od = test_params_od(&f->params);
    ctx.device_info = &f->info;
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
    ctx.od = test_params_od(&f->params);
    ctx.device_info = &f->info;
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
    ds_fixture_init(&f);
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
    ds_fixture_init(&f);
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
    ds_fixture_init(&f);
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
    ds_fixture_init(&f);
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
    ds_fixture_init(&f);
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
    ds_fixture_init(&f);
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
    iolink_od_entry_t merged_storage[64];
    iolink_od_app_t merged;
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
    iolink_ds_init(&ds);
    merged =
        test_od_with_tags(&app, test_params_od(test_env_params(&g_test_env)), merged_storage, 64U);
    iolink_ds_bind_sources(&ds, &merged);

    g_validate_user = NULL;
    assert_int_equal(iolink_ds_apply_image(&ds, image, sizeof(image)), -2);
    assert_int_equal(value, 0U);
    assert_ptr_equal(g_validate_user, &marker);
    /* 预校验在写入任何参数之前：前面的合法记录也不得生效。 */
    uint8_t out[4] = {0U};
    assert_int_equal(test_params_read(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U,
                                      out, sizeof(out))
                         .len,
                     0U);
}

static int g_flag_persist_calls;
static bool g_flag_persist_last;

static void flag_persist_cb(void* user, bool flag)
{
    (void) user;
    g_flag_persist_calls++;
    g_flag_persist_last = flag;
}

static void test_ds_flag_persist_callback(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds);
    iolink_ds_set_flag_persist(&ds, flag_persist_cb, NULL);
    g_flag_persist_calls = 0;
    g_flag_persist_last = false;

    iolink_ds_request_upload(&ds);
    assert_int_equal(g_flag_persist_calls, 1);
    assert_true(g_flag_persist_last);

    iolink_ds_clear_upload(&ds);
    assert_int_equal(g_flag_persist_calls, 2);
    assert_false(g_flag_persist_last);

    /* 已经是 0 时再次清除不重复通知。 */
    iolink_ds_clear_upload(&ds);
    assert_int_equal(g_flag_persist_calls, 2);
}

/* DS_UPLOAD_FLAG 由应用持久化：库只通知变化，应用把保存的值写回新上下文。 */
static void test_ds_0374_upload_flag_non_volatile(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0374");
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds);
    iolink_ds_set_flag_persist(&ds, flag_persist_cb, NULL);
    g_flag_persist_calls = 0;
    g_flag_persist_last = false;

    iolink_ds_request_upload(&ds);
    assert_int_equal(g_flag_persist_calls, 1);
    assert_true(g_flag_persist_last);

    /* 掉电：库不拥有 NVM，应用把保存的值写回新上下文。 */
    iolink_ds_ctx_t restored;
    iolink_ds_init(&restored);
    assert_int_equal(iolink_ds_state_property(&restored) & 0x80U, 0U);
    restored.ds_upload_flag = g_flag_persist_last;
    assert_int_equal(iolink_ds_state_property(&restored) & 0x80U, 0x80U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ds_checksum),
        cmocka_unit_test(test_ds_state_transitions),
        cmocka_unit_test(test_ds_auto_processed_by_device_pump),
        cmocka_unit_test(test_ds_commands_locked),
        cmocka_unit_test(test_ds_commands_unlocked),
        cmocka_unit_test(test_ds_param_round_trip),
        cmocka_unit_test(test_ds_bound_params_restore_device_context_only),
        cmocka_unit_test(test_ds_apply_rejects_truncated),
        cmocka_unit_test(test_ds_vendor_param_set_round_trip),
        cmocka_unit_test(test_ds_entry_block_len_bounds_value),
        cmocka_unit_test(test_ds_download_end_error_classification),
        cmocka_unit_test(test_ds_download_recovery),
        cmocka_unit_test(test_ds_apply_precheck_rejects_before_write),
        cmocka_unit_test(test_ds_legacy_sub0_is_runtime_compat),
        cmocka_unit_test(test_ds_0077_full_upload_via_index_list),
        cmocka_unit_test(test_ds_0078_upload_set2_after_store),
        cmocka_unit_test(test_ds_0079_no_write_store_timeout),
        cmocka_unit_test(test_ds_0080_local_change_upload),
        cmocka_unit_test(test_ds_0081_upload_abort_points),
        cmocka_unit_test(test_ds_flag_persist_callback),
        cmocka_unit_test(test_ds_0374_upload_flag_non_volatile),
    };
    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
