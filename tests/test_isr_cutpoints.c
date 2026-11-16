/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isr_cutpoints.c
 * @brief 无锁 ISR 交接的确定性切点测试。
 *
 * 宿主测试套件是单线程的，因此并发通过在消费者的每个步骤
 * 注入数据 ISR 的发布来建模：
 *
 *  - 消费者刚 peek 之后（复制记录之前），
 *  - 刚复制之后（释放槽位之前），
 *  - 刚释放之后（下一次 peek 之前），
 *
 * 另有一个端到端用例：PHY 回调在主泵进入 iolink_dll_process() 期间发布记录，
 * 即正在前往邮箱的途中。
 *
 * 被测性质是 SPSC 环约定：生产者绝不写入消费者正在读取的
 * 槽位，且记录的元数据随记录一起传递。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "dll_access.h"
#include "iolinki/iolink.h"

static uint64_t now_us;

static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return now_us;
}

/** @brief 完全按数据 ISR 的方式发布一个帧记录。 */
static bool publish_frame(iolink_dll_ctx_t* ctx, uint8_t tag, uint8_t len)
{
    iolink_frame_rec_t* rec =
        (iolink_frame_rec_t*) iolink_ring_claim(&ctx->main.shared.isr_frame_ring);
    if (rec == NULL) {
        ctx->main.shared.isr_mailbox_drops++;
        return false;
    }
    rec->len = len;
    rec->eager = (uint8_t) (tag & 1U);
    for (uint8_t i = 0U; i < len; i++) {
        rec->data[i] = (uint8_t) (tag + i);
    }
    iolink_ring_publish(&ctx->main.shared.isr_frame_ring);
    return true;
}

static void reset_rings(iolink_dll_ctx_t* ctx)
{
    iolink_ring_init(&ctx->main.shared.isr_frame_ring, (uint8_t*) ctx->main.isr_frame_slot,
                     IOLINK_MBX_SLOTS, (uint8_t) sizeof(iolink_frame_rec_t));
}

/* 生产者在消费者的每一对步骤之间发布；被消费的
 * 记录必须保持完整，而排队的记录必须随后送达。 */
static void test_frame_cutpoints(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));

    for (uint8_t cut = 0U; cut < 3U; cut++) {
        reset_rings(&ctx);

        assert_true(publish_frame(&ctx, 0x10U, 4U));

        const iolink_frame_rec_t* rec =
            (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.main.shared.isr_frame_ring);
        assert_non_null(rec);

        if (cut == 0U) {
            assert_true(publish_frame(&ctx, 0x20U, 3U)); /* peek 之后被抢占 */
        }
        uint8_t copy[8];
        const uint8_t n = rec->len;
        const uint8_t eager = rec->eager;
        (void) memcpy(copy, rec->data, n);
        if (cut == 1U) {
            assert_true(publish_frame(&ctx, 0x20U, 3U)); /* copy 之后被抢占 */
        }
        iolink_ring_release(&ctx.main.shared.isr_frame_ring);
        if (cut == 2U) {
            assert_true(publish_frame(&ctx, 0x20U, 3U)); /* release 之后被抢占 */
        }

        /* 被消费的记录从未被生产者触碰。 */
        assert_int_equal(n, 4U);
        assert_int_equal(eager, 0U);
        for (uint8_t i = 0U; i < n; i++) {
            assert_int_equal(copy[i], (uint8_t) (0x10U + i));
        }

        /* 第二条记录完整无损，且排在队首。 */
        const iolink_frame_rec_t* next =
            (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.main.shared.isr_frame_ring);
        assert_non_null(next);
        assert_int_equal(next->len, 3U);
        assert_int_equal(next->eager, 0U);
        for (uint8_t i = 0U; i < next->len; i++) {
            assert_int_equal(next->data[i], (uint8_t) (0x20U + i));
        }
        iolink_ring_release(&ctx.main.shared.isr_frame_ring);
        assert_true(iolink_ring_is_empty(&ctx.main.shared.isr_frame_ring));
    }
    assert_int_equal(ctx.main.shared.isr_mailbox_drops, 0U);
}

/* 环满时拒绝生产者，而不是覆盖已租用的槽位。 */
static void test_frame_full_rejects(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    reset_rings(&ctx);

    for (uint8_t i = 0U; i < IOLINK_MBX_SLOTS; i++) {
        assert_true(publish_frame(&ctx, (uint8_t) (0x30U + i), 2U));
    }
    assert_false(publish_frame(&ctx, 0x50U, 2U));
    assert_int_equal(ctx.main.shared.isr_mailbox_drops, 1U);
    assert_int_equal(iolink_ring_count(&ctx.main.shared.isr_frame_ring), IOLINK_MBX_SLOTS);

    /* 排队记录按 FIFO 保持完整。 */
    const iolink_frame_rec_t* first =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.main.shared.isr_frame_ring);
    assert_non_null(first);
    assert_int_equal(first->data[0], 0x30U);
    iolink_ring_release(&ctx.main.shared.isr_frame_ring);
    const iolink_frame_rec_t* second =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.main.shared.isr_frame_ring);
    assert_non_null(second);
    assert_int_equal(second->data[0], 0x31U);
    iolink_ring_release(&ctx.main.shared.isr_frame_ring);
}

/* 在主泵进入 iolink_dll_process()、尚未消费邮箱时
 * 发布第二帧的 PHY 回调（挂在故障位图诊断轮询上）。 */
static uint8_t g_publish_on_poll;
static iolink_dll_ctx_t* g_poll_ctx;

static uint8_t poll_faults_publish(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (g_publish_on_poll != 0U) {
        g_publish_on_poll = 0U;
        assert_true(publish_frame(g_poll_ctx, 0x90U, 2U));
    }
    return 0U; /* 无故障：不产生事件 */
}

/* 端到端：真实的消费者（dll_consume_isr_mailbox）每趟排空一条记录，
 * 而生产者在一趟执行中途发布。 */
static void test_main_pass_cutpoint(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    const iolink_phy_api_t phy = {
        .self = NULL,
        .get_faults = poll_faults_publish,
    };

    iolink_dll_init(&ctx.main, &ctx.isr, &phy);
    ctx.main.shared.clock.time_us = test_time_us;
    ctx.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.enforce_timing = false;
    ctx.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    ctx.main.shared.od_width = 1U;
    now_us = 1000000U;

    reset_rings(&ctx);
    assert_true(publish_frame(&ctx, 0xA0U, 2U));

    g_poll_ctx = &ctx;
    g_publish_on_poll = 1U;
    iolink_dll_process(&ctx.main); /* 轮询诊断 -> 发布 B -> 消费 A */

    assert_int_equal(iolink_ring_count(&ctx.main.shared.isr_frame_ring), 1U);
    assert_int_equal(ctx.main.shared.isr_mailbox_drops, 0U);
    const iolink_frame_rec_t* pending =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.main.shared.isr_frame_ring);
    assert_non_null(pending);
    assert_int_equal(pending->len, 2U);
    assert_int_equal(pending->data[0], 0x90U);
    assert_int_equal(pending->data[1], 0x91U);

    iolink_dll_process(&ctx.main); /* 消费 B */
    assert_true(iolink_ring_is_empty(&ctx.main.shared.isr_frame_ring));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_frame_cutpoints),
        cmocka_unit_test(test_frame_full_rejects),
        cmocka_unit_test(test_main_pass_cutpoint),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
