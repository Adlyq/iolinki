/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 现有单元测试套件尚未覆盖的一致性条目。
 *
 * 断言依据 IO-Link Test Specification 10032 V1.1.4 和
 * IO-Link Interface and System Specification V1.1.5。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/device_info.h"
#include "dll_access.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static iolink_test_env_t g_test_env;

static const iolink_clock_t g_test_clock = {.time_us = iolink_test_time_us};

static void events_init(iolink_events_ctx_t* ctx)
{
    iolink_events_init(ctx);
    ctx->clock = &g_test_clock;
}
#include "conformance_test.h"
#define GAP_NVM_SLOTS 8U
static char g_gap_nvm[GAP_NVM_SLOTS][64];
static size_t g_gap_nvm_count;

static void cleanup_gap_nvm(void)
{
    for (size_t i = 0U; i < g_gap_nvm_count; i++) {
        (void) remove(g_gap_nvm[i]);
    }
}

/* 每个进程与 tag 唯一：并行的 ctest 运行不得共用 NVM。 */
static const char* gap_nvm_path(const char* tag)
{
    static int registered = 0;
    if (registered == 0) {
        (void) atexit(cleanup_gap_nvm);
        registered = 1;
    }
    if (g_gap_nvm_count >= GAP_NVM_SLOTS) {
        return g_gap_nvm[GAP_NVM_SLOTS - 1U]; /* 不会发生：每个用例只有一个路径 */
    }
    char* path = g_gap_nvm[g_gap_nvm_count];
    (void) snprintf(path, sizeof(g_gap_nvm[0]), "/tmp/iolink-gap-%s-%ld.nvm", tag, (long) getpid());
    (void) remove(path);
    g_gap_nvm_count++;
    return path;
}

#include "conformance_device.h"

static int gap_noop_init(iolink_app_t* self)
{
    (void) self;
    return 0;
}

static void gap_noop_set_mode(iolink_app_t* self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}

static void gap_noop_set_baudrate(iolink_app_t* self, iolink_baudrate_t baudrate)
{
    (void) self;
    (void) baudrate;
}

static const iolink_phy_api_t k_gap_noop_phy = {
    .init = gap_noop_init,
    .set_mode = gap_noop_set_mode,
    .set_baudrate = gap_noop_set_baudrate,
};

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

static int read_object(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* out,
                       size_t out_len)
{
    iolink_isdu_init(ctx);
    test_isdu_bind_params(ctx, &g_test_env);
    assert_int_equal(isdu_send_read_request(ctx, index, subindex), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_len);
}

static int write_object(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                        const uint8_t* data, uint8_t data_len, uint8_t* out, size_t out_len)
{
    iolink_isdu_init(ctx);
    test_isdu_bind_params(ctx, &g_test_env);
    assert_int_equal(isdu_send_write_request(ctx, index, subindex, data, data_len), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_len);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0063 / SDCI_TC_0123：ApplicationTag 长度超限                       */
/* ------------------------------------------------------------------------- */

static void test_0063_write_length_overrun(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0063");

    const char* nvm = gap_nvm_path("0063");
    (void) remove(nvm);
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_gap_noop_phy, nvm),
        0);
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U,
                                                    (const uint8_t[70]){0}, 70U),
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    conformance_device_close(&app);
    (void) remove(nvm);
}

static void test_0064_write_length_underrun(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0064");

    const char* nvm = gap_nvm_path("0064");
    (void) remove(nvm);
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_gap_noop_phy, nvm),
        0);
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U,
                                                    (const uint8_t[63]){0}, 63U),
                     IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);
    conformance_device_close(&app);
    (void) remove(nvm);
}

static void test_0123_application_tag_overlong(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0123");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t too_long[33];
    uint8_t response[8] = {0};
    (void) memset(too_long, 'B', sizeof(too_long));

    int n = write_object(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, too_long, sizeof(too_long), response,
                         sizeof(response));
    assert_int_equal(n, 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0089：DPP1 0x01 MasterCycleTime 回读                               */
/* ------------------------------------------------------------------------- */

static void test_0089_master_cycle_time_readback(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0089");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_device_ctx_t dev;
    (void) memset(&dev, 0, sizeof(dev));
    iolink_dll_main_t* dll = test_dll(&dev);
    dll->master_cycle_time = 0x4FU;

    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    ctx.dll_ctx = dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x02U), 1);
    iolink_isdu_process(&ctx);

    uint8_t value[2] = {0};
    int n = isdu_collect_response(&ctx, value, sizeof(value));
    assert_int_equal(n, 1);
    assert_int_equal(value[0], dll->master_cycle_time);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0092：DPP1 RevisionID 来自已配置的身份信息                         */
/* ------------------------------------------------------------------------- */

static void test_0092_revision_id_configured(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0092");

    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.revision_id = IOLINK_REVISION_ID_1_0;
    test_env_init(&g_test_env, &info);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t value = 0U;
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05U, &value, 1U), 1);
    assert_int_equal(value, IOLINK_REVISION_ID_1_0);

    info.revision_id = IOLINK_REVISION_ID_1_1;
    test_env_init(&g_test_env, &info);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05U, &value, 1U), 1);
    assert_int_equal(value, IOLINK_REVISION_ID_1_1);

    /* 字段是一个八位组：宽度由类型保证，线上按原值写出（0x21 -> 0x21）。 */
    assert_int_equal(sizeof(info.revision_id), 1U);
    info.revision_id = 0x21U;
    test_env_init(&g_test_env, &info);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05U, &value, 1U), 1);
    assert_int_equal(value, 0x21U);

    /* 没有绑定的设备身份就没有 DPP1：不臆造 RevisionID，直接拒绝。 */
    iolink_isdu_init(&ctx);
    ctx.od = &g_test_env.params.od;
    assert_null(ctx.device_info);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05U), 1);
    iolink_isdu_process(&ctx);
    uint8_t response[2] = {0U, 0U};
    assert_int_equal(isdu_collect_response(&ctx, response, sizeof(response)), 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0097：DPP1 FunctionID 为保留字段，应始终为 0                       */
/* ------------------------------------------------------------------------- */

static void test_0097_function_id_zero(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0097");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t msb = 0xFFU;
    uint8_t lsb = 0xFFU;

    assert_int_equal(read_object(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x0DU, &msb, 1U), 1);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x0EU, &lsb, 1U), 1);
    assert_int_equal(msb, 0x00U);
    assert_int_equal(lsb, 0x00U);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0110：DeviceAccessLocks 保留位                                     */
/* ------------------------------------------------------------------------- */

static void test_0110_access_locks_reserved_bits(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0110");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
    iolink_device_info_ctx_set_access_locks(test_env_info(&g_test_env), 0U);

    /* B.2.4 / Table B.12：位 0..3 已实现，位 4..15 为保留位。 */
    for (unsigned bit = 4U; bit <= 15U; bit++) {
        iolink_isdu_ctx_t ctx;
        const uint16_t locks = (uint16_t) (1U << bit);
        const uint8_t value[2] = {(uint8_t) (locks >> 8), (uint8_t) (locks & 0xFFU)};
        uint8_t response[8] = {0};

        int n = write_object(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, value, sizeof(value),
                             response, sizeof(response));
        assert_int_equal(n, 2);
        assert_int_equal(response[0], 0x80U);
        assert_int_equal(response[1], IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);

        uint8_t readback[2] = {0};
        assert_int_equal(
            read_object(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, readback, sizeof(readback)), 2);
        assert_int_equal(readback[0], 0x00U);
        assert_int_equal(readback[1], 0x00U);
    }
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0112 / SDCI_TC_0113：无配置文件时的 PD 描述符                      */
/* ------------------------------------------------------------------------- */

static void test_0112_pd_input_descriptor_absent(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0112");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t response[8] = {0};
    int n = read_object(&ctx, 0x000EU, 0U, response, sizeof(response));
    assert_int_equal(n, 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

static void test_0113_pd_output_descriptor_absent(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0113");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t response[8] = {0};
    int n = read_object(&ctx, 0x000FU, 0U, response, sizeof(response));
    assert_int_equal(n, 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0134：OffsetTime —— 条件项。当前 DUT 未实现。                      */
/* ------------------------------------------------------------------------- */

static void test_0134_offset_time_conditional(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0134");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t response[8] = {0};
    int n = read_object(&ctx, IOLINK_IDX_OFFSET_TIME, 0U, response, sizeof(response));

    if ((n == 2) && (response[0] == 0x80U) && (response[1] == IOLINK_ISDU_ERROR_IDX_NOT_AVAIL)) {
        /* 当前配置：未实现 OffsetTime（N/A）。 */
        return;
    }

    /* Test Spec 0134：时基 0/1/2 与乘数 0/63，在 Figure
       B.7 中编码为 (time base << 6) | multiplier。该记录为 2 字节。 */
    assert_int_equal(n, 2);
    const uint8_t values[] = {0x00U, 0x3FU, 0x40U, 0x7FU, 0x80U, 0xBFU};
    for (size_t i = 0U; i < (sizeof(values) / sizeof(values[0])); i++) {
        uint8_t written[2] = {values[i], 0x00U};
        uint8_t write_response[8] = {0};
        assert_int_equal(write_object(&ctx, IOLINK_IDX_OFFSET_TIME, 0U, written, sizeof(written),
                                      write_response, sizeof(write_response)),
                         0);
        uint8_t readback[2] = {0};
        assert_int_equal(read_object(&ctx, IOLINK_IDX_OFFSET_TIME, 0U, readback, sizeof(readback)),
                         2);
        assert_int_equal(readback[0], values[i]);
        assert_int_equal(readback[1], 0x00U);
    }
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0323：无 DS 参数集时的 DataStorageIndex 标记                       */
/* ------------------------------------------------------------------------- */

static void test_0323_no_ds_record_marker(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0323");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds);

    /* Test Spec 0323（不支持 DS 的设备）：Sub1 长 1，Sub2 长 1 且
       位 0 与位 3..6 为 0，Sub3 长 4 值 0，Sub4 长 4，Sub5 长 2 值 0。 */
    const uint8_t expected_len[6] = {0U, 1U, 1U, 4U, 4U, 2U};
    for (uint8_t sub = 1U; sub <= 5U; sub++) {
        iolink_isdu_ctx_t ctx;
        iolink_isdu_init(&ctx);
        test_isdu_bind_params(&ctx, &g_test_env);
        ctx.ds_ctx = &ds;
        uint8_t data[8] = {0xFFU};
        assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, sub), 1);
        iolink_isdu_process(&ctx);
        int n = isdu_collect_response(&ctx, data, sizeof(data));
        assert_int_equal(n, (int) expected_len[sub]);
        if (sub == 1U) {
            assert_true(data[0] <= 5U);
        }
        if (sub == 2U) {
            assert_int_equal((uint8_t) (data[0] & 0x79U), 0x00U);
        }
        if (sub == 3U) {
            assert_int_equal(data[0], 0x00U);
            assert_int_equal(data[1], 0x00U);
            assert_int_equal(data[2], 0x00U);
            assert_int_equal(data[3], 0x00U);
        }
        if (sub == 5U) {
            assert_int_equal(data[0], 0x00U);
            assert_int_equal(data[1], 0x00U);
        }
    }
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0072：通信中断保留未清除的事件                                     */
/* ------------------------------------------------------------------------- */

static void test_0072_comm_interruption_event_persists(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0072");

    iolink_events_ctx_t events;
    events_init(&events);
    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&events));

    /* DTU 复位/暂停：时间流逝并跑一次事件处理，只要原因未被移除，
       设备不会清除活动事件。 */
    test_clock_advance_ms(2U);
    iolink_events_process(&events);
    assert_true(iolink_events_flag(&events));

    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, 0x8C00U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0080：本地参数变更置起 DS_UPLOAD_FLAG                              */
/* ------------------------------------------------------------------------- */

static void test_0080_local_change_requests_upload(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0080");
    (void) state;

    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds);
    assert_int_equal((iolink_ds_state_property(&ds) & 0x80U), 0U);

    iolink_ds_request_upload(&ds);
    assert_int_equal((iolink_ds_state_property(&ds) & 0x80U), 0x80U);
    assert_true(ds.upload_event_pending);

    iolink_ds_clear_upload(&ds);
    assert_int_equal((iolink_ds_state_property(&ds) & 0x80U), 0U);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0073：冷重启不会重放过期的消失事件                                 */
/* ------------------------------------------------------------------------- */

static void test_0073_event_power_interruption(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0073");

    iolink_events_ctx_t events;
    events_init(&events);
    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending(&events));

    /* 模拟掉电：易失的事件内存被重新初始化。 */
    events_init(&events);
    assert_false(iolink_events_pending(&events));
    assert_int_equal(iolink_events_memory_read(&events, 0x00U), 0x00U);

    /* 应用重新宣告一个仍处于活动状态的事件。重连后的
       第一个事件是 "appears"，而不是过期的 "disappears"。 */
    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_t ev;
    assert_true(iolink_events_pop(&events, &ev));
    assert_int_equal(ev.code, 0x8C00U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0101：对保留 DPP1 的写入不得干扰通信                               */
/* ------------------------------------------------------------------------- */

static void test_0101_reserved_dpp1_write(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0101");

    setup_mock_phy();
    will_return(mock_phy_init, 0);

    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    for (unsigned value = 0U; value <= 0xFFU; value++) {
        uint8_t frame[3];
        frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_PAGE | 0x0EU);
        frame[1] = IOLINK_MSEQ_TYPE_0;
        frame[2] = (uint8_t) value;
        frame[1] = (uint8_t) (frame[1] | test_expected_checksum6(frame, sizeof(frame)));

        mock_phy_feed_byte(frame[0]);
        mock_phy_feed_byte(frame[1]);
        mock_phy_feed_byte(frame[2]);
        mock_phy_feed_chk();

        test_io_cycle(&dev.ctx);
        assert_tx_len(1);

        assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
        assert_false(iolink_events_pending(iolink_device_get_events_ctx(&dev.ctx)));
    }
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0061：保留的 16 位索引                                             */
/* ------------------------------------------------------------------------- */

static void test_0061_reserved_16bit_index(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0061");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t response[8] = {0};
    int n = read_object(&ctx, 0x7F32U, 0U, response, sizeof(response));
    assert_int_equal(n, 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0062：子索引不可用的 16 位索引                                     */
/* ------------------------------------------------------------------------- */

static void test_0062_16bit_subindex_unavailable(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0062");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t response[8] = {0};

    int n = read_object(&ctx, 0x0010U, 0U, response, sizeof(response));
    assert_true(n >= 1);
    assert_int_not_equal(response[0], 0x80U);

    n = read_object(&ctx, 0x0010U, 2U, response, sizeof(response));
    assert_int_equal(n, 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0111：无配置文件时的 ProfileCharacteristic                         */
/* ------------------------------------------------------------------------- */

static void test_0111_profile_characteristic_absent(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0111");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    iolink_isdu_ctx_t ctx;
    uint8_t response[8] = {0};
    int n = read_object(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0U, response, sizeof(response));
    assert_int_equal(n, 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0136 / SDCI_TC_0137：配置文件参数范围 0x31..0x3F                   */
/* ------------------------------------------------------------------------- */

static void test_0136_profile_parameters_read_absent(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0136");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    for (uint16_t index = 0x0031U; index <= 0x003FU; index++) {
        iolink_isdu_ctx_t ctx;
        uint8_t response[8] = {0};
        int n = read_object(&ctx, index, 0U, response, sizeof(response));
        assert_int_equal(n, 2);
        assert_int_equal(response[0], 0x80U);
        assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
}

static void test_0137_profile_parameters_write_absent(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0137");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    for (uint16_t index = 0x0031U; index <= 0x003FU; index++) {
        iolink_isdu_ctx_t ctx;
        uint8_t response[8] = {0};
        const uint8_t value = 0x00U;
        int n = write_object(&ctx, index, 0U, &value, 1U, response, sizeof(response));
        assert_int_equal(n, 2);
        assert_int_equal(response[0], 0x80U);
        assert_int_equal(response[1], IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0104：保留/依赖能力的 SystemCommand 代码                           */
/* ------------------------------------------------------------------------- */

static int gap_write_system_command(iolink_ds_ctx_t* ds, uint8_t cmd, uint8_t* out, size_t out_len)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    ctx.ds_ctx = ds;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U), 1);
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, out_len);
}

static void test_0104_system_command_ranges(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0104");

    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);

    for (unsigned value = 0U; value <= 0xFFU; value++) {
        iolink_ds_ctx_t ds;
        iolink_ds_init(&ds);

        uint8_t response[8] = {0};
        const int n = gap_write_system_command(&ds, (uint8_t) value, response, sizeof(response));

        bool implemented = false;
#if IOLINK_FEATURE_SYSTEM_COMMAND
        /* Table B.9：0x80-0x83 为 Device/Application 复位与恢复
           命令；本协议栈全部处理。 */
        if ((value >= 0x80U) && (value <= 0x83U)) {
            implemented = true;
        }
#if IOLINK_FEATURE_DATA_STORAGE
        /* 编入 DS 时会处理块参数化 + Data Storage 命令
           (0x01-0x06)。0x84-0x9F 仍为保留，包括
           0x95/0x96（旧的别名已移除）。 */
        if ((value >= 0x01U) && (value <= 0x06U)) {
            implemented = true;
        }
#endif
#endif
        if (!implemented) {
            /* 0x00、0x07-0x3F、0x40-0x7F、0x84-0x9F 和 0xA0-0xFF 应
               以 FUNC_NOTAVAIL (0x35) 应答。 */
            assert_int_equal(n, 2);
            assert_int_equal(response[0], 0x80U);
            assert_int_equal(response[1], IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
        }
        else {
            /* 受支持的命令可能因自身状态相关的原因失败，
               但绝不会以 FUNC_NOTAVAIL 失败。 */
            assert_false((n == 2) && (response[0] == 0x80U) &&
                         (response[1] == IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL));
        }
    }
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0054：VendorName 与合成的一致性身份一致                            */
/* ------------------------------------------------------------------------- */

static uint8_t gap_chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

static void test_0054_vendor_name_conformance_identity(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0054");

    const char* nvm = gap_nvm_path("0054");
    (void) remove(nvm);
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_gap_noop_phy, nvm),
        0);

    iolink_isdu_ctx_t* ctx = test_dll_isdu(&app.device);
    assert_int_equal(isdu_send_read_request(ctx, IOLINK_IDX_VENDOR_NAME, 0U), 1);

    /* 尚未应答：首次读的 START 必须返回 Busy (0x01)。 */
    uint8_t octet = 0U;
    iolink_isdu_od_read(ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x01U);

    iolink_isdu_process(ctx);

    const char* expected = app.profile->identity->vendor_name;
    const size_t expected_len = strlen(expected);

    /* A.5.3/A.5.6：正向读取，ExtLength 统计 data + I-Service/Length +
       CHKPDU，且 CHKPDU 为所有响应字节的 XOR。 */
    assert_true(expected_len > 13U);
    assert_int_equal(ctx->resp_total, (int) (expected_len + 3U));
    assert_int_equal(ctx->resp_buf[0], 0xD1U);
    assert_int_equal(ctx->resp_buf[1], (uint8_t) (expected_len + 3U));
    assert_memory_equal(&ctx->resp_buf[2], expected, expected_len);
    assert_int_equal(ctx->resp_buf[expected_len + 2U],
                     gap_chkpdu(ctx->resp_buf, expected_len + 2U));

    uint8_t data[64] = {0};
    int n = isdu_collect_response(ctx, data, sizeof(data));
    assert_int_equal(n, (int) expected_len);
    assert_memory_equal(data, expected, expected_len);

    conformance_device_close(&app);
    (void) remove(nvm);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0056：Config1 写入/回读（而非 FunctionTag 替代）                   */
/* ------------------------------------------------------------------------- */

static void test_0056_config1_write_readback(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0056");

    const char* nvm = gap_nvm_path("0056");
    (void) remove(nvm);
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_gap_noop_phy, nvm),
        0);

    iolink_isdu_ctx_t* ctx = test_dll_isdu(&app.device);
    uint8_t payload[CONFORMANCE_CONFIG1_MAX];
    for (size_t i = 0U; i < sizeof(payload); i++) {
        payload[i] = (uint8_t) (0xA0U + i);
    }

    assert_int_equal(isdu_send_write_request(ctx, CONFORMANCE_CONFIG1_INDEX, 0U, payload,
                                             (uint8_t) sizeof(payload)),
                     1);

    /* 在应用消费之前，写请求不会被应答。 */
    uint8_t octet = 0U;
    iolink_isdu_od_read(ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x01U); /* 忙 */

    iolink_isdu_process(ctx);

    /* 正向写响应：线上为 0x52 后跟 CHKPDU。 */
    uint8_t write_response[2] = {0};
    iolink_isdu_od_read(ctx, IOLINK_FLOWCTRL_START, &write_response[0], 1U);
    iolink_isdu_od_read(ctx, 1U, &write_response[1], 1U);
    assert_int_equal(write_response[0], 0x52U);
    assert_int_equal(write_response[1], gap_chkpdu(write_response, 1U));

    /* 通过 ISDU 回读 Config1 并校验其精确镜像。 */
    assert_int_equal(isdu_send_read_request(ctx, CONFORMANCE_CONFIG1_INDEX, 0U), 1);
    iolink_isdu_process(ctx);
    uint8_t readback[CONFORMANCE_CONFIG1_MAX] = {0};
    int n = isdu_collect_response(ctx, readback, sizeof(readback));
    assert_int_equal(n, (int) sizeof(payload));
    assert_memory_equal(readback, payload, sizeof(payload));

    /* FunctionTag 未被用作 Config1 的替代。 */
    assert_int_equal(isdu_send_read_request(ctx, IOLINK_IDX_FUNCTION_TAG, 0U), 1);
    iolink_isdu_process(ctx);
    uint8_t tag[8] = {0};
    assert_int_equal(isdu_collect_response(ctx, tag, sizeof(tag)), 0);

    conformance_device_close(&app);
    (void) remove(nvm);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0055：200 字节的长 ISDU 读取                                       */
/* ------------------------------------------------------------------------- */

static uint8_t g_long_data[200];

static iolink_result_t long_param_read(iolink_app_t* self, const iolink_od_entry_t* entry,
                                       uint8_t* out, size_t cap)
{
    (void) self;
    (void) entry;
    if (cap < sizeof(g_long_data)) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    }
    (void) memcpy(out, g_long_data, sizeof(g_long_data));
    return iolink_result_ok((uint16_t) sizeof(g_long_data));
}

static const iolink_od_entry_t k_long_entry[] = {
    {.index = 0x00E4U,
     .subindex = 0U,
     .max_subindex = 0U,
     .datatype = IOLINK_OD_DT_OCTET_STRING,
     .access = IOLINK_OD_ACCESS_RO,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = 0U,
     .handler = IOLINK_OD_HANDLER_NONE,
     .backend = IOLINK_OD_BACKEND_HANDLER,
     .u.handler = {.read = long_param_read, .write = NULL},
     .block_len = 200U,
     .range_min = 0U,
     .range_max = 0U,
     .scalar_default = 0U,
     .block_default = NULL,
     .name = "LongParameter",
     .validate = NULL,
     .on_change = NULL},
};

static void long_vendor_init(void)
{
    for (size_t i = 0U; i < sizeof(g_long_data); i++) {
        g_long_data[i] = (uint8_t) (0x30U + (i & 0x07U));
    }
    test_env_init(&g_test_env, NULL);
    test_env_reset_params(&g_test_env);
}

static void test_0055_200_byte_read(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0055");

    long_vendor_init();
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    test_isdu_bind_params(&ctx, &g_test_env);
    iolink_od_app_t app = {
        .entries = k_long_entry,
        .count = sizeof(k_long_entry) / sizeof(k_long_entry[0]),
    };
    iolink_isdu_bind_od(&ctx, &app, NULL);

    assert_int_equal(isdu_send_read_request(&ctx, 0x00E4U, 0U), 1);
    iolink_isdu_process(&ctx);

    /* A.5.3：200 个数据字节 -> I-Service/Length + ExtLength + data + CHKPDU。 */
    assert_int_equal(ctx.resp_total, 203);
    assert_int_equal(ctx.resp_buf[0], 0xD1U);
    assert_int_equal(ctx.resp_buf[1], 203U);
    assert_int_equal(ctx.resp_buf[202], gap_chkpdu(ctx.resp_buf, 202U));

    uint8_t data[200] = {0};
    assert_int_equal(isdu_collect_response(&ctx, data, sizeof(data)), (int) sizeof(data));
    assert_memory_equal(data, g_long_data, sizeof(g_long_data));
}

/* SDCI_TC_0055：通过一致性配置文件的应用 OD 后端读取合成的
 * Config3（200 字节）对象，包括 Busy 与 COUNT 回绕。 */
static void test_0055_config3_long_read(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0055");

    const char* nvm = gap_nvm_path("0055");
    (void) remove(nvm);
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_gap_noop_phy, nvm),
        0);
    assert_int_equal(app.config3_len, CONFORMANCE_CONFIG3_MAX);

    iolink_isdu_ctx_t* ctx = test_dll_isdu(&app.device);
    assert_int_equal(isdu_send_read_request(ctx, CONFORMANCE_CONFIG3_INDEX, 0U), 1);

    uint8_t octet = 0U;
    iolink_isdu_od_read(ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x01U); /* 在主循环应答前保持忙。 */

    iolink_isdu_process(ctx);

    const size_t total = CONFORMANCE_CONFIG3_MAX + 3U; /* D1 + ExtLength + data + CHKPDU */
    assert_int_equal(ctx->resp_total, (int) total);
    assert_int_equal(ctx->resp_buf[0], 0xD1U);
    assert_int_equal(ctx->resp_buf[1], (uint8_t) total);
    assert_int_equal(ctx->resp_buf[total - 1U], gap_chkpdu(ctx->resp_buf, total - 1U));

    /* 以 1 字节分段读取成帧的响应，并触发 COUNT 从 15 回绕到 0。 */
    uint8_t frame[CONFORMANCE_CONFIG3_MAX + 3U] = {0};
    uint8_t fc = IOLINK_FLOWCTRL_START;
    unsigned wraps = 0U;
    for (size_t i = 0U; i < total; i++) {
        iolink_isdu_od_read(ctx, fc, &frame[i], 1U);
        if (fc == 15U) {
            wraps++;
        }
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    assert_true(wraps >= 1U);
    assert_memory_equal(&frame[2], app.config3, CONFORMANCE_CONFIG3_MAX);
    assert_int_equal(frame[total - 1U], gap_chkpdu(frame, total - 1U));

    conformance_device_close(&app);
    (void) remove(nvm);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0069 / SDCI_TC_0070 / SDCI_TC_0075 / SDCI_TC_0076：事件            */
/* ------------------------------------------------------------------------- */

static void test_0069_single_event_operate(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0069");

    iolink_events_ctx_t events;
    events_init(&events);

    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_t event;
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.code, 0x8C00U);
    assert_int_equal(event.mode, IOLINK_EVENT_MODE_APPEARS);

    iolink_event_trigger(&events, 0x8C01U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.code, 0x8C01U);
}

static void test_0070_single_event_preoperate(void** state)
{
    (void) state;

    /* 事件引擎本身与状态无关；DLL 不会在 PREOPERATE
       丢弃事件。 */
    iolink_events_ctx_t events;
    events_init(&events);
    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&events));
}

static void test_0075_multi_event_order(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0075");

    iolink_events_ctx_t events;
    events_init(&events);

    /* Table D.1：0x8C00 是 Error，0x8C01 是 Warning。触发类型由
       注册表规范化，因此这里按规范类型触发（传错也会被规范化）。 */
    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&events, 0x8C01U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&events, 0x8C02U, IOLINK_EVENT_TYPE_NOTIFICATION);

    iolink_event_t all[8];
    uint8_t n = iolink_events_get_all(&events, all, 8U);
    assert_int_equal(n, 3);
    assert_int_equal(all[0].code, 0x8C00U); /* Error 先于 Warning */
    assert_int_equal(all[2].code, 0x8C02U); /* Notification 最后 */
}

static void test_0076_short_event(void** state)
{
    (void) state;
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0076");

    iolink_events_ctx_t events;
    events_init(&events);

    iolink_event_trigger(&events, 0x8C00U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_clear(&events, 0x8C00U);

    iolink_event_t event;
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.mode, IOLINK_EVENT_MODE_APPEARS);

    test_clock_advance_ms(60U);
    iolink_events_process(&events);
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

static int group_setup(void** state)
{
    (void) state;
    test_clock_install();
    return 0;
}

static int group_teardown(void** state)
{
    test_clock_uninstall();
    return test_tx_all_asserted(state);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_0063_write_length_overrun, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0064_write_length_underrun, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0123_application_tag_overlong, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0089_master_cycle_time_readback, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0092_revision_id_configured, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0097_function_id_zero, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0110_access_locks_reserved_bits, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0112_pd_input_descriptor_absent, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0113_pd_output_descriptor_absent, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0134_offset_time_conditional, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0323_no_ds_record_marker, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0072_comm_interruption_event_persists, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0080_local_change_requests_upload, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0073_event_power_interruption, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0101_reserved_dpp1_write, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0061_reserved_16bit_index, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0062_16bit_subindex_unavailable, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0111_profile_characteristic_absent, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0136_profile_parameters_read_absent, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0137_profile_parameters_write_absent, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0104_system_command_ranges, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0054_vendor_name_conformance_identity, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0056_config1_write_readback, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0055_200_byte_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0055_config3_long_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0069_single_event_operate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0070_single_event_preoperate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0075_multi_event_order, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0076_short_event, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, group_setup, group_teardown);
}
