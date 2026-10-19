/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll_isr.c
 * @brief Unit tests for the pure ISR-side framing helpers and the pre-staged
 *        cyclic reply variants (Figure A.5/A.9/A.14).
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/dll.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static void init_dll(iolink_dll_ctx_t* dll, iolink_m_seq_type_t type, uint8_t od_width,
                     uint8_t pd_in, uint8_t pd_out)
{
    /* These helpers are pure/read-only: no PHY traffic is generated, so a bare
     * context with a bound PHY is enough (avoids CMocka expectations). */
    (void) memset(dll, 0, sizeof(*dll));
    dll->phy = &g_phy_mock;
    dll->m_seq_type = (uint8_t) type;
    dll->od_width = od_width;
    dll->pd_in_len_current = pd_in;
    dll->pd_out_len_current = pd_out;
}

/* Type 0 (STARTUP/PREOPERATE) request lengths: Figure A.5. */
static void test_expected_req_len_type0(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    init_dll(&dll, IOLINK_M_SEQ_TYPE_0, 1U, 0U, 0U);
    dll.state = IOLINK_DLL_STATE_PREOPERATE;

    /* Reads and process-channel access are the bare MC + CKT header. */
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x80U), 2U);
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x00U), 2U);

    /* A Type-0 WRITE on page/diagnosis/ISDU carries exactly one OD octet. */
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x20U), 3U);
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x40U), 3U);
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x60U), 3U);
}

/* Multi-octet OPERATE request lengths: MC + CKT + PD-out + OD-on-write. */
static void test_expected_req_len_operate(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    init_dll(&dll, IOLINK_M_SEQ_TYPE_1_2, 2U, 2U, 2U);
    dll.state = IOLINK_DLL_STATE_OPERATE;

    /* READ (MC bit 7 = 1): header + PD-out, no OD. */
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x80U), 4U);
    /* WRITE process channel: header + PD-out + OD. */
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x00U), 6U);
    /* READ on the ISDU channel: no OD. */
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0xE0U), 4U);
    /* WRITE on the ISDU channel: OD present. */
    assert_int_equal(iolink_dll_expected_req_len(&dll, 0x60U), 6U);

    iolink_dll_ctx_t dll2;
    init_dll(&dll2, IOLINK_M_SEQ_TYPE_2_2, 1U, 0U, 3U);
    dll2.state = IOLINK_DLL_STATE_OPERATE;
    assert_int_equal(iolink_dll_expected_req_len(&dll2, 0x80U), 5U);
    assert_int_equal(iolink_dll_expected_req_len(&dll2, 0x00U), 6U);

    /* NULL context is safe. */
    assert_int_equal(iolink_dll_expected_req_len(NULL, 0x80U), 0U);
}

/* Reply-variant selection (A.14): process vs acyclic OD, read vs write. */
static void test_reply_variant_selection(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    init_dll(&dll, IOLINK_M_SEQ_TYPE_0, 1U, 0U, 0U);
    dll.state = IOLINK_DLL_STATE_PREOPERATE;
    assert_int_equal(iolink_dll_reply_variant(&dll, 0x80U), IOLINK_REPLY_TYPE0_OD);
    assert_int_equal(iolink_dll_reply_variant(&dll, 0x00U), IOLINK_REPLY_TYPE0_CKS);

    iolink_dll_ctx_t op;
    init_dll(&op, IOLINK_M_SEQ_TYPE_1_2, 2U, 2U, 2U);
    op.state = IOLINK_DLL_STATE_OPERATE;

    /* Process channel. */
    assert_int_equal(iolink_dll_reply_variant(&op, 0x80U), IOLINK_REPLY_CYCLIC_READ);
    assert_int_equal(iolink_dll_reply_variant(&op, 0x00U), IOLINK_REPLY_CYCLIC_WRITE);
    /* Acyclic OD channels: READ serves the staged OD response, WRITE has no OD. */
    assert_int_equal(iolink_dll_reply_variant(&op, 0xE0U), IOLINK_REPLY_ACYCLIC_READ);
    assert_int_equal(iolink_dll_reply_variant(&op, 0x60U), IOLINK_REPLY_CYCLIC_WRITE);
    assert_int_equal(iolink_dll_reply_variant(&op, 0xA0U), IOLINK_REPLY_ACYCLIC_READ);
    assert_int_equal(iolink_dll_reply_variant(&op, 0xC0U), IOLINK_REPLY_ACYCLIC_READ);

    assert_int_equal(iolink_dll_reply_variant(NULL, 0x80U), IOLINK_REPLY_NONE);
}

/* The old pre-staged double buffer is gone: replies are generated directly
 * from the shared state by the ISR. The public helper stays as a safe no-op. */
static void test_prepare_cyclic_reply_is_noop(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    init_dll(&dll, IOLINK_M_SEQ_TYPE_2_V, 2U, 2U, 2U);
    dll.cyclic_reply_precached = true;
    iolink_dll_prepare_cyclic_reply(&dll);
    iolink_dll_prepare_cyclic_reply(NULL); /* NULL-safe */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_expected_req_len_type0),
        cmocka_unit_test(test_expected_req_len_operate),
        cmocka_unit_test(test_reply_variant_selection),
        cmocka_unit_test(test_prepare_cyclic_reply_is_noop),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
