/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_slots.c
 * @brief ISDU 写入口的双请求槽与发布字：主泵执行期间 ISR 继续收请求。
 *
 * 切点用应用侧 `on_change` 钩子构造：它运行在 `iolink_isdu_process()` 的
 * 服务分发内部（主泵已认领并解析完该槽、ack 之后）。钩子里直接调用
 * `iolink_isdu_od_write()`，与"数据 ISR 抢占主泵"的时序等价。解析期间的
 * 抢占（`req_busy_sel` 保护窗口）没有回调可插入，因此这里不构造；该窗口
 * 由选槽规则本身保证（ISR 绝不写 busy 槽）。
 *
 * 覆盖：
 *  - 主泵执行槽 A 期间 ISR 收齐槽 B：A 的缓冲不被驱逐，B 随后执行；
 *  - A 已发布但未被接管时又来 START：计 `req_overruns`，被覆盖的请求
 *    绝不执行；
 *  - ack 前进后槽位重新可用：轮换到旧槽，不计数。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/isdu.h"
#include "iolinki/od.h"
#include "iolinki/protocol.h"

/* 被测引擎：内嵌 scratch、没有 DLL，发布字完全在 ISDU 上下文内。 */
static iolink_isdu_ctx_t s_ctx;
static uint8_t s_value_a;
static uint8_t s_value_b;
static uint8_t s_value_c;

/* 切点注入：钩子要把"下一帧"喂进去，参数经这些静态变量传递。 */
static const uint8_t* s_inject;
static uint8_t s_inject_len;
static void (*s_cut_hook)(void);

/** @brief 按 FlowCTRL 序把整个请求交给写入口（与 ISR 逐段投递等价）。 */
static void feed_write(const uint8_t* req, uint8_t len)
{
    uint8_t fc = (uint8_t) IOLINK_FLOWCTRL_START;
    for (uint8_t i = 0U; i < len; i++) {
        iolink_isdu_od_write(&s_ctx, fc, &req[i], 1U);
        fc = (fc == (uint8_t) IOLINK_FLOWCTRL_START)
                 ? 1U
                 : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
}

/** @brief 切点动作：注入一条已收齐的请求（新 START + 全部 COUNT）。 */
static void cut_feed_injected(void)
{
    feed_write(s_inject, s_inject_len);
    s_cut_hook = NULL;
}

/** @brief 切点动作：先注入一条已收齐的请求，再开始累计第三条。 */
static void cut_feed_then_start(void)
{
    feed_write(s_inject, s_inject_len);
    iolink_isdu_od_write(&s_ctx, IOLINK_FLOWCTRL_START, s_inject, 1U);
    s_cut_hook = NULL;
}

/** @brief 切点动作：注入了什么就什么都不做（对照组）。 */
static iolink_result_t on_change_a(iolink_app_t* IOLINK_NULLABLE self,
                                   const iolink_od_entry_t* entry, const uint8_t* data, size_t len)
{
    (void) self;
    (void) entry;
    (void) data;
    (void) len;
    if (s_cut_hook != NULL) {
        s_cut_hook();
    }
    return iolink_result_ok(0U);
}

static const iolink_od_entry_t k_entries[3] = {
    {
        .index = 0x0040U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U8,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .flags = 0U,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = &s_value_a, .str_len = NULL},
        .block_len = 0U,
        .range_min = 0U,
        .range_max = 255U,
        .scalar_default = 0U,
        .block_default = NULL,
        .name = "slot-a",
        .validate = NULL,
        .on_change = on_change_a,
    },
    {
        .index = 0x0041U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U8,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .flags = 0U,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = &s_value_b, .str_len = NULL},
        .block_len = 0U,
        .range_min = 0U,
        .range_max = 255U,
        .scalar_default = 0U,
        .block_default = NULL,
        .name = "slot-b",
        .validate = NULL,
        .on_change = NULL,
    },
    {
        .index = 0x0042U,
        .subindex = 0U,
        .max_subindex = 0U,
        .datatype = IOLINK_OD_DT_U8,
        .access = IOLINK_OD_ACCESS_RW,
        .write_semantics = IOLINK_OD_WRITE_VOLATILE,
        .storage = IOLINK_OD_STORAGE_VOLATILE,
        .flags = 0U,
        .handler = IOLINK_OD_HANDLER_NONE,
        .backend = IOLINK_OD_BACKEND_STORAGE,
        .u.storage = {.value = &s_value_c, .str_len = NULL},
        .block_len = 0U,
        .range_min = 0U,
        .range_max = 255U,
        .scalar_default = 0U,
        .block_default = NULL,
        .name = "slot-c",
        .validate = NULL,
        .on_change = NULL,
    },
};

static const iolink_od_app_t k_app = {.entries = k_entries, .count = 3U};

static void engine_init(void)
{
    s_value_a = 0U;
    s_value_b = 0U;
    s_value_c = 0U;
    s_inject = NULL;
    s_inject_len = 0U;
    s_cut_hook = NULL;
    iolink_isdu_init(&s_ctx);
    iolink_isdu_bind_od(&s_ctx, &k_app, NULL);
    iolink_isdu_bind_caps(&s_ctx, NULL);
}

/** @brief 8 位地址域的写请求：I-Service 0x34（写、16 位索引 + 子索引 + 1 值）。 */
static uint8_t make_write(uint8_t* out, uint16_t index, uint8_t value)
{
    out[0] = 0x36U;
    out[1] = (uint8_t) (index >> 8);
    out[2] = (uint8_t) (index & 0xFFU);
    out[3] = 0U;
    out[4] = value;
    uint8_t chk = 0U;
    for (uint8_t i = 0U; i < 5U; i++) {
        chk = (uint8_t) (chk ^ out[i]);
    }
    out[5] = chk;
    return 6U;
}

static uint8_t pub_sel(void)
{
    return IOLINK_ISDU_REQ_PUB_SEL(s_ctx.req_pub);
}

static uint16_t pub_gen(void)
{
    return IOLINK_ISDU_REQ_PUB_GEN(s_ctx.req_pub);
}

static bool pub_pending(void)
{
    return (IOLINK_ISDU_REQ_PUB_LEN(s_ctx.req_pub) != 0U) && (pub_gen() != s_ctx.req_ack_gen);
}

/** @brief 槽 @p sel 的缓冲（与 `req_base + sel * req_size` 一致）。 */
static const uint8_t* slot_buf(uint8_t sel)
{
    return &s_ctx.req_base[(size_t) sel * s_ctx.req_size];
}

/* ① 主泵执行槽 A 期间收齐槽 B：A 的缓冲与解析结果保持完整，B 随后执行。 */
static void test_next_request_during_service_uses_other_slot(void** state)
{
    (void) state;
    engine_init();

    uint8_t req_a[6];
    uint8_t req_b[6];
    const uint8_t len_a = make_write(req_a, 0x0040U, 0x11U);
    const uint8_t len_b = make_write(req_b, 0x0041U, 0x22U);

    feed_write(req_a, len_a);
    const uint8_t sel_a = pub_sel();
    const uint16_t gen_a = pub_gen();
    assert_true(pub_pending());

    /* 主泵执行 A 期间（服务回调内），ISR 收齐 B。 */
    s_inject = req_b;
    s_inject_len = len_b;
    s_cut_hook = cut_feed_injected;
    iolink_isdu_process(&s_ctx);

    /* A 已 ack 并执行完；B 在另一个槽里等待。 */
    assert_int_equal(s_value_a, 0x11U);
    assert_int_equal(s_value_b, 0U);
    assert_int_equal(s_ctx.header.index, 0x0040U);
    assert_int_equal(s_ctx.req_ack_gen, gen_a);
    assert_int_equal(pub_sel(), 1U - sel_a);
    assert_int_not_equal(pub_gen(), gen_a);
    assert_true(pub_pending());
    assert_int_equal(s_ctx.req_overruns, 0U);

    /* A 的槽内容未被 ISR 覆盖：B 用的是另一个槽。 */
    assert_memory_equal(slot_buf(sel_a), req_a, len_a);
    assert_memory_equal(slot_buf((uint8_t) (1U - sel_a)), req_b, len_b);

    /* 下一轮主泵执行 B。 */
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value_b, 0x22U);
    assert_int_equal(s_value_a, 0x11U);
    assert_false(pub_pending());
}

/* ② 已发布但未被接管的请求被新 START 取代：计数，且被覆盖的请求不执行。 */
static void test_unclaimed_request_is_replaced_and_counted(void** state)
{
    (void) state;
    engine_init();

    uint8_t req_a[6];
    uint8_t req_b[6];
    uint8_t req_c[6];
    const uint8_t len_a = make_write(req_a, 0x0040U, 0x11U);
    const uint8_t len_b = make_write(req_b, 0x0041U, 0x22U);
    const uint8_t len_c = make_write(req_c, 0x0042U, 0x33U);

    feed_write(req_a, len_a);
    const uint16_t gen_a = pub_gen();
    assert_true(pub_pending());

    /* ISR 收齐 B 后又开始 C：B 已发布但主泵尚未接管，被 C 取代并计数。
     * 主泵认领过的槽在解析结束后已经释放，但发布字仍指向 B 的槽。 */
    s_inject = req_b;
    s_inject_len = len_b;
    s_cut_hook = cut_feed_then_start;
    iolink_isdu_process(&s_ctx);

    /* B 被覆盖并计数，且从发布字上撤回（len = 0）。 */
    assert_int_equal(s_ctx.req_overruns, 1U);
    assert_int_equal(IOLINK_ISDU_REQ_PUB_LEN(s_ctx.req_pub), 0U);
    assert_int_equal(s_ctx.req_ack_gen, gen_a);

    /* 补齐 C：它是被覆盖槽的接手者，执行后 B 仍未执行。 */
    for (uint8_t i = 1U; i < len_c; i++) {
        iolink_isdu_od_write(&s_ctx, i, &req_c[i], 1U);
    }
    assert_true(pub_pending());
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value_a, 0x11U);
    assert_int_equal(s_value_c, 0x33U);
    assert_int_equal(s_value_b, 0U);
    assert_false(pub_pending());
}

/* ③ ack 前进后槽位重新可用：轮换回旧槽，且不计数。 */
static void test_ack_lets_isr_reuse_slot(void** state)
{
    (void) state;
    engine_init();

    uint8_t req_a[6];
    uint8_t req_b[6];
    uint8_t req_a2[6];
    const uint8_t len_a = make_write(req_a, 0x0040U, 0x11U);
    const uint8_t len_b = make_write(req_b, 0x0041U, 0x22U);
    const uint8_t len_a2 = make_write(req_a2, 0x0040U, 0x44U);

    feed_write(req_a, len_a);
    const uint8_t sel_a = pub_sel();
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value_a, 0x11U);

    /* 已 ack：下一条走另一个槽。 */
    feed_write(req_b, len_b);
    assert_int_equal(pub_sel(), 1U - sel_a);
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value_b, 0x22U);

    /* 又已 ack：槽位轮换回第一个，请求照常完成。 */
    feed_write(req_a2, len_a2);
    assert_int_equal(pub_sel(), sel_a);
    assert_ptr_equal(s_ctx.req_buf, slot_buf(sel_a));
    iolink_isdu_process(&s_ctx);
    assert_int_equal(s_value_a, 0x44U);
    assert_int_equal(s_ctx.req_overruns, 0U);
}

/* ④ 主泵正在解析“已发布槽”时，新 START 落另一个槽：不计数，且被解析的
 *   槽内容不被改写。busy 选槽保护只有白盒设置 req_busy_sel 才能覆盖。 */
static void test_start_prefers_the_free_slot_while_published_is_parsed(void** state)
{
    (void) state;
    engine_init();

    uint8_t req_a[6];
    uint8_t req_b[6];
    const uint8_t len_a = make_write(req_a, 0x0040U, 0x11U);
    const uint8_t len_b = make_write(req_b, 0x0041U, 0x22U);

    feed_write(req_a, len_a);
    const uint8_t sel = pub_sel();
    const uint32_t overruns = s_ctx.req_overruns;

    s_ctx.req_busy_sel = sel; /* 主泵正在解析已发布槽 */
    feed_write(req_b, len_b);

    assert_int_equal(s_ctx.req_slot, (uint8_t) (1U - sel));
    assert_int_equal(s_ctx.req_overruns, overruns);
    assert_memory_equal(slot_buf(sel), req_a, len_a);
}

/* ⑤ 另一个槽被解析时只能写已发布槽：未接管的请求被取代并计数。 */
static void test_start_replaces_the_unclaimed_slot_and_counts(void** state)
{
    (void) state;
    engine_init();

    uint8_t req_a[6];
    uint8_t req_b[6];
    const uint8_t len_a = make_write(req_a, 0x0040U, 0x11U);
    const uint8_t len_b = make_write(req_b, 0x0041U, 0x22U);

    feed_write(req_a, len_a);
    const uint8_t sel = pub_sel();
    const uint32_t overruns = s_ctx.req_overruns;

    s_ctx.req_busy_sel = (uint8_t) (1U - sel); /* 主泵在解析另一个槽 */
    feed_write(req_b, len_b);

    assert_int_equal(s_ctx.req_slot, sel);
    assert_int_equal(s_ctx.req_overruns, overruns + 1U);
    /* 被取代的是未接管的那条，槽内容已是新请求。 */
    assert_memory_equal(slot_buf(sel), req_b, len_b);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_next_request_during_service_uses_other_slot),
        cmocka_unit_test(test_unclaimed_request_is_replaced_and_counted),
        cmocka_unit_test(test_ack_lets_isr_reuse_slot),
        cmocka_unit_test(test_start_prefers_the_free_slot_while_published_is_parsed),
        cmocka_unit_test(test_start_replaces_the_unclaimed_slot_and_counts),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
