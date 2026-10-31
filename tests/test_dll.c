/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll.c
 * @brief Unit tests for Data Link Layer (DLL) state machine
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "iolinki/device.h"
#include "iolinki/dll.h"
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

/* Independent Direct Parameter page 1 vectors (Interface Spec B.1) for the
 * fixed test configurations. These are literals, not calls into the production
 * DPP1 builder, so a test cannot use the code under test as its own oracle.
 *
 * COM2 (38400 bit/s), default identity, app MinCycleTime floor 0:
 *   0x02 = MinCycleTime, 0x03 = M-sequenceCapability, 0x04 = RevisionID,
 *   0x05 = ProcessDataIn, 0x06 = ProcessDataOut, 0x07/0x08 = VendorID,
 *   0x09..0x0B = DeviceID. */
static const uint8_t k_dpp1_type0_com2[16] = {
    [0x01] = 0x00U, /* MasterCycleTime (before any master write) */
    [0x02] = 0x10U, /* 1.6 ms: ceil((58*1e6/38400)/100us) = 16 -> TB 00 */
    [0x03] = 0x01U, /* ISDU supported, OPERATE code 0 (TYPE_0) */
    [0x04] = 0x11U, /* RevisionID 1.1 */
    [0x05] = 0x00U, /* no PD in */
    [0x06] = 0x00U, /* no PD out */
    [0x07] = 0xFFU, [0x08] = 0xFFU, [0x09] = 0x00U, [0x0A] = 0x00U, [0x0B] = 0x01U,
};

/* TYPE_2_1: PD-in 1 octet, PD-out 0, OD 1, COM2. */
static const uint8_t k_dpp1_type21_com2[16] = {
    [0x01] = 0x00U, [0x02] = 0x13U, /* 1.9 ms: ceil((72*1e6/38400)/100us) = 19 */
    [0x03] = 0x01U,                 /* ISDU supported, OPERATE code 0 (TYPE_2_1) */
    [0x04] = 0x11U, [0x05] = 0x08U, /* 1 octet PD-in */
    [0x06] = 0x00U, [0x07] = 0xFFU, [0x08] = 0xFFU, [0x09] = 0x00U, [0x0A] = 0x00U, [0x0B] = 0x01U,
};

/* TYPE_2_V: PD-in 2 octets, PD-out 2 octets, OD 2, COM2. */
static const uint8_t k_dpp1_type2v_com2[16] = {
    [0x01] = 0x00U, [0x02] = 0x21U, /* 3.3 ms: ceil((124*1e6/38400)/100us) = 33 */
    [0x03] = 0x0BU,                 /* ISDU supported, OPERATE code 5 (TYPE_2_V, OD 2) */
    [0x04] = 0x11U, [0x05] = 0x10U, /* 2 octets PD-in */
    [0x06] = 0x10U,                 /* 2 octets PD-out */
    [0x07] = 0xFFU, [0x08] = 0xFFU, [0x09] = 0x00U, [0x0A] = 0x00U, [0x0B] = 0x01U,
};

/* Table B.3 decode, written independently of the production helper. */
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

/* An ISDU idle read (READ | ISDU channel | FlowCTRL idle) tagged with a
 * non-Type-0 M-sequence type. A V1.1 Device that is still in STARTUP must not
 * answer a legacy OPERATE M-sequence. */
static void feed_isdu_idle_with_type(uint8_t type_bits)
{
    uint8_t frame[2] = {
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE), 0x00U};
    frame[1] = (uint8_t) (type_bits | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
}

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

/* Full default (TYPE_0, no PD) PREOPERATE -> OPERATE handshake. */
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

/* Full TYPE_2_1 PREOPERATE -> OPERATE handshake (non-TYPE_0 establishment). */
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
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* PD-in 1 + CKS */
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(ctx);
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Full TYPE_2_V (PD 2/2, OD 2) PREOPERATE -> OPERATE handshake. */
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

/* Read one DPP1 octet with the TYPE_2_V OPERATE M-sequence (PD-out 2, OD 2). */
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

/* Decode a DPP1 ProcessDataIn/Out octet (B.1.6) to a byte count. */
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

    /* Trigger wake-up */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_AWAITING_COMM);

    /* Wait for t_dwu to expire */
    usleep(200);

    /* Send valid Type 0 frame (idle) to enter PREOPERATE */
    uint8_t mc = 0x00;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

static void test_dll_preoperate_to_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* Wake-up */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t trans_mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t trans_ck = test_frame_checksum(trans_mc);
    mock_phy_feed_byte(trans_mc);
    mock_phy_feed_byte(trans_ck);
    mock_phy_feed_chk();
    /* No response for Transition Command in PREOPERATE */
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* ESTAB_COM -> OPERATE on first valid frame (WRITE 2_V: MC, CKT,
       PD-out(1), OD(2); reply PD-in(2) + CKS). */
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
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* Wake-up */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();
    /* No response for Transition Command */
    test_io_cycle(&dev.ctx);

    /* ESTAB_COM -> OPERATE (WRITE 2_V: MC, CKT, PD-out(1), OD(2)). */
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

    /* Inject CRC errors: ST rc=2 means discard the frame and re-listen. The
       DL mode and baudrate must not change (no SIO fallback). */
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
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Master sends 0x0F (Transition) while in OPERATE. The 2_V WRITE frame is
       MC, CKT, PD-out(1), OD(2). */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t frame[5] = {mc, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    for (int i = 0; i < 5; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    test_io_cycle(&dev.ctx);

    /* Should NOT change state and should increment framing_errors */
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    iolink_dll_stats_t stats;
    iolink_device_get_dll_stats(&dev.ctx, &stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

/* Bring a Type-0 device to OPERATE: wake, PREOPERATE, transition command,
   then a valid Type-0 idle frame. */
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
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);
    test_io_cycle(ctx);
}

/* ST rc=2: a CKT type that does not match the configured M-sequence type in
   OPERATE is dropped and the link re-listens. A misaligned/overlapped PD byte
   stream has exactly this shape, so demoting to STARTUP here would flap the
   link on every collision. */
static void test_dll_illegal_mseq_type(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Type-0 device receives a Type-1 CKT. */
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

/* C2/7.3.5.3: a read on the ISDU channel with FlowCTRL IDLE is no request for
   transmission; the device answers No Service (0x00) and it is not an error. */
static void test_dll_isdu_channel_idle(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* MC = READ | ISDU channel | FlowCTRL IDLE (0x11). */
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

/* C2/C4: a read on the diagnosis channel address 0 is served (StatusCode) and
   is not an error. */
static void test_dll_diagnosis_channel(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* MC = READ | diagnosis channel | address 0. */
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

/* C4/7.3.8.2: the reply CKS Event bit (bit 7) follows the event flag, and a
   diagnosis read returns the Table 58 StatusCode. */
static void test_dll_event_flag_in_cks(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);

    /* Trigger an error event: flag set, memory slot 1 populated. */
    iolink_event_trigger(iolink_device_get_events_ctx(&dev.ctx), 0x1801U, IOLINK_EVENT_TYPE_ERROR);

    /* MC = READ | diagnosis channel | address 0. Reply [StatusCode][CKS]. */
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

    /* Clean channel: flag stays set until the StatusCode write confirmation. */
    uint8_t mc_write = (uint8_t) (IOLINK_MC_CHANNEL_DIAGNOSIS); /* WRITE, addr 0 */
    uint8_t wframe[3] = {mc_write, 0x00, 0x00};
    wframe[1] = iolink_checksum6(wframe, 2);
    for (int i = 0; i < 3; i++) {
        mock_phy_feed_byte(wframe[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    assert_false(iolink_events_flag(iolink_device_get_events_ctx(&dev.ctx)));
}

/* Regression: after OPERATE with PD widths configured, a re-startup followed by
   a 3-octet Type-0 ISDU write in PREOPERATE must be parsed as a Type-0 frame
   (OD at offset 2) and answered with the CKS only, not with the OPERATE PD
   widths (which shifted the byte stream and broke ISDU after recovery). */
static void test_dll_preoperate_type0_od_write_ignores_operate_widths(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_2, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    usleep(200);

    /* Startup probe: Type-0 READ of MinCycleTime (MC 0xA2) -> PREOPERATE. */
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

    /* ISDU write START in PREOPERATE: MC 0x70 (W, ISDU, START), CKT, OD 0xB5. */
    uint8_t req[3] = {0x70U, 0x00U, 0xB5U};
    req[1] = (uint8_t) (req[1] | iolink_checksum6(req, 3U));
    for (size_t i = 0U; i < 3U; i++) {
        mock_phy_feed_byte(req[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Figure A.5: Type-0 write -> CKS only */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* Regression: with a 2-octet-OD M-sequence configured (TYPE_2_V, OPERATE code
   5), the Type-0 DeviceOperate write is still a 3-octet frame (Figure A.5) and
   must be answered with the CKS; a 4-octet expectation left the frame incomplete
   and the device silent, so a real master never saw the ack. */
static void test_dll_type0_write_is_three_octets_with_two_octet_od(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
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

    /* DeviceOperate: MC 0x20, CKT, OD 0x99 -> CKS-only reply, ESTAB_COM. */
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
/* Test Spec 0039: reach PREOPERATE from STARTUP via 0x9A, then read the full
 * communication-parameter set from DPP1 and compare with an independent literal
 * vector (M-sequenceCapability, PDIn, PDOut included). */
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

    /* MasterCommand PREOPERATE (0x9A): CKS-only acknowledgement. */
    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* DPP1 0x02..0x06: MinCycleTime, M-seqCapability, RevisionID, PDIn, PDOut. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(k_dpp1_type0_com2[0x05], 0x00U); /* no ProcessDataIn */
    assert_int_equal(k_dpp1_type0_com2[0x06], 0x00U); /* no ProcessDataOut */
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

    /* The master first reads DPP1, then mirrors one octet back. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    const uint8_t mirror = k_dpp1_type0_com2[0x03];

    /* A short DPP1 write (no OD octet) is not answered. */
    feed_type0_page_short_write(0x03U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* The complete mirror write is accepted with a CKS-only reply. */
    feed_type0_page_write(0x03U, mirror);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* MasterCycleTime (0x01) is writable and reads back unchanged. */
    feed_type0_page_write(0x01U, 0x4FU);
    deliver_expect_type0_ack(&dev.ctx);
    feed_type0_page_read(0x01U);
    assert_int_equal(deliver_type0_read(&dev.ctx), 0x4FU);

    /* A read-only DPP1 octet keeps its value after the mirror write. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
}

/* Deliver one frame without discarding RX bytes before the main pump. Needed
 * because the ESTAB_COM 0x98 frame is handled in main context, not by the ISR. */
static void deliver_frame_to_main(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    iolink_device_process(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    mock_phy_clear_rx();
}

/* Same, but bypass the Timer-B user-code gate for tests that set the DLL state
 * directly instead of running the full startup handshake. */
static void deliver_frame_to_dll(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    ctx->dll.phy_mode = IOLINK_PHY_MODE_SDCI;
    iolink_dll_process(&ctx->dll);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    mock_phy_clear_rx();
}

/* Test Spec 0045: PREOPERATE -> OPERATE via MasterCommand 0x99 followed by
 * 0x98, then read the complete DPP1 communication parameter set. */
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

    /* STARTUP -> PREOPERATE. */
    feed_master_command(IOLINK_CMD_DEVICE_PREOPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* PREOPERATE -> ESTAB_COM via 0x99. */
    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* ESTAB_COM -> OPERATE via PDOperate 0x98. */
    feed_master_command(IOLINK_CMD_PROCESS_DATA_OPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* DPP1 0x02..0x06 includes M-sequenceCapability, PDIn and PDOut. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(k_dpp1_type0_com2[0x05], 0x00U); /* no ProcessDataIn */
    assert_int_equal(k_dpp1_type0_com2[0x06], 0x00U); /* no ProcessDataOut */
}

/* Test Spec 0035: a V1.1 Device shall not accept 0x99 during STARTUP, but must
 * accept the same command after the Type-0 startup probe has reached
 * PREOPERATE and then complete a non-TYPE_0 establishment. */
static void test_dll_startup_0x99_rejected(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0035");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    /* Steps b-h: the legacy STARTUP->OPERATE attempt is repeated with both
       MasterCycleTime values. In each pass a V1.1 Device rejects DeviceOperate
       0x99 and a legacy OPERATE-type ISDU idle request from STARTUP, then
       returns to STARTUP for the next value. */
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

        /* Applying MasterCycleTime is a valid Type-0 page write. */
        feed_type0_page_write(0x01U, cycle_times[i]);
        deliver_expect_type0_ack(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
        feed_type0_page_read(0x01U);
        assert_int_equal(deliver_type0_read(&dev.ctx), cycle_times[i]);

        /* Step g: return to STARTUP before the next MasterCycleTime value. */
        feed_master_command(IOLINK_CMD_DEVICE_STARTUP);
        deliver_expect_type0_ack(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
    }

    /* The legal Type-0 startup probe answers DPP1 and enters PREOPERATE. */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* Steps a-c: read the communication parameters, then apply MasterCycleTime. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type21_com2, 0x03U, 0x06U);
    feed_type0_page_write(0x01U, 0x1AU);
    deliver_expect_type0_ack(&dev.ctx);

    /* From PREOPERATE, 0x99 is accepted and reaches ESTAB_COM. */
    feed_master_command(IOLINK_CMD_DEVICE_OPERATE);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* The first non-TYPE_0 frame completes the transition to OPERATE. */
    uint8_t frame[3] = {0x00U, IOLINK_MSEQ_TYPE_2, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_frame_bytes(frame, sizeof(frame));
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    deliver_frame_to_main(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Test Spec 0038: in a non-TYPE_0 OPERATE link, the first TYPE_0 page read is
 * consumed without a reply and drops the Device to STARTUP; the next one is
 * answered as the startup probe, and an OPERATE-type idle stays unanswered. */
static void test_dll_operate_type0_read_resets(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0038");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* Step a: TYPE_0 page read while OPERATE is a reset request, no reply. */
    feed_type0_page_read(0x02U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    iolink_dll_stats_t after;
    iolink_device_get_dll_stats(&dev.ctx, &after);
    assert_int_equal(after.framing_errors, before.framing_errors + 1U);

    /* Step b: the repeated TYPE_0 read is the startup probe and is answered. */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* Step c: an OPERATE-type idle request must not be answered in PREOPERATE. */
    uint8_t idle[2] = {0x00U, IOLINK_MSEQ_TYPE_2};
    idle[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(idle, sizeof(idle)));
    feed_frame_bytes(idle, sizeof(idle));
    deliver_expect_no_reply(&dev.ctx);
    assert_int_not_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Test Spec 0049: in OPERATE the first TYPE_0 MinCycleTime read drops the link
 * to STARTUP without a reply; the second one returns the MinCycleTime octet. */
static void test_dll_operate_type0_min_cycle_time_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0049");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
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

/* Feed a complete frame including its CHK marker. */
static void feed_frame_bytes(const uint8_t* frame, size_t len)
{
    for (size_t i = 0U; i < len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
}

/* Test Spec 0034: the master applies different CycTime values and every DPP1
 * read of 0x02..0x06 must still return the same, valid device data. The
 * literal cycle-time octets are the Table B.3 encodings used by the test:
 * 0x00 (no MasterCycleTime), 0x1A (2.6 ms ~ 100 TBIT at COM2), 0x88 (44.8 ms
 * ~ 10000 TBIT at COM3) and 0xBF (132.8 ms, the largest representable value).
 * The spec's 10 s variant cannot be represented in DPP1 and stays HIL-only. */
static void test_dll_startup_cycle_time_reads(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0034");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    wake_device(&dev.ctx);
    assert_true((iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM) ||
                (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_STARTUP));

    const uint8_t cycle_times[] = {0x00U, 0x1AU, 0x88U, 0xBFU};
    for (size_t c = 0U; c < sizeof(cycle_times); c++) {
        /* The master writes MasterCycleTime (DPP1 0x01) over the M-sequence. */
        feed_type0_page_write(0x01U, cycle_times[c]);
        deliver_expect_type0_ack(&dev.ctx);

        /* The write is visible on read-back. */
        feed_type0_page_read(0x01U);
        assert_int_equal(deliver_type0_read(&dev.ctx), cycle_times[c]);

        /* The device communication data 0x02..0x06 must stay constant. */
        assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    }
}

/* Test Spec 0036: an OPERATE-type request in STARTUP must not be answered. */
static void test_dll_startup_operate_type_no_response(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0036");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
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

/* Test Spec 0037: from a fully established non-TYPE_0 OPERATE link, 0x97
 * returns the Device to STARTUP; an OPERATE-type idle is not answered and the
 * following TYPE_0 DPP1 read is answered with the MinCycleTime octet. */
static void test_dll_operate_to_startup_0x97(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0037");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_1, .pd_in_len = 1, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    establish_operate_type21(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Step a: DeviceStartup 0x97 is acknowledged and returns to STARTUP. */
    feed_master_command(IOLINK_CMD_DEVICE_STARTUP);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    /* Step b: an OPERATE-type ISDU idle request must not be answered. */
    const uint8_t idle_mc =
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE);
    uint8_t idle[2] = {idle_mc, IOLINK_MSEQ_TYPE_2};
    idle[1] = (uint8_t) (IOLINK_MSEQ_TYPE_2 | iolink_checksum6(idle, sizeof(idle)));
    feed_frame_bytes(idle, sizeof(idle));
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    /* Step c: the TYPE_0 DPP1 read is the startup probe and is answered. */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), k_dpp1_type21_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* Test Spec 0041: a truncated DPP1 write in PREOPERATE is not answered; the
 * following complete write is answered. */
static void test_dll_preoperate_truncated_write(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0041");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    dev.ctx.dll.state = IOLINK_DLL_STATE_PREOPERATE;

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

/* Read one DPP1 octet with an explicit CKT/OD M-sequence type in PREOPERATE.
 * Returns true when the Device answered; the answer is stored in *value. */
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

    /* Case a: PREOPERATE = TYPE_0 and != OPERATE (TYPE_2_1). A read with the
       OPERATE M-sequence type is unanswered and forces STARTUP; the same read
       with the PREOPERATE and STARTUP types (both TYPE_0) is answered. */
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    dev.ctx.dll.state = IOLINK_DLL_STATE_PREOPERATE;
    dev.ctx.dll.m_seq_type = IOLINK_M_SEQ_TYPE_2_1;

    uint8_t value = 0U;
    assert_false(preoperate_read_with_type(&dev.ctx, IOLINK_MSEQ_TYPE_2, 0x02U, false, &value));
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);

    /* c) PREOPERATE M-sequence type (TYPE_0): answered, returns to PREOPERATE. */
    assert_true(preoperate_read_with_type(&dev.ctx, IOLINK_MSEQ_TYPE_0, 0x02U, true, &value));
    assert_int_equal(value, k_dpp1_type0_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* d) STARTUP M-sequence type (TYPE_0): answered. */
    assert_true(preoperate_read_with_type(&dev.ctx, IOLINK_MSEQ_TYPE_0, 0x02U, true, &value));
    assert_int_equal(value, k_dpp1_type0_com2[0x02]);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* Table 62 case c: PREOPERATE = TYPE_0 = OPERATE. All three reads are
 * answered with the DPP1 MinCycleTime value. */
static void test_dll_preoperate_type0_same_type(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0044");
    (void) state;
    iolink_config_t type0 = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev0;
    iolink_test_device_init(&dev0, &type0, NULL);
    iolink_device_set_timing_enforcement(&dev0.ctx, false);
    dev0.ctx.dll.state = IOLINK_DLL_STATE_PREOPERATE;

    uint8_t value = 0U;
    for (int pass = 0; pass < 3; pass++) {
        assert_true(preoperate_read_with_type(&dev0.ctx, IOLINK_MSEQ_TYPE_0, 0x02U, true, &value));
        assert_int_equal(value, k_dpp1_type0_com2[0x02]);
        assert_int_equal(iolink_device_get_state(&dev0.ctx), IOLINK_DLL_STATE_PREOPERATE);
    }
}

/* Test Spec 0097: in OPERATE the reserved DPP1 FunctionID octets are zero. */
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

/* Test Spec 0046: from a full 0x9A/0x99/0x98 transition, read DPP1, mirror the
 * octets back and verify every write is accepted without changing the state. */
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

    /* Read the full communication parameter set. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);

    /* Mirror each read octet back; every complete write is acknowledged. */
    for (uint8_t addr = 0x02U; addr <= 0x06U; addr++) {
        feed_type0_page_write(addr, k_dpp1_type0_com2[addr]);
        deliver_expect_type0_ack(&dev.ctx);
        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    }

    /* Read-only DPP1 octets are unchanged after the mirror writes. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type0_com2, 0x02U, 0x06U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Test Spec 0047: a one-octet-short DPP1 write in OPERATE is not answered; the
 * complete write is accepted and its value is visible on read-back. */
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

    /* Read DPP1 and save the octet the master will mirror. */
    feed_type0_page_read(0x02U);
    const uint8_t mirror = deliver_type0_read(&dev.ctx);
    assert_int_equal(mirror, k_dpp1_type0_com2[0x02]);

    /* One octet less than expected: not answered, state unchanged. */
    feed_type0_page_short_write(0x02U);
    deliver_expect_no_reply(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Correct length: answered with the CKS only. */
    feed_type0_page_write(0x02U, mirror);
    deliver_expect_type0_ack(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* The accepted write did not corrupt the read-only MinCycleTime. */
    feed_type0_page_read(0x02U);
    assert_int_equal(deliver_type0_read(&dev.ctx), mirror);
}

/* Test Spec 0052: read M-sequenceCapability with an actual Type-0 DPP1
 * M-sequence after establishing PREOPERATE; bit 0 (ISDU) must be set. */
static void test_dll_dpp1_mseqcap_isdu_bit(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0052");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
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
    assert_int_equal(capability & 0x01U, 0x01U);     /* ISDU channel supported */
    assert_int_equal((capability >> 1) & 0x07U, 5U); /* OPERATE code TYPE_2_V OD 2 */
    assert_int_equal(capability & 0xC0U, 0x00U);     /* reserved bits zero */

    /* The rest of the negotiated communication parameters are coherent. */
    assert_dpp1_octets(&dev.ctx, k_dpp1_type2v_com2, 0x02U, 0x06U);
    assert_int_equal(k_dpp1_type2v_com2[0x05], 0x10U); /* 2 octet PD-in */
    assert_int_equal(k_dpp1_type2v_com2[0x06], 0x10U); /* 2 octet PD-out */
}

/* Test Spec 0089: the master writes MasterCycleTime over DPP1 and reads the
 * same value back; it is not enough to poke the DLL context field. */
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

/* Test Spec 0375: in OPERATE read DPP1.MinCycleTime/M-sequenceCapability/PDIn/
 * PDOut and check MinCycleTime against the A.3.4-A.3.6 best case and the
 * Table B.3 allowed range, using an independent decoder. */
static void test_dll_operate_min_cycle_time_valid(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0375");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
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

    /* Table B.3: 0x00 means "no MinCycleTime"; reserved bases are 0b11. */
    assert_true((min_cycle_time >= 0x04U) && (min_cycle_time <= 0xBFU));
    const uint32_t advertised_us = b3_decode_us(min_cycle_time);
    assert_true((advertised_us >= 400U) && (advertised_us <= 132800U));
    assert_int_equal(advertised_us, 3300U); /* 0x21: 33 * 0.1 ms */

    /* A.3.4-A.3.6 best case with t1=t2=tidle=0 and tA=1 bit time.
     * TYPE_2_V OD 2: request = MC+CKT+PDout(2) = 4 frames,
     * reply = OD(2)+PDin(2)+CKS = 5 frames. */
    const uint32_t od_octets = ((capability >> 1) & 0x07U) == 5U ? 2U : 1U;
    const uint32_t m = 2U + dpp1_pd_octets(pd_out);
    const uint32_t n = od_octets + dpp1_pd_octets(pd_in) + 1U;
    const uint32_t baud = 38400U; /* COM2 default */
    const uint32_t best_case_us = (((m + n) * 11U + 1U) * 1000000U + baud - 1U) / baud;
    assert_true(advertised_us >= best_case_us);
}

/* Test Spec 0376: a PDOut-only Device always reports "PD valid" (bit 6 = 0). */
static void test_dll_pdout_only_status_valid(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0376");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    dev.ctx.dll.state = IOLINK_DLL_STATE_OPERATE;
    dev.ctx.dll.pd_valid = false;

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

/* Test Spec 0315 (test to fail): an incompatible DeviceID override must be
 * rejected and the configured DeviceID must survive a restart. */
static void test_dll_device_id_override_rejected(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0315");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
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

    /* Power cycle: re-init with the same configuration; DID must be unchanged. */
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
    assert_int_equal(dev.ctx.dll.frame_index, 0);
    assert_int_equal(IOLINK_STAT_TOTAL(&dev.ctx.dll, framing_errors), 1);

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
    assert_int_equal(dev.ctx.dll.frame_index, 0);
}

static void test_min_cycle_time_from_m_sequence(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));

    /* 28-byte PD in/out, 1 OD, COM3: the worst-case write M-sequence is
     * (29*14 + 31*12 + 6) us ~= 3.40 ms -> 35 (0.1 ms units). */
    dll.od_width = 1U;
    dll.pd_in_len_current = 28U;
    dll.pd_out_len_current = 28U;
    dll.baudrate = IOLINK_BAUDRATE_COM3;
    assert_int_equal(iolink_dll_compute_min_cycle_time(&dll), 35U);

    /* The application floor wins when it is higher (5.0 ms). */
    dll.min_cycle_time_app_us = 5000U;
    assert_int_equal(iolink_dll_compute_min_cycle_time(&dll), 50U);

    /* COM1 needs far more time for the same frame. */
    dll.min_cycle_time_app_us = 0U;
    dll.baudrate = IOLINK_BAUDRATE_COM1;
    assert_true(iolink_dll_compute_min_cycle_time(&dll) > 35U);
}

/* Back-to-box (10.7.5): after ~3 master cycles the DLL stops communicating.
 * Streams a reply through mock_phy_send, so exercise the streaming transport. */
/* ISR transport: every served frame must advance the Back-to-box count exactly
 * once (the data ISR also records master activity, but that is a timestamp-only
 * refresh). A double count stops the device one MasterCycle too early. */
static void test_dll_isr_back_to_box_counts_each_frame_once(void** state)
{
    (void) state;
    iolink_config_t config = {
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
    iolink_dll_request_back_to_box(&dev.ctx.dll);

    /* Three served MasterCycles keep the device answering. */
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

    /* The next frame flips the device into the communication-disabled state. */
    for (int j = 0; j < 3; j++) {
        mock_phy_feed_byte(frame[j]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_any(mock_phy_send, len);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_false(dev.ctx.dll.back_to_box_pending);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_COMM_DISABLED);
}

/* Main-pump fallback reply for a TYPE_1/2 frame: publish a valid process READ
 * with no ISR eager answer (as if the ISR could not pre-stage it) and check the
 * [OD][PD-in]CKS reply built by dll_send_od_reply(). */
static void test_dll_main_pump_type1_2_reply(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
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

    dev.ctx.dll.pd_in_len = 2U;
    dev.ctx.dll.pd_in[dev.ctx.dll.pd_in_active][dev.ctx.dll.od_width] = 0xAAU;
    dev.ctx.dll.pd_in[dev.ctx.dll.pd_in_active][dev.ctx.dll.od_width + 1U] = 0x55U;

    /* Publish the request into the mailbox ring (what the data ISR does). */
    iolink_frame_rec_t* rec = (iolink_frame_rec_t*) iolink_ring_claim(&dev.ctx.dll.isr_frame_ring);
    assert_non_null(rec);
    rec->len = (uint8_t) sizeof(frame);
    rec->eager = 0U; /* no ISR answer: the main pump must reply */
    (void) memcpy(rec->data, frame, sizeof(frame));
    iolink_ring_publish(&dev.ctx.dll.isr_frame_ring);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 5); /* OD 2 + PD-in 2 + CKS */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    while (dev.ctx.dll.isr_tx_active != 0U) {
        iolink_device_process_isr(&dev.ctx);
    }

    size_t tx_len = 0U;
    const uint8_t* tx = mock_phy_tx(&tx_len);
    assert_int_equal(tx_len, 5U);
    assert_int_equal(tx[0], 0x00U); /* process-channel OD reply is zero */
    assert_int_equal(tx[1], 0x00U);
    assert_int_equal(tx[2], 0xAAU);
    assert_int_equal(tx[3], 0x55U);

    /* A.1.6 checksum over [OD][PD-in] with the status-only CKS octet; the
     * compressor itself is validated exhaustively in test_crc_vectors. */
    uint8_t acc = 0x52U;
    for (size_t i = 0U; i < tx_len; i++) {
        uint8_t b = tx[i];
        if (i == (tx_len - 1U)) {
            b = (uint8_t) (b & 0xC0U); /* status only: the low 6 bits are the CKS */
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
        cmocka_unit_test_setup_teardown(test_dll_startup_0x99_rejected, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_type0_read_resets, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_type0_min_cycle_time_read, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_startup_cycle_time_reads, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_startup_operate_type_no_response, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_to_startup_0x97, test_setup,
                                        test_teardown),
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
