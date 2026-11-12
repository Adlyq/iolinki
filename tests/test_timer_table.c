/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_timer_table.c
 * @brief P7 截止时间表：契约完整性、装载/取消、回绕单点、同刻优先级。
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/dll_ctx.h"

static uint64_t g_now;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return g_now;
}

static void init_ctx(iolink_dll_ctx_t* ctx)
{
    static const iolink_phy_api_t phy = {0};
    iolink_dll_init(&ctx->main, &ctx->isr, &phy);
    ctx->main.shared.time_us = test_time_us;
    ctx->main.enforce_timing = true;
    g_now = 1000000U;
}

/* 契约表必须覆盖全部 timer，且索引即同刻优先级。 */
static void test_contract_table(void)
{
    uint8_t count = 0U;
    const iolink_timer_contract_t* contract = iolink_dll_timer_contract(&count);
    assert(contract != NULL);
    assert(count == (uint8_t) IOLINK_TIMER_COUNT);
    for (uint8_t i = 0U; i < count; i++) {
        assert(contract[i].id == (iolink_timer_id_t) i);
        assert(contract[i].name != NULL);
        assert(contract[i].clause != NULL);
        assert(contract[i].name[0] != '\0');
        assert(contract[i].clause[0] != '\0');
        assert((contract[i].kind == IOLINK_TIMER_KIND_DEADLINE) ||
               (contract[i].kind == IOLINK_TIMER_KIND_WINDOW));
        assert(contract[i].phase <= 1U);
    }
    /* 边沿窗口不进 next_deadline；其余为到期分发项。 */
    assert(contract[IOLINK_TIMER_T_REN].kind == IOLINK_TIMER_KIND_WINDOW);
    assert(contract[IOLINK_TIMER_T_CYCLE].kind == IOLINK_TIMER_KIND_WINDOW);
    assert(contract[IOLINK_TIMER_T_PD].kind == IOLINK_TIMER_KIND_WINDOW);
    assert(contract[IOLINK_TIMER_T_WU].kind == IOLINK_TIMER_KIND_WINDOW);
    assert(contract[IOLINK_TIMER_T_A].kind == IOLINK_TIMER_KIND_WINDOW);
    assert(contract[IOLINK_TIMER_T_DSIO].kind == IOLINK_TIMER_KIND_DEADLINE);
    /* 分发阶段划分与 dll_process_body 的门控顺序一致。 */
    assert(contract[IOLINK_TIMER_MASTER_LOST].phase == 0U);
    assert(contract[IOLINK_TIMER_T_FBD].phase == 0U);
    assert(contract[IOLINK_TIMER_T_BYTE].phase == 1U);
}

/* next_deadline 始终是最早的 DEADLINE 项；WINDOW 项不影响它。 */
static void test_next_deadline_is_minimum(void)
{
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);

    assert(ctx.main.next_deadline == 0U);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_IDLE, 5000U);
    assert(ctx.main.next_deadline == 5000U);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_DSIO, 3000U);
    assert(ctx.main.next_deadline == 3000U);
    /* 将表头推后需要重算。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_DSIO, 7000U);
    assert(ctx.main.next_deadline == 5000U);
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_IDLE);
    assert(ctx.main.next_deadline == 7000U);
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_T_DSIO);
    assert(ctx.main.next_deadline == 0U);

    /* WINDOW 类（t_REN / T_CYCLE）只由边沿检查，不参与主循环轮询。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_REN, 1U);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_CYCLE, 2U);
    assert(ctx.main.next_deadline == 0U);
    assert(iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_REN));
    assert(iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_CYCLE));
}

/* 所有跨回绕比较只有一处实现。 */
static void test_rollover_single_point(void)
{
    assert(!dll_time_reached(100U, 200U));
    assert(dll_time_reached(200U, 200U));
    assert(!dll_time_reached(199U, 200U));
    assert(!dll_time_passed(200U, 200U));
    assert(dll_time_passed(201U, 200U));

    /* 64 位回绕：deadline 已越过 UINT64_MAX。 */
    assert(!dll_time_reached(0xFFFFFFFFFFFFFF00ULL, 0x100ULL));
    assert(dll_time_reached(0x100ULL, 0xFFFFFFFFFFFFFF00ULL));
    assert(dll_time_passed(0x101ULL, 0xFFFFFFFFFFFFFF00ULL));
}

/* 同刻到期：按表序执行，且一次主循环内全部服务完。 */
static void test_same_instant_priority(void)
{
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    g_now = 4000000U;
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.master_lost_cycles = 3U;
    ctx.main.min_cycle_time_us = 1000U;
    ctx.main.master_cycle_time = 0U;
    ctx.main.master_last_activity_ms = 0U;
    ctx.main.shared.last_activity_ms = 1000U;

    /* 三个 DEADLINE 项同刻（g_now）到期，t_PD 处于过去（门控关闭）。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_FBD, g_now);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_PD, g_now - 1U);

    iolink_dll_process(&ctx.main);

    /* 表序 = 原检查顺序：MASTER_LOST -> IDLE -> T_FBD。 */
    assert(ctx.main.master_lost);
    assert(ctx.main.idle_timeouts == 1U);
    assert(ctx.main.shared.state == IOLINK_DLL_STATE_STARTUP);
    assert(ctx.main.phy_mode == IOLINK_PHY_MODE_SIO);
    assert(!iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_MASTER_LOST));
    assert(!iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_IDLE));
    assert(!iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_FBD));
    /* 门控窗口不参与轮询：t_PD 已过期（关闭），但仍保留其绝对时间。 */
    assert(ctx.main.next_deadline == 0U);
}

/* t_byte 是边沿可重装项：每个字节推进截止时间，帧结束取消。 */
static void test_t_byte_rearm_and_cancel(void)
{
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.frame_index = 1U;
    ctx.main.t_byte_limit_us = 1000U;
    ctx.main.shared.last_byte_us = (uint32_t) g_now;

    iolink_dll_process(&ctx.main);
    const uint64_t deadline = iolink_dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_BYTE);
    assert(deadline == g_now + 1001U); /* 严格 > limit */

    /* 新字节：绝对截止时间不变（同一个 last_byte 时间戳）。 */
    g_now += 400U;
    iolink_dll_process(&ctx.main);
    assert(iolink_dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_BYTE) == deadline);

    g_now = deadline - 1U;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.t_byte_violations == 0U);
    assert(iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));

    g_now = deadline;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.t_byte_violations == 1U);
    assert(ctx.main.frame_index == 0U);
    assert(!iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));

    /* 帧结束后重新开始组装：再次装载。 */
    ctx.main.frame_index = 1U;
    ctx.main.shared.last_byte_us = (uint32_t) g_now;
    iolink_dll_process(&ctx.main);
    assert(iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));

    /* 帧结束（frame_index = 0）取消。 */
    ctx.main.frame_index = 0U;
    iolink_dll_process(&ctx.main);
    assert(!iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));
}

/* t_PD 兼容入口：device.c 写入的字段在首个 process 导入表项。 */
static void test_t_pd_legacy_import(void)
{
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.t_pd_deadline_us = g_now + 500U;

    iolink_dll_process(&ctx.main);
    assert(ctx.main.t_pd_deadline_us == 0U);
    assert(iolink_dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_PD) == g_now + 500U);
    assert(ctx.main.timing_errors == 0U);

    g_now += 500U;
    iolink_dll_process(&ctx.main);
    assert(dll_time_reached(g_now, iolink_dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_PD)));
}

/* 超过 2^32 ms（约 49.7 天）后，32 位毫秒时间戳回绕不得误触发。 */
static void test_millisecond_wrap(void)
{
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.min_cycle_time_us = 1000U;
    ctx.main.master_cycle_time = 0U;
    ctx.main.master_lost_cycles = 2U;

    /* 现在 = 2^32 ms + 5 s；截断后的毫秒值是 5000。 */
    g_now = ((uint64_t) 0x100000000ULL + 5000ULL) * 1000ULL;
    ctx.main.shared.last_activity_ms = (uint32_t) (g_now / 1000U) - 200U;
    ctx.main.master_last_activity_ms = (uint32_t) (g_now / 1000U) - 1U;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.idle_timeouts == 0U);
    assert(!ctx.main.master_lost);

    /* 1.2 s 静默：空闲诊断触发一次；主站丢失（2 ms 窗口）保持提示。 */
    g_now += 1000000U;
    iolink_dll_process(&ctx.main);
    assert(ctx.main.idle_timeouts == 1U);
    assert(ctx.main.master_lost);
    assert(!iolink_dll_timer_armed(&ctx.main, IOLINK_TIMER_MASTER_LOST));
}

int main(void)
{
    test_contract_table();
    test_next_deadline_is_minimum();
    test_rollover_single_point();
    test_same_instant_priority();
    test_t_byte_rearm_and_cancel();
    test_t_pd_legacy_import();
    test_millisecond_wrap();
    return 0;
}
