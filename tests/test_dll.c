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

static int test_setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
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
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
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

    /* ESTAB_COM -> OPERATE on first valid frame.
       TYPE_1_1 has OD width 0, so the READ request is MC + CKT + PD-out (3 octets). */
    uint8_t frame[3] = {0x80, IOLINK_MSEQ_TYPE_1, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3));
    for (int i = 0; i < 3; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* OD(0) + PD-in(1) + CKS */
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

static void test_dll_fallback_on_crc_errors(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
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

    /* ESTAB_COM -> OPERATE. TYPE_1_1: READ request = MC + CKT + PD-out (3 octets). */
    uint8_t ok_frame[3] = {0x80, IOLINK_MSEQ_TYPE_1, 0x00};
    ok_frame[1] = (uint8_t) (ok_frame[1] | iolink_checksum6(ok_frame, 3));
    for (int i = 0; i < 3; i++) {
        mock_phy_feed_byte(ok_frame[i]);
    }
    mock_phy_feed_chk();
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* OD(0) + PD-in(1) + CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Inject CRC errors to trigger fallback (3-octet frames for TYPE_1_1). */
    for (int r = 0; r < 3; r++) {
        uint8_t bad_frame[3] = {0x80, 0x00, 0x00};
        for (int i = 0; i < 3; i++) {
            mock_phy_feed_byte(bad_frame[i]);
        }
        mock_phy_feed_chk();
        test_io_cycle(&dev.ctx);
    }

    /* Ensure there is exactly one mock value for the final process cycle check */
    /* mock_phy_feed_chk(); // Removed: in SIO mode we don't call recv_byte */

    /* Next process call applies fallback */
    test_io_cycle(&dev.ctx);

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(iolink_device_get_baudrate(&dev.ctx), IOLINK_BAUDRATE_COM1);
}

static void test_dll_reject_transition_in_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Master sends 0x0F (Transition) while in OPERATE
     * Type 1_1 frame for pd_out_len=1 is 5 bytes: MC, CKT, PD, OD, CK */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t frame[4] = {mc, IOLINK_MSEQ_TYPE_1, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));

    for (int i = 0; i < 4; i++) {
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

/* C5/Table 47 T11: a CKT type that does not match the configured M-sequence
   type in OPERATE is an illegal M-sequence and returns the device to STARTUP. */
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

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
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
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_2, .pd_in_len = 2, .pd_out_len = 2};
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
    assert_int_equal(dev.ctx.dll.framing_errors, 1);

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

/* Feed a complete master frame to the device one octet at a time, then signal
   end-of-bytes so the DLL processes it. */
static void test_dll_feed_frame(const uint8_t* frame, size_t len)
{
    for (size_t i = 0U; i < len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
}

/* Bring the device up to PREOPERATE via wake-up + the spec Type-0 page probe. */
static void test_dll_up_to_preoperate(iolink_test_device_t* dev)
{
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev->ctx);
    usleep(200);

    uint8_t probe[2] = {0xA2U, 0x00U};
    probe[1] = (uint8_t) (probe[1] | iolink_checksum6(probe, 2U));
    test_dll_feed_frame(probe, 2U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev->ctx);
}

/* Table B.2: the extended MasterCommands (0x97/0x98/0x9A) drive the DLL state
   machine. Each is a page-channel Type-0 write answered with the CKS only. */
static void test_dll_master_commands(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    test_dll_up_to_preoperate(&dev);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 0x9A DevicePreoperate: acknowledged, remains PREOPERATE. */
    uint8_t pre[3] = {0x20U, 0x00U, IOLINK_CMD_DEVICE_PREOPERATE};
    pre[1] = (uint8_t) (pre[1] | iolink_checksum6(pre, 3U));
    test_dll_feed_frame(pre, 3U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* 0x97 DeviceStartup: -> STARTUP. */
    uint8_t stu[3] = {0x20U, 0x00U, IOLINK_CMD_DEVICE_STARTUP};
    stu[1] = (uint8_t) (stu[1] | iolink_checksum6(stu, 3U));
    test_dll_feed_frame(stu, 3U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
}

/* Table B.2/43: FALLBACK (0x5A) is a page-channel write to DPP1 address 0 and
   must arm the SIO transition from PREOPERATE (Type-0) too. */
static void test_dll_fallback_command_preoperate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    test_dll_up_to_preoperate(&dev);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* Type-0 write: MC 0x20, CKT, OD 0x5A -> CKS-only reply, FALLBACK armed. */
    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3));
    test_dll_feed_frame(frame, 3U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_FALLBACK);
}

/* Table B.2/43: FALLBACK (0x5A) inside the OPERATE Type-2 frame (PD-out before
   OD) must also arm the SIO transition. */
static void test_dll_fallback_command_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 1,
                              .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Type-2 write: MC 0x20, CKT, PD-out(1), OD 0x5A -> [PD-in]CKS reply. */
    uint8_t frame[4] = {0x20U, IOLINK_MSEQ_TYPE_2, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    test_dll_feed_frame(frame, 4U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_FALLBACK);
}

/* Table B.2: 0x99 DeviceOperate marks the process output invalid. */
static void test_dll_device_operate_clears_pd_valid(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 1,
                              .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);

    uint8_t pd[1] = {0xAAU};
    iolink_device_pd_input_update(&dev.ctx, pd, 1U, true);
    assert_true(dev.ctx.dll.pd_valid);

    uint8_t frame[4] = {0x20U, IOLINK_MSEQ_TYPE_2, 0x00U, IOLINK_CMD_DEVICE_OPERATE};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    test_dll_feed_frame(frame, 4U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_false(dev.ctx.dll.pd_valid);
}

/* 0x98 ProcessDataOutputOperate: ESTAB_COM and PD marked valid. */
static void test_dll_process_data_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    test_dll_up_to_preoperate(&dev);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    uint8_t pdo[3] = {0x20U, 0x00U, IOLINK_CMD_PROCESS_DATA_OPERATE};
    pdo[1] = (uint8_t) (pdo[1] | iolink_checksum6(pdo, 3U));
    test_dll_feed_frame(pdo, 3U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);
    assert_true(dev.ctx.dll.pd_valid);
}

/* Figure A.14: a WRITE request (MC bit 7 = 0) carries the OD octets after
   PD-out; the device reply then carries no OD. Verified by the OD side effect:
   a Type-2 page write to DPP1 0x01 must store MasterCycleTime from the OD field. */
static void test_dll_type2_write_carries_od(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 1,
                              .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* MC 0x21 = page channel, WRITE, address 0x01 (MasterCycleTime).
       Frame: MC CKT PD-out(1) OD(1) = 4 octets; OD (0x29) must reach the store. */
    uint8_t frame[4] = {0x21U, IOLINK_MSEQ_TYPE_2, 0x5AU, 0x29U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    test_dll_feed_frame(frame, 4U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* write reply: PD-in(1) + CKS, no OD */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(dev.ctx.dll.master_cycle_time, 0x29U);
}

/* Table B.2 in OPERATE: an extended MasterCommand arrives inside the cyclic
   Type-2 frame as a page-channel write to DPP1 address 0 (PD-out precedes OD).
   0x97 DeviceStartup must switch the device back to STARTUP. */
static void test_dll_master_command_in_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 1,
                              .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* MC 0x20 = page channel, WRITE, address 0. Frame: MC CKT PD-out(1) OD(1). */
    uint8_t frame[4] = {0x20U, IOLINK_MSEQ_TYPE_2, 0x00U, IOLINK_CMD_DEVICE_STARTUP};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    test_dll_feed_frame(frame, 4U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* write reply: PD-in(1) + CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
}

/* Captures the first reply octet (the OD of a READ reply) for the ISDU tests. */
static uint8_t g_isdu_reply_od;
static int test_dll_capture_od(const LargestIntegralType value,
                               const LargestIntegralType check_value_data)
{
    (void) check_value_data;
    const uint8_t* data = (const uint8_t*) value;
    g_isdu_reply_od = data[0];
    return 1;
}

/* 7.3.6.2 / Figure A.14: an acyclic ISDU READ in OPERATE keeps the negotiated
   PD (the OD uses a separate acyclic channel), so the frame is the full
   TYPE_2_V M-sequence and the device reply carries the ISDU response octet in
   the leading OD field: [OD][PD-in]CKS. The DLL must actually serve it. */
static void test_dll_operate_isdu_read_serves_od(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 2,
                              .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Prime a pending ISDU response (READ of VendorID, index 0x000A). */
    assert_int_equal(isdu_send_read_request(&dev.ctx.dll.isdu, IOLINK_IDX_VENDOR_ID, 0x00), 1);

    /* Acyclic READ: MC = READ | ISDU | FlowCTRL START; PD-out(2), no OD. */
    uint8_t frame[4] = {
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
        IOLINK_MSEQ_TYPE_2, 0x11U, 0x22U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    test_dll_feed_frame(frame, 4U);

    g_isdu_reply_od = 0x00U;
    expect_check(mock_phy_send, data, test_dll_capture_od, (void*) (uintptr_t) 0U);
    expect_value(mock_phy_send, len, 4); /* [OD][PD-in(2)]CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* OD = ISDU READ-RESPONSE (+) I-Service (0xD0 | length), i.e. not "No
       Service" (0x00) - proof the DLL served the response. */
    assert_int_equal((g_isdu_reply_od & 0xF0U), 0xD0U);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* 7.3.6.2: an acyclic ISDU WRITE in OPERATE carries the request octet in the
   trailing OD field and is answered by [PD-in]CKS (no OD). */
static void test_dll_operate_isdu_write_dispatched(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 2,
                              .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Acyclic WRITE: MC = WRITE | ISDU | FlowCTRL START; PD-out(2) + OD(1). */
    uint8_t frame[5] = {(uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
                        IOLINK_MSEQ_TYPE_2, 0x11U, 0x22U,
                        (uint8_t) (0x90U | 0x03U) /* I-Service READ, Length 3 */};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));
    test_dll_feed_frame(frame, 5U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3); /* write reply: PD-in(2) + CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* The OD octet reached the ISDU engine: WRITE START accepted. */
    assert_true(dev.ctx.dll.isdu.flowctrl_seen);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Regression: the cyclic frame (ISDU channel + FlowCTRL IDLE) keeps its full
   PD width and answers No Service (0x00) in the OD - it must NOT be misread as
   a short acyclic frame. */
static void test_dll_operate_cyclic_idle_od_zero(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 2,
                              .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[4] = {
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE),
        IOLINK_MSEQ_TYPE_2, 0x11U, 0x22U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    test_dll_feed_frame(frame, 4U);
    g_isdu_reply_od = 0xFFU;
    expect_check(mock_phy_send, data, test_dll_capture_od, (void*) (uintptr_t) 0U);
    expect_value(mock_phy_send, len, 4); /* [OD][PD-in(2)]CKS */
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(g_isdu_reply_od, 0x00U);
}

/* Table B.1: a write to DPP1 0x01 (MasterCycleTime) is stored, not discarded. */
static void test_dll_master_cycle_time_stored(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    test_dll_up_to_preoperate(&dev);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* MC 0x21 = write | page channel | address 0x01; OD = 0x29 (4.1 ms, tb=00). */
    uint8_t wr[3] = {0x21U, 0x00U, 0x29U};
    wr[1] = (uint8_t) (wr[1] | iolink_checksum6(wr, 3U));
    test_dll_feed_frame(wr, 3U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);
    assert_int_equal(dev.ctx.dll.master_cycle_time, 0x29U);
}

/* A corrupt request checksum must never be answered. For a pre-cached
 * (TYPE_2_V) device the process channel is answered eagerly from the ISR, so
 * the checksum/type gate must drop the frame there (no transmit at all). */
static void test_dll_bad_cks_process_no_reply(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 1,
                              .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    mock_phy_clear_tx();

    /* Process-channel READ with a deliberately corrupted CKT checksum. */
    uint8_t frame[3] = {0x80U, IOLINK_MSEQ_TYPE_2, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3));
    frame[1] = (uint8_t) (frame[1] ^ 0x01U); /* corrupt the checksum bits */
    for (int i = 0; i < 3; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);

    size_t len = 0U;
    (void) mock_phy_tx(&len);
    assert_int_equal(len, 0U); /* must not answer a corrupt frame */
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* Same guarantee for a non-cyclic channel (answered by the main pump): a bad
 * checksum is rejected there and no reply is transmitted. */
static void test_dll_bad_cks_acyclic_no_reply(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                              .od_width = 1,
                              .pd_in_len = 1,
                              .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    mock_phy_clear_tx();

    /* Acyclic ISDU READ with a deliberately corrupted CKT checksum. */
    uint8_t frame[3] = {
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
        IOLINK_MSEQ_TYPE_2, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3));
    frame[1] = (uint8_t) (frame[1] ^ 0x01U);
    for (int i = 0; i < 3; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);

    size_t len = 0U;
    (void) mock_phy_tx(&len);
    assert_int_equal(len, 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_dll_receive_error_discards_partial_frame, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_master_commands, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_fallback_command_preoperate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_fallback_command_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_device_operate_clears_pd_valid, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_process_data_operate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_type2_write_carries_od, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_master_command_in_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_isdu_read_serves_od, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_isdu_write_dispatched, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_operate_cyclic_idle_od_zero, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_bad_cks_process_no_reply, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_bad_cks_acyclic_no_reply, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_master_cycle_time_stored, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_wakeup_to_preoperate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_to_operate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_fallback_on_crc_errors, test_setup, test_teardown),
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
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
