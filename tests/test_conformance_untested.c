/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Coverage for the Test Spec cases that the 387-item audit still listed as
 * untested. The cases that need a real power cycle are exercised at the
 * library boundary by re-initialising the device/parameter contexts; the
 * physical OFF/ON remains an external HIL step.
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

#include "conformance_device.h"
#include "conformance_profiles.h"
#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/device_info.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/params.h"
#include "iolinki/protocol.h"
#include "conformance_test.h"
#include "test_helpers.h"

#define TEST_NVM_SLOTS 8U
static char g_test_nvm[TEST_NVM_SLOTS][64];
static size_t g_test_nvm_count;

static void cleanup_test_nvm(void)
{
    for (size_t i = 0U; i < g_test_nvm_count; i++) {
        (void) remove(g_test_nvm[i]);
    }
}

/* One NVM file per process and tag: parallel ctest runs / build trees must not
   share the persistence file. */
static const char* unique_nvm_path(const char* tag)
{
    static int registered = 0;
    if (registered == 0) {
        (void) atexit(cleanup_test_nvm);
        registered = 1;
    }
    if (g_test_nvm_count >= TEST_NVM_SLOTS) {
        return g_test_nvm[TEST_NVM_SLOTS - 1U]; /* cannot happen: one path per case */
    }
    char* path = g_test_nvm[g_test_nvm_count];
    (void) snprintf(path, sizeof(g_test_nvm[0]), "/tmp/iolink-untested-%s-%ld.nvm", tag,
                    (long) getpid());
    (void) remove(path);
    g_test_nvm_count++;
    return path;
}

static int test_setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    iolink_ds_mock_reset();
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    iolink_ds_mock_reset();
    return 0;
}

static int noop_init(void* user)
{
    (void) user;
    return 0;
}

static void noop_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void noop_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static const iolink_phy_api_t k_noop_phy = {
    .init = noop_init,
    .set_mode = noop_set_mode,
    .set_baudrate = noop_set_baudrate,
};

static void make_isdu_ctx(conformance_device_t* app, iolink_isdu_ctx_t* ctx)
{
    iolink_isdu_init(ctx);
    ctx->event_ctx = iolink_device_get_events_ctx(&app->device);
    ctx->ds_ctx = iolink_device_get_ds_ctx(&app->device);
    ctx->dll_ctx = &app->device.dll;
    ctx->params_ctx = &app->device.params;
    ctx->vendor_service = app->config.vendor_service;
    ctx->vendor_user = app->config.user;
    ctx->direct_param_page2 = app->device.direct_param_page2;
}

/* The caller prepares the context (make_isdu_ctx() or an explicit init); do not
   re-initialise here or the bound device/params/DS pointers would be lost. */
static int isdu_read(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* out,
                     size_t out_len)
{
    assert_int_equal(isdu_send_read_request(ctx, index, subindex), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_len);
}

static int isdu_write(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, const uint8_t* data,
                      uint8_t data_len, uint8_t* out, size_t out_len)
{
    assert_int_equal(isdu_send_write_request(ctx, index, subindex, data, data_len), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_len);
}

/* Read ErrorCount through the ISDU context that is bound to a live device. */
static uint16_t device_error_count(iolink_device_ctx_t* device)
{
    /* ErrorCount is derived from the bound DLL counters, so bind them on a
       fresh context (iolink_isdu_init() clears the pointer fields). */
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &device->dll;
    uint8_t data[2] = {0U};
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0U), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, data, sizeof(data)), 2);
    return (uint16_t) (((uint16_t) data[0] << 8) | data[1]);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0063 / 0064: length error must leave Config1 untouched            */
/* ------------------------------------------------------------------------- */

static void test_0063_0064_length_errors_preserve_config1(void** state)
{
    (void) state;
    const char* nvm = unique_nvm_path("0063");
    (void) remove(nvm);

    conformance_device_t app;
    assert_int_equal(conformance_device_init(
                         &app, conformance_profile_get(CONFORMANCE_PROFILE_FULL), &k_noop_phy, nvm),
                     0);

    uint8_t before[CONFORMANCE_CONFIG1_MAX];
    size_t before_len = sizeof(before);
    assert_int_equal(
        conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, before, &before_len), 0);
    assert_int_equal(before_len, CONFORMANCE_CONFIG1_MAX);

    /* 0063: 70 octets is more than the fixed 64-octet length. */
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0063");
    uint8_t too_long[70];
    (void) memset(too_long, 0xA5, sizeof(too_long));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, too_long,
                                                    sizeof(too_long)),
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    uint8_t after_overrun[CONFORMANCE_CONFIG1_MAX];
    size_t after_overrun_len = sizeof(after_overrun);
    assert_int_equal(conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U,
                                                   after_overrun, &after_overrun_len),
                     0);
    assert_int_equal(after_overrun_len, CONFORMANCE_CONFIG1_MAX);
    assert_memory_equal(after_overrun, before, sizeof(before));

    /* 0064: 63 octets is one octet short. */
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0064");
    uint8_t too_short[63];
    (void) memset(too_short, 0x5A, sizeof(too_short));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, too_short,
                                                    sizeof(too_short)),
                     IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);
    uint8_t after_underrun[CONFORMANCE_CONFIG1_MAX];
    size_t after_underrun_len = sizeof(after_underrun);
    assert_int_equal(conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U,
                                                   after_underrun, &after_underrun_len),
                     0);
    assert_int_equal(after_underrun_len, CONFORMANCE_CONFIG1_MAX);
    assert_memory_equal(after_underrun, before, sizeof(before));

    conformance_device_close(&app);
    (void) remove(nvm);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0122 / 0123: ApplicationSpecificTag in OPERATE                    */
/* ------------------------------------------------------------------------- */

static void test_0122_application_tag_valid_persists(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0122");
    (void) state;
    const char* nvm = unique_nvm_path("0122");
    (void) remove(nvm);

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    /* The power cycle below re-initialises the PHY once more. */
    will_return(mock_phy_init, 0);

    conformance_device_t app;
    assert_int_equal(conformance_device_init(
                         &app, conformance_profile_get(CONFORMANCE_PROFILE_FULL), &g_phy_mock, nvm),
                     0);
    move_to_operate_ctx(&app.device);
    assert_int_equal(iolink_device_get_state(&app.device), IOLINK_DLL_STATE_OPERATE);

    const uint8_t tag[] = "0123456789ABCDEF01234567"; /* 24 octets, < fixed length */
    const uint8_t tag_len = (uint8_t) (sizeof(tag) - 1U);
    iolink_isdu_ctx_t ctx;
    make_isdu_ctx(&app, &ctx);
    uint8_t response[4] = {0U};
    assert_int_equal(
        isdu_write(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, tag, tag_len, response, sizeof(response)),
        0);

    make_isdu_ctx(&app, &ctx);
    uint8_t readback[33] = {0U};
    assert_int_equal(isdu_read(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, readback, sizeof(readback)),
                     tag_len);
    assert_memory_equal(readback, tag, tag_len);

    const conformance_reset_observation_t* obs = conformance_device_reset_observation(&app);
    const uint32_t startups_before = obs->startup_count;
    assert_int_equal(conformance_device_power_cycle(&app, &g_phy_mock, nvm), 0);
    assert_int_equal(obs->startup_count, startups_before + 1U);

    make_isdu_ctx(&app, &ctx);
    uint8_t after_power[33] = {0U};
    assert_int_equal(
        isdu_read(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, after_power, sizeof(after_power)), tag_len);
    assert_memory_equal(after_power, tag, tag_len);

    conformance_device_close(&app);
    (void) remove(nvm);
}

static void test_0123_application_tag_overlong_preserves_value(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0123");
    (void) state;
    const char* nvm = unique_nvm_path("0123");
    (void) remove(nvm);

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    conformance_device_t app;
    assert_int_equal(conformance_device_init(
                         &app, conformance_profile_get(CONFORMANCE_PROFILE_FULL), &g_phy_mock, nvm),
                     0);
    move_to_operate_ctx(&app.device);
    assert_int_equal(iolink_device_get_state(&app.device), IOLINK_DLL_STATE_OPERATE);

    const uint8_t tag[] = "KEEP-ME";
    iolink_isdu_ctx_t ctx;
    make_isdu_ctx(&app, &ctx);
    uint8_t response[4] = {0U};
    assert_int_equal(isdu_write(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, tag,
                                (uint8_t) (sizeof(tag) - 1U), response, sizeof(response)),
                     0);

    make_isdu_ctx(&app, &ctx);
    uint8_t before[33] = {0U};
    const int before_len = isdu_read(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, before, sizeof(before));
    assert_int_equal(before_len, (int) (sizeof(tag) - 1U));

    uint8_t too_long[33];
    (void) memset(too_long, 'B', sizeof(too_long));
    make_isdu_ctx(&app, &ctx);
    assert_int_equal(isdu_write(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, too_long,
                                (uint8_t) sizeof(too_long), response, sizeof(response)),
                     2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    make_isdu_ctx(&app, &ctx);
    uint8_t after[33] = {0U};
    const int after_len = isdu_read(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, after, sizeof(after));
    assert_int_equal(after_len, before_len);
    assert_memory_equal(after, before, (size_t) before_len);

    conformance_device_close(&app);
    (void) remove(nvm);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0128: DeviceStatus in OPERATE                                     */
/* ------------------------------------------------------------------------- */

static void test_0128_device_status_in_operate(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0128");
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    uint8_t status = 0xFFU;
    assert_int_equal(isdu_read(&dev.ctx.dll.isdu, IOLINK_IDX_DEVICE_STATUS, 0U, &status, 1U), 1);
    assert_true(status <= 4U);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0124: ErrorCount increments once and resets on power-on           */
/* ------------------------------------------------------------------------- */

static void test_0124_error_count_increment_and_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0124");
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    assert_int_equal(device_error_count(&dev.ctx), 0U);

    /* Stimulate exactly one error registered for ErrorCount: an RX timeout is
       counted once in the DLL. */
    iolink_dll_on_rx_timeout(&dev.ctx.dll);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(device_error_count(&dev.ctx), 1U);

    /* Power OFF/ON: the counter is reset and communication re-established. */
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(device_error_count(&dev.ctx), 0U);
}

/* ------------------------------------------------------------------------- */
/* SDCI_TC_0131: active Event remains visible after a restart                 */
/* ------------------------------------------------------------------------- */

static void test_0131_detailed_device_status_active_after_restart(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0131");
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_ctx_t events;
    iolink_events_init(&events);
    iolink_isdu_ctx_t ctx;

    iolink_event_trigger(&events, 0x8C10U, IOLINK_EVENT_TYPE_WARNING);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U), 1);
    iolink_isdu_process(&ctx);
    uint8_t before[64] = {0U};
    const int before_len = isdu_collect_response(&ctx, before, sizeof(before));
    assert_true(before_len >= 3);
    assert_int_equal(before_len % 3, 0);
    assert_int_equal(before[0], 0xE4U); /* appears | warning | application */
    assert_int_equal(before[1], 0x8CU);
    assert_int_equal(before[2], 0x10U);

    /* Power OFF/ON: volatile event memory is re-initialised, the still-active
       incident is announced again by the application. */
    iolink_events_init(&events);
    iolink_event_trigger(&events, 0x8C10U, IOLINK_EVENT_TYPE_WARNING);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U), 1);
    iolink_isdu_process(&ctx);
    uint8_t after[64] = {0U};
    const int after_len = isdu_collect_response(&ctx, after, sizeof(after));
    assert_int_equal(after_len, before_len);
    assert_memory_equal(after, before, (size_t) before_len);
}

/* ------------------------------------------------------------------------- */
/* Block parameter / Data Storage reset cases (0145, 0329)                   */
/* ------------------------------------------------------------------------- */

typedef struct
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_ds_ctx_t ds;
} reset_fixture_t;

static void reset_fixture_init(reset_fixture_t* f)
{
    iolink_device_info_ctx_init(&f->info, NULL);
    iolink_params_ctx_init(&f->params, &f->info);
    iolink_ds_init(&f->ds, NULL);
    iolink_ds_bind_params(&f->ds, &f->params);
}

static void reset_fixture_power_cycle(reset_fixture_t* f)
{
    iolink_ds_init(&f->ds, NULL);
    iolink_ds_bind_params(&f->ds, &f->params);
}

static int reset_fixture_isdu(reset_fixture_t* f, bool write, uint16_t index, uint8_t sub,
                              const uint8_t* data, uint8_t len, uint8_t* out, size_t cap)
{
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &f->ds;
    ctx.params_ctx = &f->params;
    if (write) {
        assert_int_equal(isdu_send_write_request(&ctx, index, sub, data, len), 1);
    }
    else {
        assert_int_equal(isdu_send_read_request(&ctx, index, sub), 1);
    }
    iolink_isdu_process(&ctx);
    return isdu_collect_response(&ctx, out, cap);
}

static int reset_fixture_tag(reset_fixture_t* f, uint8_t* out, size_t cap)
{
    return reset_fixture_isdu(f, false, IOLINK_IDX_APPLICATION_TAG, 0U, NULL, 0U, out, cap);
}

static void test_0145_download_break_by_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0145");
    (void) state;
    reset_fixture_t f;
    reset_fixture_init(&f);

    const uint8_t value1[] = "VALUE1";
    assert_int_equal(iolink_params_ctx_set(&f.params, IOLINK_IDX_APPLICATION_TAG, 0U, value1,
                                           sizeof(value1) - 1U, true),
                     0);
    uint8_t readback[16] = {0U};
    assert_int_equal(reset_fixture_tag(&f, readback, sizeof(readback)),
                     (int) (sizeof(value1) - 1U));

    uint8_t response[4] = {0U};
    const uint8_t start = IOLINK_CMD_PARAM_DOWNLOAD_START;
    assert_int_equal(reset_fixture_isdu(&f, true, IOLINK_IDX_SYSTEM_COMMAND, 0U, &start, 1U,
                                        response, sizeof(response)),
                     0);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_DOWNLOADING);

    const uint8_t value2[] = "VALUE2";
    assert_int_equal(reset_fixture_isdu(&f, true, IOLINK_IDX_APPLICATION_TAG, 0U, value2,
                                        sizeof(value2) - 1U, response, sizeof(response)),
                     0);

    /* Reset during the block download: the staged value is discarded. */
    reset_fixture_power_cycle(&f);
    (void) memset(readback, 0, sizeof(readback));
    assert_int_equal(reset_fixture_tag(&f, readback, sizeof(readback)),
                     (int) (sizeof(value1) - 1U));
    assert_memory_equal(readback, value1, sizeof(value1) - 1U);

    /* DownloadEnd in Idle after the reset is unexpected: FUNC_TEMP_UNAVAIL. */
    const uint8_t end = IOLINK_CMD_PARAM_DOWNLOAD_END;
    assert_int_equal(reset_fixture_isdu(&f, true, IOLINK_IDX_SYSTEM_COMMAND, 0U, &end, 1U, response,
                                        sizeof(response)),
                     2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);

    (void) memset(readback, 0, sizeof(readback));
    assert_int_equal(reset_fixture_tag(&f, readback, sizeof(readback)),
                     (int) (sizeof(value1) - 1U));
    assert_memory_equal(readback, value1, sizeof(value1) - 1U);
}

static void test_0329_upload_break_by_reset(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0329");
    (void) state;
    reset_fixture_t f;
    reset_fixture_init(&f);

    const uint8_t value1[] = "VALUE1";
    assert_int_equal(iolink_params_ctx_set(&f.params, IOLINK_IDX_APPLICATION_TAG, 0U, value1,
                                           sizeof(value1) - 1U, true),
                     0);

    uint8_t response[4] = {0U};
    const uint8_t upload_start = IOLINK_CMD_PARAM_UPLOAD_START;
    assert_int_equal(reset_fixture_isdu(&f, true, IOLINK_IDX_SYSTEM_COMMAND, 0U, &upload_start, 1U,
                                        response, sizeof(response)),
                     0);
    iolink_ds_process(&f.ds);
    assert_int_equal(f.ds.state, IOLINK_DS_STATE_UPLOADING);

    /* A parameter write during an active Upload is rejected with 0x8022. */
    const uint8_t value2[] = "VALUE2";
    assert_int_equal(reset_fixture_isdu(&f, true, IOLINK_IDX_APPLICATION_TAG, 0U, value2,
                                        sizeof(value2) - 1U, response, sizeof(response)),
                     2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);

    /* The reset aborts the upload; the write is accepted afterwards. */
    reset_fixture_power_cycle(&f);
    (void) memset(response, 0, sizeof(response));
    assert_int_equal(reset_fixture_isdu(&f, true, IOLINK_IDX_APPLICATION_TAG, 0U, value2,
                                        sizeof(value2) - 1U, response, sizeof(response)),
                     0);

    uint8_t readback[16] = {0U};
    assert_int_equal(reset_fixture_tag(&f, readback, sizeof(readback)),
                     (int) (sizeof(value2) - 1U));
    assert_memory_equal(readback, value2, sizeof(value2) - 1U);
}

/* ------------------------------------------------------------------------- */
/* IODD reset/restore sequences (0155, 0316, 0317, 0318)                     */
/* ------------------------------------------------------------------------- */

static void conformance_open(conformance_device_t* app, const char* nvm,
                             const iolink_phy_api_t* phy)
{
    (void) remove(nvm);
    assert_int_equal(
        conformance_device_init(app, conformance_profile_get(CONFORMANCE_PROFILE_FULL), phy, nvm),
        0);
}

static void conformance_send_system_command(conformance_device_t* app, uint8_t cmd)
{
    /* Run on the Device-owned ISDU context so reset_pending/flags reach the
       device pump, exactly as the DLL transport would. */
    iolink_isdu_execute_system_command(&app->device.dll.isdu, cmd);
    conformance_device_process(app);
}

static uint8_t conformance_ds_flag(conformance_device_t* app)
{
    iolink_isdu_ctx_t ctx;
    make_isdu_ctx(app, &ctx);
    /* Subindex 2 is State_Property (DS_UPLOAD_FLAG is bit 7). */
    uint8_t state = 0xFFU;
    assert_int_equal(isdu_read(&ctx, IOLINK_IDX_DATA_STORAGE, 2U, &state, 1U), 1);
    return state;
}

static void test_0316_device_reset_sequence(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0316");
    (void) state;
    const char* nvm = unique_nvm_path("0316");
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    will_return(mock_phy_init, 0);
    conformance_device_t app;
    conformance_open(&app, nvm, &g_phy_mock);
    move_to_operate_ctx(&app.device);
    assert_int_equal(iolink_device_get_state(&app.device), IOLINK_DLL_STATE_OPERATE);

    const conformance_reset_observation_t* obs = conformance_device_reset_observation(&app);
    const uint32_t startup_before = obs->startup_count;

    uint8_t before[CONFORMANCE_CONFIG1_MAX];
    size_t before_len = sizeof(before);
    assert_int_equal(
        conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, before, &before_len), 0);
    const uint16_t vendor_id = app.profile->identity->vendor_id;
    const uint32_t device_id = app.profile->identity->device_id;
    const uint16_t revision_id = app.profile->identity->revision_id;

    conformance_send_system_command(&app, IOLINK_CMD_DEVICE_RESET);
    iolink_isdu_ctx_t ctx;
    assert_int_equal(obs->device_reset_count, 1U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_DEVICE);

    /* Exactly one communication startup sequence after the reset. */
    assert_int_equal(conformance_device_power_cycle(&app, &g_phy_mock, nvm), 0);
    assert_int_equal(obs->startup_count, startup_before + 1U);

    /* RID, DID and the selected parameter survive the restart. */
    assert_int_equal(app.profile->identity->vendor_id, vendor_id);
    assert_int_equal(app.profile->identity->device_id, device_id);
    assert_int_equal(app.profile->identity->revision_id, revision_id);

    uint8_t after[CONFORMANCE_CONFIG1_MAX];
    size_t after_len = sizeof(after);
    assert_int_equal(
        conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, after, &after_len), 0);
    assert_int_equal(after_len, before_len);
    assert_memory_equal(after, before, sizeof(before));

    make_isdu_ctx(&app, &ctx);
    uint8_t status = 0xFFU;
    assert_int_equal(isdu_read(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, &status, 1U), 1);
    assert_int_equal(status, 0U);

    conformance_device_close(&app);
    (void) remove(nvm);
}

static void test_0317_application_reset_sequence(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0317");
    (void) state;
    const char* nvm = unique_nvm_path("0317");
    conformance_device_t app;
    conformance_open(&app, nvm, &k_noop_phy);
    const conformance_reset_observation_t* obs = conformance_device_reset_observation(&app);
    const uint32_t startup_before = obs->startup_count;

    iolink_isdu_ctx_t ctx;
    uint8_t response[4] = {0U};
    const uint8_t app_tag[] = "APP";
    const uint8_t fun_tag[] = "FUN";
    const uint8_t loc_tag[] = "LOC";
    make_isdu_ctx(&app, &ctx);
    assert_int_equal(isdu_write(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, app_tag,
                                (uint8_t) (sizeof(app_tag) - 1U), response, sizeof(response)),
                     0);
    make_isdu_ctx(&app, &ctx);
    assert_int_equal(isdu_write(&ctx, IOLINK_IDX_FUNCTION_TAG, 0U, fun_tag,
                                (uint8_t) (sizeof(fun_tag) - 1U), response, sizeof(response)),
                     0);
    make_isdu_ctx(&app, &ctx);
    assert_int_equal(isdu_write(&ctx, IOLINK_IDX_LOCATION_TAG, 0U, loc_tag,
                                (uint8_t) (sizeof(loc_tag) - 1U), response, sizeof(response)),
                     0);

    uint8_t changed[CONFORMANCE_CONFIG1_MAX];
    (void) memset(changed, 0x7E, sizeof(changed));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, changed,
                                                    sizeof(changed)),
                     0);
    assert_false(conformance_device_is_default_state(&app));

    const uint16_t vendor_id = app.profile->identity->vendor_id;
    const uint32_t device_id = app.profile->identity->device_id;
    const uint16_t revision_id = app.profile->identity->revision_id;

    conformance_send_system_command(&app, IOLINK_CMD_APPLICATION_RESET);
    assert_int_equal(obs->application_reset_count, 1U);
    /* No communication startup sequence for an application reset. */
    assert_int_equal(obs->startup_count, startup_before);
    assert_true(conformance_device_is_default_state(&app));

    /* Identification and writable tags are preserved. */
    assert_int_equal(app.profile->identity->vendor_id, vendor_id);
    assert_int_equal(app.profile->identity->device_id, device_id);
    assert_int_equal(app.profile->identity->revision_id, revision_id);

    make_isdu_ctx(&app, &ctx);
    uint8_t tag_read[33] = {0U};
    assert_int_equal(isdu_read(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, tag_read, sizeof(tag_read)),
                     (int) (sizeof(app_tag) - 1U));
    assert_memory_equal(tag_read, app_tag, sizeof(app_tag) - 1U);

    /* DS_UPLOAD_REQ event and DS_UPLOAD_FLAG are set. */
    iolink_event_t event;
    assert_true(iolink_events_pop(iolink_device_get_events_ctx(&app.device), &event));
    assert_int_equal(event.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    assert_int_equal((conformance_ds_flag(&app) & 0x80U), 0x80U);

    conformance_device_close(&app);
    (void) remove(nvm);
}

static void test_0155_factory_settings_sequence(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0155");
    (void) state;
    const char* nvm = unique_nvm_path("0155");
    conformance_device_t app;
    conformance_open(&app, nvm, &k_noop_phy);
    const conformance_reset_observation_t* obs = conformance_device_reset_observation(&app);
    const uint32_t startup_before = obs->startup_count;

    uint8_t changed[CONFORMANCE_CONFIG1_MAX];
    (void) memset(changed, 0x6C, sizeof(changed));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, changed,
                                                    sizeof(changed)),
                     0);
    const uint8_t tag[] = "TAG-VALUE";
    iolink_isdu_ctx_t ctx;
    make_isdu_ctx(&app, &ctx);
    uint8_t response[4] = {0U};
    assert_int_equal(isdu_write(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, tag,
                                (uint8_t) (sizeof(tag) - 1U), response, sizeof(response)),
                     0);

    conformance_send_system_command(&app, IOLINK_CMD_PARAM_DOWNLOAD_STORE);
    assert_int_equal((conformance_ds_flag(&app) & 0x80U), 0x80U);

    conformance_send_system_command(&app, IOLINK_CMD_RESTORE_FACTORY_SETTINGS);
    /* Application-level completion hook for the factory restore. */
    conformance_device_note_factory_reset(&app);
    assert_int_equal(obs->factory_reset_count, 1U);
    /* No or exactly one startup sequence. */
    assert_true(obs->startup_count == startup_before || obs->startup_count == startup_before + 1U);
    /* Defaults and identification. */
    assert_true(conformance_device_is_default_state(&app));
    assert_int_equal(app.profile->identity->revision_id, 0x0111U);
    assert_int_equal(app.profile->identity->vendor_id, 1234U);
    assert_int_equal(app.profile->identity->device_id, 5680U);

    make_isdu_ctx(&app, &ctx);
    uint8_t tag_read[33] = {0U};
    assert_int_equal(isdu_read(&ctx, IOLINK_IDX_APPLICATION_TAG, 0U, tag_read, sizeof(tag_read)),
                     0);

    make_isdu_ctx(&app, &ctx);
    uint8_t status = 0xFFU;
    assert_int_equal(isdu_read(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, &status, 1U), 1);
    assert_int_equal(status, 0U);

    assert_int_equal((conformance_ds_flag(&app) & 0x80U), 0U);
    conformance_device_close(&app);
    (void) remove(nvm);
}

static void test_0318_back_to_box_sequence(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0318");
    (void) state;
    const char* nvm = unique_nvm_path("0318");
    conformance_device_t app;
    conformance_open(&app, nvm, &k_noop_phy);
    const conformance_reset_observation_t* obs = conformance_device_reset_observation(&app);

    uint8_t changed[CONFORMANCE_CONFIG1_MAX];
    (void) memset(changed, 0x5B, sizeof(changed));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, changed,
                                                    sizeof(changed)),
                     0);
    conformance_send_system_command(&app, IOLINK_CMD_PARAM_DOWNLOAD_STORE);
    assert_int_equal((conformance_ds_flag(&app) & 0x80U), 0x80U);

    const uint32_t startups_before = obs->startup_count;
    conformance_send_system_command(&app, IOLINK_CMD_BACK_TO_BOX);
    /* Application-level completion hook for Back-to-box. */
    conformance_device_note_back_to_box(&app);
    assert_int_equal(obs->back_to_box_count, 1U);
    assert_true(conformance_device_is_default_state(&app));

    /* A power cycle after Back-to-box performs exactly one startup sequence. */
    assert_int_equal(conformance_device_power_cycle(&app, &k_noop_phy, nvm), 0);
    assert_int_equal(obs->startup_count, startups_before + 1U);
    assert_true(conformance_device_is_default_state(&app));
    assert_int_equal(app.profile->identity->revision_id, 0x0111U);
    assert_int_equal(app.profile->identity->vendor_id, 1234U);
    assert_int_equal(app.profile->identity->device_id, 5680U);

    assert_int_equal((conformance_ds_flag(&app) & 0x80U), 0U);
    conformance_device_close(&app);
    (void) remove(nvm);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_0063_0064_length_errors_preserve_config1, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0122_application_tag_valid_persists, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0123_application_tag_overlong_preserves_value,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0128_device_status_in_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0124_error_count_increment_and_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0131_detailed_device_status_active_after_restart,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0145_download_break_by_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0329_upload_break_by_reset, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0316_device_reset_sequence, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_0317_application_reset_sequence, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0155_factory_settings_sequence, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_0318_back_to_box_sequence, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
