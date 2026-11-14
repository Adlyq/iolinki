/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file transport_probe.c
 * @brief 编译期校验：套件声明的传输必须与实际编译的 ISR 粒度一致。
 *
 * `IOLINK_TEST_TRANSPORT` 只是 CMake 侧的标签，真正决定布局的是
 * `IOLINK_ISR_RX_MODE`/`IOLINK_ISR_TX_MODE`。两者不一致时（例如忘了传
 * FRAME 编译定义），测试会跑在 BYTE 库上却报告"FRAME 已测试"。这个
 * 目标只参与编译，不做任何运行时检查。
 */

#include "iolinki/config.h"

#if defined(IOLINK_TEST_EXPECT_FRAME)
#if (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_FRAME) || (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_FRAME)
#error "IOLINK_TEST_TRANSPORT=FRAME but the library is not built for FRAME rx/tx"
#endif
#elif defined(IOLINK_TEST_EXPECT_BYTE)
#if (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_BYTE) || (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_BYTE)
#error "IOLINK_TEST_TRANSPORT=BYTE but the library is not built for BYTE rx/tx"
#endif
#else
#error "IOLINK_TEST_EXPECT_<transport> is not defined"
#endif

int main(void)
{
    return 0;
}
