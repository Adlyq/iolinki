/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_device.c
 * @brief 顶层设备 API（device.h）的单元测试：过程数据边界、
 *        PD-valid 标志、复位投递与内省。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "iolinki/device.h"
#include "iolinki/events.h"
#include "test_helpers.h"

static int g_reset_count;
static iolink_reset_type_t g_last_reset;

static void device_reset_handler(iolink_reset_type_t type)
{
    g_reset_count++;
    g_last_reset = type;
}

static iolink_device_config_t make_device_config(void)
{
    iolink_device_config_t config;

    (void) memset(&config, 0, sizeof(config));
    config.time_us = iolink_test_time_us;
#if IOLINK_FEATURE_DATA_STORAGE
    config.storage = &g_ds_storage_mock;
#endif
    config.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    config.stack.pd_in_len = 2U;
    config.stack.pd_out_len = 2U;
    config.stack.od_width = 1U;
    return config;
}

static void init_device(iolink_test_device_t* dev, iolink_m_seq_type_t type, uint8_t pd_in,
                        uint8_t pd_out)
{
    iolink_stack_config_t config = {0};
    config.m_seq_type = type;
    config.pd_in_len = pd_in;
    config.pd_out_len = pd_out;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(dev, &config, NULL), 0);
}

static void test_device_init_rejects_null(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;
    (void) memset(&config, 0, sizeof(config));

    assert_int_equal(iolink_device_init(NULL, &config), IOLINK_DEVICE_INIT_ERR_NULL);
    assert_int_equal(iolink_device_init(&ctx, NULL), IOLINK_DEVICE_INIT_ERR_NULL);
}

static void test_device_init_rejects_invalid_config(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config;

    (void) memset(&ctx, 0, sizeof(ctx));

    config = make_device_config();
    config.stack.m_seq_type = (iolink_m_seq_type_t) 10; /* > IOLINK_M_SEQ_TYPE_MAX */
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE);

    config = make_device_config();
    config.stack.pd_in_len = IOLINK_PD_IN_MAX_SIZE + 1U;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_PD_IN_LEN);

    config = make_device_config();
    config.stack.pd_out_len = IOLINK_PD_OUT_MAX_SIZE + 1U;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_PD_OUT_LEN);

    config = make_device_config();
    config.stack.od_width = 3U;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_OD_WIDTH);

    config = make_device_config();
    config.stack.od_width = IOLINK_OD_MAX_SIZE + 1U;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_OD_CAPACITY);

    config = make_device_config();
    config.stack.baudrate_set = true;
    config.stack.baudrate = (iolink_baudrate_t) 3;
    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_ERR_BAUDRATE);
}

static void test_device_init_validates_config_not_context(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config = make_device_config();

    /* 未初始化上下文中的值不得参与校验：只有
     * 调用者提供的配置才是权威来源。 */
    (void) memset(&ctx, 0, sizeof(ctx));
    ctx.dll.main.shared.m_seq_type = 7U;
    ctx.dll.main.shared.pd_in_len = IOLINK_PD_IN_MAX_SIZE + 1U;
    ctx.dll.main.pd_out_len = IOLINK_PD_OUT_MAX_SIZE + 1U;
    ctx.dll.main.shared.od_width = 3U;

    assert_int_equal(iolink_device_init(&ctx, &config), IOLINK_DEVICE_INIT_OK);
}

static void test_device_init_accepts_valid_config(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_stack_config_t stack = {0};

    setup_mock_phy();

    /* Type 2_V 的 PD 上限，配合应用侧的 1 字节 OD。 */
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    stack.pd_in_len = IOLINK_PD_IN_MAX_SIZE;
    stack.pd_out_len = IOLINK_PD_OUT_MAX_SIZE;
    stack.od_width = 1U;

    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &stack, NULL), IOLINK_DEVICE_INIT_OK);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), IOLINK_PD_IN_MAX_SIZE);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), IOLINK_PD_OUT_MAX_SIZE);

#if (IOLINK_PD_IN_MAX_SIZE >= 28U) && (IOLINK_PD_OUT_MAX_SIZE >= 2U)
    /* 不对称的 PD 尺寸必须能被独立接受。 */
    stack = (iolink_stack_config_t){0};
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    stack.pd_in_len = 28U;
    stack.pd_out_len = 2U;
    stack.od_width = 1U;

    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &stack, NULL), IOLINK_DEVICE_INIT_OK);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 28U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 2U);
#endif

    /* 除非 baudrate_set 请求，否则波特率被忽略。 */
    stack = (iolink_stack_config_t){0};
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    stack.pd_in_len = 2U;
    stack.pd_out_len = 2U;
    stack.od_width = 1U;
    stack.baudrate_set = false;
    stack.baudrate = (iolink_baudrate_t) 3;

    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &stack, NULL), IOLINK_DEVICE_INIT_OK);
}

static void test_device_pd_input_bounds(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, IOLINK_M_SEQ_TYPE_2_2, 2U, 0U);

    const uint8_t data[IOLINK_PD_IN_MAX_SIZE + 1U] = {0};

    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, NULL, 2U, true), -1);
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), -1);

    const uint8_t pd[2] = {0x11U, 0x22U};
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, pd, sizeof(pd), true), 0);
    assert_int_equal(dev.ctx.dll.main.pd_in_len_current, 2U);
    assert_memory_equal(&dev.ctx.dll.main.shared.pd_in[dev.ctx.dll.main.shared.pd_in_active]
                                                      [dev.ctx.dll.main.shared.od_width],
                        pd, sizeof(pd));
}

static void test_device_pd_valid_flag(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, IOLINK_M_SEQ_TYPE_2_2, 2U, 0U);

    const uint8_t pd[2] = {0xAAU, 0xBBU};
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, pd, sizeof(pd), false), 0);
    assert_false(dev.ctx.dll.main.pd_valid);

    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, pd, sizeof(pd), true), 0);
    assert_true(dev.ctx.dll.main.pd_valid);
}

static void test_device_pd_output_read(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, IOLINK_M_SEQ_TYPE_2_V, 2U, 3U);

    dev.ctx.dll.main.pd_out[0] = 0x01U;
    dev.ctx.dll.main.pd_out[1] = 0x02U;
    dev.ctx.dll.main.pd_out[2] = 0x03U;

    uint8_t out[4] = {0};
    assert_int_equal(iolink_device_pd_output_read(&dev.ctx, out, 2U), 2);
    assert_int_equal(out[0], 0x01U);
    assert_int_equal(out[1], 0x02U);

    (void) memset(out, 0, sizeof(out));
    assert_int_equal(iolink_device_pd_output_read(&dev.ctx, out, sizeof(out)), 3);
    assert_memory_equal(out, dev.ctx.dll.main.pd_out, 3U);

    assert_int_equal(iolink_device_pd_output_read(&dev.ctx, NULL, 1U), -1);
}

static void test_device_reset_handler(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, IOLINK_M_SEQ_TYPE_0, 0U, 0U);

    iolink_device_set_reset_handler(&dev.ctx, device_reset_handler);

    g_reset_count = 0;
    g_last_reset = IOLINK_RESET_APPLICATION;
    dev.ctx.dll.main.shared.isdu.reset_pending = true;
    iolink_device_process(&dev.ctx);
    assert_int_equal(g_reset_count, 1);
    assert_int_equal(g_last_reset, IOLINK_RESET_DEVICE);

    dev.ctx.dll.main.shared.isdu.app_reset_pending = true;
    iolink_device_process(&dev.ctx);
    assert_int_equal(g_reset_count, 2);
    assert_int_equal(g_last_reset, IOLINK_RESET_APPLICATION);
}

static void test_device_introspection(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, IOLINK_M_SEQ_TYPE_2_V, 2U, 3U);

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(iolink_device_get_m_seq_type(&dev.ctx), IOLINK_M_SEQ_TYPE_2_V);
    assert_int_equal(iolink_device_get_baudrate(&dev.ctx), IOLINK_BAUDRATE_COM2);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 2U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 3U);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
    assert_non_null(iolink_device_get_events_ctx(&dev.ctx));
    assert_non_null(iolink_device_get_ds_ctx(&dev.ctx));

    /* NULL 安全的访问器回退到文档记载的默认值。 */
    assert_int_equal(iolink_device_get_state(NULL), IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(iolink_device_get_m_seq_type(NULL), IOLINK_M_SEQ_TYPE_0);
}

static void test_device_set_pd_length_fixed(void** state)
{
    (void) state;
    iolink_test_device_t fixed;
    init_device(&fixed, IOLINK_M_SEQ_TYPE_1_2, 0U, 0U);
    /* 只有可变类型接受运行时长度变更（Table A.10）。 */
    assert_int_equal(iolink_device_set_pd_length(&fixed.ctx, 1U, 1U), -2);
    assert_int_equal(iolink_device_set_pd_length(NULL, 1U, 1U), -1);
}

static void test_device_set_pd_length_variable(void** state)
{
    (void) state;
    iolink_test_device_t var;
    init_device(&var, IOLINK_M_SEQ_TYPE_2_V, 4U, 4U);
    assert_int_equal(iolink_device_set_pd_length(&var.ctx, 2U, 2U), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&var.ctx), 2U);
    assert_int_equal(iolink_device_get_pd_out_len(&var.ctx), 2U);
    /* 超出配置的最大值。 */
    assert_int_equal(iolink_device_set_pd_length(&var.ctx, 5U, 5U), -1);
}

static void test_device_config_baudrate(void** state)
{
    (void) state;
    iolink_stack_config_t config = {0};
    config.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    config.pd_in_len = 2U;
    config.pd_out_len = 2U;
    config.baudrate = IOLINK_BAUDRATE_COM1;
    config.baudrate_set = true;

    iolink_test_device_t dev;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    assert_int_equal(iolink_device_get_baudrate(&dev.ctx), IOLINK_BAUDRATE_COM1);
    assert_int_equal(dev.ctx.dll.main.operating_baudrate, IOLINK_BAUDRATE_COM1);
}

/* 当绑定了存储后端且不存在已持久化的镜像时，DS 镜像和校验和会在
 * 初始化时建立，因此 Data_Storage_Size (0x0003/3) 与
 * Parameter_Checksum (0x0003/4) 在首次上传之前就已有效。 */
static void test_device_init_builds_ds_image(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_stack_config_t stack = {0};
    stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    stack.pd_in_len = 2U;
    stack.pd_out_len = 2U;
    stack.od_width = 1U;

    iolink_ds_mock_reset();
    setup_mock_phy();
    will_return(mock_phy_init, 0);

    (void) memset(&dev, 0, sizeof(dev));
    dev.cfg.phy = g_phy_mock;
    dev.cfg.time_us = iolink_test_time_us;
    dev.cfg.stack = stack;
    dev.cfg.storage = &g_ds_storage_mock;
    assert_int_equal(iolink_device_init(&dev.ctx, &dev.cfg), IOLINK_DEVICE_INIT_OK);

    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&dev.ctx);
    assert_non_null(ds);
    assert_true(ds->image_len > 0U);
}

/* 当 ISDU 传输空闲时，iolink_device_process() 会驱动事件状态机，
 * 无需应用调用 iolink_events_process()。 */
static void test_device_process_pumps_events(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    init_device(&dev, IOLINK_M_SEQ_TYPE_0, 0U, 0U);

    iolink_events_ctx_t* evt = iolink_device_get_events_ctx(&dev.ctx);
    iolink_event_trigger(evt, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(evt, 0x1000U);

    /* 在 50 ms 的最短出现窗口内，消失仍保持推迟；
       泵只输出 "appears" 条目。 */
    iolink_device_process(&dev.ctx);
    iolink_event_t ev;
    assert_true(iolink_events_pop(evt, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_false(iolink_events_pop(evt, &ev));

    /* 窗口之后，泵会自行输出消失事件。 */
    usleep(60000U);
    iolink_device_process(&dev.ctx);
    assert_true(iolink_events_pop(evt, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* Table A.10：当配置保留 od_width = 0 时，TYPE_1_2 默认使用 2 字节 OD（code 1）。
 * DLL 仅在 ISR 传输下存储 od_width，因此有条件地断言它；
 * 配置校验（接受推导出的 2）在两种传输下都会运行。 */
static void test_device_od_width_default(void** state)
{
    (void) state;
    iolink_test_device_t dev;
    iolink_stack_config_t config = {0};

    config.m_seq_type = IOLINK_M_SEQ_TYPE_1_2;
    config.pd_in_len = 0U;
    config.pd_out_len = 0U;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    assert_int_equal(dev.ctx.dll.main.shared.od_width, 2U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_init_rejects_null),
        cmocka_unit_test(test_device_init_rejects_invalid_config),
        cmocka_unit_test(test_device_init_validates_config_not_context),
        cmocka_unit_test(test_device_init_accepts_valid_config),
        cmocka_unit_test(test_device_pd_input_bounds),
        cmocka_unit_test(test_device_pd_valid_flag),
        cmocka_unit_test(test_device_pd_output_read),
        cmocka_unit_test(test_device_reset_handler),
        cmocka_unit_test(test_device_introspection),
        cmocka_unit_test(test_device_config_baudrate),
        cmocka_unit_test(test_device_od_width_default),
        cmocka_unit_test(test_device_set_pd_length_fixed),
        cmocka_unit_test(test_device_set_pd_length_variable),
        cmocka_unit_test(test_device_init_builds_ds_image),
        cmocka_unit_test(test_device_process_pumps_events),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
