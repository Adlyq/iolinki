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
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/protocol.h"

static uint64_t now_us;
static unsigned tx_octets;
static unsigned tx_frames;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

/** @brief 向邮箱环发布一个请求（由 ISR 执行）。 */
static void push_mailbox(iolink_dll_ctx_t* ctx, const uint8_t* frame, uint8_t len)
{
    iolink_frame_rec_t* rec = (iolink_frame_rec_t*) iolink_ring_claim(&ctx->isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    rec->len = len;
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, len);
    iolink_ring_publish(&ctx->isr_frame_ring);
}

static int rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    (void) data;
    (void) status;
    return 0; /* 测试直接向邮箱发布数据。 */
}

static int tx_octet(void* user, uint8_t data)
{
    (void) user;
    (void) data;
    tx_octets++;
    return 0;
}

static void tx_done(void* user)
{
    (void) user;
    tx_frames++;
}

static uint8_t int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((ctx != NULL) && (ctx->isr_tx_active != 0U)) ? 1U : 0U;
}

/* 一个合法的 Type-0 请求：携带 A.1.6 校验和的 MC + CKT。 */
static void make_type0_frame(uint8_t mc, uint8_t frame[2])
{
    frame[0] = mc;
    frame[1] = 0x00U; /* Type 0；校验和占用 bit 0-5。 */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2U));
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

        iolink_dll_process(ctx);
        while (ctx->isr_tx_active != 0U) {
            iolink_dll_process_isr(ctx);
        }

        if (!iolink_ring_is_empty(&ctx->isr_frame_ring)) {
            return 1; /* 邮箱未被消费 */
        }
        if (ctx->timeout_errors != 0U) {
            return 2; /* 空闲监控被触发 */
        }
        if (ctx->crc_errors != 0U) {
            return 3;
        }
        if (ctx->framing_errors != 0U) {
            return 4;
        }
        if (ctx->state == IOLINK_DLL_STATE_STARTUP) {
            return 5;
        }
    }
    return 0;
}

int main(void)
{
    iolink_dll_ctx_t ctx;
    const iolink_phy_api_t phy = {
        .user = &ctx,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
    };

    iolink_dll_init(&ctx, &phy);
    ctx.time_us = test_time_us;
    ctx.state = IOLINK_DLL_STATE_ESTAB_COM;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    now_us = 1000000U;
    ctx.last_activity_ms = ((uint32_t) (now_us / 1000U));

    /* 场景 A：每周期 1 ms，仿真 60 秒（60k 帧）。 */
    if (run_cycles(&ctx, 60000U, 1000U) != 0) {
        return 10;
    }
    if (ctx.state != IOLINK_DLL_STATE_OPERATE) {
        return 11;
    }
    if (tx_frames != 60000U) {
        return 12; /* 每个被消费的帧对应一个应答 */
    }
    if (tx_octets != 2U * 60000U) {
        return 13; /* Type-0 READ 的应答为 OD + CKS */
    }

    /* 场景 B：跨越 32 位毫秒时间戳回绕。 */
    now_us = ((uint64_t) UINT32_MAX - 5U) * 1000U;
    ctx.last_activity_ms = ((uint32_t) (now_us / 1000U));
    ctx.timeout_errors = 0U;
    if (run_cycles(&ctx, 20U, 600000U) != 0) {
        return 20;
    }
    if (ctx.state != IOLINK_DLL_STATE_OPERATE) {
        return 21;
    }
    if (ctx.timeout_errors != 0U) {
        return 22;
    }

    return 0;
}
