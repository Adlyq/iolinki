/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_od_app.c
 * @brief 对象字典的应用侧契约：一行表项即可新增参数。
 *
 * 这里用两个应用表验证 OD 扩展路径：
 *  - switching_sensor 的 4 个参数（真实示例）；
 *  - 一个只存在于本测试的单项表，证明"新增参数 = 一行表项"，不需要
 *    改动 src/isdu.c 或 src/od.c。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/od.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "switching_sensor.h"
#include "test_helpers.h"

static iolink_test_env_t g_od_env;

static int od_setup(void** state)
{
    (void) state;
    return 0;
}

static int od_teardown(void** state)
{
    (void) state;
    return 0;
}

/** @brief 发送读请求并收集应答体。 */
static int od_read(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* buf,
                   size_t cap)
{
    assert_int_equal(isdu_send_read_request(ctx, index, subindex), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, buf, cap);
}

/** @brief 发送写请求并收集应答体。 */
static int od_write(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, const uint8_t* data,
                    uint8_t len, uint8_t* buf, size_t cap)
{
    assert_int_equal(isdu_send_write_request(ctx, index, subindex, data, len), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, buf, cap);
}

/* -------------------------------------------------------------------------
 * 单项应用表：新增参数的全部代价
 * ------------------------------------------------------------------------- */

typedef struct
{
    uint16_t value;
} single_cell_t;

static iolink_result_t single_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                   size_t cap)
{
    const single_cell_t* cell = user;

    return iolink_od_encode(entry, cell->value, out, cap);
}

static iolink_result_t single_write(void* user, const iolink_od_entry_t* entry, uint8_t subindex,
                                    const uint8_t* data, size_t len)
{
    single_cell_t* cell = user;
    uint32_t value = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &value);

    (void) subindex;
    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    cell->value = (uint16_t) value;
    return iolink_result_ok(0U);
}

/* 一行表项新增一个 ISDU 参数 0x0200。 */
static const iolink_od_entry_t k_single_entry[] = {
    IOLINK_OD_SCALAR(0x0200U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                     IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_BACKEND, 0U, 0xFFFFU, 42U,
                     "TestOnlyParameter"),
};

static void test_od_single_entry_parameter(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_od_app_t app;
    single_cell_t cell = {.value = 42U};
    uint8_t buf[8] = {0U};

    test_env_init(&g_od_env, NULL);
    test_env_reset_params(&g_od_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_od_env);

    app.entries = k_single_entry;
    app.count = sizeof(k_single_entry) / sizeof(k_single_entry[0]);
    app.read = single_read;
    app.write = single_write;
    app.user = &cell;
    iolink_isdu_bind_od(&ctx, &app);

    /* 表项默认值经 od_default 解析，与应用初值一致；应用后端读出同一值。 */
    assert_int_equal(cell.value, 42U);
    assert_int_equal(iolink_od_default(&k_single_entry[0], buf, sizeof(buf)).len, 2U);
    assert_int_equal(buf[0], 0x00U);
    assert_int_equal(buf[1], 0x2AU);
    assert_int_equal(od_read(&ctx, 0x0200U, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x00U);
    assert_int_equal(buf[1], 0x2AU);

    /* 写后立即读回。 */
    const uint8_t value[2] = {0x12U, 0x34U};
    assert_int_equal(od_write(&ctx, 0x0200U, 0U, value, 2U, buf, sizeof(buf)), 0);
    assert_int_equal(od_read(&ctx, 0x0200U, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x12U);
    assert_int_equal(buf[1], 0x34U);
    assert_int_equal(cell.value, 0x1234U);

    /* 长度不符由 codec 统一拒绝。 */
    assert_int_equal(od_write(&ctx, 0x0200U, 0U, value, 1U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x80U);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    /* 未声明的索引仍返回 IDX_NOT_AVAIL。 */
    assert_int_equal(od_read(&ctx, 0x0201U, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* -------------------------------------------------------------------------
 * switching_sensor：4 个参数经 OD 表走 ISDU
 * ------------------------------------------------------------------------- */

static void test_od_switching_sensor(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_od_app_t app;
    switching_sensor_t sensor;
    uint8_t buf[8] = {0U};

    switching_sensor_init(&sensor);
    test_env_init(&g_od_env, NULL);
    test_env_reset_params(&g_od_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_od_env);
    switching_sensor_od_init(&app, &sensor);
    iolink_isdu_bind_od(&ctx, &app);

    /* 表项默认值与应用初值一致，并据此读出。 */
    const iolink_od_entry_t* threshold_entry =
        iolink_od_find(&app, SWITCHING_SENSOR_IDX_THRESHOLD, 0U);
    assert_non_null(threshold_entry);
    assert_int_equal(iolink_od_default(threshold_entry, buf, sizeof(buf)).len, 2U);
    assert_int_equal((buf[0] << 8) | buf[1], 5000);
    assert_int_equal(od_read(&ctx, SWITCHING_SENSOR_IDX_THRESHOLD, 0U, buf, sizeof(buf)), 2);
    assert_int_equal((buf[0] << 8) | buf[1], 5000);
    assert_int_equal(od_read(&ctx, SWITCHING_SENSOR_IDX_HYSTERESIS, 0U, buf, sizeof(buf)), 2);
    assert_int_equal((buf[0] << 8) | buf[1], 200);
    assert_int_equal(od_read(&ctx, SWITCHING_SENSOR_IDX_INVERSION, 0U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x00U);

    /* 写入 SP1 并读回。 */
    const uint8_t sp1[2] = {0x13U, 0x88U}; /* 5000 */
    assert_int_equal(od_write(&ctx, SWITCHING_SENSOR_IDX_THRESHOLD, 0U, sp1, 2U, buf, sizeof(buf)),
                     0);
    assert_int_equal(sensor.threshold, 5000U);

    /* 交叉约束：迟滞大于 SP1 被拒绝（0x30）。 */
    const uint8_t big[2] = {0x27U, 0x10U}; /* 10000 */
    assert_int_equal(od_write(&ctx, SWITCHING_SENSOR_IDX_HYSTERESIS, 0U, big, 2U, buf, sizeof(buf)),
                     2);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    assert_int_equal(sensor.hysteresis, 200U);

    /* 只写对象：读返回 NOT_ACCESSIBLE。 */
    assert_int_equal(od_read(&ctx, SWITCHING_SENSOR_IDX_TEACH, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);

    /* teach（TRIGGER + on_change）把当前测量值写成 SP1。 */
    assert_true(switching_sensor_sample(&sensor, 5432U, true));
    const uint8_t one = 1U;
    assert_int_equal(od_write(&ctx, SWITCHING_SENSOR_IDX_TEACH, 0U, &one, 1U, buf, sizeof(buf)), 0);
    assert_int_equal(sensor.threshold, 5432U);

    /* 无效测量值时 teach 被拒绝。 */
    (void) switching_sensor_sample(&sensor, 0U, false);
    assert_int_equal(od_write(&ctx, SWITCHING_SENSOR_IDX_TEACH, 0U, &one, 1U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);

    /* 子索引越界。 */
    assert_int_equal(od_read(&ctx, SWITCHING_SENSOR_IDX_THRESHOLD, 1U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* -------------------------------------------------------------------------
 * codec 与值层的错误/边界路径
 * ------------------------------------------------------------------------- */

/* 带默认值块的字节串表项（用于 iolink_od_default 的拷贝与容量不足分支）。 */
static const uint8_t k_block_default[] = {'A', 'B', 'C', 'D'};
static const iolink_od_entry_t k_octet_entry[] = {
    {.index = 0x0300U,
     .datatype = IOLINK_OD_DT_OCTET_STRING,
     .access = IOLINK_OD_ACCESS_RW,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .handler = IOLINK_OD_HANDLER_BACKEND,
     .block_len = 4U,
     .block_default = k_block_default,
     .name = "OctetParameter"},
};

/* 触发语义但没有 on_change：写入必须返回 FUNC_NOTAVAIL。 */
static const iolink_od_entry_t k_trigger_no_hook[] = {
    {.index = 0x0301U,
     .datatype = IOLINK_OD_DT_U8,
     .access = IOLINK_OD_ACCESS_WO,
     .write_semantics = IOLINK_OD_WRITE_TRIGGER,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .handler = IOLINK_OD_HANDLER_BACKEND,
     .range_max = 1U,
     .name = "TriggerWithoutHook"},
};

/* 0..1 的小范围，用于验证 codec 的越界错误码。 */
static const iolink_od_entry_t k_range_entry[] = {
    IOLINK_OD_SCALAR(0x0302U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                     IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_BACKEND, 0U, 1U, 0U,
                     "RangeOnly"),
};

static int g_od_change_calls;

static iolink_result_t track_change(void* user, const iolink_od_entry_t* entry, const uint8_t* data,
                                    size_t len)
{
    (void) user;
    (void) entry;
    (void) data;
    (void) len;
    g_od_change_calls++;
    return iolink_result_ok(0U);
}

/* STORE/VOLATILE 写后仍要执行 on_change。 */
static const iolink_od_entry_t k_change_entry[] = {
    IOLINK_OD_SCALAR_EX(0x0303U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                        IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_BACKEND, 0U, 1U, 0U,
                        "WriteWithHook", NULL, track_change),
};

static iolink_result_t failing_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                    size_t cap)
{
    (void) user;
    (void) entry;
    (void) out;
    (void) cap;
    return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL);
}

/* 标量只返回 1 字节：与表项 codec 的 2 字节不一致。 */
static iolink_result_t short_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                  size_t cap)
{
    (void) user;
    (void) entry;
    (void) out;
    (void) cap;
    return iolink_result_ok(1U);
}

/* 字节串返回超过调用方容量的长度。 */
static iolink_result_t long_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                 size_t cap)
{
    (void) user;
    (void) entry;
    (void) out;
    return iolink_result_ok((uint16_t) (cap + 1U));
}

static void test_od_codec_edges(void** state)
{
    (void) state;
    uint8_t buf[8] = {0U};
    const iolink_od_entry_t* scalar = &k_single_entry[0];
    const iolink_od_entry_t* octet = &k_octet_entry[0];
    const iolink_od_entry_t* range = &k_range_entry[0];
    uint32_t value = 0U;

    /* 未知 datatype / 表尾索引。 */
    assert_null(iolink_od_codec((uint8_t) IOLINK_OD_DT_COUNT));
    assert_null(iolink_od_codec(0xFFU));
    /* 内建标准表规模契约：新增内建项时同步更新此处。 */
    assert_int_equal(iolink_od_count(), 24U);
    assert_non_null(iolink_od_at(0U));
    assert_non_null(iolink_od_at(iolink_od_count() - 1U));
    assert_null(iolink_od_at(iolink_od_count()));

    /* decode：空参数、非标量、范围越界。 */
    assert_int_equal(iolink_od_decode(NULL, buf, 1U, &value).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_decode(scalar, buf, 1U, NULL).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_decode(octet, buf, 4U, &value).code, IOLINK_RESULT_INVALID);
    const uint8_t too_big[1] = {2U};
    assert_int_equal(iolink_od_decode(range, too_big, 1U, &value).code,
                     IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);

    /* encode：空参数、非标量、超范围与容量不足。 */
    assert_int_equal(iolink_od_encode(NULL, 0U, buf, sizeof(buf)).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_encode(scalar, 0U, NULL, sizeof(buf)).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_encode(octet, 0U, buf, sizeof(buf)).code, IOLINK_RESULT_NOT_HANDLED);
    assert_int_equal(iolink_od_encode(range, 0x100U, buf, sizeof(buf)).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_encode(scalar, 0U, buf, 1U).code, IOLINK_RESULT_INVALID);

    /* check_value：空表项、空数据但声明了长度。 */
    assert_int_equal(iolink_od_check_value(NULL, NULL, buf, 1U).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_check_value(scalar, NULL, NULL, 1U).code, IOLINK_RESULT_INVALID);
    assert_true(iolink_result_succeeded(iolink_od_check_value(octet, NULL, NULL, 0U)));
    /* OctetString 的 block_len 是写上限；block_len == 0 才表示不限制。 */
    const uint8_t block_ok[4] = {0U};
    const uint8_t block_too_long[5] = {0U};
    assert_true(iolink_result_succeeded(iolink_od_check_value(octet, NULL, block_ok, 4U)));
    assert_int_equal(iolink_od_check_value(octet, NULL, block_too_long, 5U).code,
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    const iolink_od_entry_t* detailed = iolink_od_find(NULL, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U);
    assert_non_null(detailed);
    assert_int_equal(detailed->block_len, 0U);
    assert_true(iolink_result_succeeded(iolink_od_check_value(detailed, NULL, buf, 8U)));
}

static void test_od_lookup_edges(void** state)
{
    (void) state;
    iolink_od_app_t empty = {0};

    /* 空应用表与空指针都返回 NULL，而不是解引用。 */
    assert_null(iolink_od_find_app(NULL, 0x0100U, 0U));
    assert_null(iolink_od_find_app(&empty, 0x0100U, 0U));
    assert_null(iolink_od_find(&empty, 0x0004U, 0U));

    /* DS 表尾越界。 */
    assert_null(iolink_od_ds_at(iolink_od_ds_count()));
    assert_non_null(iolink_od_ds_at(iolink_od_ds_count() - 1U));

    /* 应用侧 DS 查询的空表边界。 */
    assert_int_equal(iolink_od_ds_count_app(NULL), 0U);
    assert_null(iolink_od_ds_at_app(NULL, 0U));
    assert_null(iolink_od_ds_at_app(&empty, 0U));
    assert_false(iolink_od_is_ds(&empty, 0x0300U, 0U));
}

static void test_od_value_layer_edges(void** state)
{
    (void) state;
    uint8_t buf[8] = {0U};
    const uint8_t one = 1U;
    const iolink_od_entry_t* scalar = &k_single_entry[0];
    const iolink_od_entry_t* octet = &k_octet_entry[0];

    /* default：空参数、块默认值拷贝与容量不足。 */
    assert_int_equal(iolink_od_default(NULL, buf, sizeof(buf)).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_default(octet, buf, sizeof(buf)).len, 4);
    assert_memory_equal(buf, k_block_default, sizeof(k_block_default));
    assert_int_equal(iolink_od_default(octet, buf, 3U).code, IOLINK_RESULT_INVALID);

    /* read_value：空参数、缺后端、后端失败、线长不符、超出容量。 */
    iolink_od_ctx_t ctx = {0};
    assert_int_equal(iolink_od_read_value(NULL, scalar, 0U, buf, sizeof(buf)).code,
                     IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_read_value(&ctx, scalar, 0U, buf, sizeof(buf)).code,
                     IOLINK_RESULT_NOT_HANDLED);

    iolink_od_app_t failing_app = {0};
    failing_app.entries = k_single_entry;
    failing_app.count = 1U;
    failing_app.read = failing_read;
    ctx.app = &failing_app;
    assert_int_equal(iolink_od_read_value(&ctx, scalar, 0U, buf, sizeof(buf)).code,
                     IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL);

    iolink_od_app_t short_app = failing_app;
    short_app.read = short_read;
    ctx.app = &short_app;
    assert_int_equal(iolink_od_read_value(&ctx, scalar, 0U, buf, sizeof(buf)).code,
                     IOLINK_RESULT_INVALID);

    iolink_od_app_t long_app = failing_app;
    long_app.read = long_read;
    long_app.entries = k_octet_entry;
    ctx.app = &long_app;
    assert_int_equal(iolink_od_read_value(&ctx, octet, 0U, buf, 4U).code, IOLINK_RESULT_INVALID);

    /* write_value：空参数、TRIGGER 无钩子、TAG 无 params、缺后端、写后钩子。 */
    assert_int_equal(iolink_od_write_value(NULL, scalar, 0U, &one, 1U).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_write_value(&ctx, &k_trigger_no_hook[0], 0U, &one, 1U).code,
                     IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);

    iolink_od_ctx_t tag_ctx = {0};
    const iolink_od_entry_t* tag = iolink_od_find(NULL, IOLINK_IDX_APPLICATION_TAG, 0U);
    assert_non_null(tag);
    assert_int_equal(iolink_od_write_value(&tag_ctx, tag, 0U, &one, 1U).code,
                     IOLINK_RESULT_NOT_HANDLED);

    iolink_od_ctx_t no_app = {0};
    const uint8_t two_bytes[2] = {0U, 1U};
    assert_int_equal(iolink_od_write_value(&no_app, scalar, 0U, two_bytes, 2U).code,
                     IOLINK_RESULT_NOT_HANDLED);

    single_cell_t cell = {.value = 0U};
    iolink_od_app_t app = {0};
    app.entries = k_change_entry;
    app.count = 1U;
    app.write = single_write;
    app.user = &cell;
    iolink_od_ctx_t change_ctx = {.app = &app, .params = NULL};
    g_od_change_calls = 0;
    assert_true(iolink_result_succeeded(
        iolink_od_write_value(&change_ctx, &k_change_entry[0], 0U, &one, 1U)));
    assert_int_equal(g_od_change_calls, 1);
}

/* -------------------------------------------------------------------------
 * 表自身的契约：DS 成员、codec 错误码与默认值
 * ------------------------------------------------------------------------- */

static void test_od_table_contract(void** state)
{
    (void) state;
    uint8_t buf[8] = {0U};

    /* DS 成员只由 storage 标志决定：三个标签，其余不是。 */
    assert_int_equal(iolink_od_ds_count(), 3U);
    assert_true(iolink_od_is_ds(NULL, IOLINK_IDX_APPLICATION_TAG, 0U));
    assert_true(iolink_od_is_ds(NULL, IOLINK_IDX_FUNCTION_TAG, 0U));
    assert_true(iolink_od_is_ds(NULL, IOLINK_IDX_LOCATION_TAG, 0U));
    assert_false(iolink_od_is_ds(NULL, IOLINK_IDX_VENDOR_NAME, 0U));
    assert_false(iolink_od_is_ds(NULL, SWITCHING_SENSOR_IDX_THRESHOLD, 0U));

    /* 强制索引与厂商错误统计都在标准表内。 */
    const uint16_t present[] = {IOLINK_IDX_DIRECT_PARAMETERS_1,
                                IOLINK_IDX_DIRECT_PARAMETERS_2,
                                IOLINK_IDX_SYSTEM_COMMAND,
                                IOLINK_IDX_DATA_STORAGE,
                                IOLINK_IDX_DEVICE_ACCESS_LOCKS,
                                IOLINK_IDX_PROFILE_CHARACTERISTIC,
                                IOLINK_IDX_VENDOR_NAME,
                                IOLINK_IDX_PRODUCT_URI,
                                IOLINK_IDX_ERROR_COUNT,
                                IOLINK_IDX_DEVICE_STATUS,
                                IOLINK_IDX_DETAILED_DEVICE_STATUS,
                                IOLINK_IDX_PROCESS_DATA_INPUT,
                                IOLINK_IDX_PROCESS_DATA_OUTPUT,
                                IOLINK_IDX_ERROR_STATS};
    for (size_t i = 0U; i < (sizeof(present) / sizeof(present[0])); i++) {
        assert_non_null(iolink_od_find(NULL, present[i], 0U));
    }
    assert_null(iolink_od_find(NULL, 0x0004U, 0U)); /* Table B.8 保留 */
    assert_null(iolink_od_find(NULL, 0x001CU, 0U));

    /* codec：长度与范围错误码唯一。 */
    const iolink_od_entry_t* status_entry = iolink_od_find(NULL, IOLINK_IDX_DEVICE_STATUS, 0U);
    assert_non_null(status_entry);
    uint32_t value = 0U;
    assert_int_equal(iolink_od_decode(status_entry, buf, 2U, &value).code,
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    const iolink_od_entry_t* app_tag = iolink_od_find(NULL, IOLINK_IDX_APPLICATION_TAG, 0U);
    assert_non_null(app_tag);
    assert_int_equal(iolink_od_default(app_tag, buf, sizeof(buf)).len, 0);
    assert_int_equal(iolink_od_default(&k_single_entry[0], buf, sizeof(buf)).len, 2);
    assert_int_equal(buf[1], 0x2AU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_od_single_entry_parameter, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_switching_sensor, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_table_contract, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_codec_edges, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_lookup_edges, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_value_layer_edges, od_setup, od_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
