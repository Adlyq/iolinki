/* SPDX-License-Identifier: GPL-3.0-or-later */

/*
 * PD-input callback profile (IOLINK_APP_PD_INPUT_CALLBACK=1):
 *  - the polling API does not exist; a configuration without on_pd_input is
 *    rejected at init with IOLINK_DEVICE_INIT_ERR_PD_INPUT;
 *  - the callback fills a free PD-in buffer and its return value becomes the
 *    PD-valid flag.
 *
 * Built against a library copy compiled with the callback profile (see
 * tests/CMakeLists.txt).
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"

void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static uint64_t test_time_us(void* user)
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

static const iolink_storage_api_t g_storage = {.read = storage_read, .write = storage_write};

static int phy_init(void* user)
{
    (void) user;
    return 0;
}

static void phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}

static void phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}

static const iolink_phy_api_t g_phy = {.init = phy_init,
                                       .set_mode = phy_set_mode,
                                       .set_baudrate = phy_set_baudrate,
                                       .detect_wakeup = NULL};

static int g_pd_calls;
static uint8_t g_pd_pattern;
static void* g_expected_user;

static bool on_pd_input(void* user, uint8_t* data, uint8_t len)
{
    assert(user == g_expected_user);
    g_pd_calls++;
    (void) memset(data, g_pd_pattern, len);
    return true;
}

static const iolink_app_callbacks_t g_cbs = {.on_pd_input = on_pd_input};

static void make_config(iolink_device_config_t* cfg, const iolink_app_callbacks_t* cbs)
{
    (void) memset(cfg, 0, sizeof(*cfg));
    cfg->phy = g_phy;
    cfg->stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    cfg->stack.pd_in_len = 2U;
    cfg->stack.pd_out_len = 0U;
    cfg->app_callbacks = cbs;
#if IOLINK_FEATURE_DATA_STORAGE
    cfg->storage = &g_storage;
#endif
    cfg->time_us = test_time_us;
}

static void test_init_requires_on_pd_input(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;

    /* Real read of the link-time marker: also proves device.c defined it in
     * this profile (a wrong #if or a polling-profile fixture fails here). */
    assert(iolink_app_pd_input_callback_profile == 1U);

    make_config(&cfg, NULL);
    assert(iolink_device_init(&ctx, &cfg) == IOLINK_DEVICE_INIT_ERR_PD_INPUT);

    make_config(&cfg, &g_cbs);
    assert(iolink_device_init(&ctx, &cfg) == 0);
}

static void test_callback_fills_pd_input(void)
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;

    g_pd_calls = 0;
    g_pd_pattern = 0xA5U;
    static int user_sentinel;
    g_expected_user = &user_sentinel;
    make_config(&cfg, &g_cbs);
    cfg.user = g_expected_user;
    assert(iolink_device_init(&ctx, &cfg) == 0);

    /* Drive one main-loop pass in OPERATE: the callback publishes PD-in. */
    ctx.dll.state = IOLINK_DLL_STATE_OPERATE;
    iolink_device_process(&ctx);

    assert(g_pd_calls == 1);
    assert(ctx.dll.pd_valid);
    assert(ctx.dll.pd_in_len == 2U);
    assert(ctx.dll.pd_in[ctx.dll.pd_in_active][ctx.dll.od_width] == 0xA5U);
    assert(ctx.dll.pd_in[ctx.dll.pd_in_active][ctx.dll.od_width + 1U] == 0xA5U);
}

int main(void)
{
    test_init_requires_on_pd_input();
    test_callback_fills_pd_input();
    return 0;
}
