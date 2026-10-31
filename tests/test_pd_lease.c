/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd_lease.c
 * @brief PD-in buffer lease: skip accounting and stale-lease recovery.
 *
 * TX BYTE ends the reply inside the library, so the main pump can tell that a
 * recorded PD-in capture is no longer being streamed. A transceiver that
 * stalls mid-frame must not freeze the PD-in updates for the rest of the
 * session.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/device.h"
#include "iolinki/dll.h"
#include "test_helpers.h"

#if !IOLINK_APP_PD_INPUT_CALLBACK

static void test_lease_recovery_and_skip_counter(void** state)
{
    (void) state;
    const iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0};
    iolink_test_device_t dev;
    const uint8_t data[2] = {0x11U, 0x22U};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);

    /* First publish: a free buffer exists, no skip. */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), 0);
    assert_int_equal(dev.ctx.dll.pd_in_skips, 0U);

    /* Model a transceiver that stalled mid-reply: the ISR's capture is still
     * recorded although no reply is in flight. */
    dev.ctx.dll.pd_in_captured = (uint8_t) (dev.ctx.dll.pd_in_active ^ 1U);
    dev.ctx.dll.isr_tx_active = 0U;

    /* No spare buffer: the update is skipped and counted, not silently lost. */
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), -1);
    assert_int_equal(dev.ctx.dll.pd_in_skips, 1U);

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    /* The main pump releases a lease that cannot be in flight any more. */
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.pd_in_captured, 0xFFU);
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true), 0);
    assert_int_equal(dev.ctx.dll.pd_in_skips, 1U);
#else
    /* TX FRAME keeps the lease: the DMA completion is only known to the PHY,
     * so the main pump must not reuse the buffer. */
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.pd_in_captured, (uint8_t) (dev.ctx.dll.pd_in_active ^ 1U));
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_lease_recovery_and_skip_counter),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}

#else /* IOLINK_APP_PD_INPUT_CALLBACK: update() is a no-op in that profile */

int main(void)
{
    return 0;
}

#endif
