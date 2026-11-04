/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_system_commands.c
 * @brief SystemCommand (index 0x0002, Table B.9) coverage beyond the basic
 *        reset/factory cases: back-to-box, reserved commands and the
 *        Data-Storage access-lock path.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/dll.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/events.h"
#include "iolinki/protocol.h"
#include "iolinki/device_info.h"
#include "test_helpers.h"

#include "conformance_test.h"

static int test_setup(void** state)
{
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    return 0;
}

static int send_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    return isdu_send_write_request(ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U);
}

/* Fixture for the DS/Block conformance cases (0078-0084, 0143-0147, 0321,
 * 0322, 0327, 0328, 0330). It keeps every parameter context isolated from the
 * legacy singleton used by the older tests in this file. */
typedef struct
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
} ds_block_fixture_t;

static void ds_block_fixture_init(ds_block_fixture_t* f)
{
    iolink_device_info_ctx_init(&f->info, NULL);
    iolink_params_ctx_init(&f->params, &f->info);
    iolink_ds_init(&f->ds, NULL);
    iolink_ds_bind_params(&f->ds, &f->params);
    iolink_events_init(&f->events);
}

static void ds_block_set_tags(ds_block_fixture_t* f, const char* app, const char* fun,
                              const char* loc)
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

static int ds_block_write(ds_block_fixture_t* f, uint16_t index, uint8_t sub, const uint8_t* data,
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

static int ds_block_read(ds_block_fixture_t* f, uint16_t index, uint8_t sub, uint8_t* buf,
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

static int ds_block_command(ds_block_fixture_t* f, uint8_t cmd, uint8_t* buf, size_t cap)
{
    return ds_block_write(f, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U, buf, cap);
}

static int ds_block_ds_command(ds_block_fixture_t* f, uint8_t cmd, uint8_t* buf, size_t cap)
{
    return ds_block_write(f, IOLINK_IDX_DATA_STORAGE, 1U, &cmd, 1U, buf, cap);
}

static uint32_t ds_block_checksum(ds_block_fixture_t* f)
{
    uint8_t r[4] = {0U};
    assert_int_equal(ds_block_read(f, IOLINK_IDX_DATA_STORAGE, 4U, r, sizeof(r)), 4);
    return ((uint32_t) r[0] << 24) | ((uint32_t) r[1] << 16) | ((uint32_t) r[2] << 8) |
           (uint32_t) r[3];
}

static void ds_block_assert_tag(ds_block_fixture_t* f, uint16_t index, const char* expected)
{
    uint8_t buf[40] = {0U};
    const size_t expected_len = strlen(expected);
    assert_int_equal(iolink_params_ctx_get(&f->params, index, 0U, buf, sizeof(buf)),
                     (int) expected_len);
    assert_memory_equal(buf, expected, expected_len);
}

static void ds_block_write_tags(ds_block_fixture_t* f, const char* app, const char* fun,
                                const char* loc)
{
    uint8_t resp[2] = {0U};
    assert_int_equal(ds_block_write(f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) app,
                                    (uint8_t) strlen(app), resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_block_write(f, IOLINK_IDX_FUNCTION_TAG, 0U, (const uint8_t*) fun,
                                    (uint8_t) strlen(fun), resp, sizeof(resp)),
                     0);
    assert_int_equal(ds_block_write(f, IOLINK_IDX_LOCATION_TAG, 0U, (const uint8_t*) loc,
                                    (uint8_t) strlen(loc), resp, sizeof(resp)),
                     0);
}

/* 0x83 Back-to-box restores factory defaults like 0x82. */
static void test_back_to_box_restores_defaults(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0318");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    const uint8_t tag[] = "BoxTag";
    assert_int_equal(iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, tag, sizeof(tag) - 1U, true),
                     0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_BACK_TO_BOX), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[1];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    uint8_t readback[33];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, readback, sizeof(readback)),
                     0);
}

/* Table 101: the reset SystemCommands clear Device diagnosis and status;
 * Application reset leaves them untouched. */
static void test_reset_commands_clear_diagnostics(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0316");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;

    const uint8_t reset_commands[] = {IOLINK_CMD_DEVICE_RESET, IOLINK_CMD_RESTORE_FACTORY_SETTINGS,
                                      IOLINK_CMD_BACK_TO_BOX};
    for (size_t i = 0U; i < (sizeof(reset_commands) / sizeof(reset_commands[0])); i++) {
        (void) memset(&dll, 0, sizeof(dll));
        iolink_events_init(&dll.events);
        dll.crc_errors = 3U;
        dll.timeout_errors = 2U;
        dll.framing_errors = 1U;
        dll.timing_errors = 4U;
        iolink_event_trigger(&dll.events, 0x1000U, IOLINK_EVENT_TYPE_ERROR);

        iolink_isdu_init(&ctx);
        ctx.dll_ctx = &dll;
        ctx.event_ctx = &dll.events;
        assert_int_equal(send_system_command(&ctx, reset_commands[i]), 1);
        iolink_isdu_process(&ctx);

        assert_int_equal(dll.crc_errors, 0U);
        assert_int_equal(dll.timeout_errors, 0U);
        assert_int_equal(dll.framing_errors, 0U);
        assert_int_equal(dll.timing_errors, 0U);
        assert_int_equal(dll.events.count, 0U);
        assert_false(iolink_events_flag(&dll.events));
    }

    /* Application reset (0x81): Table 101 says "No". */
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    dll.crc_errors = 5U;
    iolink_event_trigger(&dll.events, 0x2000U, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    ctx.event_ctx = &dll.events;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_APPLICATION_RESET), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(dll.crc_errors, 5U);
    iolink_event_t ev;
    assert_true(iolink_events_peek(&dll.events, &ev));
    assert_int_equal(ev.code, 0x2000U); /* the original diagnosis survives */
}

/* Reserved commands (0x07..0x3F, 0x84..0x9F) answer FUNC_NOTAVAIL. */
static void test_reserved_commands_rejected(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0104");
    (void) state;
    const uint8_t reserved[] = {0x07U, 0x3FU, 0x84U, 0x9FU};

    for (size_t i = 0U; i < sizeof(reserved); i++) {
        iolink_isdu_ctx_t ctx;
        iolink_isdu_init(&ctx);

        assert_int_equal(send_system_command(&ctx, reserved[i]), 1);
        iolink_isdu_process(&ctx);

        uint8_t resp[2];
        int n = isdu_collect_response(&ctx, resp, sizeof(resp));
        assert_int_equal(n, 2);
        assert_int_equal(resp[0], 0x80U);
        assert_int_equal(resp[1], IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
    }
}

/* A Data-Storage download is refused while the DS access lock is set. */
static void test_ds_download_rejected_by_access_lock(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0156");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    iolink_device_info_set_access_locks(IOLINK_LOCK_DS);
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    int n = isdu_collect_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Upload (a read of device data) is still allowed under the DS lock. */
    iolink_device_info_set_access_locks(0U);
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
}

/* 0327: a second UploadStart during an active upload restarts the upload. */
static void test_ds_command_busy(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0327");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
}

/* B.2.4: the parameter (write) access lock shall NOT block Data Storage
 * downloads (between DS_DownloadStart and DS_DownloadEnd/Break). */
static void test_param_lock_does_not_block_ds_download(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0156");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
}

/* 10.3.2: parameter writes during a block download are buffered and only
 * committed atomically by ParamDownloadEnd. */
static void test_block_param_download_buffered(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0143");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "OLD", 3U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    /* A tag write is staged, not applied. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "NEW", 3U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    uint8_t buf[8];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 3);
    assert_memory_equal(buf, "OLD", 3);

    /* ParamDownloadEnd commits the buffered write. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 3);
    assert_memory_equal(buf, "NEW", 3);
}

/* ParamBreak cancels a block download without applying any buffered writes. */
static void test_block_param_download_break(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0144");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "OLD", 3U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "NEW", 3U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_BREAK), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    uint8_t buf[8];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 3);
    assert_memory_equal(buf, "OLD", 3);

    /* A DownloadEnd after the break is unexpected -> 0x8036 (0324/0084). */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

/* ParamDownloadStore finalizes the download and raises DS_UPLOAD_REQ (10.4.2). */
static void test_param_download_store_raises_ds_upload_req(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0078");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0079");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0321");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "OLD", 3U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "NEW", 3U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_STORE), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    uint8_t buf[8];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 3);
    assert_memory_equal(buf, "NEW", 3);

    /* The DS_UPLOAD_REQ notification is deferred until the ISDU transport is
       idle (the device pump publishes it), so the flag cannot appear inside the
       store response. The DS_UPLOAD_FLAG is already set, the queue is not. */
    assert_true(ds.upload_event_pending);
    assert_false(iolink_events_pending(&events));
    assert_int_equal(iolink_ds_state_property(&ds) & 0x80U, 0x80U);

    /* Simulate the device pump: publish the deferred notification. */
    ds.upload_event_pending = false;
    iolink_event_trigger(&events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST,
                         IOLINK_EVENT_TYPE_NOTIFICATION);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
}

/* Table B.10: Parameter_Checksum reflects the current parameter set. A direct
 * parameter write must already be visible in the checksum, and ParamDownloadStore
 * must not change it (conformance 0078). */
static void test_ds_checksum_stable_across_store(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0078");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "DS-SET2", 7U, false),
        0);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 4U), 1);
    iolink_isdu_process(&ctx);
    uint8_t cks0[4];
    assert_int_equal(isdu_collect_response(&ctx, cks0, sizeof(cks0)), 4);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_STORE), 1);
    iolink_isdu_process(&ctx);
    uint8_t dummy[2];
    (void) isdu_collect_response(&ctx, dummy, sizeof(dummy));

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 4U), 1);
    iolink_isdu_process(&ctx);
    uint8_t cks1[4];
    assert_int_equal(isdu_collect_response(&ctx, cks1, sizeof(cks1)), 4);

    assert_memory_equal(cks0, cks1, sizeof(cks0));
}

/* 0081: ParamBreak aborts an upload at every stage without applying data. */
static void test_0081_upload_abort_points(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0081");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];

    /* Abort directly after UploadStart. */
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_BREAK), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Abort while the upload is active. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_ds_process(&ds);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_BREAK), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

/* 0324: unexpected ending commands in Idle -> 0x8036. */
static void test_0324_unexpected_end_in_idle(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0324");
    (void) state;
    const uint8_t cmds[] = {IOLINK_CMD_PARAM_UPLOAD_END, IOLINK_CMD_PARAM_BREAK,
                            IOLINK_CMD_PARAM_DOWNLOAD_END};
    for (size_t i = 0U; i < sizeof(cmds); i++) {
        iolink_isdu_ctx_t ctx;
        iolink_ds_ctx_t ds;
        iolink_ds_init(&ds, NULL);
        iolink_isdu_init(&ctx);
        ctx.ds_ctx = &ds;
        assert_int_equal(send_system_command(&ctx, cmds[i]), 1);
        iolink_isdu_process(&ctx);
        uint8_t resp[2];
        assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
        assert_int_equal(resp[0], 0x80U);
        assert_int_equal(resp[1], IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
        assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    }
}

/* 0325: parameter writes are rejected during an active upload. */
static void test_0325_write_during_upload(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0325");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_ds_process(&ds);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "X", 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

/* 0326: parameter reads are rejected during an active download, while the
 * DataStorageIndex remains readable. */
static void test_0326_read_during_download(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0326");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0U), 1);
    iolink_isdu_process(&ctx);
    uint8_t ds_buf[32];
    assert_true(isdu_collect_response(&ctx, ds_buf, sizeof(ds_buf)) > 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

/* 0147: a second DownloadStart restarts the download without an error. */
static void test_0147_double_download_start(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0147");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET1", 4U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "TMP", 3U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET2", 4U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    uint8_t buf[8];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET2", 4);
}

/* 0328: starting the opposite action interrupts the current one. */
static void test_0328_switch_upload_download(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0328");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_ds_process(&ds);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
}

/* 0330: UploadStart/UploadEnd during Download discards staged values. */
static void test_0330_upload_discards_download(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0330");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];
    uint8_t buf[8];

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET1", 4U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET2", 4U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET1", 4);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET2", 4U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_UPLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET1", 4);
}

/* 0082: download after a direct parameter modification. */
static void test_0082_download_after_modification(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0082");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];
    uint8_t buf[8];

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET1", 4U, true), 0);
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "MOD", 3U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET2", 4U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET2", 4);
}

/* 0083: download restores a parameter set after a factory reset. */
static void test_0083_download_after_factory_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0083");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    uint8_t resp[2];
    uint8_t buf[8];

    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET1", 4U, true), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_RESTORE_FACTORY_SETTINGS), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 0);

    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_START), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "SET1", 4U),
        1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(send_system_command(&ctx, IOLINK_CMD_PARAM_DOWNLOAD_END), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET1", 4);
}

/* 0143: full Config4/Block download of a multi-parameter set, committed
 * atomically; staged values must not leak before DownloadEnd and the checksum
 * must follow the committed set. */
static void test_0143_multi_parameter_block(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0143");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "OLD1", "OLD2", "OLD3");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;
    const uint32_t checksum_old = ds_block_checksum(&f);
    uint8_t resp[2] = {0U};

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);
    ds_block_write_tags(&f, "NEW1", "NEW2", "NEW3");

    /* Still the old set while the block is buffered. */
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "OLD1");
    ds_block_assert_tag(&f, IOLINK_IDX_FUNCTION_TAG, "OLD2");
    ds_block_assert_tag(&f, IOLINK_IDX_LOCATION_TAG, "OLD3");
    assert_int_equal(ds_block_checksum(&f), checksum_old);

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_END, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "NEW1");
    ds_block_assert_tag(&f, IOLINK_IDX_FUNCTION_TAG, "NEW2");
    ds_block_assert_tag(&f, IOLINK_IDX_LOCATION_TAG, "NEW3");
    assert_int_not_equal(ds_block_checksum(&f), checksum_old);

    /* A plain block download does not raise DS_UPLOAD_REQ by itself. */
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
    assert_false(f.ds.upload_event_pending);
}

/* 0147: a second DownloadStart restarts the download; values staged by the
 * first attempt are discarded, the second set is committed. */
static void test_0147_double_download_start_full(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0147");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "SET1A", "SET1B", "SET1C");
    iolink_ds_clear_upload(&f.ds);
    const uint32_t checksum_set1 = ds_block_checksum(&f);
    uint8_t resp[2] = {0U};

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);
    ds_block_write_tags(&f, "TMP1", "TMP2", "TMP3");

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);
    ds_block_write_tags(&f, "SET2A", "SET2B", "SET2C");
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_END, resp, sizeof(resp)), 0);

    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "SET2A");
    ds_block_assert_tag(&f, IOLINK_IDX_FUNCTION_TAG, "SET2B");
    ds_block_assert_tag(&f, IOLINK_IDX_LOCATION_TAG, "SET2C");
    assert_int_not_equal(ds_block_checksum(&f), checksum_set1);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 0321: a single parameter write must not raise DS_UPLOAD_REQ / set the flag;
 * ParamDownloadStore must. */
static void test_0321_single_write_no_upload(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0321");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "OLD", "OLD", "OLD");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;
    uint8_t resp[2] = {0U};

    /* a/b/c: a single parameter write changes the checksum but must not
       request Data Storage or set DS_UPLOAD_FLAG. */
    assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "ONE", 3U,
                                    resp, sizeof(resp)),
                     0);
    iolink_ds_process(&f.ds);
    assert_true(ds_block_checksum(&f) != 0U);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
    assert_false(f.ds.upload_event_pending);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);

    /* d/e/f/g: ParamDownloadStore in DSIdle_2 raises the request and flag. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);
    assert_true(f.ds.upload_event_pending);
}

/* 0322: DS_UPLOAD_FLAG must follow the 1/0/1/0 sequence over two store/upload
 * and download cycles. */
static void test_0322_upload_flag_four_step(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0322");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "A", "B", "C");
    iolink_ds_clear_upload(&f.ds);
    uint8_t resp[2] = {0U};

    /* a/b: ParamDownloadStore -> DSIdle_2 with flag 1. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);

    /* c/d: DS_UploadEnd while in DSIdle_2 -> flag 0. */
    assert_int_equal(ds_block_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);

    /* e/f: another ParamDownloadStore -> flag 1. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);

    /* g/h: DS_DownloadEnd while in DSIdle_2 -> flag 0. */
    assert_int_equal(ds_block_ds_command(&f, 0x04U, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 0327: unexpected parameter writes during an upload are rejected with 0x8022,
 * a DownloadEnd discards the upload, and a write after it succeeds. */
static void test_0327_unexpected_in_upload(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0327");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "SET1", "SET1", "SET1");
    iolink_ds_clear_upload(&f.ds);
    uint8_t resp[2] = {0U};

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "X", 1U,
                                    resp, sizeof(resp)),
                     2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    /* A second UploadStart restarts the upload (no error). */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "Y", 1U,
                                    resp, sizeof(resp)),
                     2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    /* DownloadEnd terminates the upload; the following write succeeds. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_END, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "Z", 1U,
                                    resp, sizeof(resp)),
                     0);
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "Z");

    /* g/h/i/j: dataStorage=true. ParamDownloadStore aborts an active upload,
       raises DS_UPLOAD_REQ and sets DS_UPLOAD_FLAG; DS_UploadEnd clears it. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_STORE, resp, sizeof(resp)), 0);
    assert_true(f.ds.upload_event_pending);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0x80U);
    assert_int_equal(ds_block_ds_command(&f, 0x02U, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 0328: switching Upload -> Download -> Upload interrupts each active action
 * and keeps the parameter access locks of the new state. */
static void test_0328_switch_states_full(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0328");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "SET1", "SET1", "SET1");
    iolink_ds_clear_upload(&f.ds);
    uint8_t resp[2] = {0U};

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "A", 1U,
                                    resp, sizeof(resp)),
                     2);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    /* Upload -> Download. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);
    assert_int_equal(ds_block_read(&f, IOLINK_IDX_APPLICATION_TAG, 0U, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    /* Download -> Upload. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)), 0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) "B", 1U,
                                    resp, sizeof(resp)),
                     2);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_END, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
}

/* 0330: UploadStart/UploadEnd during a Download discard the staged values; the
 * original parameter set remains readable at every step. */
static void test_0330_upload_discards_download_full(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0330");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "SET1", "SET1", "SET1");
    iolink_ds_clear_upload(&f.ds);
    uint8_t resp[2] = {0U};

    /* a: original value. */
    uint8_t buf[8] = {0U};
    assert_int_equal(ds_block_read(&f, IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET1", 4);

    /* b/c: Download + staged set 2. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    ds_block_write_tags(&f, "SET2", "SET2", "SET2");

    /* d/e: UploadStart discards the staged download; the read returns set 1. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_START, resp, sizeof(resp)), 0);
    assert_int_equal(ds_block_read(&f, IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, "SET1", 4);

    /* f: UploadEnd leaves the set intact. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_END, resp, sizeof(resp)), 0);
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "SET1");

    /* g/h/i: a second download is discarded by UploadEnd as well. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    ds_block_write_tags(&f, "SET3", "SET3", "SET3");
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_UPLOAD_END, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "SET1");
    ds_block_assert_tag(&f, IOLINK_IDX_FUNCTION_TAG, "SET1");
    ds_block_assert_tag(&f, IOLINK_IDX_LOCATION_TAG, "SET1");
}

/* 0082: after a direct (non-block) modification a block download restores the
 * stored set, and the modification alone does not request a DS upload. */
static void test_0082_restore_after_direct_change(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0082");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "SET1", "SET1", "SET1");
    iolink_ds_clear_upload(&f.ds);
    f.ds.upload_event_pending = false;
    const uint32_t checksum_set1 = ds_block_checksum(&f);
    uint8_t resp[2] = {0U};

    /* Direct modification outside a block download. */
    assert_int_equal(iolink_params_ctx_set(&f.params, IOLINK_IDX_APPLICATION_TAG, 0U,
                                           (const uint8_t*) "MOD", 3U, true),
                     0);
    iolink_ds_process(&f.ds);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
    assert_false(f.ds.upload_event_pending);

    /* Restore set 1 through the block download path. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    ds_block_write_tags(&f, "SET1", "SET1", "SET1");
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_END, resp, sizeof(resp)), 0);
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "SET1");
    assert_int_equal(ds_block_checksum(&f), checksum_set1);
    assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
}

/* 0083: Restore factory settings followed by a block download restores the
 * stored set and its checksum. */
static void test_0083_restore_after_factory_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0083");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "FACT", "FACT", "FACT");
    iolink_ds_clear_upload(&f.ds);
    const uint32_t checksum_factory = ds_block_checksum(&f);
    uint8_t resp[2] = {0U};
    const uint8_t factory_cmd = IOLINK_CMD_RESTORE_FACTORY_SETTINGS;

    assert_int_equal(
        ds_block_write(&f, IOLINK_IDX_SYSTEM_COMMAND, 0U, &factory_cmd, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);

    /* The stored master set is downloaded again. */
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)), 0);
    ds_block_write_tags(&f, "FACT", "FACT", "FACT");
    assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_END, resp, sizeof(resp)), 0);
    ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "FACT");
    ds_block_assert_tag(&f, IOLINK_IDX_FUNCTION_TAG, "FACT");
    ds_block_assert_tag(&f, IOLINK_IDX_LOCATION_TAG, "FACT");
    assert_int_equal(ds_block_checksum(&f), checksum_factory);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
}

/* 0084: ParamBreak aborts a block download at three points without changing
 * the active set; a DownloadEnd after the break is unexpected (0x8036). */
static void test_0084_download_abort_points(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0084");
    (void) state;
    ds_block_fixture_t f;
    ds_block_fixture_init(&f);
    ds_block_set_tags(&f, "SET1", "SET1", "SET1");
    iolink_ds_clear_upload(&f.ds);
    const uint32_t checksum_set1 = ds_block_checksum(&f);
    uint8_t resp[2] = {0U};

    for (int point = 0; point < 3; point++) {
        assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_START, resp, sizeof(resp)),
                         0);
        assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);
        if (point == 1) {
            assert_int_equal(ds_block_write(&f, IOLINK_IDX_APPLICATION_TAG, 0U,
                                            (const uint8_t*) "TMP", 3U, resp, sizeof(resp)),
                             0);
        }
        else if (point == 2) {
            ds_block_write_tags(&f, "TMP", "TMP", "TMP");
        }
        assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_BREAK, resp, sizeof(resp)), 0);
        assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
        assert_int_equal(iolink_ds_state_property(&f.ds) & 0x80U, 0U);
        assert_int_equal(ds_block_checksum(&f), checksum_set1);
        ds_block_assert_tag(&f, IOLINK_IDX_APPLICATION_TAG, "SET1");

        /* Post-break DownloadEnd -> 0x8036. */
        assert_int_equal(ds_block_command(&f, IOLINK_CMD_PARAM_DOWNLOAD_END, resp, sizeof(resp)),
                         2);
        assert_int_equal(resp[0], 0x80U);
        assert_int_equal(resp[1], IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
        assert_int_equal(f.ds.state, IOLINK_DS_STATE_IDLE);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_back_to_box_restores_defaults, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_reset_commands_clear_diagnostics, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_reserved_commands_rejected, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_ds_download_rejected_by_access_lock, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_ds_command_busy, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_param_lock_does_not_block_ds_download, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_block_param_download_buffered, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_block_param_download_break, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_param_download_store_raises_ds_upload_req, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_ds_checksum_stable_across_store, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0081_upload_abort_points, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0324_unexpected_end_in_idle, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0325_write_during_upload, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0326_read_during_download, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0147_double_download_start, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0328_switch_upload_download, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0330_upload_discards_download, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0082_download_after_modification, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0083_download_after_factory_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0143_multi_parameter_block, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0147_double_download_start_full, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0321_single_write_no_upload, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0322_upload_flag_four_step, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0327_unexpected_in_upload, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0328_switch_states_full, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0330_upload_discards_download_full, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0082_restore_after_direct_change, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0083_restore_after_factory_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0084_download_abort_points, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
