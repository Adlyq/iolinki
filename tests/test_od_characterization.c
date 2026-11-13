/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_od_characterization.c
 * @brief 对象字典改造的回归安全网：钉死改造前的线上可观察行为。
 *
 * 本文件只断言"当前实现实际发出的字节"，不做规范解读。表驱动重构
 * （src/isdu.c + src/od.c）之后这些断言必须保持原样通过，否则即为
 * 行为回归。覆盖 0x0002、0x000C、0x000D、0x0024、0x0025、0x0028、
 * 0x0029、DPP1/DPP2、标识对象、标签对象与保留索引。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "iolinki/device_info.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static iolink_test_env_t g_char_env;

static int char_setup(void** state)
{
    (void) state;
    return 0;
}

static int char_teardown(void** state)
{
    (void) state;
    return 0;
}

/** 发送读请求并收集载荷；返回值同时用于正向长度与负向的两字节错误体。 */
static int char_read(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* buf,
                     size_t cap)
{
    assert_int_equal(isdu_send_read_request(ctx, index, subindex), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, buf, cap);
}

/** 发送写请求并收集应答（成功时长度为 0）。 */
static int char_write(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, const uint8_t* data,
                      uint8_t len, uint8_t* buf, size_t cap)
{
    assert_int_equal(isdu_send_write_request(ctx, index, subindex, data, len), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, buf, cap);
}

/** 断言写请求得到指定的否定应答码。 */
static void char_expect_write_error(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                    const uint8_t* data, uint8_t len, uint8_t code)
{
    uint8_t buf[4] = {0U};

    assert_int_equal(char_write(ctx, index, subindex, data, len, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x80U);
    assert_int_equal(buf[1], code);
}

/** 断言读请求得到指定的否定应答码。 */
static void char_expect_read_error(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                   uint8_t code)
{
    uint8_t buf[4] = {0U};

    assert_int_equal(char_read(ctx, index, subindex, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x80U);
    assert_int_equal(buf[1], code);
}

static void char_prepare(iolink_isdu_ctx_t* ctx, const iolink_device_info_t* info)
{
    test_env_init(&g_char_env, info);
    test_env_reset_params(&g_char_env);
    iolink_isdu_init(ctx);
    test_isdu_bind_params(ctx, &g_char_env);
}

static const iolink_device_info_t k_char_info = {
    .vendor_name = "iolinki",
    .vendor_text = "vendor text",
    .product_name = "product",
    .product_id = "pid",
    .product_text = "product text",
    .serial_number = "SN-0001",
    .hardware_revision = "HW1",
    .firmware_revision = "FW1",
    .product_uri = NULL,
    .application_tag = "",
    .vendor_id = 0x04D2U,
    .device_id = 0x00162FU,
    .function_id = 0x0001U,
    .profile_characteristic = 0x1234U,
    .min_cycle_time = 0U,
    .revision_id = 0x0111U,
    .device_status = 0U,
    .detailed_device_status = 0U,
    .access_locks = 0U,
};

/* 0x0002 SystemCommand：只写，读返回 NOT_ACCESSIBLE，未知命令返回 FUNC_NOTAVAIL。 */
static void test_char_system_command(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    char_prepare(&ctx, &k_char_info);

    char_expect_read_error(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);

    uint8_t cmd = IOLINK_CMD_DEVICE_RESET;
    uint8_t buf[4] = {0U};
    assert_int_equal(char_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U, buf, sizeof(buf)),
                     0);
    assert_true(ctx.reset_pending);

    char_prepare(&ctx, &k_char_info);
    cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(char_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U, buf, sizeof(buf)),
                     0);
    assert_true(ctx.app_reset_pending);

    char_prepare(&ctx, &k_char_info);
    cmd = 0x84U; /* Table B.9 保留值 */
    char_expect_write_error(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U,
                            IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);

    char_prepare(&ctx, &k_char_info);
    char_expect_write_error(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, NULL, 0U,
                            IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* 0x000D ProfileCharacteristic：UIntegerT16 的 ArrayT，sub 0/1 同值，sub 2 不可用。 */
static void test_char_profile_characteristic(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t buf[8] = {0U};

    char_prepare(&ctx, &k_char_info);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x12U);
    assert_int_equal(buf[1], 0x34U);

    assert_int_equal(char_read(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 1U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x12U);
    assert_int_equal(buf[1], 0x34U);

    char_expect_read_error(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 2U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    char_expect_write_error(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0U, buf, 2U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    iolink_device_info_t no_profile = k_char_info;
    no_profile.profile_characteristic = 0U;
    char_prepare(&ctx, &no_profile);
    char_expect_read_error(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0U,
                           IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* 0x0024 DeviceStatus：单字节只读，写被拒绝，sub 1 不可用。 */
static void test_char_device_status(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    uint8_t buf[8] = {0U};

    char_prepare(&ctx, &k_char_info);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x00U);

    char_expect_write_error(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, buf, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    char_expect_read_error(&ctx, IOLINK_IDX_DEVICE_STATUS, 1U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    iolink_events_init(&events);
    ctx.event_ctx = &events;
    iolink_event_trigger(&events, 0x8CA0U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x04U); /* Error -> DeviceStatus Error */
}

/* Direct Parameter page 1：只读 16 字节映像，sub 16 后不可用。 */
static void test_char_direct_param_page1(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t buf[32] = {0U};

    char_prepare(&ctx, &k_char_info);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0U, buf, sizeof(buf)), 16);
    assert_int_equal(buf[0x00], 0x00U); /* MasterCommand（未绑定主站命令） */
    assert_int_equal(buf[0x01], 0x00U); /* MasterCycleTime（未绑定 DLL） */
    assert_int_equal(buf[0x02], 0x00U); /* MinCycleTime（未配置） */
    assert_int_equal(buf[0x04], 0x11U); /* RevisionID 低字节 */
    assert_int_equal(buf[0x07], 0x04U); /* VendorID MSB */
    assert_int_equal(buf[0x08], 0xD2U); /* VendorID LSB */
    assert_int_equal(buf[0x09], 0x00U); /* DeviceID 高字节 */
    assert_int_equal(buf[0x0A], 0x16U);
    assert_int_equal(buf[0x0B], 0x2FU);

    assert_int_equal(char_read(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 8U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x04U); /* sub 8 对应 page[7]（VendorID MSB） */
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 9U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0xD2U);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 16U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x00U);

    char_expect_read_error(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 17U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    char_expect_write_error(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0U, buf, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

/* DPP1 映像访问器与规范字面量一致：期望值来自 B.1 的独立向量，
 * 不调用构建器生成。 */
static void test_char_direct_param_page1_image(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;

    char_prepare(&ctx, &k_char_info);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x00U), 0x00U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x01U), 0x00U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x02U), 0x00U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x04U), 0x11U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x07U), 0x04U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x08U), 0xD2U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x09U), 0x00U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x0AU), 0x16U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x0BU), 0x2FU);
    for (uint8_t addr = 0x0CU; addr < 16U; addr++) {
        assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, addr), 0x00U);
    }
}

/* Direct Parameter page 2：可读可写，受 Parameter 访问锁保护。 */
static void test_char_direct_param_page2(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t page2[16] = {0U};
    uint8_t buf[32] = {0U};

    char_prepare(&ctx, &k_char_info);
    ctx.direct_param_page2 = page2;

    assert_int_equal(char_read(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0U, buf, sizeof(buf)), 16);
    for (size_t i = 0U; i < 16U; i++) {
        assert_int_equal(buf[i], 0x00U);
    }

    uint8_t block[16];
    (void) memset(block, 0xA5, sizeof(block));
    assert_int_equal(
        char_write(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0U, block, 16U, buf, sizeof(buf)), 0);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0U, buf, sizeof(buf)), 16);
    assert_memory_equal(buf, block, sizeof(block));

    uint8_t octet = 0x5AU;
    assert_int_equal(
        char_write(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 5U, &octet, 1U, buf, sizeof(buf)), 0);
    assert_int_equal(page2[4], 0x5AU);

    iolink_device_info_ctx_set_access_locks(&g_char_env.info, IOLINK_LOCK_PARAM);
    char_expect_write_error(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0U, &octet, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    iolink_device_info_ctx_set_access_locks(&g_char_env.info, 0U);
    char_expect_read_error(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 17U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* 标识对象：只读，写返回 WRITE_PROTECTED，sub 1 不可用，缺失的 ProductURI 不可用。 */
static void test_char_identification_objects(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    const uint16_t ro[] = {
        IOLINK_IDX_VENDOR_NAME,       IOLINK_IDX_VENDOR_TEXT,       IOLINK_IDX_PRODUCT_NAME,
        IOLINK_IDX_PRODUCT_ID,        IOLINK_IDX_PRODUCT_TEXT,      IOLINK_IDX_SERIAL_NUMBER,
        IOLINK_IDX_HARDWARE_REVISION, IOLINK_IDX_FIRMWARE_REVISION, IOLINK_IDX_ERROR_COUNT,
    };
    uint8_t buf[32] = {0U};

    char_prepare(&ctx, &k_char_info);
    for (size_t i = 0U; i < (sizeof(ro) / sizeof(ro[0])); i++) {
        char_expect_write_error(&ctx, ro[i], 0U, buf, 1U, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        char_expect_read_error(&ctx, ro[i], 1U, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }

    assert_int_equal(char_read(&ctx, IOLINK_IDX_VENDOR_NAME, 0U, buf, sizeof(buf)), 7);
    assert_memory_equal(buf, "iolinki", 7);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_SERIAL_NUMBER, 0U, buf, sizeof(buf)), 7);
    assert_memory_equal(buf, "SN-0001", 7);
    char_expect_read_error(&ctx, IOLINK_IDX_PRODUCT_URI, 0U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);

    iolink_device_info_t with_uri = k_char_info;
    with_uri.product_uri = "https://example.invalid/uri";
    char_prepare(&ctx, &with_uri);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_PRODUCT_URI, 0U, buf, sizeof(buf)), 27);
}

/* 标签对象：可读写，超过 32 字节上限的写入被拒绝（行为变更）。 */
static void test_char_tag_objects(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t long_tag[40];
    uint8_t buf[48] = {0U};

    (void) memset(long_tag, 'A', sizeof(long_tag));
    char_prepare(&ctx, &k_char_info);

    char_expect_write_error(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, long_tag, 40U,
                            IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    /* 有意的行为变更：function/location tag 过去被静默截断到 32 字节，
       现在与 application tag 一致，在 OD 层按 block_len 拒绝（0x33），
       参数值保持不变。 */
    char_expect_write_error(&ctx, IOLINK_IDX_FUNCTION_TAG, 0U, long_tag, 40U,
                            IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    char_expect_write_error(&ctx, IOLINK_IDX_LOCATION_TAG, 0U, long_tag, 40U,
                            IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    /* 恰好 32 字节仍可写入并原样读回。 */
    assert_int_equal(char_write(&ctx, IOLINK_IDX_FUNCTION_TAG, 0U, long_tag, 32U, buf, sizeof(buf)),
                     0);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf)), 32);
    assert_memory_equal(buf, long_tag, 32U);
    assert_int_equal(char_write(&ctx, IOLINK_IDX_LOCATION_TAG, 0U, long_tag, 32U, buf, sizeof(buf)),
                     0);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_LOCATION_TAG, 0U, buf, sizeof(buf)), 32);

    char_expect_read_error(&ctx, IOLINK_IDX_APPLICATION_TAG, 1U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    char_prepare(&ctx, &k_char_info);
    iolink_device_info_ctx_set_access_locks(&g_char_env.info, IOLINK_LOCK_PARAM);
    char_expect_write_error(&ctx, IOLINK_IDX_FUNCTION_TAG, 0U, long_tag, 4U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    /* DeviceAccessLocks 自身不受 Parameter 锁保护。 */
    uint8_t unlock[2] = {0x00U, 0x00U};
    assert_int_equal(
        char_write(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, unlock, 2U, buf, sizeof(buf)), 0);
}

/* 0x000C DeviceAccessLocks：2 字节，保留位写入被拒。 */
static void test_char_access_locks(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t buf[8] = {0U};

    char_prepare(&ctx, &k_char_info);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x00U);
    assert_int_equal(buf[1], 0x00U);

    uint8_t invalid[2] = {0x00U, 0x10U}; /* bit 4 保留 */
    char_expect_write_error(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, invalid, 2U,
                            IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);

    uint8_t valid[2] = {0x00U, 0x01U};
    assert_int_equal(
        char_write(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, valid, 2U, buf, sizeof(buf)), 0);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[1], 0x01U);
    char_expect_read_error(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 1U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* 0x0025 DetailedDeviceStatus 与 0x0028/0x0029 过程数据：只读快照。 */
static void test_char_status_and_process_data(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_dll_ctx_t dll_ctx;
    uint8_t buf[32] = {0U};

    (void) memset(&dll_ctx, 0, sizeof(dll_ctx));
    char_prepare(&ctx, &k_char_info);

    assert_int_equal(char_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0U, buf, sizeof(buf)), 0);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, sizeof(buf)), 0);
    char_expect_write_error(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0U, buf, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    char_expect_write_error(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    iolink_events_init(&events);
    ctx.event_ctx = &events;
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, buf, sizeof(buf)), 0);
    char_expect_write_error(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, buf, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    iolink_event_trigger(&events, 0x8CA0U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, buf, sizeof(buf)), 3);
    assert_int_equal(buf[0], 0xE4U);
    assert_int_equal(buf[1], 0x8CU);
    assert_int_equal(buf[2], 0xA0U);
    char_expect_read_error(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 5U,
                           IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    /* 绑定 DLL 后 PD 输入为当前长度的快照。 */
    char_prepare(&ctx, &k_char_info);
    ctx.dll_ctx = &dll_ctx.main;
    dll_ctx.main.shared.od_width = 2U;
    dll_ctx.main.pd_in_len_current = 2U;
    dll_ctx.main.shared.pd_in[0][0] = 0x11U;
    dll_ctx.main.shared.pd_in[0][1] = 0x22U;
    dll_ctx.main.shared.pd_in[0][2] = 0x33U;
    dll_ctx.main.shared.pd_in[0][3] = 0x44U;
    assert_int_equal(char_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x33U);
    assert_int_equal(buf[1], 0x44U);
}

/* -------------------------------------------------------------------------
 * M1：标准索引不可被应用表遮蔽；请求子索引透传到应用写钩子
 * ------------------------------------------------------------------------- */

static int g_char_app_reads;
static int g_char_app_writes;

static iolink_result_t char_shadow_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                        size_t cap)
{
    (void) user;
    g_char_app_reads++;
    return iolink_od_encode(entry, 0xAAU, out, cap);
}

static iolink_result_t char_shadow_write(void* user, const iolink_od_entry_t* entry,
                                         uint8_t subindex, const uint8_t* data, size_t len)
{
    (void) user;
    (void) entry;
    (void) subindex;
    (void) data;
    (void) len;
    g_char_app_writes++;
    return iolink_result_ok(0U);
}

/* 应用表刻意声明规范固定的 0x0024：分发必须仍走内建处理器。 */
static const iolink_od_entry_t k_char_shadow_entries[] = {
    IOLINK_OD_SCALAR_HANDLER(IOLINK_IDX_DEVICE_STATUS, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFU, 0U,
                             "ShadowDeviceStatus", char_shadow_read, char_shadow_write),
};

typedef struct
{
    uint16_t value;
    uint8_t seen_subindex;
} char_multi_cell_t;

static iolink_result_t char_multi_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                       size_t cap)
{
    const char_multi_cell_t* cell = user;

    return iolink_od_encode(entry, cell->value, out, cap);
}

static iolink_result_t char_multi_write(void* user, const iolink_od_entry_t* entry,
                                        uint8_t subindex, const uint8_t* data, size_t len)
{
    char_multi_cell_t* cell = user;
    uint32_t value = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &value);

    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    cell->value = (uint16_t) value;
    cell->seen_subindex = subindex;
    return iolink_result_ok(0U);
}

/* 多元素对象：一个表项覆盖 sub 0..1，写入必须带上请求的子索引。 */
static const iolink_od_entry_t k_char_multi_entries[] = {
    {.index = 0x0304U,
     .subindex = 0U,
     .max_subindex = 1U,
     .datatype = IOLINK_OD_DT_U16,
     .access = IOLINK_OD_ACCESS_RW,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = 0U,
     .handler = IOLINK_OD_HANDLER_NONE,
     .backend = IOLINK_OD_BACKEND_HANDLER,
     .u.handler = {.read = char_multi_read, .write = char_multi_write},
     .block_len = 0U,
     .range_min = 0U,
     .range_max = 0xFFFFU,
     .name = "MultiElementParameter"},
};

static void test_char_standard_index_wins_over_app(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_od_app_t app = {0};
    uint8_t buf[8] = {0U};

    char_prepare(&ctx, &k_char_info);
    app.entries = k_char_shadow_entries;
    app.count = sizeof(k_char_shadow_entries) / sizeof(k_char_shadow_entries[0]);
    iolink_isdu_bind_od(&ctx, &app);

    g_char_app_reads = 0;
    g_char_app_writes = 0;
    assert_int_equal(char_read(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x00U);
    char_expect_write_error(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, buf, 1U,
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(g_char_app_reads, 0);
    assert_int_equal(g_char_app_writes, 0);
}

static void test_char_subindex_reaches_app_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_od_app_t app = {0};
    char_multi_cell_t cell = {0U, 0U};
    uint8_t buf[8] = {0U};

    char_prepare(&ctx, &k_char_info);
    app.entries = k_char_multi_entries;
    app.count = sizeof(k_char_multi_entries) / sizeof(k_char_multi_entries[0]);
    app.user = &cell;
    iolink_isdu_bind_od(&ctx, &app);

    const uint8_t at_sub1[2] = {0x12U, 0x34U};
    assert_int_equal(char_write(&ctx, 0x0304U, 1U, at_sub1, 2U, buf, sizeof(buf)), 0);
    assert_int_equal(cell.seen_subindex, 1U);
    assert_int_equal(cell.value, 0x1234U);
    assert_int_equal(char_read(&ctx, 0x0304U, 1U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x12U);
    assert_int_equal(buf[1], 0x34U);

    const uint8_t at_sub0[2] = {0x00U, 0x07U};
    assert_int_equal(char_write(&ctx, 0x0304U, 0U, at_sub0, 2U, buf, sizeof(buf)), 0);
    assert_int_equal(cell.seen_subindex, 0U);

    /* max_subindex 之外的子索引在进入后端之前被拒绝。 */
    char_expect_write_error(&ctx, 0x0304U, 2U, at_sub1, 2U, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    assert_int_equal(cell.value, 0x0007U);
}

/* 保留/未知索引统一返回 IDX_NOT_AVAIL。 */
static void test_char_reserved_and_unknown_indices(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t buf[8] = {0U};

    char_prepare(&ctx, &k_char_info);
    for (uint16_t index = 0x0004U; index <= 0x000BU; index++) {
        char_expect_read_error(&ctx, index, 0U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
    for (uint16_t index = 0x001CU; index <= 0x001FU; index++) {
        char_expect_read_error(&ctx, index, 0U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
    char_expect_read_error(&ctx, IOLINK_IDX_OFFSET_TIME, 0U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    char_expect_read_error(&ctx, 0x00FFU, 0U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    char_expect_read_error(&ctx, 0x0100U, 0U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    char_expect_write_error(&ctx, 0x0100U, 0U, buf, 1U, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_char_system_command, char_setup, char_teardown),
        cmocka_unit_test_setup_teardown(test_char_profile_characteristic, char_setup,
                                        char_teardown),
        cmocka_unit_test_setup_teardown(test_char_device_status, char_setup, char_teardown),
        cmocka_unit_test_setup_teardown(test_char_direct_param_page1, char_setup, char_teardown),
        cmocka_unit_test_setup_teardown(test_char_direct_param_page1_image, char_setup,
                                        char_teardown),
        cmocka_unit_test_setup_teardown(test_char_direct_param_page2, char_setup, char_teardown),
        cmocka_unit_test_setup_teardown(test_char_identification_objects, char_setup,
                                        char_teardown),
        cmocka_unit_test_setup_teardown(test_char_tag_objects, char_setup, char_teardown),
        cmocka_unit_test_setup_teardown(test_char_access_locks, char_setup, char_teardown),
        cmocka_unit_test_setup_teardown(test_char_status_and_process_data, char_setup,
                                        char_teardown),
        cmocka_unit_test_setup_teardown(test_char_standard_index_wins_over_app, char_setup,
                                        char_teardown),
        cmocka_unit_test_setup_teardown(test_char_subindex_reaches_app_write, char_setup,
                                        char_teardown),
        cmocka_unit_test_setup_teardown(test_char_reserved_and_unknown_indices, char_setup,
                                        char_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
