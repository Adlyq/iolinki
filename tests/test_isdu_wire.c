/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_wire.c
 * @brief 字节精确的 ISDU 传输测试（C3、A.5、Table 52/54）。
 *
 * 所有向量均按规范公式计算（CHKPDU = 所有 ISDU
 * 八位组在将校验和八位组置零后的 XOR，A.5.6），且不得从
 * 实现中推导。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/params.h"
#include "iolinki/device_info.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static iolink_test_env_t g_test_env;

#include "conformance_test.h"

static int test_setup(void** state)
{
    (void) state;
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    return 0;
}

static uint8_t chkpdu(const uint8_t* o, size_t n)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < n; i++) {
        c ^= o[i];
    }
    return c;
}

/* 在索引 0x0010 处提供的两八位组标识对象 {0x12, 0x34}。 */
static const char g_vendor_name[3] = {(char) 0x12, (char) 0x34, '\0'};

static void wire_device_info_init(void)
{
    static iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.vendor_name = g_vendor_name;
    info.vendor_text = "v";
    info.product_name = "p";
    info.product_id = "i";
    info.product_text = "t";
    info.serial_number = "s";
    info.hardware_revision = "h";
    info.firmware_revision = "f";
    info.vendor_id = 0x0123U;
    info.device_id = 0x010203U;
    info.profile_characteristic = 0x0001U;
    info.revision_id = 0x0111U;
    info.min_cycle_time = 0U;
    test_env_init(&g_test_env, &info);
}

/* 一个 63 字符的 VendorName，通过 ExtLength（>15 个数据八位组）提供。 */
static uint8_t g_long_name[64];

static void wire_long_vendor_init(void)
{
    for (uint8_t i = 0U; i < 64U; i++) {
        g_long_name[i] = (uint8_t) (0x40U + i);
    }
    g_long_name[63] = 0U; /* NUL 结尾，使 strlen() 看到 63 个字符 */

    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.vendor_name = (const char*) g_long_name;
    info.vendor_text = "v";
    info.product_name = "p";
    info.product_id = "i";
    info.product_text = "t";
    info.serial_number = "s";
    info.hardware_revision = "h";
    info.firmware_revision = "f";
    test_env_init(&g_test_env, &info);
    test_env_reset_params(&g_test_env);
}

/* 以连续的 1 八位组报文送入完整请求：START 然后是 COUNT 1..0。 */
static void wire_write_request(iolink_isdu_ctx_t* ctx, const uint8_t* data, size_t len)
{
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < len; i++) {
        iolink_isdu_od_write(ctx, fc, &data[i], 1U);
        if (fc == IOLINK_FLOWCTRL_START) {
            fc = 1U;
        }
        else {
            fc = (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        }
    }
    iolink_isdu_process(ctx);
}

/* 通过 ISDU 通道以 1 八位组读的方式读取分段应答。
 * START 时返回第一个八位组；长度半字节决定其余部分。 */
static size_t wire_read_response(iolink_isdu_ctx_t* ctx, uint8_t* out, size_t out_size)
{
    uint8_t octet = 0U;
    uint8_t fc = IOLINK_FLOWCTRL_START;
    size_t n = 0U;
    size_t total = 0U;

    iolink_isdu_od_read(ctx, fc, &octet, 1U);
    if (n < out_size) {
        out[n] = octet;
    }
    n++;
    if (octet == 0x01U) {
        return 1U; /* Busy */
    }
    uint8_t len = (uint8_t) (octet & 0x0FU);
    if (len == 0U) {
        return 1U; /* No Service */
    }
    total = (len == 1U) ? 0U : len; /* ExtLength 的读取在下面进行 */

    fc = 1U;
    while (1) {
        if ((total != 0U) && (n >= total)) {
            break;
        }
        iolink_isdu_od_read(ctx, fc, &octet, 1U);
        if (n < out_size) {
            out[n] = octet;
        }
        n++;
        if ((total == 0U) && (n == 2U)) {
            len = octet;
            if ((len >= 17U) && (len <= 238U)) {
                total = len;
            }
            else {
                total = n; /* 无扩展长度：这两个八位组就是整个应答 */
            }
        }
        if ((total != 0U) && (n >= total)) {
            break;
        }
        fc = (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    return n;
}

/* 图 A.20：读取 8 位索引 0x10 -> D4 12 34 F2。 */
static void test_isdu_wire_read_figure_a20(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0054");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    const uint8_t expected[4] = {0xD4U, 0x12U, 0x34U, 0xF2U};
    assert_memory_equal(resp, expected, sizeof(expected));
}

/* 图 A.20 / Table C.1：对只读 VendorName (0x10) 的 WRITE
 * 被以 0x80 0x23 拒绝。 */
static void test_isdu_wire_write_figure_a20(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0066");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    const uint8_t req[6] = {0x26U, 0x10U, 0x01U, 0x12U, 0x34U, 0x11U};
    assert_int_equal(chkpdu(req, 6U), 0U);
    wire_write_request(&ctx, req, sizeof(req));

    const uint8_t expected[4] = {0x44U, 0x80U, IOLINK_ISDU_ERROR_WRITE_PROTECTED, 0xE7U};
    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, sizeof(expected));
    assert_memory_equal(resp, expected, sizeof(expected));
}

/* 对 VendorName 0x0010 的 16 位索引读（I-Service 0xB，Length 5）。该
 * 请求较短，因此这里不涉及 COUNT 回绕；参见专门的
 * 写/读回绕用例。 */
static void test_isdu_wire_16bit_index_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0059");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    /* 16 位索引 + 子索引读（I-Service 0xB，Length 5）：B5 00 10 00 A5。 */
    const uint8_t req[5] = {0xB5U, 0x00U, 0x10U, 0x00U, 0xA5U};
    assert_int_equal(chkpdu(req, 5U), 0U);
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < sizeof(req); i++) {
        iolink_isdu_od_write(&ctx, fc, &req[i], 1U);
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[0], 0xD4U);
    assert_int_equal(resp[1], 0x12U);
    assert_int_equal(resp[2], 0x34U);
    assert_int_equal(resp[3], 0xF2U);
}

/* 超过 16 个八位组的请求会使 FlowCTRL COUNT 从 15 回绕到 0。回绕后的八位组
 * 必须继续该请求，而不能作为结构违规被拒绝。 */
static void test_isdu_wire_write_count_wrap(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0060");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    /* 带 11 个数据八位组的 16 位 ApplicationTag 写：共 17 个八位组，因此
       最后一个八位组（CHKPDU）由紧接 COUNT 15 的 COUNT 0 承载。 */
    uint8_t req[17];
    req[0] = 0x31U; /* 写，16 位索引，后跟 ExtLength。 */
    req[1] = (uint8_t) sizeof(req);
    req[2] = 0x00U;
    req[3] = (uint8_t) (IOLINK_IDX_APPLICATION_TAG & 0xFFU);
    req[4] = 0x00U; /* 子索引 0。 */
    for (size_t i = 0U; i < 11U; i++) {
        req[5U + i] = (uint8_t) ('A' + i);
    }
    req[16] = chkpdu(req, 16U);
    assert_int_equal(chkpdu(req, sizeof(req)), 0U);

    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < sizeof(req); i++) {
        iolink_isdu_od_write(&ctx, fc, &req[i], 1U);
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    /* COUNT 0 在 COUNT 15 之后被接受（若回绕被拒绝则会停留在 15）。 */
    assert_int_equal(ctx.last_flowctrl, 0U);
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 2U);
    assert_int_equal(resp[0], 0x52U); /* Write Response (+) */
    assert_int_equal(resp[1], chkpdu(resp, 1U));

    uint8_t readback[16];
    assert_int_equal(iolink_params_ctx_get(test_env_params(&g_test_env), IOLINK_IDX_APPLICATION_TAG,
                                           0U, readback, sizeof(readback)),
                     11);
    assert_memory_equal(readback, &req[5], 11U);
}

/* 重复的 FlowCTRL 重发上一条报文：其载荷被忽略。 */
static void test_isdu_wire_repeated_flowctrl(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0309");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_write(&ctx, 1U, (const uint8_t[]){0x10U}, 1U);
    /* 用伪造载荷重复 COUNT 1：必须被忽略。 */
    iolink_isdu_od_write(&ctx, 1U, (const uint8_t[]){0xAAU}, 1U);
    iolink_isdu_od_write(&ctx, 2U, (const uint8_t[]){0x83U}, 1U);
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[3], 0xF2U);
}

/* 既不是 COUNT+1 也不是重复的 FlowCTRL 属于结构违规：
   请求被丢弃，下一次读应答 No Service (0x00)。 */
static void test_isdu_wire_flowctrl_error(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_write(&ctx, 3U, (const uint8_t[]){0x10U}, 1U); /* 跳过 1,2 */
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 1U);
    assert_int_equal(resp[0], 0x00U);
}

/* Test Spec 10032 V1.1.4 Table 84 (SDCI_TC_0065)：CHKPDU 不正确的
 * 请求被丢弃，下一次读应答为 I-Service 0 ("No Service")。 */
static void test_isdu_wire_chkpdu_corrupt(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0065");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    const uint8_t req[3] = {0x93U, 0x10U, 0x82U}; /* 应为 0x83 */
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[8];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 1U);
    assert_int_equal(resp[0], 0x00U);
}

/* Table C.1 / C.2.14：不支持的 SystemCommand 为 FUNC_NOTAVAIL。 */
static void test_isdu_wire_unsupported_system_command(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0104");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    const uint8_t req[] = {0x14U, 0x02U, 0xFFU, 0xE9U};
    const uint8_t expected[] = {0x44U, 0x80U, 0x35U, 0xF1U};
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[4];
    assert_int_equal(wire_read_response(&ctx, resp, sizeof(resp)), sizeof(expected));
    assert_memory_equal(resp, expected, sizeof(expected));
}

/* Table C.1 / C.2.3：不存在的 16 位索引仍返回 IDX_NOTAVAIL。 */
static void test_isdu_wire_unknown_index(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    const uint8_t read_req[] = {0xB5U, 0x01U, 0x00U, 0x00U, 0xB4U};
    const uint8_t read_expected[] = {0xC4U, 0x80U, 0x11U, 0x55U};
    wire_write_request(&ctx, read_req, sizeof(read_req));
    uint8_t resp[4];
    assert_int_equal(wire_read_response(&ctx, resp, sizeof(resp)), sizeof(read_expected));
    assert_memory_equal(resp, read_expected, sizeof(read_expected));

    const uint8_t write_req[] = {0x36U, 0x01U, 0x00U, 0x00U, 0x00U, 0x37U};
    const uint8_t write_expected[] = {0x44U, 0x80U, 0x11U, 0xD5U};
    wire_write_request(&ctx, write_req, sizeof(write_req));
    assert_int_equal(wire_read_response(&ctx, resp, sizeof(resp)), sizeof(write_expected));
    assert_memory_equal(resp, write_expected, sizeof(write_expected));
}

/* 应用尚未应答时的 START 读返回 Busy (0x01)。 */
static void test_isdu_wire_busy_polling(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0054");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    /* 解析请求，但暂不运行 iolink_isdu_process()。 */
    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < sizeof(req); i++) {
        iolink_isdu_od_write(&ctx, fc, &req[i], 1U);
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }

    uint8_t octet = 0U;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x01U); /* Busy */

    /* 应用应答；下一次 START 读返回该应答。 */
    iolink_isdu_process(&ctx);
    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[0], 0xD4U);
}

/* 请求中途的 ABORT 丢弃所有内容；下一次读应答 No Service。 */
static void test_isdu_wire_abort(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0067");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_ABORT, NULL, 0U);

    uint8_t octet = 0U;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x00U);
}

/* WRITE 通道上的 ABORT 同样拒绝请求数据并将
 * 传输返回 Idle（Table 52 与方向无关）。 */
static void test_isdu_wire_write_abort(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    test_env_reset_params(&g_test_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_ABORT, (const uint8_t[]){0x00U}, 1U);

    uint8_t octet = 0xAAU;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x00U);
}

/* 63 八位组字符串的 ExtLength 读：D1 42 <63 data> <CHKPDU>（图 A.19 例 3：n = data+3）。 */
static void test_isdu_wire_extlength_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0055");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_long_vendor_init();
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    /* 索引 0x10 的 8 位读：0x93 0x10 0x83。 */
    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[128];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    /* 应答总长 = I-Service/Length 八位组 + ExtLength + 63 数据 + CHKPDU = 66 (0x42)。 */
    assert_int_equal(n, 66U);
    assert_int_equal(resp[0], 0xD1U);
    assert_int_equal(resp[1], 0x42U);
    assert_memory_equal(&resp[2], g_long_name, 63U);
    assert_int_equal(resp[65], chkpdu(resp, 65U));
}

/* 超过 16 个八位组的应答会跨 COUNT 15 -> 0 回绕读取。 */
static void test_isdu_wire_read_count_wrap(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0055");
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_long_vendor_init();
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);

    /* 63 八位组 VendorName -> 66 八位组应答。 */
    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[128];
    uint8_t octet = 0U;
    size_t n = 0U;
    unsigned wraps = 0U;

    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    resp[n++] = octet;
    uint8_t prev = IOLINK_FLOWCTRL_START;
    uint8_t fc = 1U;
    while (n < 66U) {
        if ((prev == 15U) && (fc == 0U)) {
            wraps++;
        }
        iolink_isdu_od_read(&ctx, fc, &octet, 1U);
        resp[n++] = octet;
        prev = fc;
        fc = (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    assert_true(wraps >= 1U);
    assert_int_equal(resp[0], 0xD1U);
    assert_int_equal(resp[1], 0x42U);
    assert_memory_equal(&resp[2], g_long_name, 63U);
    assert_int_equal(resp[65], chkpdu(resp, 65U));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_isdu_wire_read_figure_a20, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_write_figure_a20, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_16bit_index_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_repeated_flowctrl, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_flowctrl_error, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_chkpdu_corrupt, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_unsupported_system_command, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_unknown_index, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_busy_polling, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_abort, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_write_abort, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_write_count_wrap, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_read_count_wrap, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_extlength_read, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
