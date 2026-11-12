/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu.c
 * @brief ISDU 非循环报文的单元测试
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/protocol.h"
#include "iolinki/dll.h"
#include "iolinki/device_info.h"
#include "test_helpers.h"

#include "conformance_test.h"

static int test_setup(void** state)
{
    (void) state;
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    return 0;
}

static void test_isdu_vendor_name_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0114");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 1. 为索引 0x10（Vendor Name）发送 READ 请求 */
    assert_int_equal(isdu_send_read_request(&ctx, 0x0010, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* 2. 收集应答 */
    char name[32] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) name, sizeof(name) - 1);

    assert_int_equal(len, 7);
    assert_memory_equal(name, "iolinki", 7);
}

static void test_isdu_device_status_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0128");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* 1. 初始状态应为 OK (0) */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t status_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 0);

    /* 2. 触发一个错误并再次检查状态 */
    iolink_event_trigger(&events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 4); /* Failure */
}

static void test_isdu_detailed_device_status_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0129");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, 0x1801, IOLINK_EVENT_TYPE_ERROR);

    /* 读取 Detailed Status（索引 0x0025，Table B.8） */
    assert_int_equal(isdu_send_read_request(&ctx, 0x0025, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t detailed_buf[3];
    assert_int_equal(isdu_collect_response(&ctx, detailed_buf, sizeof(detailed_buf)), 3);
    /* 按 V1.1.5（图 A.24/Table A.17）的 EventQualifier：MODE=appears(0xC0)
       | TYPE=error(0x03<<4=0x30) | SOURCE=device(0) | INSTANCE=Application(0x04) = 0xF4 */
    assert_int_equal(detailed_buf[0], 0xF4);
    assert_int_equal(detailed_buf[1], 0x18); /* 代码高位 */
    assert_int_equal(detailed_buf[2], 0x01); /* 代码低位 */
}

/* CR443（Package 2026 Interface D1.1.6-02，B.2.22 Table B.14 第 270 页）：
 * 每条 DetailedDeviceStatus 记录恰好是 [EventQualifier][EventCode MSB]
 * [EventCode LSB]。这固定了原始八位组顺序，使退回到旧
 * "Octet 2,3: EventCode" 措辞（V1.1.5）的回归无法悄悄溜入。 */
static void test_isdu_detailed_device_status_cr443_octet_order(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* 一条排队的警告：0x8CA0（厂商专用的 Device 范围）。 */
    iolink_event_trigger(&events, 0x8CA0, IOLINK_EVENT_TYPE_WARNING);

    /* 读取 DetailedDeviceStatus（索引 0x0025，Table B.14）。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    /* 按图 A.24/Table A.17 的 EventQualifier：
       MODE=event appears (0xC0) | TYPE=warning (0x02 << 4) | INSTANCE=Application (0x04)。 */
    uint8_t record[3];
    assert_int_equal(isdu_collect_response(&ctx, record, sizeof(record)), 3);
    assert_int_equal(record[0], 0xE4U); /* 八位组 1：EventQualifier */
    assert_int_equal(record[1], 0x8CU); /* 八位组 2：EventCode MSB */
    assert_int_equal(record[2], 0xA0U); /* 八位组 3：EventCode LSB */

    const uint8_t expected[3] = {0xE4U, 0x8CU, 0xA0U};
    assert_memory_equal(record, expected, sizeof(expected));
}

static void test_isdu_error_stats_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll_ctx;
    (void) memset(&dll_ctx, 0, sizeof(dll_ctx));
    dll_ctx.crc_errors = 0x11223344U;
    dll_ctx.timeout_errors = 0xAABBCCDDU;
    dll_ctx.framing_errors = 0x01020304U;
    dll_ctx.timing_errors = 0x05060708U;

    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll_ctx;

    /* 读取错误统计（索引 0x0040 处的厂商对象） */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_STATS, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[16];
    int len = isdu_collect_response(&ctx, data, sizeof(data));
    assert_int_equal(len, sizeof(data));

    const uint8_t expected[16] = {0x11U, 0x22U, 0x33U, 0x44U, 0xAAU, 0xBBU, 0xCCU, 0xDDU,
                                  0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
    assert_memory_equal(data, expected, sizeof(expected));
}

/* SystemCommand 用例 */

static void test_system_cmd_device_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0316");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 向索引 0x0002 写入 0x80 */
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* 校验复位标志已置位 */
    assert_true(ctx.reset_pending);

    /* 校验成功应答（空） */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* 无数据 */
}

static void test_system_cmd_application_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0317");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 向索引 0x0002 写入 0x81 */
    uint8_t cmd_data[] = {IOLINK_CMD_APPLICATION_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* 校验应用复位标志已置位 */
    assert_true(ctx.app_reset_pending);

    /* 校验成功应答 */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* 无数据 */
}

/* Application Reset 会改变参数集，因此设备必须触发
 * DS_UPLOAD_REQ 事件（规范 10.4.2）。 */
static void test_system_cmd_app_reset_raises_ds_upload_req(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0317");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    uint8_t cmd_data[] = {IOLINK_CMD_APPLICATION_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);
    iolink_isdu_process(&ctx);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
}

static void test_system_cmd_factory_restore(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0155");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 先设置一个自定义应用标签 */
    const uint8_t test_tag[] = "TestTag123";
    iolink_params_set(0x0018, 0x00, test_tag, sizeof(test_tag) - 1, false);

    /* 向索引 0x0002 写入 0x82 */
    uint8_t cmd_data[] = {IOLINK_CMD_RESTORE_FACTORY_SETTINGS};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* 校验成功应答（空）。 */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* 无数据 */

    /* 校验应用标签已被复位 */
    uint8_t readback[33];
    int len = iolink_params_get(0x0018, 0x00, readback, sizeof(readback));
    assert_int_equal(len, 0); /* 恢复出厂设置后应为空 */
}

static void test_system_cmd_reserved_ds_commands(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0104");
    (void) state;
    const uint8_t reserved[] = {0x95U, 0x96U};

    for (uint8_t i = 0U; i < 2U; i++) {
        iolink_isdu_ctx_t ctx;
        iolink_ds_ctx_t ds;
        iolink_device_info_init(NULL);
        iolink_params_init();
        iolink_ds_init(&ds, NULL);
        iolink_isdu_init(&ctx);
        ctx.ds_ctx = &ds;

        uint8_t cmd_data[] = {reserved[i]};
        assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);
        iolink_isdu_process(&ctx);

        /* Table B.9：0x84-0x9F 是保留的 SystemCommand 值。 */
        assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
        uint8_t byte;
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, 0x80U);
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, 0x35U);
    }
}

static void test_system_cmd_param_break(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* 先将 DS 置为上传状态 */
    ds.state = IOLINK_DS_STATE_UPLOADING;

    /* 向索引 0x0002 写入 0x06（ParamBreak）。 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_BREAK};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* 校验 DS 状态已中止回到 idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 校验成功应答（空）。 */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* 无数据 */
}

static void test_system_cmd_invalid(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0104");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 向索引 0x0002 写入无效命令 0xFF */
    uint8_t cmd_data[] = {0xFF};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* 校验错误应答：否定载荷 {0x80, AdditionalCode}。 */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x35U); /* FUNC_NOTAVAIL（Table C.1 / C.2.14）。 */
}

static void test_isdu_function_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 写入 Function Tag */
    const char* test_tag = "TestFunction";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(
        isdu_send_write_request(&ctx, 0x0019, 0x00, (const uint8_t*) test_tag, tag_len), 1);

    iolink_isdu_process(&ctx);

    /* 校验成功应答（空） */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* 无数据 */

    /* 回读 Function Tag */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x0019, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* 收集应答 */
    char readback[33] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) readback, sizeof(readback) - 1);

    assert_int_equal(len, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_location_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 写入 Location Tag */
    const char* test_tag = "Building-A";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(
        isdu_send_write_request(&ctx, 0x001A, 0x00, (const uint8_t*) test_tag, tag_len), 1);

    iolink_isdu_process(&ctx);

    /* 校验成功应答（空） */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* 无数据 */

    /* 回读 Location Tag */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x001A, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* 收集应答 */
    char readback[33] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) readback, sizeof(readback) - 1);

    assert_int_equal(len, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

/* 图 B.5/Table B.6：解码 DPP1 ProcessDataIn/Out 长度描述符。 */
static unsigned pd_descriptor_octets(uint8_t code)
{
    const unsigned field = (unsigned) (code & 0x1FU);
    if ((code & 0x80U) != 0U) {
        return field + 1U; /* BYTE=1：Length 以八位组数减 1 表示 */
    }
    return field / 8U; /* BYTE=0：Length 以位数表示 */
}

/* 读取 Direct Parameter 第 1 页的一个八位组（地址 0x00..0x0F）。 */
static uint8_t read_dpp1_octet(iolink_dll_ctx_t* dll, uint8_t addr)
{
    iolink_isdu_ctx_t page_ctx;
    iolink_isdu_init(&page_ctx);
    page_ctx.dll_ctx = dll;
    assert_int_equal(
        isdu_send_read_request(&page_ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, (uint8_t) (addr + 1U)),
        1);
    iolink_isdu_process(&page_ctx);
    uint8_t value = 0U;
    assert_int_equal(isdu_collect_response(&page_ctx, &value, 1U), 1);
    return value;
}

static void test_isdu_pdin_descriptor_read(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0132");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 连接一个运行时 PD-in 长度为 3 的 DLL 上下文（公开的 setter
       会更新 pd_in_len_current，这正是 0x0028 必须提供的内容）。 */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 3, 0), 0);
    dll.pd_valid = true;
    dll.pd_in[0][dll.od_width + 0U] = 0x11U;
    dll.pd_in[0][dll.od_width + 1U] = 0x22U;
    dll.pd_in[0][dll.od_width + 2U] = 0x33U;
    ctx.dll_ctx = &dll;

    /* Table B.8/B.2.23：读操作返回运行时的 PD-in 数据（3 个八位组）。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    const uint8_t expected[3] = {0x11U, 0x22U, 0x33U};
    for (size_t i = 0U; i < sizeof(expected); i++) {
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte, expected[i]);
    }
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0);

    /* SDCI_TC_0132：PD 长度必须与 DPP1 0x05 一致，且保留的
       (位 5) 与未使用的 SIO (位 6) 描述符位必须为零。 */
    const uint8_t dpp1_pdin = read_dpp1_octet(&dll, 0x05U);
    assert_int_equal(dpp1_pdin & 0x60U, 0x00U);
    assert_int_equal(pd_descriptor_octets(dpp1_pdin), sizeof(expected));

    /* 测试写保护 */
    iolink_isdu_init(&ctx);
    uint8_t cmd_data[] = {0x05};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* 校验写保护错误：{0x80, AdditionalCode}。 */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_data_storage_round_trip(void** state)
{
    (void) state;
#if !IOLINK_DS_LEGACY_IMAGE_SUB0
    /* V1.1 的默认行为是在 sub0 上返回拼接后的 DataStorageIndex 记录；
       旧式的整幅镜像往返仅在兼容
       开关下可用。 */
    skip();
#else
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    /* 配置一个已知的 ApplicationTag。 */
    const char* app = "ISDU-DS";
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) app, strlen(app), true),
        0);

    /* 上传（读 0x0003）：主站备份序列化后的参数镜像。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t image[IOLINK_DS_IMAGE_MAX];
    int img_len = isdu_collect_response(&ctx, image, sizeof(image));
    assert_true(img_len > 0);

    /* 清空参数。 */
    iolink_params_factory_reset();
    uint8_t buf[40];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 0);

    /* 下载（写 0x0003）：主站恢复已备份的镜像。 */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00, image, (uint8_t) img_len), 1);
    iolink_isdu_process(&ctx);

    /* ApplicationTag 已通过总线恢复。 */
    int len = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(app));
    assert_memory_equal(buf, app, strlen(app));
#endif
}

/* Table B.10：DataStorageIndex 的子索引 02-05 是只读访问器。 */
static void test_isdu_data_storage_subindices(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0107");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0108");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0378");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    assert_true(iolink_ds_build_image(&ds) >= 0);
    uint8_t r[64];

    /* sub 01 DS_Command：1 个八位组，取值 0..5。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 1);
    assert_true(r[0] <= 5U);

    /* sub 00：拼接记录，长度 12 + 3n（n = 3）。 */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 21);
    assert_int_equal(r[0], 0x00U); /* DS_Command */
    assert_int_equal(r[1], 0x00U); /* State_Property */
    assert_int_equal((uint32_t) ((uint32_t) r[2] << 24) | ((uint32_t) r[3] << 16) |
                         ((uint32_t) r[4] << 8) | r[5],
                     (uint32_t) ds.image_len);
    assert_int_equal((uint32_t) ((uint32_t) r[6] << 24) | ((uint32_t) r[7] << 16) |
                         ((uint32_t) r[8] << 8) | r[9],
                     ds.current_checksum);
    static const uint8_t expect_record_tail[] = {
        0x00U, 0x18U, 0x00U, 0x00U, 0x19U, 0x00U, 0x00U, 0x1AU, 0x00U, 0x00U, 0x00U,
    };
    assert_memory_equal(&r[10], expect_record_tail, sizeof(expect_record_tail));

    /* sub 02 State_Property：1 个八位组，Inactive + 无上传标志。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x02), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 1);
    assert_int_equal(r[0], 0x00U);

    /* sub 03 Data_Storage_Size：4 八位组大端 == 镜像长度。 */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x03), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 4);
    assert_int_equal((uint32_t) ((uint32_t) r[0] << 24) | ((uint32_t) r[1] << 16) |
                         ((uint32_t) r[2] << 8) | r[3],
                     (uint32_t) ds.image_len);

    /* sub 04 Parameter_Checksum：4 八位组大端 == current_checksum。 */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x04), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 4);
    assert_int_equal((uint32_t) ((uint32_t) r[0] << 24) | ((uint32_t) r[1] << 16) |
                         ((uint32_t) r[2] << 8) | r[3],
                     ds.current_checksum);

    /* sub 05 Index_List：3 个条目（0x0018/0x0019/0x001A，sub 0）+ 0x0000 标记。 */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x05), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 11);
    static const uint8_t expect_list[] = {0x00U, 0x18U, 0x00U, 0x00U, 0x19U, 0x00U,
                                          0x00U, 0x1AU, 0x00U, 0x00U, 0x00U};
    assert_memory_equal(r, expect_list, sizeof(expect_list));

    /* sub 00-05 以只读为由拒绝 WRITE (0x8023)；只有 sub1 可写。 */
    for (uint8_t sub = 0U; sub <= 5U; sub++) {
        if (sub == 1U) {
            continue;
        }
        iolink_isdu_init(&ctx);
        ctx.ds_ctx = &ds;
        uint8_t w[2] = {0x00U, 0x00U};
        assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, sub, w, 2U), 1);
        iolink_isdu_process(&ctx);
        assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
        assert_int_equal(r[0], 0x80U);
        assert_int_equal(r[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    }
}

/* Table B.10 子索引 01：DS_Command 八位组驱动 DS 状态机。 */
static void test_isdu_ds_command_sequence(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0077");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0321");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0322");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    uint8_t r[4];
    uint8_t cmd[1] = {0x00U};

    /* DS_UploadStart (0x01) -> 上传状态；上传标志可被观测。 */
    iolink_ds_request_upload(&ds);
    assert_int_equal(iolink_ds_state_property(&ds) & 0x80U, 0x80U);
    cmd[0] = 0x01U;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01, cmd, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    /* DS_UploadEnd (0x02) -> idle 并清除上传标志。 */
    ds.state = IOLINK_DS_STATE_UPLOADING;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    cmd[0] = 0x02U;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01, cmd, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(iolink_ds_state_property(&ds) & 0x80U, 0x00U);

    /* DS 锁生效时的 DS_DownloadStart (0x03) -> 访问被拒绝 (0x8023)。 */
    iolink_device_info_set_access_locks(IOLINK_LOCK_DS);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    cmd[0] = 0x03U;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01, cmd, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
    assert_int_equal(r[0], 0x80U);
    assert_int_equal(r[1], IOLINK_ISDU_ERROR_WRITE_PROTECTED);

    /* DS_Break (0x05) 将下载复位为 idle。 */
    iolink_device_info_set_access_locks(0U);
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    cmd[0] = 0x05U;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01, cmd, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static void test_isdu_direct_parameters_page1(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0052");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 整页 Direct Parameter 第 1 页（16 字节）。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t page[16];
    assert_int_equal(isdu_collect_response(&ctx, page, sizeof(page)), 16);

    const iolink_device_info_t* info = iolink_device_info_get();
    assert_int_equal(page[0x04], 0x11);          /* RevisionID = v1.1 */
    assert_int_equal(page[0x03] & 0x01U, 0x01U); /* 支持 ISDU */
    assert_int_equal(page[0x07], (uint8_t) (info->vendor_id >> 8));
    assert_int_equal(page[0x08], (uint8_t) (info->vendor_id & 0xFFU));
    assert_int_equal(page[0x09], (uint8_t) ((info->device_id >> 16) & 0xFFU));
    assert_int_equal(page[0x0A], (uint8_t) ((info->device_id >> 8) & 0xFFU));
    assert_int_equal(page[0x0B], (uint8_t) (info->device_id & 0xFFU));

    /* 通过子索引进行单八位组读取（地址 0x04 -> 子索引 5）。 */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05), 1);
    iolink_isdu_process(&ctx);
    uint8_t b[2];
    assert_int_equal(isdu_collect_response(&ctx, b, sizeof(b)), 1);
    assert_int_equal(b[0], 0x11);

    /* 第 1 页为只读。 */
    iolink_isdu_init(&ctx);
    uint8_t w = 0x55;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x01, &w, 1), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &w), 1);
    assert_int_equal(w, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &w), 1);
    assert_int_equal(w, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_direct_parameters_page2(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_isdu_init(&ctx);

    /* 写入整个第 2 页，然后回读。 */
    uint8_t img[16];
    for (uint8_t i = 0U; i < 16U; i++) {
        img[i] = (uint8_t) (0xA0U + i);
    }
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00, img, 16),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t rb[16];
    assert_int_equal(isdu_collect_response(&ctx, rb, sizeof(rb)), 16);
    assert_memory_equal(rb, img, 16);

    /* 在子索引 3 处进行单八位组写，然后回读。 */
    iolink_isdu_init(&ctx);
    uint8_t one = 0x5A;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x03, &one, 1),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x03), 1);
    iolink_isdu_process(&ctx);
    uint8_t one_rb[2];
    assert_int_equal(isdu_collect_response(&ctx, one_rb, sizeof(one_rb)), 1);
    assert_int_equal(one_rb[0], 0x5A);
}

static void test_isdu_reserved_indices_negative(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 0x001C..0x001F 未使用（C5）；读操作得到否定应答。 */
    assert_int_equal(isdu_send_read_request(&ctx, 0x001CU, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

static void test_isdu_system_command_read_only_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* SystemCommand (0x0002) 为只写（Table B.8）：读请求会以
       IDX_NOT_ACCESSIBLE 0x80 0x23 应答（Table C.1）。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);

    /* 对 0x0002 的写仍会执行该命令。 */
    iolink_isdu_init(&ctx);
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, cmd_data, 1),
                     1);
    iolink_isdu_process(&ctx);
    assert_true(ctx.reset_pending);
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0);
}

static uint8_t page1_octet3(uint8_t m_seq_type, uint8_t pd_in, uint8_t pd_out)
{
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.m_seq_type = m_seq_type;
    assert_int_equal(iolink_dll_set_pd_length(&dll, pd_in, pd_out), 0);
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    return iolink_isdu_direct_param_page1_octet(&ctx, 0x03U);
}

static void test_isdu_mseq_capability_codes(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0052");
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();

    /* Table A.10 / 计划任务 6。 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_2_1, 1, 0), 0x01U); /* OD1, code 0 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_1_2, 0, 0), 0x03U); /* OD2, code 1 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_1_V, 0, 0), 0x0DU); /* OD8, code 6 */
    /* 对 TYPE_0，PREOPERATE 的位 4-5 = 0。 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_0, 0, 0) & 0x30U, 0x00U);
}

static void test_isdu_spec_index_constants(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x0025U);
    assert_int_equal(IOLINK_IDX_DEVICE_STATUS, 0x0024U);
    assert_int_equal(IOLINK_IDX_ERROR_COUNT, 0x0020U);
    assert_int_equal(IOLINK_IDX_ERROR_STATS, 0x0040U);
}

/* C3/ISR：主循环泵为 ISR 读泵快照分段后的应答。 */
static void test_isdu_snapshot_response(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    uint8_t out[8] = {0xAAU};
    bool busy = true;

    /* 空闲：无服务、不忙、未复制任何内容。 */
    assert_int_equal(iolink_isdu_snapshot_response(&ctx, &busy, out, sizeof(out)), 0U);
    assert_false(busy);

    /* 服务执行中：忙、未复制任何内容。 */
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    assert_int_equal(iolink_isdu_snapshot_response(&ctx, &busy, out, sizeof(out)), 0U);
    assert_true(busy);

    /* 应答就绪：整个分段应答被复制。 */
    ctx.state = ISDU_STATE_RESPONSE_READY;
    const uint8_t resp[4] = {0xD4U, 0x12U, 0x34U, 0xF2U};
    (void) memcpy(ctx.resp_buf, resp, sizeof(resp));
    ctx.resp_total = sizeof(resp);
    assert_int_equal(iolink_isdu_snapshot_response(&ctx, &busy, out, sizeof(out)),
                     (int) sizeof(resp));
    assert_false(busy);
    assert_memory_equal(out, resp, sizeof(resp));

    /* 目标缓冲区容量限制了复制量。 */
    (void) memset(out, 0, sizeof(out));
    assert_int_equal(iolink_isdu_snapshot_response(&ctx, &busy, out, 2U), 2);
    assert_memory_equal(out, resp, 2U);

    /* NULL 安全。 */
    assert_int_equal(iolink_isdu_snapshot_response(NULL, NULL, NULL, 0U), 0U);
}

/* 事件/DS 服务门控：当且仅当传输处于 ISDU_STATE_IDLE 时为空闲。 */
static void test_isdu_is_idle(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    assert_true(iolink_isdu_is_idle(&ctx));

    ctx.state = ISDU_STATE_HEADER_INITIAL;
    assert_false(iolink_isdu_is_idle(&ctx));
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    assert_false(iolink_isdu_is_idle(&ctx));
    ctx.state = ISDU_STATE_RESPONSE_READY;
    assert_false(iolink_isdu_is_idle(&ctx));

    ctx.state = ISDU_STATE_IDLE;
    assert_true(iolink_isdu_is_idle(&ctx));
    assert_false(iolink_isdu_is_idle(NULL));
}

/* B.2.4：DeviceAccessLocks 仅可通过子索引 0 访问；B.2.5：
 * ProfileCharacteristic 是仅暴露一个元素的 ArrayT。 */
static void test_isdu_subindexed_objects(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0058");
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0111");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    uint8_t r[8];

    /* 0x000C 子索引 1 -> SUBINDEX_NOT_AVAIL。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
    assert_int_equal(r[0], 0x80U);
    assert_int_equal(r[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    /* SDCI_TC_0058：VendorName (0x10) 没有子索引 2。 */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_VENDOR_NAME, 2U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
    assert_int_equal(r[0], 0x80U);
    assert_int_equal(r[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    /* 未配置配置文件时 0x000D 不可用（IDX_NOTAVAIL）；
       子索引边界仅在存在 ProfileCharacteristic 时才有意义。 */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 2U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
    assert_int_equal(r[0], 0x80U);
    assert_int_equal(r[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);

    /* 单元素对象拒绝任何不为 0 的子索引。 */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_VENDOR_NAME, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
    assert_int_equal(r[0], 0x80U);
    assert_int_equal(r[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* 7.3.5.3：DPP1 SystemCommand 执行时不暂存 ISDU 应答。 */
static void test_isdu_dpp1_system_command(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    iolink_isdu_execute_system_command(&ctx, IOLINK_CMD_APPLICATION_RESET);
    assert_true(ctx.app_reset_pending);
    assert_int_equal(ctx.response_len, 0U);
}

/* 使用绑定到所供给事件引擎的新 ISDU 上下文读取
 * DeviceStatus/DetailedDeviceStatus。 */
static int read_events_index(iolink_events_ctx_t* events, uint16_t index, uint8_t* out,
                             size_t out_size)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.event_ctx = events;
    assert_int_equal(isdu_send_read_request(&ctx, index, 0U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_size);
}

static uint32_t g_dds_event_ms;

static uint64_t dds_event_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_dds_event_ms * 1000U;
}

/* SDCI_TC_0129：在发生任何事件之前 DDS 为空（不是错误），且最多
 * 64 条记录。协议栈至多保留 IOLINK_EVENT_PENDING_MAX 条活动记录。 */
static void test_isdu_dds_empty_and_max_records(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0129");
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();

    iolink_events_ctx_t events;
    iolink_events_init(&events);

    uint8_t records[IOLINK_ISDU_BUFFER_SIZE];
    int n = read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records));
    assert_int_equal(n, 0); /* 空对象：无记录，也无否定应答。 */

    /* 用互不相同的 Error 事件填满每个活动槽位。 */
    for (uint16_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        iolink_event_trigger(&events, (uint16_t) (0x2000U + i), IOLINK_EVENT_TYPE_ERROR);
    }

    n = read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records));
    assert_true(IOLINK_EVENT_PENDING_MAX <= 64U);
    assert_int_equal(n, (int) (3U * IOLINK_EVENT_PENDING_MAX));
    assert_true(n <= (int) (3U * 64U));
    assert_int_equal(n % 3, 0);
    for (int i = 0; i < n; i += 3) {
        assert_int_equal(records[i] & 0xC0U, 0xC0U); /* MODE = event appears */
    }

    /* 最后一条记录之后的下一项为 SUBINDEX_NOT_AVAIL。 */
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS,
                                            (uint8_t) (IOLINK_EVENT_PENDING_MAX + 1U)),
                     1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, records, sizeof(records)), 2);
    assert_int_equal(records[0], 0x80U);
    assert_int_equal(records[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* B.2.22：条目在报告 "event disappears" 时才离开 DetailedDeviceStatus，
 * 而不是在条件清除时。当主站仍持有该条目时，撤回会按
 * 10.10.2 的间隔被推迟。 */
static void test_isdu_dds_keeps_cleared_event_until_disappear(void** state)
{
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();

    iolink_events_ctx_t events;
    iolink_events_init(&events);
    events.time_us = dds_event_time_us;
    g_dds_event_ms = 0U;

    uint8_t records[16];
    iolink_event_trigger(&events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(
        read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records)), 3);
    assert_int_equal(records[0], 0xF4U); /* appears | error | instance Application */
    assert_int_equal(records[1], 0x18U);
    assert_int_equal(records[2], 0x01U);

    /* 主站消费了 appear 事件；条件仍保持置位。 */
    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);

    /* 条件已消失，但撤回尚未被报告。 */
    iolink_event_clear(&events, IOLINK_EVENT_COMM_CRC);
    assert_int_equal(
        read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records)), 3);

    /* 在最短 appear 窗口之前的瞬间，该条目仍会被列出。 */
    g_dds_event_ms = IOLINK_EVENT_MIN_APPEAR_MS - 1U;
    iolink_events_process(&events);
    assert_int_equal(
        read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records)), 3);

    /* 到达该窗口时报告 disappear，条目随即消失。 */
    g_dds_event_ms = IOLINK_EVENT_MIN_APPEAR_MS;
    iolink_events_process(&events);
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_CRC);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_int_equal(
        read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records)), 0);
}

/* SDCI_TC_0373：DeviceStatus 与 DetailedDeviceStatus 反映在一次干净的
 * 基线读之后出现的事件。 */
static void test_isdu_device_status_follows_event(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0373");
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();

    iolink_events_ctx_t events;
    iolink_events_init(&events);

    uint8_t status = 0xFFU;
    uint8_t records[16] = {0xFFU};

    assert_int_equal(read_events_index(&events, IOLINK_IDX_DEVICE_STATUS, &status, 1U), 1);
    assert_int_equal(status, 0U); /* OK */
    assert_int_equal(
        read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records)), 0);

    /* 事件用例所使用的参考配置文件 Config7 事件代码。 */
    iolink_event_trigger(&events, 0x1801U, IOLINK_EVENT_TYPE_ERROR);

    assert_int_equal(read_events_index(&events, IOLINK_IDX_DEVICE_STATUS, &status, 1U), 1);
    assert_int_equal(status, 4U); /* Failure */
    assert_int_equal(
        read_events_index(&events, IOLINK_IDX_DETAILED_DEVICE_STATUS, records, sizeof(records)), 3);
    assert_int_equal(records[0], 0xF4U); /* appears | error | instance Application */
    assert_int_equal((uint16_t) (((uint16_t) records[1] << 8) | records[2]), 0x1801U);
}

/* SDCI_TC_0057：保留索引 0xFF 以 8 位索引形式请求
 * （I-Service 0x9），而不是 16 位索引 0x00FF。 */
static void test_isdu_read_8bit_reserved_index(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0057");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, 0x00FFU, 0U), 1);
    /* 请求为 0x93 0xFF 0x6C：8 位索引形式，而非 0xB5 0x00 0xFF。 */
    assert_int_equal(ctx.req_total, 3U);
    assert_int_equal(ctx.req_buf[0], 0x93U);
    assert_int_equal(ctx.req_buf[1], 0xFFU);

    iolink_isdu_process(&ctx);
    uint8_t response[4] = {0};
    assert_int_equal(isdu_collect_response(&ctx, response, sizeof(response)), 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* Table B.14：DetailedDeviceStatus 是由 3 八位组条目组成的 ArrayT。 */
static void test_isdu_dds_subindices(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0129");
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&events, 0x2000U, IOLINK_EVENT_TYPE_WARNING);

    uint8_t r[16];

    /* sub 0：两个活动条目（6 个八位组）。 */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 6);

    /* sub 1：第一个条目（3 个八位组）。 */
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 1U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 3);

    /* sub 3：超出范围 -> SUBINDEX_NOT_AVAIL。 */
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 3U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, r, sizeof(r)), 2);
    assert_int_equal(r[0], 0x80U);
    assert_int_equal(r[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_isdu_vendor_name_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_device_status_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_read, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_cr443_octet_order,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_stats_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reserved_indices_negative, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_mseq_capability_codes, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_spec_index_constants, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_system_command_read_only_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_device_reset, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_app_reset_raises_ds_upload_req, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_factory_restore, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_reserved_ds_commands, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_break, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_invalid, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_function_tag_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_location_tag_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_pdin_descriptor_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_direct_parameters_page1, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_direct_parameters_page2, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_data_storage_round_trip, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_data_storage_subindices, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_ds_command_sequence, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_subindexed_objects, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_dpp1_system_command, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_dds_subindices, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_dds_keeps_cleared_event_until_disappear,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_dds_empty_and_max_records, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_device_status_follows_event, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_read_8bit_reserved_index, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_snapshot_response, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_is_idle, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
