/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * ISR log family with the switch enabled: a configured main macro plus
 * IOLINK_LOG_ENABLE_ISR=1 must forward the ISR calls unchanged.
 */

extern void log_capture(const char* format, ...);

#define IOLINK_LOG_ENABLE_ISR 1
#define IOLINK_LOG_ERROR(...) log_capture(__VA_ARGS__)
#define IOLINK_LOG_WARN(...) log_capture(__VA_ARGS__)
#define IOLINK_LOG_INFO(...) log_capture(__VA_ARGS__)
#define IOLINK_LOG_DEBUG(...) log_capture(__VA_ARGS__)

#include "iolinki/config.h"

void test_log_isr_on_run(void)
{
    IOLINK_LOG_ISR_INFO("isr on %d", 7);
}
