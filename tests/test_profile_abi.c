/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_profile_abi.c
 * @brief 链接期档位断言的正向用例。
 *
 * `IOLINK_PROFILE_ANCHOR` 引用应用侧期望的全部档位符号。库以同一组
 * 开关编译时链接成功；档位不一致时链接失败，错误信息直接给出缺失
 * 符号的方向（负向探针见 tools/check_profile_abi.sh）。
 */

#include <cmocka.h>

#include "iolinki/capability.h"
#include "iolinki/profile.h"

IOLINK_PROFILE_ANCHOR(iolink_test_profile_anchor);

static void test_build_caps_match_compiled_profile(void** state)
{
    (void) state;
    const iolink_build_caps_t* build = iolink_caps_build();

    /* 锚点数组的初始化即链接期引用：符号缺失时不会链接成功。这里只校验
     * 档位字段本身，避免同义反复的断言。 */
    (void) iolink_test_profile_anchor;
    assert_int_equal(build->reserved, 0U);
    assert_int_not_equal(build->built, 0U);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    assert_int_equal(build->isr_rx, IOLINK_CAP_ISR_RX_FRAME);
#else
    assert_int_equal(build->isr_rx, IOLINK_CAP_ISR_RX_BYTE);
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    assert_int_equal(build->isr_tx, IOLINK_CAP_ISR_TX_FRAME);
#else
    assert_int_equal(build->isr_tx, IOLINK_CAP_ISR_TX_BYTE);
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_build_caps_match_compiled_profile),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
