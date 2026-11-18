/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_timer_table.c
 * @brief 截止时间表：契约完整性、装载/取消、回绕单点、同刻优先级。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "dll_access.h"
#include "crc_oracle.h"

static uint64_t g_now;

static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return g_now;
}

static void init_ctx(iolink_dll_ctx_t* ctx)
{
    static const iolink_phy_api_t phy = {0};
    iolink_dll_init(&ctx->main, &ctx->isr, &phy);
    ctx->main.shared.c_clock.time_us = test_time_us;
    ctx->main.enforce_timing = true;
    g_now = 1000000U;
}

/* 契约表必须覆盖全部 timer，且索引即同刻优先级。 */
static void test_contract_table(void** state)
{
    (void) state;
    uint8_t count = 0U;
    const iolink_timer_contract_t* contract = iolink_dll_timer_contract(&count);
    assert_non_null(contract);
    assert_int_equal(count, (uint8_t) IOLINK_TIMER_COUNT);
    for (uint8_t i = 0U; i < count; i++) {
        assert_int_equal(contract[i].id, (iolink_timer_id_t) i);
        assert_non_null(contract[i].name);
        assert_non_null(contract[i].clause);
        assert_true(contract[i].name[0] != '\0');
        assert_true(contract[i].clause[0] != '\0');
        assert_true((contract[i].kind == IOLINK_TIMER_KIND_DEADLINE) ||
                    (contract[i].kind == IOLINK_TIMER_KIND_WINDOW));
        assert_true(contract[i].phase <= 1U);
    }
    /* 边沿窗口不进 next_deadline；其余为到期分发项。 */
    assert_int_equal(contract[IOLINK_TIMER_T_REN].kind, IOLINK_TIMER_KIND_WINDOW);
    assert_int_equal(contract[IOLINK_TIMER_T_CYCLE].kind, IOLINK_TIMER_KIND_WINDOW);
    assert_int_equal(contract[IOLINK_TIMER_T_PD].kind, IOLINK_TIMER_KIND_WINDOW);
    assert_int_equal(contract[IOLINK_TIMER_T_WU].kind, IOLINK_TIMER_KIND_WINDOW);
    assert_int_equal(contract[IOLINK_TIMER_T_A].kind, IOLINK_TIMER_KIND_WINDOW);
    assert_int_equal(contract[IOLINK_TIMER_T_DSIO].kind, IOLINK_TIMER_KIND_DEADLINE);
    /* 分发阶段划分与 dll_process_body 的门控顺序一致。 */
    assert_int_equal(contract[IOLINK_TIMER_MASTER_LOST].phase, 0U);
    assert_int_equal(contract[IOLINK_TIMER_T_FBD].phase, 0U);
    assert_int_equal(contract[IOLINK_TIMER_T_BYTE].phase, 1U);
}

/* next_deadline 始终是最早的 DEADLINE 项；WINDOW 项不影响它。 */
static void test_next_deadline_is_minimum(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);

    assert_int_equal(ctx.main.next_deadline, 0U);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_IDLE, 5000U);
    assert_int_equal(ctx.main.next_deadline, 5000U);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_DSIO, 3000U);
    assert_int_equal(ctx.main.next_deadline, 3000U);
    /* 将表头推后需要重算。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_DSIO, 7000U);
    assert_int_equal(ctx.main.next_deadline, 5000U);
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_IDLE);
    assert_int_equal(ctx.main.next_deadline, 7000U);
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_T_DSIO);
    assert_int_equal(ctx.main.next_deadline, 0U);

    /* WINDOW 类（t_REN / T_CYCLE）只由边沿检查，不参与主循环轮询。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_REN, 1U);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_CYCLE, 2U);
    assert_int_equal(ctx.main.next_deadline, 0U);
    assert_true(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_REN));
    assert_true(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_CYCLE));
}

/* 所有跨回绕比较只有一处实现。 */
static void test_rollover_single_point(void** state)
{
    (void) state;
    assert_false(dll_time_reached(100U, 200U));
    assert_true(dll_time_reached(200U, 200U));
    assert_false(dll_time_reached(199U, 200U));
    assert_false(dll_time_passed(200U, 200U));
    assert_true(dll_time_passed(201U, 200U));

    /* 64 位回绕：deadline 已越过 UINT64_MAX。 */
    assert_false(dll_time_reached(0xFFFFFFFFFFFFFF00ULL, 0x100ULL));
    assert_true(dll_time_reached(0x100ULL, 0xFFFFFFFFFFFFFF00ULL));
    assert_true(dll_time_passed(0x101ULL, 0xFFFFFFFFFFFFFF00ULL));
}

/* 最早截止时间必须用回绕安全判定：跨 2^63 的绝对时间不能按裸 < 比较。 */
static void test_next_deadline_wrap_safe(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);

    /* near = 2^64 - 256；small = 0x100 = 256。按回绕关系 small 落在 near 之后
     * （相差 512 us），裸 `<` 会错选 small 作为最早项。 */
    const uint64_t near = 0xFFFFFFFFFFFFFF00ULL;
    const uint64_t small = 0x100ULL;

    dll_timer_arm(&ctx.main, IOLINK_TIMER_IDLE, near);
    assert_int_equal(ctx.main.next_deadline, near);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_DSIO, small);
    assert_int_equal(ctx.main.next_deadline, near);

    /* 与到期判定一致：在 near 时刻表头已到达，而 small 尚未到达。 */
    assert_true(dll_time_reached(near, ctx.main.next_deadline));
    assert_false(dll_time_reached(near, small));

    /* 重算路径（取消表头）同样回绕安全：near + 10 才是新的最早项。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_FBD, (uint64_t) (small + 100U));
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_DSIO, (uint64_t) (near + 10U));
    assert_int_equal(ctx.main.next_deadline, near);
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_IDLE);
    assert_int_equal(ctx.main.next_deadline, (uint64_t) (near + 10U));
    assert_true(dll_time_reached((uint64_t) (near + 20U), ctx.main.next_deadline));
    assert_false(dll_time_reached((uint64_t) (near + 20U), (uint64_t) (small + 100U)));

    /* deadline == 0 仍表示未装载：不参与比较，也不算"最早"。 */
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_T_DSIO);
    dll_timer_cancel(&ctx.main, IOLINK_TIMER_T_FBD);
    assert_int_equal(ctx.main.next_deadline, 0U);
}

/* 同刻到期：按表序执行，且一次主循环内全部服务完。 */
static void test_same_instant_priority(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    g_now = 4000000U;
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.master_lost_cycles = 3U;
    ctx.main.min_cycle_time_us = 1000U;
    ctx.main.shared.m_master_cycle_time = 0U;
    /* MASTER_LOST 截止时间 = last_activity + 3 × cycle = g_now。 */
    ctx.main.master_last_activity_ms = (uint32_t) ((g_now - 3000U) / 1000U);
    ctx.main.shared.last_activity_ms = 1000U;

    /* 三个 DEADLINE 项同刻（g_now）到期，t_PD 处于过去（门控关闭）。 */
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_FBD, g_now);
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_PD, g_now - 1U);

    iolink_dll_process(&ctx.main);

    /* 表序 = 原检查顺序：MASTER_LOST -> IDLE -> T_FBD。 */
    assert_true(ctx.main.master_lost);
    assert_int_equal(ctx.main.idle_timeouts, 1U);
    assert_int_equal(ctx.main.shared.state, IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(ctx.main.phy_mode, IOLINK_PHY_MODE_SIO);
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_MASTER_LOST));
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_IDLE));
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_FBD));
    /* 门控窗口不参与轮询：t_PD 已过期（关闭），但仍保留其绝对时间。 */
    assert_int_equal(ctx.main.next_deadline, 0U);
}

/* t_byte 是边沿可重装项：每个字节推进截止时间，帧结束取消。 */
static void test_t_byte_rearm_and_cancel(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.frame_index = 1U;
    ctx.main.t_byte_limit_us = 1000U;
    ctx.main.shared.last_byte_us = (uint32_t) g_now;

    iolink_dll_process(&ctx.main);
    const uint64_t deadline = dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_BYTE);
    assert_int_equal(deadline, g_now + 1001U); /* 严格 > limit */

    /* 新字节：截止时间随 last_byte 推进（同一 limit）。 */
    g_now += 400U;
    ctx.main.shared.last_byte_us = (uint32_t) g_now;
    iolink_dll_process(&ctx.main);
    const uint64_t advanced = dll_timer_deadline(&ctx.main, IOLINK_TIMER_T_BYTE);
    assert_int_equal(advanced, g_now + 1001U);
    assert_true(advanced > deadline);

    g_now = advanced - 1U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.t_byte_violations, 0U);
    assert_true(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));

    g_now = advanced;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.t_byte_violations, 1U);
    assert_int_equal(ctx.main.frame_index, 0U);
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));

    /* 帧结束后重新开始组装：再次装载。 */
    ctx.main.frame_index = 1U;
    ctx.main.shared.last_byte_us = (uint32_t) g_now;
    iolink_dll_process(&ctx.main);
    assert_true(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));

    /* 帧结束（frame_index = 0）取消。 */
    ctx.main.frame_index = 0U;
    iolink_dll_process(&ctx.main);
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_T_BYTE));
}

/* t_PD 保护窗口：窗口内到达的帧被丢弃并计入 t_pd_violations，
 * 窗口结束后同一帧被正常处理。 */
static void test_t_pd_window_drops_frames(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    dll_timer_arm(&ctx.main, IOLINK_TIMER_T_PD, g_now + 500U);

    iolink_frame_rec_t* rec =
        (iolink_frame_rec_t*) iolink_ring_claim(&ctx.main.shared.i_isr_frame_ring);
    assert_non_null(rec);
    rec->len = 3U;
    rec->eager = 0U;
    rec->data[0] = 0x40U;
    rec->data[1] = 0x40U;
    rec->data[2] = 0x00U;
    iolink_ring_publish(&ctx.main.shared.i_isr_frame_ring);

    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.t_pd_violations, 1U);
    assert_true(iolink_ring_is_empty(&ctx.main.shared.i_isr_frame_ring));

    /* 窗口结束：送入一条校验和正确的 Type-0 页写（地址 0，命令
     * PDOperate 0x98）。它必须被真正处理：状态保持 OPERATE、PD 标记为
     * 有效并发出应答，而不只是"违规计数不变"。 */
    g_now += 500U;
    uint8_t frame[3] = {IOLINK_MC_CHANNEL_PAGE, 0x00U, IOLINK_CMD_PROCESS_DATA_OPERATE};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | test_expected_checksum6(frame, sizeof(frame)));
    rec = (iolink_frame_rec_t*) iolink_ring_claim(&ctx.main.shared.i_isr_frame_ring);
    assert_non_null(rec);
    rec->len = (uint8_t) sizeof(frame);
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, sizeof(frame));
    iolink_ring_publish(&ctx.main.shared.i_isr_frame_ring);

    test_pd_set_valid(&ctx.main, false);
    const uint32_t framing_before = ctx.main.shared.framing_errors;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.t_pd_violations, 1U);
    assert_int_equal(ctx.main.shared.framing_errors, framing_before);
    assert_int_equal(ctx.main.shared.state, IOLINK_DLL_STATE_OPERATE);
    assert_true(test_pd_valid(&ctx.main));
    assert_int_equal(ctx.main.last_response_us, g_now);
}

/* 超过 2^32 ms（约 49.7 天）后，32 位毫秒时间戳回绕不得误触发。 */
static void test_millisecond_wrap(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.min_cycle_time_us = 1000U;
    ctx.main.shared.m_master_cycle_time = 0U;
    ctx.main.master_lost_cycles = 2U;

    /* 现在 = 2^32 ms + 5 s；截断后的毫秒值是 5000。 */
    g_now = ((uint64_t) 0x100000000ULL + 5000ULL) * 1000ULL;
    ctx.main.shared.last_activity_ms = (uint32_t) (g_now / 1000U) - 200U;
    ctx.main.master_last_activity_ms = (uint32_t) (g_now / 1000U);
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.idle_timeouts, 0U);
    assert_false(ctx.main.master_lost);

    /* 1.2 s 静默：空闲诊断触发一次；主站丢失（2 ms 窗口）提示。 */
    g_now += 1000000U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.idle_timeouts, 1U);
    assert_true(ctx.main.master_lost);
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_MASTER_LOST));
}

/* 1 s 空闲诊断的 1 ms 边界：elapsed == 1000 差 1 ms，1001 立即到期。 */
static void test_idle_deadline_boundary(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    init_ctx(&ctx);
    g_now = 4000000U;
    ctx.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.master_lost_cycles = 0U;

    ctx.main.shared.last_activity_ms = (uint32_t) (g_now / 1000U) - 1000U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(dll_timer_deadline(&ctx.main, IOLINK_TIMER_IDLE), g_now + 1000U);
    assert_true(dll_timer_armed(&ctx.main, IOLINK_TIMER_IDLE));
    assert_int_equal(ctx.main.idle_timeouts, 0U);

    g_now += 1000U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.idle_timeouts, 1U);
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_IDLE));

    /* 更长的静默（2 s）同样必须判为过期；把截止时间算成"未来"的实现
     * 会在这里漏掉。 */
    ctx.main.shared.last_activity_ms = (uint32_t) (g_now / 1000U) - 2000U;
    iolink_dll_process(&ctx.main);
    assert_int_equal(ctx.main.idle_timeouts, 2U);
    assert_false(dll_timer_armed(&ctx.main, IOLINK_TIMER_IDLE));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_contract_table),
        cmocka_unit_test(test_next_deadline_is_minimum),
        cmocka_unit_test(test_rollover_single_point),
        cmocka_unit_test(test_next_deadline_wrap_safe),
        cmocka_unit_test(test_same_instant_priority),
        cmocka_unit_test(test_t_byte_rearm_and_cancel),
        cmocka_unit_test(test_t_pd_window_drops_frames),
        cmocka_unit_test(test_millisecond_wrap),
        cmocka_unit_test(test_idle_deadline_boundary),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
