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
#include "iolinki/dll_ctx.h"
#include "iolinki/config.h"

static void test_config_defaults(void** state)
{
    (void) state;

    /* 验证 ISDU 缓冲区大小 */
    iolink_isdu_ctx_t isdu;
    /* 预期：2 个 IOLINK_ISDU_BUFFER_SIZE（默认 256）缓冲区 + 额外开销 */
    assert_true(sizeof(isdu.buffer) == IOLINK_ISDU_BUFFER_SIZE);
    assert_true(sizeof(isdu.response_buf) == IOLINK_ISDU_BUFFER_SIZE);

    /* 验证事件队列 */
    iolink_events_ctx_t events;
    /* 预期：IOLINK_EVENT_QUEUE_SIZE（默认 4）* sizeof(iolink_event_t) */
    assert_true(sizeof(events.queue) / sizeof(iolink_event_t) == IOLINK_EVENT_QUEUE_SIZE);

    /* 验证 DLL 过程数据缓冲区 */
    iolink_dll_ctx_t dll;
    assert_true(sizeof(dll.main.shared.pd_in[0]) == IOLINK_PD_REPLY_MAX);
    assert_true(sizeof(dll.main.pd_out) == IOLINK_PD_OUT_MAX_SIZE);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_defaults),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
