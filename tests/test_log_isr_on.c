/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * ISR log family with the switch enabled: the ISR macros must behave exactly
 * like the main macros once IOLINK_LOG_ENABLE_ISR is 1 (threshold, user
 * propagation and lazy argument evaluation included).
 */

#include <assert.h>
#include <string.h>

#define IOLINK_LOG_ENABLE_ISR 1

#include "iolinki/log.h"

extern void log_capture(void* user, iolink_log_level_t level, const char* format, ...);
extern int g_calls;
extern char g_last_msg[128];
extern void* g_last_user;

static int g_side_effects;

static int side_effect(void)
{
    g_side_effects++;
    return 1;
}

void test_log_isr_on_run(void)
{
    iolink_log_sink_t sink;
    int token;

    iolink_log_sink_init(&sink, log_capture, &token, IOLINK_LOG_LEVEL_INFO);

    g_calls = 0;
    g_last_msg[0] = '\0';
    g_last_user = NULL;
    g_side_effects = 0;
    IOLINK_LOG_ISR_DEBUG(&sink, "filtered %d", side_effect());
    assert(g_calls == 0);
    assert(g_side_effects == 0);

    IOLINK_LOG_ISR_INFO(&sink, "isr on %d", 7);
    assert(g_calls == 1);
    assert(g_last_user == &token);
    assert(strcmp(g_last_msg, "isr on 7") == 0);
}
