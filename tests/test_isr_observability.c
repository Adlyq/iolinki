/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * 双泵 ISR 传输的可观测性（阶段 1）：
 *  - 单邮箱槽位繁忙期间到达的帧会被计数；
 *  - 校验和/类型校验失败的即时帧会被计数；
 *  - 合法的即时帧仍会产生应答。
 *
 * ISR 双泵传输可观测性。
 */

#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "conformance_test.h"

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
static uint8_t chk_pending;
static unsigned timer_a_starts;
static unsigned timer_a_stops;
static unsigned timer_b_starts;
static unsigned tx_done_calls;

static int rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
    *status = 0U;
    if (rx_i < rx_n) {
        *data = rx_q[rx_i++];
        return 1;
    }
    if (chk_pending != 0U) {
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
    tx_done_calls++;
}

static void timer_a_start(void* user)
{
    (void) user;
    timer_a_starts++;
}

static void timer_a_stop(void* user)
{
    (void) user;
    timer_a_stops++;
}

static void timer_b_start(void* user)
{
    (void) user;
    timer_b_starts++;
}

/* 仅当 PHY 能报告唤醒（dll_phy_has_wakeup()）时，DLL 才会启用 FALLBACK 迁移；
 * 因此提供该钩子，让快速路径上的 Fallback 命令
 * 走完整个迁移 + 截止时间流程。 */
static int mock_detect_wakeup(void* user)
{
    (void) user;
    return 0;
}

static uint8_t int_active(void* user)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) user;
    return ((rx_i < rx_n) || (chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U)) ? 1U
                                                                                            : 0U;
}

static void feed(const uint8_t* frame, size_t n)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    chk_pending = 0U;
}

static void feed_chk(const uint8_t* frame, size_t n)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    chk_pending = 1U;
}

static void make_type2_process_read(uint8_t* frame, size_t len)
{
    frame[0] = IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK; /* READ */
    frame[1] = IOLINK_MSEQ_TYPE_2;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static iolink_phy_api_t g_phy;

static void init_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.user = dll;
    g_phy.rx_octet = rx_octet;
    g_phy.tx_octet = tx_octet;
    g_phy.tx_done = tx_done;
    g_phy.int_active = int_active;
    g_phy.response_timer_start = timer_a_start;
    g_phy.response_timer_stop = timer_a_stop;
    g_phy.stack_timer_start = timer_b_start;
    g_phy.detect_wakeup = mock_detect_wakeup;
    iolink_dll_init(&dll->main, &dll->isr, &g_phy);
    dll->main.shared.time_us = test_time_us;
    dll->main.phy_mode = IOLINK_PHY_MODE_SDCI;
    dll->main.shared.state = IOLINK_DLL_STATE_OPERATE;
    dll->main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll->main.shared.od_width = 1U;
    dll->main.shared.pd_out_len_current = 0U;
    dll->main.pd_in_len_current = 0U;
    dll->main.pd_valid = true;
    now_us = 1000000U;
    rx_n = 0U;
    rx_i = 0U;
    tx_n = 0U;
}

static void exchange(iolink_dll_ctx_t* dll, const uint8_t* frame, size_t len)
{
    feed(frame, len);
    iolink_dll_process_isr(&dll->main);
    while (dll->main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll->main);
    }
    iolink_dll_process(&dll->main);
}

/* Type-2_V ISDU WRITE 的 M 序列：MC + CKT + 一个 OD 八位组。 */
static void make_isdu_write(const uint8_t mc, uint8_t od, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_2;
    frame[2] = od;
    for (size_t i = 3U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void make_type0(uint8_t mc, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void init_type0_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.user = dll;
    g_phy.rx_octet = rx_octet;
    g_phy.tx_octet = tx_octet;
    g_phy.tx_done = tx_done;
    g_phy.int_active = int_active;
    g_phy.response_timer_start = timer_a_start;
    g_phy.response_timer_stop = timer_a_stop;
    g_phy.stack_timer_start = timer_b_start;
    g_phy.detect_wakeup = mock_detect_wakeup;
    iolink_dll_init(&dll->main, &dll->isr, &g_phy);
    dll->main.shared.time_us = test_time_us;
    dll->main.phy_mode = IOLINK_PHY_MODE_SDCI;
    dll->main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    dll->main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll->main.shared.od_width = 1U;
    dll->main.shared.pd_out_len_current = 0U;
    dll->main.pd_in_len_current = 0U;
    dll->main.pd_valid = true;
    dll->main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll->main.shared.isdu.resp_buf[0] = 0x11U;
    dll->main.shared.isdu.resp_buf[1] = 0x22U;
    dll->main.shared.isdu.resp_total = 2U;
    now_us = 1000000U;
    rx_n = 0U;
    rx_i = 0U;
    chk_pending = 0U;
    tx_n = 0U;
    iolink_dll_process(&dll->main); /* 同步 ISDU 快照 */
}

/* PREOPERATE 下的 Type-0 ISDU 读由 ISR 直接应答。 */
static int test_type0_isdu_read_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    if (IOLINK_ISDU_PUB_LEN(dll.main.shared.isr_isdu_pub) != 2U) {
        return 1;
    }

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | IOLINK_FLOWCTRL_START),
               frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }

    if ((tx_n != 2U) || (tx_q[0] != 0x11U)) {
        return 2;
    }
    if (!iolink_ring_is_empty(&dll.main.shared.isr_frame_ring)) {
        return 3; /* 快速路径不得发布 */
    }
    return 0;
}

/* SDCI_TC_0053：FlowCTRL 0x11..0x1F 必须应答 “No Service”（0x00）。 */
static int test_0053_reserved_flowctrl_no_service(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0053");
    for (uint8_t fc = 0x11U; fc <= 0x1FU; fc++) {
        iolink_dll_ctx_t dll;
        init_type0_ctx(&dll);
        uint8_t frame[2];
        make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | fc), frame,
                   sizeof(frame));
        tx_n = 0U;
        feed_chk(frame, sizeof(frame));
        iolink_dll_process_isr(&dll.main);
        while (dll.main.shared.isr_tx_active != 0U) {
            iolink_dll_process_isr(&dll.main);
        }
        /* 无效 FlowCTRL 被有意留给主泵处理。 */
        iolink_dll_process(&dll.main);
        while (dll.main.shared.isr_tx_active != 0U) {
            iolink_dll_process_isr(&dll.main);
        }
        if ((tx_n != 2U) || (tx_q[0] != 0x00U)) {
            return (int) (fc - 0x10U);
        }
    }
    return 0;
}

/* SDCI_TC_0309：重复的 FlowCTRL 必须重发前一段，且
 * 下一个新 FlowCTRL 必须从响应中断处继续。 */
static uint8_t type0_isdu_exchange(iolink_dll_ctx_t* dll, uint8_t fc)
{
    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | fc), frame, sizeof(frame));
    tx_n = 0U;
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll->main);
    while (dll->main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll->main);
    }
    /* IDLE/ABORT 留给主泵处理；合法的 COUNT/START 已由
       ISR 应答，且不发布帧。 */
    iolink_dll_process(&dll->main);
    while (dll->main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll->main);
    }
    return tx_q[0];
}

static int test_0309_isdu_segment_repeat(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0309");
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t response[40];
    response[0] = 0xD1U;
    response[1] = (uint8_t) sizeof(response);
    for (uint8_t i = 2U; i < sizeof(response); i++) {
        response[i] = (uint8_t) (0x30U + i);
    }
    /* 通过正常的主泵->ISR 快照交接发布一个 40 八位组的已组帧响应，
       然后检验 ISR 的分段重发逻辑。 */
    dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.main.shared.isdu.resp_total = (uint8_t) sizeof(response);
    (void) memcpy(dll.main.shared.isdu.resp_buf, response, sizeof(response));
    iolink_dll_process(&dll.main);
    if (IOLINK_ISDU_PUB_LEN(dll.main.shared.isr_isdu_pub) != (uint8_t) sizeof(response)) {
        return 6;
    }

    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_START) != response[0]) {
        return 1;
    }
    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_START) != response[0]) {
        return 2;
    }

    for (uint8_t i = 1U; i < sizeof(response); i++) {
        const uint8_t fc = (uint8_t) (i & IOLINK_FLOWCTRL_COUNT_MASK);
        if (type0_isdu_exchange(&dll, fc) != response[i]) {
            return 3;
        }
        if ((i == 1U) || (i == 20U) || (i == (sizeof(response) - 1U))) {
            if (type0_isdu_exchange(&dll, fc) != response[i]) {
                return 4;
            }
        }
    }

    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_IDLE) != 0x00U) {
        return 5;
    }
    return 0;
}

/* SDCI_TC_0068：中止一个部分接收的 Read 响应会返回 No Service。 */
static int test_0068_aborted_read_response(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0068");
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t response[16];
    response[0] = 0xD1U;
    response[1] = (uint8_t) sizeof(response);
    for (uint8_t i = 2U; i < sizeof(response); i++) {
        response[i] = (uint8_t) (0x40U + i);
    }
    dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.main.shared.isdu.resp_total = (uint8_t) sizeof(response);
    (void) memcpy(dll.main.shared.isdu.resp_buf, response, sizeof(response));
    iolink_dll_process(&dll.main);

    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_START) != response[0]) {
        return 1;
    }
    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_ABORT) != 0x00U) {
        return 2;
    }
    if (type0_isdu_exchange(&dll, IOLINK_FLOWCTRL_IDLE) != 0x00U) {
        return 3;
    }
    return 0;
}

/* PREOPERATE 下的 Type-0 page 读由 ISR 直接应答。 */
static int test_type0_page_read_fast_path(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0052");
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK), frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }

    const uint8_t expected = iolink_isdu_direct_param_page1_octet(&dll.main.shared.isdu, 0U);
    if ((tx_n != 2U) || (tx_q[0] != expected)) {
        return 1;
    }
    return 0;
}

/* PREOPERATE 下的 Type-0 ISDU 写：ISR 将其缓冲，并且只回复 CKS。 */
static int test_type0_isdu_write_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    uint8_t frame[3];
    frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START);
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = 0x14U; /* ISDU 服务八位组 */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }

    if (tx_n != 1U) {
        return 1; /* 仅 CKS */
    }
    /* 14d63cb：ISR 只暂存写请求；ctx->main.shared.isdu 仍归主上下文所有。 */
    if (dll.main.shared.isdu.req_idx != 0U) {
        return 2;
    }
    const iolink_od_rec_t* staged =
        (const iolink_od_rec_t*) iolink_ring_peek(&dll.main.shared.isr_od_ring);
    if ((staged == NULL) || (staged->len != 1U) || (staged->data[0] != 0x14U) ||
        (staged->flowctrl != IOLINK_FLOWCTRL_START)) {
        return 3;
    }

    /* 主窗口把已暂存的八位组排入 ISDU 传输层。 */
    iolink_dll_process(&dll.main);
    if (!iolink_ring_is_empty(&dll.main.shared.isr_od_ring) ||
        (dll.main.shared.isdu.req_idx != 1U) || (dll.main.shared.isdu.req_buf[0] != 0x14U)) {
        return 5;
    }
    return 0;
}

/** @brief 向邮箱环发布一个帧（即数据 ISR 所做的事）。 */
static void push_mailbox(iolink_dll_ctx_t* dll, const uint8_t* frame, uint8_t len)
{
    iolink_frame_rec_t* rec =
        (iolink_frame_rec_t*) iolink_ring_claim(&dll->main.shared.isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    rec->len = len;
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, len);
    iolink_ring_publish(&dll->main.shared.isr_frame_ring);
}

/* 邮箱中已有帧：新的即时帧必须被记为丢弃。 */
static int test_mailbox_drop(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));

    /* 填满两个槽位：此前的帧尚未被消费。 */
    push_mailbox(&dll, frame, (uint8_t) sizeof(frame));
    push_mailbox(&dll, frame, (uint8_t) sizeof(frame));
    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }

    if (dll.main.shared.isr_mailbox_drops != 1U) {
        return 1;
    }
    /* 即时应答不再依赖邮箱：邮箱满只会丢弃主泵的副本，
     * 不得抑制应答。 */
    if (tx_n != 2U) {
        return 2;
    }
    return 0;
}

/* 即时帧上的错误校验和必须被计数，而不能静默丢弃。 */
static int test_eager_reject(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));
    frame[1] ^= 0x01U; /* 破坏校验和 */

    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);

    if (dll.isr.isr_eager_rejects != 1U) {
        return 1;
    }
    if (tx_n != 0U) {
        return 2; /* 损坏的帧不会得到即时应答 */
    }
    if (iolink_ring_count(&dll.main.shared.isr_frame_ring) != 1U) {
        return 3; /* 已发布，以便主泵校验或回退 */
    }
    return 0;
}

/* 合法的即时帧仍会应答并发布。 */
static int test_valid_eager(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));

    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }

    if ((dll.main.shared.isr_mailbox_drops != 0U) || (dll.isr.isr_eager_rejects != 0U)) {
        return 1;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&dll.main.shared.isr_frame_ring);
    if ((iolink_ring_count(&dll.main.shared.isr_frame_ring) != 1U) || (rec == NULL) ||
        (rec->eager != 1U)) {
        return 2;
    }
    /* 过程数据 READ 应答：OD 占位 + CKS。 */
    if (tx_n != 2U) {
        return 3;
    }
    return 0;
}

/* 损坏的 ISDU 写不得进入传输层。 */
static int test_isdu_write_bad_crc_not_consumed(void)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0065");
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[3];
    make_isdu_write((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START), 0x14U, frame,
                    sizeof(frame));
    frame[1] ^= 0x01U; /* 破坏 CKT 校验和 */

    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);

    if (dll.main.shared.isdu.req_idx != 0U) {
        return 1;
    }
    if ((dll.isr.isr_eager_rejects != 1U) ||
        !iolink_ring_is_empty(&dll.main.shared.isr_frame_ring)) {
        return 2;
    }
    return 0;
}

/* 阶段 2：被拆分为四个 M 序列的 ISDU 写由 ISR
 * 缓冲、由主泵执行，且不会被重复追加。 */
static int test_isdu_write_consumed_by_isr(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    /* 写索引 0x10（VendorName，只读），带一个数据八位组：
     * I-Service 0x14、index 0x10、data 0x00、CHKPDU 0x04。 */
    const uint8_t isdu_octets[4] = {0x14U, 0x10U, 0x00U, 0x04U};
    uint8_t frame[3];
    const uint8_t flowctrl[4] = {IOLINK_FLOWCTRL_START, 1U, 2U, 3U};

    for (uint8_t i = 0U; i < 4U; i++) {
        const uint8_t mc = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | flowctrl[i]);
        make_isdu_write(mc, isdu_octets[i], frame, sizeof(frame));
        exchange(&dll, frame, sizeof(frame));
    }

    /* ISR 恰好缓冲了每个八位组一次（主泵中不会重复追加）。 */
    if (dll.main.shared.isdu.req_idx != 4U) {
        return 1;
    }
    if (dll.main.shared.isdu.req_total != 4U) {
        return 2;
    }
    /* 对只读索引的写会产生一个已组帧的否定响应。 */
    if ((dll.main.shared.isdu.state != ISDU_STATE_RESPONSE_READY) ||
        (IOLINK_ISDU_PUB_LEN(dll.main.shared.isr_isdu_pub) == 0U)) {
        return 3;
    }
    if (!iolink_ring_is_empty(&dll.main.shared.isr_od_ring)) {
        return 4; /* 主泵排空了每个已暂存的段 */
    }
    return 0;
}

/* 阶段 3b：改变状态的 Type-0 命令在 ISR 中处理。 */
static int test_type0_commands_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    uint8_t frame[3];

    /* 迁移命令（PREOP -> ESTAB_COM），无应答。 */
    make_type0(IOLINK_MC_TRANSITION_COMMAND, frame, 2U);
    tx_n = 0U;
    feed_chk(frame, 2U);
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }
    if ((dll.main.shared.state != IOLINK_DLL_STATE_ESTAB_COM) || (tx_n != 0U)) {
        return 1;
    }

    /* DeviceOperate（0x99，page 写地址 0）：ESTAB_COM + 仅 CKS。 */
    dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_DEVICE_OPERATE;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3U));
    tx_n = 0U;
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }
    if ((dll.main.shared.state != IOLINK_DLL_STATE_ESTAB_COM) || (tx_n != 1U)) {
        return 2;
    }

    /* Fallback（0x5A）：FALLBACK + 仅 CKS。ISR 只标记该请求；
     * 64 位 T_FBD 截止时间由主泵根据 MinCycleTime 设置。 */
    dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    frame[2] = IOLINK_CMD_FALLBACK;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3U));
    tx_n = 0U;
    dll.main.fallback_deadline_ms = 0U;
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }
    if ((dll.main.shared.state != IOLINK_DLL_STATE_FALLBACK) || (tx_n != 1U) ||
        (dll.main.shared.isr_fallback_pending == 0U) || (dll.main.fallback_deadline_ms != 0U)) {
        return 3;
    }
    iolink_dll_process(&dll.main);
    if ((dll.main.shared.isr_fallback_pending != 0U) || (dll.main.fallback_deadline_ms == 0U)) {
        return 5;
    }

    /* 启动探测（STARTUP 下的 page 读）：PREOP + OD/CKS。 */
    dll.main.shared.state = IOLINK_DLL_STATE_STARTUP;
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), frame, 2U);
    tx_n = 0U;
    feed_chk(frame, 2U);
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }
    if ((dll.main.shared.state != IOLINK_DLL_STATE_PREOPERATE) || (tx_n != 2U)) {
        return 4;
    }
    return 0;
}

/* 非 FALLBACK 的 MasterCommand 不得从 ISR 写入 64 位 T_FBD
 * 截止时间：ISR 只暂存一个清除请求，由主泵实施。 */
static int test_fallback_clear_is_staged(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    dll.main.fallback_deadline_ms = 0x0000000123456789ULL;

    uint8_t frame[3];
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_DEVICE_OPERATE;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 3U));
    tx_n = 0U;
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main);

    if ((dll.main.fallback_deadline_ms != 0x0000000123456789ULL) ||
        (dll.main.fallback_clear_request == 0U)) {
        return 1; /* ISR 触碰了该 64 位字段 */
    }
    iolink_dll_process(&dll.main);
    if ((dll.main.fallback_deadline_ms != 0U) || (dll.main.fallback_clear_request != 0U)) {
        return 2; /* 主泵未实施已暂存的清除 */
    }
    return 0;
}

/* 阶段 4a：Timer A 在第一个八位组上启动，并在应答前停止；
 * 应答完成时启动 Timer B。 */
static int test_timer_interlock(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    timer_a_starts = 0U;
    timer_a_stops = 0U;
    timer_b_starts = 0U;

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | IOLINK_FLOWCTRL_START),
               frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main);
    while (dll.main.shared.isr_tx_active != 0U) {
        iolink_dll_process_isr(&dll.main);
    }

    if ((timer_a_starts == 0U) || (timer_a_stops == 0U) || (timer_b_starts == 0U)) {
        return 1;
    }
    return 0;
}

/* 阶段 4a：Timer A 超时会中止部分帧并计入一次超时。 */
static int test_rx_timeout(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    timer_a_starts = 0U;
    timer_a_stops = 0U;

    const uint8_t partial[1] = {0x70U};
    feed(partial, sizeof(partial));
    iolink_dll_process_isr(&dll.main);
    if ((timer_a_starts == 0U) || (dll.isr.isr_rx_cnt == 0U)) {
        return 1;
    }

    iolink_dll_on_rx_timeout(&dll.main);
    if ((IOLINK_STAT_TOTAL(&dll.main, timeout_errors) == 0U) || (dll.isr.isr_rx_cnt != 0U) ||
        (timer_a_stops == 0U)) {
        return 2;
    }
    return 0;
}

/* 阶段 4a：Timer B tick 恰好授权一个主上下文窗口。 */
static int test_stack_tick_window(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    dll.isr.stack_tick_pending = 0U;
    if (iolink_dll_take_service_window(&dll.main) != 0U) {
        return 1; /* 尚无 tick（已安装硬件钩子） */
    }
    iolink_dll_on_stack_tick(&dll.main);
    if (iolink_dll_take_service_window(&dll.main) != 1U) {
        return 2;
    }
    if (iolink_dll_take_service_window(&dll.main) != 0U) {
        return 3; /* 恰好被消费一次 */
    }
    return 0;
}

/* RX BYTE CHK/间隔帧结束的表征。只有过短或不完整的
 * M 序列可能以间隔结束：长度完整的帧会在 CHK 指示之前
 * 就被字节引擎消费。该间隔必须丢弃帧、
 * 只统计真正的截断、释放收发器，且绝不发布或
 * 应答。 */
static int test_byte_gap_incomplete(void)
{
    /* 过短的 M 序列（少于两个头部八位组）。 */
    {
        iolink_dll_ctx_t dll;
        init_ctx(&dll);
        tx_done_calls = 0U;
        const uint8_t one[1] = {IOLINK_MC_CHANNEL_PROCESS};
        feed_chk(one, sizeof(one));
        iolink_dll_process_isr(&dll.main);
        if (IOLINK_STAT_TOTAL(&dll.main, framing_errors) != 0U) {
            return 1; /* 过短的帧不算 framing 错误 */
        }
        if ((dll.isr.isr_rx_cnt != 0U) || (dll.isr.isr_rx_expect != 0U)) {
            return 2;
        }
        if (!iolink_ring_is_empty(&dll.main.shared.isr_frame_ring)) {
            return 3;
        }
        if (tx_done_calls != 1U) {
            return 4;
        }
    }

    /* 被截断的 M 序列：头部完整，但未达到已知长度。 */
    {
        iolink_dll_ctx_t dll;
        init_ctx(&dll);
        tx_done_calls = 0U;
        uint8_t frame[3];
        make_isdu_write((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START), 0x14U, frame,
                        sizeof(frame));
        feed_chk(frame, 2U); /* 先送头部，再送 CHK 间隔 */
        iolink_dll_process_isr(&dll.main);
        if (IOLINK_STAT_TOTAL(&dll.main, framing_errors) != 1U) {
            return 5;
        }
        if ((dll.isr.isr_rx_cnt != 0U) || (dll.isr.isr_rx_expect != 0U)) {
            return 6;
        }
        if (!iolink_ring_is_empty(&dll.main.shared.isr_frame_ring)) {
            return 7; /* 绝不为主泵发布 */
        }
        if (dll.isr.isr_eager_rejects != 0U) {
            return 8; /* 间隔路径不执行即时校验 */
        }
        if (tx_done_calls != 1U) {
            return 9;
        }
    }
    return 0;
}

int main(void)
{
    int rc = test_mailbox_drop();
    if (rc != 0) {
        return 10 + rc;
    }
    rc = test_eager_reject();
    if (rc != 0) {
        return 20 + rc;
    }
    rc = test_valid_eager();
    if (rc != 0) {
        return 30 + rc;
    }
    rc = test_isdu_write_consumed_by_isr();
    if (rc != 0) {
        return 40 + rc;
    }
    rc = test_isdu_write_bad_crc_not_consumed();
    if (rc != 0) {
        return 50 + rc;
    }
    rc = test_type0_isdu_read_fast_path();
    if (rc != 0) {
        return 60 + rc;
    }
    rc = test_0053_reserved_flowctrl_no_service();
    if (rc != 0) {
        return 62 + rc;
    }
    rc = test_0309_isdu_segment_repeat();
    if (rc != 0) {
        return 63 + rc;
    }
    rc = test_0068_aborted_read_response();
    if (rc != 0) {
        return 65 + rc;
    }
    rc = test_type0_page_read_fast_path();
    if (rc != 0) {
        return 70 + rc;
    }
    rc = test_type0_isdu_write_fast_path();
    if (rc != 0) {
        return 80 + rc;
    }
    rc = test_type0_commands_fast_path();
    if (rc != 0) {
        return 85 + rc;
    }
    rc = test_timer_interlock();
    if (rc != 0) {
        return 90 + rc;
    }
    rc = test_rx_timeout();
    if (rc != 0) {
        return 100 + rc;
    }
    rc = test_stack_tick_window();
    if (rc != 0) {
        return 110 + rc;
    }
    rc = test_byte_gap_incomplete();
    if (rc != 0) {
        return 120 + rc;
    }
    rc = test_fallback_clear_is_staged();
    if (rc != 0) {
        return 130 + rc;
    }
    return 0;
}
