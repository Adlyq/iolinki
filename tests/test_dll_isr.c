/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll_isr.c
 * @brief 纯 ISR 侧组帧辅助函数的单元测试（Figure A.5/A.9/A.14）。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/dll_ctx.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

#include "conformance_test.h"

static void init_dll(iolink_dll_ctx_t* dll, iolink_m_seq_type_t type, uint8_t od_width,
                     uint8_t pd_in, uint8_t pd_out)
{
    /* 这些辅助函数是纯函数/只读的：不会产生 PHY 通信，因此仅需一个绑定了 PHY
     * 的空上下文即可（避免 CMocka 的期望设置）。 */
    (void) memset(dll, 0, sizeof(*dll));
    dll->main.shared.phy = &g_phy_mock;
    dll->main.shared.m_seq_type = (uint8_t) type;
    dll->main.shared.od_width = od_width;
    dll->main.pd_in_len_current = pd_in;
    dll->main.shared.pd_out_len_current = pd_out;
}

/* Type 0（STARTUP/PREOPERATE）请求长度：Figure A.5。 */
static void test_expected_req_len_type0(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    init_dll(&dll, IOLINK_M_SEQ_TYPE_0, 1U, 0U, 0U);
    dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;

    /* 读取和过程通道访问仅使用 MC + CKT 报头。 */
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x80U), 2U);
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x00U), 2U);

    /* Type-0 在 page/diagnosis/ISDU 通道上的 WRITE 恰好携带一个 OD 字节。 */
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x20U), 3U);
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x40U), 3U);
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x60U), 3U);
}

/* 多字节 OPERATE 请求长度：MC + CKT + PD-out + 写入时的 OD。 */
static void test_expected_req_len_operate(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    init_dll(&dll, IOLINK_M_SEQ_TYPE_1_2, 2U, 2U, 2U);
    dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;

    /* READ（MC bit 7 = 1）：报头 + PD-out，无 OD。 */
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x80U), 4U);
    /* WRITE 过程通道：报头 + PD-out + OD。 */
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x00U), 6U);
    /* ISDU 通道上的 READ：无 OD。 */
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0xE0U), 4U);
    /* ISDU 通道上的 WRITE：存在 OD。 */
    assert_int_equal(iolink_dll_expected_req_len(&dll.main, 0x60U), 6U);

    iolink_dll_ctx_t dll2;
    init_dll(&dll2, IOLINK_M_SEQ_TYPE_2_2, 1U, 0U, 3U);
    dll2.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    assert_int_equal(iolink_dll_expected_req_len(&dll2.main, 0x80U), 5U);
    assert_int_equal(iolink_dll_expected_req_len(&dll2.main, 0x00U), 6U);

    /* NULL 上下文是安全的。 */
    assert_int_equal(iolink_dll_expected_req_len(NULL, 0x80U), 0U);
}

/* 未调用 iolink_dll_init() 时 shared.isr 为 NULL；所有 ISR 入口必须安全返回，
 * 而不是在中断上下文里解引用空指针。 */
static void test_isr_entries_without_binding_are_safe(void** state)
{
    (void) state;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.main.shared.phy = &g_phy_mock;

    iolink_dll_process_isr(&dll.main);
    iolink_dll_on_rx_timeout(&dll.main);
    iolink_dll_on_stack_tick(&dll.main);
    assert_int_equal(iolink_dll_take_service_window(&dll.main), 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_expected_req_len_type0),
        cmocka_unit_test(test_expected_req_len_operate),
        cmocka_unit_test(test_isr_entries_without_binding_are_safe),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
