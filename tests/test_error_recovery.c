/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_error_recovery.c
 * @brief Unit tests for error detection and recovery
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/events.h"
#include "iolinki/dll.h"
#include "test_helpers.h"

static void test_crc_error_recovery(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_to_operate_ctx(&dev.ctx);

    /* 2. Simulate 3 CRC errors (Type 1_1 with 1-byte PD: MC, CKT, PD(1), OD(1), CK = 5 bytes) */
    for (int r = 0; r < 3; r++) {
        /* Provide the whole frame in the mock queue */
        mock_phy_feed_byte(0x00); /* MC (write) */
        mock_phy_feed_byte(IOLINK_MSEQ_TYPE_2); /* CKT with a bad checksum */
        mock_phy_feed_byte(0x00); /* PD-out */
        mock_phy_feed_byte(0x00); /* OD0 */
        mock_phy_feed_byte(0x00); /* OD1 */
        mock_phy_feed_chk();    /* End frame */

        /* One call to process the whole available frame */
        test_io_cycle(&dev.ctx);
    }

    /* 3. Check if error was detected (Event 0x5000 triggered after 3 retries) */
    assert_true(iolink_events_pending(iolink_device_get_events_ctx(&dev.ctx)));
}

static void test_communication_timeout(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);

    /* Trigger wakeup to move from SIO to SDCI */
    iolink_phy_mock_set_wakeup(1);
    test_io_cycle(&dev.ctx);
    iolink_phy_mock_set_wakeup(0);

    /* Ensure no data available in SDCI mode */
    mock_phy_feed_chk();
    test_io_cycle(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc_error_recovery),
        cmocka_unit_test(test_communication_timeout),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
