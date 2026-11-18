/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * ISR 双泵传输的长时程稳定性测试。
 *
 * mock 时钟把 N 个仿真秒以及 32 位毫秒时间戳回绕
 * 压缩成数毫秒的实际时间。它检查：
 *  - 持续的邮箱流量绝不触发 1 s 无活动监控；
 *  - 合法帧绝不被拒绝（crc/framing/timeout 保持 0）；
 *  - 每个被消费的帧恰好启动一个应答；
 *  - 毫秒时间戳回绕不会复位健康的链路。
 *
 * ISR 双泵传输稳定性检查。
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "crc_oracle.h"
#include "dll_access.h"
#include "iolinki/protocol.h"
#include "test_case.h"

static uint64_t now_us;
static unsigned tx_octets;
static unsigned tx_frames;
/* 交付给用例的应答数与用例已经查看过的应答数：group teardown 要求相等。 */
static uint64_t tx_delivered;
static uint64_t tx_asserted;

static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return now_us;
}

/** @brief 向邮箱环发布一个请求（由 ISR 执行）。 */
static void push_mailbox(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    iolink_frame_rec_t* rec =
        (iolink_frame_rec_t*) iolink_ring_claim(&ctx->main.shared.i_isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    rec->len = len;
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, len);
    iolink_ring_publish(&ctx->main.shared.i_isr_frame_ring);
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
static const uint8_t* rx_frame(iolink_app_t* IOLINK_NULLABLE self, uint8_t* status, uint8_t* len)
{
    (void) self;
    *status = 0U;
    *len = 0U;
    return NULL; /* 测试直接向邮箱发布数据，PHY 不交付帧。 */
}
#else
static int rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
    (void) data;
    (void) status;
    return 0; /* 测试直接向邮箱发布数据。 */
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
static int tx_frame(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    (void) self;
    (void) data;
    tx_frames++;
    tx_octets += (unsigned) len;
    tx_delivered++;
    return 0;
}
#else
static uint8_t tx_active; /* 只在 BYTE 传输下有字节流式发送 */
static int tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
{
    (void) self;
    (void) data;
    tx_active = 1U;
    tx_octets++;
    return 0;
}

static void tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (tx_active != 0U) {
        tx_active = 0U;
        tx_frames++;
        tx_delivered++;
    }
}
#endif

/* 读取即视为查看：调用点断言的是累计总数（60000/60020），因此一次读取
 * 覆盖此前全部交付；group teardown 再核对"交付 == 查看"。 */
static unsigned tx_frame_count(void)
{
    tx_asserted = tx_delivered;
    return tx_frames;
}

static void request_service(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    /* 宿主机没有中断源：应答由 run_cycles() 的服务循环排空。 */
}

static uint8_t int_active(iolink_app_t* IOLINK_NULLABLE self)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) self;
    return ((ctx != NULL) && (ctx->main.shared.b_isr_tx_active != 0U)) ? 1U : 0U;
}

/* 一个合法的 Type-0 请求：携带 A.1.6 校验和的 MC + CKT。 */
static void make_type0_frame(uint8_t mc, uint8_t frame[2])
{
    frame[0] = mc;
    frame[1] = 0x00U; /* Type 0；校验和占用 bit 0-5。 */
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 2U));
}

/* 每个周期送入一个邮箱帧，排空应答，校验各项不变式。
 * 成功返回 0，否则返回非零的测试失败码。 */
static int run_cycles(iolink_dll_ctx_t* ctx, unsigned cycles, uint64_t step_us)
{
    uint8_t frame[2];
    make_type0_frame(0x80U, frame); /* Type-0 READ：两个八位组的应答。 */

    for (unsigned i = 0U; i < cycles; i++) {
        now_us += step_us;
        push_mailbox(ctx, frame, (uint8_t) sizeof(frame));

        iolink_dll_process(&ctx->main);
        while ((ctx->main.shared.b_isr_tx_pending != 0U) ||
               (ctx->main.shared.b_isr_tx_active != 0U)) {
            iolink_dll_process_isr(&ctx->main.shared);
        }

        if (!iolink_ring_is_empty(&ctx->main.shared.i_isr_frame_ring)) {
            return 1; /* 邮箱未被消费 */
        }
        if (ctx->main.shared.b_timeout_errors != 0U) {
            return 2; /* 空闲监控被触发 */
        }
        if (ctx->main.crc_errors != 0U) {
            return 3;
        }
        if (ctx->main.shared.b_framing_errors != 0U) {
            return 4;
        }
        if (ctx->main.shared.b_state == IOLINK_DLL_STATE_STARTUP) {
            return 5;
        }
    }
    return 0;
}

static int run_all(void)
{
    iolink_dll_ctx_t ctx;
    const iolink_phy_api_t phy = {
        .self = (iolink_app_t * IOLINK_NULLABLE) & ctx,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
        .rx_frame = rx_frame,
#else
        .rx_octet = rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        .tx_frame = tx_frame,
#else
        .tx_octet = tx_octet,
        .tx_done = tx_done,
#endif
        .int_active = int_active,
        .request_service = request_service,
    };

    iolink_dll_init(&ctx.main, &ctx.isr, &phy);
    ctx.main.shared.c_clock.time_us = test_time_us;
    ctx.main.shared.b_state = IOLINK_DLL_STATE_ESTAB_COM;
    ctx.main.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.main.enforce_timing = false;
    now_us = 1000000U;
    ctx.main.shared.b_last_activity_ms = ((uint32_t) (now_us / 1000U));

    /* 场景 A：每周期 1 ms，仿真 60 秒（60k 帧）。 */
    if (run_cycles(&ctx, 60000U, 1000U) != 0) {
        return 10;
    }
    if (ctx.main.shared.b_state != IOLINK_DLL_STATE_OPERATE) {
        return 11;
    }
    if (tx_frame_count() != 60000U) {
        return 12; /* 每个被消费的帧对应一个应答 */
    }
    if (tx_octets != 2U * 60000U) {
        return 13; /* Type-0 READ 的应答为 OD + CKS */
    }

    /* 场景 B：跨越 32 位毫秒时间戳回绕。 */
    now_us = ((uint64_t) UINT32_MAX - 5U) * 1000U;
    ctx.main.shared.b_last_activity_ms = ((uint32_t) (now_us / 1000U));
    ctx.main.shared.b_timeout_errors = 0U;
    if (run_cycles(&ctx, 20U, 600000U) != 0) {
        return 20;
    }
    if (ctx.main.shared.b_state != IOLINK_DLL_STATE_OPERATE) {
        return 21;
    }
    if (ctx.main.shared.b_timeout_errors != 0U) {
        return 22;
    }
    if (tx_frame_count() != 60020U) {
        return 23; /* 回绕场景每周期同样恰好一个应答 */
    }

    return 0;
}

IOLINK_TEST_RC(run_all)

static int group_teardown(void** state)
{
    (void) state;
    if (tx_asserted != tx_delivered) {
        print_message("test_isr_stability: %llu replies delivered, %llu asserted\n",
                      (unsigned long long) tx_delivered, (unsigned long long) tx_asserted);
        exit(EXIT_FAILURE); /* cmocka keeps exit 0 after a failed group teardown */
    }
    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_run_all),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
