/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * TX FRAME 线路状态钩子（tx_busy）：
 *  - 当 PHY 报告线路繁忙（前一帧仍在线路上）时，即时（eager）ISR 应答
 *    被推迟到主泵处理；
 *  - 一旦 PHY 报告线路空闲，PD-in 应答缓冲区租约立即释放，
 *    因此两个缓冲区就足够；
 *  - 带该钩子的 TX_FRAME 构建使用两个 PD-in 缓冲区。
 *
 * 针对库的 FRAME/FRAME 副本构建，并定义
 * IOLINK_PHY_FEATURE_TX_BUSY=1（见 tests/CMakeLists.txt）。
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/crc.h"
#include "iolinki/device.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/port.h"
#include "iolinki/protocol.h"

static uint64_t now_us;

static uint64_t test_time_us(void* user)
{
    (void) user;
    return now_us;
}

static uint8_t g_frame[8];
static uint8_t g_frame_len;
static uint8_t g_frame_ready;
static unsigned g_tx_calls;
static uint8_t g_line_busy;

static const uint8_t* mock_rx_frame(void* user, uint8_t* status, uint8_t* len)
{
    (void) user;
    if (g_frame_ready == 0U) {
        *status = 0U;
        *len = 0U;
        return NULL;
    }
    g_frame_ready = 0U;
    *status = IOLINK_PHY_STATUS_DATA_READY;
    *len = g_frame_len;
    return g_frame;
}

static int mock_tx_frame(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    g_tx_calls++;
    return 0;
}

static uint8_t mock_tx_busy(void* user)
{
    (void) user;
    return g_line_busy;
}

static uint8_t mock_int_active(void* user)
{
    (void) user;
    return g_frame_ready;
}

static void mock_timer_start(void* user)
{
    (void) user;
}

static void mock_timer_stop(void* user)
{
    (void) user;
}

static void mock_request_service(void* user)
{
    (void) user;
}

static iolink_phy_api_t g_phy = {
    .user = NULL,
    .rx_frame = mock_rx_frame,
    .tx_frame = mock_tx_frame,
    .tx_busy = mock_tx_busy,
    .int_active = mock_int_active,
    .response_timer_start = mock_timer_start,
    .response_timer_stop = mock_timer_stop,
    .request_service = mock_request_service,
};

/* OPERATE 下的 Type-2_V 过程数据读：MC + CKT（仅头部，PD-out = 0）。 */
static void feed_process_read(void)
{
    g_frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_PROCESS | IOLINK_MC_RW_MASK);
    g_frame[1] = IOLINK_MSEQ_TYPE_2;
    g_frame[1] = (uint8_t) (g_frame[1] | iolink_checksum6(g_frame, 2U));
    g_frame_len = 2U;
    g_frame_ready = 1U;
}

static void init_ctx(iolink_dll_ctx_t* dll)
{
    g_phy.user = dll;
    iolink_dll_init(&dll->main, &dll->isr, &g_phy);
    dll->main.shared.time_us = test_time_us;
    dll->main.phy_mode = IOLINK_PHY_MODE_SDCI;
    dll->main.shared.state = IOLINK_DLL_STATE_OPERATE;
    dll->main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    dll->main.shared.od_width = 1U;
    dll->main.shared.pd_out_len_current = 0U;
    dll->main.pd_in_len_current = 0U;
    dll->main.pd_valid = true;
    g_frame_ready = 0U;
    g_tx_calls = 0U;
    g_line_busy = 0U;
    now_us = 1000000U;
}

/* 繁忙的线路不得被覆盖：ISR 放弃处理，由主泵接管
 * 该帧，待线路空闲后再发送。 */
static void test_busy_defers_eager_reply(void)
{
    iolink_dll_ctx_t dll;

    init_ctx(&dll);
    g_line_busy = 1U;
    feed_process_read();

    iolink_dll_process_isr(&dll.main);
    assert(g_tx_calls == 0U);                     /* 即时应答被放弃 */
    assert(dll.main.shared.isr_tx_pending == 0U); /* ISR 未暂存任何内容 */
    assert(dll.main.shared.pd_in_captured == 0xFFU);

    iolink_dll_process(&dll.main);                /* 主泵应答该帧 */
    assert(dll.main.shared.isr_tx_pending == 1U); /* 已暂存，等待 ISR */

    g_line_busy = 0U;
    iolink_dll_process_isr(&dll.main); /* 排空已暂存的应答 */
    assert(g_tx_calls == 1U);
    assert(dll.main.shared.isr_tx_pending == 0U);
}

/* 传输仍在线路上时，应答缓冲区的租约保持占用；一旦 tx_busy()
 * 报告线路空闲，就由主泵释放。 */
static void test_lease_released_when_line_free(void)
{
    iolink_dll_ctx_t dll;

    init_ctx(&dll);
    feed_process_read();

    iolink_dll_process_isr(&dll.main); /* 由 ISR 发出即时应答 */
    assert(g_tx_calls == 1U);
    assert(dll.main.shared.pd_in_captured != 0xFFU);

    g_line_busy = 1U; /* 仍在线路上 */
    iolink_dll_process(&dll.main);
    assert(dll.main.shared.pd_in_captured != 0xFFU);

    g_line_busy = 0U; /* PHY 观察到传输完成 */
    iolink_dll_process(&dll.main);
    assert(dll.main.shared.pd_in_captured == 0xFFU);
}

/* 没有该钩子时，库无法观测 TX FRAME 传输的结束，
 * 因此会保留第三个缓冲区。 */
IOLINK_STATIC_ASSERT(IOLINK_PD_IN_BUFFERS == 2U, "tx_busy build must use two PD-in buffers");

/* IOLINK_PHY_FEATURE_TX_BUSY=1 承诺通过 tx_busy() 释放租约；不带该钩子的 PHY
 * 必须在设备初始化时被拒绝，而不是静默地
 * 让 PD-in 缓冲区被永久占用。 */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t device_time_us(void* user)
{
    (void) user;
    return 1000000ULL;
}

static int storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

static int storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    (void) offset;
    (void) buf;
    (void) len;
    return 0;
}

static const iolink_storage_api_t g_dev_storage = {.read = storage_read, .write = storage_write};

static void test_device_init_requires_tx_busy(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;

    (void) memset(&cfg, 0, sizeof(cfg));
    cfg.phy = g_phy;
    cfg.phy.tx_busy = NULL;
#if IOLINK_FEATURE_DATA_STORAGE
    cfg.storage = &g_dev_storage;
#endif
    cfg.time_us = device_time_us;
    assert(iolink_device_init(&ctx, &cfg) == IOLINK_DEVICE_INIT_ERR_PHY);

    cfg.phy.tx_busy = mock_tx_busy;
    assert(iolink_device_init(&ctx, &cfg) == 0);
}

int main(void)
{
    test_busy_defers_eager_reply();
    test_lease_released_when_line_free();
    test_device_init_requires_tx_busy();
    return 0;
}
