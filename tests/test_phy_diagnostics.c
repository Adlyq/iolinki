/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_phy_diagnostics.c
 * @brief PHY 诊断（电压监测与短路检测）的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>

#include "iolinki/dll.h"
#include "iolinki/phy.h"
#include "iolinki/phy_generic.h"
#include "iolinki/events.h"

/* 可配置电压与故障返回值的 mock PHY 实现 */
static int mock_voltage_mv = 24000; /* 默认值：24V（正常） */
static bool mock_short_circuit = false;

static int mock_init(void* user)
{
    (void) user;
    return 0;
}

static void mock_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void mock_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static int mock_get_voltage_mv(void* user)
{
    (void) user;
    return mock_voltage_mv;
}

static bool mock_is_short_circuit(void* user)
{
    (void) user;
    return mock_short_circuit;
}

static const iolink_phy_api_t mock_phy = {.init = mock_init,
                                          .set_mode = mock_set_mode,
                                          .set_baudrate = mock_set_baudrate,
                                          .detect_wakeup = NULL,
                                          .set_cq_line = NULL,
                                          .get_voltage_mv = mock_get_voltage_mv,
                                          .is_short_circuit = mock_is_short_circuit};

/* 不支持诊断的 PHY */
static const iolink_phy_api_t mock_phy_no_diag = {.init = mock_init,
                                                  .set_mode = mock_set_mode,
                                                  .set_baudrate = mock_set_baudrate,
                                                  .detect_wakeup = NULL,
                                                  .set_cq_line = NULL,
                                                  .get_voltage_mv = NULL,
                                                  .is_short_circuit = NULL};

/* 上报故障位掩码（首选通道）以及旧式一对接口的 PHY，
 * 以便观察优先级规则。 */
static uint8_t mock_faults;
static int mock_legacy_voltage_calls;
static int mock_legacy_short_calls;

static uint8_t mock_get_faults(void* user)
{
    (void) user;
    return mock_faults;
}

static int mock_get_voltage_counted(void* user)
{
    (void) user;
    mock_legacy_voltage_calls++;
    return mock_voltage_mv;
}

static bool mock_is_short_counted(void* user)
{
    (void) user;
    mock_legacy_short_calls++;
    return mock_short_circuit;
}

static const iolink_phy_api_t mock_phy_faults = {
    .init = mock_init,
    .set_mode = mock_set_mode,
    .set_baudrate = mock_set_baudrate,
    .detect_wakeup = NULL,
    .set_cq_line = NULL,
    .get_faults = mock_get_faults,
    .get_voltage_mv = mock_get_voltage_counted,
    .is_short_circuit = mock_is_short_counted,
};

static void reset_fault_mock(void)
{
    mock_faults = 0U;
    mock_voltage_mv = 24000;
    mock_short_circuit = false;
    mock_legacy_voltage_calls = 0;
    mock_legacy_short_calls = 0;
}

static void test_voltage_monitoring_normal(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* 24V - 正常 */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 不应检测到电压故障 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_voltage_monitoring_low(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 12000; /* 12V - 过低 */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 应检测到电压故障 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* 应使用 Table D.1 的设备代码
       "Primary supply voltage underrun"（0x5111）触发事件。 */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x5111U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

static void test_voltage_monitoring_high(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 35000; /* 35V - 过高 */

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 应检测到电压故障 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* 应使用 Table D.1 的设备代码
       "Primary supply voltage overrun"（0x5110）触发事件。 */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x5110U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

static void test_voltage_monitoring_boundaries(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;

    /* 18 V 与 30 V 为包含在内的限值：区间之内即正常。 */
    mock_voltage_mv = 18000;
    mock_short_circuit = false;
    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));

    mock_voltage_mv = 30000;
    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_voltage_measurement_unavailable(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = -1; /* 负值：测量不可用，并非故障 */
    mock_short_circuit = false;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 不可用的测量不得产生故障或事件。 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_generic_phy_negative_voltage_is_not_a_fault(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* 模板 PHY 在每次轮询时都报告 -1（不可用）；这不得
       在每个周期都产生电压事件。 */
    const iolink_phy_api_t* generic = iolink_phy_generic_get();
    assert_non_null(generic->get_voltage_mv);
    assert_int_equal(generic->get_voltage_mv(NULL), -1);

    iolink_dll_init(&ctx, generic);
    iolink_dll_process(&ctx);

    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_voltage_monitoring_multiple_cycles(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 10000; /* 10V - 过低 */

    iolink_dll_init(&ctx, &mock_phy);

    /* 运行多个处理周期 */
    for (int i = 0; i < 5; i++) {
        iolink_dll_process(&ctx);
    }

    /* 电压故障应累加 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 5);
}

static void test_short_circuit_detection(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* 确保电压正常 */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 应检测到短路 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.short_circuits, 1);

    /* 应使用 Table D.1 的设备代码
       "Short circuit"（0x7710）触发事件。 */
    assert_true(iolink_events_pending(&ctx.events));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x7710U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
}

static void test_short_circuit_no_fault(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 24000; /* 确保电压正常 */
    mock_short_circuit = false;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 不应检测到短路 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.short_circuits, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static void test_phy_no_diagnostics_support(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* 使用不支持诊断的 PHY */
    iolink_dll_init(&ctx, &mock_phy_no_diag);
    iolink_dll_process(&ctx);

    /* 不应崩溃，计数器应保持为零 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 0);
    assert_int_equal(stats.short_circuits, 0);
}

static void test_combined_faults(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    mock_voltage_mv = 10000; /* 过低 */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy);
    iolink_dll_process(&ctx);

    /* 两种故障都应被检测到 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_int_equal(stats.short_circuits, 1);

    /* 两个事件都应被触发 */
    assert_true(iolink_events_pending(&ctx.events));
}

/* 故障位掩码只上报边沿：每次 0->1 跳变计数/上报一次，
 * 故障保持置起期间不重复，下降沿会清除事件。 */
static void test_get_faults_edges_only(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, 0x5111U);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.events.pending_count, 1U);

    /* 持续故障：不重复计数。 */
    iolink_dll_process(&ctx);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    /* 下降沿：挂起的条件被清除（disappears 被推迟到
     * 事件引擎，它需要 50 ms 窗口）。 */
    mock_faults = 0U;
    iolink_dll_process(&ctx);
    assert_int_equal(ctx.events.pending_count, 0U);

    /* 新的上升沿会再次计数。 */
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 2);
}

static void test_get_faults_overtemp_and_short(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = (uint8_t) (IOLINK_PHY_FAULT_OVERTEMP | IOLINK_PHY_FAULT_SHORT_CIRCUIT);

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.overtemp_faults, 1);
    assert_int_equal(stats.short_circuits, 1);
    assert_int_equal(stats.voltage_faults, 0);

    /* 两个错误都入队；同一严重级别内的顺序不作断言。 */
    const uint16_t expected[2] = {IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD,
                                  IOLINK_EVENTCODE_SHORT_CIRCUIT};
    bool seen[2] = {false, false};
    for (int i = 0; i < 2; i++) {
        iolink_event_t ev;
        assert_true(iolink_events_pop(&ctx.events, &ev));
        assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
        for (int k = 0; k < 2; k++) {
            if (ev.code == expected[k]) {
                seen[k] = true;
            }
        }
    }
    assert_true(seen[0]);
    assert_true(seen[1]);
    assert_false(iolink_events_pending(&ctx.events));

    /* 部分下降：只有过温条件被清除。检查每个代码的
     * present 标志，使清除错误事件无法通过。 */
    mock_faults = IOLINK_PHY_FAULT_SHORT_CIRCUIT;
    iolink_dll_process(&ctx);
    assert_int_equal(ctx.events.pending_count, 1U);
    bool overtemp_present = false;
    bool short_present = false;
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        if (!ctx.events.pending[i].active) {
            continue;
        }
        if (ctx.events.pending[i].code == IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD) {
            overtemp_present = ctx.events.pending[i].present;
        }
        if (ctx.events.pending[i].code == IOLINK_EVENTCODE_SHORT_CIRCUIT) {
            short_present = ctx.events.pending[i].present;
        }
    }
    assert_false(overtemp_present);
    assert_true(short_present);

    /* 完全下降会清除剩余条件。 */
    mock_faults = 0U;
    iolink_dll_process(&ctx);
    assert_int_equal(ctx.events.pending_count, 0U);
}

static void test_get_faults_takes_priority_over_legacy(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_voltage_mv = 10000; /* 会通过旧式钩子产生故障 */
    mock_short_circuit = true;

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);

    /* get_faults() == 0 优先：不得查询旧式钩子。 */
    assert_int_equal(stats.voltage_faults, 0);
    assert_int_equal(stats.short_circuits, 0);
    assert_int_equal(mock_legacy_voltage_calls, 0);
    assert_int_equal(mock_legacy_short_calls, 0);
    assert_false(iolink_events_pending(&ctx.events));
}

static uint32_t g_fault_ms;

static uint64_t fault_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_fault_ms * 1000U;
}

/* 清除 -> disappears 端到端：下降沿必须清除映射的代码，
 * 且事件引擎必须在 50 ms 窗口结束之后发出其消失。
 * 映射错误会清除另一个代码，并使本代码
 * 永远保持挂起。 */
static void test_get_faults_clear_emits_disappears(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_event_t ev;
    reset_fault_mock();
    g_fault_ms = 0U;
    mock_faults = IOLINK_PHY_FAULT_OVERTEMP;

    iolink_dll_init(&ctx, &mock_phy_faults);
    ctx.time_us = fault_time_us;
    ctx.events.time_us = fault_time_us;
    iolink_dll_process(&ctx);

    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* 下降沿会立即上报，但消失必须遵守
     * 50 ms 最小窗口：10 ms 时不会入队任何内容。 */
    g_fault_ms = 10U;
    mock_faults = 0U;
    iolink_dll_process(&ctx);
    iolink_events_process(&ctx.events);
    assert_false(iolink_events_pending(&ctx.events));

    g_fault_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx.events);

    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

static void test_get_faults_overvoltage(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = IOLINK_PHY_FAULT_OVERVOLTAGE;

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx.events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);

    /* 下降沿清除过压条件。 */
    assert_int_equal(ctx.events.pending_count, 1U);
    mock_faults = 0U;
    iolink_dll_process(&ctx);
    assert_int_equal(ctx.events.pending_count, 0U);
}

/* 诊断复位会丢弃条件跟踪；仍然存在的故障
 * 必须在新会话中再次上报（否则主站会看到
 * 已清除的 DeviceStatus，而硬件仍处于故障状态）。 */
static void test_get_faults_rearmed_after_reset(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;
    iolink_dll_stats_t stats;
    reset_fault_mock();
    mock_faults = IOLINK_PHY_FAULT_UNDERVOLTAGE;

    iolink_dll_init(&ctx, &mock_phy_faults);
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 1);
    assert_int_equal(ctx.events.pending_count, 1U);

    iolink_dll_reset_diagnostics(&ctx);
    assert_int_equal(ctx.events.pending_count, 0U);

    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 2);
    assert_int_equal(ctx.events.pending_count, 1U);

    /* STARTUP 路径将同一请求暂存为标志（dll_set_state 可能在
     * ISR 上下文中运行）；用仍显示该故障的锁存来消费它。 */
    ctx.phy_faults_last = IOLINK_PHY_FAULT_UNDERVOLTAGE;
    ctx.phy_faults_rearm = 1U;
    iolink_dll_process(&ctx);
    iolink_dll_get_stats(&ctx, &stats);
    assert_int_equal(stats.voltage_faults, 3);
    assert_int_equal(ctx.phy_faults_last, IOLINK_PHY_FAULT_UNDERVOLTAGE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_voltage_monitoring_normal),
        cmocka_unit_test(test_voltage_monitoring_low),
        cmocka_unit_test(test_voltage_monitoring_high),
        cmocka_unit_test(test_voltage_monitoring_boundaries),
        cmocka_unit_test(test_voltage_measurement_unavailable),
        cmocka_unit_test(test_generic_phy_negative_voltage_is_not_a_fault),
        cmocka_unit_test(test_voltage_monitoring_multiple_cycles),
        cmocka_unit_test(test_short_circuit_detection),
        cmocka_unit_test(test_short_circuit_no_fault),
        cmocka_unit_test(test_phy_no_diagnostics_support),
        cmocka_unit_test(test_combined_faults),
        cmocka_unit_test(test_get_faults_edges_only),
        cmocka_unit_test(test_get_faults_overtemp_and_short),
        cmocka_unit_test(test_get_faults_takes_priority_over_legacy),
        cmocka_unit_test(test_get_faults_clear_emits_disappears),
        cmocka_unit_test(test_get_faults_overvoltage),
        cmocka_unit_test(test_get_faults_rearmed_after_reset),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
