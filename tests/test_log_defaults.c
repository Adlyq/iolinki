/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Default log macros: no override is defined, so every print point must
 * expand to a no-op and must not evaluate its arguments.
 */

#include <assert.h>

#include "iolinki/config.h"

static int g_side_effects;

static int side_effect(void)
{
    g_side_effects++;
    return 1;
}

void test_log_defaults_run(void)
{
    /* The macros discard their arguments, so take the address once to keep
     * the compiler from flagging the would-be side effect as unused. */
    (void) side_effect;

    IOLINK_LOG_ERROR("e %d", side_effect());
    IOLINK_LOG_WARN("w %d", side_effect());
    IOLINK_LOG_INFO("i %d", side_effect());
    IOLINK_LOG_DEBUG("d %d", side_effect());
    IOLINK_LOG_ISR_ERROR("ie %d", side_effect());
    IOLINK_LOG_ISR_WARN("iw %d", side_effect());
    IOLINK_LOG_ISR_INFO("ii %d", side_effect());
    IOLINK_LOG_ISR_DEBUG("id %d", side_effect());

    assert(g_side_effects == 0);
}
