/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_profile_abi.c
 * @brief 链接期档位断言的正向用例（P6）。
 *
 * `IOLINK_PROFILE_ANCHOR` 引用应用侧期望的全部档位符号。库以同一组
 * 开关编译时链接成功；档位不一致时链接失败，错误信息直接给出缺失
 * 符号的方向（负向探针见 tools/check_profile_abi.sh）。
 */

#include <assert.h>

#include "iolinki/capability.h"
#include "iolinki/profile.h"

IOLINK_PROFILE_ANCHOR(iolink_test_profile_anchor);

int main(void)
{
    const iolink_build_caps_t* build = iolink_caps_build();

    /* 锚点符号已解析（否则不会链接）；同时校验档位字段本身。 */
    assert(iolink_test_profile_anchor[0] != NULL);
    assert(build->abi == IOLINK_CAP_ABI);
    assert(build->reserved == 0U);
    assert(build->built != 0U);
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    assert(build->isr_rx == IOLINK_CAP_ISR_RX_FRAME);
#else
    assert(build->isr_rx == IOLINK_CAP_ISR_RX_BYTE);
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    assert(build->isr_tx == IOLINK_CAP_ISR_TX_FRAME);
#else
    assert(build->isr_tx == IOLINK_CAP_ISR_TX_BYTE);
#endif
    return 0;
}
