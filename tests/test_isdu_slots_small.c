/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_slots_small.c
 * @brief 小 ISDU 缓冲（64 B）下双请求槽仍能完成请求。
 *
 * 该目标链接一份用 `IOLINK_ISDU_BUFFER_SIZE=64` 编译的协议栈
 * （`iolink_isdu_slots_fixture`），因此单槽容量是 70 字节。请求按
 * FlowCTRL 逐段投递，两条请求交替使用两个槽，验证槽步长、发布字与
 * `req_size` 边界在非默认容量下同样成立。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/isdu.h"
#include "iolinki/od.h"
#include "iolinki/protocol.h"

static iolink_isdu_ctx_t s_ctx;
static uint8_t s_value[2];

static const iolink_od_entry_t k_entries[2] = {
    {
        .index = 0x0040U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U8,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .flags = 0U,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = &s_value[0], .str_len = NULL},
        .block_len = 0U,
        .range_min = 0U,
        .range_max = 255U,
        .scalar_default = 0U,
        .block_default = NULL,
        .name = "small-a",
        .validate = NULL,
        .on_change = NULL,
    },
    {
        .index = 0x0041U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U8,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .flags = 0U,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = &s_value[1], .str_len = NULL},
        .block_len = 0U,
        .range_min = 0U,
        .range_max = 255U,
        .scalar_default = 0U,
        .block_default = NULL,
        .name = "small-b",
        .validate = NULL,
        .on_change = NULL,
    },
};

static const iolink_od_app_t k_app = {.entries = k_entries, .count = 2U};

static void feed_write(const uint8_t* req, uint8_t len)
{
    uint8_t fc = (uint8_t) IOLINK_FLOWCTRL_START;
    for (uint8_t i = 0U; i < len; i++) {
        iolink_isdu_od_write(&s_ctx, fc, &req[i], 1U);
        fc = (fc == (uint8_t) IOLINK_FLOWCTRL_START)
                 ? 1U
                 : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
}

static uint8_t make_write(uint8_t* out, uint16_t index, uint8_t value)
{
    out[0] = 0x36U;
    out[1] = (uint8_t) (index >> 8);
    out[2] = (uint8_t) (index & 0xFFU);
    out[3] = 0U;
    out[4] = value;
    uint8_t chk = 0U;
    for (uint8_t i = 0U; i < 5U; i++) {
        chk = (uint8_t) (chk ^ out[i]);
    }
    out[5] = chk;
    return 6U;
}

static void test_requests_complete_with_small_slots(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_ISDU_BUFFER_SIZE, 64U);

    s_value[0] = 0U;
    s_value[1] = 0U;
    iolink_isdu_init(&s_ctx);
    iolink_isdu_bind_od(&s_ctx, &k_app, NULL);
    iolink_isdu_bind_caps(&s_ctx, NULL);

    /* 单槽 70 字节：两槽步长必须按它计算。 */
    assert_int_equal(s_ctx.req_size, 70U);

    uint8_t req_a[6];
    uint8_t req_b[6];
    const uint8_t len_a = make_write(req_a, 0x0040U, 0x5AU);
    const uint8_t len_b = make_write(req_b, 0x0041U, 0xA5U);

    /* 第一条不 pump：第二条必须进另一个槽，且第一条仍可执行。 */
    feed_write(req_a, len_a);
    const uint8_t sel_a = IOLINK_ISDU_REQ_PUB_SEL(s_ctx.req_pub);
    assert_int_equal(IOLINK_ISDU_REQ_PUB_LEN(s_ctx.req_pub), len_a);
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value[0], 0x5AU);

    feed_write(req_b, len_b);
    assert_int_not_equal(IOLINK_ISDU_REQ_PUB_SEL(s_ctx.req_pub), sel_a);
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value[1], 0xA5U);

    /* 两个槽都用过之后，第一条请求的槽重新可用。 */
    feed_write(req_a, len_a);
    assert_int_equal(IOLINK_ISDU_REQ_PUB_SEL(s_ctx.req_pub), sel_a);
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value[0], 0x5AU);
    assert_int_equal(s_ctx.req_overruns, 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_requests_complete_with_small_slots),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
