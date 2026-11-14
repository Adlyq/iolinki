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

#include "iolinki/dll_ctx.h"

static void prepare_ctx(iolink_dll_ctx_t* ctx)
{
    (void) memset(ctx, 0, sizeof(*ctx));
    ctx->main.shared.framing_errors = 3U;
    ctx->main.shared.timeout_errors = 5U;
    ctx->main.shared.isr_tx_dropped = 2U;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    ctx->main.shared.isr_stat.framing_errors = 4U;
    ctx->main.shared.isr_stat.timeout_errors = 6U;
    ctx->main.shared.isr_stat.isr_tx_dropped = 3U;
#endif
}

static void test_public_stats_mirror_the_totals(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;

    prepare_ctx(&ctx);
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.framing_errors, IOLINK_STAT_TOTAL(&ctx.main, framing_errors));
    assert_int_equal(stats.timeout_errors, IOLINK_STAT_TOTAL(&ctx.main, timeout_errors));
    assert_int_equal(stats.isr_tx_dropped, IOLINK_STAT_TOTAL(&ctx.main, isr_tx_dropped));
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
    assert_ptr_not_equal(&IOLINK_STAT_ISR(&ctx.main, framing_errors),
                         &ctx.main.shared.framing_errors);
    IOLINK_STAT_ISR(&ctx.main, framing_errors)++;
    assert_int_equal(ctx.main.shared.framing_errors, 3U);
    assert_int_equal(ctx.main.shared.isr_stat.framing_errors, 5U);
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
    assert_int_equal(stats.isr_tx_dropped, ctx.main.shared.isr_tx_dropped);
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
