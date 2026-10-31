/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_stats_split.c
 * @brief Checks the statistics counter sets in both IOLINK_STATS_MODE values.
 *
 * SHARED: one set, the public statistics mirror the context fields.
 * SPLIT: the ISR owns a second set, the public statistics report the sum, and
 * an ISR increment never touches the main-context field.
 */

#include <assert.h>
#include <string.h>

#include "iolinki/dll.h"

int main(void)
{
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;

    (void) memset(&ctx, 0, sizeof(ctx));
    ctx.framing_errors = 3U;
    ctx.timeout_errors = 5U;
    ctx.isr_tx_dropped = 2U;

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    ctx.isr_stat.framing_errors = 4U;
    ctx.isr_stat.timeout_errors = 6U;
    ctx.isr_stat.isr_tx_dropped = 3U;

    /* The ISR slot is distinct from the main-context field: incrementing it
     * leaves the main counter untouched (that is the point of the split). */
    assert(&IOLINK_STAT_ISR(&ctx, framing_errors) != &ctx.framing_errors);
    IOLINK_STAT_ISR(&ctx, framing_errors)++;
    assert(ctx.framing_errors == 3U);
    assert(ctx.isr_stat.framing_errors == 5U);
#endif

    iolink_dll_get_stats(&ctx, &stats);
    assert(stats.framing_errors == IOLINK_STAT_TOTAL(&ctx, framing_errors));
    assert(stats.timeout_errors == IOLINK_STAT_TOTAL(&ctx, timeout_errors));
    assert(stats.isr_tx_dropped == IOLINK_STAT_TOTAL(&ctx, isr_tx_dropped));

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
    assert(stats.framing_errors == 8U);  /* 3 main + 5 ISR */
    assert(stats.timeout_errors == 11U); /* 5 main + 6 ISR */
    assert(stats.isr_tx_dropped == 5U);  /* 2 main + 3 ISR */
#else
    assert(stats.framing_errors == 3U);
    assert(stats.timeout_errors == 5U);
    /* One set: the ISR increments the main-context field itself. */
    assert(stats.isr_tx_dropped == ctx.isr_tx_dropped);
    assert(stats.isr_tx_dropped == 2U);
#endif

    return 0;
}
