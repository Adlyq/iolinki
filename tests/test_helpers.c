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
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "iolinki/platform.h"

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

/* 传输适配层：两种传输都从 g_rxq 读取，并汇聚到 mock_phy_send。 */
#define MOCK_RXQ_SIZE 8192U
static uint8_t g_rxq[MOCK_RXQ_SIZE];
static size_t g_rxq_head;
static size_t g_rxq_tail;
static size_t g_chk_pending;
static int g_rx_err_pending;
static uint8_t g_txq[1024];
static size_t g_txq_len;
static uint8_t g_tx_active;
int mock_phy_init(void* user)
{
    (void) user;
    return (int) mock();
}

void mock_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    check_expected(mode);
}

void mock_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    check_expected(baudrate);
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

int mock_phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    check_expected_ptr(data);
    check_expected(len);
    trace_tx_frame(data, len);
    if (g_mock_send_delay_us > 0U) {
        usleep(g_mock_send_delay_us);
    }
    return (int) mock();
}

int mock_phy_detect_wakeup(void* user)
{
    (void) user;
    int ret = g_mock_wakeup;
    g_mock_wakeup = 0;
    return ret;
}

void mock_phy_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    g_mock_cq_state = state;
}

/* ---- 原始八位字节 ISR 传输（RX BYTE / TX BYTE） ------------------------ */
void mock_phy_feed_byte(uint8_t b)
{
    if (g_rxq_head < MOCK_RXQ_SIZE) {
        g_rxq[g_rxq_head++] = b;
    }
}
void mock_phy_feed_chk(void)
{
    g_chk_pending++;
}
void mock_phy_feed_error(void)
{
    g_rx_err_pending = 1;
}
void mock_phy_clear_rx(void)
{
    g_rxq_head = 0U;
    g_rxq_tail = 0U;
    g_chk_pending = 0U;
    g_rx_err_pending = 0;
}
void mock_phy_clear_tx(void)
{
    g_txq_len = 0U;
}
const uint8_t* mock_phy_tx(size_t* len)
{
    if (len != NULL) {
        *len = g_txq_len;
    }
    return g_txq;
}

static int mock_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;
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

static int mock_tx_octet(void* user, uint8_t b)
{
    (void) user;
    if (!g_tx_active) {
        g_txq_len = 0U; /* 新应答的开始 */
        g_tx_active = 1U;
    }
    if (g_txq_len < sizeof(g_txq)) {
        g_txq[g_txq_len++] = b;
    }
    return 0;
}

static void mock_tx_done(void* user)
{
    if (!g_tx_active) {
        /* CHK 路径复位，并非应答：无需交付。 */
        return;
    }
    g_tx_active = 0U;
    /* 将收集到的应答交给流式传输所用的同一期望。 */
    (void) mock_phy_send(user, g_txq, g_txq_len);
}

static uint8_t mock_int_active(void* user)
{
    (void) user;
    if (g_tx_active) {
        return 1U;
    }
    if (g_rxq_tail != g_rxq_head) {
        return 1U;
    }
    if (g_rx_err_pending) {
        return 1U;
    }
    if (g_chk_pending != 0U) {
        return 1U;
    }
    return 0U;
}

const iolink_phy_api_t g_phy_mock = {.user = NULL,
                                     .init = mock_phy_init,
                                     .set_mode = mock_phy_set_mode,
                                     .set_baudrate = mock_phy_set_baudrate,
                                     .detect_wakeup = mock_phy_detect_wakeup,
                                     .set_cq_line = mock_phy_set_cq_line,
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
                                     .rx_frame = NULL,
#else
                                     .rx_octet = mock_rx_octet,
#endif
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
                                     .tx_frame = NULL,
#else
                                     .tx_octet = mock_tx_octet,
#endif
                                     .tx_done = mock_tx_done,
                                     .int_active = mock_int_active};

/* 显式测试环境（替代进程级单例）。调用方拥有存储并负责初始化。 */
void test_env_init(iolink_test_env_t* env, const iolink_device_info_t* info)
{
    if (env == NULL) {
        return;
    }
    (void) memset(env, 0, sizeof(*env));
    iolink_device_info_ctx_init(&env->info, info);
    iolink_params_ctx_init(&env->params, &env->info);
}

void test_env_reset_params(iolink_test_env_t* env)
{
    if (env != NULL) {
        iolink_params_ctx_init(&env->params, &env->info);
    }
}

iolink_device_info_ctx_t* test_env_info(iolink_test_env_t* env)
{
    return (env != NULL) ? &env->info : NULL;
}

iolink_params_ctx_t* test_env_params(iolink_test_env_t* env)
{
    return (env != NULL) ? &env->params : NULL;
}

void test_isdu_bind_params(iolink_isdu_ctx_t* ctx, iolink_test_env_t* env)
{
    if ((ctx != NULL) && (env != NULL)) {
        ctx->params_ctx = &env->params;
    }
}

void setup_mock_phy(void)
{
    /* 使用 -1 表示无限期望，避免用例提前退出时报错。 */
    expect_any_count(mock_phy_set_mode, mode, -1);
    expect_any_count(mock_phy_set_baudrate, baudrate, -1);

    /* 此处不提供默认 will_return。测试必须自行提供。 */
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
    g_txq_len = 0U;
    g_tx_active = 0U;
    mock_phy_clear_rx();
}

/* 为一个请求驱动两个泵：ISR 泵对排队的八位字节组帧（并
 * 排空应答），随后主泵消费邮箱并运行
 * 协议（Type-0 应答由主泵发送）。在流式构建中，
 * 一次主泵遍历直接排空 g_rxq。 */
void test_io_cycle(iolink_device_ctx_t* ctx)
{
    iolink_device_process_isr(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
    /* 丢弃遗留的帧结束标记（Type-1/2 帧按长度完成）。 */
    mock_phy_clear_rx();
    iolink_device_process(ctx);
    while (ctx->dll.isr_tx_active != 0U) {
        iolink_device_process_isr(ctx);
    }
}

uint64_t iolink_test_time_us(void* user)
{
    (void) user;
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

int iolink_test_device_init(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                            const iolink_app_callbacks_t* callbacks)
{
    if (dev == NULL) {
        return -1;
    }

    (void) memset(dev, 0, sizeof(*dev));
    dev->cfg.phy = g_phy_mock;
    dev->cfg.time_us = (g_iolink_test_time_us_override != NULL) ? g_iolink_test_time_us_override
                                                                : iolink_test_time_us;
#if IOLINK_FEATURE_DATA_STORAGE
    dev->cfg.storage = &g_ds_storage_mock;
#endif
    if (stack != NULL) {
        dev->cfg.stack = *stack;
    }
    else {
        dev->cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
        dev->cfg.stack.min_cycle_time = 0U;
    }
    dev->cfg.app_callbacks = callbacks;

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
    uint8_t od_w = ctx->dll.od_width; /* 实际的 OPERATE OD 宽度（1/2/8/32） */

    if (type == IOLINK_M_SEQ_TYPE_0) {
        uint8_t idle_mc = 0x00;
        uint8_t idle_ck = test_frame_checksum(idle_mc);
        mock_phy_feed_byte(idle_mc);
        mock_phy_feed_byte(idle_ck);
        mock_phy_feed_chk();

        expect_any(mock_phy_send, data);
        expect_value(mock_phy_send, len, 1); /* Type-0 写应答仅 CKS */
        will_return(mock_phy_send, 0);
        test_io_cycle(ctx);
        return;
    }

    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    /* Figure A.2：主站报文为 MC、CKT、data……，A.1.6 校验和位于
       CKT 八位字节（字节 1）中；没有尾随的校验和八位字节。
       使用 WRITE M 序列：其请求布局（MC、CKT、PD-out、OD）与
       应答（PD-in、CKS）在两种传输上完全相同，而 READ
       请求的 OD 是否存在则不同。 */
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
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, frame_len));

    for (uint8_t i = 0U; i < frame_len; i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();

    /* WRITE 应答只携带 PD-in 与 CKS。 */
    uint8_t resp_len = (uint8_t) (pd_in_len + 1U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, resp_len);
    will_return(mock_phy_send, 0);

    test_io_cycle(ctx);
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

/* 用于数据存储（DS）测试的 mock 存储 */
#include "iolinki/data_storage.h"
#define DS_MOCK_SIZE ((size_t) IOLINK_STORAGE_TOTAL_SIZE)
static uint8_t g_ds_mock_buf[DS_MOCK_SIZE];

int ds_mock_read(void* user, uint32_t addr, uint8_t* buf, size_t len)
{
    (void) user;
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(buf, &g_ds_mock_buf[addr], len);
    return 0;
}

int ds_mock_write(void* user, uint32_t addr, const uint8_t* buf, size_t len)
{
    (void) user;
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(&g_ds_mock_buf[addr], buf, len);
    return 0;
}

const iolink_storage_api_t g_ds_storage_mock = {
    .user = NULL, .read = ds_mock_read, .write = ds_mock_write};

void iolink_ds_mock_reset(void)
{
    memset(g_ds_mock_buf, 0, DS_MOCK_SIZE);
}

uint8_t* iolink_ds_mock_get_buf(void)
{
    return g_ds_mock_buf;
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
