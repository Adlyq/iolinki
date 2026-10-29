/* SPDX-License-Identifier: GPL-3.0-or-later */
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
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"
#include "conformance_test.h"
#include "test_helpers.h"

static char g_nvm_path[64];

static void cleanup_nvm_path(void)
{
    (void) remove(g_nvm_path);
}

/* Unique per process so parallel ctest runs in different build trees do not
   share the persistence file. */
static const char* nvm_path(void)
{
    if (g_nvm_path[0] == '\0') {
        (void) snprintf(g_nvm_path, sizeof(g_nvm_path), "/tmp/iolink-conformance-device-%ld.nvm",
                        (long) getpid());
        (void) atexit(cleanup_nvm_path);
    }
    return g_nvm_path;
}

static int noop_phy_init(void* user)
{
    (void) user;
    return 0;
}

static void noop_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void noop_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static const iolink_phy_api_t k_noop_phy = {
    .init = noop_phy_init,
    .set_mode = noop_phy_set_mode,
    .set_baudrate = noop_phy_set_baudrate,
};

static int test_setup(void** state)
{
    (void) state;
    (void) remove(nvm_path());
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    (void) remove(nvm_path());
    return 0;
}

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

static int read_object(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* out,
                       size_t out_len)
{
    if (isdu_send_read_request(ctx, index, subindex) != 1) {
        return -1;
    }
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_len);
}

/* 0155/0316/0317/0318: application-level reset observation hooks. */
static void test_conformance_reset_observation(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0316");
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_noop_phy, nvm_path()),
        0);

    const conformance_reset_observation_t* obs = conformance_device_reset_observation(&app);
    assert_non_null(obs);
    assert_int_equal(obs->startup_count, 1U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_POWER_ON);
    assert_true(conformance_device_is_default_state(&app));

    uint8_t value[CONFORMANCE_CONFIG1_MAX];
    (void) memset(value, 0x5A, sizeof(value));
    assert_int_equal(
        conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, value, sizeof(value)),
        0);
    assert_false(conformance_device_is_default_state(&app));

    /* 0x80 Device Reset preserves parameters and records the reason. */
    iolink_isdu_execute_system_command(&app.device.dll.isdu, IOLINK_CMD_DEVICE_RESET);
    conformance_device_process(&app);
    assert_int_equal(obs->device_reset_count, 1U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_DEVICE);
    assert_false(conformance_device_is_default_state(&app));

    /* 0x81 Application Reset restores application defaults. */
    iolink_isdu_execute_system_command(&app.device.dll.isdu, IOLINK_CMD_APPLICATION_RESET);
    conformance_device_process(&app);
    assert_int_equal(obs->application_reset_count, 1U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_APPLICATION);
    assert_true(conformance_device_is_default_state(&app));

    conformance_device_note_factory_reset(&app);
    assert_int_equal(obs->factory_reset_count, 1U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_FACTORY);
    assert_true(conformance_device_is_default_state(&app));

    conformance_device_note_back_to_box(&app);
    assert_int_equal(obs->back_to_box_count, 1U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_BACK_TO_BOX);
    assert_true(conformance_device_is_default_state(&app));

    assert_int_equal(conformance_device_power_cycle(&app, &k_noop_phy, nvm_path()), 0);
    assert_int_equal(obs->startup_count, 2U);
    assert_int_equal(obs->last_reset_reason, CONFORMANCE_RESET_POWER_ON);
    assert_int_equal(obs->device_reset_count, 1U);
    assert_int_equal(obs->application_reset_count, 1U);

    conformance_device_close(&app);
}

/* 0080: a local teach-in changes a parameter and requests a DS upload. */
static void test_conformance_local_change(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0080");
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_noop_phy, nvm_path()),
        0);

    const uint16_t before = app.config2;
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&app.device);
    assert_non_null(ds);
    const uint32_t checksum_before = iolink_ds_checksum(ds);

    assert_int_equal(conformance_device_local_change(&app), 0);
    assert_int_equal(app.config2, (uint16_t) (before + 1U));
    assert_int_equal(app.local_change_count, 1U);
    assert_int_equal(iolink_ds_state_property(ds) & 0x80U, 0x80U);
    assert_true(ds->upload_event_pending);
    assert_int_not_equal(iolink_ds_checksum(ds), checksum_before);

    conformance_device_close(&app);
}

/* 0373: DeviceStatus / DetailedDeviceStatus interlock before and after an event. */
static void test_conformance_event_device_status(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0373");
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_noop_phy, nvm_path()),
        0);

    iolink_isdu_ctx_t ctx;
    make_isdu_ctx(&app, &ctx);
    uint8_t response[64];

    assert_int_equal(read_object(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, response, sizeof(response)),
                     1);
    assert_int_equal(response[0], IOLINK_DEVICE_STATUS_OK);
    make_isdu_ctx(&app, &ctx);
    int len = read_object(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, response, sizeof(response));
    assert_int_equal(len, 0);

    assert_int_equal(conformance_device_inject_event(&app, 0x8C10U, IOLINK_EVENT_TYPE_WARNING), 0);
    make_isdu_ctx(&app, &ctx);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, response, sizeof(response)),
                     1);
    assert_int_equal(response[0], IOLINK_DEVICE_STATUS_OUT_OF_SPEC);
    make_isdu_ctx(&app, &ctx);
    len = read_object(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, response, sizeof(response));
    assert_int_equal(len, 3);
    assert_int_equal(response[1], 0x8CU);
    assert_int_equal(response[2], 0x10U);

    iolink_events_ctx_t* events = iolink_device_get_events_ctx(&app.device);
    iolink_events_memory_write(events, 0U, 0U);
    assert_int_equal(conformance_device_clear_event(&app, 0x8C10U), 0);
    (void) usleep(120000);
    iolink_events_process(events);
    make_isdu_ctx(&app, &ctx);
    len = read_object(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, response, sizeof(response));
    assert_int_equal(len, 0);
    iolink_events_memory_write(events, 0U, 0U);
    make_isdu_ctx(&app, &ctx);
    assert_int_equal(read_object(&ctx, IOLINK_IDX_DEVICE_STATUS, 0U, response, sizeof(response)),
                     1);
    assert_int_equal(response[0], IOLINK_DEVICE_STATUS_OK);

    conformance_device_close(&app);
}

static void test_conformance_full_profile(void** state)
{
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_noop_phy, nvm_path()),
        0);

    assert_int_equal(iolink_device_get_pd_in_len(&app.device), 3U);
    assert_int_equal(iolink_device_get_pd_out_len(&app.device), 1U);

    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&app.device);
    assert_non_null(ds);
    assert_int_equal(iolink_ds_param_count(ds), 10U);

    uint8_t buf[256];
    size_t len = sizeof(buf);
    assert_int_equal(conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, buf, &len),
                     0);
    assert_int_equal(len, CONFORMANCE_CONFIG1_MAX);
    assert_int_equal(buf[0], 0x49U);

    uint8_t too_short[63];
    (void) memset(too_short, 0x11, sizeof(too_short));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, too_short,
                                                    sizeof(too_short)),
                     IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);

    uint8_t too_long[65];
    (void) memset(too_long, 0x22, sizeof(too_long));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, too_long,
                                                    sizeof(too_long)),
                     IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    uint8_t config1[CONFORMANCE_CONFIG1_MAX];
    (void) memset(config1, 0x5A, sizeof(config1));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, config1,
                                                    sizeof(config1)),
                     0);
    len = sizeof(buf);
    assert_int_equal(conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, buf, &len),
                     0);
    assert_int_equal(len, sizeof(config1));
    assert_memory_equal(buf, config1, sizeof(config1));

    const uint8_t config2[2] = {0x12U, 0x34U};
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG2_INDEX, 0U, config2,
                                                    sizeof(config2)),
                     0);
    len = sizeof(buf);
    assert_int_equal(conformance_device_read_param(&app, CONFORMANCE_CONFIG2_INDEX, 0U, buf, &len),
                     0);
    assert_int_equal(len, 2U);
    assert_memory_equal(buf, config2, 2U);

    uint8_t config3[CONFORMANCE_CONFIG3_MAX];
    for (size_t i = 0U; i < sizeof(config3); i++) {
        config3[i] = (uint8_t) i;
    }
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG3_INDEX, 0U, config3,
                                                    sizeof(config3)),
                     0);
    len = sizeof(buf);
    assert_int_equal(conformance_device_read_param(&app, CONFORMANCE_CONFIG3_INDEX, 0U, buf, &len),
                     0);
    assert_int_equal(len, sizeof(config3));
    assert_memory_equal(buf, config3, sizeof(config3));

    uint8_t block[CONFORMANCE_CONFIG4_SIZE];
    (void) memset(block, 0xA5, sizeof(block));
    assert_int_equal(conformance_device_write_param(&app, CONFORMANCE_CONFIG4_INDEX0 + 2U, 0U,
                                                    block, sizeof(block)),
                     0);
    len = sizeof(buf);
    assert_int_equal(
        conformance_device_read_param(&app, CONFORMANCE_CONFIG4_INDEX0 + 2U, 0U, buf, &len), 0);
    assert_int_equal(len, sizeof(block));
    assert_memory_equal(buf, block, sizeof(block));

    assert_int_equal(conformance_device_inject_event(&app, 0x8C00U, IOLINK_EVENT_TYPE_WARNING), 0);
    assert_true(iolink_events_pending(iolink_device_get_events_ctx(&app.device)));
    assert_int_equal(conformance_device_local_change(&app), 0);
    assert_int_equal(iolink_ds_state_property(iolink_device_get_ds_ctx(&app.device)) & 0x80U,
                     0x80U);

    assert_int_equal(conformance_device_tick(&app, true), 0);
    assert_int_equal(app.counter, 1U);
    assert_int_equal(app.output[0], 0U);

    conformance_device_close(&app);
}

/* 0146: an illegal parameter structure during Download poisons the transfer;
 * DownloadEnd returns PAR_SETINCONSIST and the old value is kept. */
static void test_conformance_0146_illegal_download(void** state)
{
    IOLINK_CONFORMANCE_CASE("SDCI_TC_0146");
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_FULL),
                                &k_noop_phy, nvm_path()),
        0);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&app.device);
    assert_non_null(ds);

    uint8_t original[CONFORMANCE_CONFIG1_MAX];
    size_t original_len = sizeof(original);
    assert_int_equal(
        conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, original, &original_len),
        0);
    assert_int_equal(original_len, sizeof(original));

    assert_int_equal(iolink_ds_handle_ds_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);

    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = ds;
    ctx.vendor_service = app.config.vendor_service;
    ctx.vendor_user = app.config.user;

    uint8_t valid[CONFORMANCE_CONFIG1_MAX];
    (void) memset(valid, 0x5A, sizeof(valid));
    assert_int_equal(isdu_send_write_request(&ctx, CONFORMANCE_CONFIG1_INDEX, 0U, valid,
                                             (uint8_t) sizeof(valid)),
                     1);
    iolink_isdu_process(&ctx);
    uint8_t response[8];
    assert_int_equal(isdu_collect_response(&ctx, response, sizeof(response)), 0);

    uint8_t illegal[CONFORMANCE_CONFIG1_MAX + 1U];
    (void) memset(illegal, 0x6B, sizeof(illegal));
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = ds;
    ctx.vendor_service = app.config.vendor_service;
    ctx.vendor_user = app.config.user;
    assert_int_equal(isdu_send_write_request(&ctx, CONFORMANCE_CONFIG1_INDEX, 0U, illegal,
                                             (uint8_t) sizeof(illegal)),
                     1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, response, sizeof(response)), 2);
    assert_int_equal(response[0], 0x80U);
    assert_int_equal(response[1], IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);

    assert_int_equal(iolink_ds_handle_ds_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), -4);
    assert_int_equal(ds->state, IOLINK_DS_STATE_IDLE);

    uint8_t readback[CONFORMANCE_CONFIG1_MAX];
    size_t readback_len = sizeof(readback);
    assert_int_equal(
        conformance_device_read_param(&app, CONFORMANCE_CONFIG1_INDEX, 0U, readback, &readback_len),
        0);
    assert_int_equal(readback_len, sizeof(original));
    assert_memory_equal(readback, original, sizeof(original));

    conformance_device_close(&app);
}

static void test_conformance_pdout_only_profile(void** state)
{
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_PDOUT_ONLY),
                                &k_noop_phy, nvm_path()),
        0);
    assert_int_equal(iolink_device_get_pd_in_len(&app.device), 0U);
    assert_int_equal(iolink_device_get_pd_out_len(&app.device), 1U);
    assert_int_equal(conformance_device_tick(&app, false), 0);
    conformance_device_close(&app);
}

static void test_conformance_minimal_profile(void** state)
{
    (void) state;
    conformance_device_t app;
    assert_int_equal(
        conformance_device_init(&app, conformance_profile_get(CONFORMANCE_PROFILE_MINIMAL),
                                &k_noop_phy, nvm_path()),
        0);
    assert_int_equal(iolink_device_get_pd_in_len(&app.device), 0U);
    assert_int_equal(iolink_device_get_pd_out_len(&app.device), 0U);
    conformance_device_close(&app);
}

static void test_conformance_storage_persistence(void** state)
{
    (void) state;
    uint8_t written[2] = {0xABU, 0xCDU};

    conformance_file_storage_t first;
    assert_int_equal(conformance_file_storage_init(&first, nvm_path()), 0);
    assert_int_equal(first.api.write(first.api.user, 0U, written, sizeof(written)), 0);
    conformance_file_storage_close(&first);

    conformance_file_storage_t second;
    assert_int_equal(conformance_file_storage_init(&second, nvm_path()), 0);
    uint8_t readback[2] = {0};
    assert_int_equal(second.api.read(second.api.user, 0U, readback, sizeof(readback)), 0);
    assert_memory_equal(readback, written, sizeof(written));
    conformance_file_storage_close(&second);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_conformance_full_profile, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_0146_illegal_download, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_pdout_only_profile, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_minimal_profile, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_storage_persistence, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_reset_observation, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_local_change, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_conformance_event_device_status, test_setup,
                                        test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
