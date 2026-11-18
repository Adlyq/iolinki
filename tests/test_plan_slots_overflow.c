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

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "dll_access.h"
#include "iolinki/iolink.h"

static const iolink_phy_api_t s_phy = {.self = NULL};

static void test_plan_slot_overflow_is_counted(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    iolink_dll_init(&ctx.main, &ctx.isr, &s_phy);

    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.shared.c_m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    ctx.main.shared.c_od_width = 2U;
    iolink_dll_publish_reply_plans(&ctx.main);

    /* 计划表只容得下一条；按状态切片构建的其余计划必须被计数。 */
    assert_int_equal(ctx.main.shared.c_reply_plan_cnt[IOLINK_PLAN_SLOT_STARTUP], 1U);
    assert_int_equal(ctx.main.shared.c_reply_plan_cnt[IOLINK_PLAN_SLOT_PREOPERATE], 0U);
    assert_int_equal(ctx.main.shared.c_reply_plan_cnt[IOLINK_PLAN_SLOT_OPERATE], 0U);
    /* Type-1/2 配置共 42 条切片行（STARTUP 11 + PREOPERATE 11 + ESTAB 10 +
     * OPERATE 10），只容下 1 条，其余 41 条必须逐条计数。计数描述当前这张
     * 表，因此一次发布的结果是绝对值而不是累计值。 */
    assert_int_equal(ctx.main.reply_plan_overflows, 41U);
    assert_int_equal(ctx.main.shared.c_reply_plan_cnt[IOLINK_PLAN_SLOT_ESTAB_COM], 0U);

    iolink_dll_stats_t stats;
    (void) memset(&stats, 0, sizeof(stats));
    iolink_dll_get_stats(&ctx.main, &stats);
    assert_int_equal(stats.reply_plan_overflows, ctx.main.reply_plan_overflows);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_plan_slot_overflow_is_counted),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
