/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu.c
 * @brief Unit tests for ISDU acyclic messaging
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/protocol.h"
#include "iolinki/platform.h"
#include "iolinki/dll.h"
#include "iolinki/device_info.h"
#include "test_helpers.h"

static int test_setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static void test_isdu_vendor_name_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 1. Send READ Request for Index 0x10 (Vendor Name) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x0010, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* 2. Collect Response */
    char name[32] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) name, sizeof(name) - 1);

    assert_int_equal(len, 7);
    assert_memory_equal(name, "iolinki", 7);
}

static void test_isdu_device_status_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* 1. Initially status should be OK (0) */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t status_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 0);

    /* 2. Trigger an error and check status again */
    iolink_event_trigger(&events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 4); /* Failure (Table B.13) */
}

static void test_isdu_detailed_device_status_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, 0x1801, IOLINK_EVENT_TYPE_ERROR);

    /* Read Detailed Status (Index 0x0025, Table B.8) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x0025, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t detailed_buf[3];
    assert_int_equal(isdu_collect_response(&ctx, detailed_buf, sizeof(detailed_buf)), 3);
    /* EventQualifier per V1.1.5 (Figure A.24/Table A.17): MODE=appears(0xC0)
       | TYPE=error(0x03<<4=0x30) | SOURCE=device(0) | INSTANCE=Application(0x04) = 0xF4 */
    assert_int_equal(detailed_buf[0], 0xF4);
    assert_int_equal(detailed_buf[1], 0x18); /* Code High */
    assert_int_equal(detailed_buf[2], 0x01); /* Code Low */
}

/* CR443 (Package 2026 Interface D1.1.6-02, B.2.22 Table B.14 p. 270):
 * each DetailedDeviceStatus record is exactly [EventQualifier][EventCode MSB]
 * [EventCode LSB]. This pins the raw octet order so a regression to the old
 * "Octet 2,3: EventCode" wording (V1.1.5) cannot slip in unnoticed. */
static void test_isdu_detailed_device_status_cr443_octet_order(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* One queued warning: 0x8CA0 (vendor-specific Device range). */
    iolink_event_trigger(&events, 0x8CA0, IOLINK_EVENT_TYPE_WARNING);

    /* Read DetailedDeviceStatus (Index 0x0025, Table B.14). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    /* EventQualifier per Figure A.24/Table A.17:
       MODE=event appears (0xC0) | TYPE=warning (0x02 << 4) | INSTANCE=Application (0x04). */
    uint8_t record[3];
    assert_int_equal(isdu_collect_response(&ctx, record, sizeof(record)), 3);
    assert_int_equal(record[0], 0xE4U); /* Octet 1: EventQualifier */
    assert_int_equal(record[1], 0x8CU); /* Octet 2: EventCode MSB */
    assert_int_equal(record[2], 0xA0U); /* Octet 3: EventCode LSB */

    const uint8_t expected[3] = {0xE4U, 0x8CU, 0xA0U};
    assert_memory_equal(record, expected, sizeof(expected));
}

static void test_isdu_error_stats_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll_ctx;
    (void) memset(&dll_ctx, 0, sizeof(dll_ctx));
    dll_ctx.crc_errors = 0x11223344U;
    dll_ctx.timeout_errors = 0xAABBCCDDU;
    dll_ctx.framing_errors = 0x01020304U;
    dll_ctx.timing_errors = 0x05060708U;

    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll_ctx;

    /* Read Error Statistics (vendor object at Index 0x0040) */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_STATS, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[16];
    int len = isdu_collect_response(&ctx, data, sizeof(data));
    assert_int_equal(len, sizeof(data));

    const uint8_t expected[16] = {0x11U, 0x22U, 0x33U, 0x44U, 0xAAU, 0xBBU, 0xCCU, 0xDDU,
                                  0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
    assert_memory_equal(data, expected, sizeof(expected));
}

/* System Command Tests */

static void test_system_cmd_device_reset(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write 0x80 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify reset flag is set */
    assert_true(ctx.reset_pending);

    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_application_reset(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write 0x81 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_APPLICATION_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify app reset flag is set */
    assert_true(ctx.app_reset_pending);

    /* Verify success response */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_factory_restore(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Set a custom application tag first */
    const uint8_t test_tag[] = "TestTag123";
    iolink_params_set(0x0018, 0x00, test_tag, sizeof(test_tag) - 1, false);

    /* Write 0x82 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_RESTORE_FACTORY_SETTINGS};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */

    /* Verify application tag was reset */
    uint8_t readback[33];
    int len = iolink_params_get(0x0018, 0x00, readback, sizeof(readback));
    assert_int_equal(len, 0); /* Should be empty after factory reset */
}

/* Table 101: Device reset sets the diagnosis and status to "0"; Restore
 * factory settings and Back-to-box "Clear" it by resetting ErrorCount and
 * withdrawing the active Events; Application reset leaves it untouched. */
static void test_system_cmd_resets_diagnostics(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    iolink_device_info_init(NULL);
    iolink_params_init();
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
    const uint8_t device_reset = IOLINK_CMD_DEVICE_RESET;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &device_reset, 1), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(dll.crc_errors, 0U);
    assert_int_equal(dll.timeout_errors, 0U);
    assert_int_equal(dll.framing_errors, 0U);
    assert_int_equal(dll.timing_errors, 0U);
    assert_int_equal(dll.events.count, 0U);
    assert_false(iolink_events_flag(&dll.events));

    /* Table 101 "0": both DeviceStatus (0x0024) and DetailedDeviceStatus
       (0x0025) read back as "0" / empty after the reset. */
    uint8_t drain[2];
    assert_int_equal(isdu_collect_response(&ctx, drain, sizeof(drain)), 0);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t status = 0xFFU;
    assert_int_equal(isdu_collect_response(&ctx, &status, 1), 1);
    assert_int_equal(status, 0U);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(ctx.response_len, 0U);
    assert_int_equal(isdu_collect_response(&ctx, drain, sizeof(drain)), 0);

    /* 0x82 withdraws the active diagnosis through Event Disappears; 0x83
       discards it without queuing any Event (Table 101 "0" vs "Clear"). */
    const uint8_t reset_commands[] = {IOLINK_CMD_RESTORE_FACTORY_SETTINGS, IOLINK_CMD_BACK_TO_BOX};
    for (size_t i = 0U; i < (sizeof(reset_commands) / sizeof(reset_commands[0])); i++) {
        (void) memset(&dll, 0, sizeof(dll));
        iolink_events_init(&dll.events);
        dll.crc_errors = 3U;
        dll.timeout_errors = 2U;
        dll.framing_errors = 1U;
        dll.timing_errors = 4U;
        iolink_event_trigger(&dll.events, IOLINK_EVENTCODE_GENERAL_MALFUNCTION,
                             IOLINK_EVENT_TYPE_ERROR);

        iolink_isdu_init(&ctx);
        ctx.dll_ctx = &dll;
        ctx.event_ctx = &dll.events;
        const uint8_t cmd = reset_commands[i];
        assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &cmd, 1),
                         1);
        iolink_isdu_process(&ctx);

        assert_int_equal(dll.crc_errors, 0U);
        assert_int_equal(dll.timeout_errors, 0U);
        assert_int_equal(dll.framing_errors, 0U);
        assert_int_equal(dll.timing_errors, 0U);
        if (cmd == IOLINK_CMD_RESTORE_FACTORY_SETTINGS) {
            /* Table 101 "Clear": the appearance already published stays in the
               queue, the withdraw pair is appended immediately, and DeviceStatus
               drops out because the condition is no longer asserted. */
            iolink_event_t event;
            assert_int_equal(dll.events.count, 2U);
            assert_true(iolink_events_flag(&dll.events));
            assert_true(iolink_events_pop(&dll.events, &event));
            assert_int_equal(event.code, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
            assert_int_equal(event.mode, IOLINK_EVENT_MODE_APPEARS);
            assert_true(iolink_events_pop(&dll.events, &event));
            assert_int_equal(event.code, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
            assert_int_equal(event.mode, IOLINK_EVENT_MODE_DISAPPEARS);
            assert_int_equal(iolink_events_get_highest_severity(&dll.events), 0U);
        }
        else {
            /* Table 101 "0": no Event is queued and the memory is blank. */
            assert_int_equal(dll.events.count, 0U);
            assert_false(iolink_events_flag(&dll.events));
            assert_int_equal(iolink_events_memory_read(&dll.events, 0x00U), 0x00U);
            assert_int_equal(iolink_events_get_highest_severity(&dll.events), 0U);
        }
    }

    /* Application reset (0x81): Table 101 says "No". */
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    dll.crc_errors = 5U;
    iolink_event_trigger(&dll.events, 0x2000U, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    const uint8_t app_reset = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &app_reset, 1),
                     1);
    iolink_isdu_process(&ctx);

    assert_int_equal(dll.crc_errors, 5U);
    assert_int_equal(dll.events.count, 1U);
}

/* Table 100/C.2.15: a DS command that the current state does not allow is
 * answered with 0x36 (FUNC_UNAVAILTEMP), not 0x30 (PAR_VALOUTOFRNG). */
static void test_system_cmd_param_download_store_busy(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    ds.state = IOLINK_DS_STATE_UPLOADING;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    const uint8_t store = IOLINK_CMD_PARAM_DOWNLOAD_STORE;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &store, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t response[2];
    assert_int_equal(isdu_collect_response(&ctx, response, sizeof(response)), 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
}

/* B.2.22: after Restore factory settings the withdrawn Events are not part of
 * DetailedDeviceStatus, and DeviceStatus is back to "0". */
static void test_system_cmd_factory_restore_clears_detailed_status(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    iolink_device_info_init(NULL);
    iolink_params_init();
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    iolink_event_trigger(&dll.events, IOLINK_EVENTCODE_GENERAL_MALFUNCTION,
                         IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    ctx.event_ctx = &dll.events;
    const uint8_t restore = IOLINK_CMD_RESTORE_FACTORY_SETTINGS;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &restore, 1),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    ctx.event_ctx = &dll.events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00U), 1);
    iolink_isdu_process(&ctx);
    uint8_t status = 0xFFU;
    assert_int_equal(isdu_collect_response(&ctx, &status, 1), 1);
    assert_int_equal(status, 0U);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    ctx.event_ctx = &dll.events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00U), 1);
    iolink_isdu_process(&ctx);
    uint8_t detailed[8];
    assert_int_equal(isdu_collect_response(&ctx, detailed, sizeof(detailed)), 0);
}

static void test_system_cmd_param_upload(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x01 (ParamUploadStart) to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_UPLOAD_START};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to upload */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_param_download(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x03 (ParamDownloadStart) to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_DOWNLOAD_START};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to download */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

/* 10.3.5/10.4.2: ParamDownloadStore ends the download and asks the Master to
 * upload the parameter set through the DS_UPLOAD_REQ Event. */
static void test_system_cmd_param_download_store(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    const uint8_t download_start = IOLINK_CMD_PARAM_DOWNLOAD_START;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &download_start, 1), 1);
    iolink_isdu_process(&ctx);
    iolink_ds_process(&ds); /* DOWNLOAD_REQ -> DOWNLOADING */

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    const uint8_t store = IOLINK_CMD_PARAM_DOWNLOAD_STORE;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &store, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    iolink_event_t event;
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    assert_int_equal(event.type, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_false(iolink_events_pop(&events, &event));
}

/* ParamDownloadEnd does not start the upload. */
static void test_system_cmd_param_download_end_no_upload_request(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    const uint8_t download_end = IOLINK_CMD_PARAM_DOWNLOAD_END;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &download_end, 1), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    iolink_event_t event;
    assert_false(iolink_events_pop(&events, &event));
}

static void test_system_cmd_param_download_store_rejects_invalid_image(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    ds.image[0] = 0x00U;
    ds.image[1] = IOLINK_IDX_APPLICATION_TAG;
    ds.image[2] = 0x00U;
    ds.image[3] = 0xFFU;
    ds.image_len = 4U;

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    const uint8_t store = IOLINK_CMD_PARAM_DOWNLOAD_STORE;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &store, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t response[2];
    assert_int_equal(isdu_collect_response(&ctx, response, sizeof(response)), 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    iolink_event_t event;
    assert_false(iolink_events_pop(&events, &event));
}

static void test_system_cmd_param_break(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Set DS to uploading state first */
    ds.state = IOLINK_DS_STATE_UPLOADING;

    /* Write 0x06 (ParamBreak) to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_BREAK};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state was aborted to idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_invalid(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write invalid command 0xFF to Index 0x0002 */
    uint8_t cmd_data[] = {0xFF};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify error response: negative payload {0x80, AdditionalCode}. */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x35U); /* FUNC_NOTAVAIL (Table C.1 / C.2.14). */
}

static void test_isdu_function_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write Function Tag */
    const char* test_tag = "TestFunction";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(
        isdu_send_write_request(&ctx, 0x0019, 0x00, (const uint8_t*) test_tag, tag_len), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */

    /* Read Function Tag back */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x0019, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Collect Response */
    char readback[33] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) readback, sizeof(readback) - 1);

    assert_int_equal(len, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_location_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write Location Tag */
    const char* test_tag = "Building-A";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(
        isdu_send_write_request(&ctx, 0x001A, 0x00, (const uint8_t*) test_tag, tag_len), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */

    /* Read Location Tag back */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x001A, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Collect Response */
    char readback[33] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) readback, sizeof(readback) - 1);

    assert_int_equal(len, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_process_data_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Wire a DLL context holding PDin of 3 octets and PDout of 1 octet. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 3, 1), 0);
    dll.pd_in[0] = 0xAAU;
    dll.pd_in[1] = 0xBBU;
    dll.pd_in[2] = 0xCCU;
    /* The PD status flag is reported through the CKS bit (A.1.5) and does not
       gate 0x0028; the buffer is served either way (see the invalid-PD test). */
    dll.pd_valid = true;
    dll.pd_out[0] = 0x11U;
    ctx.dll_ctx = &dll;

    /* Read ProcessDataInput (Index 0x0028): Process Data of the PDin channel. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(ctx.response_len, 3);
    uint8_t pdi[3];
    assert_int_equal(isdu_collect_response(&ctx, pdi, sizeof(pdi)), 3);
    assert_int_equal(pdi[0], 0xAA);
    assert_int_equal(pdi[1], 0xBB);
    assert_int_equal(pdi[2], 0xCC);

    /* Read ProcessDataOutput (Index 0x0029): regression for the pre-dispatch
       index validation, which must not answer IDX_NOTAVAIL for 0x0029. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(ctx.response_len, 1);
    uint8_t pdo[1];
    assert_int_equal(isdu_collect_response(&ctx, pdo, sizeof(pdo)), 1);
    assert_int_equal(pdo[0], 0x11);

    /* Both objects are read-only and single-element: a WRITE is access-denied
       and a non-zero subindex is not available. */
    const uint16_t read_only[] = {IOLINK_IDX_PROCESS_DATA_INPUT, IOLINK_IDX_PROCESS_DATA_OUTPUT};
    uint8_t cmd_data[] = {0x05};
    uint8_t byte;
    for (size_t i = 0U; i < (sizeof(read_only) / sizeof(read_only[0])); i++) {
        print_message("read-only Process Data index 0x%04X\n", read_only[i]);
        iolink_isdu_init(&ctx);
        ctx.dll_ctx = &dll;
        assert_int_equal(isdu_send_write_request(&ctx, read_only[i], 0x00, cmd_data, 1), 1);
        iolink_isdu_process(&ctx);
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, 0x80);
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);

        iolink_isdu_init(&ctx);
        ctx.dll_ctx = &dll;
        assert_int_equal(isdu_send_read_request(&ctx, read_only[i], 0x01), 1);
        iolink_isdu_process(&ctx);
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, 0x80);
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }
}

/* B.2.23 does not define the answer while the PD status is invalid. The stack
 * follows the reference ST implementation, which keeps serving the Process
 * Data buffer; the validity itself is carried by the CKS PD-status bit
 * (A.1.5, Table A.5). */
static void test_isdu_pdin_invalid_serves_process_data(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(iolink_dll_set_pd_length(&dll, 2, 1), 0);
    dll.pd_in[0] = 0xDEU;
    dll.pd_in[1] = 0xADU;
    dll.pd_valid = false;
    ctx.dll_ctx = &dll;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[2];
    assert_int_equal(ctx.response_len, 2);
    assert_int_equal(isdu_collect_response(&ctx, data, sizeof(data)), 2);
    assert_int_equal(data[0], 0xDE);
    assert_int_equal(data[1], 0xAD);
}

/* B.2.24 is silent before the first PDout cycle; following the reference ST
 * implementation the configured-length PDout buffer is served, not an error. */
static void test_isdu_pdout_before_first_frame_serves_buffer(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(iolink_dll_set_pd_length(&dll, 0, 2), 0);
    ctx.dll_ctx = &dll;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[2] = {0xFFU, 0xFFU};
    assert_int_equal(ctx.response_len, 2);
    assert_int_equal(isdu_collect_response(&ctx, data, sizeof(data)), 2);
    assert_int_equal(data[0], 0U);
    assert_int_equal(data[1], 0U);
}

static void test_isdu_zero_length_process_data_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* A device without PD in or out answers with an empty positive response,
       not with an error. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 0, 0), 0);
    dll.pd_valid = true;
    ctx.dll_ctx = &dll;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    const uint8_t guard = 0xFFU;
    uint8_t buf[1] = {guard};
    assert_int_equal(ctx.response_len, 0);
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 0);
    assert_int_equal(buf[0], guard);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(ctx.response_len, 0);
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 0);
    assert_int_equal(buf[0], guard);

    /* Without a bound DLL the objects answer with an empty positive response. */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(ctx.response_len, 0);
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 0);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(ctx.response_len, 0);
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 0);
}

/* Device path: 0x0028 serves exactly the snapshot published with
 * iolink_device_pd_input_update(), and a runtime length change is reflected by
 * the next read (B.2.23: length and structure of the PDin channel). 0x0029
 * serves the zero-initialised PDout buffer until the first PDout frame. */
static void test_isdu_process_data_device_api(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    (void) memset(&dev, 0, sizeof(dev));
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    dev.cfg.phy = g_phy_mock;
    dev.cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dev.cfg.stack.pd_in_len = 3U;
    dev.cfg.stack.pd_out_len = 2U;
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), 0);

    iolink_isdu_ctx_t* isdu = &dev.ctx.dll.isdu;

    const uint8_t in3[3] = {0x01U, 0x02U, 0x03U};
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, in3, sizeof(in3), true), 0);
    assert_int_equal(isdu_send_read_request(isdu, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(isdu);

    assert_int_equal(isdu->response_len, 3);
    uint8_t pdi[3];
    assert_int_equal(isdu_collect_response(isdu, pdi, sizeof(pdi)), 3);
    assert_memory_equal(pdi, in3, sizeof(in3));

    assert_int_equal(isdu_send_read_request(isdu, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00), 1);
    iolink_isdu_process(isdu);

    assert_int_equal(isdu->response_len, 2);
    uint8_t pdo[2] = {0xFFU, 0xFFU};
    assert_int_equal(isdu_collect_response(isdu, pdo, sizeof(pdo)), 2);
    assert_int_equal(pdo[0], 0U);
    assert_int_equal(pdo[1], 0U);

    /* A shorter runtime length is served immediately; an update marked invalid
       still publishes the snapshot (B.2.23 is silent, ST reference behaviour). */
    const uint8_t in2[2] = {0x11U, 0x22U};
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, in2, sizeof(in2), false), 0);
    assert_int_equal(isdu_send_read_request(isdu, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(isdu);

    uint8_t pdi2[2];
    assert_int_equal(isdu_collect_response(isdu, pdi2, sizeof(pdi2)), 2);
    assert_memory_equal(pdi2, in2, sizeof(in2));
}

/* B.2.24: the served value is the PDout of the process data channel. Feed a
 * write M-sequence and verify that the DLL stores its PDout octets and that
 * 0x0029 returns them (covers the frame -> pd_out -> ISDU path). */
static void test_isdu_process_data_output_from_frame(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 2, .pd_out_len = 2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    /* Write M-sequence: MC, CKT, PDout (0x11 0x22), OD. */
    uint8_t frame[] = {0x00, IOLINK_MSEQ_TYPE_1, 0x11, 0x22, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Reply to a write M-sequence: PD-in (2 octets) + CKS. */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3);
    will_return(mock_phy_send, 0);

    iolink_device_process(&dev.ctx);

    iolink_isdu_ctx_t* isdu = &dev.ctx.dll.isdu;
    assert_int_equal(isdu_send_read_request(isdu, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00), 1);
    iolink_isdu_process(isdu);

    assert_int_equal(isdu->response_len, 2);
    uint8_t pdo[2] = {0U, 0U};
    assert_int_equal(isdu_collect_response(isdu, pdo, sizeof(pdo)), 2);
    assert_int_equal(pdo[0], 0x11U);
    assert_int_equal(pdo[1], 0x22U);
}

static void test_isdu_profile_characteristic_arrayt(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t with_profile;
    iolink_device_info_t no_profile;
    iolink_device_info_ctx_t with_ctx;
    iolink_device_info_ctx_t no_ctx;
    const uint8_t pid_write[2] = {0x12U, 0x34U};
    uint8_t byte;

    /* Bind the identities to the ISDU context instead of mutating the legacy
       singleton, so the sub-cases stay independent of their execution order. */
    (void) memset(&with_profile, 0, sizeof(with_profile));
    with_profile.profile_characteristic = 0x1234U;
    (void) memset(&no_profile, 0, sizeof(no_profile));
    iolink_device_info_ctx_init(&with_ctx, &with_profile);
    iolink_device_info_ctx_init(&no_ctx, &no_profile);
    iolink_device_info_init(NULL);
    iolink_params_init();

    /* B.2.5: ArrayT of UIntegerT16; subindex 0 returns the single element. */
    iolink_isdu_init(&ctx);
    ctx.device_info = &with_ctx;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t buf[2];
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x12);
    assert_int_equal(buf[1], 0x34);

    /* Subindex > 1 is out of range for a single-element array. */
    iolink_isdu_init(&ctx);
    ctx.device_info = &with_ctx;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x02), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    /* The object is read-only: a WRITE is access-denied (C.2.8). */
    iolink_isdu_init(&ctx);
    ctx.device_info = &with_ctx;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x01,
                                             pid_write, sizeof(pid_write)),
                     1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    /* A device without a profile does not expose the conditional parameter
       (C.2.3: the object does not exist), for reads, writes and any subindex. */
    iolink_isdu_init(&ctx);
    ctx.device_info = &no_ctx;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);

    iolink_isdu_init(&ctx);
    ctx.device_info = &no_ctx;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x00, pid_write, 2), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);

    iolink_isdu_init(&ctx);
    ctx.device_info = &no_ctx;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x02), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

static void test_isdu_data_storage_round_trip(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    /* Configure a known ApplicationTag. */
    const char* app = "ISDU-DS";
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) app, strlen(app), true),
        0);

    /* Upload (read 0x0003): Master backs up the serialized parameter image. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t image[IOLINK_DS_IMAGE_MAX];
    int img_len = isdu_collect_response(&ctx, image, sizeof(image));
    assert_true(img_len > 0);

    /* Wipe parameters. */
    iolink_params_factory_reset();
    uint8_t buf[40];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 0);

    /* Download (write 0x0003): Master restores the backed-up image. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00, image, (uint8_t) img_len), 1);
    iolink_isdu_process(&ctx);

    /* ApplicationTag restored over the wire. */
    int len = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(app));
    assert_memory_equal(buf, app, strlen(app));
}

static void test_isdu_direct_parameters_page1(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.min_cycle_time = 63U; /* 6.3 ms -> Table B.3 time base 00, multiplier 63 */
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Whole Direct Parameter page 1 (16 bytes). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t page[16];
    assert_int_equal(isdu_collect_response(&ctx, page, sizeof(page)), 16);

    const iolink_device_info_t* configured = iolink_device_info_get();
    assert_int_equal(page[0x02], 0x3FU);         /* MinCycleTime (Table B.3) */
    assert_int_equal(page[0x04], 0x11);          /* RevisionID = v1.1 */
    assert_int_equal(page[0x03] & 0x01U, 0x01U); /* ISDU supported */
    assert_int_equal(page[0x07], (uint8_t) (configured->vendor_id >> 8));
    assert_int_equal(page[0x08], (uint8_t) (configured->vendor_id & 0xFFU));
    assert_int_equal(page[0x09], (uint8_t) ((configured->device_id >> 16) & 0xFFU));
    assert_int_equal(page[0x0A], (uint8_t) ((configured->device_id >> 8) & 0xFFU));
    assert_int_equal(page[0x0B], (uint8_t) (configured->device_id & 0xFFU));

    /* Single-octet read via subindex (addr 0x04 -> subindex 5). */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05), 1);
    iolink_isdu_process(&ctx);
    uint8_t b[2];
    assert_int_equal(isdu_collect_response(&ctx, b, sizeof(b)), 1);
    assert_int_equal(b[0], 0x11);

    /* Page 1 is read-only. */
    iolink_isdu_init(&ctx);
    uint8_t w = 0x55;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x01, &w, 1), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &w), 1);
    assert_int_equal(w, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &w), 1);
    assert_int_equal(w, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_direct_parameters_page2(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_isdu_init(&ctx);

    /* Write the whole page 2, then read it back. */
    uint8_t img[16];
    for (uint8_t i = 0U; i < 16U; i++) {
        img[i] = (uint8_t) (0xA0U + i);
    }
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00, img, 16),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t rb[16];
    assert_int_equal(isdu_collect_response(&ctx, rb, sizeof(rb)), 16);
    assert_memory_equal(rb, img, 16);

    /* Single-octet write at subindex 3, read back. */
    iolink_isdu_init(&ctx);
    uint8_t one = 0x5A;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x03, &one, 1),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x03), 1);
    iolink_isdu_process(&ctx);
    uint8_t one_rb[2];
    assert_int_equal(isdu_collect_response(&ctx, one_rb, sizeof(one_rb)), 1);
    assert_int_equal(one_rb[0], 0x5A);
}

static void test_isdu_vendor_device_id_not_available(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Table B.8: 0x000A/0x000B are reserved; VendorID/DeviceID live in DPP1
       (0x07-0x0B), not in an ISDU index. A read returns IDX_NOTAVAIL. */
    uint8_t byte;
    assert_int_equal(isdu_send_read_request(&ctx, 0x000AU, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);

    assert_int_equal(isdu_send_read_request(&ctx, 0x000BU, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

static void test_isdu_reserved_indices_negative(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 0x001C..0x001F are not used (C5); a read is a negative response. */
    assert_int_equal(isdu_send_read_request(&ctx, 0x001CU, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

/* C.2.3: a non-existent index answers IDX_NOTAVAIL, not SUBIDX_NOTAVAIL, even
 * when the request also carries a non-zero subindex. */
static void test_isdu_reserved_index_with_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, 0x000AU, 0x01U), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

/* B.2.4: DeviceAccessLocks is accessible through subindex 0 only. */
static void test_isdu_access_locks_rejects_nonzero_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x01U), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* Table B.14: 0x0025 is an ArrayT, but subindex access is not supported. */
static void test_isdu_detailed_status_rejects_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_event_trigger(&events, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&events, 0x8CA0U, IOLINK_EVENT_TYPE_WARNING);

    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    /* Subindex 0 returns the complete list. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00U), 1);
    iolink_isdu_process(&ctx);
    uint8_t all[6];
    assert_int_equal(isdu_collect_response(&ctx, all, sizeof(all)), 6);
    assert_int_equal(all[1], 0x10U);
    assert_int_equal(all[4], 0x8CU);

    /* Any non-zero subindex is rejected, even when the corresponding array
       element exists. */
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x01U), 1);
    iolink_isdu_process(&ctx);
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* This stack serves the serialized Data Storage image through subindex 0 and
 * does not implement the B.10 record items (DS_Command, State_Property, ...);
 * a non-zero subindex is therefore SUBIDX_NOTAVAIL. */
static void test_isdu_data_storage_subindex_not_available(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01U), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* B.2.5/Table F.17: ProfileCharacteristic is an ArrayT with one element in
 * this stack; subindex 1 carries the same two octets as subindex 0. */
static void test_isdu_profile_characteristic_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    iolink_device_info_ctx_t info_ctx;

    (void) memset(&info, 0, sizeof(info));
    info.profile_characteristic = 0x1234U;
    iolink_device_info_ctx_init(&info_ctx, &info);
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x01U), 1);
    iolink_isdu_process(&ctx);
    uint8_t element[2] = {0xFFU, 0xFFU};
    assert_int_equal(isdu_collect_response(&ctx, element, sizeof(element)), 2);
    assert_int_equal(element[0], 0x12);
    assert_int_equal(element[1], 0x34);

    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x02U), 1);
    iolink_isdu_process(&ctx);
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

static void test_isdu_system_command_read_only_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* SystemCommand (0x0002) is write-only (Table B.8): a read is answered
       with IDX_NOT_ACCESSIBLE 0x80 0x23 (Table C.1). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);

    /* A write to 0x0002 still executes the command. */
    iolink_isdu_init(&ctx);
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, cmd_data, 1),
                     1);
    iolink_isdu_process(&ctx);
    assert_true(ctx.reset_pending);
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0);
}

static uint8_t page1_octet3(uint8_t m_seq_type, uint8_t pd_in, uint8_t pd_out)
{
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.m_seq_type = m_seq_type;
    assert_int_equal(iolink_dll_set_pd_length(&dll, pd_in, pd_out), 0);
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    return iolink_isdu_direct_param_page1_octet(&ctx, 0x03U);
}

static void test_isdu_mseq_capability_codes(void** state)
{
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();

    /* Table A.10 / plan Task 6. */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_2_1, 1, 0), 0x01U); /* OD1, code 0 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_1_2, 0, 0), 0x03U); /* OD2, code 1 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_1_V, 0, 0), 0x0DU); /* OD8, code 6 */
    /* PREOPERATE bits 4-5 = 0 for TYPE_0. */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_0, 0, 0) & 0x30U, 0x00U);
}

static void test_isdu_spec_index_constants(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x0025U);
    assert_int_equal(IOLINK_IDX_DEVICE_STATUS, 0x0024U);
    assert_int_equal(IOLINK_IDX_ERROR_COUNT, 0x0020U);
    assert_int_equal(IOLINK_IDX_ERROR_STATS, 0x0040U);
}

static void test_isdu_write_rejects_read_only_identification(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Table C.1: a WRITE to a read-only identification object is access
       denied (0x80 0x23). */
    uint8_t data[] = {0x41U, 0x42U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_VENDOR_NAME, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_param_lock_blocks_tag_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    uint8_t data[] = {0x41U, 0x42U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static unsigned g_vendor_writes = 0U;

static uint8_t vendor_write_spy(void* user, uint16_t index, uint8_t subindex, bool write,
                                const uint8_t* input, size_t length, uint8_t* output,
                                size_t* capacity)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) input;
    (void) length;
    (void) output;
    if (write) {
        g_vendor_writes++;
    }
    *capacity = 0U;
    return 0U;
}

/* B.2.4/10.6.5: the Parameter access lock covers every writable parameter, not
 * only the identification tags. */
static void test_isdu_param_lock_blocks_dpp2_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t page2[16] = {0U};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.direct_param_page2 = page2;

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    uint8_t data[] = {0x11U, 0x22U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(page2[0], 0U);
    assert_int_equal(page2[1], 0U);
}

/* B.2.27: device-specific parameters are writable parameters as well. */
static void test_isdu_param_lock_blocks_vendor_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.vendor_service = vendor_write_spy;

    g_vendor_writes = 0U;
    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    const uint8_t value = 0x01U;
    assert_int_equal(isdu_send_write_request(&ctx, 0x0100U, 0x00, &value, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(g_vendor_writes, 0U);
}

/* B.2.4: DeviceAccessLocks stays writable, and neither SystemCommand nor the
 * Data Storage download are parameters. */
static void test_isdu_param_lock_exempt_writes(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    uint8_t locks[2] = {0x00U, 0x01U}; /* keep the Parameter lock set */
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00, locks, 2),
                     1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_get_access_locks(), IOLINK_LOCK_PARAM);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    const uint8_t reset_cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &reset_cmd, 1),
                     1);
    iolink_isdu_process(&ctx);
    assert_true(ctx.app_reset_pending);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00, NULL, 0), 1);
    iolink_isdu_process(&ctx);
    uint8_t image_resp[2];
    const int image_len = isdu_collect_response(&ctx, image_resp, sizeof(image_resp));
    assert_true(image_len == 0);
}

static void test_isdu_error_count_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* ErrorCount (0x0020) is a 2-octet read-only object; zero without a DLL. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t buf[2];
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0);
    assert_int_equal(buf[1], 0);
}

/* B.2.20: this Device reports the SDCI error counters since power-on/reset,
 * big-endian and saturated at 0xFFFF. */
static void test_isdu_error_count_sums_dll_errors(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    iolink_device_info_init(NULL);
    iolink_params_init();
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    dll.crc_errors = 2U;
    dll.framing_errors = 3U;
    dll.timeout_errors = 4U;
    dll.timing_errors = 1U;

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t buf[2];
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x00U);
    assert_int_equal(buf[1], 0x0AU);

    dll.crc_errors = 0x10000U;
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0xFFU);
    assert_int_equal(buf[1], 0xFFU);
}

static void test_isdu_product_uri_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.product_uri = "https://example.org/iolinki";
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00), 1);
    iolink_isdu_process(&ctx);

    char uri[64];
    int len = isdu_collect_response(&ctx, (uint8_t*) uri, sizeof(uri) - 1);
    assert_int_equal(len, (int) strlen("https://example.org/iolinki"));
    uri[len] = '\0';
    assert_string_equal(uri, "https://example.org/iolinki");
}

static void test_isdu_product_uri_absent(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL); /* default info: product_uri is NULL */
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* B.2.19: ProductURI is conditional. Without one, the object is not
       available and the read is a negative response (Table C.1). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

/* Blocker: the ISDU engine must read the identity and access locks from the
 * device-local context, not from the process-wide legacy singleton. */
static void test_isdu_uses_bound_device_info(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    iolink_device_info_ctx_t info_ctx;

    (void) memset(&info, 0, sizeof(info));
    info.product_uri = "urn:iolinki:bound";
    iolink_device_info_ctx_init(&info_ctx, &info);
    iolink_device_info_init(NULL); /* The legacy identity has no ProductURI. */
    iolink_params_init();

    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00), 1);
    iolink_isdu_process(&ctx);

    char uri[64];
    const int len = isdu_collect_response(&ctx, (uint8_t*) uri, sizeof(uri) - 1);
    assert_int_equal(len, (int) strlen("urn:iolinki:bound"));
    uri[len] = '\0';
    assert_string_equal(uri, "urn:iolinki:bound");
}

/* Blocker: a lock set on the device's own device_info context must protect the
 * parameters, and a lock set only on the legacy singleton must not. */
static void test_isdu_param_lock_uses_bound_device_info(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_ctx_t info_ctx;

    iolink_device_info_init(NULL);
    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_init();
    iolink_device_info_ctx_set_access_locks(&info_ctx, IOLINK_LOCK_PARAM);

    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;

    const uint8_t data[] = {0x41U, 0x42U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80U);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    /* The reverse: an unlocked device context wins over a locked legacy one. */
    iolink_device_info_ctx_set_access_locks(&info_ctx, 0U);
    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
}

/* Table B.14/B.2.22: writing a read-only object must produce a framed negative
 * confirmation, not a silently dropped response. */
static void test_isdu_write_read_only_objects_negative_confirm(void** state)
{
    (void) state;
    const uint16_t read_only[] = {IOLINK_IDX_DETAILED_DEVICE_STATUS, IOLINK_IDX_ERROR_STATS};

    for (size_t i = 0U; i < (sizeof(read_only) / sizeof(read_only[0])); i++) {
        iolink_isdu_ctx_t ctx;
        iolink_events_ctx_t events;
        iolink_device_info_init(NULL);
        iolink_params_init();
        iolink_events_init(&events);
        iolink_isdu_init(&ctx);
        ctx.event_ctx = &events;

        const uint8_t value = 0x00U;
        assert_int_equal(isdu_send_write_request(&ctx, read_only[i], 0x00U, &value, 1), 1);
        iolink_isdu_process(&ctx);

        uint8_t resp[2];
        assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
        assert_int_equal(resp[0], 0x80U);
        assert_int_equal(resp[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    }
}

/* B.2.4: the 16-bit DeviceAccessLocks object rejects a short write and keeps
 * the previous value. */
static void test_isdu_access_locks_rejects_short_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_ctx_t info_ctx;
    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_init();
    iolink_device_info_ctx_set_access_locks(&info_ctx, IOLINK_LOCK_PARAM);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;

    const uint8_t one_byte = 0x01U;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, &one_byte, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_PARAM_LEN_UNDERRUN);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_PARAM);
}

/* C.2.12: a write longer than the two-octet object is a length overrun. */
static void test_isdu_access_locks_rejects_overlong_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_ctx_t info_ctx;
    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;

    const uint8_t three_bytes[3] = {0x00U, 0x01U, 0x02U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, three_bytes, 3), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_PARAM_LEN_OVERRUN);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), 0U);
}

/* Table B.12: bits 4-15 of DeviceAccessLocks are reserved and must not be
 * stored (they would also collide with the 0x80 negative-response prefix). */
static void test_isdu_access_locks_masks_reserved_bits(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_ctx_t info_ctx;
    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;

    const uint8_t lock_write[2] = {0x80U, 0x01U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, lock_write, 2), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x00U);
    assert_int_equal(resp[1], IOLINK_LOCK_PARAM);
}

/* Table 101: Application reset, Restore factory settings and Back-to-box
 * restore the access locks to the state of delivery (a non-zero factory value)
 * and persist it; Device reset does not change them. */
static void test_isdu_reset_commands_restore_default_access_locks(void** state)
{
    (void) state;
    const uint8_t reset_to_default[] = {
        IOLINK_CMD_APPLICATION_RESET, IOLINK_CMD_RESTORE_FACTORY_SETTINGS, IOLINK_CMD_BACK_TO_BOX};

    for (size_t i = 0U; i < (sizeof(reset_to_default) / sizeof(reset_to_default[0])); i++) {
        iolink_isdu_ctx_t ctx;
        iolink_device_info_t info;
        iolink_device_info_ctx_t info_ctx;
        iolink_params_ctx_t params;

        (void) memset(&info, 0, sizeof(info));
        info.access_locks = IOLINK_LOCK_DS; /* state of delivery */

        iolink_nvm_mock_cleanup();
        iolink_device_info_ctx_init(&info_ctx, &info);
        iolink_params_ctx_init(&params, &info_ctx);

        iolink_isdu_init(&ctx);
        ctx.device_info = &info_ctx;
        ctx.params_ctx = &params;

        /* The Master locks the Parameter access and persists it. */
        const uint8_t lock_write[2] = {0x00U, IOLINK_LOCK_PARAM};
        assert_int_equal(
            isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, lock_write, 2), 1);
        iolink_isdu_process(&ctx);
        uint8_t resp[2];
        assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
        assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_PARAM);

        assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U,
                                                 &reset_to_default[i], 1),
                         1);
        iolink_isdu_process(&ctx);
        assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
        assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_DS);

        /* The restored default is persisted: a power cycle must not resurrect
           the previous lock value. */
        iolink_device_info_ctx_init(&info_ctx, &info);
        iolink_params_ctx_init(&params, &info_ctx);
        assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_DS);
    }

    /* Device reset (0x80) leaves the access locks untouched ("No"). */
    {
        iolink_isdu_ctx_t ctx;
        iolink_device_info_ctx_t info_ctx;
        iolink_device_info_ctx_init(&info_ctx, NULL);
        iolink_params_init();
        iolink_device_info_ctx_set_access_locks(&info_ctx, IOLINK_LOCK_PARAM);
        iolink_isdu_init(&ctx);
        ctx.device_info = &info_ctx;
        const uint8_t cmd = IOLINK_CMD_DEVICE_RESET;
        assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1),
                         1);
        iolink_isdu_process(&ctx);
        assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_PARAM);
    }
}

/* The device-local identity/access locks must not leak between device contexts. */
static void test_isdu_access_locks_are_per_device(void** state)
{
    (void) state;
    iolink_device_info_ctx_t info_a;
    iolink_device_info_ctx_t info_b;
    iolink_isdu_ctx_t ctx_a;
    iolink_isdu_ctx_t ctx_b;

    iolink_device_info_ctx_init(&info_a, NULL);
    iolink_device_info_ctx_init(&info_b, NULL);
    iolink_params_init();
    iolink_device_info_ctx_set_access_locks(&info_a, IOLINK_LOCK_PARAM);

    iolink_isdu_init(&ctx_a);
    ctx_a.device_info = &info_a;
    iolink_isdu_init(&ctx_b);
    ctx_b.device_info = &info_b;

    const uint8_t tag[] = {0x41U};
    assert_int_equal(
        isdu_send_write_request(&ctx_a, IOLINK_IDX_APPLICATION_TAG, 0x00U, tag, sizeof(tag)), 1);
    iolink_isdu_process(&ctx_a);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx_a, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    assert_int_equal(
        isdu_send_write_request(&ctx_b, IOLINK_IDX_APPLICATION_TAG, 0x00U, tag, sizeof(tag)), 1);
    iolink_isdu_process(&ctx_b);
    assert_int_equal(isdu_collect_response(&ctx_b, resp, sizeof(resp)), 0);
}

/* A corrupt access-lock record must be ignored, not applied. */
static void test_isdu_access_locks_corrupt_nvm_record_ignored(void** state)
{
    (void) state;
    iolink_device_info_ctx_t info_ctx;
    iolink_params_ctx_t params;
    uint8_t garbage[8];

    (void) memset(garbage, 0xFF, sizeof(garbage));
    assert_int_equal(iolink_nvm_write(IOLINK_NVM_LOCKS_OFFSET, garbage, sizeof(garbage)), 0);

    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_ctx_init(&params, &info_ctx);

    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), 0U);
}

/* A failed persistent write must be reported and must not change the lock value. */
static void test_isdu_access_locks_write_failure_reported(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_ctx_t info_ctx;
    iolink_params_ctx_t params;

    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_ctx_init(&params, &info_ctx);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;
    ctx.params_ctx = &params;

    iolink_nvm_mock_fail_writes(true);
    const uint8_t lock_write[2] = {0x00U, IOLINK_LOCK_PARAM};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, lock_write, 2), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_APP_DEVICE);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), 0U);
    iolink_nvm_mock_fail_writes(false);
}

/* Table 101 "Default": a reset whose lock commit fails is reported instead of
 * confirming a change that a power cycle would undo. */
static void test_isdu_reset_lock_commit_failure_reported(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    iolink_device_info_ctx_t info_ctx;
    iolink_params_ctx_t params;

    (void) memset(&info, 0, sizeof(info));
    info.access_locks = IOLINK_LOCK_DS;
    iolink_device_info_ctx_init(&info_ctx, &info);
    iolink_params_ctx_init(&params, &info_ctx);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;
    ctx.params_ctx = &params;
    iolink_device_info_ctx_set_access_locks(&info_ctx, IOLINK_LOCK_PARAM);

    iolink_nvm_mock_fail_writes(true);
    const uint8_t cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_APP_DEVICE);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_PARAM);
    assert_false(ctx.app_reset_pending);
    iolink_nvm_mock_fail_writes(false);
}

/* B.2.4/Table 101: the access locks are non-volatile. */
static void test_isdu_access_locks_persist_across_reinit(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_ctx_t info_ctx;
    iolink_params_ctx_t params;

    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_ctx_init(&params, &info_ctx);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info_ctx;
    ctx.params_ctx = &params;

    const uint8_t locks[2] = {0x00U, IOLINK_LOCK_PARAM};
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, locks, 2),
                     1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_PARAM);

    /* Power cycle: both managers are rebuilt from the non-volatile record. */
    iolink_device_info_ctx_init(&info_ctx, NULL);
    iolink_params_ctx_init(&params, &info_ctx);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(&info_ctx), IOLINK_LOCK_PARAM);
}

/* Table 101: Application reset makes a Data Storage upload required. */
static void test_system_cmd_application_reset_requests_ds_upload(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    const uint8_t cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
    iolink_event_t event;
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    assert_int_equal(event.type, IOLINK_EVENT_TYPE_NOTIFICATION);
}

/* Table 101: Restore factory settings and Back-to-box delete a pending Data
 * Storage upload request (DS_UPLOAD_REQ = 0). */
static void test_system_cmd_reset_commands_cancel_ds_upload_request(void** state)
{
    (void) state;
    const uint8_t cancel[] = {IOLINK_CMD_RESTORE_FACTORY_SETTINGS, IOLINK_CMD_BACK_TO_BOX};

    for (size_t i = 0U; i < (sizeof(cancel) / sizeof(cancel[0])); i++) {
        iolink_isdu_ctx_t ctx;
        iolink_ds_ctx_t ds;
        iolink_device_info_init(NULL);
        iolink_params_init();
        iolink_ds_init(&ds, NULL);
        ds.state = IOLINK_DS_STATE_UPLOAD_REQ;
        iolink_isdu_init(&ctx);
        ctx.ds_ctx = &ds;

        assert_int_equal(
            isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cancel[i], 1), 1);
        iolink_isdu_process(&ctx);
        assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    }
}

/* Table 101 "Discard": every reset command drops a staged download image, so a
 * later iolink_ds_process() cannot re-apply the pre-reset parameter set. */
static void test_system_cmd_reset_discards_staged_download_image(void** state)
{
    (void) state;
    const uint8_t cmds[] = {IOLINK_CMD_DEVICE_RESET, IOLINK_CMD_RESTORE_FACTORY_SETTINGS,
                            IOLINK_CMD_BACK_TO_BOX};

    for (size_t i = 0U; i < (sizeof(cmds) / sizeof(cmds[0])); i++) {
        iolink_isdu_ctx_t ctx;
        iolink_ds_ctx_t ds;
        iolink_device_info_init(NULL);
        iolink_params_init();
        iolink_ds_init(&ds, NULL);
        /* A real device image makes the consistency assertions meaningful (a
           zeroed context would make them trivially true). */
        assert_true(iolink_ds_build_image(&ds) > 0);
        ds.state = IOLINK_DS_STATE_DOWNLOADING;
        /* Staged image: ApplicationTag = "X". */
        ds.image[0] = 0x00U;
        ds.image[1] = IOLINK_IDX_APPLICATION_TAG;
        ds.image[2] = 0x00U;
        ds.image[3] = 0x01U;
        ds.image[4] = (uint8_t) 'X';
        ds.image_len = 5U;

        iolink_isdu_init(&ctx);
        ctx.ds_ctx = &ds;

        assert_int_equal(
            isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmds[i], 1), 1);
        iolink_isdu_process(&ctx);

        /* The staged image is gone and the image/checksum pair stays valid. */
        assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
        assert_true(iolink_ds_verify(&ds));

        /* DS_Data serves the device's own parameters, rebuilt after the reset
           (not the staged "X" and not a pre-reset snapshot). */
        size_t len = 0U;
        const uint8_t* img = iolink_ds_get_image(&ds, &len);
        assert_non_null(img);
        assert_int_equal(img[0], 0x00U);
        assert_int_equal(img[1], IOLINK_IDX_APPLICATION_TAG);
        if (cmds[i] == IOLINK_CMD_DEVICE_RESET) {
            /* Tags survive a Device reset: the factory default is still there. */
            assert_true(img[3] > 0U);
            assert_int_equal(img[4], (uint8_t) 'D');
        }
        else {
            /* Restore factory settings / Back-to-box clear the tags. */
            assert_int_equal(img[3], 0U);
        }

        /* A later ds_process() must not apply the staged "X". */
        iolink_ds_process(&ds);
        uint8_t tag[33];
        const int tag_len = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0x00U, tag, sizeof(tag));
        assert_true(tag_len >= 0);
        if (tag_len > 0) {
            assert_int_not_equal(tag[0], (uint8_t) 'X');
        }
    }
}

/* Table 101: Application reset discards an active transfer before it asks for
 * the upload; it must not announce an upload the DS engine refuses. */
static void test_system_cmd_application_reset_discards_busy_ds(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    ds.state = IOLINK_DS_STATE_UPLOADING;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    const uint8_t cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
    iolink_event_t event;
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
}

/* Without a DS engine there is nothing to upload: no 0xFF91 is announced. */
static void test_system_cmd_application_reset_without_ds_engine_quiet(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    const uint8_t cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    iolink_event_t event;
    assert_false(iolink_events_pop(&events, &event));
}

/* B.2.22: reading DetailedDeviceStatus without an event host still answers an
 * (empty) positive response instead of timing out. */
static void test_isdu_detailed_status_without_event_ctx_responds(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00U), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
}

/* Table 98 rule 7: a rejected ParamDownloadEnd must not leave the rejected
 * Master image behind for the DS_Data (0x0003) read path. */
static void test_system_cmd_param_download_end_failure_discards_image(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    assert_true(iolink_ds_build_image(&ds) > 0);
    const uint16_t device_checksum = ds.current_checksum;

    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    ds.image[0] = 0x00U;
    ds.image[1] = IOLINK_IDX_APPLICATION_TAG;
    ds.image[2] = 0x00U;
    ds.image[3] = 0xFFU; /* Truncated record payload. */
    ds.image_len = 4U;

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    const uint8_t cmd = IOLINK_CMD_PARAM_DOWNLOAD_END;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
    assert_int_not_equal(ds.image_len, 4U); /* not the rejected 4-octet image */

    /* The device keeps its own image and checksum. */
    size_t len = 0U;
    assert_non_null(iolink_ds_get_image(&ds, &len));
    assert_true(iolink_ds_verify(&ds));
    assert_int_equal(ds.current_checksum, device_checksum);
}

/* Table D.1: the internal DS_UPLOAD_REQ notification must not raise the
 * DeviceStatus reported through index 0x0024. */
static void test_system_cmd_param_download_store_keeps_device_status_zero(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    const uint8_t store = IOLINK_CMD_PARAM_DOWNLOAD_STORE;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &store, 1), 1);
    iolink_isdu_process(&ctx);
    assert_true(iolink_events_pending(&events));

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00U), 1);
    iolink_isdu_process(&ctx);
    uint8_t status = 0xFFU;
    assert_int_equal(isdu_collect_response(&ctx, &status, 1), 1);
    assert_int_equal(status, 0U);
}

/* Blocker regression: iolink_device_init() must bind the configured identity
 * and access locks into the ISDU engine instead of relying on the legacy
 * process-wide singleton. */
static void test_isdu_device_init_binds_device_info(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_device_info_t info;

    (void) memset(&info, 0, sizeof(info));
    info.product_uri = "urn:dev-init";
    info.access_locks = IOLINK_LOCK_PARAM;

    (void) memset(&dev, 0, sizeof(dev));
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    dev.cfg.phy = g_phy_mock;
    dev.cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dev.cfg.device_info = &info;
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), 0);

    iolink_isdu_ctx_t* isdu = &dev.ctx.dll.isdu;
    assert_int_equal(isdu_send_read_request(isdu, IOLINK_IDX_PRODUCT_URI, 0x00), 1);
    iolink_isdu_process(isdu);

    char uri[64];
    const int len = isdu_collect_response(isdu, (uint8_t*) uri, sizeof(uri) - 1);
    assert_int_equal(len, (int) strlen("urn:dev-init"));
    uri[len] = '\0';
    assert_string_equal(uri, "urn:dev-init");

    /* The configured Parameter lock protects the device's parameters. */
    const uint8_t tag[] = {0x41U};
    assert_int_equal(
        isdu_send_write_request(isdu, IOLINK_IDX_APPLICATION_TAG, 0x00, tag, sizeof(tag)), 1);
    iolink_isdu_process(isdu);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(isdu, resp, sizeof(resp)), 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

/* Legacy (context-less) configuration: Restore factory settings must keep the
 * configured state-of-delivery locks and persist that value, not overwrite the
 * NVM record with the parameter manager's private 0 default. */
static void test_params_legacy_factory_reset_keeps_configured_default_locks(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;

    (void) memset(&info, 0, sizeof(info));
    info.access_locks = IOLINK_LOCK_DS;
    iolink_device_info_init(&info);
    iolink_params_init();

    iolink_isdu_init(&ctx);
    const uint8_t lock_write[2] = {0x00U, IOLINK_LOCK_PARAM};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00U, lock_write, 2), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_get_access_locks(), IOLINK_LOCK_PARAM);

    iolink_isdu_init(&ctx);
    const uint8_t cmd = IOLINK_CMD_RESTORE_FACTORY_SETTINGS;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00U, &cmd, 1), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_get_access_locks(), IOLINK_LOCK_DS);

    /* The NVM record must hold the configured default, not the parameter
       manager's private 0. */
    uint8_t rec[8];
    assert_int_equal(iolink_nvm_read(IOLINK_NVM_LOCKS_OFFSET, rec, sizeof(rec)), 0);
    uint16_t stored = 0U;
    (void) memcpy(&stored, &rec[4], sizeof(stored));
    assert_int_equal(stored, IOLINK_LOCK_DS);

    /* The persisted value must match, or a power cycle resurrects the lock. */
    iolink_device_info_init(&info);
    iolink_params_init();
    assert_int_equal(iolink_device_info_get_access_locks(), IOLINK_LOCK_DS);
}

/* B.2.22: a cleared Error/Warning stays in DetailedDeviceStatus until its
 * "event disappears" is reported (the 10.10.2 spacing defers it); afterwards
 * the entry leaves the list. */
static void test_isdu_detailed_device_status_until_disappear(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);

    uint8_t record[3];
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, record, sizeof(record)), 3);

    /* The fault is gone, but the withdrawal has not reached the Master yet, so
       the diagnosis entry must still be reported. */
    iolink_event_clear(&events, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, record, sizeof(record)), 3);

    /* After the minimum appearance window the disappear is emitted and the
       entry drops out. */
    usleep((IOLINK_EVENT_MIN_APPEAR_MS + 20U) * 1000U);
    iolink_events_process(&events);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(ctx.response_len, 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_isdu_vendor_name_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_device_status_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_read, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_until_disappear,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_cr443_octet_order,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_stats_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_vendor_device_id_not_available, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reserved_indices_negative, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reserved_index_with_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_rejects_nonzero_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_status_rejects_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_data_storage_subindex_not_available, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_profile_characteristic_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_mseq_capability_codes, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_spec_index_constants, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_system_command_read_only_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_write_rejects_read_only_identification,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_blocks_tag_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_blocks_dpp2_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_blocks_vendor_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_exempt_writes, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_count_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_count_sums_dll_errors, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_product_uri_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_product_uri_absent, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_device_reset, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_factory_restore, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_resets_diagnostics, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_store_busy, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_factory_restore_clears_detailed_status,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_upload, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_break, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_store, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_end_no_upload_request,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_store_rejects_invalid_image,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_invalid, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_function_tag_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_location_tag_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_process_data_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_pdin_invalid_serves_process_data, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_pdout_before_first_frame_serves_buffer,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_zero_length_process_data_read, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_process_data_device_api, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_process_data_output_from_frame, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_profile_characteristic_arrayt, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_direct_parameters_page1, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_direct_parameters_page2, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_data_storage_round_trip, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_uses_bound_device_info, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_device_init_binds_device_info, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_uses_bound_device_info, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_write_read_only_objects_negative_confirm,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_rejects_short_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_rejects_overlong_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_masks_reserved_bits, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reset_commands_restore_default_access_locks,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_are_per_device, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_corrupt_nvm_record_ignored,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_write_failure_reported, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reset_lock_commit_failure_reported, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(
            test_params_legacy_factory_reset_keeps_configured_default_locks, test_setup,
            test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_persist_across_reinit, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset_requests_ds_upload,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_reset_commands_cancel_ds_upload_request,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_reset_discards_staged_download_image,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset_discards_busy_ds,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset_without_ds_engine_quiet,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_status_without_event_ctx_responds,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_end_failure_discards_image,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(
            test_system_cmd_param_download_store_keeps_device_status_zero, test_setup,
            test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
