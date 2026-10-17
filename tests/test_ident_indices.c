/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ident_indices.c
 * @brief Identification indices that the stack already serves: VendorID,
 *        ProfileCharacteristic, the identification strings and
 *        DeviceAccessLocks (0x000C) read/write.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/device_info.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static iolink_device_info_t make_info(void)
{
    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.vendor_id = 0x1234U;
    info.device_id = 0x010203U;
    info.profile_characteristic = 0x0001U;
    info.function_id = 0xABCDU;
    info.revision_id = 0x0111U;
    info.min_cycle_time = 0x0AU;

    info.vendor_name = "VendorX";
    info.vendor_text = "Vendor text";
    info.product_name = "Product name";
    info.product_id = "PID-42";
    info.product_text = "Product text";
    info.serial_number = "SN-0001";
    info.hardware_revision = "HW-1";
    info.firmware_revision = "FW-2";
    return info;
}

static int setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    iolink_device_info_t info = make_info();
    iolink_device_info_init(&info);
    iolink_params_init();
    return 0;
}

static int teardown(void** state)
{
    (void) state;
    iolink_device_info_init(NULL);
    iolink_nvm_mock_cleanup();
    return 0;
}

static void expect_read_value(uint16_t index, const uint8_t* expected, size_t expected_len)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, index, 0U), 1);
    iolink_isdu_process(&ctx);

    uint8_t out[64] = {0};
    int n = isdu_collect_response(&ctx, out, sizeof(out));
    assert_int_equal(n, (int) expected_len);
    assert_memory_equal(out, expected, expected_len);
}

static void expect_read_string(uint16_t index, const char* expected)
{
    expect_read_value(index, (const uint8_t*) expected, strlen(expected));
}

static void test_profile_characteristic_read(void** state)
{
    (void) state;
    const uint8_t expected[] = {0x00U, 0x01U};
    expect_read_value(IOLINK_IDX_PROFILE_CHARACTERISTIC, expected, sizeof(expected));
}

static void test_identification_strings(void** state)
{
    (void) state;

    expect_read_string(IOLINK_IDX_VENDOR_NAME, "VendorX");
    expect_read_string(IOLINK_IDX_VENDOR_TEXT, "Vendor text");
    expect_read_string(IOLINK_IDX_PRODUCT_NAME, "Product name");
    expect_read_string(IOLINK_IDX_PRODUCT_ID, "PID-42");
    expect_read_string(IOLINK_IDX_PRODUCT_TEXT, "Product text");
    expect_read_string(IOLINK_IDX_SERIAL_NUMBER, "SN-0001");
    expect_read_string(IOLINK_IDX_HARDWARE_REVISION, "HW-1");
    expect_read_string(IOLINK_IDX_FIRMWARE_REVISION, "FW-2");
}

static void test_device_access_locks_round_trip(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Default: all locks released. */
    const uint8_t unlocked[] = {0x00U, 0x00U};
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, unlocked, sizeof(unlocked));

    /* Write DeviceAccessLocks = DS lock | Local UI lock. */
    const uint8_t locks[] = {0x00U, (uint8_t) (IOLINK_LOCK_DS | IOLINK_LOCK_LOCAL_UI)};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, locks, sizeof(locks)), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    assert_int_equal(iolink_device_info_get_access_locks(),
                     (IOLINK_LOCK_DS | IOLINK_LOCK_LOCAL_UI));

    /* Read-back reflects the new lock word (MSB first). */
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, locks, sizeof(locks));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_profile_characteristic_read, setup, teardown),
        cmocka_unit_test_setup_teardown(test_identification_strings, setup, teardown),
        cmocka_unit_test_setup_teardown(test_device_access_locks_round_trip, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
