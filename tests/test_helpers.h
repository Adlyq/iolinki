/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_helpers.h
 * @brief 共享测试工具与 mock 实现
 */

#ifndef TEST_HELPERS_H_
#define TEST_HELPERS_H_

#include <stdint.h>
#include <stddef.h>
#include "iolinki/iolink.h"
#include "iolinki/device.h"
#include "iolinki/phy.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"

/* 测试缓冲区 */
extern uint8_t g_tx_buf[1024];
extern uint8_t g_rx_buf[1024];

/* 需要真实时间源的测试所使用的单调主机时钟。 */
uint64_t iolink_test_time_us(void* user);
uint32_t iolink_test_time_ms(void);
/* 由驱动受控时钟的测试使用的可选覆盖项。 */
extern iolink_device_time_us_fn g_iolink_test_time_us_override;

/* 为 2 个八位字节的报文 [octet, checksum-with-low-bits-zero] 构造校验和。
 * 供用例构造主站帧与设备应答时使用（A.1.6）。 */
static inline uint8_t test_frame_checksum(uint8_t octet)
{
    uint8_t frame[2] = {octet, 0x00U};
    return iolink_checksum6(frame, 2U);
}

/* mock 实现（导出以供 CMocka 符号使用） */
int mock_phy_init(void* user);
void mock_phy_set_mode(void* user, iolink_phy_mode_t mode);
void mock_phy_set_baudrate(void* user, iolink_baudrate_t baudrate);
int mock_phy_send(void* user, const uint8_t* data, size_t len);
int mock_phy_recv_byte(void* user, uint8_t* byte);
int mock_phy_detect_wakeup(void* user);
void mock_phy_set_cq_line(void* user, uint8_t state);

/*
 * 传输适配层：将主站帧送入 mock PHY，并驱动实际编译的
 * 传输方式。流式（send/recv_byte）与可选的
 * ISR（rx_octet/tx_octet）路径最终都会调用 mock_phy_send() 交付应答，
 * 因此同一套测试断言在两种构建中都适用。
 */
void mock_phy_feed_byte(uint8_t b);
void mock_phy_feed_chk(void);
void mock_phy_feed_error(void);
void mock_phy_clear_rx(void);
void mock_phy_clear_tx(void);
const uint8_t* mock_phy_tx(size_t* len);

/* 在生效的传输上驱动一个完整的请求/应答周期。 */
void test_io_cycle(iolink_device_ctx_t* ctx);

/* mock PHY 驱动 API */
extern const iolink_phy_api_t g_phy_mock;

/* 已删除的无上下文 API 的替代：测试需要参数/标识时先调用
 * test_device_info_reset()，再把 params 上下文绑定到 ISDU 上下文
 * （test_isdu_bind_params）。 */
iolink_device_info_ctx_t* test_device_info_ctx(void);
iolink_params_ctx_t* test_params_ctx(void);
void test_device_info_reset(const iolink_device_info_t* info);
void test_params_reset(void);
void test_isdu_bind_params(iolink_isdu_ctx_t* ctx);

/* 用于设置 mock 期望的辅助函数 */
void setup_mock_phy(void);
void iolink_phy_mock_reset(void);
void iolink_phy_mock_set_wakeup(int detected);
uint8_t iolink_phy_mock_get_cq_state(void);
void iolink_phy_mock_set_send_delay_us(uint32_t delay_us);

typedef struct
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;
} iolink_test_device_t;

int iolink_test_device_init(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                            const iolink_app_callbacks_t* callbacks);
void move_to_operate_ctx(iolink_device_ctx_t* ctx);

/* 用于数据存储（DS）测试的 mock 存储 */
#include "iolinki/data_storage.h"
extern const iolink_storage_api_t g_ds_storage_mock;
void iolink_ds_mock_reset(void);
uint8_t* iolink_ds_mock_get_buf(void);

/* ISDU V1.1.5 交织格式辅助函数 */
#include "iolinki/isdu.h"

/**
 * @brief 以 V1.1.5 交织格式发送 ISDU 读请求
 * @param ctx ISDU 上下文
 * @param index ISDU 索引
 * @param subindex ISDU 子索引
 * @return 请求可处理时为 1，仍在收集时为 0，出错时为 -1
 */
int isdu_send_read_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex);

/**
 * @brief 以 V1.1.5 交织格式发送 ISDU 写请求
 * @param ctx ISDU 上下文
 * @param index ISDU 索引
 * @param subindex ISDU 子索引
 * @param data 要写入的数据
 * @param data_len 数据长度
 * @return 请求可处理时为 1，仍在收集时为 0，出错时为 -1
 */
int isdu_send_write_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                            const uint8_t* data, uint8_t data_len);

/**
 * @brief 以 V1.1.5 交织格式收集 ISDU 响应
 * @param ctx ISDU 上下文
 * @param buffer 用于存放响应数据的缓冲区
 * @param buffer_size 缓冲区大小
 * @return 收集到的数据字节数，出错时为 -1
 */
int isdu_collect_response(iolink_isdu_ctx_t* ctx, uint8_t* buffer, size_t buffer_size);

#endif  // TEST_HELPERS_H_
