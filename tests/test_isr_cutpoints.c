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

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/dll.h"
#include "iolinki/iolink.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

/** @brief 完全按数据 ISR 的方式发布一个帧记录。 */
static bool publish_frame(iolink_dll_ctx_t* ctx, uint8_t tag, uint8_t len)
{
    iolink_frame_rec_t* rec = (iolink_frame_rec_t*) iolink_ring_claim(&ctx->isr_frame_ring);
    if (rec == NULL) {
        ctx->isr_mailbox_drops++;
        return false;
    }
    rec->len = len;
    rec->eager = (uint8_t) (tag & 1U);
    for (uint8_t i = 0U; i < len; i++) {
        rec->data[i] = (uint8_t) (tag + i);
    }
    iolink_ring_publish(&ctx->isr_frame_ring);
    return true;
}

/** @brief 完全按数据 ISR 的方式发布一个已暂存的 ISDU 写段。 */
static bool publish_od(iolink_dll_ctx_t* ctx, uint8_t flowctrl, uint8_t tag)
{
    iolink_od_rec_t* rec = (iolink_od_rec_t*) iolink_ring_claim(&ctx->isr_od_ring);
    if (rec == NULL) {
        ctx->isr_od_overruns++;
        return false;
    }
    rec->flowctrl = flowctrl;
    rec->len = 2U;
    rec->data[0] = tag;
    rec->data[1] = (uint8_t) (tag ^ 0xFFU);
    iolink_ring_publish(&ctx->isr_od_ring);
    return true;
}

static void reset_rings(iolink_dll_ctx_t* ctx)
{
    iolink_ring_init(&ctx->isr_frame_ring, (uint8_t*) ctx->isr_frame_slot, IOLINK_MBX_SLOTS,
                     (uint8_t) sizeof(iolink_frame_rec_t));
    iolink_ring_init(&ctx->isr_od_ring, (uint8_t*) ctx->isr_od_slot, IOLINK_OD_SLOTS,
                     (uint8_t) sizeof(iolink_od_rec_t));
}

/* 生产者在消费者的每一对步骤之间发布；被消费的
 * 记录必须保持完整，而排队的记录必须随后送达。 */
static int test_frame_cutpoints(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));

    for (uint8_t cut = 0U; cut < 3U; cut++) {
        reset_rings(&ctx);

        assert(publish_frame(&ctx, 0x10U, 4U));

        const iolink_frame_rec_t* rec =
            (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
        assert(rec != NULL);

        if (cut == 0U) {
            assert(publish_frame(&ctx, 0x20U, 3U)); /* peek 之后被抢占 */
        }
        uint8_t copy[8];
        const uint8_t n = rec->len;
        const uint8_t eager = rec->eager;
        (void) memcpy(copy, rec->data, n);
        if (cut == 1U) {
            assert(publish_frame(&ctx, 0x20U, 3U)); /* copy 之后被抢占 */
        }
        iolink_ring_release(&ctx.isr_frame_ring);
        if (cut == 2U) {
            assert(publish_frame(&ctx, 0x20U, 3U)); /* release 之后被抢占 */
        }

        /* 被消费的记录从未被生产者触碰。 */
        assert(n == 4U);
        assert(eager == 0U);
        for (uint8_t i = 0U; i < n; i++) {
            assert(copy[i] == (uint8_t) (0x10U + i));
        }

        /* 第二条记录完整无损，且排在队首。 */
        const iolink_frame_rec_t* next =
            (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
        assert(next != NULL);
        assert(next->len == 3U);
        assert(next->eager == 0U);
        for (uint8_t i = 0U; i < next->len; i++) {
            assert(next->data[i] == (uint8_t) (0x20U + i));
        }
        iolink_ring_release(&ctx.isr_frame_ring);
        assert(iolink_ring_is_empty(&ctx.isr_frame_ring));
    }
    assert(ctx.isr_mailbox_drops == 0U);
    return 0;
}

/* 环满时拒绝生产者，而不是覆盖已租用的槽位。 */
static int test_frame_full_rejects(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    reset_rings(&ctx);

    assert(publish_frame(&ctx, 0x30U, 2U));
    assert(publish_frame(&ctx, 0x40U, 2U));
    assert(!publish_frame(&ctx, 0x50U, 2U));
    assert(ctx.isr_mailbox_drops == 1U);
    assert(iolink_ring_count(&ctx.isr_frame_ring) == IOLINK_MBX_SLOTS);

    /* 两条排队记录保持不变。 */
    const iolink_frame_rec_t* first =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
    assert(first != NULL);
    assert(first->data[0] == 0x30U);
    iolink_ring_release(&ctx.isr_frame_ring);
    const iolink_frame_rec_t* second =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
    assert(second != NULL);
    assert(second->data[0] == 0x40U);
    iolink_ring_release(&ctx.isr_frame_ring);
    return 0;
}

/* 暂存 ISDU 写环上的同一组切点性质。 */
static int test_od_cutpoints(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    reset_rings(&ctx);

    assert(publish_od(&ctx, IOLINK_FLOWCTRL_START, 0x61U));

    const iolink_od_rec_t* rec = (const iolink_od_rec_t*) iolink_ring_peek(&ctx.isr_od_ring);
    assert(rec != NULL);
    assert(publish_od(&ctx, 1U, 0x71U)); /* peek 之后被抢占 */
    uint8_t fc = rec->flowctrl;
    uint8_t len = rec->len;
    uint8_t od[IOLINK_OD_MAX_SIZE];
    (void) memcpy(od, rec->data, len);
    iolink_ring_release(&ctx.isr_od_ring);

    assert(fc == IOLINK_FLOWCTRL_START);
    assert(len == 2U);
    assert(od[0] == 0x61U);
    assert(od[1] == (uint8_t) (0x61U ^ 0xFFU));

    const iolink_od_rec_t* next = (const iolink_od_rec_t*) iolink_ring_peek(&ctx.isr_od_ring);
    assert(next != NULL);
    assert(next->flowctrl == 1U);
    assert(next->data[0] == 0x71U);
    iolink_ring_release(&ctx.isr_od_ring);
    assert(ctx.isr_od_overruns == 0U);
    return 0;
}

/* 在主泵进入 iolink_dll_process()、尚未消费邮箱时
 * 发布第二帧的 PHY 回调（挂在故障位图诊断轮询上）。 */
static uint8_t g_publish_on_poll;
static iolink_dll_ctx_t* g_poll_ctx;

static uint8_t poll_faults_publish(void* user)
{
    (void) user;
    if (g_publish_on_poll != 0U) {
        g_publish_on_poll = 0U;
        assert(publish_frame(g_poll_ctx, 0x90U, 2U));
    }
    return 0U; /* 无故障：不产生事件 */
}

/* 端到端：真实的消费者（dll_consume_isr_mailbox）每趟排空一条记录，
 * 而生产者在一趟执行中途发布。 */
static int test_main_pass_cutpoint(void)
{
    iolink_dll_ctx_t ctx;
    const iolink_phy_api_t phy = {
        .user = NULL,
        .get_faults = poll_faults_publish,
    };

    iolink_dll_init(&ctx, &phy);
    ctx.time_us = test_time_us;
    ctx.state = IOLINK_DLL_STATE_PREOPERATE;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    ctx.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    now_us = 1000000U;

    reset_rings(&ctx);
    assert(publish_frame(&ctx, 0xA0U, 2U));

    g_poll_ctx = &ctx;
    g_publish_on_poll = 1U;
    iolink_dll_process(&ctx); /* 轮询诊断 -> 发布 B -> 消费 A */

    assert(iolink_ring_count(&ctx.isr_frame_ring) == 1U);
    assert(ctx.isr_mailbox_drops == 0U);
    const iolink_frame_rec_t* pending =
        (const iolink_frame_rec_t*) iolink_ring_peek(&ctx.isr_frame_ring);
    assert(pending != NULL);
    assert(pending->len == 2U);
    assert(pending->data[0] == 0x90U);
    assert(pending->data[1] == 0x91U);

    iolink_dll_process(&ctx); /* 消费 B */
    assert(iolink_ring_is_empty(&ctx.isr_frame_ring));
    return 0;
}

int main(void)
{
    int rc = test_frame_cutpoints();
    if (rc != 0) {
        return rc;
    }
    rc = test_frame_full_rejects();
    if (rc != 0) {
        return rc;
    }
    rc = test_od_cutpoints();
    if (rc != 0) {
        return rc;
    }
    rc = test_main_pass_cutpoint();
    if (rc != 0) {
        return rc;
    }
    return 0;
}
