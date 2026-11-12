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
    /* 锚点数组的第 0 项非 NULL 即证明符号解析成功。 */
    assert(iolink_test_profile_anchor[0] != NULL);

    const iolink_build_caps_t* build = iolink_caps_build();
    assert(build->abi == IOLINK_CAP_ABI);
    return 0;
}
