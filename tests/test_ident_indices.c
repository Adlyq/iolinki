/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ident_indices.c
 * @brief 协议栈已提供的标识类索引：VendorID、
 *        ProfileCharacteristic、标识字符串以及
 *        DeviceAccessLocks (0x000C) 读/写。
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

#include "conformance_test.h"

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
    iolink_device_info_t info = make_info();
    test_device_info_reset(&info);
    test_params_reset();
    return 0;
}

static int teardown(void** state)
{
    (void) state;
    test_device_info_reset(NULL);
    return 0;
}

static void expect_read_value(uint16_t index, const uint8_t* expected, size_t expected_len)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
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
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0111");
    (void) state;
    const uint8_t expected[] = {0x00U, 0x01U};
    expect_read_value(IOLINK_IDX_PROFILE_CHARACTERISTIC, expected, sizeof(expected));
}

static void test_identification_strings(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0114");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0115");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0116");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0117");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0118");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0119");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0120");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0121");
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
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0109");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);

    /* 默认：所有锁均已释放。 */
    const uint8_t unlocked[] = {0x00U, 0x00U};
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, unlocked, sizeof(unlocked));

    /* 写入 DeviceAccessLocks = DS 锁 | 本地 UI 锁。 */
    const uint8_t locks[] = {0x00U, (uint8_t) (IOLINK_LOCK_DS | IOLINK_LOCK_LOCAL_UI)};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, locks, sizeof(locks)), 1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    assert_int_equal(iolink_device_info_ctx_get_access_locks(test_device_info_ctx()),
                     (IOLINK_LOCK_DS | IOLINK_LOCK_LOCAL_UI));

    /* 回读反映新的锁字（MSB 在前）。 */
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, locks, sizeof(locks));

    /* 置位每个已实现的锁位（0..3）并校验原始字。 */
    const uint8_t all_locks[] = {0x00U, (uint8_t) IOLINK_ACCESS_LOCKS_IMPLEMENTED};
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, all_locks,
                                             sizeof(all_locks)),
                     1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(test_device_info_ctx()),
                     IOLINK_ACCESS_LOCKS_IMPLEMENTED);
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, all_locks, sizeof(all_locks));

    /* SDCI_TC_0109 步骤 c/d：写入 0x0000 释放所有锁，且回读
       值保持不变。 */
    const uint8_t zero[] = {0x00U, 0x00U};
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, zero, sizeof(zero)), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(test_device_info_ctx()), 0U);
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, zero, sizeof(zero));

    /* 位 0（Parameter）与位 1（Data Storage）已实现。对置位它们的写入
       返回肯定应答在测试规范中是警告而非
       失败；固定该被接受的值，然后再释放它。 */
    const uint8_t param_ds[] = {0x00U, (uint8_t) (IOLINK_LOCK_PARAM | IOLINK_LOCK_DS)};
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, param_ds,
                                             sizeof(param_ds)),
                     1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(test_device_info_ctx()),
                     (IOLINK_LOCK_PARAM | IOLINK_LOCK_DS));
    expect_read_value(IOLINK_IDX_DEVICE_ACCESS_LOCKS, param_ds, sizeof(param_ds));
    print_message("[warning] SDCI_TC_0109: positive write with bits 0/1 set\n");

    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, zero, sizeof(zero)), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(test_device_info_ctx()), 0U);
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
