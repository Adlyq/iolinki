/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * 双泵 ISR 传输：ISDU 响应快照 + request_service 暂存。
 *
 * - 主泵为已组帧的 ISDU 响应制作快照；ISR 据此应答
 *   START/COUNT 读（Busy / No Service / 数据），因此没有任何读操作
 *   需要往返主循环。
 * - 主上下文的应答会被暂存，并通过 request_service() 报告；
 *   TX 钩子只会从 ISR 中调用。
 *
 * ISR ISDU 响应快照检查。
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

#define RX_MAX 16U
#define TX_MAX 32U

static uint8_t rx_q[RX_MAX];
static size_t rx_n;
static size_t rx_i;
static uint8_t tx_q[TX_MAX];
static size_t tx_n;
static unsigned request_calls;
static uint8_t chk_pending;

static int rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (rx_i < rx_n) {
        *data = rx_q[rx_i++];
        return 1;
    }
    if (chk_pending != 0U) {
        /* Type-0 帧由收发器的帧结束（CHK）定界。 */
        chk_pending = 0U;
        *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}

static int tx_octet(void* user, uint8_t data)
{
    (void) user;
    if (tx_n < sizeof(tx_q)) {
        tx_q[tx_n++] = data;
    }
    return 0;
}

static void tx_done(void* user)
{
    (void) user;
}

static uint8_t int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((rx_i < rx_n) || (chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U)) ? 1U
                                                                                            : 0U;
}

static void request_service(void* user)
{
    request_calls++;
    iolink_dll_process_isr(&((iolink_dll_ctx_t*) user)->main);
}

static void feed(const uint8_t* frame, size_t n, uint8_t chk)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    chk_pending = chk;
}

static void drain_tx(iolink_dll_ctx_t* ctx)
{
    while (ctx->main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&ctx->main);
    }
}

/* MC + CKT + PD-out，A.1.6 校验和位于 CKT 的低位。 */
static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

/* 让一个请求走完 ISR，排空应答，然后让主泵
 * 消费邮箱，以便发布下一个帧。 */
static void exchange(iolink_dll_ctx_t* ctx, const uint8_t* frame, size_t len, uint8_t chk)
{
    feed(frame, len, chk);
    iolink_dll_process_isr(&ctx->main);
    drain_tx(ctx);
    iolink_dll_process(&ctx->main);
}

/* START/COUNT 读由快照提供服务；Busy 与 No Service 映射为
 * 0x01/0x00。OD 宽度为 2（TYPE_2_V）。 */
static int test_eager_isdu_snapshot(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .user = &dll,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
    };

    iolink_dll_init(&dll.main, &dll.isr, &phy);
    dll.main.shared.time_us = test_time_us;
    dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll.main.shared.od_width = 2U;
    dll.main.shared.pd_out_len_current = 2U;
    dll.main.pd_in_len_current = 0U;

    dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.main.shared.isdu.resp_buf[0] = 0xD4U;
    dll.main.shared.isdu.resp_buf[1] = 0x12U;
    dll.main.shared.isdu.resp_buf[2] = 0x34U;
    dll.main.shared.isdu.resp_buf[3] = 0xF2U;
    dll.main.shared.isdu.resp_total = 4U;

    now_us = 1000000U;
    dll.main.shared.last_activity_ms = ((uint32_t) (now_us / 1000U));
    iolink_dll_process(&dll.main); /* 刷新 ISR 快照 */
    if ((IOLINK_ISDU_PUB_LEN(dll.main.shared.isr_isdu_pub) != 4U) ||
        (IOLINK_ISDU_PUB_BUSY(dll.main.shared.isr_isdu_pub) != 0U)) {
        return 1;
    }

    /* START 读：MC = RW | ISDU | FlowCTRL START = 0xF0。 */
    uint8_t frame[4];
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if (tx_n != 3U) {
        return 2; /* 两个 OD 八位组 + CKS */
    }
    if ((tx_q[0] != 0xD4U) || (tx_q[1] != 0x12U)) {
        return 3;
    }

    /* COUNT 1 读：MC = RW | ISDU | COUNT 1 = 0xE1 -> 接下来的两个八位组。 */
    make_frame(0xE1U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if (tx_n != 3U) {
        return 4;
    }
    if ((tx_q[0] != 0x34U) || (tx_q[1] != 0xF2U)) {
        return 5;
    }

    /* 重复的 COUNT 会重发前一段（相同的两个八位组）。 */
    make_frame(0xE1U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n != 3U) || (tx_q[0] != 0x34U) || (tx_q[1] != 0xF2U)) {
        return 8;
    }

    /* 乱序的 COUNT 属于分段错误 -> No Service（0x00）。 */
    make_frame(0xE3U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n < 1U) || (tx_q[0] != 0x00U)) {
        return 9;
    }

    /* Busy START。 */
    dll.main.shared.isr_isdu_pub = IOLINK_ISDU_PUB_WORD(0U, 1U, 0U);
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n < 1U) || (tx_q[0] != 0x01U)) {
        return 6;
    }

    /* No Service START（没有已暂存的响应）。 */
    dll.main.shared.isr_isdu_pub = IOLINK_ISDU_PUB_WORD(0U, 0U, 0U);
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_n = 0U;
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_n < 1U) || (tx_q[0] != 0x00U)) {
        return 7;
    }

    return 0;
}

/* 主上下文的 Type-0 应答必须被暂存并通过 request_service 报告；
 * ISR 是 TX 钩子的唯一调用者。 */
static int test_request_service_staging(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .user = &dll,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
        .request_service = request_service,
    };

    iolink_dll_init(&dll.main, &dll.isr, &phy);
    dll.main.shared.time_us = test_time_us;
    dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll.main.shared.pd_out_len_current = 0U;
    dll.main.pd_in_len_current = 0U;
    now_us = 1000000U;
    dll.main.shared.last_activity_ms = ((uint32_t) (now_us / 1000U));

    uint8_t frame[2];
    /* Type-0 过程数据写不经由 ISR 快速路径服务，因此它会在主上下文
     * 中被暂存，并通过 request_service 报告。 */
    make_frame(0x00U, IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    tx_n = 0U;
    request_calls = 0U;
    feed(frame, sizeof(frame), 1U);
    iolink_dll_process_isr(&dll.main); /* RX -> 邮箱（Type 0 不做即时应答） */
    iolink_dll_process(&dll.main);     /* 主上下文 -> dll_send -> request_service */
    if (request_calls != 1U) {
        return 10;
    }
    drain_tx(&dll);
    if (tx_n < 1U) {
        return 11; /* Type-0 WRITE 应答：仅 CKS */
    }

    return 0;
}

/* 诊断读由 ISR 快速路径（事件存储器）服务，因此应答
 * 不得经过 request_service / 主泵。 */
static int test_type0_diagnosis_read_isr(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .user = &dll,
        .rx_octet = rx_octet,
        .tx_octet = tx_octet,
        .tx_done = tx_done,
        .int_active = int_active,
        .request_service = request_service,
    };

    iolink_dll_init(&dll.main, &dll.isr, &phy);
    dll.main.shared.time_us = test_time_us;
    dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll.main.shared.pd_out_len_current = 0U;
    dll.main.pd_in_len_current = 0U;
    now_us = 1000000U;
    dll.main.shared.last_activity_ms = ((uint32_t) (now_us / 1000U));

    uint8_t frame[2];
    make_frame(0xC0U, IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    tx_n = 0U;
    request_calls = 0U;
    exchange(&dll, frame, sizeof(frame), 1U);
    if (request_calls != 0U) {
        return 12;
    }
    if (tx_n != 2U) {
        return 13; /* Type-0 READ 应答：OD + CKS */
    }
    return 0;
}

int main(void)
{
    int rc = test_eager_isdu_snapshot();
    if (rc != 0) {
        return rc;
    }
    rc = test_type0_diagnosis_read_isr();
    if (rc != 0) {
        return rc;
    }
    return test_request_service_staging();
}
