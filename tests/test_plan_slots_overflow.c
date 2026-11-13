/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_plan_slots_overflow.c
 * @brief 计划槽位耗尽必须可观测，而不是静默截断。
 *
 * 本目标把 `IOLINK_REPLY_PLAN_SLOTS` 覆盖为 1（见 tests/CMakeLists.txt），
 * 因此 OPERATE/Type-2 的计划集合（8 条）必然溢出：第一条进入计划表，
 * 其余计入 `reply_plan_overflows`。若发布路径重新退化为静默返回，
 * 计数保持不变，测试失败。
 */

#include <assert.h>
#include <string.h>

#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"

static const iolink_phy_api_t s_phy = {.user = NULL};

int main(void)
{
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    iolink_dll_init(&ctx.main, &ctx.isr, &s_phy);

    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    iolink_dll_publish_reply_plans(&ctx.main);

    /* 计划表只容得下一条；其余 8 条（Type-2 的 8 条 + OPERATE 中的
     * Type-0 复位计划）必须被计数。 */
    assert(ctx.main.shared.reply_plan_count == 1U);
    assert(ctx.main.reply_plan_overflows == 8U);

    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&ctx.main, &stats);
    assert(stats.reply_plan_overflows == 8U);
    return 0;
}
