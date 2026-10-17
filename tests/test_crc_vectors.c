/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_crc_vectors.c
 * @brief A.1.6 checksum: known compression vectors and the streaming
 *        (accumulator) equivalence used by the ISR transmit path.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/crc.h"

/* Compress an 8-bit accumulator with the A.1 equations, independently of the
 * implementation under test (Table A.1). */
static uint8_t reference_compress(uint8_t acc)
{
    const uint8_t d7 = (uint8_t) ((acc >> 7U) & 1U);
    const uint8_t d6 = (uint8_t) ((acc >> 6U) & 1U);
    const uint8_t d5 = (uint8_t) ((acc >> 5U) & 1U);
    const uint8_t d4 = (uint8_t) ((acc >> 4U) & 1U);
    const uint8_t d3 = (uint8_t) ((acc >> 3U) & 1U);
    const uint8_t d2 = (uint8_t) ((acc >> 2U) & 1U);
    const uint8_t d1 = (uint8_t) ((acc >> 1U) & 1U);
    const uint8_t d0 = (uint8_t) (acc & 1U);

    uint8_t out = 0U;
    out |= (uint8_t) ((d7 ^ d5 ^ d3 ^ d1) << 5U);
    out |= (uint8_t) ((d6 ^ d4 ^ d2 ^ d0) << 4U);
    out |= (uint8_t) ((d7 ^ d6) << 3U);
    out |= (uint8_t) ((d5 ^ d4) << 2U);
    out |= (uint8_t) ((d3 ^ d2) << 1U);
    out |= (uint8_t) (d1 ^ d0);
    return out;
}

static void test_checksum6_from_acc_vectors(void** state)
{
    (void) state;

    /* Exhaustively compare the streaming compressor against the A.1 equations. */
    for (uint16_t acc = 0U; acc <= 0xFFU; acc++) {
        assert_int_equal(iolink_checksum6_from_acc((uint8_t) acc),
                         reference_compress((uint8_t) acc));
    }

    /* A few explicit values read off the equations. */
    assert_int_equal(iolink_checksum6_from_acc(0x52U), 0x2DU); /* seed only */
    assert_int_equal(iolink_checksum6_from_acc(0x00U), 0x00U);
    assert_int_equal(iolink_checksum6_from_acc(0xFFU), 0x00U);
    assert_int_equal(iolink_checksum6_from_acc(0x01U), 0x11U);
    assert_int_equal(iolink_checksum6_from_acc(0x80U), 0x28U);
}

static void test_checksum6_from_acc_matches_stream(void** state)
{
    (void) state;

    const uint8_t frame1[] = {0x00U, 0x00U};
    const uint8_t frame2[] = {0xA2U, 0x00U};
    const uint8_t frame3[] = {0x20U, 0x00U, 0x99U};
    const uint8_t frame4[] = {0x00U, 0x40U, 0xA5U, 0x5AU};
    const uint8_t frame5[] = {0x80U, 0x80U};
    const uint8_t frame6[] = {0x10U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U};

    const uint8_t* frames[] = {frame1, frame2, frame3, frame4, frame5, frame6};
    const size_t lens[] = {sizeof(frame1), sizeof(frame2), sizeof(frame3),
                           sizeof(frame4), sizeof(frame5), sizeof(frame6)};

    for (size_t f = 0U; f < (sizeof(frames) / sizeof(frames[0])); f++) {
        uint8_t acc = 0x52U;
        for (size_t i = 0U; i < lens[f]; i++) {
            acc = (uint8_t) (acc ^ frames[f][i]);
        }
        assert_int_equal(iolink_checksum6_from_acc(acc), iolink_checksum6(frames[f], lens[f]));
    }
}

static void test_checksum6_empty_message(void** state)
{
    (void) state;

    /* A NULL buffer with zero length is valid; the checksum is the seed folded
     * once through the compressor. */
    assert_int_equal(iolink_checksum6(NULL, 0U), 0x2DU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_checksum6_from_acc_vectors),
        cmocka_unit_test(test_checksum6_from_acc_matches_stream),
        cmocka_unit_test(test_checksum6_empty_message),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
