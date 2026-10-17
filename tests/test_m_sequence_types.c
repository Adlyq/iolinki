/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_m_sequence_types.c
 * @brief Unit tests for M-sequence Types 1_1, 1_2, 2_1, 2_2
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

/* Figures A.9/A.10/A.13/A.14: the Device reply to a READ M-sequence is
   [OD...][PD-in...] CKS and the reply to a WRITE M-sequence is [PD-in...] CKS
   with no OD. Both tests feed the same request layout (MC, CKT, PD-out, OD) and
   assert the reply bytes exactly. */
static void test_m_seq_type_1_1_read_reply_od_first(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 2, .pd_out_len = 2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    move_to_operate_ctx(&dev.ctx);

    uint8_t input_pd[2] = {0xAA, 0xBB};
    iolink_device_pd_input_update(&dev.ctx, input_pd, 2, true);

    /* Type 1_1 with 2-byte PD: Req = MC, CKT, PD(2), OD(1), CK = 6 bytes.
       The CKT carries the Type-1 bits (A.1.3). MC bit 7 = READ access. */
    uint8_t frame[] = {0x80, IOLINK_MSEQ_TYPE_1, 0x11, 0x22, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    for (int i = 0; i < 5; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* Resp = OD(1), PD(2), CKS = 4 bytes. The process channel read serves no
       OD octet (0x00), so the reply is 0x00 0xAA 0xBB 0x28 (A.1.6). */
    uint8_t expected[] = {0x00, 0xAA, 0xBB, 0x28};
    expect_memory(mock_phy_send, data, expected, sizeof(expected));
    expect_value(mock_phy_send, len, sizeof(expected));
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
}

static void test_m_seq_type_1_1_write_reply_no_od(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 2, .pd_out_len = 2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    move_to_operate_ctx(&dev.ctx);

    uint8_t input_pd[2] = {0xAA, 0xBB};
    iolink_device_pd_input_update(&dev.ctx, input_pd, 2, true);

    /* MC bit 7 = 0: a WRITE M-sequence. The reply carries no OD. */
    uint8_t frame[] = {0x00, IOLINK_MSEQ_TYPE_1, 0x11, 0x22, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    for (int i = 0; i < 5; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* Resp = PD(2), CKS = 3 bytes: 0xAA 0xBB 0x28 (A.1.6). */
    uint8_t expected[] = {0xAA, 0xBB, 0x28};
    expect_memory(mock_phy_send, data, expected, sizeof(expected));
    expect_value(mock_phy_send, len, sizeof(expected));
    will_return(mock_phy_send, 0);

    test_io_cycle(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_m_seq_type_1_1_read_reply_od_first),
        cmocka_unit_test(test_m_seq_type_1_1_write_reply_no_od),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
