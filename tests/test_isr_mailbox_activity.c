/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * 回归测试：ISR 邮箱路径必须刷新活动时间戳。
 *
 * 此前邮箱分支在消费已发布的帧时没有更新
 * last_activity_ms/last_byte_us，导致 1 s 无活动监控在每个超时 tick 上
 * 都把健康的 ESTAB_COM 链路复位到 STARTUP。
 *
 * ISR 双泵邮箱活动检查。
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll.h"
#include "iolinki/protocol.h"

static uint64_t now_us;

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

/* 一个合法的 Type-0 请求：携带 A.1.6 校验和的 MC + CKT。 */
static void make_type0_frame(uint8_t mc, uint8_t frame[2])
{
    frame[0] = mc;
    frame[1] = 0x00U; /* Type 0；校验和占用 bit 0-5。 */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2U));
}

int main(void)
{
    const iolink_phy_api_t phy = {0};
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx, &phy);
    ctx.time_us = test_time_us;

    ctx.state = IOLINK_DLL_STATE_ESTAB_COM;
    ctx.phy_mode = IOLINK_PHY_MODE_SDCI;
    ctx.enforce_timing = false;
    now_us = 1000000U;
    ctx.last_activity_ms = ((uint32_t) (now_us / 1000U));

    uint8_t frame[2];
    make_type0_frame(0x80U, frame); /* Type-0 READ */

    for (unsigned i = 0U; i < 3U; i++) {
        /* 被服务的帧之间间隔 600 ms；在整个循环中累计会超过 1 s。 */
        now_us += 600000U;
        push_mailbox(&ctx, frame, (uint8_t) sizeof(frame));
        iolink_dll_process(&ctx);

        /* 邮箱帧属于活跃流量：它不得触发空闲计时器。
           显式检查保证在设置 NDEBUG 时该测试仍然有效。 */
        if ((ctx.timeout_errors != 0U) || (ctx.state == IOLINK_DLL_STATE_STARTUP)) {
            return 1;
        }
    }

    return 0;
}
