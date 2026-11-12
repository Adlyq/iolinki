/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * ISR log family with the switch enabled: the ISR macros must forward to the
 * context sink exactly like the main macros once IOLINK_LOG_ENABLE_ISR is 1.
 */

#define IOLINK_LOG_ENABLE_ISR 1

#include "iolinki/log.h"

extern void log_capture(void* user, iolink_log_level_t level, const char* format, ...);

void test_log_isr_on_run(void)
{
    iolink_log_sink_t sink;
    iolink_log_sink_init(&sink, log_capture, NULL, IOLINK_LOG_LEVEL_DEBUG);
    IOLINK_LOG_ISR_INFO(&sink, "isr on %d", 7);
}
