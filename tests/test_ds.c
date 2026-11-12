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
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. 触发 Upload（主站为 0x0000——空） */
    iolink_ds_check(&ds, 0x0000);
    iolink_ds_process(&ds); /* Req -> Uploading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    iolink_ds_process(&ds); /* 保持 Uploading 直到 UploadEnd */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_UPLOAD_END, 0), 0);
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
    assert_int_equal(iolink_ds_start_upload(&dev.ctx.dll.main.ds), 0);
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
    int ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, -2); /* 访问被拒绝 */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. 在锁定状态下尝试 Upload Start（读）——通常应通过？
       规范：访问锁通常适用于参数写入（Index 2）。
       Data Storage 上传是从设备读取，因此或许允许？
       我们实现的代码只对 Download（写）检查锁定。
    */
    ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_UPLOAD_START, IOLINK_LOCK_DS);
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
    int ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    /* 2. 结束 Download */
    ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0);
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
    assert_int_equal(iolink_params_ctx_set(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG,
                                           0U, (const uint8_t*) app, strlen(app), true),
                     0);
    assert_int_equal(iolink_params_ctx_set(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG,
                                           0U, (const uint8_t*) fun, strlen(fun), true),
                     0);
    assert_int_equal(iolink_params_ctx_set(test_env_params(&g_test_env), IOLINK_IDX_LOCATION_TAG,
                                           0U, (const uint8_t*) loc, strlen(loc), true),
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
    assert_int_equal(iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG,
                                           0U, buf, sizeof(buf)),
                     0);
    assert_int_equal(iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG,
                                           0U, buf, sizeof(buf)),
                     0);

    /* 下载：应用备份镜像（恢复）。 */
    assert_int_equal(iolink_ds_apply_image(&ds, backup, backup_len), 0);
    assert_int_equal(ds.current_checksum, backup_cs);

    /* 参数被逐字节恢复。 */
    int len = iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG, 0U,
                                    buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(app));
    assert_memory_equal(buf, app, strlen(app));
    len = iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_FUNCTION_TAG, 0U, buf,
                                sizeof(buf));
    assert_int_equal(len, (int) strlen(fun));
    assert_memory_equal(buf, fun, strlen(fun));
    len = iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_LOCATION_TAG, 0U, buf,
                                sizeof(buf));
    assert_int_equal(len, (int) strlen(loc));
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
    ds_a.params_ctx = test_env_params(&g_test_env);
    iolink_ds_init(&ds_b, NULL);
    ds_b.params_ctx = test_env_params(&g_test_env);
    iolink_ds_bind_params(&ds_a, &params_a);
    iolink_ds_bind_params(&ds_b, &params_b);

    assert_int_equal(iolink_params_ctx_set(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, tag_a,
                                           sizeof(tag_a) - 1U, true),
                     0);
    assert_int_equal(iolink_params_ctx_set(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, tag_b,
                                           sizeof(tag_b) - 1U, true),
                     0);

    assert_true(iolink_ds_build_image(&ds_a) > 0);
    assert_true(ds_a.image_len <= sizeof(backup));
    memcpy(backup, ds_a.image, ds_a.image_len);

    iolink_params_ctx_factory_reset(&params_a);
    assert_int_equal(iolink_ds_apply_image(&ds_a, backup, ds_a.image_len), 0);

    assert_int_equal(
        iolink_params_ctx_get(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out)),
        (int) (sizeof(tag_a) - 1U));
    assert_memory_equal(out, tag_a, sizeof(tag_a) - 1U);

    memset(out, 0, sizeof(out));
    assert_int_equal(
        iolink_params_ctx_get(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out)),
        (int) (sizeof(tag_b) - 1U));
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

/* 由静态缓冲区支撑的厂商参数（0x0050），通过绑定的
 * 访问器进行验证，以证明厂商参数可以加入 DS 集合。 */
#define VENDOR_PARAM_IDX 0x0050U
static uint8_t g_vendor_param[8];
static size_t g_vendor_param_len;

static iolink_result_t vendor_ds_read(void* user, uint16_t index, uint8_t subindex, uint8_t* out,
                                      size_t cap)
{
    (void) user;
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        return iolink_params_ctx_read(test_env_params(&g_test_env), index, subindex, out, cap);
    }
    if ((index == VENDOR_PARAM_IDX) && (subindex == 0U)) {
        if (g_vendor_param_len > cap) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
        (void) memcpy(out, g_vendor_param, g_vendor_param_len);
        return iolink_result_ok((uint16_t) g_vendor_param_len);
    }
    return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
}

static iolink_result_t vendor_ds_write(void* user, uint16_t index, uint8_t subindex,
                                       const uint8_t* data, size_t len, bool persist)
{
    (void) user;
    (void) persist;
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        return iolink_params_ctx_write(test_env_params(&g_test_env), index, subindex, data, len,
                                       true);
    }
    if ((index == VENDOR_PARAM_IDX) && (subindex == 0U)) {
        if (len > sizeof(g_vendor_param)) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
        (void) memcpy(g_vendor_param, data, len);
        g_vendor_param_len = len;
        return iolink_result_ok(0U);
    }
    return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
}

static void test_ds_vendor_param_set_round_trip(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_ds_param_desc_t set[] = {
        {IOLINK_IDX_APPLICATION_TAG, 0U, 0U},
        {VENDOR_PARAM_IDX, 0U, 0U},
    };
    static const uint8_t vend[] = {0xDEU, 0xADU, 0xBEU, 0xEFU};

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    iolink_ds_bind_param_access(&ds, vendor_ds_read, vendor_ds_write, NULL, set,
                                sizeof(set) / sizeof(set[0]));

    assert_int_equal(iolink_params_ctx_set(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG,
                                           0U, (const uint8_t*) "TAG", 3U, true),
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
    assert_int_equal(iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG,
                                           0U, tag, sizeof(tag)),
                     3);
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
    assert_int_equal(iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG,
                                           0U, buf, sizeof(buf)),
                     1);
    assert_int_equal(buf[0], 'X');

    /* 设备校验和反映实际状态（未知索引被丢弃），因此
       它与主站下载镜像的校验和不同，且存储的镜像
       内部一致。 */
    assert_true(iolink_ds_verify(&ds));
    assert_int_not_equal(ds.current_checksum, downloaded_cs);

    /* 针对过时主站校验和的一致性检查会重新请求下载。 */
    iolink_ds_check(&ds, downloaded_cs);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
}

static iolink_result_t ds_rejecting_write(void* user, uint16_t index, uint8_t subindex,
                                          const uint8_t* data, size_t len, bool persist)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return iolink_result_error(IOLINK_RESULT_INVALID);
}

static iolink_result_t ds_zero_read(void* user, uint16_t index, uint8_t subindex, uint8_t* out,
                                    size_t cap)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) out;
    (void) cap;
    return iolink_result_ok(0U);
}

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
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -5);

    /* 已知参数被拒绝 -> -2 -> PAR_SETINCONSIST。 */
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 0U}};
    iolink_ds_bind_param_access(&ds, ds_zero_read, ds_rejecting_write, NULL, set, 1U);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t img[] = {0x00U, 0x18U, 0x00U, 0x03U, 'A', 'B', 'C'};
    (void) memcpy(ds.image, img, sizeof(img));
    ds.image_len = sizeof(img);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -4);
}

/* 损坏的 DS 镜像（位翻转/写撕裂）必须在加载时被丢弃。 */
static void test_ds_image_checksum_rejects_corruption(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);
    ds.params_ctx = test_env_params(&g_test_env);

    /* 通过参数变更请求持久化一个有效镜像。 */
    iolink_ds_request_upload(&ds);
    assert_true(ds.image_len > 0U);

    /* 翻转存储镜像中的一个字节。 */
    uint8_t* buf = iolink_ds_mock_get_buf();
    buf[IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_HDR_SIZE] ^= 0xFFU;

    /* 重新加载：校验和不匹配时必须丢弃该镜像。 */
    iolink_ds_init(&ds, &g_ds_storage_mock);
    ds.params_ctx = test_env_params(&g_test_env);
    assert_int_equal(ds.image_len, 0U);
}

static iolink_result_t ds_param_read_mock(void* user, uint16_t index, uint8_t subindex,
                                          uint8_t* out, size_t cap)
{
    static const uint8_t value[] = "ABCDEFGH";
    (void) user;
    (void) index;
    (void) subindex;
    size_t n = (sizeof(value) - 1U < cap) ? sizeof(value) - 1U : cap;
    (void) memcpy(out, value, n);
    return iolink_result_ok((uint16_t) n);
}

static iolink_result_t ds_param_write_mock(void* user, uint16_t index, uint8_t subindex,
                                           const uint8_t* data, size_t len, bool persist)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return iolink_result_ok(0U);
}

/* 传入 NULL 访问器时必须保留设备绑定的访问器与用户指针。 */
static void test_ds_bind_null_keeps_accessors(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    int marker = 0;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    iolink_ds_bind_param_access(&ds, ds_param_read_mock, ds_param_write_mock, &marker, NULL, 0U);
    assert_ptr_equal(ds.param_user, &marker);

    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 0U}};
    iolink_ds_bind_param_access(&ds, NULL, NULL, NULL, set, 1U);
    assert_ptr_equal(ds.param_user, &marker);
    assert_ptr_equal(ds.read_param, ds_param_read_mock);
    assert_ptr_equal(ds.write_param, ds_param_write_mock);
    assert_int_equal(ds.param_count, 1U);
}

/* set_param_desc 只替换描述符集合。 */
static void test_ds_set_param_desc_keeps_accessors(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    int marker = 0;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    iolink_ds_bind_param_access(&ds, ds_param_read_mock, ds_param_write_mock, &marker, NULL, 0U);

    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 0U}};
    iolink_ds_set_param_desc(&ds, set, 1U);
    assert_ptr_equal(ds.param_user, &marker);
    assert_ptr_equal(ds.read_param, ds_param_read_mock);
    assert_int_equal(ds.param_count, 1U);
}

/* 描述符的 max_len 限制存入镜像的值。 */
static void test_ds_desc_max_len_bounds_value(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    ds.params_ctx = test_env_params(&g_test_env);
    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 4U}};
    iolink_ds_bind_param_access(&ds, ds_param_read_mock, ds_param_write_mock, NULL, set, 1U);

    assert_true(iolink_ds_build_image(&ds) > 0);
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &len);
    assert_non_null(img);
    assert_int_equal(img[3], 4U); /* 长度字节被限制为 max_len */
    assert_memory_equal(&img[4], "ABCD", 4U);
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
    if (storage != NULL) {
        iolink_params_bind_storage(&f->params, storage);
    }
    iolink_ds_init(&f->ds, storage);
    iolink_ds_bind_params(&f->ds, &f->params);
    iolink_events_init(&f->events);
}

static void ds_fixture_set_tags(ds_fixture_t* f, const char* app, const char* fun, const char* loc)
{
    assert_int_equal(iolink_params_ctx_set(&f->params, IOLINK_IDX_APPLICATION_TAG, 0U,
                                           (const uint8_t*) app, strlen(app), true),
                     0);
    assert_int_equal(iolink_params_ctx_set(&f->params, IOLINK_IDX_FUNCTION_TAG, 0U,
                                           (const uint8_t*) fun, strlen(fun), true),
                     0);
    assert_int_equal(iolink_params_ctx_set(&f->params, IOLINK_IDX_LOCATION_TAG, 0U,
                                           (const uint8_t*) loc, strlen(loc), true),
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
    assert_int_equal(iolink_params_ctx_get(&f->params, index, 0U, buf, sizeof(buf)),
                     (int) expected_len);
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
        cmocka_unit_test(test_ds_image_checksum_rejects_corruption),
        cmocka_unit_test(test_ds_bind_null_keeps_accessors),
        cmocka_unit_test(test_ds_set_param_desc_keeps_accessors),
        cmocka_unit_test(test_ds_desc_max_len_bounds_value),
        cmocka_unit_test(test_ds_download_end_error_classification),
        cmocka_unit_test(test_ds_download_recovery),
        cmocka_unit_test(test_ds_0077_full_upload_via_index_list),
        cmocka_unit_test(test_ds_0078_upload_set2_after_store),
        cmocka_unit_test(test_ds_0079_no_write_store_timeout),
        cmocka_unit_test(test_ds_0080_local_change_upload),
        cmocka_unit_test(test_ds_0081_upload_abort_points),
        cmocka_unit_test(test_ds_0374_upload_flag_power_cycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
