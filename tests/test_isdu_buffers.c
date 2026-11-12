/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_buffers.c
 * @brief 分段 ISDU 应答绝不覆盖已发布的缓冲区。
 *
 * 引擎构建到 resp_store[resp_sel]；数据 ISR 服务
 * resp_store[resp_pub]。新的 START 只能在前一个应答已发布后切换
 * 构建缓冲区，因此连续不断的请求不能
 * 写入 ISR 仍被允许读取的缓冲区。
 */

#include <assert.h>
#include <stdint.h>

#include "iolinki/dll.h"
#include "iolinki/isdu.h"

/* 8 位索引读请求（I-Service 0x9，索引 0x10）及其 CHKPDU。 */
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

    /* 第一个请求：在另一个缓冲区中构建。 */
    send_start(&isdu);
    assert(isdu.resp_sel != isdu.resp_pub);
    const uint8_t published = isdu.resp_pub;

    /* 在主循环泵发布第一个应答之前的第二个 START 不得
     * 再次切换：那会覆盖 resp_store[resp_pub]。 */
    isdu.resp_store[published][0] = 0xA5U; /* 哨兵：已发布的那份副本 */
    send_start(&isdu);
    assert(isdu.resp_sel != isdu.resp_pub);
    assert(isdu.resp_store[published][0] == 0xA5U);

    /* 一旦发布，下一个请求切换到另一个缓冲区。 */
    isdu.resp_pub = isdu.resp_sel;
    const uint8_t previous = isdu.resp_sel;
    send_start(&isdu);
    assert(isdu.resp_sel != previous);

    return 0;
}
