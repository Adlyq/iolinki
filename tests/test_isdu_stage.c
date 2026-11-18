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
 * 已建立的写请求（`req_idx`/`req_total`/`flowctrl_seen`/`stage_state`）
 * 保持原样——主泵从不改写这些字段，这是无锁接管的全部前提。另外覆盖
 * DPP1 映像在失效序号下的 ISR 读，以及数据 ISR 内的 workspace 重绑定拒绝。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "dll_access.h"
#include "iolinki/device_info.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "iolinki/workspace.h"
#include "isdu_internal.h"

/* 16 位索引 + 子索引 + 1 字节载荷的写请求，共 6 八位组。索引 0x0000 是
 * Direct Parameter page 1（只读），因此完整请求的线上应答是
 * WRITE_PROTECTED。 */
static const uint8_t k_page1_write[6] = {0x36U, 0x00U, 0x00U, 0x00U, 0x42U, 0x74U};

/* 只用于把 `in_isr` 置位的伪 DLL 上下文：没有初始化，不驱动 DLL 逻辑。 */
static iolink_dll_ctx_t s_dll;

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

/* 读复位只清读传输层；写入口相位与发布字都保持原样。 */
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
    /* 读复位不推进 ack：写入口那一代仍然未处理。 */
    assert_int_equal(ctx.req_ack_gen, 0U);
    assert_int_not_equal(IOLINK_ISDU_REQ_PUB_GEN(ctx.req_pub), ctx.req_ack_gen);
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
    /* 相位闩锁在 COMPLETE（ISR 单写），主泵以 ack 代号表达"已处理"。 */
    assert_int_equal(ctx.stage_state, IOLINK_ISDU_STAGE_COMPLETE);
    assert_int_equal(ctx.req_ack_gen, IOLINK_ISDU_REQ_PUB_GEN(ctx.req_pub));
    expect_write_protected(&ctx);
}

/* 主泵上下文：首次读取重建映像，单字节与整段都读到当前一代。 */
static void test_dpp1_stable_image_reads(void** state)
{
    (void) state;
    static const iolink_device_info_t identity = {
        .vendor_id = 0x1234U,
        .device_id = 0x005678U,
        .min_cycle_time = 10U,
        .revision_id = IOLINK_REVISION_ID_1_1,
    };
    iolink_device_info_ctx_t info;
    iolink_isdu_ctx_t ctx;

    assert_int_equal(iolink_device_info_ctx_init(&info, &identity), 0);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info;

    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x04U), IOLINK_REVISION_ID_1_1);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x07U), 0x12U);
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x08U), 0x34U);

    uint8_t range[2] = {0U, 0U};
    assert_true(iolink_isdu_dp_page1_read_range(&ctx, 0x07U, range, (uint8_t) sizeof(range)));
    assert_int_equal(range[0], 0x12U);
    assert_int_equal(range[1], 0x34U);
}

/* 序号为奇数（主泵正在覆盖映像或映像已失效）时，ISR 读只返回不可用，
 * 三次重试后单字节为 0、整段清零且返回 false。 */
static void test_dpp1_odd_sequence_reads_zero(void** state)
{
    (void) state;
    static const iolink_device_info_t identity = {
        .vendor_id = 0x1234U,
        .device_id = 0x005678U,
        .min_cycle_time = 10U,
        .revision_id = IOLINK_REVISION_ID_1_1,
    };
    iolink_device_info_ctx_t info;
    iolink_isdu_ctx_t ctx;

    assert_int_equal(iolink_device_info_ctx_init(&info, &identity), 0);
    iolink_isdu_init(&ctx);
    ctx.device_info = &info;
    (void) iolink_isdu_direct_param_page1_octet(&ctx, 0x04U); /* 建立稳定映像 */

    (void) memset(&s_dll, 0, sizeof(s_dll));
    s_dll.main.shared.in_isr = 1U;
    ctx.dll_ctx = &s_dll.main;

    /* 稳定的一代仍可读：ISR 侧只读，不重建整页。 */
    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x04U), IOLINK_REVISION_ID_1_1);

    ctx.dp_page1_seq = (uint8_t) (ctx.dp_page1_seq | 1U);

    assert_int_equal(iolink_isdu_direct_param_page1_octet(&ctx, 0x04U), 0U);

    uint8_t range[4];
    (void) memset(range, 0xAA, sizeof(range));
    assert_false(iolink_isdu_dp_page1_read_range(&ctx, 0x04U, range, (uint8_t) sizeof(range)));
    const uint8_t zeros[4] = {0U, 0U, 0U, 0U};
    assert_memory_equal(range, zeros, sizeof(zeros));
}

/* 数据 ISR 内重绑定 workspace 被拒绝，指针不变；数据泵静止后同一调用生效。 */
static void test_bind_workspace_rejected_in_isr(void** state)
{
    (void) state;
    iolink_workspace_t ws;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    uint8_t* const builtin_buffer = ctx.buffer;
    uint8_t* const builtin_req = ctx.req_buf;
    assert_non_null(builtin_buffer);
    assert_non_null(builtin_req);

    (void) memset(&s_dll, 0, sizeof(s_dll));
    s_dll.main.shared.in_isr = 1U;
    ctx.dll_ctx = &s_dll.main;

    iolink_isdu_bind_workspace(&ctx, &ws);
    assert_null(ctx.workspace);
    assert_ptr_equal(ctx.buffer, builtin_buffer);
    assert_ptr_equal(ctx.req_buf, builtin_req);

    s_dll.main.shared.in_isr = 0U;
    iolink_isdu_bind_workspace(&ctx, &ws);
    assert_ptr_equal(ctx.workspace, &ws);
    assert_ptr_equal(ctx.buffer, ws.isdu.payload);
    assert_ptr_equal(ctx.req_buf, ws.isdu.request);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_read_reset_keeps_write_phase),
        cmocka_unit_test(test_read_idle_keeps_pending_write),
        cmocka_unit_test(test_read_idle_keeps_complete_request),
        cmocka_unit_test(test_dpp1_stable_image_reads),
        cmocka_unit_test(test_dpp1_odd_sequence_reads_zero),
        cmocka_unit_test(test_bind_workspace_rejected_in_isr),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
