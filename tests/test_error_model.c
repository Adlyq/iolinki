/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_error_model.c
 * @brief 统一结果模型（result.h）的契约测试。
 *
 * 验证 P3 的核心承诺：
 *  - 未知索引与"未处理"用负的内部码区分；无效参数返回 INVALID；
 *  - 协议层错误以正的 ISDU ErrorCode 返回，不被压缩成 IDX_NOT_AVAIL；
 *  - 参数长度边界：32 字节接受、33 字节拒绝（0x33）；
 *  - DS 暂存路径：限长内接受、超限返回 0x33、镜像满返回 INVALID；
 *  - 端到端：正码穿过 ISDU 应答边界，线上应答为 0x80 0x33。
 *
 * 明确不覆盖 IOLINK_RESULT_IO（需要存储后端失败），属于显式取舍。
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "iolinki/isdu.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "iolinki/result.h"
#include "test_helpers.h"

static iolink_test_env_t g_env;

static int test_setup(void** state)
{
    (void) state;
    test_env_init(&g_env, NULL);
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    return 0;
}

/** @brief 返回成功（空响应）的厂商桩；端到端用例在 DS 分支即返回，
 * 该桩不会被执行。 */
static uint8_t test_vendor_service(void* user, uint16_t index, uint8_t subindex, bool write,
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
    (void) output_length;
    return 0U;
}

/* 未处理的索引不得伪装成协议错误：调用方据此回退到下一层。
   0x0100 不是保留索引，也不属于 params 管理的标签集。 */
static void test_unknown_index_is_not_handled(void** state)
{
    (void) state;
    uint8_t buffer[8] = {0U};

    const iolink_result_t read_result =
        iolink_params_ctx_read(&g_env.params, 0x0100U, 0U, buffer, sizeof(buffer));
    assert_int_equal(read_result.code, IOLINK_RESULT_NOT_HANDLED);
    assert_int_equal(read_result.len, 0U);

    const iolink_result_t write_result =
        iolink_params_ctx_write(&g_env.params, 0x0100U, 0U, buffer, 1U, IOLINK_PERSIST_NONE);
    assert_int_equal(write_result.code, IOLINK_RESULT_NOT_HANDLED);
    assert_int_equal(write_result.len, 0U);
}

/* 无效参数与"未处理"是两种负码。 */
static void test_invalid_arguments_are_invalid(void** state)
{
    (void) state;
    static const uint8_t data[1] = {0U};
    const iolink_result_t read_result =
        iolink_params_ctx_read(&g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, NULL, 0U);
    assert_int_equal(read_result.code, IOLINK_RESULT_INVALID);

    const iolink_result_t write_result = iolink_params_ctx_write(
        &g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, NULL, 1U, IOLINK_PERSIST_NONE);
    assert_int_equal(write_result.code, IOLINK_RESULT_INVALID);

    const iolink_result_t null_ctx_result = iolink_params_ctx_write(
        NULL, IOLINK_IDX_APPLICATION_TAG, 0U, data, 1U, IOLINK_PERSIST_NONE);
    assert_int_equal(null_ctx_result.code, IOLINK_RESULT_INVALID);
}

/* 边界：32 字节（标签缓冲区上限）必须接受，33 字节必须拒绝为 0x33。 */
static void test_tag_length_boundary(void** state)
{
    (void) state;
    uint8_t data[33];
    uint8_t readback[33];
    (void) memset(data, 'B', sizeof(data));

    const iolink_result_t write_ok = iolink_params_ctx_write(
        &g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, data, 32U, IOLINK_PERSIST_NONE);
    assert_int_equal(write_ok.code, 0);
    assert_int_equal(write_ok.len, 0U);

    const iolink_result_t read_ok = iolink_params_ctx_read(
        &g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, readback, sizeof(readback));
    assert_int_equal(read_ok.code, 0);
    assert_int_equal(read_ok.len, 32U);
    assert_memory_equal(readback, data, 32U);

    const iolink_result_t write_overrun = iolink_params_ctx_write(
        &g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, data, 33U, IOLINK_PERSIST_NONE);
    assert_int_equal(write_overrun.code, IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    assert_int_equal(write_overrun.len, 0U);
}

/* 成功结果携带传输字节数，读回内容与写入一致。 */
static void test_round_trip_reports_length(void** state)
{
    (void) state;
    static const uint8_t tag[] = {'O', 'K'};
    uint8_t readback[8];

    const iolink_result_t write_result = iolink_params_ctx_write(
        &g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, tag, sizeof(tag), IOLINK_PERSIST_NONE);
    assert_int_equal(write_result.code, 0);
    assert_int_equal(write_result.len, 0U);

    const iolink_result_t read_result = iolink_params_ctx_read(
        &g_env.params, IOLINK_IDX_APPLICATION_TAG, 0U, readback, sizeof(readback));
    assert_int_equal(read_result.code, 0);
    assert_int_equal(read_result.len, sizeof(tag));
    assert_memory_equal(readback, tag, sizeof(tag));
}

/** @brief 初始化一个处于下载状态的 DS 上下文（描述符由调用方提供）。 */
static void ds_download_begin(iolink_ds_ctx_t* ds, const iolink_ds_param_desc_t* set, size_t count)
{
    iolink_ds_init(ds, NULL);
    iolink_ds_bind_param_access(ds, NULL, NULL, NULL, set, count);
    /* 0x03 同时是 ParamDownloadStart ISDU 命令与 DS_DownloadStart 命令。 */
    assert_int_equal(iolink_ds_handle_ds_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
}

/* 正控制：限长内的暂存必须成功并写入镜像。 */
static void test_ds_stage_accepts_within_limit(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_ds_param_desc_t set[] = {{0x0100U, 0U, 2U}};
    static const uint8_t data[2] = {0xAAU, 0x55U};

    ds_download_begin(&ds, set, 1U);
    const iolink_result_t result = iolink_ds_stage_param(&ds, 0x0100U, 0U, data, sizeof(data));
    assert_int_equal(result.code, 0);
    assert_int_equal(result.len, 0U);
    assert_int_equal(ds.image_len, 4U + sizeof(data));
}

/* DS 暂存路径的值长度超长返回正 0x33，供 ISDU 边界上送。 */
static void test_ds_stage_length_overrun_is_protocol_error(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_ds_param_desc_t set[] = {{0x0100U, 0U, 2U}};
    static const uint8_t data[3] = {0x01U, 0x02U, 0x03U};

    ds_download_begin(&ds, set, 1U);
    const iolink_result_t result = iolink_ds_stage_param(&ds, 0x0100U, 0U, data, sizeof(data));
    assert_int_equal(result.code, IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    assert_int_equal(result.len, 0U);
}

/* 镜像缓冲区满（4 + 253 > IOLINK_DS_IMAGE_MAX）返回负的内部码。 */
static void test_ds_stage_rejects_when_image_is_full(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    static const iolink_ds_param_desc_t set[] = {{0x0101U, 0U, 0U}};
    uint8_t data[253];
    (void) memset(data, 0x5AU, sizeof(data));

    ds_download_begin(&ds, set, 1U);
    const iolink_result_t result = iolink_ds_stage_param(&ds, 0x0101U, 0U, data, sizeof(data));
    assert_int_equal(result.code, IOLINK_RESULT_INVALID);
    assert_int_equal(result.len, 0U);
}

/* 端到端：正码穿过 ISDU 应答边界，线上应答为 0x80 0x33。 */
static void test_isdu_reports_positive_error_code(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_isdu_ctx_t isdu;
    static const iolink_ds_param_desc_t set[] = {{0x0100U, 0U, 2U}};
    static const uint8_t data[3] = {0x01U, 0x02U, 0x03U};
    uint8_t response[4];

    ds_download_begin(&ds, set, 1U);
    iolink_isdu_init(&isdu);
    isdu.ds_ctx = &ds;
    isdu.vendor_service = test_vendor_service;
    isdu.vendor_user = NULL;

    assert_int_equal(isdu_send_write_request(&isdu, 0x0100U, 0U, data, sizeof(data)), 1);
    iolink_isdu_process(&isdu);
    assert_int_equal(isdu_collect_response(&isdu, response, sizeof(response)), 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_unknown_index_is_not_handled, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_invalid_arguments_are_invalid, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_tag_length_boundary, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_round_trip_reports_length, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_ds_stage_accepts_within_limit, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_ds_stage_length_overrun_is_protocol_error, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_ds_stage_rejects_when_image_is_full, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reports_positive_error_code, test_setup,
                                        test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
