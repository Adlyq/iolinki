/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 裁剪构建的行为契约。
 *
 * `IOLINK_FEATURE_DATA_STORAGE=OFF` / `IOLINK_FEATURE_EVENTS=OFF` 时完整套件
 * 无法运行（它依赖未被裁剪的栈），但这两个开关改变的是主站能观测到的
 * 应答。这里只断言这部分契约，否则"能编译"会被当成"行为正确"。
 *
 * 每个被裁掉的子系统都配一条控制断言（DeviceStatus 仍可读），避免
 * "整个 ISDU 都坏了"也被读成合规。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/isdu.h"
#include "iolinki/protocol.h"

/** @brief V1.1.5 交织格式：CHKPDU 是全部八位组（校验位清零）的 XOR。 */
static uint8_t chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c = (uint8_t) (c ^ octets[i]);
    }
    return c;
}

static int collect_response(iolink_isdu_ctx_t* ctx, uint8_t* out, size_t capacity)
{
    int n = 0;
    uint8_t byte = 0U;

    while ((n < (int) capacity) && (iolink_isdu_get_response_byte(ctx, &byte) == 1)) {
        out[n++] = byte;
    }
    return n;
}

static int read_object(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* out,
                       size_t capacity)
{
    uint8_t request[6];
    size_t n = 0U;

    if (index > 0xFFU) {
        request[n++] = 0xB5U;
        request[n++] = (uint8_t) (index >> 8U);
        request[n++] = (uint8_t) (index & 0xFFU);
        request[n++] = subindex;
    }
    else if (subindex != 0U) {
        request[n++] = 0xA4U;
        request[n++] = (uint8_t) index;
        request[n++] = subindex;
    }
    else {
        request[n++] = 0x93U;
        request[n++] = (uint8_t) index;
    }

    request[n] = 0U;
    request[n] = chkpdu(request, n + 1U);
    n++;

    iolink_isdu_od_write(ctx, IOLINK_FLOWCTRL_START, request, (uint8_t) n);
    iolink_isdu_process(ctx);
    return collect_response(ctx, out, capacity);
}

static void test_standard_object_still_readable(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t out[8] = {0};

    iolink_isdu_init(&ctx);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, out, sizeof(out)), 1);
}

#if !IOLINK_FEATURE_DATA_STORAGE
static void test_data_storage_index_unavailable(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t out[8] = {0};

    iolink_isdu_init(&ctx);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DATA_STORAGE, 0U, out, sizeof(out)), 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

static void test_data_storage_commands_unavailable(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t request[4];
    uint8_t out[8] = {0};

    /* 8 位索引 + 子索引 0 + 1 字节数据（I-Service 0x1，长度 4）。 */
    request[0] = 0x14U;
    request[1] = (uint8_t) IOLINK_IDX_SYSTEM_COMMAND;
    request[2] = IOLINK_CMD_PARAM_DOWNLOAD_START;
    request[3] = 0U;
    request[3] = chkpdu(request, sizeof(request));

    iolink_isdu_init(&ctx);
    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, request, (uint8_t) sizeof(request));
    iolink_isdu_process(&ctx);
    assert_int_equal(collect_response(&ctx, out, sizeof(out)), 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
}
#endif

#if !IOLINK_FEATURE_EVENTS
static void test_detailed_device_status_unavailable(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t out[8] = {0};

    iolink_isdu_init(&ctx);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, out, sizeof(out)), 2);
    assert_int_equal(out[0], 0x80U);
    assert_int_equal(out[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}
#endif

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_standard_object_still_readable),
#if !IOLINK_FEATURE_DATA_STORAGE
        cmocka_unit_test(test_data_storage_index_unavailable),
        cmocka_unit_test(test_data_storage_commands_unavailable),
#endif
#if !IOLINK_FEATURE_EVENTS
        cmocka_unit_test(test_detailed_device_status_unavailable),
#endif
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
