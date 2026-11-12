/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_spec_conformance.c
 * @brief 强制性 ISDU 对象与访问规则的一致性断言，
 *        依据随附的 IO-Link V1.1.5 规范检查。
 *
 * 覆盖：保留的 0x000A/0x000B 索引、ErrorCount (0x0020)、ProductURI
 * (0x001B)、ProcessDataInput/Output (0x0028/0x0029)、对只读对象的 WRITE 拒绝
 * (Table C.1)、Parameter 访问锁 (Table B.2.4) 以及标识字符串
 * fixedLength 限制 (B.2.8-B.2.15)。
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

/* 读取一个对象并把响应拷贝到 out；返回响应长度。 */
static int read_object(uint16_t index, uint8_t* out, size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, index, 0U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

/* 同上，但绑定了 DLL 上下文（过程数据对象需要）。 */
static int read_object_with_dll(uint16_t index, iolink_dll_ctx_t* dll, uint8_t* out,
                                size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    ctx.dll_ctx = dll;
    assert_int_equal(isdu_send_read_request(&ctx, index, 0U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

/* 接受所有厂商服务访问（用于验证 Parameter 锁）。 */
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

/* 写入一个对象并拷贝响应；返回响应长度。 */
static int write_object(uint16_t index, const uint8_t* data, uint8_t len, uint8_t* out,
                        size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    assert_int_equal(isdu_send_write_request(&ctx, index, 0U, data, len), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

/* Table B.8：0x0004-0x000B 为保留索引；VendorID/DeviceID 位于 DPP1。 */
static void test_vendor_device_id_indices_reserved(void** state)
{
    (void) state;

    const uint16_t reserved[] = {
        0x0004U, 0x0005U, 0x0006U, 0x0007U, 0x0008U, 0x0009U,
        0x000AU, 0x000BU, 0x001CU, 0x001DU, 0x001EU, 0x001FU,
    };
    for (size_t i = 0U; i < (sizeof(reserved) / sizeof(reserved[0])); i++) {
        uint8_t out[8] = {0};
        int n = read_object(reserved[i], out, sizeof(out));
        assert_int_equal(n, 2);
        assert_int_equal(out[0], 0x80U);
        assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);

        /* C.2.3：写入不存在的索引同样返回 IDX_NOTAVAIL。 */
        const uint8_t value = 0x5AU;
        n = write_object(reserved[i], &value, 1U, out, sizeof(out));
        assert_int_equal(n, 2);
        assert_int_equal(out[0], 0x80U);
        assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
}

/* Table B.8：ErrorCount (0x0020) 是 2 字节 UIntegerT（不得返回错误）。 */
static void test_error_count_index(void** state)
{
    (void) state;
    uint8_t out[8] = {0};
    int n = read_object(IOLINK_IDX_ERROR_COUNT, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_not_equal(out[0], 0x80U); /* 已服务，而非 IDX_NOTAVAIL */
}

/* Table B.8 / B.2.19：声明了配置文件时 ProductURI (0x001B) 会被服务。 */
static void test_product_uri_index(void** state)
{
    (void) state;
    const char* expected = "https://example.com/device";
    uint8_t out[64] = {0};
    int n = read_object(IOLINK_IDX_PRODUCT_URI, out, sizeof(out));
    assert_int_equal(n, (int) strlen(expected));
    assert_memory_equal(out, expected, strlen(expected));
}

/* B.2.19/Table C.1：未配置的可选 ProductURI 不可用。 */
static void test_product_uri_absent(void** state)
{
    (void) state;
    test_device_info_reset(NULL); /* 默认 info：product_uri = NULL */

    uint8_t out[8] = {0};
    int n = read_object(IOLINK_IDX_PRODUCT_URI, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* Table B.8 / B.2.23：ProcessDataInput (0x0028) 返回 PDin 通道的
 * 过程数据及其运行时长度，而不是描述符/长度字节。 */
static void test_process_data_input_returns_pd(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0132");
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));

    /* 只有公共 setter 会配置长度：它更新运行时
       字段，因此这里改用 pd_in_len 会把应答截断为 0。 */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 2U, 0U), 0);
    assert_int_equal(dll.pd_in_len, 0U);
    dll.pd_valid = true;
    dll.pd_in[0][dll.od_width + 0U] = 0x11U;
    dll.pd_in[0][dll.od_width + 1U] = 0x22U;

    uint8_t out[8] = {0};
    int n = read_object_with_dll(0x0028U, &dll, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x11U);
    assert_int_equal(out[1], 0x22U);
}

/* 当 DLL 将 PDin 标记为无效时 B.2.23 未作规定：PD 状态位
 * (A.1.5) 承载有效性，因此返回当前过程数据缓冲区
 * 而不是 ISDU 错误（ST 参考行为）。 */
static void test_process_data_input_invalid_still_serves_buffer(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.pd_in_len_current = 2U;
    dll.pd_in_len = 2U;
    dll.pd_valid = false;
    dll.pd_in[0][dll.od_width + 0U] = 0x11U;
    dll.pd_in[0][dll.od_width + 1U] = 0x22U;

    uint8_t out[8] = {0};
    int n = read_object_with_dll(0x0028U, &dll, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x11U);
    assert_int_equal(out[1], 0x22U);
}

/* 首个 PDout 帧之前 B.2.24 未作规定：按原样返回缓冲区
 * （ST 参考行为），它在 DLL 初始化后被清零。 */
static void test_process_data_output_before_first_frame(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.pd_out_len_current = 2U;

    uint8_t out[8] = {0xFFU, 0xFFU};
    int n = read_object_with_dll(0x0029U, &dll, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x00U);
    assert_int_equal(out[1], 0x00U);
}

/* B.2.23/B.2.24：不含过程数据的设备以空的正向
 * 响应应答，而不是旧的 1 字节长度描述符。 */
static void test_process_data_zero_length(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));

    uint8_t out[8] = {0};
    assert_int_equal(read_object_with_dll(0x0028U, &dll, out, sizeof(out)), 0);
    assert_int_equal(read_object_with_dll(0x0029U, &dll, out, sizeof(out)), 0);
}

/* B.2.23：以 0x80 开头的标准 2 字节正向载荷不得
 * 被误判为负向响应；线上的 I-Service 才是判别依据。 */
static void test_process_data_80xx_is_positive(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.pd_in_len_current = 2U;
    dll.pd_in_len = 2U;
    dll.pd_valid = true;
    dll.pd_in[0][dll.od_width + 0U] = 0x80U;
    dll.pd_in[0][dll.od_width + 1U] = 0x42U;

    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0U), 1);
    iolink_isdu_process(&ctx);

    uint8_t wire[8] = {0};
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, wire, sizeof(wire));
    assert_int_equal(wire[0], 0xD4U); /* 读响应（+），2 个数据字节 */
    assert_int_equal(wire[1], 0x80U);
    assert_int_equal(wire[2], 0x42U);
}

/* Figure B.5/Table B.6：解码 DPP1 的 ProcessDataIn/Out 长度描述符。 */
static unsigned pd_descriptor_octets(uint8_t code)
{
    const unsigned field = (unsigned) (code & 0x1FU);
    if ((code & 0x80U) != 0U) {
        return field + 1U; /* BYTE=1：Length 以（字节数 - 1）表示 */
    }
    return field / 8U; /* BYTE=0：Length 以位数表示 */
}

/* 读取 Direct Parameter page 1 的一个字节（地址 0x00..0x0F）。 */
static uint8_t read_dpp1_octet(iolink_dll_ctx_t* dll, uint8_t addr)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
    ctx.dll_ctx = dll;
    assert_int_equal(
        isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, (uint8_t) (addr + 1U)), 1);
    iolink_isdu_process(&ctx);
    uint8_t value = 0U;
    assert_int_equal(isdu_collect_response(&ctx, &value, 1U), 1);
    return value;
}

/* Table B.8 / B.2.24：ProcessDataOutput (0x0029) 返回写入设备的
 * 最后一个有效 PD 输出数据。 */
static void test_process_data_output_returns_pd(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0133");
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.pd_out_len_current = 2U;
    dll.pd_out[0] = 0xAAU;
    dll.pd_out[1] = 0xBBU;
    dll.pd_out_seen = true;

    uint8_t out[8] = {0};
    int n = read_object_with_dll(0x0029U, &dll, out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0xAAU);
    assert_int_equal(out[1], 0xBBU);

    /* SDCI_TC_0133：长度与 DPP1 0x06 一致；描述符中
       保留位 5 和未使用的 SIO 位 6 均为 0。 */
    const uint8_t dpp1_pdout = read_dpp1_octet(&dll, 0x06U);
    assert_int_equal(dpp1_pdout & 0x60U, 0x00U);
    assert_int_equal(pd_descriptor_octets(dpp1_pdout), n);
}

/* Table C.1：对只读标识对象的 WRITE 返回 0x80 0x23。 */
static void test_read_only_identification_rejects_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0140");
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

/* DeviceAccessLocks 位 0：设置参数锁后，参数写入
 * 会被拒绝（Table C.1 访问被拒）。 */
static void test_parameter_lock_blocks_tag_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0156");
    (void) state;

    /* 在启用锁之前建立一个已知值。 */
    const uint8_t original[] = {'X', 'Y', 'Z'};
    uint8_t out[8] = {0};
    int n = write_object(IOLINK_IDX_APPLICATION_TAG, original, sizeof(original), out, sizeof(out));
    assert_int_equal(n, 0);

    iolink_device_info_ctx_set_access_locks(test_device_info_ctx(), IOLINK_LOCK_PARAM);

    const uint8_t overwrite[] = {'A', 'B', 'C'};
    n = write_object(IOLINK_IDX_APPLICATION_TAG, overwrite, sizeof(overwrite), out, sizeof(out));
    assert_int_equal(n, 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    /* B.2.4：读访问不受影响，被锁定的写入也没有改变
     * 已存储的值。 */
    uint8_t readback[16] = {0};
    n = read_object(IOLINK_IDX_APPLICATION_TAG, readback, sizeof(readback));
    assert_int_equal(n, (int) sizeof(original));
    assert_memory_equal(readback, original, sizeof(original));

    /* SDCI_TC_0156 步骤 d：写入 DeviceAccessLocks = 0x0000 解除
       Parameter 锁，随后同样的参数写入即可成功。 */
    const uint8_t unlock[] = {0x00U, 0x00U};
    n = write_object(IOLINK_IDX_DEVICE_ACCESS_LOCKS, unlock, sizeof(unlock), out, sizeof(out));
    assert_int_equal(n, 0);
    assert_int_equal(iolink_device_info_ctx_get_access_locks(test_device_info_ctx()), 0U);

    n = write_object(IOLINK_IDX_APPLICATION_TAG, overwrite, sizeof(overwrite), out, sizeof(out));
    assert_int_equal(n, 0);
    (void) memset(readback, 0, sizeof(readback));
    n = read_object(IOLINK_IDX_APPLICATION_TAG, readback, sizeof(readback));
    assert_int_equal(n, (int) sizeof(overwrite));
    assert_memory_equal(readback, overwrite, sizeof(overwrite));
}

/* B.2.4：Parameter 锁同样阻止对设备特定参数的写入；
 * 读取仍会到达厂商服务。 */
static void test_parameter_lock_blocks_vendor_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0156");
    (void) state;

    iolink_device_info_ctx_set_access_locks(test_device_info_ctx(), IOLINK_LOCK_PARAM);

    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx);
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
    test_isdu_bind_params(&ctx);
    ctx.vendor_service = vendor_service_ok;
    assert_int_equal(isdu_send_read_request(&ctx, 0x0040U, 0U), 1);
    iolink_isdu_process(&ctx);
    n = isdu_collect_response(&ctx, out, sizeof(out));
    assert_int_equal(n, 0);
}

/* B.2.8-B.2.19：字符串被截断到其对象的 fixedLength
 * （64 字节，SerialNumber 为 16，ProductURI 最多 100，tag 为 32）。 */
static void test_identification_string_length_limits(void** state)
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

    static char long_name[81];
    static char long_serial[21];
    static char long_uri[121];
    uint8_t long_tag[40];
    (void) memset(long_name, 'A', sizeof(long_name) - 1U);
    long_name[sizeof(long_name) - 1U] = '\0';
    (void) memset(long_serial, 'B', sizeof(long_serial) - 1U);
    long_serial[sizeof(long_serial) - 1U] = '\0';
    (void) memset(long_uri, 'U', sizeof(long_uri) - 1U);
    long_uri[sizeof(long_uri) - 1U] = '\0';
    (void) memset(long_tag, 'T', sizeof(long_tag));

    iolink_device_info_t info = make_info();
    info.vendor_name = long_name;
    info.serial_number = long_serial;
    info.product_uri = long_uri;
    test_device_info_reset(&info);

    uint8_t out[160] = {0};
    int n = read_object(IOLINK_IDX_VENDOR_NAME, out, sizeof(out));
    assert_int_equal(n, 64);

    n = read_object(IOLINK_IDX_SERIAL_NUMBER, out, sizeof(out));
    assert_int_equal(n, 16);

    /* B.2.19 / Table B.8：ProductURI 最多允许 100 字节。 */
    n = read_object(IOLINK_IDX_PRODUCT_URI, out, sizeof(out));
    assert_int_equal(n, 100);

    /* B.2.16-B.2.18 / Table B.8：tag 上限为 32 字节。 */
    uint8_t tag_resp[8] = {0};
    n = write_object(IOLINK_IDX_FUNCTION_TAG, long_tag, (uint8_t) sizeof(long_tag), tag_resp,
                     sizeof(tag_resp));
    assert_int_equal(n, 0);
    n = read_object(IOLINK_IDX_FUNCTION_TAG, out, sizeof(out));
    assert_int_equal(n, 32);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_vendor_device_id_indices_reserved, setup, teardown),
        cmocka_unit_test_setup_teardown(test_error_count_index, setup, teardown),
        cmocka_unit_test_setup_teardown(test_product_uri_index, setup, teardown),
        cmocka_unit_test_setup_teardown(test_product_uri_absent, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_data_input_returns_pd, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_data_input_invalid_still_serves_buffer, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_process_data_output_before_first_frame, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_process_data_zero_length, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_data_80xx_is_positive, setup, teardown),
        cmocka_unit_test_setup_teardown(test_process_data_output_returns_pd, setup, teardown),
        cmocka_unit_test_setup_teardown(test_read_only_identification_rejects_write, setup,
                                        teardown),
        cmocka_unit_test_setup_teardown(test_parameter_lock_blocks_tag_write, setup, teardown),
        cmocka_unit_test_setup_teardown(test_parameter_lock_blocks_vendor_write, setup, teardown),
        cmocka_unit_test_setup_teardown(test_identification_string_length_limits, setup, teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
