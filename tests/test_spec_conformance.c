/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_spec_conformance.c
 * @brief Conformance assertions for the mandatory ISDU objects and access
 *        rules, checked against the bundled IO-Link V1.1.5 specification.
 *
 * Covers: reserved 0x000A/0x000B indices, ErrorCount (0x0020), ProductURI
 * (0x001B), ProcessDataInput/Output (0x0028/0x0029), read-only WRITE rejection
 * (Table C.1), the Parameter access lock (Table B.2.4) and the identification
 * string fixedLength limits (B.2.8-B.2.15).
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/dll.h"
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
    info.vendor_name = "VendorX";
    info.vendor_text = "Vendor text";
    info.product_name = "Product name";
    info.product_id = "PID-42";
    info.product_text = "Product text";
    info.serial_number = "SN-0001";
    info.hardware_revision = "HW-1";
    info.firmware_revision = "FW-2";
    info.product_uri = "https://example.com/device";
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

/* Read an object and copy the response into out; returns the response length. */
static int read_object(uint16_t index, uint8_t* out, size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, index, 0U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

/* Same, with a DLL context bound (needed by the process-data objects). */
static int read_object_with_dll(uint16_t index, iolink_dll_ctx_t* dll, uint8_t* out,
                                size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = dll;
    assert_int_equal(isdu_send_read_request(&ctx, index, 0U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

/* Accept every vendor-service access (used to exercise the parameter lock). */
static uint8_t vendor_service_ok(void* user, uint16_t index, uint8_t subindex, bool write,
                                 const uint8_t* input, size_t length, uint8_t* output,
                                 size_t* output_length)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) write;
    (void) input;
    (void) length;
    (void) output;
    *output_length = 0U;
    return 0U;
}

/* Write an object and copy the response; returns the response length. */
static int write_object(uint16_t index, const uint8_t* data, uint8_t len, uint8_t* out,
                        size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_write_request(&ctx, index, 0U, data, len), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

/* Table B.8: 0x0004-0x000B are reserved; VendorID/DeviceID live in DPP1. */
static void test_vendor_device_id_indices_reserved(void** state)
{
    (void) state;

    const uint16_t reserved[] = {
        0x0004U, 0x0005U, 0x0006U, 0x0007U, 0x0008U, 0x0009U, 0x000AU, 0x000BU,
        0x001CU, 0x001DU, 0x001EU, 0x001FU,
    };
    for (size_t i = 0U; i < (sizeof(reserved) / sizeof(reserved[0])); i++) {
        uint8_t out[8] = {0};
        int n = read_object(reserved[i], out, sizeof(out));
        assert_int_equal(n, 2);
        assert_int_equal(out[0], 0x80U);
        assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);

        /* C.2.3: a write to a non-existing index is also IDX_NOTAVAIL. */
        const uint8_t value = 0x5AU;
        n = write_object(reserved[i], &value, 1U, out, sizeof(out));
        assert_int_equal(n, 2);
        assert_int_equal(out[0], 0x80U);
        assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
}

/* Table B.8: ErrorCount (0x0020) is a 2-octet UIntegerT (must not be an error). */
static void test_error_count_index(void** state)
{
    (void) state;
    uint8_t out[8] = {0};
    int n = read_object(IOLINK_IDX_ERROR_COUNT, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_not_equal(out[0], 0x80U); /* served, not IDX_NOTAVAIL */
}

/* Table B.8 / B.2.19: ProductURI (0x001B) is served when a profile is declared. */
static void test_product_uri_index(void** state)
{
    (void) state;
    const char* expected = "https://example.com/device";
    uint8_t out[64] = {0};
    int n = read_object(IOLINK_IDX_PRODUCT_URI, out, sizeof(out));
    assert_int_equal(n, (int) strlen(expected));
    assert_memory_equal(out, expected, strlen(expected));
}

/* Table B.8 / B.2.23: ProcessDataInput (0x0028) returns the last valid PD input
 * data, not a descriptor/length byte. */
static void test_process_data_input_returns_pd(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.pd_in_len_current = 2U;
    dll.pd_in[0] = 0x11U;
    dll.pd_in[1] = 0x22U;

    uint8_t out[8] = {0};
    int n = read_object_with_dll(0x0028U, &dll, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x11U);
    assert_int_equal(out[1], 0x22U);
}

/* Table B.8 / B.2.24: ProcessDataOutput (0x0029) returns the last valid PD
 * output data written to the device. */
static void test_process_data_output_returns_pd(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.pd_out_len_current = 2U;
    dll.pd_out[0] = 0xAAU;
    dll.pd_out[1] = 0xBBU;

    uint8_t out[8] = {0};
    int n = read_object_with_dll(0x0029U, &dll, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0xAAU);
    assert_int_equal(out[1], 0xBBU);
}

/* Table C.1: WRITE to a read-only identification object is 0x80 0x23. */
static void test_read_only_identification_rejects_write(void** state)
{
    (void) state;

    const uint16_t read_only[] = {
        IOLINK_IDX_PROFILE_CHARACTERISTIC,
        IOLINK_IDX_VENDOR_NAME,
        IOLINK_IDX_VENDOR_TEXT,
        IOLINK_IDX_PRODUCT_NAME,
        IOLINK_IDX_PRODUCT_ID,
        IOLINK_IDX_PRODUCT_TEXT,
        IOLINK_IDX_SERIAL_NUMBER,
        IOLINK_IDX_HARDWARE_REVISION,
        IOLINK_IDX_FIRMWARE_REVISION,
        IOLINK_IDX_PRODUCT_URI,
        IOLINK_IDX_ERROR_COUNT,
        IOLINK_IDX_DEVICE_STATUS,
        IOLINK_IDX_DETAILED_DEVICE_STATUS,
        IOLINK_IDX_PROCESS_DATA_INPUT,
        IOLINK_IDX_PROCESS_DATA_OUTPUT,
    };

    for (size_t i = 0U; i < (sizeof(read_only) / sizeof(read_only[0])); i++) {
        uint8_t out[8] = {0};
        const uint8_t value = 0x5AU;
        int n = write_object(read_only[i], &value, 1U, out, sizeof(out));
        assert_int_equal(n, 2);
        assert_int_equal(out[0], 0x80U);
        assert_int_equal(out[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    }
}

/* DeviceAccessLocks bit 0: with the parameter lock set, parameter writes are
 * refused (Table C.1 access denied). */
static void test_parameter_lock_blocks_tag_write(void** state)
{
    (void) state;

    /* Establish a known value before enabling the lock. */
    const uint8_t original[] = {'X', 'Y', 'Z'};
    uint8_t out[8] = {0};
    int n = write_object(IOLINK_IDX_APPLICATION_TAG, original, sizeof(original), out, sizeof(out));
    assert_int_equal(n, 0);

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    const uint8_t overwrite[] = {'A', 'B', 'C'};
    n = write_object(IOLINK_IDX_APPLICATION_TAG, overwrite, sizeof(overwrite), out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    /* B.2.4: Read access is not affected and the locked write did not change
     * the stored value. */
    uint8_t readback[16] = {0};
    n = read_object(IOLINK_IDX_APPLICATION_TAG, readback, sizeof(readback));
    assert_int_equal(n, (int) sizeof(original));
    assert_memory_equal(readback, original, sizeof(original));
}

/* B.2.4: the Parameter lock also inhibits writes to device-specific
 * parameters; reads still reach the vendor service. */
static void test_parameter_lock_blocks_vendor_write(void** state)
{
    (void) state;

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.vendor_service = vendor_service_ok;
    const uint8_t value = 0x5AU;
    assert_int_equal(isdu_send_write_request(&ctx, 0x0040U, 0U, &value, 1U), 1);
    iolink_isdu_process(&ctx);

    uint8_t out[8] = {0};
    int n = isdu_collect_response(&ctx, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    iolink_isdu_init(&ctx);
    ctx.vendor_service = vendor_service_ok;
    assert_int_equal(isdu_send_read_request(&ctx, 0x0040U, 0U), 1);
    iolink_isdu_process(&ctx);
    n = isdu_collect_response(&ctx, out, sizeof(out));
    assert_int_equal(n, 0);
}

/* B.2.8-B.2.15: identification strings are truncated to their fixedLength
 * (64 octets, SerialNumber 16 octets). */
static void test_identification_string_length_limits(void** state)
{
    (void) state;

    static char long_name[81];
    static char long_serial[21];
    (void) memset(long_name, 'A', sizeof(long_name) - 1U);
    long_name[sizeof(long_name) - 1U] = '\0';
    (void) memset(long_serial, 'B', sizeof(long_serial) - 1U);
    long_serial[sizeof(long_serial) - 1U] = '\0';

    iolink_device_info_t info = make_info();
    info.vendor_name = long_name;
    info.serial_number = long_serial;
    iolink_device_info_init(&info);

    uint8_t out[128] = {0};
    int n = read_object(IOLINK_IDX_VENDOR_NAME, out, sizeof(out));
    assert_int_equal(n, 64);

    n = read_object(IOLINK_IDX_SERIAL_NUMBER, out, sizeof(out));
    assert_int_equal(n, 16);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_vendor_device_id_indices_reserved, setup, teardown),
        cmocka_unit_test_setup_teardown(test_error_count_index, setup, teardown),
        cmocka_unit_test_setup_teardown(test_product_uri_index, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_data_input_returns_pd, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_data_output_returns_pd, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_only_identification_rejects_write, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_parameter_lock_blocks_tag_write, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parameter_lock_blocks_vendor_write, setup, teardown),
        cmocka_unit_test_setup_teardown(test_identification_string_length_limits, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
