/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_config_trim.c
 * @brief 编译期配置：显式容量覆盖与关闭可选功能。
 *
 * 覆盖在首次 include 之前定义，因此会像项目构建那样准确命中 config.h 中的
 * #ifndef 防护。MINIMAL/TINY 组合包已退役，最小镜像改为直接关闭
 * Data Storage 与事件（`target_minimal` 结构配置）。
 */

#define IOLINK_FEATURE_EVENTS 0
#define IOLINK_ISDU_BUFFER_SIZE 64
#define IOLINK_EVENT_QUEUE_SIZE 2
#define IOLINK_MBX_SLOTS 8
#define IOLINK_REPLY_PLAN_SLOTS 12

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/config.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"

static void test_explicit_trim_is_honoured(void** state)
{
    (void) state;

    /* 显式定义优先于配置文件默认值；未关闭的开关保持默认开启。 */
    assert_int_equal(IOLINK_FEATURE_EVENTS, 0);
    assert_int_equal(IOLINK_FEATURE_DATA_STORAGE, 1);
    assert_int_equal(IOLINK_FEATURE_SYSTEM_COMMAND, 1);
}

static void test_capacity_overrides(void** state)
{
    (void) state;

    assert_int_equal(IOLINK_ISDU_BUFFER_SIZE, 64);
    assert_int_equal(IOLINK_EVENT_QUEUE_SIZE, 2);
    assert_int_equal(IOLINK_MBX_SLOTS, 8);
    assert_int_equal(IOLINK_REPLY_PLAN_SLOTS, 12);

    /* 覆盖值必须真正进入 DLL 布局，而不只是宏层生效。 */
    iolink_dll_ctx_t dll;
    (void) dll;
    assert_int_equal(sizeof(dll.main.isr_frame_slot) / sizeof(dll.main.isr_frame_slot[0]),
                     IOLINK_MBX_SLOTS);
    assert_int_equal(sizeof(dll.main.shared.reply_plan) / sizeof(dll.main.shared.reply_plan[0]),
                     IOLINK_REPLY_PLAN_SLOTS);
    /* 主泵槽位 0/1 + ISR 槽位：派生量不允许被单独覆盖。 */
    assert_int_equal(
        sizeof(dll.main.shared.isr_tx_payload) / sizeof(dll.main.shared.isr_tx_payload[0]),
        IOLINK_TX_SLOTS);
    assert_int_equal(IOLINK_TX_SLOTS, IOLINK_TX_ISR_SLOT + 1);

    iolink_isdu_ctx_t isdu;
    (void) isdu;
#if IOLINK_WORKSPACE_EMBEDDED
    assert_int_equal(sizeof(((iolink_isdu_ctx_t*) 0)->scratch.payload), 64U);
    assert_int_equal(sizeof(((iolink_isdu_ctx_t*) 0)->scratch.response), 64U);
    assert_int_equal(sizeof(((iolink_isdu_ctx_t*) 0)->scratch.request), 64U + 6U);
    assert_int_equal(sizeof(((iolink_isdu_ctx_t*) 0)->scratch.frame), 64U + 3U);
#endif

    iolink_events_ctx_t events;
    assert_int_equal(sizeof(events.queue) / sizeof(iolink_event_t), 2U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_explicit_trim_is_honoured),
        cmocka_unit_test(test_capacity_overrides),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
