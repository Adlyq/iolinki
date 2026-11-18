/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_stats_split.c
 * @brief 检查两种 IOLINK_STATS_MODE 取值下的统计计数器集合。
 *
 * SHARED：只有一个集合，公共统计镜像上下文字段。
 * SPLIT：ISR 拥有第二个集合，公共统计报告两者之和，
 * 且 ISR 侧的自增不会触及主上下文字段。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "dll_access.h"

static void prepare_ctx(iolink_dll_ctx_t* ctx)
{
    (void) memset(ctx, 0, sizeof(*ctx));
    ctx->main.shared.b_framing_errors = 3U;
    ctx->main.shared.b_timeout_errors = 5U;
    ctx->main.shared.b_isr_tx_dropped = 2U;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    ctx->main.shared.b_isr_stat.framing_errors = 4U;
    ctx->main.shared.b_isr_stat.timeout_errors = 6U;
    ctx->main.shared.b_isr_stat.isr_tx_dropped = 3U;
#endif
}

/* Counts a known event sequence into the counter slots and asserts the totals
 * as literals: neither IOLINK_STAT_TOTAL nor IOLINK_STAT_ISR takes part in the
 * expected value, so a wrong summation in dll.h cannot pass silently. */
static void test_public_stats_mirror_the_totals(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;

    (void) memset(&ctx, 0, sizeof(ctx));

    /* Main-context events. */
    ctx.main.shared.b_framing_errors = 3U;
    ctx.main.shared.b_timeout_errors = 2U;
    ctx.main.shared.b_isr_tx_dropped = 1U;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    /* ISR-side events in the ISR-owned counter set. */
    ctx.main.shared.b_isr_stat.framing_errors = 2U;
    ctx.main.shared.b_isr_stat.timeout_errors = 4U;
    ctx.main.shared.b_isr_stat.isr_tx_dropped = 5U;
#endif

    iolink_dll_get_stats(&ctx.main, &stats);

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    /* SPLIT: both sets are reported, summed. */
    assert_int_equal(stats.framing_errors, 5U); /* 3 + 2 */
    assert_int_equal(stats.timeout_errors, 6U); /* 2 + 4 */
    assert_int_equal(stats.isr_tx_dropped, 6U); /* 1 + 5 */
#else
    /* SHARED: a single set; the ISR increments the same fields. */
    assert_int_equal(stats.framing_errors, 3U);
    assert_int_equal(stats.timeout_errors, 2U);
    assert_int_equal(stats.isr_tx_dropped, 1U);
#endif
}

static void test_stats_totals_per_mode(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;

    prepare_ctx(&ctx);

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    /* ISR 槽位与主上下文字段相互独立：对其自增
     * 不会改动主计数器（这正是 split 的意义）。 */
    assert_ptr_not_equal(&IOLINK_STAT_ISR(&ctx.main.shared, b_framing_errors, framing_errors),
                         &ctx.main.shared.b_framing_errors);
    IOLINK_STAT_ISR(&ctx.main.shared, b_framing_errors, framing_errors)++;
    assert_int_equal(ctx.main.shared.b_framing_errors, 3U);
    assert_int_equal(ctx.main.shared.b_isr_stat.framing_errors, 5U);
#endif

    iolink_dll_get_stats(&ctx.main, &stats);

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    assert_int_equal(stats.framing_errors, 8U);  /* 主上下文 3 + ISR 5 */
    assert_int_equal(stats.timeout_errors, 11U); /* 主上下文 5 + ISR 6 */
    assert_int_equal(stats.isr_tx_dropped, 5U);  /* 主上下文 2 + ISR 3 */
#else
    assert_int_equal(stats.framing_errors, 3U);
    assert_int_equal(stats.timeout_errors, 5U);
    /* 只有一个集合：ISR 直接自增主上下文字段本身。 */
    assert_int_equal(stats.isr_tx_dropped, ctx.main.shared.b_isr_tx_dropped);
    assert_int_equal(stats.isr_tx_dropped, 2U);
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_public_stats_mirror_the_totals),
        cmocka_unit_test(test_stats_totals_per_mode),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
