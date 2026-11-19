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
#include <stdlib.h>
#include <string.h>

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "crc_oracle.h"
#include "dll_access.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "conformance_test.h"
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
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
/* 逐字节 CHK 只存在于 RX BYTE；FRAME 传输下没有读者，写点用宏置空。 */
static uint8_t chk_pending;
#define TEST_CHK_PENDING(chk_) (chk_pending = (chk_))
#define TEST_CHK_CLEAR() (chk_pending = 0U)
#define TEST_CHK_SET() (chk_pending = 1U)
#else
#define TEST_CHK_PENDING(chk_) ((void) (chk_))
#define TEST_CHK_CLEAR() ((void) 0)
#define TEST_CHK_SET() ((void) 0)
#endif
static unsigned timer_a_starts;
static unsigned timer_a_stops;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static uint8_t tx_active;       /* 一个应答正在发送（tx_done 交付） */
static unsigned tx_done_calls;  /* tx_done 只在 TX BYTE 存在 */
static unsigned timer_b_starts; /* 应答完成时重启 Timer B，仅 TX BYTE */
#define TEST_TIMER_B_STARTED() (timer_b_starts++)
#define TEST_TIMER_B_RESET() (timer_b_starts = 0U)
#else
#define TEST_TIMER_B_STARTED() ((void) 0)
#define TEST_TIMER_B_RESET() ((void) 0)
#endif

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
/* RX FRAME：feed()/feed_chk() 送入的字节即一整个 PHY 帧。 */
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
static int tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t data)
{
    (void) self;
    tx_active = 1U;
    if (tx_n < sizeof(tx_q)) {
        tx_q[tx_n++] = data;
    }
    return 0;
}

static void tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    tx_done_calls++;
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

static void timer_a_start(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    timer_a_starts++;
}

static void timer_a_stop(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    timer_a_stops++;
}

static void timer_b_start(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    TEST_TIMER_B_STARTED();
}

/* 仅当 PHY 能报告唤醒（dll_phy_has_wakeup()）时，DLL 才会启用 FALLBACK 迁移；
 * 因此提供该钩子，让快速路径上的 Fallback 命令
 * 走完整个迁移 + 截止时间流程。 */
static int mock_detect_wakeup(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
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
    (void) self;
    /* 宿主机没有中断源：暂存应答由用例的服务循环排空。 */
}

static void feed(const uint8_t* frame, size_t n)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    TEST_CHK_CLEAR();
}

static void feed_chk(const uint8_t* frame, size_t n)
{
    (void) memcpy(rx_q, frame, n);
    rx_n = n;
    rx_i = 0U;
    TEST_CHK_SET();
}

static void make_type2_process_read(uint8_t* frame, size_t len)
{
    frame[0] = IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK; /* READ */
    frame[1] = IOLINK_MSEQ_TYPE_2;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

static iolink_phy_api_t g_phy;

/* DPP1 页读需要绑定的设备身份：DLL 层没有默认身份。 */
static iolink_device_info_ctx_t s_identity_ctx;

static void bind_test_identity(iolink_dll_main_t* main)
{
    static const iolink_device_info_t identity = {
        .vendor_id = 1234U,
        .device_id = 5678U,
        .min_cycle_time = 10U,
        .revision_id = IOLINK_REVISION_ID_1_1,
    };
    assert_int_equal(iolink_device_info_ctx_init(&s_identity_ctx, &identity), 0);
    main->shared.b_isdu.device_info = &s_identity_ctx;
}

static void init_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.self = (iolink_app_t * IOLINK_NULLABLE) dll;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    g_phy.rx_frame = rx_frame;
#else
    g_phy.rx_octet = rx_octet;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    g_phy.tx_frame = tx_frame;
#else
    g_phy.tx_octet = tx_octet;
    g_phy.tx_done = tx_done;
#endif
    g_phy.int_active = int_active;
    g_phy.request_service = request_service;
    g_phy.response_timer_start = timer_a_start;
    g_phy.response_timer_stop = timer_a_stop;
    g_phy.stack_timer_start = timer_b_start;
    g_phy.detect_wakeup = mock_detect_wakeup;
    iolink_dll_init(&dll->main, &dll->isr, &g_phy);
    bind_test_identity(&dll->main);
    dll->main.shared.c_clock.time_us = test_time_us;
    dll->main.phy_mode = IOLINK_PHY_MODE_SDCI;
    dll->main.shared.b_state = IOLINK_DLL_STATE_OPERATE;
    dll->main.shared.c_m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll->main.shared.c_od_width = 1U;
    dll->main.shared.c_pd_out_len_current = 0U;
    dll->main.shared.c_pd_in_len = 0U;
    test_pd_set_valid(&dll->main, true);
    /* 计划表只在初始化时编译；本文件直接驱动 ISR 且在 init 之后才配置，
     * 故显式发布一次。 */
    iolink_dll_publish_reply_plans(&dll->main);
    now_us = 1000000U;
    rx_n = 0U;
    rx_i = 0U;
    tx_reset();
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    tx_active = 0U;
#endif
}

static void exchange(iolink_dll_ctx_t* dll, const uint8_t* frame, size_t len)
{
    feed(frame, len);
    iolink_dll_process_isr(&dll->main.shared);
    while ((dll->main.shared.b_isr_tx_pending != 0U) || (dll->main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll->main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    iolink_dll_process(&dll->main);
    while ((dll->main.shared.b_isr_tx_pending != 0U) || (dll->main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll->main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
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
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

static void make_type0(uint8_t mc, uint8_t* frame, size_t len)
{
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    for (size_t i = 2U; i < len; i++) {
        frame[i] = 0x00U;
    }
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, len));
}

static void init_type0_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.self = (iolink_app_t * IOLINK_NULLABLE) dll;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    g_phy.rx_frame = rx_frame;
#else
    g_phy.rx_octet = rx_octet;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    g_phy.tx_frame = tx_frame;
#else
    g_phy.tx_octet = tx_octet;
    g_phy.tx_done = tx_done;
#endif
    g_phy.int_active = int_active;
    g_phy.request_service = request_service;
    g_phy.response_timer_start = timer_a_start;
    g_phy.response_timer_stop = timer_a_stop;
    g_phy.stack_timer_start = timer_b_start;
    g_phy.detect_wakeup = mock_detect_wakeup;
    iolink_dll_init(&dll->main, &dll->isr, &g_phy);
    bind_test_identity(&dll->main);
    dll->main.shared.c_clock.time_us = test_time_us;
    dll->main.phy_mode = IOLINK_PHY_MODE_SDCI;
    dll->main.shared.b_state = IOLINK_DLL_STATE_PREOPERATE;
    dll->main.shared.c_m_seq_type = IOLINK_M_SEQ_TYPE_0;
    dll->main.shared.c_od_width = 1U;
    dll->main.shared.c_pd_out_len_current = 0U;
    dll->main.shared.c_pd_in_len = 0U;
    test_pd_set_valid(&dll->main, true);
    dll->main.shared.b_isdu.state = ISDU_STATE_RESPONSE_READY;
    dll->main.shared.b_isdu.resp_buf[0] = 0x11U;
    dll->main.shared.b_isdu.resp_buf[1] = 0x22U;
    dll->main.shared.b_isdu.resp_total = 2U;
    now_us = 1000000U;
    rx_n = 0U;
    rx_i = 0U;
    TEST_CHK_CLEAR();
    tx_reset();
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
    tx_active = 0U;
#endif
    iolink_dll_process(&dll->main); /* 同步 ISDU 快照 */
    /* 计划表在初始化时编译；本用例在 init 之后改了配置，因此显式重发一次
     * （ISR 尚未运行）。 */
    iolink_dll_publish_reply_plans(&dll->main);
}

/* PREOPERATE 下的 Type-0 ISDU 读由 ISR 直接应答。 */
static int test_type0_isdu_read_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    if (IOLINK_ISDU_PUB_LEN(dll.main.shared.m_isr_isdu_pub) != 2U) {
        return 1;
    }

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | IOLINK_FLOWCTRL_START),
               frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif

    if ((tx_observed_len() != 2U) || (tx_q[0] != 0x11U)) {
        return 2;
    }
    if (!iolink_ring_is_empty(&dll.main.shared.i_isr_frame_ring)) {
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
        tx_reset();
        feed_chk(frame, sizeof(frame));
        iolink_dll_process_isr(&dll.main.shared);
        while ((dll.main.shared.b_isr_tx_pending != 0U) ||
               (dll.main.shared.b_isr_tx_active != 0U)) {
            iolink_dll_process_isr(&dll.main.shared);
        }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        tx_frame_sync();
#endif
        if ((tx_observed_len() != 2U) || (tx_q[0] != 0x00U)) {
            return (int) (fc - 0x10U);
        }
        /* ISR 只回 0x00；传输层复位经一次性标志交给主泵。 */
        if (dll.main.shared.i_isr_isdu_read_reset == 0U) {
            return 0x40 + (int) (fc - 0x10U);
        }
        iolink_dll_process(&dll.main);
        if (dll.main.shared.i_isr_isdu_read_reset != 0U) {
            return 0x60 + (int) (fc - 0x10U);
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
    tx_reset();
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll->main.shared);
    while ((dll->main.shared.b_isr_tx_pending != 0U) || (dll->main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll->main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    /* IDLE/ABORT 由 ISR 回 0x00（CONSUME），复位请求经标志交给主泵；
       合法的 COUNT/START 同样由 ISR 应答且不发布帧。 */
    iolink_dll_process(&dll->main);
    while ((dll->main.shared.b_isr_tx_pending != 0U) || (dll->main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll->main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    /* 返回首字节即为查看该应答：把交付标记为已断言。 */
    (void) tx_observed_len();
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
    dll.main.shared.b_isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.main.shared.b_isdu.resp_total = (uint8_t) sizeof(response);
    (void) memcpy(dll.main.shared.b_isdu.resp_buf, response, sizeof(response));
    iolink_dll_process(&dll.main);
    if (IOLINK_ISDU_PUB_LEN(dll.main.shared.m_isr_isdu_pub) != (uint8_t) sizeof(response)) {
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
    dll.main.shared.b_isdu.state = ISDU_STATE_RESPONSE_READY;
    dll.main.shared.b_isdu.resp_total = (uint8_t) sizeof(response);
    (void) memcpy(dll.main.shared.b_isdu.resp_buf, response, sizeof(response));
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
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif

    const uint8_t expected = iolink_isdu_direct_param_page1_octet(&dll.main.shared.b_isdu, 0U);
    if ((tx_observed_len() != 2U) || (tx_q[0] != expected)) {
        return 1;
    }
    /* 该帧由 ISR 完全处理：计入消费，不进入邮箱。 */
    if (dll.main.shared.i_isr_frames_consumed != 1U) {
        return 2;
    }
    if (dll.main.shared.i_isr_frames_handoff != 0U) {
        return 3;
    }
    /* 公共统计与共享区一致（覆盖 get_stats 的两个拷贝）。 */
    iolink_dll_stats_t stats = {0};
    iolink_dll_get_stats(&dll.main, &stats);
    if ((stats.isr_frames_consumed != 1U) || (stats.isr_frames_handoff != 0U)) {
        return 4;
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
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, sizeof(frame)));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif

    if (tx_observed_len() != 1U) {
        return 1; /* 仅 CKS */
    }
    /* ISR 直接在 shared.b_isdu 中累计写请求（ST 的 StackBaseOdWrite 分工）：
     * 一个 Type-0 八位组只推进位置，不执行服务。 */
    if ((dll.main.shared.b_isdu.req_idx != 1U) || (dll.main.shared.b_isdu.req_buf[0] != 0x14U) ||
        (dll.main.shared.b_isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION)) {
        return 2;
    }

    /* 主窗口不会重复追加：请求未收齐时保持原状。 */
    iolink_dll_process(&dll.main);
    if ((dll.main.shared.b_isdu.req_idx != 1U) ||
        (dll.main.shared.b_isdu.stage_state != IOLINK_ISDU_STAGE_RECEPTION)) {
        return 5;
    }
    return 0;
}

/** @brief 向邮箱环发布一个帧（即数据 ISR 所做的事）。 */
static void push_mailbox(iolink_dll_ctx_t* dll, const uint8_t* frame, uint8_t len)
{
    iolink_frame_rec_t* rec =
        (iolink_frame_rec_t*) iolink_ring_claim(&dll->main.shared.i_isr_frame_ring);
    if (rec == NULL) {
        return;
    }
    rec->len = len;
    rec->eager = 0U;
    (void) memcpy(rec->data, frame, len);
    iolink_ring_publish(&dll->main.shared.i_isr_frame_ring);
}

/* 邮箱中已有帧：新的即时帧必须被记为丢弃。 */
static int test_mailbox_drop(void)
{
    iolink_dll_ctx_t dll;
    init_ctx(&dll);

    uint8_t frame[2];
    make_type2_process_read(frame, sizeof(frame));

    /* 填满全部槽位：此前的帧尚未被消费。 */
    for (uint8_t i = 0U; i < IOLINK_MBX_SLOTS; i++) {
        push_mailbox(&dll, frame, (uint8_t) sizeof(frame));
    }
    feed(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif

    if (dll.main.shared.i_isr_mailbox_drops != 1U) {
        return 1;
    }
    /* 即时应答不再依赖邮箱：邮箱满只会丢弃主泵的副本，
     * 不得抑制应答。 */
    if (tx_observed_len() != 2U) {
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
    iolink_dll_process_isr(&dll.main.shared);

    if (dll.main.shared.i_isr_eager_rejects != 1U) {
        return 1;
    }
    if (tx_observed_len() != 0U) {
        return 2; /* 损坏的帧不会得到即时应答 */
    }
    if (iolink_ring_count(&dll.main.shared.i_isr_frame_ring) != 1U) {
        return 3; /* 已发布，以便主泵校验或回退 */
    }
    /* 坏帧不被应答，但必须交给主泵：consumed 不变，handoff 加一。 */
    if (dll.main.shared.i_isr_frames_consumed != 0U) {
        return 4;
    }
    if (dll.main.shared.i_isr_frames_handoff != 1U) {
        return 5;
    }
    /* 同一计数经公共统计快照可见：主上下文无需访问 ISR 结构。 */
    iolink_dll_stats_t stats;
    iolink_dll_get_stats(&dll.main, &stats);
    if (stats.isr_eager_rejects != 1U) {
        return 6;
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
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif

    if ((dll.main.shared.i_isr_mailbox_drops != 0U) ||
        (dll.main.shared.i_isr_eager_rejects != 0U)) {
        return 1;
    }
    const iolink_frame_rec_t* rec =
        (const iolink_frame_rec_t*) iolink_ring_peek(&dll.main.shared.i_isr_frame_ring);
    if ((iolink_ring_count(&dll.main.shared.i_isr_frame_ring) != 1U) || (rec == NULL) ||
        (rec->eager != 1U)) {
        return 2;
    }
    /* 过程数据 READ 应答：OD 占位 + CKS。 */
    if (tx_observed_len() != 2U) {
        return 3;
    }
    /* ISR 应答了，但帧仍进入邮箱由主泵做副作用：不算消费，只算交接。 */
    if (dll.main.shared.i_isr_frames_consumed != 0U) {
        return 4;
    }
    if (dll.main.shared.i_isr_frames_handoff != 1U) {
        return 5;
    }
    /* 公共统计必须与共享区一致（覆盖 get_stats 的拷贝）。 */
    iolink_dll_stats_t stats = {0};
    iolink_dll_get_stats(&dll.main, &stats);
    if ((stats.isr_frames_consumed != dll.main.shared.i_isr_frames_consumed) ||
        (stats.isr_frames_handoff != dll.main.shared.i_isr_frames_handoff)) {
        return 6;
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
    iolink_dll_process_isr(&dll.main.shared);

    if (dll.main.shared.b_isdu.req_idx != 0U) {
        return 1;
    }
    if ((dll.main.shared.i_isr_eager_rejects != 1U) ||
        !iolink_ring_is_empty(&dll.main.shared.i_isr_frame_ring)) {
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
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        /* BYTE 传输按八位组累积，因此比较增量。 */
        const size_t len_before = tx_n;
#endif

        make_isdu_write(mc, isdu_octets[i], frame, sizeof(frame));
        exchange(&dll, frame, sizeof(frame));
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        const size_t ack_len = tx_observed_len() - len_before;
#else
        /* FRAME 传输每次交付覆盖捕获缓冲区。 */
        const size_t ack_len = tx_observed_len();
#endif
        /* 每个写段都被 ISR 以一个 CKS 八位组确认。 */
        if (ack_len != 1U) {
            return 5;
        }
    }

    /* ISR 恰好缓冲了每个八位组一次（主泵中不会重复追加）。 */
    if (dll.main.shared.b_isdu.req_idx != 4U) {
        return 1;
    }
    if (dll.main.shared.b_isdu.req_total != 4U) {
        return 2;
    }
    /* 四个写段都由 ISR 应答；帧仍要交给主泵执行，因此只计交接。 */
    if (dll.main.shared.i_isr_frames_consumed != 0U) {
        return 7;
    }
    if (dll.main.shared.i_isr_frames_handoff != 4U) {
        return 8;
    }
    /* 对只读索引的写会产生一个已组帧的否定响应。 */
    if ((dll.main.shared.b_isdu.state != ISDU_STATE_RESPONSE_READY) ||
        (IOLINK_ISDU_PUB_LEN(dll.main.shared.m_isr_isdu_pub) == 0U)) {
        return 3;
    }
    /* 请求收齐后主泵已接管：发布字已 ack（相位闩锁留在 COMPLETE）。 */
    if ((dll.main.shared.b_isdu.stage_state != IOLINK_ISDU_STAGE_COMPLETE) ||
        (dll.main.shared.b_isdu.req_ack_gen !=
         IOLINK_ISDU_REQ_PUB_GEN(dll.main.shared.b_isdu.req_pub))) {
        return 4;
    }
    return 0;
}

/* 阶段 3b：改变状态的 Type-0 命令在 ISR 中处理。 */
static int test_type0_commands_fast_path(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    uint8_t frame[3];

    /* 0x99 DeviceOperate（页写地址 0）：PREOP -> ESTAB_COM + 仅 CKS。 */
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_DEVICE_OPERATE;
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 3U));
    tx_reset();
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if ((dll.main.shared.b_state != IOLINK_DLL_STATE_ESTAB_COM) || (tx_observed_len() != 1U)) {
        return 1;
    }

    /* 0x98 PDOperate（page 写地址 0）：PREOP -> OPERATE + 仅 CKS。 */
    dll.main.shared.b_state = IOLINK_DLL_STATE_PREOPERATE;
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_PROCESS_DATA_OPERATE;
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 3U));
    tx_reset();
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if ((dll.main.shared.b_state != IOLINK_DLL_STATE_OPERATE) || (tx_observed_len() != 1U)) {
        return 2;
    }

    /* Fallback（0x5A）：FALLBACK + 仅 CKS。ISR 只标记该请求；
     * 64 位 T_FBD 截止时间由主泵根据 MinCycleTime 设置。 */
    dll.main.shared.b_state = IOLINK_DLL_STATE_PREOPERATE;
    frame[2] = IOLINK_CMD_FALLBACK;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 3U));
    tx_reset();
    dll_timer_cancel(&dll.main, IOLINK_TIMER_T_FBD);
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if ((dll.main.shared.b_state != IOLINK_DLL_STATE_FALLBACK) || (tx_observed_len() != 1U) ||
        (dll.main.shared.i_isr_fallback_pending == 0U) ||
        dll_timer_armed(&dll.main, IOLINK_TIMER_T_FBD)) {
        return 3;
    }
    iolink_dll_process(&dll.main);
    if ((dll.main.shared.i_isr_fallback_pending != 0U) ||
        !dll_timer_armed(&dll.main, IOLINK_TIMER_T_FBD)) {
        return 5;
    }

    /* 启动探测（STARTUP 下的 page 读）：PREOP + OD/CKS。 */
    dll.main.shared.b_state = IOLINK_DLL_STATE_STARTUP;
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK | 0x02U), frame, 2U);
    tx_reset();
    feed_chk(frame, 2U);
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if ((dll.main.shared.b_state != IOLINK_DLL_STATE_PREOPERATE) || (tx_observed_len() != 2U)) {
        return 4;
    }
    /* 四帧都由 ISR 定案：0x99/0x98 回 CKS、0x5A 回 CKS、探测回 OD/CKS。 */
    if (dll.main.shared.i_isr_frames_consumed != 4U) {
        return 5;
    }
    if (dll.main.shared.i_isr_frames_handoff != 0U) {
        return 6;
    }
    return 0;
}

/* 非 FALLBACK 的 MasterCommand 不得从 ISR 写入 64 位 T_FBD
 * 截止时间：ISR 只暂存一个清除请求，由主泵实施。 */
static int test_fallback_clear_is_staged(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    dll_timer_arm(&dll.main, IOLINK_TIMER_T_FBD, 0x0000000123456789ULL);

    uint8_t frame[3];
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_DEVICE_OPERATE;
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 3U));
    tx_reset();
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main.shared);

    if ((dll_timer_deadline(&dll.main, IOLINK_TIMER_T_FBD) != 0x0000000123456789ULL) ||
        (dll.main.shared.b_fallback_clear_request == 0U)) {
        return 1; /* ISR 触碰了该 64 位字段 */
    }
    iolink_dll_process(&dll.main);
    if (dll_timer_armed(&dll.main, IOLINK_TIMER_T_FBD) ||
        (dll.main.shared.b_fallback_clear_request != 0U)) {
        return 2; /* 主泵未实施已暂存的清除 */
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if (tx_observed_len() != 1U) {
        return 3; /* ISR 仍以仅 CKS 确认该命令 */
    }
    return 0;
}

/* 0x97 DeviceStartup：ISR 只登记一次性请求并按计划确认该写；
 * 协议错误记账（invalid_frames）与 STARTUP 迁移由下一个主泵周期完成。 */
static int test_device_startup_deferred_to_main(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    dll.main.shared.b_state = IOLINK_DLL_STATE_PREOPERATE;
    const uint32_t invalid_before = dll.main.invalid_frames;

    uint8_t frame[3];
    frame[0] = IOLINK_MC_CHANNEL_PAGE;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    frame[2] = IOLINK_CMD_DEVICE_STARTUP;
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, 3U));
    tx_reset();
    feed_chk(frame, 3U);
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if ((dll.main.shared.b_state != IOLINK_DLL_STATE_PREOPERATE) ||
        (dll.main.shared.i_isr_device_startup_requested == 0U) ||
        (dll.main.invalid_frames != invalid_before) || (tx_observed_len() != 1U)) {
        return 1; /* ISR 提前改状态、漏记账或漏确认 */
    }

    iolink_dll_process(&dll.main);
    if ((dll.main.shared.b_state != IOLINK_DLL_STATE_STARTUP) ||
        (dll.main.shared.i_isr_device_startup_requested != 0U) ||
        (dll.main.invalid_frames != (invalid_before + 1U))) {
        return 2; /* 主泵未消费该请求 */
    }
    return 0;
}

/* ISR 完全消费（CONSUME）的帧不直接写主上下文的主站活动字段：
 * ISR 只置 `shared.i_isr_master_activity`，由主泵在
 * `dll_take_isr_master_activity()` 中刷新 `master_last_activity_ms`、
 * `master_lost_count` 与 `master_lost`。 */
static int test_consume_frame_activity_is_main_only(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    dll.main.master_last_activity_ms = 0x1111U;
    dll.main.master_lost_count = 7U;
    dll.main.master_lost = true;

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | IOLINK_MC_RW_MASK), frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif
    if (tx_observed_len() != 2U) {
        return 1;
    }
    if (dll.main.shared.i_isr_frames_consumed != 1U) {
        return 2; /* 该场景必须走 CONSUME 路径 */
    }
    if (dll.main.shared.i_isr_master_activity != 1U) {
        return 3;
    }
    /* ISR 阶段不得改写主上下文字段。 */
    if ((dll.main.master_last_activity_ms != 0x1111U) || (dll.main.master_lost_count != 7U) ||
        !dll.main.master_lost) {
        return 4;
    }

    iolink_dll_process(&dll.main);
    if (dll.main.shared.i_isr_master_activity != 0U) {
        return 5; /* 主泵未消费活动标志 */
    }
    /* PREOPERATE 下没有别的路径写该时间戳：只有 take 路径会刷新它。 */
    if (dll.main.master_last_activity_ms != (uint32_t) (now_us / 1000U)) {
        return 6;
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
    TEST_TIMER_B_RESET();

    uint8_t frame[2];
    make_type0((uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_MC_RW_MASK | IOLINK_FLOWCTRL_START),
               frame, sizeof(frame));
    feed_chk(frame, sizeof(frame));
    iolink_dll_process_isr(&dll.main.shared);
    while ((dll.main.shared.b_isr_tx_pending != 0U) || (dll.main.shared.b_isr_tx_active != 0U)) {
        iolink_dll_process_isr(&dll.main.shared);
    }
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    tx_frame_sync();
#endif

    if (tx_observed_len() != 2U) {
        return 2; /* ISDU 读应答：第一个响应八位组 + CKS */
    }
    if ((timer_a_starts == 0U) ||
        (timer_a_stops == 0U)
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
        /* TX BYTE 在发送完最后一字节时由库重启 Timer B；TX FRAME 的
         * 帧结束由 PHY 的 DMA 完成中断自行处理，库不可观测。 */
        || (timer_b_starts == 0U)
#endif
    ) {
        return 1;
    }
    return 0;
}

/* 阶段 4a：Timer A 超时会中止部分帧并计入一次超时。
 *
 * 半帧是 RX BYTE 的中间状态：RX FRAME 的 PHY 从不交付截断前缀。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static int test_rx_timeout(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);
    timer_a_starts = 0U;
    timer_a_stops = 0U;

    const uint8_t partial[1] = {0x70U};
    feed(partial, sizeof(partial));
    iolink_dll_process_isr(&dll.main.shared);
    if ((timer_a_starts == 0U) || (dll.isr.isr_rx_cnt == 0U)) {
        return 1;
    }

    iolink_dll_on_rx_timeout(&dll.main.shared);
    if ((IOLINK_STAT_TOTAL(&dll.main, b_timeout_errors, timeout_errors) == 0U) ||
        (dll.isr.isr_rx_cnt != 0U) || (timer_a_stops == 0U)) {
        return 2;
    }
    return 0;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

/* 阶段 4a：Timer B tick 恰好授权一个主上下文窗口。 */
static int test_stack_tick_window(void)
{
    iolink_dll_ctx_t dll;
    init_type0_ctx(&dll);

    dll.isr.stack_tick_pending = 0U;
    if (iolink_dll_take_service_window(&dll.main) != 0U) {
        return 1; /* 尚无 tick（已安装硬件钩子） */
    }
    iolink_dll_on_stack_tick(&dll.main.shared);
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
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static int test_byte_gap_incomplete(void)
{
    /* 过短的 M 序列（少于两个头部八位组）。 */
    {
        iolink_dll_ctx_t dll;
        init_ctx(&dll);
        tx_done_calls = 0U;
        const uint8_t one[1] = {IOLINK_MC_CHANNEL_PROCESS};
        feed_chk(one, sizeof(one));
        iolink_dll_process_isr(&dll.main.shared);
        if (IOLINK_STAT_TOTAL(&dll.main, b_framing_errors, framing_errors) != 0U) {
            return 1; /* 过短的帧不算 framing 错误 */
        }
        if ((dll.isr.isr_rx_cnt != 0U) || (dll.isr.isr_rx_expect != 0U)) {
            return 2;
        }
        if (!iolink_ring_is_empty(&dll.main.shared.i_isr_frame_ring)) {
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
        iolink_dll_process_isr(&dll.main.shared);
        if (IOLINK_STAT_TOTAL(&dll.main, b_framing_errors, framing_errors) != 1U) {
            return 5;
        }
        if ((dll.isr.isr_rx_cnt != 0U) || (dll.isr.isr_rx_expect != 0U)) {
            return 6;
        }
        if (!iolink_ring_is_empty(&dll.main.shared.i_isr_frame_ring)) {
            return 7; /* 绝不为主泵发布 */
        }
        if (dll.main.shared.i_isr_eager_rejects != 0U) {
            return 8; /* 间隔路径不执行即时校验 */
        }
        if (tx_done_calls != 1U) {
            return 9;
        }
    }
    return 0;
}
#endif /* IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE */

IOLINK_TEST_RC(test_mailbox_drop)
IOLINK_TEST_RC(test_eager_reject)
IOLINK_TEST_RC(test_valid_eager)
IOLINK_TEST_RC(test_isdu_write_consumed_by_isr)
IOLINK_TEST_RC(test_isdu_write_bad_crc_not_consumed)
IOLINK_TEST_RC(test_type0_isdu_read_fast_path)
IOLINK_TEST_RC(test_0053_reserved_flowctrl_no_service)
IOLINK_TEST_RC(test_0309_isdu_segment_repeat)
IOLINK_TEST_RC(test_0068_aborted_read_response)
IOLINK_TEST_RC(test_type0_page_read_fast_path)
IOLINK_TEST_RC(test_type0_isdu_write_fast_path)
IOLINK_TEST_RC(test_type0_commands_fast_path)
IOLINK_TEST_RC(test_timer_interlock)
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
IOLINK_TEST_RC(test_rx_timeout)
#endif
IOLINK_TEST_RC(test_stack_tick_window)
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
IOLINK_TEST_RC(test_byte_gap_incomplete)
#endif
IOLINK_TEST_RC(test_fallback_clear_is_staged)
IOLINK_TEST_RC(test_device_startup_deferred_to_main)
IOLINK_TEST_RC(test_consume_frame_activity_is_main_only)

static int group_teardown(void** state)
{
    (void) state;
    if ((tx_asserted != tx_delivered) || (tx_leaked != 0U)) {
        print_message(
            "test_isr_observability: %llu replies delivered, %llu asserted, %llu discarded\n",
            (unsigned long long) tx_delivered, (unsigned long long) tx_asserted,
            (unsigned long long) tx_leaked);
        exit(EXIT_FAILURE); /* cmocka keeps exit 0 after a failed group teardown */
    }
    return 0;
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(iolink_case_test_mailbox_drop),
        cmocka_unit_test(iolink_case_test_eager_reject),
        cmocka_unit_test(iolink_case_test_valid_eager),
        cmocka_unit_test(iolink_case_test_isdu_write_consumed_by_isr),
        cmocka_unit_test(iolink_case_test_isdu_write_bad_crc_not_consumed),
        cmocka_unit_test(iolink_case_test_type0_isdu_read_fast_path),
        cmocka_unit_test(iolink_case_test_0053_reserved_flowctrl_no_service),
        cmocka_unit_test(iolink_case_test_0309_isdu_segment_repeat),
        cmocka_unit_test(iolink_case_test_0068_aborted_read_response),
        cmocka_unit_test(iolink_case_test_type0_page_read_fast_path),
        cmocka_unit_test(iolink_case_test_type0_isdu_write_fast_path),
        cmocka_unit_test(iolink_case_test_type0_commands_fast_path),
        cmocka_unit_test(iolink_case_test_timer_interlock),
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
        cmocka_unit_test(iolink_case_test_rx_timeout),
#endif
        cmocka_unit_test(iolink_case_test_stack_tick_window),
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
        cmocka_unit_test(iolink_case_test_byte_gap_incomplete),
#endif
        cmocka_unit_test(iolink_case_test_fallback_clear_is_staged),
        cmocka_unit_test(iolink_case_test_device_startup_deferred_to_main),
        cmocka_unit_test(iolink_case_test_consume_frame_activity_is_main_only),
    };
    return cmocka_run_group_tests(tests, NULL, group_teardown);
}
