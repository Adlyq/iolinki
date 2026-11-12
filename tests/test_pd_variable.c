/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd_variable.c
 * @brief 可变过程数据长度（Type 2_V）的单元测试。
 *
 * 附录 A 固定了除 TYPE_2_V 之外所有 TYPE_1_x/TYPE_2_x M 序列的 PD 布局；
 * 只有 TYPE_2_V 携带可变 PD（A.14）。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/dll.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

#include "conformance_test.h"

static void test_pd_variable_lengths(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 8, .pd_out_len = 8};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    /* TYPE_2_V WRITE：MC、CKT、PD-out(8)、OD(2)。 */
    uint8_t frame[2U + 8U + 2U];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x00;
    frame[1] = IOLINK_MSEQ_TYPE_2;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));

    for (size_t i = 0; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* WRITE 应答：PD-in(8) + CKS。 */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 9);
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
}

static void test_pd_invalid_flag(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* PID 与 PD-in 有效标志绑定；送入一个字节并标记为无效。 */
    uint8_t dummy = 0;
    iolink_device_pd_input_update(&dev.ctx, &dummy, 1, false);

    move_to_operate_ctx(&dev.ctx);

    /* TYPE_2_V WRITE：MC、CKT、PD-out(1)、OD(2)。 */
    uint8_t frame[5] = {0x00, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));

    for (size_t i = 0; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* WRITE 应答：PD-in(1) + CKS（PD 无效标志位于 CKS 第 6 位）。 */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
}

static void test_pd_runtime_negotiation(void** state)
{
    (void) state;
    /* 最大 8 个八位字节的可变类型。 */
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 8, .pd_out_len = 8};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 8);

    /* 缩短运行时 PD 长度；该变更可通过 getter 观察到。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, 4, 4), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 4);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 4);

    /* 超过配置的最大值会被拒绝。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, 16, 16), -1);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 4); /* 未改变 */
}

static void test_pd_fixed_not_negotiable(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_2, .pd_in_len = 0, .pd_out_len = 0};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* 定长 M 序列拒绝运行时 PD 长度变更。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, 1, 1), -2);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 0);
}

/* 以显式 PD-in/PD-out 内容发送一个 OPERATE WRITE 周期，并逐字节校验
 * 两个方向，包括 PD-in + CKS 的应答布局。
 * 与 READ 不同（其 OD 是否存在在旧式流式路径中不同），WRITE 在两种
 * 传输上的请求/应答结构相同。 */
static void run_pd_roundtrip(iolink_test_device_t* dev, uint8_t pd_in_len, uint8_t pd_out_len)
{
    const uint8_t od_len = 2U; /* TYPE_2_V 默认值（Table A.10 code 5） */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t pd_in[IOLINK_PD_IN_MAX_SIZE];
    uint8_t frame[2U + IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE];
    uint8_t expected[IOLINK_PD_IN_MAX_SIZE + 1U];
    uint8_t readback[IOLINK_PD_OUT_MAX_SIZE];

    (void) memset(pd_out, 0, sizeof(pd_out));
    (void) memset(pd_in, 0, sizeof(pd_in));
    (void) memset(frame, 0, sizeof(frame));
    (void) memset(expected, 0, sizeof(expected));
    (void) memset(readback, 0, sizeof(readback));

    for (uint8_t i = 0U; i < pd_out_len; i++) {
        pd_out[i] = (uint8_t) (0x10U + i);
    }
    for (uint8_t i = 0U; i < pd_in_len; i++) {
        pd_in[i] = (uint8_t) (0x80U + i);
    }
    assert_int_equal(iolink_device_pd_input_update(&dev->ctx, pd_in, pd_in_len, true), 0);

    /* 主站报文：MC、CKT、PD-out、OD。校验和位于 CKT 中。 */
    const size_t frame_len = (size_t) 2U + pd_out_len + od_len;
    frame[0] = 0x00U; /* 过程通道上的 OPERATE WRITE */
    frame[1] = IOLINK_MSEQ_TYPE_2;
    (void) memcpy(&frame[2], pd_out, pd_out_len);
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, frame_len));

    for (size_t i = 0U; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* 应答：PD-in + CKS。 */
    (void) memcpy(expected, pd_in, pd_in_len);
    expected[pd_in_len] = iolink_checksum6(expected, pd_in_len);

    expect_memory(mock_phy_send, data, expected, (size_t) pd_in_len + 1U);
    expect_value(mock_phy_send, len, (size_t) pd_in_len + 1U);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev->ctx);

    assert_int_equal(iolink_device_pd_output_read(&dev->ctx, readback, pd_out_len), pd_out_len);
    assert_memory_equal(readback, pd_out, pd_out_len);
}

/* TYPE_2_V 必须在 PD 长度的两端极值处往返成功，且各方向长度相互
 * 独立（Table A.10 允许不同的 PDin/PDout 宽度）。 */

static void run_variable_pd_case(uint8_t pd_in_len, uint8_t pd_out_len)
{
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = pd_in_len, .pd_out_len = pd_out_len};
    iolink_test_device_t dev;

    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), pd_in_len);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), pd_out_len);

    move_to_operate_ctx(&dev.ctx);
    run_pd_roundtrip(&dev, pd_in_len, pd_out_len);
}

static void test_type2v_pdio_len_32_roundtrip(void** state)
{
    (void) state;
    setup_mock_phy();
    run_variable_pd_case(IOLINK_PD_IN_MAX_SIZE, IOLINK_PD_OUT_MAX_SIZE);
}

static void test_variable_pd_roundtrip_at_min_bounds(void** state)
{
    (void) state;
    setup_mock_phy();
    run_variable_pd_case(1U, 1U);
}

static void test_variable_pd_roundtrip_asymmetric_bounds(void** state)
{
    (void) state;
    static const uint8_t lengths[][2] = {
        {1U, 32U},
        {32U, 1U},
        {28U, 2U}, /* 旧式 28 字节输入 / 2 字节输出产品 */
        {2U, 28U},
    };

    setup_mock_phy();

    for (size_t l = 0U; l < (sizeof(lengths) / sizeof(lengths[0])); l++) {
        run_variable_pd_case(lengths[l][0], lengths[l][1]);
    }
}

/* 对 PDin 和 PDout 分别拒绝超出区间上界一个八位字节的取值，
 * 并接受单一方向的上界为零（Table A.10）。 */
static void test_variable_pd_length_limits(void** state)
{
    (void) state;
    iolink_stack_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                                    .pd_in_len = IOLINK_PD_IN_MAX_SIZE,
                                    .pd_out_len = IOLINK_PD_OUT_MAX_SIZE};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* 区间两端均被接受。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, 1U, 1U), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 1U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 1U);
    assert_int_equal(
        iolink_device_set_pd_length(&dev.ctx, IOLINK_PD_IN_MAX_SIZE, IOLINK_PD_OUT_MAX_SIZE), 0);

#if (IOLINK_PD_IN_MAX_SIZE >= 28U) && (IOLINK_PD_OUT_MAX_SIZE >= 2U)
    /* 非对称运行时长度独立协商。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, 28U, 2U), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 28U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 2U);
#endif

    /* Table A.10 允许单一方向为零；两者相互独立。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, 0U, IOLINK_PD_OUT_MAX_SIZE), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), 0U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), IOLINK_PD_OUT_MAX_SIZE);
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, IOLINK_PD_IN_MAX_SIZE, 0U), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), IOLINK_PD_IN_MAX_SIZE);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 0U);

    /* 超过任一最大值一个八位字节会被拒绝，且两者均保持不变。 */
    assert_int_equal(iolink_device_set_pd_length(&dev.ctx, IOLINK_PD_IN_MAX_SIZE + 1U, 0U), -1);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), IOLINK_PD_IN_MAX_SIZE);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 0U);
    assert_int_equal(
        iolink_device_set_pd_length(&dev.ctx, IOLINK_PD_IN_MAX_SIZE, IOLINK_PD_OUT_MAX_SIZE + 1U),
        -1);
    assert_int_equal(iolink_device_get_pd_in_len(&dev.ctx), IOLINK_PD_IN_MAX_SIZE);
    assert_int_equal(iolink_device_get_pd_out_len(&dev.ctx), 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_variable_lengths),
        cmocka_unit_test(test_pd_invalid_flag),
        cmocka_unit_test(test_pd_runtime_negotiation),
        cmocka_unit_test(test_pd_fixed_not_negotiable),
        cmocka_unit_test(test_variable_pd_roundtrip_at_min_bounds),
        cmocka_unit_test(test_variable_pd_roundtrip_asymmetric_bounds),
        cmocka_unit_test(test_type2v_pdio_len_32_roundtrip),
        cmocka_unit_test(test_variable_pd_length_limits),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
