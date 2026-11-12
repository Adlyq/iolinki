/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_capability.c
 * @brief 能力表（P6）：构建档位、唯一解析点与运行时覆盖规则。
 *
 * 套件只在 BYTE/BYTE 结构配置下构建，因此 vtable 探测用字节钩子。
 */

#include <setjmp.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include <cmocka.h>

#include "iolinki/capability.h"
#include "iolinki/device.h"
#include "test_helpers.h"

static void test_build_profile_matches_macros(void** state)
{
    (void) state;
    const iolink_build_caps_t* build = iolink_caps_build();

    assert_non_null(build);
    assert_int_equal(build->abi, IOLINK_CAP_ABI);
    assert_int_equal(build->concurrency, (uint8_t) IOLINK_CONCURRENCY);
    assert_int_equal(build->stats, (uint8_t) IOLINK_STATS_MODE);
    assert_int_equal(build->pd_in_buffers, (uint8_t) IOLINK_PD_IN_BUFFERS);
    assert_int_equal(build->event_min_appear_ms, IOLINK_EVENT_MIN_APPEAR_MS);
    assert_int_equal(build->event_min_repeat_ms, IOLINK_EVENT_MIN_REPEAT_MS);

#if IOLINK_FEATURE_EVENTS
    assert_true(iolink_caps_built(build, IOLINK_CAPB_EVENTS));
#else
    assert_false(iolink_caps_built(build, IOLINK_CAPB_EVENTS));
#endif
#if IOLINK_FEATURE_DATA_STORAGE
    assert_true(iolink_caps_built(build, IOLINK_CAPB_DATA_STORAGE));
#endif
#if IOLINK_FEATURE_SYSTEM_COMMAND
    assert_true(iolink_caps_built(build, IOLINK_CAPB_SYSTEM_COMMAND));
#endif
}

static void test_resolve_probes_the_built_vtable(void** state)
{
    (void) state;

    /* 空 vtable：没有任何传输能力。 */
    iolink_phy_api_t empty = {0};
    const iolink_caps_t none = iolink_caps_resolve(&empty, NULL, NULL, NULL);
    assert_false(iolink_caps_has(none, IOLINK_CAP_RX));
    assert_false(iolink_caps_has(none, IOLINK_CAP_TX));
    assert_false(iolink_caps_has(none, IOLINK_CAP_WAKEUP));

    /* mock PHY 提供构建档位所选的收发钩子与唤醒钩子。 */
    const iolink_caps_t caps = iolink_caps_resolve(&g_phy_mock, NULL, NULL, NULL);
    assert_true(iolink_caps_has(caps, IOLINK_CAP_RX));
    assert_true(iolink_caps_has(caps, IOLINK_CAP_TX));
    assert_true(iolink_caps_has(caps, IOLINK_CAP_WAKEUP));

    /* 缺 send 钩子：TX 能力必须消失（不是"有 tx_octet 就算"）。 */
    iolink_phy_api_t no_tx = g_phy_mock;
    no_tx.tx_octet = NULL;
    const iolink_caps_t caps_no_tx = iolink_caps_resolve(&no_tx, NULL, NULL, NULL);
    assert_true(iolink_caps_has(caps_no_tx, IOLINK_CAP_RX));
    assert_false(iolink_caps_has(caps_no_tx, IOLINK_CAP_TX));

    /* PHY 为 NULL：只有构建/配置能力。 */
    const iolink_caps_t null_phy = iolink_caps_resolve(NULL, NULL, NULL, NULL);
    assert_false(iolink_caps_has(null_phy, IOLINK_CAP_RX));
    assert_true(iolink_caps_has(null_phy, IOLINK_CAP_SYSTEM_COMMAND));
}

static void test_resolve_merges_runtime_switches(void** state)
{
    (void) state;
    iolink_stack_config_t stack = {0};
    iolink_app_callbacks_t app = {0};

    iolink_caps_t caps = iolink_caps_resolve(NULL, &stack, &app, NULL);
    assert_true(iolink_caps_has(caps, IOLINK_CAP_SYSTEM_COMMAND));
    assert_false(iolink_caps_has(caps, IOLINK_CAP_PD_IN_CALLBACK));
    assert_false(iolink_caps_has(caps, IOLINK_CAP_COMPAT_V10_OP));
    assert_false(iolink_caps_has(caps, IOLINK_CAP_REQUIRE_ISR_TX));

    stack.flags = IOLINK_STACK_FLAG_NO_SYSTEM_COMMAND | IOLINK_STACK_FLAG_REQUIRE_ISR_TX;
    stack.compat_flags = IOLINK_STACK_COMPAT_V10_STARTUP_OPERATE |
                         IOLINK_STACK_COMPAT_TYPE0_PAGE_READ_OPERATE |
                         IOLINK_STACK_COMPAT_DS_LEGACY_IMAGE_SUB0;
    app.on_pd_input = NULL;
    caps = iolink_caps_resolve(NULL, &stack, &app, NULL);
    assert_false(iolink_caps_has(caps, IOLINK_CAP_SYSTEM_COMMAND));
    assert_true(iolink_caps_has(caps, IOLINK_CAP_REQUIRE_ISR_TX));
    assert_true(iolink_caps_has(caps, IOLINK_CAP_COMPAT_V10_OP));
    assert_true(iolink_caps_has(caps, IOLINK_CAP_COMPAT_T0_READ));
    assert_true(iolink_caps_has(caps, IOLINK_CAP_DS_LEGACY_SUB0));
}

static void test_instance_caps_cannot_forge_hardware(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_stack_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    const iolink_caps_t before = iolink_device_capabilities(&dev.ctx);
    assert_true(iolink_caps_has(before, IOLINK_CAP_RX));

    /* vtable 绑定的位由 resolve() 重新推导：既不能伪造，也不能删除。 */
    const iolink_caps_t set =
        iolink_device_set_capabilities(&dev.ctx, IOLINK_CAP_SYSTEM_COMMAND | IOLINK_CAP_EVENTS);
    assert_true(iolink_caps_has(set, IOLINK_CAP_RX));
    assert_true(iolink_caps_has(set, IOLINK_CAP_SYSTEM_COMMAND));

    /* 运行时关闭 SystemCommand 是允许的。 */
    const iolink_caps_t off =
        iolink_device_set_capabilities(&dev.ctx, set & (iolink_caps_t) ~IOLINK_CAP_SYSTEM_COMMAND);
    assert_false(iolink_caps_has(off, IOLINK_CAP_SYSTEM_COMMAND));
    assert_int_equal(iolink_device_capabilities(&dev.ctx), off);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_build_profile_matches_macros),
        cmocka_unit_test(test_resolve_probes_the_built_vtable),
        cmocka_unit_test(test_resolve_merges_runtime_switches),
        cmocka_unit_test(test_instance_caps_cannot_forge_hardware),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
