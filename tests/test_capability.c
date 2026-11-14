/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_capability.c
 * @brief 能力表：构建档位、唯一解析点与运行时覆盖规则。
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
#include "iolinki/dll.h"
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
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    assert_int_equal(build->isr_rx, IOLINK_CAP_ISR_RX_FRAME);
#else
    assert_int_equal(build->isr_rx, IOLINK_CAP_ISR_RX_BYTE);
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    assert_int_equal(build->isr_tx, IOLINK_CAP_ISR_TX_FRAME);
#else
    assert_int_equal(build->isr_tx, IOLINK_CAP_ISR_TX_BYTE);
#endif
    assert_int_equal(
        build->compat_defaults,
        (uint8_t) ((IOLINK_COMPAT_V10_STARTUP_OPERATE ? IOLINK_STACK_COMPAT_V10_STARTUP_OPERATE
                                                      : 0U) |
                   (IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
                        ? IOLINK_STACK_COMPAT_TYPE0_PAGE_READ_OPERATE
                        : 0U) |
                   (IOLINK_DS_LEGACY_IMAGE_SUB0 ? IOLINK_STACK_COMPAT_DS_LEGACY_IMAGE_SUB0 : 0U)));
    assert_int_equal(build->timing_enforce_default, IOLINK_TIMING_ENFORCE_DEFAULT);
    assert_int_equal(build->reserved, 0U);

#if IOLINK_FEATURE_EVENTS
    assert_true(iolink_caps_built(build, IOLINK_CAPB_EVENTS));
#else
    assert_false(iolink_caps_built(build, IOLINK_CAPB_EVENTS));
#endif
#if IOLINK_FEATURE_DATA_STORAGE
    assert_true(iolink_caps_built(build, IOLINK_CAPB_DATA_STORAGE));
#else
    assert_false(iolink_caps_built(build, IOLINK_CAPB_DATA_STORAGE));
#endif
#if IOLINK_FEATURE_SYSTEM_COMMAND
    assert_true(iolink_caps_built(build, IOLINK_CAPB_SYSTEM_COMMAND));
#else
    assert_false(iolink_caps_built(build, IOLINK_CAPB_SYSTEM_COMMAND));
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
    assert_true(iolink_caps_has(caps, IOLINK_CAP_SIO_CQ));
    /* 否定侧：同样的 vtable 去掉 set_cq_line 后能力必须消失。 */
    iolink_phy_api_t no_cq = g_phy_mock;
    no_cq.set_cq_line = NULL;
    assert_false(iolink_caps_has(iolink_caps_resolve(&no_cq, NULL, NULL, NULL), IOLINK_CAP_SIO_CQ));
    /* mock PHY 没有这些可选钩子：能力位必须为 0。 */
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* TX FRAME 的 mock 上报线路占用（tx_frame 之后到 DMA 完成之前）。 */
    assert_true(iolink_caps_has(caps, IOLINK_CAP_TX_BUSY));
#else
    assert_false(iolink_caps_has(caps, IOLINK_CAP_TX_BUSY));
#endif
    assert_false(iolink_caps_has(caps, IOLINK_CAP_REQUEST_SERVICE));
    assert_false(iolink_caps_has(caps, IOLINK_CAP_PHY_FAULTS));

    /* 缺发送钩子：TX 能力必须消失（不是"有 rx 钩子就算"）。 */
    iolink_phy_api_t no_tx = g_phy_mock;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    no_tx.tx_frame = NULL;
#else
    no_tx.tx_octet = NULL;
#endif
    const iolink_caps_t caps_no_tx = iolink_caps_resolve(&no_tx, NULL, NULL, NULL);
    assert_true(iolink_caps_has(caps_no_tx, IOLINK_CAP_RX));
    assert_false(iolink_caps_has(caps_no_tx, IOLINK_CAP_TX));

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    /* tx_done 只是可选的帧结束通知：缺少它不改变 TX 能力，
     * 与 dll_isr_tx_step 只依赖 tx_octet 的实现一致。 */
    iolink_phy_api_t no_done = g_phy_mock;
    no_done.tx_done = NULL;
    const iolink_caps_t caps_no_done = iolink_caps_resolve(&no_done, NULL, NULL, NULL);
    assert_true(iolink_caps_has(caps_no_done, IOLINK_CAP_TX));
#endif

    /* PHY 为 NULL：只有构建/配置能力。 */
    const iolink_caps_t null_phy = iolink_caps_resolve(NULL, NULL, NULL, NULL);
    assert_false(iolink_caps_has(null_phy, IOLINK_CAP_RX));
#if IOLINK_FEATURE_SYSTEM_COMMAND
    assert_true(iolink_caps_has(null_phy, IOLINK_CAP_SYSTEM_COMMAND));
#endif
}

static void test_resolve_merges_runtime_switches(void** state)
{
    (void) state;
    iolink_stack_config_t stack = {0};
    iolink_app_callbacks_t app = {0};

    iolink_caps_t caps = iolink_caps_resolve(NULL, &stack, &app, NULL);
#if IOLINK_FEATURE_SYSTEM_COMMAND
    assert_true(iolink_caps_has(caps, IOLINK_CAP_SYSTEM_COMMAND));
#endif
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
#if IOLINK_FEATURE_SYSTEM_COMMAND
    assert_true(iolink_caps_has(set, IOLINK_CAP_SYSTEM_COMMAND));
#endif
#if IOLINK_FEATURE_EVENTS
    assert_true(iolink_caps_has(set, IOLINK_CAP_EVENTS));
#endif

    /* 伪造硬件位（mock PHY 未提供 request_service）必须被 resolve 丢弃。 */
    const iolink_caps_t forged =
        iolink_device_set_capabilities(&dev.ctx, IOLINK_CAP_REQUEST_SERVICE);
    assert_false(iolink_caps_has(forged, IOLINK_CAP_REQUEST_SERVICE));
    assert_true(iolink_caps_has(forged, IOLINK_CAP_RX));

    /* 运行时关闭 SystemCommand 是允许的。 */
    const iolink_caps_t off =
        iolink_device_set_capabilities(&dev.ctx, set & (iolink_caps_t) ~IOLINK_CAP_SYSTEM_COMMAND);
    assert_false(iolink_caps_has(off, IOLINK_CAP_SYSTEM_COMMAND));
    assert_int_equal(iolink_device_capabilities(&dev.ctx), off);
}

/* resolve() 的 build 覆盖入口：合成档位下的三级优先级。 */
static void test_resolve_uses_build_override(void** state)
{
    (void) state;
    iolink_build_caps_t fake = *iolink_caps_build();
    iolink_stack_config_t stack = {0};

    fake.built = 0U;
    const iolink_caps_t none = iolink_caps_resolve(NULL, &stack, NULL, &fake);
    assert_false(iolink_caps_has(none, IOLINK_CAP_EVENTS));
    assert_false(iolink_caps_has(none, IOLINK_CAP_SYSTEM_COMMAND));

    fake.built = IOLINK_CAPB_SYSTEM_COMMAND;
    const iolink_caps_t only_system = iolink_caps_resolve(NULL, &stack, NULL, &fake);
    assert_true(iolink_caps_has(only_system, IOLINK_CAP_SYSTEM_COMMAND));
    assert_false(iolink_caps_has(only_system, IOLINK_CAP_EVENTS));

    stack.flags = IOLINK_STACK_FLAG_NO_SYSTEM_COMMAND;
    const iolink_caps_t disabled = iolink_caps_resolve(NULL, &stack, NULL, &fake);
    assert_false(iolink_caps_has(disabled, IOLINK_CAP_SYSTEM_COMMAND));

    /* 构建期 ISR-TX-ONLY 也会置位运行时要求位。 */
    fake.built = IOLINK_CAPB_ISR_TX_ONLY;
    assert_true(
        iolink_caps_has(iolink_caps_resolve(NULL, NULL, NULL, &fake), IOLINK_CAP_REQUIRE_ISR_TX));
}

/* REQUIRE_ISR_TX 但 PHY 没有 request_service()：init 必须拒绝。 */
static void test_require_isr_tx_needs_request_service(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_stack_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0,
                                    .flags = IOLINK_STACK_FLAG_REQUIRE_ISR_TX};

    /* 校验在 PHY 初始化之前完成，因此不需要任何 mock 期望。 */
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), IOLINK_DEVICE_INIT_ERR_PHY);
}

/* 设备层的接线：iolink_device_set_capabilities() 必须让已发布的应答计划
 * 失效，否则计划键（状态/类型/宽度/波特率）不变时 ISR 会继续执行旧计划。 */
static void test_capability_override_invalidates_plans(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_stack_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    iolink_dll_publish_reply_plans(&dev.ctx.dll.main);
    assert_true(iolink_atomic_load(&dev.ctx.dll.main.shared.reply_plan_seq) != 0U);

    iolink_device_set_capabilities(
        &dev.ctx, iolink_device_capabilities(&dev.ctx) | IOLINK_CAP_COMPAT_T0_READ);
    assert_int_equal(iolink_atomic_load(&dev.ctx.dll.main.shared.reply_plan_seq), 0U);

    iolink_dll_publish_reply_plans(&dev.ctx.dll.main);
    assert_true(iolink_atomic_load(&dev.ctx.dll.main.shared.reply_plan_seq) != 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_build_profile_matches_macros),
        cmocka_unit_test(test_resolve_probes_the_built_vtable),
        cmocka_unit_test(test_resolve_merges_runtime_switches),
        cmocka_unit_test(test_resolve_uses_build_override),
        cmocka_unit_test(test_instance_caps_cannot_forge_hardware),
        cmocka_unit_test(test_capability_override_invalidates_plans),
        cmocka_unit_test(test_require_isr_tx_needs_request_service),
    };
    return cmocka_run_group_tests(tests, NULL, test_tx_all_asserted);
}
