/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_m_sequence_types.c
 * @brief ISR-transport conformance for the fixed Annex A M-sequence types.
 *
 * Only the opt-in ISR transport implements the per-type frame layouts of
 * Figures A.7-A.13; the legacy streaming transport keeps upstream's single
 * generic layout. The byte-exact requests/replies below are the oracle.
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

#if IOLINK_PHY_FEATURE_ISR_PUMP

typedef struct
{
    iolink_m_seq_type_t type;
    uint8_t od;
    uint8_t pd_in;
    uint8_t pd_out;
} fixed_case_t;

/* Figures A.7 (1_2), A.8 (1_V), A.9 (2_1), A.10 (2_2), A.11 (2_3),
 * A.12 (2_4) and A.13 (2_5). */
static const fixed_case_t g_cases[] = {
    {IOLINK_M_SEQ_TYPE_1_2, 2U, 0U, 0U},
    {IOLINK_M_SEQ_TYPE_1_V, 8U, 0U, 0U},
    {IOLINK_M_SEQ_TYPE_1_V, 32U, 0U, 0U},
    {IOLINK_M_SEQ_TYPE_2_1, 1U, 1U, 0U},
    {IOLINK_M_SEQ_TYPE_2_2, 1U, 2U, 0U},
    {IOLINK_M_SEQ_TYPE_2_3, 1U, 0U, 1U},
    {IOLINK_M_SEQ_TYPE_2_4, 1U, 0U, 2U},
    {IOLINK_M_SEQ_TYPE_2_5, 1U, 1U, 1U},
};

static uint8_t case_type_bits(iolink_m_seq_type_t type)
{
    switch (type) {
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            return IOLINK_MSEQ_TYPE_1;
        default:
            return IOLINK_MSEQ_TYPE_2;
    }
}

static void feed_request(uint8_t* frame, size_t len, uint8_t type_bits)
{
    /* The CKT type bits are part of the checksummed message. */
    frame[1] = type_bits;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
    for (size_t i = 0U; i < len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
}

static void run_fixed_case(const fixed_case_t* c)
{
    iolink_config_t config = {.m_seq_type = c->type,
                              .pd_in_len = c->pd_in,
                              .pd_out_len = c->pd_out,
                              .od_width = c->od};
    iolink_test_device_t dev;
    const uint8_t type_bits = case_type_bits(c->type);
    uint8_t pd_in[IOLINK_PD_IN_MAX_SIZE] = {0};
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE] = {0};
    uint8_t frame[2U + IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE];
    uint8_t expected[IOLINK_OD_MAX_SIZE + IOLINK_PD_IN_MAX_SIZE + 1U];
    uint8_t readback[IOLINK_PD_OUT_MAX_SIZE];

    mock_phy_clear_rx();
    mock_phy_clear_tx();
    iolink_phy_mock_reset();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    for (uint8_t i = 0U; i < c->pd_in; i++) {
        pd_in[i] = (uint8_t) (0x80U + i);
    }
    for (uint8_t i = 0U; i < c->pd_out; i++) {
        pd_out[i] = (uint8_t) (0x10U + i);
    }
    assert_int_equal(iolink_device_pd_input_update(&dev.ctx, pd_in, c->pd_in, true), 0);

    /* --- READ: request = MC, CKT, PD-out (no OD); reply = OD, PD-in, CKS. */
    (void) memset(frame, 0, sizeof(frame));
    const size_t read_len = 2U + c->pd_out;
    frame[0] = 0x80U;
    (void) memcpy(&frame[2], pd_out, c->pd_out);
    feed_request(frame, read_len, type_bits);

    (void) memset(expected, 0, sizeof(expected));
    size_t pos = 0U;
    pos += c->od; /* process-channel OD octets read as zero */
    (void) memcpy(&expected[pos], pd_in, c->pd_in);
    pos += c->pd_in;
    expected[pos] = iolink_checksum6(expected, pos);
    pos++;

    expect_memory(mock_phy_send, data, expected, pos);
    expect_value(mock_phy_send, len, pos);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    /* --- WRITE: request = MC, CKT, PD-out, OD; reply = PD-in, CKS. */
    (void) memset(frame, 0, sizeof(frame));
    const size_t write_len = 2U + c->pd_out + c->od;
    frame[0] = 0x00U;
    (void) memcpy(&frame[2], pd_out, c->pd_out);
    feed_request(frame, write_len, type_bits);

    (void) memset(expected, 0, sizeof(expected));
    (void) memcpy(expected, pd_in, c->pd_in);
    expected[c->pd_in] = iolink_checksum6(expected, c->pd_in);

    expect_memory(mock_phy_send, data, expected, (size_t) c->pd_in + 1U);
    expect_value(mock_phy_send, len, (size_t) c->pd_in + 1U);
    will_return(mock_phy_send, 0);
    test_io_cycle(&dev.ctx);

    if (c->pd_out > 0U) {
        assert_int_equal(iolink_device_pd_output_read(&dev.ctx, readback, c->pd_out), c->pd_out);
        assert_memory_equal(readback, pd_out, c->pd_out);
    }
}

static void test_fixed_types_byte_exact(void** state)
{
    (void) state;
    setup_mock_phy();
    for (size_t i = 0U; i < (sizeof(g_cases) / sizeof(g_cases[0])); i++) {
        run_fixed_case(&g_cases[i]);
    }
}

static void test_type_1_1_rejected(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 2, .pd_out_len = 0};
    iolink_test_device_t dev;

    /* Validation rejects the config before the PHY is touched. */
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL),
                     IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE);
}

static void test_fixed_layout_mismatch_rejected(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    /* TYPE_2_2 needs PD-in=2 and PD-out=0. */
    {
        iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_2,
                                  .pd_in_len = 2,
                                  .pd_out_len = 2};
        assert_int_equal(iolink_test_device_init(&dev, &config, NULL),
                         IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT);
    }
    /* TYPE_1_V is OD-only (8/32), not a variable-PD type. */
    {
        iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_V,
                                  .pd_in_len = 8,
                                  .pd_out_len = 8};
        assert_int_equal(iolink_test_device_init(&dev, &config, NULL),
                         IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT);
    }
}

#else /* !IOLINK_PHY_FEATURE_ISR_PUMP */

/* The legacy streaming transport keeps upstream's generic layout and is
 * intentionally not exercised here. */

#endif

int main(void)
{
#if IOLINK_PHY_FEATURE_ISR_PUMP
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_fixed_types_byte_exact),
        cmocka_unit_test(test_type_1_1_rejected),
        cmocka_unit_test(test_fixed_layout_mismatch_rejected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
#else
    return 0;
#endif
}
