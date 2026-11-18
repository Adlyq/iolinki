/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_buffers.c
 * @brief 成帧应答使用单个缓冲 + 租约，两个视图槽指向同一内存。
 *
 * 旧实现用 `resp_store[2]` 双缓冲避免覆盖 ISR 正在读取的应答。租约模型
 * 改为：引擎在写缓冲之前先通过 DLL 发布字发布 Busy，ISR 看到 Busy 后不再
 * 读取缓冲；`resp_sel`/`resp_pub` 仍交替，保证发布字能区分新旧应答。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/dll.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "iolinki/workspace.h"

/* 8 位索引读请求（I-Service 0x9，索引 0x10）及其 CHKPDU。 */
static void send_start(iolink_isdu_ctx_t* isdu)
{
    uint8_t req[3] = {0x93U, 0x10U, 0x00U};
    req[2] = (uint8_t) (req[0] ^ req[1]);
    iolink_isdu_od_write(isdu, IOLINK_FLOWCTRL_START, req, sizeof(req));
}

static void test_init_shares_one_frame_buffer(void** state)
{
    (void) state;
    iolink_isdu_ctx_t isdu;
    iolink_isdu_init(&isdu);
    assert_int_equal(isdu.resp_sel, isdu.resp_pub);
    assert_non_null(isdu.resp_frame);
    /* 租约模型：两个视图槽指向同一份成帧缓冲。 */
    assert_ptr_equal(isdu.resp_store[0], isdu.resp_frame);
    assert_ptr_equal(isdu.resp_store[1], isdu.resp_frame);
    assert_ptr_equal(isdu.resp_buf, isdu.resp_frame);
}

/* 第一个请求：视图索引切换，发布字因此能与上一份应答区分。 */
static void test_request_toggles_view_index(void** state)
{
    (void) state;
    iolink_isdu_ctx_t isdu;
    iolink_isdu_init(&isdu);

    send_start(&isdu);
    assert_int_not_equal(isdu.resp_sel, isdu.resp_pub);

    /* 在发布之前再次 START：仍使用同一视图（不会无谓切换），
     * 但绝不会丢掉已经发布的应答——因为视图指向同一缓冲，发布字
     * 才是唯一的交接协议。 */
    send_start(&isdu);
    assert_int_not_equal(isdu.resp_sel, isdu.resp_pub);
}

/* 一旦发布，下一个请求切换到另一个视图索引。 */
static void test_published_view_switches_on_next_request(void** state)
{
    (void) state;
    iolink_isdu_ctx_t isdu;
    iolink_isdu_init(&isdu);
    send_start(&isdu);

    isdu.resp_pub = isdu.resp_sel;
    const uint8_t previous = isdu.resp_sel;
    send_start(&isdu);
    assert_int_not_equal(isdu.resp_sel, previous);
}

/* 外部工作区：绑定后所有缓冲都位于 workspace 内。 */
static void test_workspace_binding_moves_buffers(void** state)
{
    (void) state;
    iolink_isdu_ctx_t isdu;
    iolink_workspace_t ws;

    iolink_isdu_init(&isdu);
    iolink_isdu_bind_workspace(&isdu, &ws);
    assert_ptr_equal(isdu.buffer, ws.isdu.payload);
    assert_ptr_equal(isdu.response_buf, ws.isdu.response);
    assert_ptr_equal(isdu.req_buf, ws.isdu.request);
    assert_ptr_equal(isdu.resp_frame, ws.isdu.frame);
    assert_ptr_equal(isdu.resp_store[0], ws.isdu.frame);
    assert_ptr_equal(isdu.resp_store[1], ws.isdu.frame);
    assert_int_equal(isdu.buffer_size, sizeof(ws.isdu.payload));
    assert_int_equal(isdu.req_size, sizeof(ws.isdu.request));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_init_shares_one_frame_buffer),
        cmocka_unit_test(test_request_toggles_view_index),
        cmocka_unit_test(test_published_view_switches_on_next_request),
        cmocka_unit_test(test_workspace_binding_moves_buffers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
