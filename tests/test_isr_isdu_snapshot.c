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
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "crc_oracle.h"
#include "dll_access.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "test_case.h"

static uint64_t now_us;

static uint64_t test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return now_us;
}

#define RX_MAX 16U
#define TX_MAX 32U

static uint8_t rx_q[RX_MAX];
static size_t rx_n;
static size_t rx_i;
static uint8_t tx_q[TX_MAX];
static size_t tx_n;
/* 交付给用例的应答数与用例已经查看过的应答数：group teardown 要求相等。 */
static uint64_t tx_delivered;
static uint64_t tx_asserted;
static uint64_t tx_leaked; /* 开始新捕获时仍未查看的交付 */
static unsigned request_calls;
/* 逐字节 CHK 只存在于 RX BYTE；FRAME 传输下一整帧自带帧尾，写点用宏置空。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static uint8_t chk_pending;
#define TEST_CHK_PENDING(chk_) (chk_pending = (chk_))
#else
#define TEST_CHK_PENDING(chk_) ((void) (chk_))
#endif

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME：feed() 送入的字节即一整个 PHY 帧。 */
static const uint8_t* rx_frame(iolink_app_t* IOLINK_NULLABLE self, uint8_t* status, uint8_t* len)
{
    (void) self;
    *len = 0U;
    *status = 0U;
    if ((rx_n == 0U) || (rx_i >= rx_n)) {
        return NULL;
    }
    const uint8_t* frame = &rx_q[rx_i];
    *len = (uint8_t) (rx_n - rx_i);
    *status = (uint8_t) (IOLINK_PHY_STATUS_DATA_READY | IOLINK_PHY_STATUS_CHK);
    rx_i = rx_n;
    return frame;
}
#else
static int rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
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
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* TX FRAME：帧尾 CKS 由库在 tx_frame() 之后写入，交付推迟到观察点。 */
static const uint8_t* tx_pending;
static size_t tx_pending_n;

static void tx_frame_sync(void)
{
    if (tx_pending == NULL) {
        return;
    }
    tx_n = (tx_pending_n <= sizeof(tx_q)) ? tx_pending_n : sizeof(tx_q);
    (void) memcpy(tx_q, tx_pending, tx_n);
    tx_pending = NULL;
    tx_pending_n = 0U;
    tx_delivered++;
}

static int tx_frame(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    (void) self;
    tx_frame_sync();
    tx_pending = data;
    tx_pending_n = len;
    return 0;
}
#else
static uint8_t tx_active; /* 只在 BYTE 传输下有字节流式发送 */
static int tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
{
    (void) self;
    if (tx_active == 0U) {
        tx_n = 0U;
        tx_active = 1U;
    }
    if (tx_n < sizeof(tx_q)) {
        tx_q[tx_n++] = data;
    }
    return 0;
}

static void tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    if (tx_active != 0U) {
        tx_active = 0U;
        tx_delivered++;
    }
}
#endif

/* 开始新一次捕获。上一帧若还没被查看，说明用例漏了断言：记入泄漏，
 * group teardown 失败。 */
static void tx_reset(void)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if (tx_asserted != tx_delivered) {
        tx_leaked += tx_delivered - tx_asserted;
        tx_asserted = tx_delivered;
    }
    tx_n = 0U;
}

/* 读取捕获长度即算一次查看：每次读取消费一次尚未查看的交付，
 * group teardown 核对"交付 == 查看"。 */
static size_t tx_observed_len(void)
{
    if (tx_asserted < tx_delivered) {
        tx_asserted++;
    }
    return tx_n;
}

static uint8_t int_active(iolink_app_t* IOLINK_NULLABLE self)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) self;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    (void) ctx;
    return ((rx_n != 0U) && (rx_i < rx_n)) ? 1U : 0U;
#else
    return ((rx_i < rx_n) || (chk_pending != 0U) || (ctx->main.shared.b_isr_tx_active != 0U)) ? 1U
                                                                                              : 0U;
#endif
}

static void request_service(iolink_app_t* IOLINK_NULLABLE self)
{
    request_calls++;
    iolink_dll_process_isr(&((iolink_dll_ctx_t*) self)->main.shared);
}

static void feed(const uint8_t* frame, size_t n, uint8_t chk)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    TEST_CHK_PENDING(chk);
}

static void drain_tx(iolink_dll_ctx_t* ctx)
{
    while (ctx->main.shared.b_isr_tx_active != 0U) {
        iolink_dll_process_isr(&ctx->main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
}

/* MC + CKT + PD-out，A.1.6 校验和位于 CKT 的低位。 */
static void make_frame(uint8_t mc, uint8_t type_bits, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = type_bits;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

/* 让一个请求走完 ISR，排空应答，然后让主泵
 * 消费邮箱，以便发布下一个帧。 */
static void exchange(iolink_dll_ctx_t* ctx, const uint8_t* frame, size_t len, uint8_t chk)
{
    feed(frame, len, chk);
    iolink_dll_process_isr(&ctx->main.shared);
    drain_tx(ctx);
    iolink_dll_process(&ctx->main);
}

/* START/COUNT 读由快照提供服务；Busy 与 No Service 映射为
 * 0x01/0x00。OD 宽度为 2（TYPE_2_V）。 */
static int test_eager_isdu_snapshot(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .self = (iolink_app_t * IOLINK_NULLABLE) & dll,
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
    };

    iolink_dll_init(&dll.main, &dll.isr, &phy);
    dll.main.shared.c_clock.time_us = test_time_us;
    dll.main.shared.b_state = IOLINK_DLL_STATE_OPERATE;
    dll.main.shared.c_m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll.main.shared.c_od_width = 2U;
    dll.main.shared.c_pd_out_len_current = 2U;
    dll.main.shared.c_pd_in_len = 0U;
    /* 计划表是 {类型, OD 宽度, 能力位} 的纯函数，只在初始化时编译；本用例
     * 在 init 之后才设定配置，因此显式提交一次（ISR 尚未运行）。 */
    iolink_dll_publish_reply_plans(&dll.main);

    dll.main.shared.b_isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.main.shared.b_isdu.resp_buf[0] = 0xD4U;
    dll.main.shared.b_isdu.resp_buf[1] = 0x12U;
    dll.main.shared.b_isdu.resp_buf[2] = 0x34U;
    dll.main.shared.b_isdu.resp_buf[3] = 0xF2U;
    dll.main.shared.b_isdu.resp_total = 4U;

    now_us = 1000000U;
    dll.main.shared.b_last_activity_ms = ((uint32_t) (now_us / 1000U));
    iolink_dll_process(&dll.main); /* 刷新 ISR 快照 */
    if ((IOLINK_ISDU_PUB_LEN(dll.main.shared.m_isr_isdu_pub) != 4U) ||
        (IOLINK_ISDU_PUB_BUSY(dll.main.shared.m_isr_isdu_pub) != 0U)) {
        return 1;
    }

    /* START 读：MC = RW | ISDU | FlowCTRL START = 0xF0。 */
    uint8_t frame[4];
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_reset();
    exchange(&dll, frame, sizeof(frame), 0U);
    if (tx_observed_len() != 3U) {
        return 2; /* 两个 OD 八位组 + CKS */
    }
    if ((tx_q[0] != 0xD4U) || (tx_q[1] != 0x12U)) {
        return 3;
    }

    /* COUNT 1 读：MC = RW | ISDU | COUNT 1 = 0xE1 -> 接下来的两个八位组。 */
    make_frame(0xE1U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_reset();
    exchange(&dll, frame, sizeof(frame), 0U);
    if (tx_observed_len() != 3U) {
        return 4;
    }
    if ((tx_q[0] != 0x34U) || (tx_q[1] != 0xF2U)) {
        return 5;
    }

    /* 重复的 COUNT 会重发前一段（相同的两个八位组）。 */
    make_frame(0xE1U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_reset();
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_observed_len() != 3U) || (tx_q[0] != 0x34U) || (tx_q[1] != 0xF2U)) {
        return 8;
    }

    /* 乱序的 COUNT 属于分段错误 -> No Service（0x00）。 */
    make_frame(0xE3U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_reset();
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_observed_len() < 1U) || (tx_q[0] != 0x00U)) {
        return 9;
    }

    /* Busy START。 */
    dll.main.shared.m_isr_isdu_pub = IOLINK_ISDU_PUB_WORD(0U, 1U, 0U);
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_reset();
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_observed_len() < 1U) || (tx_q[0] != 0x01U)) {
        return 6;
    }

    /* No Service START（没有已暂存的响应）。 */
    dll.main.shared.m_isr_isdu_pub = IOLINK_ISDU_PUB_WORD(0U, 0U, 0U);
    make_frame(0xF0U, IOLINK_MSEQ_TYPE_2, frame, sizeof(frame));
    tx_reset();
    exchange(&dll, frame, sizeof(frame), 0U);
    if ((tx_observed_len() < 1U) || (tx_q[0] != 0x00U)) {
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
        .self = (iolink_app_t * IOLINK_NULLABLE) & dll,
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

    iolink_dll_init(&dll.main, &dll.isr, &phy);
    dll.main.shared.c_clock.time_us = test_time_us;
    dll.main.shared.b_state = IOLINK_DLL_STATE_STARTUP;
    dll.main.shared.c_m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll.main.shared.c_od_width = 1U;
    dll.main.shared.c_pd_out_len_current = 0U;
    dll.main.shared.c_pd_in_len = 0U;
    now_us = 1000000U;
    dll.main.shared.b_last_activity_ms = ((uint32_t) (now_us / 1000U));
    iolink_dll_publish_reply_plans(&dll.main);

    uint8_t frame[3];
    /* STARTUP 的 MasterCommand 页写带 DEFER 计划：接受/拒绝依赖命令字节，
     * ISR 不应答，帧交主泵判定并由主上下文经 request_service 发送。 */
    make_frame(IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    frame[2] = IOLINK_CMD_DEVICE_PREOPERATE;
    frame[1] = IOLINK_MSEQ_TYPE_0; /* 重算 CKS 前必须清掉上一次的类型/校验位 */
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | test_expected_checksum6(frame, sizeof(frame)));
    tx_reset();
    request_calls = 0U;
    feed(frame, sizeof(frame), 1U);
    iolink_dll_process_isr(&dll.main.shared); /* DEFER：不进 ISR 应答路径 */
    if (request_calls != 0U) {
        return 9;
    }
    iolink_dll_process(&dll.main); /* 主上下文 -> dll_send -> request_service */
    if (request_calls != 1U) {
        return 10;
    }
    drain_tx(&dll);
    if (tx_observed_len() < 1U) {
        return 11; /* 确认：仅 CKS */
    }

    return 0;
}

/* 诊断读由 ISR 快速路径（事件存储器）服务，因此应答
 * 不得经过 request_service / 主泵。 */
static int test_type0_diagnosis_read_isr(void)
{
    iolink_dll_ctx_t dll;
    const iolink_phy_api_t phy = {
        .self = (iolink_app_t * IOLINK_NULLABLE) & dll,
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

    iolink_dll_init(&dll.main, &dll.isr, &phy);
    dll.main.shared.c_clock.time_us = test_time_us;
    dll.main.shared.b_state = IOLINK_DLL_STATE_PREOPERATE;
    dll.main.shared.c_m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll.main.shared.c_od_width = 1U;
    dll.main.shared.c_pd_out_len_current = 0U;
    dll.main.shared.c_pd_in_len = 0U;
    /* 计划表只在初始化时编译；本用例在 init 之后才配置，故显式发布一次
     * （ISR 尚未运行）。 */
    iolink_dll_publish_reply_plans(&dll.main);
    now_us = 1000000U;
    dll.main.shared.b_last_activity_ms = ((uint32_t) (now_us / 1000U));

    uint8_t frame[2];
    make_frame(0xC0U, IOLINK_MSEQ_TYPE_0, frame, sizeof(frame));
    tx_reset();
    request_calls = 0U;
    exchange(&dll, frame, sizeof(frame), 1U);
    if (request_calls != 0U) {
        return 12;
    }
    if (tx_observed_len() != 2U) {
        return 13; /* Type-0 READ 应答：OD + CKS */
    }
    return 0;
}

IOLINK_TEST_RC(test_eager_isdu_snapshot)
IOLINK_TEST_RC(test_type0_diagnosis_read_isr)
IOLINK_TEST_RC(test_request_service_staging)

static int group_teardown(void** state)
{
    (void) state;
    if ((tx_asserted != tx_delivered) || (tx_leaked != 0U)) {
        print_message(
            "test_isr_isdu_snapshot: %llu replies delivered, %llu asserted, %llu discarded\n",
            (unsigned long long) tx_delivered, (unsigned long long) tx_asserted,
            (unsigned long long) tx_leaked);
        exit(EXIT_FAILURE); /* cmocka keeps exit 0 after a failed group teardown */
    }
    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_test_eager_isdu_snapshot),
        cmocka_unit_test(iolink_case_test_type0_diagnosis_read_isr),
        cmocka_unit_test(iolink_case_test_request_service_staging),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
