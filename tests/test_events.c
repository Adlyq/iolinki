/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_events.c
 * @brief IO-Link 事件处理的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/events.h"
#include "iolinki/internal/wiring.h"

#include "events_internal.h"

#include "conformance_test.h"
#include "test_helpers.h"

static const iolink_clock_t g_test_clock = {.time_us = iolink_test_time_us};

static void events_init(iolink_events_ctx_t* ctx)
{
    iolink_events_init(ctx);
    ctx->clock = &g_test_clock;
}

static uint32_t g_fake_ms;

static uint64_t fake_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return (uint64_t) g_fake_ms * 1000U;
}

static const iolink_clock_t g_fake_clock = {.time_us = fake_time_us};

static void test_event_queue_flow(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    assert_false(iolink_events_pending(&ctx));

    iolink_event_trigger(&ctx, 0x1234, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending(&ctx));

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1234);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_queue_overflow(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* 用 Notification 填满并溢出队列（IOLINK_EVENT_QUEUE_SIZE 项）。 */
    for (uint16_t i = 0U; i < (uint16_t) (IOLINK_EVENT_QUEUE_SIZE + 1U); i++) {
        iolink_event_trigger(&ctx, i, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
    const uint32_t drops_before = iolink_events_get_dropped_count(&ctx);
    assert_true(drops_before > 0U);

    /* 更严重的事件仍会覆盖最不严重的事件，并被计数。 */
    iolink_event_trigger(&ctx, 0xFFFF, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_get_dropped_count(&ctx) > drops_before);

    iolink_event_t ev;
    iolink_events_pop(&ctx, &ev); /* 跳过第一个 */
    /* 验证可以弹出其余事件 */
    assert_true(iolink_events_pending(&ctx));
}

static void test_standard_codes(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENT_COMM_TIMEOUT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_CRC);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_TIMEOUT);
}

static void test_phy_diagnostic_codes(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    /* A.6.4：Error 优先于 Warning。 */
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
}

static void test_event_peek(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1122, IOLINK_EVENT_TYPE_NOTIFICATION);

    iolink_event_t ev;
    assert_true(iolink_events_peek(&ctx, &ev));
    assert_int_equal(ev.code, 0x1122);
    /* 应仍处于挂起状态 */
    assert_true(iolink_events_pending(&ctx));

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1122);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_helpers(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* 测试最高严重级别 */
    assert_int_equal(iolink_diag_device_status(&ctx), 0); /* OK */

    iolink_event_trigger(&ctx, 0x1001, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_diag_device_status(&ctx), 0); /* 仍为 OK */

    iolink_event_trigger(&ctx, 0x1002, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_diag_device_status(&ctx), 4); /* Failure */

    iolink_event_trigger(&ctx, 0x1003, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_diag_device_status(&ctx), 4); /* 仍为 Failure */

    /* 测试 get_all */
    iolink_event_t events[8];
    uint8_t count = iolink_events_get_all(&ctx, events, 8);
    assert_int_equal(count, 3);
    /* 严重级别顺序：Error(0x1002) > Warning(0x1003) > Notification(0x1001)。 */
    assert_int_equal(events[0].code, 0x1002);
    assert_int_equal(events[1].code, 0x1003);
    assert_int_equal(events[2].code, 0x1001);

    /* 带限制测试 get_all */
    count = iolink_events_get_all(&ctx, events, 2);
    assert_int_equal(count, 2);
}

static void test_event_classify(void** state)
{
    (void) state;
    assert_int_equal(iolink_diag_type_for(IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(
        iolink_diag_type_for(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_ERROR),
        IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(
        iolink_diag_type_for(IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN, IOLINK_EVENT_TYPE_ERROR),
        IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(
        iolink_diag_type_for(IOLINK_EVENTCODE_MAINTENANCE_REFILL, IOLINK_EVENT_TYPE_ERROR),
        IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_diag_type_for(IOLINK_EVENTCODE_HARDWARE_FAULT, IOLINK_EVENT_TYPE_ERROR),
                     IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_diag_type_for(IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR),
                     IOLINK_EVENT_TYPE_ERROR);
    /* 保留/未知代码默认归为 error。 */
    assert_int_equal(iolink_diag_type_for(0x2222U, IOLINK_EVENT_TYPE_ERROR),
                     IOLINK_EVENT_TYPE_ERROR);
}

static void test_event_memory_layout(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    assert_false(iolink_events_flag(&ctx));

    /* 一个错误事件：标志被置起时事件存储器冻结（Table 60 T3）。 */
    iolink_event_trigger(&ctx, 0x1801U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 58 / Figure A.22：StatusCode 类型 2，第 7 位置位，第 0 位激活。 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* 槽位 1 位于地址 1,2,3：限定符 0xE4（appears|warning|device|Application）、
       0x1801（注册表把该厂商代码固定为 Warning，调用方传入的 Error 被规范化）。 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE4U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x18U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x01U);

    /* 冻结期间的第二个事件会入队但不可见。 */
    iolink_event_trigger(&ctx, 0x1803U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0x00U);

    /* 保留地址/越界读取返回 0。 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x13U), 0x00U);
}

static void test_event_memory_frozen(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* 冻结期间的后续事件会入队但不可见。 */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0x00U); /* 槽位 2 未使用 */
    assert_int_equal(ctx.count, 3U);                                 /* 仍在队列中 */
}

static void test_event_memory_confirm(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 60 T5：确认会释放已呈现的槽位；冻结期间入队的事件
       随后会被重新冻结。 */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* 再次确认会清空队列。 */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0x00U);

    /* 写入其他地址会被忽略。 */
    /* Table D.1：0x4000 是 Error；传入的 Warning 会被注册表规范化。 */
    iolink_event_trigger(&ctx, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x01U, 0xAAU);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xF4U); /* Error 限定符 */
}

/* 确认只应答实际呈现过的槽位；冻结期间入队的事件
 * 会保留到下一次读出。 */
static void test_event_memory_confirm_only_presented(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    /* 第一个事件冻结一个单槽位快照。 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* 冻结期间到达的后续事件：入队但尚未呈现。 */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 3U);

    /* 确认：只应答已呈现的槽位；其余两个得以保留。 */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 2U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x83U); /* 2 个槽位 */
}

/* A.6.4：Error/Warning 只出现一次，且仅在 50 ms 最小
 * 出现窗口之后才消失。 */
static void test_event_appears_disappears(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0074");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, 0x1000U);

    /* 窗口之内，消失仍被推迟。 */
    iolink_events_process(&ctx);
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_false(iolink_events_pop(&ctx, &ev));

    test_clock_advance_ms(IOLINK_EVENT_MIN_APPEAR_MS + 10U);
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* 同一 EventCode 在激活期间不得重复出现；清除未知的
 * 代码会被忽略。 */
static void test_event_lifecycle_no_repeat(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.count, 1U);

    iolink_event_clear(&ctx, 0x2000U); /* 未激活：忽略 */
    assert_int_equal(ctx.count, 1U);
}

/* A.6.4 的两个时间窗口是运行时能力。零初始化的上下文回落到构建
 * 默认值；set_window 覆盖后，消失窗口立即按新值生效。 */
static void test_event_window_is_runtime_configurable(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);
    ctx.clock = &g_fake_clock;
    iolink_events_set_window(&ctx, 10U, 20U);
    assert_int_equal(ctx.min_appear_ms, 10U);
    assert_int_equal(ctx.min_repeat_ms, 20U);

    iolink_event_t ev;
    g_fake_ms = 0U;
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_clear(&ctx, 0x1000U);
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* 9 ms < 10 ms 窗口：消失仍被推迟。 */
    g_fake_ms = 9U;
    iolink_events_process(&ctx);
    assert_false(iolink_events_pop(&ctx, &ev));

    /* 10 ms：窗口满足，消失立即入队（构建默认值是 50 ms，说明运行时
     * 值确实生效）。 */
    g_fake_ms = 10U;
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* 未调用 iolink_events_set_window（设备层 memset 初始化的上下文）时，
 * 窗口回落到构建默认值。 */
static void test_event_window_defaults_without_config(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    (void) memset(&ctx, 0, sizeof(ctx));
    ctx.clock = &g_fake_clock;
    assert_int_equal(ctx.min_appear_ms, 0U);
    assert_int_equal(ctx.min_repeat_ms, 0U);

    iolink_event_t ev;
    g_fake_ms = 0U;
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_clear(&ctx, 0x1000U);
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));

    g_fake_ms = (uint32_t) (IOLINK_EVENT_MIN_APPEAR_MS - 1U);
    iolink_events_process(&ctx);
    assert_false(iolink_events_pop(&ctx, &ev));

    g_fake_ms = (uint32_t) IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* 10.10.2：同一 EventCode 每秒最多上报一次。第二次
 * 事件会被推迟而非丢弃，且时间戳为 0 是有效的。 */
static void test_event_recently_sent(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0076");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.clock = &g_fake_clock;
    g_fake_ms = 0U;
    iolink_event_t ev;

    /* 毫秒 0 处的首次出现（旧的哨兵值会丢弃它）。 */
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    g_fake_ms = 50U;
    iolink_event_clear(&ctx, 0x1000U);
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);

    /* 在 1 s 窗口内重新置起：被推迟，不入队也不丢弃。 */
    g_fake_ms = 60U;
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 1U);

    /* 在 999 ms 时仍处于窗口内。 */
    g_fake_ms = 999U;
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 0U);

    /* 距上次出现 1 s 后，被推迟的事件被发出。 */
    g_fake_ms = 1000U;
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
}

/* pending[] 表满时必须使丢失可被观察。 */
static void test_event_pending_full_observable(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.clock = &g_fake_clock;
    g_fake_ms = 0U;

    iolink_event_t ev;
    for (uint16_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_trigger(&ctx, (uint16_t) (0x2000U + i), IOLINK_EVENT_TYPE_ERROR);
        /* 排空（较小的）事件队列，使 pending[] 成为被填满的那一个。 */
        while (iolink_events_pop(&ctx, &ev)) {
        }
    }
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);

    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 1U);

    /* 将某一条件标记为消失并发出其撤销。 */
    iolink_event_clear(&ctx, 0x2000U);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX - 1U);
    g_fake_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx);

    /* 到 1 s 时 disappears 窗口仍然开着：该槽位不得被
       回收（否则会抹掉其间隔历史），因此多余的原因
       以可观察的方式被丢弃。 */
    g_fake_ms = IOLINK_EVENT_MIN_REPEAT_MS;
    iolink_events_process(&ctx);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX - 1U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 2U);

    /* 两个窗口均已结束：槽位被释放并可重用。 */
    g_fake_ms = 2U * IOLINK_EVENT_MIN_REPEAT_MS;
    iolink_events_process(&ctx);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.pending_count, IOLINK_EVENT_PENDING_MAX);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 2U);

    iolink_events_reset(&ctx);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
}

/* A.6.4：通信被中断时，已入队的事件会被丢弃。 */
static void test_events_reset_clears(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    iolink_events_reset(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
}

/* 事件存储器是双映像 + 发布索引：重建期间主上下文只写未发布的那一代，
 * 因此读侧（数据 ISR）永远看到完整的一代，而不是重建到一半的字节。 */
static void test_event_memory_generation_is_complete(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x10U);

    /* 第二个事件在冻结期间入队（未呈现）。 */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);

    /* 模拟重建进行到一半：为下一代替换而写的后备映像里已经是新内容。 */
    const uint8_t scratch = (uint8_t) (1U - ctx.memory_pub);
    ctx.memory[scratch][0x00U] = 0xFFU;
    ctx.memory[scratch][0x02U] = 0xEEU;

    /* 读侧只经发布索引取值：半成品不可见。 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x10U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x00U);

    /* 确认之后，剩下的 0x2000 构成新的一代并整体发布：刚写完的后备映像
     * 成为发布映像。 */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x20U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x00U);
    assert_int_equal(ctx.memory_pub, scratch);
}

/* ISR 只能在 STARTUP 迁移里登记复位请求：在请求被应用之前，事件标志与
 * 事件存储器对读侧表现为“无事件”，队列内容则留给主泵处理。 */
static void test_isr_reset_request_is_masked_until_applied(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    iolink_events_reset_request(&ctx);

    /* 复位前的 CKS 位与 Table 58 一律不可见。 */
    assert_false(iolink_events_flag(&ctx));
    for (uint8_t addr = 0U; addr < (uint8_t) sizeof(ctx.memory[0]); addr++) {
        assert_int_equal(iolink_events_memory_read(&ctx, addr), 0x00U);
    }
    /* ISR 不改写任何其它状态：队列仍等主泵处置。 */
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(ctx.pending_count, 1U);

    /* 挂起期间重复登记等价于一次：两个字节不会被推回到相等。 */
    iolink_events_reset_request(&ctx);
    iolink_events_reset_request(&ctx);
    assert_int_not_equal(ctx.reset_req, ctx.reset_applied);
    assert_false(iolink_events_flag(&ctx));

    /* 主泵应用一次：队列被丢弃，读侧恢复正常。 */
    iolink_events_reset(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 0U);
    assert_int_equal(ctx.reset_applied, ctx.reset_req);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);

    /* 复位之后事件引擎照常工作。 */
    iolink_event_trigger(&ctx, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
}

/* 事件引擎只有一个主上下文写者：DLL 绑定 ISR 深度标志之后，误从数据
 * ISR 调用的写入口被拒绝，而不是与主泵竞争。 */
static void test_isr_writer_is_rejected(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    volatile uint8_t i_in_isr = 1U;
    ctx.isr_active = &i_in_isr;

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 0U);
    assert_false(iolink_events_flag(&ctx));

    /* 复位请求是该上下文里唯一允许的 ISR 写。 */
    iolink_events_reset_request(&ctx);

    i_in_isr = 0U;
    iolink_events_reset(&ctx);
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 1U);
}

static void test_pop_to_empty_clears_the_published_image(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* 一个已发布（冻结）的 Error + 一个排队等待的事件。 */
    iolink_event_trigger(&ctx, 0x1801U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x1803U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* 排空队列时，每次 pop 之后 flag 都必须与队列一致；最后一个 pop 还必须
     * 撤下已发布的映像，否则 ISR 仍会读到已确认的事件。 */
    iolink_event_t ev;
    uint8_t popped = 0U;
    while (iolink_events_pop(&ctx, &ev)) {
        popped++;
        assert_int_equal(iolink_events_flag(&ctx), ctx.count > 0U);
    }
    assert_int_equal(popped, 2U);
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
}

static void test_event_flag_invariant(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0071");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* 空队列：无标志。 */
    assert_false(iolink_events_flag(&ctx));

    /* 即使事件存储器从未被应答，通过 pop() 排空队列
       也必须清除标志。 */
    iolink_event_trigger(&ctx, 0x1001U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_true(iolink_events_flag(&ctx));
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));

    /* 通过事件存储器确认最后一个 Error 也会清除标志。 */
    iolink_event_trigger(&ctx, 0x1002U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));
}

static void test_notification_dedup(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);

    /* 重试的主站不得把重复请求变成事件风暴：
       同一未应答的 Notification 只入队一次。 */
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    iolink_event_trigger(&ctx, 0xFF91U, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 1U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0xFF91U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_SINGLE_SHOT);
    assert_false(iolink_events_pop(&ctx, &ev));
}

/* Table B.13/B.2.21.2：DeviceStatus 与 DetailedDeviceStatus 使用同一过滤集
 * （present || reported）。disappears 仍在队列中时 DeviceStatus 不得回 0；
 * 撤销发出后两者同时归零（消除 DeviceStatus 与 DDS 的不一致窗口）。 */
static void test_event_device_status_drops_with_condition(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.clock = &g_fake_clock;
    g_fake_ms = 0U;

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_diag_device_status(&ctx), 4U);

    iolink_event_clear(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(iolink_diag_device_status(&ctx), 4U);
    assert_int_equal(ctx.count, 1U); /* appears 仍在队列中 */

    /* 在最小出现窗口内，撤销被推迟；DDS 仍列出该条目，因此
       DeviceStatus 保持 Failure。 */
    g_fake_ms = 10U;
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(iolink_diag_device_status(&ctx), 4U);

    /* disappears 入队后条件与视图同时归零。 */
    g_fake_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 2U);
    assert_int_equal(iolink_diag_device_status(&ctx), 0U);

    /* Notification 从不提升 DeviceStatus。 */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_diag_device_status(&ctx), 0U);
}

/* Table B.13：取值与优先级。优先级 4->1、3->2、1->3、2->4；
 * 优先级数值最小者胜出，即 Failure > Functional-Check > Maintenance >
 * Out-of-Specification。 */
static void test_event_device_status_matrix(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;

    const struct
    {
        uint16_t code;
        uint8_t expected;
    } single[] = {
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, 1U},   {IOLINK_EVENTCODE_MAINTENANCE_CLEANING, 1U},
        {IOLINK_EVENTCODE_MAINTENANCE_REFILL, 1U}, {IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 2U},
        {IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},  {IOLINK_EVENTCODE_SHORT_CIRCUIT, 4U},
    };
    for (size_t i = 0U; i < (sizeof(single) / sizeof(single[0])); i++) {
        events_init(&ctx);
        iolink_event_trigger(&ctx, single[i].code,
                             iolink_diag_type_for(single[i].code, IOLINK_EVENT_TYPE_ERROR));
        assert_int_equal(iolink_diag_device_status(&ctx), single[i].expected);
    }

    const struct
    {
        uint16_t first;
        uint16_t second;
        uint8_t expected;
    } pairs[] = {
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 1U},
        {IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},
        {IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 3U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENTCODE_MAINTENANCE_WEAR, 4U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 4U},
        {IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 4U},
        {IOLINK_EVENTCODE_SIMULATION_ACTIVE, IOLINK_EVENTCODE_SHORT_CIRCUIT, 4U},
    };
    for (size_t i = 0U; i < (sizeof(pairs) / sizeof(pairs[0])); i++) {
        events_init(&ctx);
        iolink_event_trigger(&ctx, pairs[i].first,
                             iolink_diag_type_for(pairs[i].first, IOLINK_EVENT_TYPE_ERROR));
        iolink_event_trigger(&ctx, pairs[i].second,
                             iolink_diag_type_for(pairs[i].second, IOLINK_EVENT_TYPE_ERROR));
        assert_int_equal(iolink_diag_device_status(&ctx), pairs[i].expected);
    }
}

/* Table 101 "Clear"：每个已置起的 Error/Warning 都以 MODE
 * "event disappears" 撤销，已入队的 Notification 被丢弃，且 DeviceStatus
 * 由复位清除。 */
static void test_event_clear_diagnosis(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.clock = &g_fake_clock;
    g_fake_ms = 0U;

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 3U);
    assert_int_equal(iolink_diag_device_status(&ctx), 4U);

    iolink_events_clear_diagnosis(&ctx);

    /* 已入队的出现记录保留到被应答为止；复位会立即补上
       对应的消失记录并丢弃 Notification
       （队列在 4 项时已满，因此缺少过滤会静默
       挤掉 Notification 而掩盖自身）。 */
    assert_int_equal(ctx.count, 4U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
    assert_int_equal(iolink_diag_device_status(&ctx), 0U);

    /* 应答冻结的 appears 快照：撤销对成为
       呈现集合，因此其 MODE=2 限定符可在线上观察到
       （Figure A.24：MODE=disappears、TYPE=error/warning、INSTANCE=Application
       -> 0xB4 / 0xA4）。 */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x87U); /* 3 个槽位 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xB4U); /* E 消失 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x10U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0xE4U); /* W 出现 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x05U), 0x8CU);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x06U), 0x42U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x07U), 0xA4U); /* W 消失 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x08U), 0x8CU);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x09U), 0x42U);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_MAINTENANCE_WEAR);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_MAINTENANCE_WEAR);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_int_equal(ctx.count, 0U);
}

/* A.6.4：仍被 1 s 窗口推迟的出现从未被发送，
 * 因此 Clear 不得在线上放入未配对的撤销。 */
static void test_event_clear_diagnosis_skips_unreported_event(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.clock = &g_fake_clock;
    g_fake_ms = 0U;

    iolink_event_t ev;
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_pop(&ctx, &ev)); /* 出现记录到达主站 */
    iolink_event_clear(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION);
    g_fake_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);

    /* 在 1 s 窗口内重新置起：新的出现被推迟，
       主站未曾看到它，因此 Clear 无内容可撤销。 */
    g_fake_ms = IOLINK_EVENT_MIN_REPEAT_MS / 2U;
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 0U);

    iolink_events_clear_diagnosis(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
    assert_int_equal(iolink_diag_device_status(&ctx), 0U);
}

static int group_setup(void** state)
{
    (void) state;
    test_clock_install();
    return 0;
}

static int group_teardown(void** state)
{
    test_clock_uninstall();
    return test_tx_all_asserted(state);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow),
        cmocka_unit_test(test_event_queue_overflow),
        cmocka_unit_test(test_standard_codes),
        cmocka_unit_test(test_phy_diagnostic_codes),
        cmocka_unit_test(test_event_peek),
        cmocka_unit_test(test_event_helpers),
        cmocka_unit_test(test_event_device_status_drops_with_condition),
        cmocka_unit_test(test_event_device_status_matrix),
        cmocka_unit_test(test_event_classify),
        cmocka_unit_test(test_event_memory_layout),
        cmocka_unit_test(test_event_memory_frozen),
        cmocka_unit_test(test_event_memory_confirm),
        cmocka_unit_test(test_event_memory_confirm_only_presented),
        cmocka_unit_test(test_event_appears_disappears),
        cmocka_unit_test(test_event_lifecycle_no_repeat),
        cmocka_unit_test(test_event_window_is_runtime_configurable),
        cmocka_unit_test(test_event_window_defaults_without_config),
        cmocka_unit_test(test_event_recently_sent),
        cmocka_unit_test(test_event_pending_full_observable),
        cmocka_unit_test(test_events_reset_clears),
        cmocka_unit_test(test_event_memory_generation_is_complete),
        cmocka_unit_test(test_isr_reset_request_is_masked_until_applied),
        cmocka_unit_test(test_isr_writer_is_rejected),
        cmocka_unit_test(test_event_flag_invariant),
        cmocka_unit_test(test_pop_to_empty_clears_the_published_image),
        cmocka_unit_test(test_notification_dedup),
        cmocka_unit_test(test_event_clear_diagnosis),
        cmocka_unit_test(test_event_clear_diagnosis_skips_unreported_event),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
