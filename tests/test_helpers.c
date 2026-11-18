/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_helpers.c
 * @brief 共享测试工具与 mock 实现
 */

#include "test_helpers.h"
#include "dll_access.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "crc_oracle.h"
#include "iolinki/protocol.h"
#include "iolinki/port.h"

/* 测试缓冲区 */
uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

/* 设备层会拒绝库中弱定义的空操作临界区。 */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

/* mock 实现 */

static int g_mock_wakeup = 0;
static uint8_t g_mock_cq_state = 0U;
static uint32_t g_mock_send_delay_us = 0U;

/* 传输适配层：接收队列与应答捕获都汇聚到 mock_phy_send。
 *
 * 两种传输粒度共用同一份状态：
 *  - RX BYTE  ：rx_octet 逐个交付八位字节，CHK 标记表示帧结束/间隔。
 *  - RX FRAME ：rx_frame 一次交付一帧，CHK 标记记录该帧的结束位置。
 *  - TX BYTE  ：tx_octet 累积、tx_done 交付。
 *  - TX FRAME ：tx_frame 交付整帧；帧尾 CKS 由库在调用之后写入，
 *               因此交付推迟到下一次 PHY 交互（见 mock_tx_finish）。 */
#define MOCK_RXQ_SIZE 8192U
static uint8_t g_rxq[MOCK_RXQ_SIZE];
static size_t g_rxq_head;
static size_t g_rxq_tail;
static int g_rx_err_pending;
static uint8_t g_txq[1024];
static size_t g_txq_len;
static uint64_t g_tx_seq;
static uint64_t g_tx_asserted;
static uint64_t g_tx_discarded; /* frames dropped by mock_phy_clear_tx() */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static size_t g_chk_pending;
static uint8_t g_tx_active;
#else
/* 每个 CHK 标记记录一帧的结束位置；帧内容为 [g_rxq_tail, end)。 */
#define MOCK_RX_FRAMES 256U
static size_t g_rxq_frame_end[MOCK_RX_FRAMES];
static size_t g_rxq_frame_head;
static size_t g_rxq_frame_tail;
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
static const uint8_t* g_tx_pending; /* 等待帧结束的应答（库的缓冲区） */
static size_t g_tx_pending_len;
#endif

/* 假时钟：测试用例显式推进，使定时器判定与主机调度无关。 */
#define TEST_CLOCK_BASE_US 1000000ULL /* 非 0：0 在 DLL 中表示“未绑定时钟” */
static uint64_t g_test_clock_us = TEST_CLOCK_BASE_US;

static uint64_t test_clock_read_us(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return g_test_clock_us;
}

void test_clock_install(void)
{
    g_test_clock_us = TEST_CLOCK_BASE_US;
    g_iolink_test_time_us_override = test_clock_read_us;
}

void test_clock_advance_ms(uint32_t ms)
{
    g_test_clock_us += (uint64_t) ms * 1000ULL;
}

uint64_t test_clock_now_us(void)
{
    return g_test_clock_us;
}

void test_clock_uninstall(void)
{
    g_iolink_test_time_us_override = NULL;
    g_test_clock_us = TEST_CLOCK_BASE_US;
}

int mock_phy_init(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return (int) mock();
}

void mock_phy_set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    (void) self;
    check_expected(mode);
}

/* 差分门：设置 IOLINK_DIFF_TRACE 后，把设备发出的每个应答帧追加到该文件。
 * 正常 ctest 运行不受影响（环境变量缺省为空）。 */
static void trace_tx_frame(const uint8_t* data, size_t len)
{
    static const char* trace_path;
    static bool trace_ready;
    FILE* trace;

    if (!trace_ready) {
        trace_path = getenv("IOLINK_DIFF_TRACE");
        trace_ready = true;
    }
    if ((trace_path == NULL) || (data == NULL)) {
        return;
    }
    trace = fopen(trace_path, "ab");
    if (trace == NULL) {
        return;
    }
    for (size_t i = 0U; i < len; i++) {
        (void) fprintf(trace, "%02x", data[i]);
    }
    (void) fputc('\n', trace);
    (void) fclose(trace);
}

int mock_phy_send(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    (void) self;
    trace_tx_frame(data, len);
    g_tx_seq++;
    if (g_mock_send_delay_us > 0U) {
        if (g_iolink_test_time_us_override != NULL) {
            g_test_clock_us += g_mock_send_delay_us;
        }
        else {
            usleep(g_mock_send_delay_us);
        }
    }
    return (int) len;
}

int mock_phy_detect_wakeup(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    int ret = g_mock_wakeup;
    g_mock_wakeup = 0;
    return ret;
}

void mock_phy_set_cq_line(iolink_app_t* IOLINK_NULLABLE self, uint8_t state)
{
    (void) self;
    g_mock_cq_state = state;
}

/* 主上下文暂存应答后请求"数据中断"。宿主机测试没有真实中断源：
 * 中断由 test_io_cycle() 或用例显式调用 iolink_dll_process_isr() 模拟，
 * 而该入口在 int_active 门控之前就排空 b_isr_tx_pending。因此这里不
 * 驱动任何东西，也不得在本上下文中重入泵。 */
static void mock_phy_request_service(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
}

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
/* TX FRAME：库在 tx_frame() 返回之后才把帧尾 CKS 写进同一个缓冲区
 * （长应答的 DMA 时序，见 dll_isr_start_reply_frame），因此交付必须推迟到
 * 下一次 PHY 交互：下一次 tx_frame（上一帧已在线上完成）、数据中断
 * （DMA 完成）或断言/读取钩子。 */
static void mock_tx_finish(iolink_app_t* IOLINK_NULLABLE self)
{
    const uint8_t* data = g_tx_pending;
    size_t len = g_tx_pending_len;

    if (data == NULL) {
        return;
    }
    g_tx_pending = NULL;
    g_tx_pending_len = 0U;
    if (len > sizeof(g_txq)) {
        len = sizeof(g_txq);
    }
    /* 帧尾字节由库在 tx_frame() 返回后写入，这里补齐。 */
    if (len > 0U) {
        g_txq[len - 1U] = data[len - 1U];
    }
    g_txq_len = len;
    (void) mock_phy_send(self, g_txq, g_txq_len);
}
#endif

/* ---- 请求注入与应答读取（两种传输共用） -------------------------------- */
void mock_phy_feed_byte(uint8_t b)
{
    if (g_rxq_head < MOCK_RXQ_SIZE) {
        g_rxq[g_rxq_head++] = b;
    }
}
void mock_phy_feed_chk(void)
{
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    assert_true(g_rxq_frame_head < MOCK_RX_FRAMES);
    g_rxq_frame_end[g_rxq_frame_head++] = g_rxq_head;
#else
    g_chk_pending++;
#endif
}
void mock_phy_feed_error(void)
{
    g_rx_err_pending = 1;
}
void mock_phy_clear_rx(void)
{
    g_rxq_head = 0U;
    g_rxq_tail = 0U;
    g_rx_err_pending = 0;
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    g_rxq_frame_head = 0U;
    g_rxq_frame_tail = 0U;
#else
    g_chk_pending = 0U;
#endif
}
void mock_phy_clear_tx(void)
{
    /* 清空观察窗口。窗口里若还有未断言的帧，这就是一次静默丢弃：
     * teardown 会看到 g_tx_discarded 非零并失败。确实不关心的帧用
     * mock_phy_ignore_tx()。 */
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    mock_tx_finish(NULL); /* 帧模式下交付发生在下一次 PHY 交互，先结算 */
#endif
    g_tx_discarded += g_tx_seq - g_tx_asserted;
    g_tx_asserted = g_tx_seq;
    g_txq_len = 0U;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    g_tx_pending = NULL;
    g_tx_pending_len = 0U;
#endif
}

void mock_phy_ignore_tx(void)
{
    g_tx_asserted = g_tx_seq;
    g_txq_len = 0U;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    g_tx_pending = NULL;
    g_tx_pending_len = 0U;
#endif
}
const uint8_t* mock_phy_tx(size_t* len)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    mock_tx_finish(NULL);
#endif
    if (len != NULL) {
        *len = g_txq_len;
    }
    return g_txq;
}

static void print_hex_frame(const char* label, const uint8_t* data, size_t len)
{
    print_message("%s (len=%u):", label, (unsigned) len);
    for (size_t i = 0U; i < len; i++) {
        print_message(" %02X", (unsigned) data[i]);
    }
    print_message("\n");
}

/* 断言点恰好消费一帧：多帧未消费（重复应答）与零帧都失败。 */
static void tx_expect_one_delivery(const char* what)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 观察点即为帧结束：库写入的 CKS 已就位。 */
    mock_tx_finish(NULL);
#endif
    const uint64_t pending = g_tx_seq - g_tx_asserted;
    if (pending == 0U) {
        print_message("%s: no frame was delivered\n", what);
    }
    else if (pending > 1U) {
        print_message("%s: %u frames delivered, only one asserted\n", what, (unsigned) pending);
    }
    assert_int_equal(pending, 1U);
    g_tx_asserted = g_tx_seq;
}

void assert_tx_frame(const uint8_t* expected, size_t len)
{
    tx_expect_one_delivery("assert_tx_frame");
    if ((g_txq_len != len) || (memcmp(g_txq, expected, len) != 0)) {
        print_hex_frame("expected", expected, len);
        print_hex_frame("actual  ", g_txq, g_txq_len);
    }
    assert_int_equal(g_txq_len, len);
    assert_memory_equal(g_txq, expected, len);
}

void assert_tx_len(size_t len)
{
    tx_expect_one_delivery("assert_tx_len");
    if (g_txq_len != len) {
        print_hex_frame("actual", g_txq, g_txq_len);
    }
    assert_int_equal(g_txq_len, len);
}

void assert_no_tx(void)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 未交付的帧同样算"发送了"：先完成观察点再判断。 */
    mock_tx_finish(NULL);
#endif
    if (g_tx_seq != g_tx_asserted) {
        print_hex_frame("unexpected", g_txq, g_txq_len);
    }
    assert_int_equal(g_tx_seq, g_tx_asserted);
    g_tx_asserted = g_tx_seq;
}

/* group teardown：整组用例结束后仍有未断言的应答即失败。
 *
 * 计数器跨用例累积（setup_mock_phy 不再清零），因此任何一个用例
 * "既不 assert_tx_*、也不 assert_no_tx()"都会被这里发现；用例之间
 * 显式 mock_phy_clear_tx() 表示"这一帧已知且不关心"。 */
int test_tx_all_asserted(void** state)
{
    (void) state;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    mock_tx_finish(NULL);
#endif
    if ((g_tx_seq != g_tx_asserted) || (g_tx_discarded != 0U)) {
        if (g_tx_seq != g_tx_asserted) {
            print_hex_frame("unasserted", g_txq, g_txq_len);
        }
        /* cmocka 2.x reports a failed group teardown but still exits 0, so the
         * gate has to end the process itself. */
        fprintf(stderr, "FAIL: %llu replies delivered, %llu asserted, %llu discarded\n",
                (unsigned long long) g_tx_seq, (unsigned long long) g_tx_asserted,
                (unsigned long long) g_tx_discarded);
        fflush(stderr);
        exit(EXIT_FAILURE);
    }
    g_tx_asserted = g_tx_seq;
    return 0;
}

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_BYTE
static int mock_rx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t* data, uint8_t* status)
{
    (void) self;
    if (g_rxq_tail != g_rxq_head) {
        *data = g_rxq[g_rxq_tail++];
        *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if (g_rx_err_pending) {
        g_rx_err_pending = 0;
        *status = 0U;
        return -1;
    }
    if (g_chk_pending != 0U) {
        g_chk_pending--;
        *status = IOLINK_PHY_STATUS_CHK;
        return 0;
    }
    *status = 0U;
    return 0;
}
#else
/* RX FRAME：整帧交付。PHY 契约禁止交付帧头或截断前缀，
 * 因此只有带 CHK 标记的完整帧才会被返回。 */
static const uint8_t* mock_rx_frame(iolink_app_t* IOLINK_NULLABLE self, uint8_t* status,
                                    uint8_t* len)
{
    (void) self;
    if (len != NULL) {
        *len = 0U;
    }
    if (status != NULL) {
        *status = 0U;
    }
    if (g_rxq_frame_tail == g_rxq_frame_head) {
        if (g_rx_err_pending) {
            /* 接收错误：没有完整帧，状态字节不带 CHK。 */
            g_rx_err_pending = 0;
        }
        return NULL;
    }
    const size_t end = g_rxq_frame_end[g_rxq_frame_tail++];
    const size_t start = g_rxq_tail;
    g_rxq_tail = end;
    const size_t n = (end > start) ? (end - start) : 0U;
    if (n == 0U) {
        return NULL; /* 空帧：只有间隔，没有内容 */
    }
    /* PHY 的 len 字段只有一个八位字节；更长的帧是无法表达的测试错误。 */
    assert_true(n <= UINT8_MAX);
    if (status != NULL) {
        *status = (uint8_t) (IOLINK_PHY_STATUS_DATA_READY | IOLINK_PHY_STATUS_CHK);
    }
    if (len != NULL) {
        *len = (uint8_t) n;
    }
    return &g_rxq[start];
}
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_BYTE
static int mock_tx_octet(iolink_app_t* IOLINK_NULLABLE self, uint8_t b)
{
    (void) self;
    if (!g_tx_active) {
        g_txq_len = 0U; /* 新应答的开始 */
        g_tx_active = 1U;
    }
    if (g_txq_len < sizeof(g_txq)) {
        g_txq[g_txq_len++] = b;
    }
    return 0;
}

static void mock_tx_done(iolink_app_t* IOLINK_NULLABLE self)
{
    if (!g_tx_active) {
        /* CHK 路径复位，并非应答：无需交付。 */
        return;
    }
    g_tx_active = 0U;
    /* 把收集到的整帧交给用例断言的发送钩子。 */
    (void) mock_phy_send(self, g_txq, g_txq_len);
}
#else
static int mock_tx_frame(iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data, size_t len)
{
    if ((data == NULL) || (len == 0U) || (len > sizeof(g_txq))) {
        return -1; /* 与 PHY 契约一致：拒绝无法发送的帧 */
    }
    /* 上一帧在本帧开始前已传输完毕。 */
    mock_tx_finish(self);
    /* 立即复制除尾字节外的内容：库在 tx_frame() 返回之后才写入帧尾 CKS，
     * 因此捕获结果不依赖库的缓冲区在观察点之前保持不变。 */
    const size_t head = (len > 1U) ? (len - 1U) : 0U;
    (void) memcpy(g_txq, data, head);
    g_txq_len = len;
    g_tx_pending = data;
    g_tx_pending_len = len;
    return 0;
}

/* 可选钩子：帧交给 PHY 之后、DMA 完成之前线路仍被判为忙。 */
static uint8_t mock_tx_busy(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return (g_tx_pending != NULL) ? 1U : 0U;
}
#endif

static uint8_t mock_int_active(iolink_app_t* IOLINK_NULLABLE self)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    /* 数据中断即为 DMA 完成边沿：上一帧在此刻结束并交付。 */
    mock_tx_finish(self);
#else
    (void) self;
#endif
    if (g_rx_err_pending) {
        return 1U;
    }
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    /* 只报告完整帧；半帧不是 PHY 可交付的事件。 */
    return (g_rxq_frame_tail != g_rxq_frame_head) ? 1U : 0U;
#else
    if (g_tx_active) {
        return 1U;
    }
    if (g_rxq_tail != g_rxq_head) {
        return 1U;
    }
    if (g_chk_pending != 0U) {
        return 1U;
    }
    return 0U;
#endif
}

const iolink_phy_api_t g_phy_mock = {.self = NULL,
                                     .init = mock_phy_init,
                                     .set_mode = mock_phy_set_mode,
                                     .detect_wakeup = mock_phy_detect_wakeup,
                                     .set_cq_line = mock_phy_set_cq_line,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
                                     .rx_frame = mock_rx_frame,
#else
                                     .rx_octet = mock_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
                                     .tx_frame = mock_tx_frame,
                                     .tx_busy = mock_tx_busy,
#else
                                     .tx_octet = mock_tx_octet,
                                     .tx_done = mock_tx_done,
#endif
                                     .int_active = mock_int_active,
                                     .request_service = mock_phy_request_service};

/* 显式测试环境（替代进程级单例）。调用方拥有存储并负责初始化。 */
void test_params_init(iolink_test_params_t* params, iolink_device_info_ctx_t* info)
{
    if (params == NULL) {
        return;
    }
    (void) memset(params, 0, sizeof(*params));
    const iolink_device_info_t* def = (info != NULL) ? iolink_device_info_ctx_get(info) : NULL;
    if ((def != NULL) && (def->application_tag != NULL)) {
        size_t n = strlen(def->application_tag);
        if (n > 32U) {
            n = 32U;
        }
        (void) memcpy(params->app, def->application_tag, n);
        params->app_len = (uint8_t) n;
    }
    params->entries[0] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_APPLICATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "ApplicationSpecificTag", params->app, &params->app_len);
    params->entries[1] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_FUNCTION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "FunctionTag", params->fun, &params->fun_len);
    params->entries[2] = (iolink_od_entry_t) IOLINK_OD_BYTES_STORAGE(
        IOLINK_IDX_LOCATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
        IOLINK_OD_STORAGE_DS, 32U, "LocationTag", params->loc, &params->loc_len);
    params->od.entries = params->entries;
    params->od.count = 3U;
}

void test_params_factory_reset(iolink_test_params_t* params)
{
    if (params == NULL) {
        return;
    }
    params->app_len = 0U;
    params->fun_len = 0U;
    params->loc_len = 0U;
}

iolink_result_t test_params_read(const iolink_test_params_t* params, uint16_t index,
                                 uint8_t subindex, uint8_t* buffer, size_t max_len)
{
    if (params == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    const iolink_od_ctx_t ctx = {.app = &params->od};
    const iolink_od_entry_t* entry = iolink_od_find(&params->od, index, subindex);
    if (entry == NULL) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    return iolink_od_read_value(&ctx, entry, subindex, buffer, max_len);
}

iolink_result_t test_params_write(iolink_test_params_t* params, uint16_t index, uint8_t subindex,
                                  const uint8_t* data, size_t len)
{
    if (params == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    iolink_od_ctx_t ctx = {.app = &params->od};
    const iolink_od_entry_t* entry = iolink_od_find(&params->od, index, subindex);
    if (entry == NULL) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    return iolink_od_write_value(&ctx, entry, subindex, data, len);
}

iolink_od_app_t* test_params_od(iolink_test_params_t* params)
{
    return (params != NULL) ? &params->od : NULL;
}

iolink_od_app_t* test_default_tag_od(void)
{
    static iolink_test_params_t params;

    if (params.od.count == 0U) {
        iolink_device_info_ctx_t info;
        assert_int_equal(iolink_device_info_ctx_init(&info, iolink_test_identity()), 0);
        test_params_init(&params, &info);
    }
    return &params.od;
}

iolink_od_app_t test_od_with_tags(const iolink_od_app_t* app, const iolink_od_app_t* tags,
                                  iolink_od_entry_t* storage, size_t capacity)
{
    iolink_od_app_t merged;
    const iolink_od_app_t* tag_table = (tags != NULL) ? tags : test_default_tag_od();
    size_t n = 0U;

    if (app != NULL) {
        for (size_t i = 0U; (i < app->count) && (n < capacity); i++) {
            storage[n++] = app->entries[i];
        }
    }
    for (size_t i = 0U; (i < tag_table->count) && (n < capacity); i++) {
        storage[n++] = tag_table->entries[i];
    }
    merged.entries = storage;
    merged.count = n;
    return merged;
}

void test_env_init(iolink_test_env_t* env, const iolink_device_info_t* info)
{
    if (env == NULL) {
        return;
    }
    (void) memset(env, 0, sizeof(*env));
    assert_int_equal(
        iolink_device_info_ctx_init(&env->info, (info != NULL) ? info : iolink_test_identity()), 0);
    test_params_init(&env->params, &env->info);
}

void test_env_reset_params(iolink_test_env_t* env)
{
    if (env != NULL) {
        test_params_init(&env->params, &env->info);
    }
}

iolink_device_info_ctx_t* test_env_info(iolink_test_env_t* env)
{
    return (env != NULL) ? &env->info : NULL;
}

iolink_test_params_t* test_env_params(iolink_test_env_t* env)
{
    return (env != NULL) ? &env->params : NULL;
}

void test_isdu_bind_params(iolink_isdu_ctx_t* ctx, iolink_test_env_t* env)
{
    if ((ctx != NULL) && (env != NULL)) {
        ctx->od = &env->params.od;
        ctx->device_info = &env->info;
    }
}

void setup_mock_phy(void)
{
    /* 使用 -1 表示无限期望，避免用例提前退出时报错。 */
    expect_any_count(mock_phy_set_mode, mode, -1);

    /* mock_phy_init 的返回值由调用方通过 will_return 提供。 */
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
    g_txq_len = 0U;
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    mock_tx_finish(NULL); /* 上一用例遗留的待交付帧先计入，再由 teardown 判定 */
    g_tx_pending = NULL;
    g_tx_pending_len = 0U;
#else
    g_tx_active = 0U;
#endif
    /* g_tx_seq / g_tx_asserted 不清零：它们跨用例累积，由 group teardown
     * （test_tx_all_asserted）统一核对；用例显式 mock_phy_clear_tx() 时清零。 */
    mock_phy_clear_rx();
}

void test_pump_tx_idle(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    while ((test_dll_shared(ctx)->b_isr_tx_pending != 0U) ||
           (test_dll_shared(ctx)->b_isr_tx_active != 0U)) {
        iolink_device_process_isr(ctx);
    }
}

/* 为一个请求驱动两个泵：ISR 泵交付/组帧请求（并排空应答），随后主泵
 * 消费邮箱并运行协议。
 *
 * 主泵负责的应答由 dll_send() 暂存并发布 b_isr_tx_pending，然后通过
 * request_service() 请求数据中断。宿主机上的 mock 没有真实中断源，因此
 * test_pump_tx_idle() 用泵调用代替该中断：iolink_dll_process_isr() 在
 * int_active 门控之前就排空暂存应答。
 *
 * 两种传输都成立：TX FRAME 的应答在 PHY 交付整帧时即完成，
 * b_isr_tx_active 保持 0，轮询循环自然成为空转。 */
void test_io_cycle(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (test_dll_shared(ctx)->b_isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    /* 丢弃遗留的帧结束标记（BYTE 按长度完成帧后仍留一个 CHK；
     * FRAME 已随整帧消费）。 */
    mock_phy_clear_rx();
    iolink_device_process(ctx);
    test_pump_tx_idle(ctx);
}

uint64_t iolink_test_time_us(iolink_app_t* IOLINK_NULLABLE self)
{
    if (g_iolink_test_time_us_override != NULL) {
        return g_iolink_test_time_us_override(self);
    }
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL);
}

uint32_t iolink_test_time_ms(void)
{
    return (uint32_t) (iolink_test_time_us(NULL) / 1000U);
}

iolink_device_time_us_fn g_iolink_test_time_us_override = NULL;

/** @brief 测试用设备身份：覆盖全部标识字段，供不关心身份的用例使用。
 *
 * 这些值（VendorID 0xFFFF、Generic 名称、"DefaultTag"）刻意沿用被删除的
 * 库内默认身份，让既有 DPP1/DS 字节断言保持原样；这是测试夹具的选择，
 * 不是库的默认值。
 */
static const iolink_device_info_t g_test_identity = {
    .vendor_name = "iolinki",
    .vendor_text = "Open-Source IO-Link Stack",
    .product_name = "Generic IO-Link Device",
    .product_id = "IOLINK-DEV-001",
    .product_text = "Reference Implementation",
    .serial_number = "0000000001",
    .hardware_revision = "1.0",
    .firmware_revision = "0.1.0",
    .application_tag = "DefaultTag",
    .vendor_id = 0xFFFFU,
    .device_id = 0x00000001U,
    .function_id = 0x0000U,
    .profile_characteristic = 0x0000U,
    .min_cycle_time = 10U,
    .revision_id = IOLINK_REVISION_ID_1_1,
    .device_status = 0x00U,
    .detailed_device_status = 0x0000U,
    .access_locks = 0x0000U,
};

const iolink_device_info_t* iolink_test_identity(void)
{
    return &g_test_identity;
}

/** @brief 未显式提供 trait 的测试使用的默认应用表（只有时钟）。 */
static const iolink_device_app_t g_default_app = {.self = NULL, .time_us = iolink_test_time_us};
static const iolink_device_app_t g_app_without_clock = {.self = NULL};

const iolink_device_app_t* iolink_test_default_app(void)
{
    return &g_default_app;
}

const iolink_device_app_t* iolink_test_app_without_clock(void)
{
    return &g_app_without_clock;
}

static void test_device_fill_config(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                                    const iolink_device_app_t* app)
{
    (void) memset(dev, 0, sizeof(*dev));
    dev->cfg.phy = &g_phy_mock;
    dev->cfg.app = (app != NULL) ? app : &g_default_app;
    /* 三个标签由应用以 STORAGE 表项提供；初值取自测试身份。 */
    iolink_device_info_ctx_t info;
    assert_int_equal(iolink_device_info_ctx_init(&info, iolink_test_identity()), 0);
    test_params_init(&dev->params, &info);
    dev->cfg.device_info = iolink_test_identity();
    dev->cfg.od = &dev->params.od;
    if (stack != NULL) {
        dev->stack = *stack;
    }
    else {
        dev->stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
        dev->stack.min_cycle_time = 0U;
        dev->stack.od_width = 1U;
    }
    dev->cfg.stack = &dev->stack;
}

int iolink_test_device_init(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                            const iolink_device_app_t* app)
{
    if (dev == NULL) {
        return -1;
    }

    test_device_fill_config(dev, stack, app);
    return iolink_device_init(&dev->ctx, &dev->cfg);
}

int iolink_test_device_init_with_od(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                                    const iolink_device_app_t* app, const iolink_od_app_t* od)
{
    if (dev == NULL) {
        return -1;
    }

    test_device_fill_config(dev, stack, app);
    dev->cfg.od = od;
    return iolink_device_init(&dev->ctx, &dev->cfg);
}

void move_to_operate_ctx(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* STARTUP -> AWAITING_COMM（通过 PHY 唤醒）。时序强制处于关闭状态，
     * 因此无需墙钟 T_DWU 等待：泵是事件驱动的。 */
    g_mock_wakeup = 1;
    test_io_cycle(ctx);

    /* AWAITING_COMM -> PREOPERATE（转换命令 MC=0x0F）。 */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    mock_phy_feed_byte(mc);
    mock_phy_feed_byte(ck);
    mock_phy_feed_chk();
    test_io_cycle(ctx);

    /* PREOPERATE/ESTAB_COM -> OPERATE（该类型下的第一个有效帧）。 */
    iolink_m_seq_type_t type = iolink_device_get_m_seq_type(ctx);
    uint8_t pd_out_len = iolink_device_get_pd_out_len(ctx);
    uint8_t pd_in_len = iolink_device_get_pd_in_len(ctx);
    uint8_t od_w = test_dll_shared(ctx)->c_od_width; /* 实际的 OPERATE OD 宽度（1/2/8/32） */

    if (type == IOLINK_M_SEQ_TYPE_0) {
        uint8_t idle_mc = 0x00;
        uint8_t idle_ck = test_frame_checksum(idle_mc);
        mock_phy_feed_byte(idle_mc);
        mock_phy_feed_byte(idle_ck);
        mock_phy_feed_chk();

        test_io_cycle(ctx);
        assert_tx_len(1); /* Type-0 写应答仅 CKS */
        mock_phy_clear_tx();
        return;
    }

    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    /* Figure A.2：主站报文为 MC、CKT、data……，A.1.6 校验和位于
       CKT 八位字节（字节 1）中；没有尾随的校验和八位字节。
       使用 WRITE M 序列：其请求布局（MC、CKT、PD-out、OD）与
       应答（PD-in、CKS）固定，而 READ 请求是否携带 OD 取决于
       M-sequence 类型。 */
    uint8_t frame_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + pd_out_len + od_w);
    frame[0] = 0x00;
    switch (type) {
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            frame[1] = IOLINK_MSEQ_TYPE_1;
            break;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_3:
        case IOLINK_M_SEQ_TYPE_2_4:
        case IOLINK_M_SEQ_TYPE_2_5:
        case IOLINK_M_SEQ_TYPE_2_V:
            frame[1] = IOLINK_MSEQ_TYPE_2;
            break;
        default:
            frame[1] = IOLINK_MSEQ_TYPE_0;
            break;
    }
    frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, frame_len));

    for (uint8_t i = 0U; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* WRITE 应答只携带 PD-in 与 CKS。 */
    uint8_t resp_len = (uint8_t) (pd_in_len + 1U);

    test_io_cycle(ctx);
    assert_tx_len(resp_len);
    mock_phy_clear_tx();
}

void iolink_phy_mock_reset(void)
{
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
}

void iolink_phy_mock_set_wakeup(int detected)
{
    g_mock_wakeup = detected;
}

uint8_t iolink_phy_mock_get_cq_state(void)
{
    return g_mock_cq_state;
}

void iolink_phy_mock_set_send_delay_us(uint32_t delay_us)
{
    g_mock_send_delay_us = delay_us;
}

/* ISDU V1.1.5 交织格式辅助函数 */

static uint8_t helper_chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

/** @brief 通过规范传输（C3）送入一条完整的 ISDU 请求。 */
static void helper_send_isdu(iolink_isdu_ctx_t* ctx, uint8_t* buf, size_t total)
{
    buf[total - 1U] = 0x00U;
    buf[total - 1U] = helper_chkpdu(buf, total);
    iolink_isdu_od_write(ctx, IOLINK_FLOWCTRL_START, buf, (uint8_t) total);
}

int isdu_send_read_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    uint8_t buf[8];
    size_t n = 0U;

    if (index > 0xFFU) {
        /* 16 位索引与 8 位子索引（I-Service 0xB，Length 0x5）。 */
        buf[n++] = 0xB5U;
        buf[n++] = (uint8_t) (index >> 8);
        buf[n++] = (uint8_t) (index & 0xFFU);
        buf[n++] = subindex;
    }
    else if (subindex != 0U) {
        /* 8 位索引与 8 位子索引（I-Service 0xA，Length 0x4）。 */
        buf[n++] = 0xA4U;
        buf[n++] = (uint8_t) index;
        buf[n++] = subindex;
    }
    else {
        /* 8 位索引（I-Service 0x9，Length 0x3）。 */
        buf[n++] = 0x93U;
        buf[n++] = (uint8_t) index;
    }

    helper_send_isdu(ctx, buf, n + 1U);
    return 1;
}

int isdu_send_write_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                            const uint8_t* data, uint8_t data_len)
{
    uint8_t buf[IOLINK_ISDU_BUFFER_SIZE];
    size_t n = 0U;

    /* 不含 ExtLength 八位字节的八位字节计数。 */
    size_t base_total;
    if (index > 0xFFU) {
        base_total = (size_t) data_len + 1U /* I-Service */ + 2U /* 索引 */ + 1U /* 子索引 */ + 1U;
    }
    else if (subindex != 0U) {
        base_total = (size_t) data_len + 1U + 1U /* 索引 */ + 1U /* 子索引 */ + 1U;
    }
    else {
        base_total = (size_t) data_len + 1U + 1U /* 索引 */ + 1U;
    }

    const bool ext = (base_total > 15U);
    const size_t total = ext ? (base_total + 1U) : base_total;
    const uint8_t base = (index > 0xFFU) ? 0x30U : ((subindex != 0U) ? 0x20U : 0x10U);
    buf[n++] = (uint8_t) (base | (ext ? 0x01U : (uint8_t) total));
    if (ext) {
        buf[n++] = (uint8_t) total;
    }
    if (index > 0xFFU) {
        buf[n++] = (uint8_t) (index >> 8);
        buf[n++] = (uint8_t) (index & 0xFFU);
        buf[n++] = subindex;
    }
    else {
        buf[n++] = (uint8_t) index;
        if (subindex != 0U) {
            buf[n++] = subindex;
        }
    }
    for (uint8_t i = 0U; i < data_len; i++) {
        buf[n++] = data[i];
    }

    helper_send_isdu(ctx, buf, total);
    return 1;
}

int isdu_collect_response(iolink_isdu_ctx_t* ctx, uint8_t* buffer, size_t buffer_size)
{
    size_t idx = 0;
    uint8_t byte;

    if (ctx == NULL || ctx->state != ISDU_STATE_RESPONSE_READY) return -1;

    while (idx < buffer_size && iolink_isdu_get_response_byte(ctx, &byte) > 0) {
        buffer[idx++] = byte;
    }

    return (int) idx;
}
