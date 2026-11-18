/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

/* 在包含头文件之前定义自定义配置以验证覆盖（如果我们要测试覆盖
   机制）但这里我们测试的是头文件是否遵循这些定义。我们先验证默认尺寸
   与 config.h 的默认值一致。 */

#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "dll_access.h"
#include "iolinki/config.h"

static void test_config_defaults(void** state)
{
    (void) state;

    /* 验证 ISDU 缓冲区大小 */
    iolink_isdu_ctx_t isdu;
    (void) isdu;
    /* 预期：内嵌 scratch 中分别为 IOLINK_ISDU_BUFFER_SIZE（默认 256）
       的载荷/应答缓冲，请求（两槽）与成帧缓冲带协议开销。 */
#if IOLINK_WORKSPACE_EMBEDDED
    assert_true(sizeof(isdu.scratch.payload) == IOLINK_ISDU_BUFFER_SIZE);
    assert_true(sizeof(isdu.scratch.response) == IOLINK_ISDU_BUFFER_SIZE);
    assert_true(sizeof(isdu.scratch.request) == 2U * (IOLINK_ISDU_BUFFER_SIZE + 6U));
    assert_true(sizeof(isdu.scratch.frame) == IOLINK_ISDU_BUFFER_SIZE + 3U);
#endif

    /* 验证事件队列 */
    iolink_events_ctx_t events;
    /* 预期：IOLINK_EVENT_QUEUE_SIZE（默认 4）* sizeof(iolink_event_t) */
    assert_true(sizeof(events.queue) / sizeof(iolink_event_t) == IOLINK_EVENT_QUEUE_SIZE);

    /* 验证 DLL 过程数据缓冲区 */
    iolink_dll_main_t main_ctx;
    const iolink_dll_main_t* dll = &main_ctx;
    assert_true(sizeof(dll->shared.pd_in[0]) == IOLINK_PD_REPLY_MAX);
    assert_true(sizeof(dll->pd_out) == IOLINK_PD_OUT_MAX_SIZE);

    /* 验证 ISR 交接容量默认值与派生槽位数。 */
    assert_true(IOLINK_MBX_SLOTS == 4);
    assert_true(IOLINK_REPLY_PLAN_SLOTS == 48);
    assert_true(sizeof(dll->isr_frame_slot) / sizeof(dll->isr_frame_slot[0]) == IOLINK_MBX_SLOTS);
    assert_true(sizeof(dll->shared.reply_plan) / sizeof(dll->shared.reply_plan[0]) ==
                IOLINK_REPLY_PLAN_SLOTS);
    assert_true(sizeof(dll->shared.isr_tx_payload) / sizeof(dll->shared.isr_tx_payload[0]) ==
                IOLINK_TX_SLOTS);
    assert_true(IOLINK_TX_SLOTS == IOLINK_TX_ISR_SLOT + 1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_defaults),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
