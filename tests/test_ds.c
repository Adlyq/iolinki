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
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

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

    /* 1. Trigger Download (Master has newer data) */
    iolink_ds_check(&ds, 0xABCD);
    iolink_ds_process(&ds); /* Req -> Downloading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    iolink_ds_process(&ds); /* remains Downloading until DownloadEnd */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. Trigger Upload (Master has 0x0000 - empty) */
    iolink_ds_check(&ds, 0x0000);
    iolink_ds_process(&ds); /* Req -> Uploading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    iolink_ds_process(&ds); /* remains Uploading until UploadEnd */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_UPLOAD_END, 0), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

/* The device pump services the DS state machine while the ISDU transport is
 * idle: no manual iolink_ds_process() call is needed. */
static void test_ds_auto_processed_by_device_pump(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    /* A busy ISDU transport gates the DS service. */
    assert_int_equal(iolink_ds_start_upload(&dev.ctx.dll.ds), 0);
    dev.ctx.dll.isdu.state = ISDU_STATE_SERVICE_EXECUTE;
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    /* Once ISDU is idle the pump advances the DS state machine. */
    dev.ctx.dll.isdu.state = ISDU_STATE_IDLE;
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.ds.state, IOLINK_DS_STATE_UPLOADING);
}

static void test_ds_commands_locked(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0156");
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
        {IOLINK_IDX_APPLICATION_TAG, 0U, 0U},
        {VENDOR_PARAM_IDX, 0U, 0U},
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

static int ds_rejecting_write(void* user, uint16_t index, uint8_t subindex, const uint8_t* data,
                              size_t len, bool persist)
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
    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 0U}};
    iolink_ds_bind_param_access(&ds, ds_zero_read, ds_rejecting_write, NULL, set, 1U);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    uint8_t img[] = {0x00U, 0x18U, 0x00U, 0x03U, 'A', 'B', 'C'};
    (void) memcpy(ds.image, img, sizeof(img));
    ds.image_len = sizeof(img);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -4);
}

/* A corrupt DS image (bit flip / torn write) must be discarded on load. */
static void test_ds_image_checksum_rejects_corruption(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;

    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);

    /* Persist a valid image via a parameter change request. */
    iolink_ds_request_upload(&ds);
    assert_true(ds.image_len > 0U);

    /* Flip one byte of the stored image. */
    uint8_t* buf = iolink_ds_mock_get_buf();
    buf[IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_HDR_SIZE] ^= 0xFFU;

    /* Reload: the checksum mismatch must discard the image. */
    iolink_ds_init(&ds, &g_ds_storage_mock);
    assert_int_equal(ds.image_len, 0U);
}

static int ds_param_read_mock(void* user, uint16_t index, uint8_t subindex, uint8_t* out,
                              size_t cap)
{
    static const uint8_t value[] = "ABCDEFGH";
    (void) user;
    (void) index;
    (void) subindex;
    size_t n = (sizeof(value) - 1U < cap) ? sizeof(value) - 1U : cap;
    (void) memcpy(out, value, n);
    return (int) n;
}

static int ds_param_write_mock(void* user, uint16_t index, uint8_t subindex, const uint8_t* data,
                               size_t len, bool persist)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return 0;
}

/* Passing NULL accessors must keep the device-bound accessors and user pointer. */
static void test_ds_bind_null_keeps_accessors(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    int marker = 0;
    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_param_access(&ds, ds_param_read_mock, ds_param_write_mock, &marker, NULL, 0U);
    assert_ptr_equal(ds.param_user, &marker);

    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 0U}};
    iolink_ds_bind_param_access(&ds, NULL, NULL, NULL, set, 1U);
    assert_ptr_equal(ds.param_user, &marker);
    assert_ptr_equal(ds.read_param, ds_param_read_mock);
    assert_ptr_equal(ds.write_param, ds_param_write_mock);
    assert_int_equal(ds.param_count, 1U);
}

/* set_param_desc only replaces the descriptor set. */
static void test_ds_set_param_desc_keeps_accessors(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    int marker = 0;
    iolink_ds_init(&ds, NULL);
    iolink_ds_bind_param_access(&ds, ds_param_read_mock, ds_param_write_mock, &marker, NULL, 0U);

    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 0U}};
    iolink_ds_set_param_desc(&ds, set, 1U);
    assert_ptr_equal(ds.param_user, &marker);
    assert_ptr_equal(ds.read_param, ds_param_read_mock);
    assert_int_equal(ds.param_count, 1U);
}

/* A descriptor max_len bounds the value stored in the image. */
static void test_ds_desc_max_len_bounds_value(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    static const iolink_ds_param_desc_t set[] = {{IOLINK_IDX_APPLICATION_TAG, 0U, 4U}};
    iolink_ds_bind_param_access(&ds, ds_param_read_mock, ds_param_write_mock, NULL, set, 1U);

    assert_true(iolink_ds_build_image(&ds) > 0);
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &len);
    assert_non_null(img);
    assert_int_equal(img[3], 4U); /* length byte capped to max_len */
    assert_memory_equal(&img[4], "ABCD", 4U);
}

/* ------------------------------------------------------------------------- */
/* Shared fixture for the Data Storage conformance cases 0077-0081, 0374.    */
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

/* 0077: complete upload through the Data Storage Index (Index_List), checking
 * size, checksum and the serialized records. */
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

    /* Index_List enumerates the complete DS set and the 0x0000 terminator. */
    uint8_t list[32] = {0U};
    const int list_len = ds_fixture_isdu_read(&f, IOLINK_IDX_DATA_STORAGE, 5U, list, sizeof(list));
    assert_int_equal(list_len, 11);
    static const uint8_t expected_list[] = {0x00U, 0x18U, 0x00U, 0x00U, 0x19U, 0x00U,
                                            0x00U, 0x1AU, 0x00U, 0x00U, 0x00U};
    assert_memory_equal(list, expected_list, sizeof(expected_list));

    /* Sub 0 is the concatenated DataStorageIndex record:
       DS_Command(1) + State_Property(1) + Size(4) + Checksum(4) + Index_List. */
    uint8_t record[64] = {0U};
    const int record_len =
        ds_fixture_isdu_read(&f, IOLINK_IDX_DATA_STORAGE, 0U, record, sizeof(record));
    assert_int_equal(record_len, 10 + list_len);
    assert_int_equal(record[0], 0x00U); /* DS_Command read: unspecified */
    assert_int_equal(record[1], 0x02U); /* State_Property: upload */
    const uint32_t size = ((uint32_t) record[2] << 24) | ((uint32_t) record[3] << 16) |
                          ((uint32_t) record[4] << 8) | (uint32_t) record[5];
    const uint32_t checksum = ((uint32_t) record[6] << 24) | ((uint32_t) record[7] << 16) |
                              ((uint32_t) record[8] << 8) | (uint32_t) record[9];
    assert_int_equal(size, f.ds.image_len);
    assert_int_equal(checksum, iolink_ds_calc_checksum(f.ds.image, f.ds.image_len));
    assert_memory_equal(&record[10], expected_list, sizeof(expected_list));
    assert_int_equal(ds_fixture_read_u32(&f, 3U), size);
    assert_int_equal(ds_fixture_read_u32(&f, 4U), checksum);

    /* Follow every Index_List entry and compare the live value with its record. */
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

/* 0078: set 1 -> set 2 via ParamDownloadStore, then upload set 2 and verify the
 * Parameter_Checksum changed with it. */
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
    /* Block writes are staged; set 1 stays active until the store. */
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "S1A");
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)),
                     0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_true(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "S2A");
    const uint32_t checksum_set2 = ds_fixture_read_u32(&f, 4U);
    assert_int_not_equal(checksum_set2, checksum_set1);

    /* Upload set 2 and verify the image now carries it. */
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

/* 0079: a plain parameter change must not raise DS_UPLOAD_REQ (no-write /
 * timeout path), while ParamDownloadStore without parameter writes still does. */
static void test_ds_0079_no_write_store_timeout(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0079");
    (void) state;
    ds_fixture_t f;
    ds_fixture_init(&f, NULL);
    ds_fixture_set_tags(&f, "OLD", "OLD", "OLD");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;

    /* Set 2 written outside a block download. The DS state must stay idle and
       no upload notification may be pending (the 1 s timeout elapses). */
    ds_fixture_set_tags(&f, "NEW", "NEW", "NEW");
    iolink_ds_process(&f.ds);
    assert_false(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);

    /* ParamDownloadStart + ParamDownloadStore without writing any parameter. */
    uint8_t resp[2] = {0U};
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_fixture_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)),
                     0);
    assert_true(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);
    ds_fixture_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "NEW");
}

/* 0080: a local parameter change (teach-in/menu equivalent) plus the library
 * hook must request and complete a DS upload of the new set. */
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

/* 0081: ParamBreak aborts an upload at three distinct points without applying
 * data or changing the parameter checksum. */
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
            /* Abort after the upload data has been read once. */
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

/* 0374: DS_UPLOAD_FLAG is non-volatile; DS_UploadEnd clears and persists it.
 * A storage-backed re-init models a library-level power cycle. */
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

    /* ParamDownloadStore after a block download raises and persists the flag. */
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

    /* Power cycle: a fresh context reads the flag back from storage. */
    ds_fixture_init(&f, &g_ds_storage_mock);
    assert_true(f.ds.ds_upload_flag);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);

    /* DS_UploadStart/UploadEnd clears and persists the flag. */
    assert_int_equal(ds_fixture_ds_command(&f, 0x01U, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_fixture_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_false(f.ds.ds_upload_flag);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);

    /* Another power cycle still sees the cleared flag. */
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
