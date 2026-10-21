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
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/protocol.h"
#include "iolinki/device_info.h"
#include "test_helpers.h"

static int test_setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    iolink_device_info_init(NULL);
    iolink_params_init();
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static int send_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    return isdu_send_write_request(ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U);
}

/* 0x83 Back-to-box restores factory defaults like 0x82. */
static void test_back_to_box_restores_defaults(void** state)
{
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

/* Reserved commands (0x07..0x3F, 0x84..0x9F) answer FUNC_NOTAVAIL. */
static void test_reserved_commands_rejected(void** state)
{
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

/* A second upload request while one is active is a temporary-unavailable Busy. */
static void test_ds_command_busy(void** state)
{
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
    int n = isdu_collect_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], IOLINK_ISDU_ERROR_BUSY);
}

/* B.2.4: the parameter (write) access lock shall NOT block Data Storage
 * downloads (between DS_DownloadStart and DS_DownloadEnd/Break). */
static void test_param_lock_does_not_block_ds_download(void** state)
{
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
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U,
                                             (const uint8_t*) "NEW", 3U),
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
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U,
                                             (const uint8_t*) "NEW", 3U),
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
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_back_to_box_restores_defaults, test_setup,
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
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
