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

/* 测试时钟：未安装假时钟时返回真实单调时间，安装后由用例手动推进。 */
uint64_t iolink_test_time_us(void* user);
uint32_t iolink_test_time_ms(void);
extern iolink_device_time_us_fn g_iolink_test_time_us_override;

/* 手动推进的假时钟；必须在 iolink_test_device_init() 之前安装。 */
void test_clock_install(void);
void test_clock_advance_ms(uint32_t ms);
uint64_t test_clock_now_us(void);
void test_clock_uninstall(void);

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
int mock_phy_detect_wakeup(void* user);
void mock_phy_set_cq_line(void* user, uint8_t state);

/*
 * 传输适配层：把主站帧的八位字节送入 mock PHY 的接收队列，
 * 由 mock_rx_octet() 逐个交付给组帧引擎。设备应答经
 * mock_tx_octet()/mock_tx_done() 收集后交给 mock_phy_send()，
 * 后者只做捕获，断言由 assert_tx_* 完成。
 */
void mock_phy_feed_byte(uint8_t b);
void mock_phy_feed_chk(void);
void mock_phy_feed_error(void);
void mock_phy_clear_rx(void);
void mock_phy_clear_tx(void);
const uint8_t* mock_phy_tx(size_t* len);

/* 应答捕获断言：每次断言消费自上次断言（或 mock_phy_clear_tx()）以来的
 * 一个已交付帧；没有交付帧即失败。 */
void assert_tx_frame(const uint8_t* expected, size_t len);
void assert_tx_len(size_t len);
void assert_no_tx(void);

/* 注入 mock_phy_send 的返回值；默认返回 len。 */
void mock_phy_set_send_result(int result);

/* 在生效的传输上驱动一个完整的请求/应答周期。 */
void test_io_cycle(iolink_device_ctx_t* ctx);

/* mock PHY 驱动 API */
extern const iolink_phy_api_t g_phy_mock;

/* 测试专用参数 shim：库已删除 params 上下文；这里用应用标签 OD 表项
 * 复刻旧的参数读写语义，让既有测试最小改动迁移。 */
typedef struct
{
    uint8_t app[33];
    uint8_t app_len;
    uint8_t fun[33];
    uint8_t fun_len;
    uint8_t loc[33];
    uint8_t loc_len;
    iolink_od_entry_t entries[3];
    iolink_od_app_t od;
} iolink_test_params_t;

void test_params_init(iolink_test_params_t* params, iolink_device_info_ctx_t* info);
void test_params_factory_reset(iolink_test_params_t* params);
iolink_result_t test_params_read(const iolink_test_params_t* params, uint16_t index,
                                 uint8_t subindex, uint8_t* buffer, size_t max_len);
iolink_result_t test_params_write(iolink_test_params_t* params, uint16_t index, uint8_t subindex,
                                  const uint8_t* data, size_t len);
iolink_od_app_t* test_params_od(iolink_test_params_t* params);

/* 进程级默认标签表（惰性初始化，三个标签绑定到静态缓冲）。 */
iolink_od_app_t* test_default_tag_od(void);
/* 把 app 的表项与 tags 的表项合并到调用方提供的 storage；tags 为 NULL 时
 * 使用进程默认标签表。 */
iolink_od_app_t test_od_with_tags(const iolink_od_app_t* app, const iolink_od_app_t* tags,
                                  iolink_od_entry_t* storage, size_t capacity);

/* 显式的测试环境：设备标识 + 标签参数（测试 shim）。
 *
 * 每个测试文件持有自己的实例（文件级 static），并在用例或 setup 中
 * 显式初始化。不再有 test_helpers.c 内的进程级单例，因此不存在
 * "访问器先于初始化被调用"的声明顺序耦合。 */
typedef struct
{
    iolink_device_info_ctx_t info;
    iolink_test_params_t params;
} iolink_test_env_t;

void test_env_init(iolink_test_env_t* env, const iolink_device_info_t* info);
void test_env_reset_params(iolink_test_env_t* env);
iolink_device_info_ctx_t* test_env_info(iolink_test_env_t* env);
iolink_test_params_t* test_env_params(iolink_test_env_t* env);
void test_isdu_bind_params(iolink_isdu_ctx_t* ctx, iolink_test_env_t* env);

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
    iolink_test_params_t params;
} iolink_test_device_t;

int iolink_test_device_init(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                            const iolink_app_callbacks_t* callbacks);
/* 同上，并在初始化前绑定应用 OD 表（用于验证 config->od 的设备层接线）。 */
int iolink_test_device_init_with_od(iolink_test_device_t* dev, const iolink_stack_config_t* stack,
                                    const iolink_app_callbacks_t* callbacks,
                                    const iolink_od_app_t* od);
void move_to_operate_ctx(iolink_device_ctx_t* ctx);

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
