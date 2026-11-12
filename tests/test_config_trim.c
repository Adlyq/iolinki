/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_config_trim.c
 * @brief 编译期配置：显式容量覆盖、IOLINK_MINIMAL_PROFILE 便捷开关以及
 *        关闭功能时的兼容层。
 *
 * 覆盖在首次 include 之前定义，因此会像项目构建那样准确命中 config.h 中的
 * #ifndef 防护。
 */

#define IOLINK_MINIMAL_PROFILE
#define IOLINK_FEATURE_EVENTS 0
#define IOLINK_ISDU_BUFFER_SIZE 64
#define IOLINK_EVENT_QUEUE_SIZE 2

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/config.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"

static void test_minimal_profile_disables_features(void** state)
{
    (void) state;

    assert_int_equal(IOLINK_FEATURE_DATA_STORAGE, 0);
    assert_int_equal(IOLINK_FEATURE_SYSTEM_COMMAND, 0);
    assert_int_equal(IOLINK_FEATURE_LEGACY_PARAMS, 0);
    assert_int_equal(IOLINK_PHY_FEATURE_WAKEUP, 0);
    assert_int_equal(IOLINK_PHY_FEATURE_SIO_CQ, 0);
    assert_int_equal(IOLINK_PHY_FEATURE_LEGACY_DIAG, 0);

    /* 显式定义优先于配置文件默认值。 */
    assert_int_equal(IOLINK_FEATURE_EVENTS, 0);
}

static void test_capacity_overrides(void** state)
{
    (void) state;

    assert_int_equal(IOLINK_ISDU_BUFFER_SIZE, 64);
    assert_int_equal(IOLINK_EVENT_QUEUE_SIZE, 2);

    iolink_isdu_ctx_t isdu;
    assert_int_equal(sizeof(isdu.buffer), 64U);
    assert_int_equal(sizeof(isdu.response_buf), 64U);

    iolink_events_ctx_t events;
    assert_int_equal(sizeof(events.queue) / sizeof(iolink_event_t), 2U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_minimal_profile_disables_features),
        cmocka_unit_test(test_capacity_overrides),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
