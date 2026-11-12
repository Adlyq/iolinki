/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll.c
 * @brief 数据链路层（DLL）状态机的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "iolinki/device.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

#include "conformance_test.h"

static int test_setup(void** state)
{
    (void) state;
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    return 0;
}

static void deliver_frame_to_main(iolink_device_ctx_t* ctx);
static void deliver_frame_to_dll(iolink_device_ctx_t* ctx);
static void feed_frame_bytes(const uint8_t* frame, size_t len);

/* 用于固定测试配置的独立 Direct Parameter page 1 向量（Interface Spec B.1）。
 * 这些是字面量，不调用生产代码中的 DPP1 构建器，因此用例
 * 不能把被测代码当作自己的参考基准。
 *
 * COM2（38400 bit/s）、默认标识、应用 MinCycleTime 下限 0：
 *   0x02 = MinCycleTime，0x03 = M-sequenceCapability，0x04 = RevisionID，
 *   0x05 = ProcessDataIn，0x06 = ProcessDataOut，0x07/0x08 = VendorID，
 *   0x09..0x0B = DeviceID。 */
static const uint8_t k_dpp1_type0_com2[16] = {
    [0x01] = 0x00U, /* MasterCycleTime（任何主站写入之前） */
    [0x02] = 0x10U, /* 1.6 ms: ceil((58*1e6/38400)/100us) = 16 -> TB 00 */
    [0x03] = 0x01U, /* 支持 ISDU，OPERATE 编码 0（TYPE_0） */
    [0x04] = 0x11U, /* RevisionID 1.1 */
    [0x05] = 0x00U, /* 无 PD 输入 */
    [0x06] = 0x00U, /* 无 PD 输出 */
    [0x07] = 0xFFU, [0x08] = 0xFFU, [0x09] = 0x00U, [0x0A] = 0x00U, [0x0B] = 0x01U,
};

/* TYPE_2_1：PD-in 1 字节，PD-out 0，OD 1，COM2。 */
static const uint8_t k_dpp1_type21_com2[16] = {
    [0x01] = 0x00U, [0x02] = 0x13U, /* 1.9 ms: ceil((72*1e6/38400)/100us) = 19 */
    [0x03] = 0x01U,                 /* 支持 ISDU，OPERATE 编码 0（TYPE_2_1） */
    [0x04] = 0x11U, [0x05] = 0x08U, /* 1 字节 PD-in */
    [0x06] = 0x00U, [0x07] = 0xFFU, [0x08] = 0xFFU, [0x09] = 0x00U, [0x0A] = 0x00U, [0x0B] = 0x01U,
};

/* TYPE_2_V：PD-in 2 字节，PD-out 2 字节，OD 2，COM2。 */
static const uint8_t k_dpp1_type2v_com2[16] = {
    [0x01] = 0x00U, [0x02] = 0x21U, /* 3.3 ms: ceil((124*1e6/38400)/100us) = 33 */
    [0x03] = 0x0BU,                 /* 支持 ISDU，OPERATE 编码 5（TYPE_2_V，OD 2） */
    [0x04] = 0x11U, [0x05] = 0x10U, /* 2 字节 PD-in */
    [0x06] = 0x10U,                 /* 2 字节 PD-out */
    [0x07] = 0xFFU, [0x08] = 0xFFU, [0x09] = 0x00U, [0x0A] = 0x00U, [0x0B] = 0x01U,
};

/* Table B.3 解码实现，独立于生产代码中的辅助函数编写。 */
static uint32_t b3_decode_us(uint8_t octet)
{
    switch (octet >> 6) {
        case 0:
            return (uint32_t) (octet & 0x3FU) * 100U;
        case 1:
            return 6400U + (uint32_t) (octet & 0x3FU) * 400U;
        case 2:
            return 32000U + (uint32_t) (octet & 0x3FU) * 1600U;
        default:
            return 0U;
    }
}

static void wake_device(iolink_device_ctx_t* ctx)
{
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(ctx);
    usleep(200);
}

static void feed_type0_page_read(uint8_t addr)
{
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | addr), 0x00U};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
}

static void feed_type0_page_write(uint8_t addr, uint8_t od)
{
    uint8_t frame[3] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | addr), 0x00U, od};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
}

static void feed_type0_page_short_write(uint8_t addr)
{
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | addr), 0x00U};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
}

static void feed_master_command(uint8_t cmd)
{
    feed_type0_page_write(0x00U, cmd);
}

#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
/* 一个标记为非 Type-0 M-sequence 类型的 ISDU 空闲读
 * （READ | ISDU channel | FlowCTRL idle）。仍处于 STARTUP 的 V1.1 设备
 * 不得应答旧式的 OPERATE M-sequence。 */
static void feed_isdu_idle_with_type(uint8_t type_bits)
{
    uint8_t frame[2] = {
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE), 0x00U};
    frame[1] = (uint8_t) (type_bits | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
}
#endif

static void deliver_expect_type0_ack(iolink_device_ctx_t* ctx)
{
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);
}

static uint8_t deliver_type0_read(iolink_device_ctx_t* ctx)
{
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);

    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 2U);
    return tx[0];
}

static void deliver_expect_no_reply(iolink_device_ctx_t* ctx)
{
    mock_phy_clear_tx();
    deliver_frame_to_main(ctx);

    size_t tx_len = 0U;
    (void) mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 0U);
}

static void assert_dpp1_octets(iolink_device_ctx_t* ctx, const uint8_t* expected, uint8_t first,
                               uint8_t last)
{
    for (uint8_t addr = first; addr <= last; addr++) {
        feed_type0_page_read(addr);
        const uint8_t value = deliver_type0_read(ctx);
        assert_int_equal(value, expected[addr]);
    }
}

/* 完整的默认（TYPE_0、无 PD）PREOPERATE -> OPERATE 握手。 */
static void establish_operate_type0(iolink_device_ctx_t* ctx)
{
    wake_device(ctx);
    assert_true((iolink_device_get_state(ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(ctx) == IOLINK_DLL_STATE_STARTUP));

    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_PREOPERATE);

    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_ESTAB_COM);

    feed_master_command(IOLINK_CMD_PROCESS_DATA_OPERATE);
    deliver_expect_type0_ack(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_OPERATE);
}

/* 完整的 TYPE_2_1 PREOPERATE -> OPERATE 握手（非 TYPE_0 的建立过程）。 */
static void establish_operate_type21(iolink_device_ctx_t* ctx)
{
    wake_device(ctx);
    assert_true((iolink_device_get_state(ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(ctx) == IOLINK_DLL_STATE_STARTUP));

    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_PREOPERATE);

    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_ESTAB_COM);

    uint8_t frame[3] = {0x00U, IOLINK_MSEQ_TYPE_2, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    mock_phy_clear_tx();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* PD-in 1 + CKS */
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_OPERATE);
}

/* 完整的 TYPE_2_V（PD 2/2、OD 2）PREOPERATE -> OPERATE 握手。 */
static void establish_operate_type2v(iolink_device_ctx_t* ctx)
{
    wake_device(ctx);
    assert_true((iolink_device_get_state(ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(ctx) == IOLINK_DLL_STATE_STARTUP));

    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(ctx), k_dpp1_type2v_com2[0x02]);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_PREOPERATE);

    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_ESTAB_COM);

    uint8_t frame[6] = {0x00U, IOLINK_MSEQ_TYPE_2, 0x00U, 0x00U, 0x00U, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3); /* PD-in 2 + CKS */
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_OPERATE);
}

/* 用 TYPE_2_V 的 OPERATE M-sequence（PD-out 2、OD 2）读取一个 DPP1 字节。 */
static uint8_t read_dpp1_type2v(iolink_device_ctx_t* ctx, uint8_t addr)
{
    uint8_t frame[4] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | addr),
                        IOLINK_MSEQ_TYPE_2, 0x00U, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 5); /* OD 2 + PD-in 2 + CKS */
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);

    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 5U);
    return tx[0];
}

/* 下发一个 TYPE_2_1 页通道 WRITE（MC + CKT + OD），并验证
 * [PD-in 1][CKS] 应答内部自洽（采用 A.1.6 布局，
 * 状态标志位位于 CKS 字节的 bit 6-7）。 */
static void deliver_type21_page_write(iolink_device_ctx_t* ctx, uint8_t addr, uint8_t od)
{
    uint8_t frame[3] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | (addr & IOLINK_MC_ADDR_MASK)),
                        IOLINK_MSEQ_TYPE_2, od};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    mock_phy_clear_tx();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* PD-in 1 + CKS */
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);

    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 2U);
    const uint8_t flags = (uint8_t) (tx[1] & 0xC0U);
    const uint8_t crc_octets[2] = {tx[0], flags};
    assert_int_equal(tx[1], (uint8_t) (flags | iolink_checksum6(crc_octets, sizeof(crc_octets))));
}

/* 把 DPP1 的 ProcessDataIn/Out 字节（B.1.6）解码为字节数。 */
static uint32_t dpp1_pd_octets(uint8_t value)
{
    if ((value & 0x80U) == 0U) {
        const uint32_t bits = (uint32_t) (value & 0x1FU);
        return (bits + 7U) / 8U;
    }
    return (uint32_t) (value & 0x1FU) + 1U;
}

static void test_dll_wakeup_to_preoperate(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* 触发唤醒 */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_AWAITING_COMM);

    /* 等待 t_dwu 到期 */
    usleep(200);

    /* 发送合法的 Type 0 帧（空闲）以进入 PREOPERATE */
    uint8_t mc = 0x00;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 写仅回 CKS */
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

static void test_dll_preoperate_to_operate(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* 唤醒 */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t trans_mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t trans_ck = test_frame_checksum(trans_mc);
    mock_phy_feed_byte(trans_mc);
    mock_phy_feed_byte(trans_ck);
    mock_phy_feed_chk();
    /* 在 PREOPERATE 中对 Transition Command 不应答 */
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* 收到首个合法帧后从 ESTAB_COM 进入 OPERATE（WRITE 2_V：MC、CKT、
       PD-out(1)、OD(2)；应答 PD-in(2) + CKS）。 */
    uint8_t frame[5] = {0x00, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));
    for (int i = 0; i < 5; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3);
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

static void test_dll_crc_error_keeps_operate(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* 唤醒 */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();
    /* 对 Transition Command 不应答 */
    test_io_cycle(&dev.ctx);

    /* ESTAB_COM -> OPERATE（WRITE 2_V：MC、CKT、PD-out(1)、OD(2)）。 */
    uint8_t ok_frame[5] = {0x00, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    ok_frame[1] = (uint8_t) (ok_frame[1] | iolink_checksum6(ok_frame, 5));
    for (int i = 0; i < 5; i++) {
        mock_phy_feed_byte(ok_frame[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    const uint32_t baud_before = iolink_device_get_baudrate(&dev.ctx);

    /* 注入 CRC 错误：ST rc=2 表示丢弃该帧并重新监听。
       DL 模式和波特率不得改变（不回落 SIO）。 */
    for (int r = 0; r < 3; r++) {
        uint8_t bad_frame[5] = {0x00, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
        for (int i = 0; i < 5; i++) {
            mock_phy_feed_byte(bad_frame[i]);
        }
        mock_phy_feed_chk();
        test_io_cycle(&dev.ctx);
    }

    test_io_cycle(&dev.ctx);

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(iolink_device_get_baudrate(&dev.ctx), baud_before);

    iolink_dll_stats_t stats;
    iolink_device_get_dll_stats(&dev.ctx, &stats);
    assert_int_not_equal(stats.crc_errors, 0);
}

static void test_dll_reject_transition_in_operate(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 主站在 OPERATE 状态下发送 0x0F（Transition）。2_V 的 WRITE 帧为
       MC、CKT、PD-out(1)、OD(2)。 */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t frame[5] = {mc, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    for (int i = 0; i < 5; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    test_io_cycle(&dev.ctx);

    /* 不应改变状态，且应增加 framing_errors */
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    iolink_dll_stats_t stats;
    iolink_device_get_dll_stats(&dev.ctx, &stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

/* 将 Type-0 设备带入 OPERATE：唤醒、PREOPERATE、Transition Command，
   然后下发一个合法的 Type-0 空闲帧。 */
static void move_type0_to_operate(iolink_device_ctx_t* ctx)
{
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(ctx);
    usleep(200);

    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();
    test_io_cycle(ctx);

    uint8_t idle = 0x00;
    uint8_t idle_ck = test_frame_checksum(idle);
    mock_phy_feed_byte(idle);
    mock_phy_feed_byte(idle_ck);
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 写仅回 CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(ctx);
}

/* ST rc=2：在 OPERATE 中，与已配置 M-sequence 类型不匹配的 CKT 类型
   会被丢弃并重新监听链路。错位或重叠的 PD 字节流正是这种形态，
   因此若在此处降级到 STARTUP，链路就会在
   每次冲突时发生抖动。 */
static void test_dll_illegal_mseq_type(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Type-0 设备收到 Type-1 的 CKT。 */
    uint8_t frame[2] = {0x00, IOLINK_MSEQ_TYPE_1};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2));
    for (int i = 0; i < 2; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    iolink_dll_stats_t stats;
    iolink_device_get_dll_stats(&dev.ctx, &stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

/* C2/7.3.5.3：使用 FlowCTRL IDLE 在 ISDU 通道上读取并不构成
   发送请求；设备回答 No Service（0x00），且这不算错误。 */
static void test_dll_isdu_channel_idle(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* MC = READ | ISDU channel | FlowCTRL IDLE（0x11）。 */
    uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE);
    uint8_t frame[2] = {mc, 0x00};
    frame[1] = iolink_checksum6(frame, 1);
    for (int i = 0; i < 2; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    iolink_dll_stats_t after;
    iolink_device_get_dll_stats(&dev.ctx, &after);
    assert_int_equal(after.framing_errors, before.framing_errors);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* C2/C4：对诊断通道地址 0 的读取会被正常处理（StatusCode），
   且不算错误。 */
static void test_dll_diagnosis_channel(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* MC = READ | diagnosis channel | 地址 0。 */
    uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS);
    uint8_t frame[2] = {mc, 0x00};
    frame[1] = iolink_checksum6(frame, 1);
    for (int i = 0; i < 2; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    iolink_dll_stats_t after;
    iolink_device_get_dll_stats(&dev.ctx, &after);
    assert_int_equal(after.framing_errors, before.framing_errors);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* C4/7.3.8.2：应答 CKS 的 Event 位（bit 7）跟随事件标志，
   诊断读取返回 Table 58 中的 StatusCode。 */
static void test_dll_event_flag_in_cks(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);

    /* 触发一个错误事件：标志置位，内存槽位 1 被填充。 */
    iolink_event_trigger(iolink_device_get_events_ctx(&dev.ctx), 0x1801U, IOLINK_EVENT_TYPE_ERROR);

    /* MC = READ | diagnosis channel | 地址 0。应答为 [StatusCode][CKS]。 */
    uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS);
    uint8_t frame[2] = {mc, 0x00};
    frame[1] = iolink_checksum6(frame, 1);
    for (int i = 0; i < 2; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* 清空通道：标志保持置位，直到 StatusCode 写入得到确认。 */
    uint8_t mc_write = (uint8_t) (IOLINK_MC_CHANNEL_DIAGNOSIS); /* WRITE，地址 0 */
    uint8_t wframe[3] = {mc_write, 0x00, 0x00};
    wframe[1] = iolink_checksum6(wframe, 2);
    for (int i = 0; i < 3; i++) {
        mock_phy_feed_byte(wframe[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 写仅回 CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    assert_false(iolink_events_flag(iolink_device_get_events_ctx(&dev.ctx)));
}

/* 回归用例：在配置了 PD 宽度并进入 OPERATE 之后，重新启动，
   随后在 PREOPERATE 中下发 3 字节的 Type-0 ISDU 写，必须按 Type-0 帧解析
   （OD 位于偏移 2）并只回 CKS，而不能使用 OPERATE 的 PD
   宽度（该宽度会偏移字节流，导致恢复后的 ISDU 失效）。 */
static void test_dll_preoperate_type0_od_write_ignores_operate_widths(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_2, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* 启动探测：以 Type-0 READ 读取 MinCycleTime（MC 0xA2）-> PREOPERATE。 */
    uint8_t probe[2] = {0xA2U, 0x00U};
    probe[1] = (uint8_t) (probe[1] | iolink_checksum6(probe, 2U));
    mock_phy_feed_byte(probe[0]);
    mock_phy_feed_byte(probe[1]);
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* OD + CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* PREOPERATE 中的 ISDU 写 START：MC 0x70（W、ISDU、START）、CKT、OD 0xB5。 */
    uint8_t req[3] = {0x70U, 0x00U, 0xB5U};
    req[1] = (uint8_t) (req[1] | iolink_checksum6(req, 3U));
    for (size_t i = 0U; i < 3U; i++) {
        mock_phy_feed_byte(req[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Figure A.5：Type-0 写 -> 仅回 CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* 回归用例：在配置了 2 字节 OD 的 M-sequence（TYPE_2_V、OPERATE 编码
   5）时，Type-0 的 DeviceOperate 写仍是 3 字节帧（Figure A.5），
   必须回 CKS；按 4 字节预期会让该帧不完整并使设备保持静默，
   于是真实主站永远收不到该应答。 */
static void test_dll_type0_write_is_three_octets_with_two_octet_od(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    uint8_t probe[2] = {0xA2U, 0x00U};
    probe[1] = (uint8_t) (probe[1] | iolink_checksum6(probe, 2U));
    mock_phy_feed_byte(probe[0]);
    mock_phy_feed_byte(probe[1]);
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* DeviceOperate：MC 0x20、CKT、OD 0x99 -> 仅回 CKS，进入 ESTAB_COM。 */
    uint8_t op[3] = {0x20U, 0x00U, IOLINK_CMD_DEVICE_OPERATE};
    op[1] = (uint8_t) (op[1] | iolink_checksum6(op, 3U));
    for (size_t i = 0U; i < 3U; i++) {
        mock_phy_feed_byte(op[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);
}
/* Test Spec 0039：通过 0x9A 从 STARTUP 进入 PREOPERATE，然后
 * 从 DPP1 读取完整的通信参数集，并与独立的字面量向量
 * 比对（含 M-sequenceCapability、PDIn、PDOut）。 */
static void test_dll_startup_to_preoperate_read_dpp1(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0039");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    /* MasterCommand PREOPERATE（0x9A）：仅回 CKS 的确认。 */
    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* DPP1 0x02..0x06：MinCycleTime、M-seqCapability、RevisionID、PDIn、PDOut。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(k_dpp1_type0_com2[0x05], 0x00U); /* 无 ProcessDataIn */
    assert_int_equal(k_dpp1_type0_com2[0x06], 0x00U); /* 无 ProcessDataOut */
}

static void test_dll_startup_to_preoperate_write_dpp1(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0040");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 主站先读取 DPP1，然后回写一个字节做镜像。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    const uint8_t mirror = k_dpp1_type0_com2[0x03];

    /* 短 DPP1 写（不含 OD 字节）不予应答。 */
    feed_type0_page_short_write(0x03U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 完整的镜像写被接受，仅回 CKS。 */
    feed_type0_page_write(0x03U, mirror);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* MasterCycleTime（0x01）可写，且回读值保持不变。 */
    feed_type0_page_write(0x01U, 0x4FU);
    deliver_expect_type0_ack(&dev.ctx);
    feed_type0_page_read(0x01U);
    assert_int_equal(deliver_type0_read(&dev.ctx), 0x4FU);

    /* 只读的 DPP1 字节在镜像写之后仍保持原值。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
}

/* 在主循环泵之前下发一帧，且不清空 RX 字节。之所以需要这样做，
 * 是因为 ESTAB_COM 的 0x98 帧在主上下文中处理，而不是由 ISR 处理。 */
static void deliver_frame_to_main(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.main.shared.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    iolink_device_process(ctx);
    while (ctx->dll.main.shared.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    mock_phy_clear_rx();
}

/* 同上，但绕过 Timer-B 的用户代码门控，供那些直接设置 DLL
 * 状态、而不执行完整启动握手的用例使用。 */
static void deliver_frame_to_dll(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.main.shared.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    ctx->dll.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    iolink_dll_process(&ctx->dll.main);
    while (ctx->dll.main.shared.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    mock_phy_clear_rx();
}

/* Test Spec 0045：经由 MasterCommand 0x99 再 0x98 从 PREOPERATE -> OPERATE，
 * 然后读取完整的 DPP1 通信参数集。 */
static void test_dll_preoperate_to_operate_read_dpp1(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0045");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    /* STARTUP -> PREOPERATE。 */
    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 经由 0x99 从 PREOPERATE -> ESTAB_COM。 */
    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* 经由 PDOperate 0x98 从 ESTAB_COM -> OPERATE。 */
    feed_master_command(IOLINK_CMD_PROCESS_DATA_OPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* DPP1 0x02..0x06 包含 M-sequenceCapability、PDIn 和 PDOut。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(k_dpp1_type0_com2[0x05], 0x00U); /* 无 ProcessDataIn */
    assert_int_equal(k_dpp1_type0_com2[0x06], 0x00U); /* 无 ProcessDataOut */
}

/* Test Spec 0035：V1.1 设备在 STARTUP 期间不得接受 0x99，但必须在
 * Type-0 启动探测到达 PREOPERATE 之后接受同一命令，
 * 随后完成一次非 TYPE_0 的建立。 */
#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
static void test_dll_startup_0x99_rejected(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0035");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    /* 步骤 b-h：用两个 MasterCycleTime 值分别重复旧式的
       STARTUP->OPERATE 尝试。每轮中 V1.1 设备都会拒绝 DeviceOperate
       0x99 以及从 STARTUP 发出的旧式 OPERATE 类型 ISDU 空闲请求，
       然后回到 STARTUP 以处理下一个值。 */
    const uint8_t cycle_times[2] = {k_dpp1_type21_com2[0x02], 0xBFU};
    for (size_t i = 0U; i < (sizeof(cycle_times) / sizeof(cycle_times[0])); i++) {
        feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
        deliver_expect_no_reply(&dev.ctx);
        assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                    (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

        feed_isdu_idle_with_type(IOLINK_MSEQ_TYPE_2);
        deliver_expect_no_reply(&dev.ctx);
        assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                    (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

        /* 应用 MasterCycleTime 是一次合法的 Type-0 页写。 */
        feed_type0_page_write(0x01U, cycle_times[i]);
        deliver_expect_type0_ack(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
        feed_type0_page_read(0x01U);
        assert_int_equal(deliver_type0_read(&dev.ctx), cycle_times[i]);

        /* 步骤 g：在处理下一个 MasterCycleTime 值之前回到 STARTUP。 */
        feed_master_command(IOLINK_CMD_DEVICE_STARTUP);
        deliver_expect_type0_ack(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
    }

    /* 合法的 Type-0 启动探测会应答 DPP1 并进入 PREOPERATE。 */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 步骤 a-c：读取通信参数，然后应用 MasterCycleTime。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type21_com2, 0x03U, 0x06U);
    feed_type0_page_write(0x01U, 0x1AU);
    deliver_expect_type0_ack(&dev.ctx);

    /* 从 PREOPERATE 起 0x99 被接受，并到达 ESTAB_COM。 */
    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* 第一个非 TYPE_0 帧完成到 OPERATE 的转换。 */
    uint8_t frame[3] = {0x00U, IOLINK_MSEQ_TYPE_2, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}
#endif

#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
/* Test Spec 0038：在非 TYPE_0 的 OPERATE 链路中，第一个 TYPE_0 页读
 * 被消耗而不予应答，并使设备降到 STARTUP；下一个页读
 * 作为启动探测被应答，而 OPERATE 类型的空闲请求保持不应答。 */
static void test_dll_operate_type0_read_resets(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0038");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* 步骤 a：OPERATE 期间的 TYPE_0 页读是复位请求，不予应答。 */
    feed_type0_page_read(0x02U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    iolink_dll_stats_t after;
    iolink_device_get_dll_stats(&dev.ctx, &after);
    /* 合法的 Type-0 复位请求不算组帧错误。 */
    assert_int_equal(after.framing_errors, before.framing_errors);

    /* 步骤 b：重复的 TYPE_0 读即为启动探测，会被应答。 */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 步骤 c：OPERATE 类型的空闲请求在 PREOPERATE 中不得被应答。 */
    uint8_t idle[2] = {0x00U, IOLINK_MSEQ_TYPE_2};
    idle[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(idle, sizeof(idle)));
    feed_frame_bytes(idle, sizeof(idle));
    deliver_expect_no_reply(&dev.ctx);
    assert_int_not_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}
#endif

#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
/* Test Spec 0049：在 OPERATE 中，第一个 TYPE_0 的 MinCycleTime 读会不应答地
 * 把链路降到 STARTUP；第二次读则返回 MinCycleTime 字节。 */
static void test_dll_operate_type0_min_cycle_time_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0049");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    feed_type0_page_read(0x02U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    feed_type0_page_read(0x02U);
    const uint8_t min_cycle_time = deliver_type0_read(&dev.ctx);
    assert_int_equal(min_cycle_time, k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}
#endif

/* B.1.1/Table B.1：DPP2 地址 0x10..0x1F 是合法的页地址。对未实现
 * 地址的读返回 0，而不是被拒绝。 */
static void test_dll_type0_dpp2_unimplemented_read_returns_zero(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    feed_type0_page_read(0x10U);
    assert_int_equal(deliver_type0_read(&dev.ctx), 0U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

#if IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
/* 旧式主站的兼容处理：开启兼容开关后，OPERATE 期间的 TYPE_0 页读
 * 就地应答，链路不会重启（与 SDCI_TC_0038/0049 不符；
 * 保留给在 OPERATE 中轮询页面的主站使用）。 */
static void test_dll_compat_type0_page_read_answered_in_operate(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}
#endif

/* 下发一个包含 CHK 标记的完整帧。 */
static void feed_frame_bytes(const uint8_t* frame, size_t len)
{
    for (size_t i = 0U; i < len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
}

/* Test Spec 0034：主站施加不同的 CycTime 值，而对 0x02..0x06 的每次 DPP1
 * 读仍必须返回相同的、合法的设备数据。所用的周期时间字节
 * 是测试采用的 Table B.3 编码：
 * 0x00（无 MasterCycleTime）、0x1A（2.6 ms，COM2 下约 100 TBIT）、0x88（44.8 ms，
 * COM3 下约 10000 TBIT）以及 0xBF（132.8 ms，可表示的最大值）。
 * 规范中的 10 s 变体无法在 DPP1 中表示，仅在 HIL 下覆盖。 */
static void test_dll_startup_cycle_time_reads(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0034");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    const uint8_t cycle_times[] = {0x00U, 0x1AU, 0x88U, 0xBFU};
    for (size_t c = 0U; c < sizeof(cycle_times); c++) {
        /* 主站通过 M-sequence 写入 MasterCycleTime（DPP1 0x01）。 */
        feed_type0_page_write(0x01U, cycle_times[c]);
        deliver_expect_type0_ack(&dev.ctx);

        /* 该写入在回读时可见。 */
        feed_type0_page_read(0x01U);
        assert_int_equal(deliver_type0_read(&dev.ctx), cycle_times[c]);

        /* 设备通信数据 0x02..0x06 必须保持不变。 */
        assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    }
}

/* Test Spec 0036：STARTUP 中的 OPERATE 类型请求不得被应答。 */
static void test_dll_startup_operate_type_no_response(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0036");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    uint8_t frame[2] = {0x00U, IOLINK_MSEQ_TYPE_2};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    deliver_frame_to_main(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));
}

/* Test Spec 0037：在已完全建立的、非 TYPE_0 的 OPERATE 链路中，0x97
 * 使设备回到 STARTUP；OPERATE 类型的空闲请求不应答，
 * 其后的 TYPE_0 DPP1 读则以 MinCycleTime 字节应答。 */
static void test_dll_operate_to_startup_0x97(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0037");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 步骤 a：DeviceStartup 0x97 得到确认并回到 STARTUP。 */
    feed_master_command(IOLINK_CMD_DEVICE_STARTUP);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    /* 步骤 b：OPERATE 类型的 ISDU 空闲请求不得被应答。 */
    const uint8_t idle_mc =
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE);
    uint8_t idle[2] = {idle_mc, IOLINK_MSEQ_TYPE_2};
    idle[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(idle, sizeof(idle)));
    feed_frame_bytes(idle, sizeof(idle));
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    /* 步骤 c：该 TYPE_0 DPP1 读即为启动探测，会被应答。 */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* Table 45 T7：由 OPERATE M-sequence 承载的 DeviceStartup 0x97（页
 * 通道、地址 0）必须使设备回到 STARTUP。同一命令在 SDCI_TC_0037 中
 * 也会以 TYPE_0 帧到达；该路径已在上文覆盖。 */
static void test_dll_operate_type_master_command_0x97_restarts(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 对照：向 DPP1 的 MasterCycleTime（地址 0x01）写入 0x97 只是普通的
     * 参数写，必须保持 OPERATE。这样就把“地址 0 上的命令 0x97”
     * 与“页通道任意位置上的值 0x97”区分开来。 */
    deliver_type21_page_write(&dev.ctx, 0x01U, IOLINK_CMD_DEVICE_STARTUP);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 页地址 0 上的 DeviceStartup 0x97 必须回到 STARTUP。 */
    deliver_type21_page_write(&dev.ctx, 0x00U, IOLINK_CMD_DEVICE_STARTUP);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
}

/* Test Spec 0041：PREOPERATE 中截断的 DPP1 写不予应答；
 * 其后的完整写会被应答。 */
static void test_dll_preoperate_truncated_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0041");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    dev.ctx.dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;

    uint8_t truncated[2] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | 0x01U), 0x00U};
    truncated[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(truncated, sizeof(truncated)));
    feed_frame_bytes(truncated, sizeof(truncated));
    deliver_frame_to_dll(&dev.ctx);

    uint8_t write[3] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | 0x01U), 0x00U, 0x4FU};
    write[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(write, sizeof(write)));
    feed_frame_bytes(write, sizeof(write));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    deliver_frame_to_dll(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* 在 PREOPERATE 中用显式指定的 CKT/OD M-sequence 类型读取一个 DPP1 字节。
 * 设备应答时返回 true；应答值存入 *value。 */
static bool preoperate_read_with_type(iolink_device_ctx_t* ctx, uint8_t type_bits, uint8_t addr,
                                      bool expect_reply, uint8_t* value)
{
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | addr), type_bits};
    frame[1] = (uint8_t) (type_bits | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));

    if (expect_reply) {
        expect_any(mock_phy_send, data);
        expect_value(mock_phy_send, len, 2);
        will_return(mock_phy_send, 0);
    }
    mock_phy_clear_tx();
    deliver_frame_to_dll(ctx);
    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    if (tx_len == 0U) {
        return false;
    }
    assert_int_equal(tx_len, 2U);
    *value = tx[0];
    return true;
}

static void test_dll_preoperate_illegal_operate_type(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0044");
    (void) state;

    /* 情形 a：PREOPERATE = TYPE_0 且 != OPERATE（TYPE_2_1）。使用
       OPERATE M-sequence 类型的读不应答并强制进入 STARTUP；同一读
       改用 PREOPERATE 和 STARTUP 类型（均为 TYPE_0）时会被应答。 */
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    dev.ctx.dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    dev.ctx.dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_1;

    uint8_t value = 0U;
    assert_false(preoperate_read_with_type(&dev.ctx, IOLINK_MSEQ_TYPE_2, 0x02U, false, &value));
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    /* c) PREOPERATE M-sequence 类型（TYPE_0）：被应答，回到 PREOPERATE。 */
    assert_true(preoperate_read_with_type(&dev.ctx, IOLINK_MSEQ_TYPE_0, 0x02U, true, &value));
    assert_int_equal(value, k_dpp1_type0_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* d) STARTUP M-sequence 类型（TYPE_0）：被应答。 */
    assert_true(preoperate_read_with_type(&dev.ctx, IOLINK_MSEQ_TYPE_0, 0x02U, true, &value));
    assert_int_equal(value, k_dpp1_type0_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* Table 62 情形 c：PREOPERATE = TYPE_0 = OPERATE。三次读全部
 * 以 DPP1 的 MinCycleTime 值应答。 */
static void test_dll_preoperate_type0_same_type(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0044");
    (void) state;
    iolink_stack_config_t type0 = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev0;
    iolink_test_device_init(&dev0, &type0, NULL);
    iolink_device_set_timing_enforcement(&dev0.ctx, false);
    dev0.ctx.dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;

    uint8_t value = 0U;
    for (int pass = 0; pass < 3; pass++) {
        assert_true(preoperate_read_with_type(&dev0.ctx, IOLINK_MSEQ_TYPE_0, 0x02U, true, &value));
        assert_int_equal(value, k_dpp1_type0_com2[0x02]);
        assert_int_equal(iolink_device_get_state(&dev0.ctx), IOLINK_DLL_STATE_PREOPERATE);
    }
}

/* Test Spec 0097：OPERATE 中保留的 DPP1 FunctionID 字节为 0。 */
static void test_dll_function_id_zero_in_operate(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0097");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    establish_operate_type0(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    feed_type0_page_read(0x0CU);
    assert_int_equal(deliver_type0_read(&dev.ctx), 0x00U);
    feed_type0_page_read(0x0DU);
    assert_int_equal(deliver_type0_read(&dev.ctx), 0x00U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Test Spec 0046：经过完整的 0x9A/0x99/0x98 转换后，读取 DPP1，
 * 将各字节回写镜像，并验证每次写入都被接受且状态不变。 */
static void test_dll_operate_dpp1_read_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0046");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    establish_operate_type0(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 读取完整的通信参数集。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);

    /* 将每个读到的字节回写镜像；每个完整的写都会得到确认。 */
    for (uint8_t addr = 0x02U; addr <= 0x06U; addr++) {
        feed_type0_page_write(addr, k_dpp1_type0_com2[addr]);
        deliver_expect_type0_ack(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    }

    /* 镜像写之后只读的 DPP1 字节保持不变。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Test Spec 0047：OPERATE 中少一个字节的 DPP1 写不予应答；
 * 完整的写被接受，且其值在回读时可见。 */
static void test_dll_operate_negative_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0047");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    establish_operate_type0(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 读取 DPP1 并保存主站将要镜像的那个字节。 */
    feed_type0_page_read(0x02U);
    const uint8_t mirror = deliver_type0_read(&dev.ctx);
    assert_int_equal(mirror, k_dpp1_type0_com2[0x02]);

    /* 比预期少一个字节：不应答，状态不变。 */
    feed_type0_page_short_write(0x02U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 长度正确：仅以 CKS 应答。 */
    feed_type0_page_write(0x02U, mirror);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* 被接受的写没有破坏只读的 MinCycleTime。 */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), mirror);
}

/* Test Spec 0052：在建立 PREOPERATE 之后，用真实的 Type-0 DPP1
 * M-sequence 读取 M-sequenceCapability；bit 0（ISDU）必须置位。 */
static void test_dll_dpp1_mseqcap_isdu_bit(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0052");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    feed_type0_page_read(0x03U);
    const uint8_t capability = deliver_type0_read(&dev.ctx);
    assert_int_equal(capability, k_dpp1_type2v_com2[0x03]);
    assert_int_equal(capability & 0x01U, 0x01U);     /* 支持 ISDU 通道 */
    assert_int_equal((capability >> 1) & 0x07U, 5U); /* OPERATE 编码 TYPE_2_V OD 2 */
    assert_int_equal(capability & 0xC0U, 0x00U);     /* 保留位为零 */

    /* 其余协商得到的通信参数彼此一致。 */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type2v_com2, 0x02U, 0x06U);
    assert_int_equal(k_dpp1_type2v_com2[0x05], 0x10U); /* 2 字节 PD-in */
    assert_int_equal(k_dpp1_type2v_com2[0x06], 0x10U); /* 2 字节 PD-out */
}

/* Test Spec 0089：主站通过 DPP1 写入 MasterCycleTime 并回读到
 * 相同的值；仅直接改写 DLL 上下文字段是不够的。 */
static void test_dll_master_cycle_time_write_readback(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0089");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    const uint8_t cycle_times[] = {0x1AU, 0x88U, 0xBFU};
    for (size_t i = 0U; i < sizeof(cycle_times); i++) {
        feed_type0_page_write(0x01U, cycle_times[i]);
        deliver_expect_type0_ack(&dev.ctx);

        feed_type0_page_read(0x01U);
        assert_int_equal(deliver_type0_read(&dev.ctx), cycle_times[i]);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    }
}

/* Test Spec 0375：在 OPERATE 中读取 DPP1.MinCycleTime/M-sequenceCapability/PDIn/
 * PDOut，并用独立解码器校验 MinCycleTime 是否满足 A.3.4-A.3.6 的最佳情况
 * 以及 Table B.3 的允许范围。 */
static void test_dll_operate_min_cycle_time_valid(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0375");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    establish_operate_type2v(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    const uint8_t min_cycle_time = read_dpp1_type2v(&dev.ctx, 0x02U);
    const uint8_t capability = read_dpp1_type2v(&dev.ctx, 0x03U);
    const uint8_t pd_in = read_dpp1_type2v(&dev.ctx, 0x05U);
    const uint8_t pd_out = read_dpp1_type2v(&dev.ctx, 0x06U);

    assert_int_equal(min_cycle_time, k_dpp1_type2v_com2[0x02]);
    assert_int_equal(capability, k_dpp1_type2v_com2[0x03]);
    assert_int_equal(pd_in, k_dpp1_type2v_com2[0x05]);
    assert_int_equal(pd_out, k_dpp1_type2v_com2[0x06]);

    /* Table B.3：0x00 表示“无 MinCycleTime”；保留的时间基准为 0b11。 */
    assert_true((min_cycle_time >= 0x04U) && (min_cycle_time <= 0xBFU));
    const uint32_t advertised_us = b3_decode_us(min_cycle_time);
    assert_true((advertised_us >= 400U) && (advertised_us <= 132800U));
    assert_int_equal(advertised_us, 3300U); /* 0x21: 33 * 0.1 ms */

    /* A.3.4-A.3.6 的最佳情况，取 t1=t2=tidle=0、tA=1 bit time。
     * TYPE_2_V OD 2：请求 = MC+CKT+PDout(2) = 4 帧，
     * 应答 = OD(2)+PDin(2)+CKS = 5 帧。 */
    const uint32_t od_octets = ((capability >> 1) & 0x07U) == 5U ? 2U : 1U;
    const uint32_t m = 2U + dpp1_pd_octets(pd_out);
    const uint32_t n = od_octets + dpp1_pd_octets(pd_in) + 1U;
    const uint32_t baud = 38400U; /* 默认 COM2 */
    const uint32_t best_case_us = (((m + n) * 11U + 1U) * 1000000U + baud - 1U) / baud;
    assert_true(advertised_us >= best_case_us);
}

/* Test Spec 0376：仅有 PDOut 的设备总是报告“PD 有效”（bit 6 = 0）。 */
static void test_dll_pdout_only_status_valid(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0376");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    dev.ctx.dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    dev.ctx.dll.main.pd_valid = false;

    uint8_t read[2] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), 0x00U};
    read[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(read, sizeof(read)));
    feed_frame_bytes(read, sizeof(read));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    deliver_frame_to_dll(&dev.ctx);
    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 2U);
    assert_int_equal((tx[1] & 0x40U), 0U);

    uint8_t pd_operate[3] = {IOLINK_MC_CHANNEL_PAGE, 0x00U, IOLINK_CMD_PROCESS_DATA_OPERATE};
    pd_operate[1] =
        (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(pd_operate, sizeof(pd_operate)));
    feed_frame_bytes(pd_operate, sizeof(pd_operate));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    deliver_frame_to_dll(&dev.ctx);

    feed_frame_bytes(read, sizeof(read));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    deliver_frame_to_dll(&dev.ctx);
    tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 2U);
    assert_int_equal((tx[1] & 0x40U), 0U);
}

static uint8_t dll_test_page_read(iolink_device_ctx_t* ctx, uint8_t addr)
{
    uint8_t frame[2] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | addr), 0x00U};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);
    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 2U);
    return tx[0];
}

/* Test Spec 0315（预期失败的用例）：不兼容的 DeviceID 覆盖必须被
 * 拒绝，且已配置的 DeviceID 必须能跨重启保留。 */
static void test_dll_device_id_override_rejected(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0315");
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    move_type0_to_operate(&dev.ctx);

    uint8_t original[3] = {0};
    for (uint8_t addr = 0x09U; addr <= 0x0BU; addr++) {
        original[addr - 0x09U] = dll_test_page_read(&dev.ctx, addr);
    }

    for (uint8_t addr = 0x09U; addr <= 0x0BU; addr++) {
        uint8_t write[3] = {(uint8_t) (IOLINK_MC_CHANNEL_PAGE | addr), 0x00U,
                            (uint8_t) (0xF0U + addr)};
        write[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(write, sizeof(write)));
        feed_frame_bytes(write, sizeof(write));
        expect_any(mock_phy_send, data);
        expect_value(mock_phy_send, len, 1);
        will_return(mock_phy_send, 0);
        deliver_frame_to_main(&dev.ctx);
    }

    for (uint8_t addr = 0x09U; addr <= 0x0BU; addr++) {
        assert_int_equal(dll_test_page_read(&dev.ctx, addr), original[addr - 0x09U]);
    }

    /* 断电重启：用相同配置重新初始化；DID 必须保持不变。 */
    iolink_test_device_t restarted;
    will_return(mock_phy_init, 0);
    iolink_test_device_init(&restarted, &config, NULL);
    move_type0_to_operate(&restarted.ctx);
    for (uint8_t addr = 0x09U; addr <= 0x0BU; addr++) {
        assert_int_equal(dll_test_page_read(&restarted.ctx, addr), original[addr - 0x09U]);
    }
}

static void test_dll_receive_error_discards_partial_frame(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);

    mock_phy_feed_byte(0xA2U);
    mock_phy_feed_error();
    test_io_cycle(&dev.ctx);
    assert_int_equal(dev.ctx.dll.main.frame_index, 0);
    assert_int_equal(IOLINK_STAT_TOTAL(&dev.ctx.dll.main, framing_errors), 1);

    uint8_t frame[2] = {0xA2U, 0};
    frame[1] = iolink_checksum6(frame, sizeof(frame));
    for (size_t i = 0; i < sizeof(frame); ++i) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(dev.ctx.dll.main.frame_index, 0);
}

static void test_min_cycle_time_from_m_sequence(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));

    /* PD 输入/输出各 28 字节、1 OD、COM3：最坏情况的写 M-sequence 为
     * (29*14 + 31*12 + 6) us ~= 3.40 ms -> 35（单位 0.1 ms）。 */
    dll.main.shared.od_width = 1U;
    dll.main.pd_in_len_current = 28U;
    dll.main.shared.pd_out_len_current = 28U;
    dll.main.baudrate = IOLINK_BAUDRATE_COM3;
    assert_int_equal(iolink_dll_compute_min_cycle_time(&dll.main), 35U);

    /* 应用层下限更高时以它为准（5.0 ms）。 */
    dll.main.min_cycle_time_app_us = 5000U;
    assert_int_equal(iolink_dll_compute_min_cycle_time(&dll.main), 50U);

    /* 同一帧在 COM1 下需要多得多的传输时间。 */
    dll.main.min_cycle_time_app_us = 0U;
    dll.main.baudrate = IOLINK_BAUDRATE_COM1;
    assert_true(iolink_dll_compute_min_cycle_time(&dll.main) > 35U);
}

/* Back-to-box（10.7.5）：约 3 个主站周期后 DLL 停止通信。
 * 通过 mock_phy_send 以流式方式发送应答，因此也覆盖了流式传输路径。 */
/* ISR 传输：每个被处理的帧必须让 Back-to-box 计数恰好
 * 前进一次（数据 ISR 也会记录主站活动，但那只是刷新时间戳）。
 * 重复计数会让设备提前一个 MasterCycle 停止。 */
static void test_dll_isr_back_to_box_counts_each_frame_once(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1,
        .pd_in_len = 1U,
        .pd_out_len = 0U,
    };
    uint8_t frame[3] = {0x00U, IOLINK_MSEQ_TYPE_2, 0x00U};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    iolink_dll_request_back_to_box(&dev.ctx.dll.main);

    /* 三个被处理的 MasterCycle 内设备仍持续应答。 */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            mock_phy_feed_byte(frame[j]);
        }
        mock_phy_feed_chk();
        expect_any(mock_phy_send, data);
        expect_any(mock_phy_send, len);
        will_return(mock_phy_send, 0);
        test_io_cycle(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    }

    /* 下一帧会使设备进入通信禁用状态。 */
    for (int j = 0; j < 3; j++) {
        mock_phy_feed_byte(frame[j]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_any(mock_phy_send, len);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_false(dev.ctx.dll.main.back_to_box_pending);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_COMM_DISABLED);
}

/* 主循环泵对 TYPE_1/2 帧的兜底应答：发布一个合法的过程 READ，
 * 但不提供 ISR 的提前应答（如同 ISR 无法预先准备），然后检查
 * dll_send_od_reply() 构建的 [OD][PD-in]CKS 应答。 */
static void test_dll_main_pump_type1_2_reply(void** state)
{
    (void) state;
    iolink_stack_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    establish_operate_type2v(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    uint8_t frame[4] = {(uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK),
                        IOLINK_MSEQ_TYPE_2, 0x00U, 0x00U};
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(frame, sizeof(frame)));

    dev.ctx.dll.main.shared.pd_in_len = 2U;
    dev.ctx.dll.main.shared
        .pd_in[dev.ctx.dll.main.shared.pd_in_active][dev.ctx.dll.main.shared.od_width] = 0xAAU;
    dev.ctx.dll.main.shared
        .pd_in[dev.ctx.dll.main.shared.pd_in_active][dev.ctx.dll.main.shared.od_width + 1U] = 0x55U;

    /* 把该请求发布到邮箱环形缓冲区（即数据 ISR 所做的工作）。 */
    iolink_frame_rec_t* rec =
        (iolink_frame_rec_t*) iolink_ring_claim(&dev.ctx.dll.main.shared.isr_frame_ring);
    assert_non_null(rec);
    rec->len = (uint8_t) sizeof(frame);
    rec->eager = 0U; /* 无 ISR 应答：必须由主循环泵回复 */
    (void) memcpy(rec->data, frame, sizeof(frame));
    iolink_ring_publish(&dev.ctx.dll.main.shared.isr_frame_ring);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 5); /* OD 2 + PD-in 2 + CKS */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    while (dev.ctx.dll.main.shared.isr_tx_active != 0U) {
        iolink_device_process_isr(&dev.ctx);
    }

    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 5U);
    assert_int_equal(tx[0], 0x00U); /* 过程通道的 OD 应答为 0 */
    assert_int_equal(tx[1], 0x00U);
    assert_int_equal(tx[2], 0xAAU);
    assert_int_equal(tx[3], 0x55U);

    /* 对 [OD][PD-in] 按 A.1.6 计算校验和，CKS 字节只含状态位；
     * 压缩器本身在 test_crc_vectors 中做过穷举验证。 */
    uint8_t acc = 0x52U;
    for (size_t i = 0U; i < tx_len; i++) {
        uint8_t b = tx[i];
        if (i == (tx_len - 1U)) {
            b = (uint8_t) (b & 0xC0U); /* 仅状态位：低 6 位为 CKS */
        }
        acc = (uint8_t) (acc ^ b);
    }
    assert_int_equal(tx[4] & 0x3FU, iolink_checksum6_from_acc(acc) & 0x3FU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_dll_receive_error_discards_partial_frame, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_wakeup_to_preoperate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_to_operate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_startup_to_preoperate_read_dpp1, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_startup_to_preoperate_write_dpp1, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_to_operate_read_dpp1, test_setup,
                                        test_teardown),
#if !IOLINK_COMPAT_V10_STARTUP_OPERATE
        cmocka_unit_test_setup_teardown(test_dll_startup_0x99_rejected, test_setup, test_teardown),
#endif
#if !IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
        cmocka_unit_test_setup_teardown(test_dll_operate_type0_read_resets, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_type0_min_cycle_time_read, test_setup,
                                        test_teardown),
#endif
        cmocka_unit_test_setup_teardown(test_dll_type0_dpp2_unimplemented_read_returns_zero,
                                        test_setup, test_teardown),
#if IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
        cmocka_unit_test_setup_teardown(test_dll_compat_type0_page_read_answered_in_operate,
                                        test_setup, test_teardown),
#endif
        cmocka_unit_test_setup_teardown(test_dll_startup_cycle_time_reads, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_startup_operate_type_no_response, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_to_startup_0x97, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_type_master_command_0x97_restarts,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_truncated_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_illegal_operate_type, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_type0_same_type, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_function_id_zero_in_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_dpp1_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_negative_write, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_dpp1_mseqcap_isdu_bit, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_master_cycle_time_write_readback, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_min_cycle_time_valid, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_pdout_only_status_valid, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_device_id_override_rejected, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_crc_error_keeps_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_reject_transition_in_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_illegal_mseq_type, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_isdu_channel_idle, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_diagnosis_channel, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_event_flag_in_cks, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_type0_od_write_ignores_operate_widths,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_type0_write_is_three_octets_with_two_octet_od,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_main_pump_type1_2_reply, test_setup,
                                        test_teardown),
        cmocka_unit_test(test_min_cycle_time_from_m_sequence),
        cmocka_unit_test_setup_teardown(test_dll_isr_back_to_box_counts_each_frame_once, test_setup,
                                        test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
