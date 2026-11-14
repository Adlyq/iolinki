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
    IOLINK_OD_SCALAR_HANDLER(0x0200U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFFFU, 42U,
                             "TestOnlyParameter", single_read, single_write),
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

    /* OD 层自己也守住 max_subindex：直接调用值层（不经 ISDU）同样拒绝。 */
    const iolink_od_entry_t* threshold = iolink_od_find(&app, SWITCHING_SENSOR_IDX_THRESHOLD, 0U);
    iolink_od_ctx_t od = {.app = &app};
    assert_non_null(threshold);
    assert_int_equal(iolink_od_read_value(&od, threshold, 1U, buf, sizeof(buf)).code,
                     IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    assert_int_equal(iolink_od_write_value(&od, threshold, 1U, buf, 2U).code,
                     IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
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
     .handler = IOLINK_OD_HANDLER_NONE,
     .backend = IOLINK_OD_BACKEND_HANDLER,
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
     .handler = IOLINK_OD_HANDLER_NONE,
     .backend = IOLINK_OD_BACKEND_HANDLER,
     .range_max = 1U,
     .name = "TriggerWithoutHook"},
};

/* 0..1 的小范围，用于验证 codec 的越界错误码。 */
static const iolink_od_entry_t k_range_entry[] = {
    IOLINK_OD_SCALAR_HANDLER(0x0302U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 1U, 0U,
                             "RangeOnly", NULL, NULL),
};

/* HANDLER 表项缺少 read：读路径返回 NOT_HANDLED。 */
static const iolink_od_entry_t k_no_read_entry[] = {
    IOLINK_OD_SCALAR_HANDLER(0x0305U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 1U, 0U,
                             "NoRead", NULL, NULL),
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
    IOLINK_OD_SCALAR_HANDLER_EX(0x0303U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                                IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 1U, 0U,
                                "WriteWithHook", single_read, single_write, NULL, track_change),
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
    assert_int_equal(iolink_od_count(), 21U);
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

    /* 标准表不含 DS 项；DS 成员全部来自应用的标签表。 */
    assert_int_equal(iolink_od_ds_count(), 0U);
    assert_null(iolink_od_ds_at(0U));
    const iolink_od_app_t* tags = test_default_tag_od();
    assert_int_equal(iolink_od_ds_count_app(tags), 3U);
    assert_null(iolink_od_ds_at_app(tags, iolink_od_ds_count_app(tags)));
    assert_non_null(iolink_od_ds_at_app(tags, iolink_od_ds_count_app(tags) - 1U));

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

    /* read_value：空参数、缺应用表、缺 read、后端失败、线长不符、超出容量。 */
    iolink_od_ctx_t ctx = {0};
    assert_int_equal(iolink_od_read_value(NULL, scalar, 0U, buf, sizeof(buf)).code,
                     IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_read_value(&ctx, scalar, 0U, buf, sizeof(buf)).code,
                     IOLINK_RESULT_NOT_HANDLED); /* HANDLER 但 app == NULL */
    single_cell_t read_cell = {.value = 0U};
    iolink_od_ctx_t handler_ctx = {0};
    iolink_od_app_t app_cell = {.entries = k_single_entry, .count = 1U, .user = &read_cell};
    handler_ctx.app = &app_cell;
    assert_int_equal(
        iolink_od_read_value(&handler_ctx, &k_no_read_entry[0], 0U, buf, sizeof(buf)).code,
        IOLINK_RESULT_NOT_HANDLED);
    assert_int_equal(iolink_od_read_value(&handler_ctx, scalar, 0U, buf, 1U).code,
                     IOLINK_RESULT_INVALID);

    iolink_od_entry_t failing_entry = k_single_entry[0];
    failing_entry.u.handler.read = failing_read;
    assert_int_equal(iolink_od_read_value(&handler_ctx, &failing_entry, 0U, buf, sizeof(buf)).code,
                     IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL);

    iolink_od_entry_t short_entry = k_single_entry[0];
    short_entry.u.handler.read = short_read;
    assert_int_equal(iolink_od_read_value(&handler_ctx, &short_entry, 0U, buf, sizeof(buf)).code,
                     IOLINK_RESULT_INVALID);

    iolink_od_entry_t long_entry = k_octet_entry[0];
    long_entry.u.handler.read = long_read;
    assert_int_equal(iolink_od_read_value(&handler_ctx, &long_entry, 0U, buf, 4U).code,
                     IOLINK_RESULT_INVALID);

    /* write_value：空参数、TRIGGER 无钩子、无 app、写后钩子。 */
    assert_int_equal(iolink_od_write_value(NULL, scalar, 0U, &one, 1U).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_write_value(&ctx, &k_trigger_no_hook[0], 0U, &one, 1U).code,
                     IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);

    iolink_od_ctx_t no_app = {0};
    const uint8_t two_bytes[2] = {0U, 1U};
    assert_int_equal(iolink_od_write_value(&no_app, scalar, 0U, two_bytes, 2U).code,
                     IOLINK_RESULT_NOT_HANDLED);

    single_cell_t cell = {.value = 0U};
    iolink_od_app_t app = {0};
    app.entries = k_change_entry;
    app.count = 1U;
    app.user = &cell;
    iolink_od_ctx_t change_ctx = {.app = &app};
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

    /* DS 成员只由 storage 标志决定：标准表无 DS 项，标签归应用表。 */
    assert_int_equal(iolink_od_ds_count(), 0U);
    const iolink_od_app_t* tags = test_default_tag_od();
    assert_int_equal(iolink_od_ds_count_app(tags), 3U);
    assert_true(iolink_od_is_ds(tags, IOLINK_IDX_APPLICATION_TAG, 0U));
    assert_true(iolink_od_is_ds(tags, IOLINK_IDX_FUNCTION_TAG, 0U));
    assert_true(iolink_od_is_ds(tags, IOLINK_IDX_LOCATION_TAG, 0U));
    assert_false(iolink_od_is_ds(tags, IOLINK_IDX_VENDOR_NAME, 0U));
    assert_false(iolink_od_is_ds(tags, SWITCHING_SENSOR_IDX_THRESHOLD, 0U));

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

    const iolink_od_entry_t* app_tag = iolink_od_find(tags, IOLINK_IDX_APPLICATION_TAG, 0U);
    assert_non_null(app_tag);
    assert_int_equal(iolink_od_default(app_tag, buf, sizeof(buf)).len, 0);
    assert_int_equal(iolink_od_default(&k_single_entry[0], buf, sizeof(buf)).len, 2);
    assert_int_equal(buf[1], 0x2AU);
}

/* -------------------------------------------------------------------------
 * 索引域路由：规范索引不可遮蔽，设备专用索引回落到内建表
 * ------------------------------------------------------------------------- */

static iolink_result_t shadow_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                   size_t cap)
{
    (void) user;
    (void) entry;
    if (cap < 1U) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    out[0] = 0x5AU;
    return iolink_result_ok(1U);
}

static const iolink_od_entry_t k_shadow_entries[] = {
    IOLINK_OD_SCALAR_HANDLER(0x0024U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFU, 0U,
                             "ShadowDeviceStatus", shadow_read, NULL),
    IOLINK_OD_SCALAR_HANDLER(0x0040U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFU, 0U,
                             "DeviceSpecificParam", shadow_read, NULL),
};

static void test_od_index_domain_routing(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    iolink_od_app_t app = {0};
    uint8_t buf[16] = {0U};

    (void) memset(&dll, 0, sizeof(dll));
    test_env_init(&g_od_env, NULL);
    test_env_reset_params(&g_od_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_od_env);
    ctx.dll_ctx = &dll.main;

    app.entries = k_shadow_entries;
    app.count = sizeof(k_shadow_entries) / sizeof(k_shadow_entries[0]);
    iolink_isdu_bind_od(&ctx, &app);

    /* 规范索引 0x0024 由标准表提供，应用表不能遮蔽。 */
    assert_int_equal(od_read(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, buf, sizeof(buf)), 1);
    assert_int_not_equal(buf[0], 0x5AU);

    /* 0x0040 属于设备专用索引：应用表优先于内建错误统计。 */
    assert_int_equal(od_read(&ctx, IOLINK_IDX_ERROR_STATS, 0U, buf, sizeof(buf)), 1);
    assert_int_equal(buf[0], 0x5AU);

    /* 未声明的设备专用索引与保留值都返回 0x11。 */
    const uint16_t unavailable[] = {0x0080U, 0x003FU, 0x00FFU, 0x00E4U};
    for (size_t i = 0U; i < (sizeof(unavailable) / sizeof(unavailable[0])); i++) {
        assert_int_equal(od_read(&ctx, unavailable[i], 0U, buf, sizeof(buf)), 2);
        assert_int_equal(buf[0], 0x80U);
        assert_int_equal(buf[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
}

static void test_od_device_specific_fallback(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    uint8_t buf[16] = {0U};

    (void) memset(&dll, 0, sizeof(dll));
    dll.main.crc_errors = 0x11223344U;
    dll.main.shared.timeout_errors = 0xAABBCCDDU;
    dll.main.shared.framing_errors = 0x01020304U;
    dll.main.timing_errors = 0x05060708U;

    test_env_init(&g_od_env, NULL);
    test_env_reset_params(&g_od_env);
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_od_env);
    ctx.dll_ctx = &dll.main;

    /* 没有应用表时 0x0040 回落到标准表里的内建错误统计对象。 */
    assert_int_equal(od_read(&ctx, IOLINK_IDX_ERROR_STATS, 0U, buf, sizeof(buf)), 16);
    const uint8_t expected[16] = {0x11U, 0x22U, 0x33U, 0x44U, 0xAAU, 0xBBU, 0xCCU, 0xDDU,
                                  0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
    assert_memory_equal(buf, expected, sizeof(expected));

    /* 表外设备专用索引没有可回落的处理器。 */
    assert_int_equal(od_read(&ctx, 0x0080U, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* 截断的请求不得执行任何表项：一律得到否定应答。 */
static void test_od_truncated_requests_never_execute(void** state)
{
    (void) state;
    const uint8_t requests[][4] = {
        {0x34, 0x01, 0x35, 0x00},                           /* 缺少子索引的 16 位写。 */
        {0xB4, 0x01, 0xB5, 0x00},                           /* 缺少子索引的 16 位读。 */
        {0x33, 0x01, 0x32, 0x00},                           /* 缺少低索引字节/子索引的 16 位写。 */
        {0xB3, 0x01, 0xB2, 0x00}, {0x23, 0x01, 0x22, 0x00}, /* 缺少子索引的 8 位写。 */
        {0xA3, 0x01, 0xA2, 0x00}, {0x12, 0x12, 0x00, 0x00}, /* 缺少索引的 8 位写。 */
        {0x92, 0x92, 0x00, 0x00}, {0x32, 0x32, 0x00, 0x00}, /* 两个索引字节都缺失的 16 位写。 */
        {0xB2, 0xB2, 0x00, 0x00},
    };
    for (size_t vector = 0; vector < sizeof(requests) / sizeof(requests[0]); vector++) {
        iolink_isdu_ctx_t ctx;
        iolink_isdu_init(&ctx);
        ctx.od = NULL;
        const size_t length = (size_t) (requests[vector][0] & 0x0FU);
        for (size_t i = 0; i < length; i++) {
            iolink_isdu_od_write(&ctx, i == 0 ? IOLINK_FLOWCTRL_START : (uint8_t) i,
                                 requests[vector] + i, 1);
        }
        iolink_isdu_process(&ctx);
        assert_true(ctx.response_negative);
        assert_int_equal(ctx.response_len, 2);
        assert_int_equal(ctx.response_buf[0], 0x80U);
    }
}

/* STORAGE 标量：读写同一块应用变量，应用本地改值立即可见。 */
static void test_od_storage_scalar_round_trip(void** state)
{
    (void) state;
    static uint16_t value = 0x1111U;
    static const iolink_od_entry_t entries[] = {
        IOLINK_OD_SCALAR_STORAGE(0x0200U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                 IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFFFU,
                                 0U, "StorageScalar", &value),
    };
    iolink_od_app_t app = {.entries = entries, .count = 1U, .user = NULL};
    iolink_od_ctx_t ctx = {.app = &app};
    const uint8_t wire[2] = {0x12U, 0x34U};
    uint8_t out[2] = {0U};

    const iolink_result_t written = iolink_od_write_value(&ctx, &entries[0], 0U, wire, 2U);
    assert_int_equal(written.code, 0);
    assert_int_equal(written.len, 0U);
    assert_int_equal(value, 0x1234U);

    const iolink_result_t read = iolink_od_read_value(&ctx, &entries[0], 0U, out, sizeof(out));
    assert_int_equal(read.code, 0);
    assert_int_equal(read.len, 2U);
    assert_memory_equal(out, wire, sizeof(wire));

    /* 单一事实源：应用本地改值，主站下一次读取即为新值。 */
    value = 0xBEEFU;
    assert_true(
        iolink_result_succeeded(iolink_od_read_value(&ctx, &entries[0], 0U, out, sizeof(out))));
    assert_int_equal(out[0], 0xBEU);
    assert_int_equal(out[1], 0xEFU);
}

/* STORAGE 字节串：写入更新缓冲与长度；超过 block_len 返回 0x33。 */
static void test_od_storage_string_round_trip(void** state)
{
    (void) state;
    static uint8_t buffer[8];
    static uint8_t length;
    static const iolink_od_entry_t entries[] = {
        IOLINK_OD_BYTES_STORAGE(0x0201U, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                                IOLINK_OD_STORAGE_VOLATILE, 8U, "StorageString", buffer, &length),
    };
    iolink_od_app_t app = {.entries = entries, .count = 1U, .user = NULL};
    iolink_od_ctx_t ctx = {.app = &app};
    const uint8_t wire[3] = {'A', 'B', 'C'};
    uint8_t out[8] = {0U};

    const iolink_result_t written = iolink_od_write_value(&ctx, &entries[0], 0U, wire, 3U);
    assert_int_equal(written.code, 0);
    assert_int_equal(written.len, 0U);
    assert_int_equal(length, 3U);
    assert_memory_equal(buffer, wire, sizeof(wire));

    const iolink_result_t read = iolink_od_read_value(&ctx, &entries[0], 0U, out, sizeof(out));
    assert_int_equal(read.code, 0);
    assert_int_equal(read.len, 3U);
    assert_memory_equal(out, wire, sizeof(wire));

    const uint8_t too_long[9] = {0U};
    assert_int_equal(iolink_od_write_value(&ctx, &entries[0], 0U, too_long, sizeof(too_long)).code,
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    assert_int_equal(length, 3U);
}

/* on_change 拒绝时 STORAGE 标量回滚到写前值。 */
static iolink_result_t reject_change(void* user, const iolink_od_entry_t* entry,
                                     const uint8_t* data, size_t len)
{
    (void) user;
    (void) entry;
    (void) data;
    (void) len;
    return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
}

static void test_od_storage_scalar_rollback(void** state)
{
    (void) state;
    static uint16_t value = 5U;
    static const iolink_od_entry_t entries[] = {
        IOLINK_OD_SCALAR_STORAGE_EX(0x0202U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                    IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U,
                                    0xFFFFU, 0U, "RollbackScalar", &value, NULL, reject_change),
    };
    iolink_od_app_t app = {.entries = entries, .count = 1U, .user = NULL};
    iolink_od_ctx_t ctx = {.app = &app};
    const uint8_t wire[2] = {0x00U, 0x07U};

    assert_int_equal(iolink_od_write_value(&ctx, &entries[0], 0U, wire, 2U).code,
                     IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    assert_int_equal(value, 5U);
}

/* 字节串的 on_change 拒绝不回滚内容：拒绝应放在 validate。 */
static void test_od_storage_string_on_change_keeps_value(void** state)
{
    (void) state;
    static uint8_t buffer[8];
    static uint8_t length;
    static const iolink_od_entry_t entries[] = {
        IOLINK_OD_BYTES_STORAGE_EX(0x0212U, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                                   IOLINK_OD_STORAGE_VOLATILE, 8U, "StringHook", buffer, &length,
                                   NULL, reject_change),
    };
    iolink_od_app_t app = {.entries = entries, .count = 1U, .user = NULL};
    iolink_od_ctx_t ctx = {.app = &app};
    const uint8_t wire[6] = {'L', 'O', 'N', 'G', 'E', 'R'};
    uint8_t out[8] = {0U};

    assert_int_equal(iolink_od_write_value(&ctx, &entries[0], 0U, wire, sizeof(wire)).code,
                     IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    assert_int_equal(length, sizeof(wire));
    const iolink_result_t read = iolink_od_read_value(&ctx, &entries[0], 0U, out, sizeof(out));
    assert_int_equal(read.len, sizeof(wire));
    assert_memory_equal(out, wire, sizeof(wire));
}

/* 恢复路径绕过 validate/on_change，只写后端。 */
static int g_restore_hooks;

static iolink_result_t restore_validate(void* user, const iolink_od_entry_t* entry,
                                        const uint8_t* data, size_t len)
{
    (void) user;
    (void) entry;
    (void) data;
    (void) len;
    g_restore_hooks++;
    return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
}

static iolink_result_t restore_change(void* user, const iolink_od_entry_t* entry,
                                      const uint8_t* data, size_t len)
{
    (void) user;
    (void) entry;
    (void) data;
    (void) len;
    g_restore_hooks++;
    return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
}

static iolink_result_t restore_handler_write(void* user, const iolink_od_entry_t* entry,
                                             uint8_t subindex, const uint8_t* data, size_t len)
{
    uint16_t* value = user;
    uint32_t raw = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &raw);

    (void) subindex;
    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    *value = (uint16_t) raw;
    return iolink_result_ok(0U);
}

static void test_od_restore_value_paths(void** state)
{
    (void) state;
    static uint16_t storage_value;
    static uint16_t handler_value;
    static uint8_t string_buffer[8];
    static uint8_t string_length;
    static const iolink_od_entry_t entries[] = {
        IOLINK_OD_SCALAR_STORAGE_EX(0x0210U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                    IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFFFU,
                                    0U, "RestoreStorage", &storage_value, restore_validate,
                                    restore_change),
        IOLINK_OD_SCALAR_HANDLER(0x0211U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                 IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_VOLATILE, 0U, 0xFFFFU, 0U,
                                 "RestoreHandler", NULL, restore_handler_write),
        IOLINK_OD_SCALAR_HANDLER(0x0213U, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                                 IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE, 0U, 1U, 0U,
                                 "NoWriter", NULL, NULL),
        IOLINK_OD_BYTES_STORAGE(0x0214U, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                                IOLINK_OD_STORAGE_VOLATILE, 4U, "RestoreBytes", string_buffer,
                                &string_length),
    };
    test_env_init(&g_od_env, NULL);
    test_env_reset_params(&g_od_env);
    iolink_od_app_t app = {.entries = entries, .count = 4U, .user = &handler_value};
    iolink_od_ctx_t ctx = {.app = &app};
    const uint8_t wire[2] = {0x12U, 0x34U};
    const uint8_t too_long[5] = {0U};
    uint8_t tag_out[4] = {0U};

    assert_int_equal(iolink_od_restore_value(NULL, &entries[0], 0U, wire, 2U).code,
                     IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_restore_value(&ctx, NULL, 0U, wire, 2U).code, IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_restore_value(&ctx, &entries[0], 1U, wire, 2U).code,
                     IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    g_restore_hooks = 0;
    assert_true(iolink_result_succeeded(iolink_od_restore_value(&ctx, &entries[0], 0U, wire, 2U)));
    assert_int_equal(storage_value, 0x1234U);
    assert_true(iolink_result_succeeded(iolink_od_restore_value(&ctx, &entries[1], 0U, wire, 2U)));
    assert_int_equal(handler_value, 0x1234U);
    assert_int_equal(g_restore_hooks, 0U); /* 钩子不参与恢复 */

    assert_int_equal(iolink_od_restore_value(&ctx, &entries[2], 0U, wire, 2U).code,
                     IOLINK_RESULT_NOT_HANDLED);
    assert_int_equal(
        iolink_od_restore_value(&ctx, &entries[3], 0U, too_long, sizeof(too_long)).code,
        IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    assert_int_equal(iolink_od_restore_value(&ctx, &entries[0], 0U, wire, 1U).code,
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    /* 内建处理器与无效上下文：恢复路径不处理。 */
    const iolink_od_entry_t* builtin = iolink_od_find(NULL, IOLINK_IDX_SYSTEM_COMMAND, 0U);
    assert_non_null(builtin);
    assert_int_equal(iolink_od_restore_value(&ctx, builtin, 0U, wire, 1U).code,
                     IOLINK_RESULT_NOT_HANDLED);
    assert_int_equal(
        iolink_od_read_value(&ctx, builtin, 0U, string_buffer, sizeof(string_buffer)).code,
        IOLINK_RESULT_NOT_HANDLED);

    /* TAG 恢复走标签存储，不经过 hook。 */
    const iolink_od_entry_t* tag =
        iolink_od_find(test_params_od(test_env_params(&g_od_env)), IOLINK_IDX_APPLICATION_TAG, 0U);
    assert_non_null(tag);
    assert_true(iolink_result_succeeded(iolink_od_restore_value(&ctx, tag, 0U, wire, 2U)));
    const iolink_result_t tag_read = test_params_read(
        test_env_params(&g_od_env), IOLINK_IDX_APPLICATION_TAG, 0U, tag_out, sizeof(tag_out));
    assert_int_equal(tag_read.len, 2U);
    assert_memory_equal(tag_out, wire, sizeof(wire));
    assert_int_equal(g_restore_hooks, 0U);
}

/* STORAGE 后端的防御分支：空指针、缺失长度、截断、无效 datatype。 */
static void test_od_storage_backend_edges(void** state)
{
    (void) state;
    static uint8_t block[4] = {1U, 2U, 3U, 4U};
    static uint8_t block_len = 8U;
    static const iolink_od_entry_t entries[] = {
        {.index = 0x0220U,
         .datatype = IOLINK_OD_DT_U8,
         .access = IOLINK_OD_ACCESS_RW,
         .write_semantics = IOLINK_OD_WRITE_VOLATILE,
         .storage = IOLINK_OD_STORAGE_VOLATILE,
         .handler = IOLINK_OD_HANDLER_NONE,
         .backend = IOLINK_OD_BACKEND_STORAGE,
         .u.storage = {.value = NULL, .str_len = NULL},
         .range_max = 0xFFU,
         .name = "NoValue"},
        {.index = 0x0221U,
         .datatype = IOLINK_OD_DT_OCTET_STRING,
         .access = IOLINK_OD_ACCESS_RW,
         .write_semantics = IOLINK_OD_WRITE_VOLATILE,
         .storage = IOLINK_OD_STORAGE_VOLATILE,
         .handler = IOLINK_OD_HANDLER_NONE,
         .backend = IOLINK_OD_BACKEND_STORAGE,
         .u.storage = {.value = block, .str_len = NULL},
         .block_len = sizeof(block),
         .name = "NoLength"},
        {.index = 0x0222U,
         .datatype = IOLINK_OD_DT_OCTET_STRING,
         .access = IOLINK_OD_ACCESS_RW,
         .write_semantics = IOLINK_OD_WRITE_VOLATILE,
         .storage = IOLINK_OD_STORAGE_VOLATILE,
         .handler = IOLINK_OD_HANDLER_NONE,
         .backend = IOLINK_OD_BACKEND_STORAGE,
         .u.storage = {.value = block, .str_len = &block_len},
         .block_len = sizeof(block),
         .name = "Clamped"},
        {.index = 0x0223U,
         .datatype = 0xFFU,
         .access = IOLINK_OD_ACCESS_RW,
         .write_semantics = IOLINK_OD_WRITE_VOLATILE,
         .storage = IOLINK_OD_STORAGE_VOLATILE,
         .handler = IOLINK_OD_HANDLER_NONE,
         .backend = IOLINK_OD_BACKEND_STORAGE,
         .u.storage = {.value = block, .str_len = &block_len},
         .name = "BadCodec"},
    };
    iolink_od_app_t app = {.entries = entries, .count = 4U, .user = NULL};
    iolink_od_ctx_t ctx = {.app = &app};
    uint8_t out[8] = {0U};
    const uint8_t wire[2] = {0x00U, 0x07U};

    assert_int_equal(iolink_od_read_value(&ctx, &entries[0], 0U, out, sizeof(out)).code,
                     IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_write_value(&ctx, &entries[0], 0U, wire, 1U).code,
                     IOLINK_RESULT_INVALID);

    assert_int_equal(iolink_od_read_value(&ctx, &entries[1], 0U, out, sizeof(out)).code,
                     IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_restore_value(&ctx, &entries[1], 0U, wire, 1U).code,
                     IOLINK_RESULT_INVALID);

    /* 长度变量超过 block_len：读取按 block_len 截断，容量不足报 0x33。 */
    const iolink_result_t clamped = iolink_od_read_value(&ctx, &entries[2], 0U, out, sizeof(out));
    assert_int_equal(clamped.len, 4U);
    assert_memory_equal(out, block, 4U);
    assert_int_equal(iolink_od_read_value(&ctx, &entries[2], 0U, out, 2U).code,
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    const uint8_t too_long[5] = {0U};
    assert_int_equal(
        iolink_od_restore_value(&ctx, &entries[2], 0U, too_long, sizeof(too_long)).code,
        IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    /* 无效 datatype：读取与恢复都拒绝。 */
    assert_int_equal(iolink_od_read_value(&ctx, &entries[3], 0U, out, sizeof(out)).code,
                     IOLINK_RESULT_INVALID);
    assert_int_equal(iolink_od_restore_value(&ctx, &entries[3], 0U, wire, 1U).code,
                     IOLINK_RESULT_INVALID);
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
        cmocka_unit_test_setup_teardown(test_od_storage_scalar_round_trip, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_storage_string_round_trip, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_storage_scalar_rollback, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_storage_string_on_change_keeps_value, od_setup,
                                        od_teardown),
        cmocka_unit_test_setup_teardown(test_od_restore_value_paths, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_storage_backend_edges, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_index_domain_routing, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_device_specific_fallback, od_setup, od_teardown),
        cmocka_unit_test_setup_teardown(test_od_truncated_requests_never_execute, od_setup,
                                        od_teardown),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
