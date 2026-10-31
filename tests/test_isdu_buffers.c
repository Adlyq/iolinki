/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_buffers.c
 * @brief The framed ISDU response never overwrites the published buffer.
 *
 * The engine builds into resp_store[resp_sel]; the data ISR serves
 * resp_store[resp_pub]. A new START may only alternate the build buffer once
 * the previous response has been published, so back-to-back requests cannot
 * write the buffer the ISR is still allowed to read.
 */

#include <assert.h>
#include <stdint.h>

#include "iolinki/dll.h"
#include "iolinki/isdu.h"

/* 8-bit Index read request (I-Service 0x9, index 0x10) with its CHKPDU. */
static void send_start(iolink_isdu_ctx_t* isdu)
{
    uint8_t req[3] = {0x93U, 0x10U, 0x00U};
    req[2] = (uint8_t) (req[0] ^ req[1]);
    iolink_isdu_od_write(isdu, IOLINK_FLOWCTRL_START, req, sizeof(req));
}

int main(void)
{
    iolink_isdu_ctx_t isdu;
    iolink_isdu_init(&isdu);
    assert(isdu.resp_sel == isdu.resp_pub);

    /* First request: build in the other buffer. */
    send_start(&isdu);
    assert(isdu.resp_sel != isdu.resp_pub);
    const uint8_t published = isdu.resp_pub;

    /* Second START before the main pump published the first response must not
     * alternate again: that would overwrite resp_store[resp_pub]. */
    isdu.resp_store[published][0] = 0xA5U; /* sentinel: the published copy */
    send_start(&isdu);
    assert(isdu.resp_sel != isdu.resp_pub);
    assert(isdu.resp_store[published][0] == 0xA5U);

    /* Once published, the next request alternates to the other buffer. */
    isdu.resp_pub = isdu.resp_sel;
    const uint8_t previous = isdu.resp_sel;
    send_start(&isdu);
    assert(isdu.resp_sel != previous);

    return 0;
}
