/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "iolinki/phy_l6362a.h"
static bool en, in2, ol = true, diag = true;
static int tx_result = 1, config_result;
static uint32_t baud;
static void reset_board(void)
{
    en = false;
    in2 = false;
    ol = true;
    diag = true;
    tx_result = 1;
    config_result = 0;
    baud = 0U;
}
static int start(void* u)
{
    (void) u;
    en = false;
    return 0;
}
static void enable(void* u, bool b)
{
    (void) u;
    en = b;
}
static void level(void* u, bool b)
{
    (void) u;
    in2 = b;
}
static int config(void* u, uint32_t b)
{
    (void) u;
    assert_false(en);
    baud = b;
    return config_result;
}
static int send_bytes(void* u, const uint8_t* b, size_t n)
{
    (void) u;
    (void) b;
    (void) n;
    assert_true(en);
    return tx_result;
}
static int recv_byte(void* u, uint8_t* b)
{
    (void) u;
    *b = 0xa5;
    return 1;
}
static int wake(void* u)
{
    (void) u;
    return 1;
}
static bool read_ol(void* u)
{
    (void) u;
    return ol;
}
static bool read_diag(void* u)
{
    (void) u;
    return diag;
}
static const iolink_l6362a_io_t g_io = {NULL,       start,     enable, level,   config,
                                        send_bytes, recv_byte, wake,   read_ol, read_diag};

static const iolink_phy_api_t* init_driver(iolink_l6362a_ctx_t* d)
{
    reset_board();
    assert_int_equal(iolink_phy_l6362a_init(d, &g_io), 0);
    const iolink_phy_api_t* p = iolink_phy_l6362a_get(d);
    assert_int_equal(p->init(p->self), 0);
    return p;
}

static void test_line_polarity_and_baudrate(void** state)
{
    (void) state;
    iolink_l6362a_ctx_t d;
    const iolink_phy_api_t* p = init_driver(&d);

    assert_false(en);
    p->set_cq_line(p->self, 1);
    p->set_mode(p->self, IOLINK_PHY_MODE_SIO);
    assert_true(en);
    assert_true(in2); /* L6362A 的 IN1=0 为同相，与 TIOL112 不同。 */
    p->set_cq_line(p->self, 0);
    assert_true(en);
    assert_false(in2);
    p->set_mode(p->self, IOLINK_PHY_MODE_SDCI);
    assert_false(en);
    assert_int_equal(baud, 4800);
    p->set_baudrate(p->self, IOLINK_BAUDRATE_COM3);
    assert_int_equal(baud, 230400);
}

static void test_diagnostics_overload_and_wakeup(void** state)
{
    (void) state;
    iolink_l6362a_ctx_t d;
    const iolink_phy_api_t* p = init_driver(&d);

    p->set_mode(p->self, IOLINK_PHY_MODE_SIO);
    assert_true(en);
    diag = false;
    assert_int_equal(iolink_phy_l6362a_fault(&d), 1);
    diag = true;
    p->set_mode(p->self, IOLINK_PHY_MODE_SDCI);
    assert_int_equal(iolink_phy_l6362a_fault(&d), -1);
    ol = false;
    assert_int_equal(iolink_phy_l6362a_overload(&d), 1);
    assert_int_equal(p->detect_wakeup(p->self), 1);
}

static void test_line_disabled_on_config_failure(void** state)
{
    (void) state;
    iolink_l6362a_ctx_t d;
    const iolink_phy_api_t* p = init_driver(&d);

    p->set_mode(p->self, IOLINK_PHY_MODE_SIO);
    assert_true(en);
    config_result = -1;
    p->set_mode(p->self, IOLINK_PHY_MODE_SDCI);
    assert_false(en);
    p->set_mode(p->self, IOLINK_PHY_MODE_INACTIVE);
    assert_false(en);
}

static void test_init_rejects_null_driver(void** state)
{
    (void) state;
    reset_board();
    assert_int_equal(iolink_phy_l6362a_init(NULL, &g_io), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_line_polarity_and_baudrate),
        cmocka_unit_test(test_diagnostics_overload_and_wakeup),
        cmocka_unit_test(test_line_disabled_on_config_failure),
        cmocka_unit_test(test_init_rejects_null_driver),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
