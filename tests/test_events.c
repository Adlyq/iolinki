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
#include <unistd.h>

#include "iolinki/events.h"

#include "conformance_test.h"
#include "test_helpers.h"

static void events_init(iolink_events_ctx_t* ctx)
{
    iolink_events_init(ctx);
    ctx->time_us = iolink_test_time_us;
}

static uint32_t g_fake_ms;

static uint64_t fake_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_fake_ms * 1000U;
}

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
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0); /* OK */

    iolink_event_trigger(&ctx, 0x1001, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0); /* 仍为 OK */

    iolink_event_trigger(&ctx, 0x1002, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4); /* Failure */

    iolink_event_trigger(&ctx, 0x1003, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4); /* 仍为 Failure */

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
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_NO_MALFUNCTION),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_MAINTENANCE_REFILL),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_HARDWARE_FAULT),
                     IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_SHORT_CIRCUIT),
                     IOLINK_EVENT_TYPE_ERROR);
    /* 保留/未知代码默认归为 error。 */
    assert_int_equal(iolink_event_classify(0x2222U), IOLINK_EVENT_TYPE_ERROR);
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

    /* 槽位 1 位于地址 1,2,3：限定符 0xF4（appears|error|device|Application）、0x1801。 */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xF4U);
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
    iolink_event_trigger(&ctx, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x01U, 0xAAU);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE4U); /* Warning 限定符 */
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

    usleep(60000U); /* > IOLINK_EVENT_MIN_APPEAR_MS */
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

/* 10.10.2：同一 EventCode 每秒最多上报一次。第二次
 * 事件会被推迟而非丢弃，且时间戳为 0 是有效的。 */
static void test_event_recently_sent(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0076");
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
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
    ctx.time_us = fake_time_us;
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

/* Table B.13：DeviceStatus 反映当前条件，因此一旦原因消失就下降，
 * 即使 "event disappears" 仍因 10.10.2 的间隔而
 * 被推迟。 */
static void test_event_device_status_drops_with_condition(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    events_init(&ctx);
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4U);

    iolink_event_clear(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
    assert_int_equal(ctx.count, 1U); /* appears 仍在队列中 */

    /* 在最小出现窗口内，撤销被推迟，但状态
       仍保持 OK，因为该条件已不再置起。 */
    g_fake_ms = 10U;
    iolink_events_process(&ctx);
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);

    /* Notification 从不提升 DeviceStatus。 */
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_NO_MALFUNCTION, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
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
        iolink_event_trigger(&ctx, single[i].code, iolink_event_classify(single[i].code));
        assert_int_equal(iolink_events_get_highest_severity(&ctx), single[i].expected);
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
        iolink_event_trigger(&ctx, pairs[i].first, iolink_event_classify(pairs[i].first));
        iolink_event_trigger(&ctx, pairs[i].second, iolink_event_classify(pairs[i].second));
        assert_int_equal(iolink_events_get_highest_severity(&ctx), pairs[i].expected);
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
    ctx.time_us = fake_time_us;
    g_fake_ms = 0U;

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_GENERAL_MALFUNCTION, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_MAINTENANCE_WEAR, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(ctx.count, 3U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4U);

    iolink_events_clear_diagnosis(&ctx);

    /* 已入队的出现记录保留到被应答为止；复位会立即补上
       对应的消失记录并丢弃 Notification
       （队列在 4 项时已满，因此缺少过滤会静默
       挤掉 Notification 而掩盖自身）。 */
    assert_int_equal(ctx.count, 4U);
    assert_int_equal(iolink_events_get_dropped_count(&ctx), 0U);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);

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
    ctx.time_us = fake_time_us;
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
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0U);
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
        cmocka_unit_test(test_event_recently_sent),
        cmocka_unit_test(test_event_pending_full_observable),
        cmocka_unit_test(test_events_reset_clears),
        cmocka_unit_test(test_event_flag_invariant),
        cmocka_unit_test(test_notification_dedup),
        cmocka_unit_test(test_event_clear_diagnosis),
        cmocka_unit_test(test_event_clear_diagnosis_skips_unreported_event),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
