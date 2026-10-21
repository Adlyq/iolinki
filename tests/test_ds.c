/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ds.c
 * @brief Unit tests for Data Storage (DS)
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
#include "iolinki/protocol.h"
#include "test_helpers.h"

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

    uint8_t write_data[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t read_buf[4] = {0};

    assert_int_equal(ds.storage->write(ds.storage->user, 0, write_data, 4), 0);
    assert_int_equal(ds.storage->read(ds.storage->user, 0, read_buf, 4), 0);
    assert_memory_equal(read_buf, write_data, 4);
}

static void test_ds_state_transitions(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);

    /* 1. Trigger Download (Master has newer data) */
    iolink_ds_check(&ds, 0xABCD);
    iolink_ds_process(&ds); /* Req -> Downloading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    iolink_ds_process(&ds); /* Downloading -> Idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. Trigger Upload (Master has 0x0000 - empty) */
    iolink_ds_check(&ds, 0x0000);
    iolink_ds_process(&ds); /* Req -> Uploading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    iolink_ds_process(&ds); /* Uploading -> Idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static void test_ds_commands_locked(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);

    /* 1. Try Download Start (Write) while locked */
    int ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, -2); /* Access Denied */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. Try Upload Start (Read) while locked - Should usually pass?
       Spec: Access Locks usually apply to Parameter Write (Index 2).
       Data Storage Upload is reading from device, so maybe allowed?
       The code we implemented only checks lock for Download (Write).
    */
    ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_UPLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, 0); /* Success */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
}

static void test_ds_commands_unlocked(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);

    /* 1. Download Start (Write) unlocked */
    int ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    /* 2. End Download */
    ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static void test_ds_param_round_trip(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);

    /* Configure known parameter values. */
    const char* app = "APP-TAG-1";
    const char* fun = "FLOW";
    const char* loc = "TANK-7";
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) app, strlen(app), true),
        0);
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_FUNCTION_TAG, 0U, (const uint8_t*) fun, strlen(fun), true), 0);
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_LOCATION_TAG, 0U, (const uint8_t*) loc, strlen(loc), true), 0);

    /* Upload: serialize current parameters into the DS image (backup). */
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

    /* Wipe the device parameters. */
    iolink_params_factory_reset();
    uint8_t buf[40];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 0);
    assert_int_equal(iolink_params_get(IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf)), 0);

    /* Download: apply the backup image (restore). */
    assert_int_equal(iolink_ds_apply_image(&ds, backup, backup_len), 0);
    assert_int_equal(ds.current_checksum, backup_cs);

    /* Parameters are restored byte-for-byte. */
    int len = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(app));
    assert_memory_equal(buf, app, strlen(app));
    len = iolink_params_get(IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(fun));
    assert_memory_equal(buf, fun, strlen(fun));
    len = iolink_params_get(IOLINK_IDX_LOCATION_TAG, 0U, buf, sizeof(buf));
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
    iolink_ds_init(&ds_b, NULL);
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
    /* Header claims a 5-byte value but only 2 data bytes follow -> rejected. */
    uint8_t bad[] = {0x00, 0x18, 0x00, 0x05, 0xAA, 0xBB};
    assert_int_equal(iolink_ds_apply_image(&ds, bad, sizeof(bad)), -1);
}

/* A vendor parameter (0x0050) backed by a static buffer, exercised through a
 * bound accessor to prove vendor parameters can join the DS set. */
#define VENDOR_PARAM_IDX 0x0050U
static uint8_t g_vendor_param[8];
static size_t g_vendor_param_len;

static int vendor_ds_read(void* user, uint16_t index, uint8_t subindex, uint8_t* out, size_t cap)
{
    (void) user;
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        return iolink_params_get(index, subindex, out, cap);
    }
    if ((index == VENDOR_PARAM_IDX) && (subindex == 0U)) {
        if (g_vendor_param_len > cap) {
            return -1;
        }
        (void) memcpy(out, g_vendor_param, g_vendor_param_len);
        return (int) g_vendor_param_len;
    }
    return -1;
}

static int vendor_ds_write(void* user, uint16_t index, uint8_t subindex, const uint8_t* data,
                           size_t len, bool persist)
{
    (void) user;
    (void) persist;
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        return iolink_params_set(index, subindex, data, len, true);
    }
    if ((index == VENDOR_PARAM_IDX) && (subindex == 0U)) {
        if (len > sizeof(g_vendor_param)) {
            return -1;
        }
        (void) memcpy(g_vendor_param, data, len);
        g_vendor_param_len = len;
        return 0;
    }
    return -1;
}

static void test_ds_vendor_param_set_round_trip(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_ds_param_desc_t set[] = {
        {IOLINK_IDX_APPLICATION_TAG, 0U},
        {VENDOR_PARAM_IDX, 0U},
    };
    static const uint8_t vend[] = {0xDEU, 0xADU, 0xBEU, 0xEFU};

    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_param_access(&ds, vendor_ds_read, vendor_ds_write, NULL, set,
                                sizeof(set) / sizeof(set[0]));

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "TAG", 3U, true), 0);
    g_vendor_param_len = sizeof(vend);
    (void) memcpy(g_vendor_param, vend, sizeof(vend));

    /* Upload: the image must now contain the standard tag AND the vendor record. */
    assert_true(iolink_ds_build_image(&ds) > 0);
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &len);
    assert_non_null(img);
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    assert_true(len <= sizeof(backup));
    (void) memcpy(backup, img, len);

    /* Wipe both sources and restore from the image. */
    g_vendor_param_len = 0U;
    (void) memset(g_vendor_param, 0, sizeof(g_vendor_param));
    iolink_params_factory_reset();
    assert_int_equal(iolink_ds_apply_image(&ds, backup, len), 0);

    assert_int_equal(g_vendor_param_len, sizeof(vend));
    assert_memory_equal(g_vendor_param, vend, sizeof(vend));
    uint8_t tag[8] = {0U};
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, tag, sizeof(tag)), 3);
    assert_memory_equal(tag, "TAG", 3U);
}

static void test_ds_download_recovery(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);

    /* Craft an image with a known parameter (ApplicationTag 0x18 = "X") plus an
       UNKNOWN index (0x9999) the device cannot store. */
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

    /* Known parameter was applied. */
    uint8_t buf[8];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 'X');

    /* Device checksum reflects ACTUAL state (unknown index dropped), so it
       differs from the Master's downloaded image checksum, and the stored image
       is internally consistent. */
    assert_true(iolink_ds_verify(&ds));
    assert_int_not_equal(ds.current_checksum, downloaded_cs);

    /* A consistency check against the stale Master checksum re-requests download. */
    iolink_ds_check(&ds, downloaded_cs);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
}

static int ds_rejecting_write(void* user, uint16_t index, uint8_t subindex,
                              const uint8_t* data, size_t len, bool persist)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return -1;
}

static int ds_zero_read(void* user, uint16_t index, uint8_t subindex, uint8_t* out, size_t cap)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) out;
    (void) cap;
    return 0;
}

/* ParamDownloadEnd distinguishes a malformed image (PAR_SETINVALID 0x8040) from
 * a known parameter the device rejected (PAR_SETINCONSIST 0x8041). */
static void test_ds_download_end_error_classification(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;

    /* Malformed staged image -> -1 -> PAR_SETINVALID. */
    iolink_ds_init(&ds, NULL);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t bad[] = {0x00U, 0x18U, 0x00U, 0x05U, 0xAAU}; /* claims 5 bytes, has 1 */
    (void) memcpy(ds.image, bad, sizeof(bad));
    ds.image_len = sizeof(bad);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -5);

    /* Known parameter rejected -> -2 -> PAR_SETINCONSIST. */
    iolink_ds_init(&ds, NULL);
    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U}};
    iolink_ds_bind_param_access(&ds, ds_zero_read, ds_rejecting_write, NULL, set, 1U);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t img[] = {0x00U, 0x18U, 0x00U, 0x03U, 'A', 'B', 'C'};
    (void) memcpy(ds.image, img, sizeof(img));
    ds.image_len = sizeof(img);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -4);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ds_checksum),
        cmocka_unit_test(test_ds_storage_integration),
        cmocka_unit_test(test_ds_state_transitions),
        cmocka_unit_test(test_ds_commands_locked),
        cmocka_unit_test(test_ds_commands_unlocked),
        cmocka_unit_test(test_ds_param_round_trip),
        cmocka_unit_test(test_ds_bound_params_restore_device_context_only),
        cmocka_unit_test(test_ds_apply_rejects_truncated),
        cmocka_unit_test(test_ds_vendor_param_set_round_trip),
        cmocka_unit_test(test_ds_download_end_error_classification),
        cmocka_unit_test(test_ds_download_recovery),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
