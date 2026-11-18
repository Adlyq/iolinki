/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_stage.c
 * @brief ISDU 传输层复位的作用域：读复位不释放写入口相位。
 *
 * 读方向（IDLE/IDLE2/ABORT/读完成）只复位主泵独占的读传输层，数据 ISR
 * 已建立的写请求（`req_idx`/`req_total`/`flowctrl_seen`/`stage_state`）保持
 * 原样；整个传输层复位（初始化与错误恢复）才同时释放两者。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "isdu_internal.h"

/* 16 位索引 + 子索引 + 1 字节载荷的写请求，共 6 八位组。索引 0x0000 是
 * Direct Parameter page 1（只读），因此完整请求的线上应答是
 * WRITE_PROTECTED。 */
static const uint8_t k_page1_write[6] = {0x36U, 0x00U, 0x00U, 0x00U, 0x42U, 0x74U};

static void feed_isdu_write(iolink_isdu_ctx_t* ctx, const uint8_t* req, size_t len)
{
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < len; i++) {
        iolink_isdu_od_write(ctx, fc, &req[i], 1U);
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
}

static void expect_write_protected(iolink_isdu_ctx_t* ctx)
{
    uint8_t resp[4] = {0U, 0U, 0U, 0U};
    iolink_isdu_od_read(ctx, IOLINK_FLOWCTRL_START, resp, sizeof(resp));
    assert_int_equal(resp[0], 0x44U);
    assert_int_equal(resp[1], 0x80U);
    assert_int_equal(resp[2], IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(resp[3], (uint8_t) (resp[0] ^ resp[1] ^ resp[2]));
}

/* 读复位只清读传输层；整个传输层复位才释放写入口相位。 */
static void test_read_reset_keeps_write_phase(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, &k_page1_write[0], 1U);
    assert_int_equal(ctx.req_idx, 1U);
    assert_int_equal(ctx.req_total, sizeof(k_page1_write));
    assert_true(ctx.flowctrl_seen);
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_RECEPTION);

    isdu_read_reset(&ctx);

    assert_int_equal(ctx.req_idx, 1U);
    assert_int_equal(ctx.req_total, sizeof(k_page1_write));
    assert_true(ctx.flowctrl_seen);
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_RECEPTION);
    assert_int_equal(ctx.state, ISDU_STATE_IDLE);

    isdu_transport_reset(&ctx);

    assert_int_equal(ctx.req_idx, 0U);
    assert_int_equal(ctx.req_total, 0U);
    assert_false(ctx.flowctrl_seen);
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_IDLE);
}

/* 读 IDLE 不丢弃 ISR 正在累计的写请求：补齐 COUNT 后仍能执行。 */
static void test_read_idle_keeps_pending_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, &k_page1_write[0], 1U);

    uint8_t octet = 0xAAU;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_IDLE, &octet, 1U);
    assert_int_equal(octet, 0x00U);
    assert_int_equal(ctx.req_idx, 1U);
    assert_int_equal(ctx.req_total, sizeof(k_page1_write));
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_RECEPTION);

    for (size_t i = 1U; i < sizeof(k_page1_write); i++) {
        iolink_isdu_od_write(&ctx, (uint8_t) i, &k_page1_write[i], 1U);
    }
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_COMPLETE);

    iolink_isdu_process(&ctx);
    expect_write_protected(&ctx);
}

/* 读 IDLE 不接管（也不丢弃）已收齐但尚未执行的写请求。 */
static void test_read_idle_keeps_complete_request(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    feed_isdu_write(&ctx, k_page1_write, sizeof(k_page1_write));
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_COMPLETE);

    uint8_t octet = 0xAAU;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_IDLE, &octet, 1U);
    assert_int_equal(octet, 0x00U);
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_COMPLETE);
    assert_int_equal(ctx.req_idx, sizeof(k_page1_write));

    iolink_isdu_process(&ctx);
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_IDLE);
    expect_write_protected(&ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_reset_keeps_write_phase),
        cmocka_unit_test(test_read_idle_keeps_pending_write),
        cmocka_unit_test(test_read_idle_keeps_complete_request),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
