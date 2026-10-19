
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/phy.h"

/* Mock PHY functions */
static int mock_init(void* user)
{
    (void) user;
    return 0;
}
static void mock_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}
static void mock_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    check_expected(baudrate);
}
static int mock_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    return (int) len;
}
static int mock_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}

static const iolink_phy_api_t mock_phy = {.init = mock_init,
                                          .set_mode = mock_set_mode,
                                          .set_baudrate = mock_set_baudrate,
                                          .send = mock_send,
                                          .recv_byte = mock_recv_byte};

static void test_baudrate_init(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* Initialized to COM2 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    iolink_dll_init(&ctx, &mock_phy);

    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM2);
}

static void test_baudrate_switch(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx, &mock_phy);

    /* Switch to COM1 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    int ret = iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM1);
    assert_int_equal(ret, 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM1);

    /* Switch to COM3 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    ret = iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM3);
    assert_int_equal(ret, 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM3);
}

static void test_operating_baudrate_reassert(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx, &mock_phy);

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    assert_int_equal(iolink_dll_set_operating_baudrate(&ctx, IOLINK_BAUDRATE_COM3), 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM3);

    /* A recovery drop to COM1 is undone by the operating COM on the next main
     * pump pass. */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    assert_int_equal(iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM1), 0);
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    iolink_dll_process(&ctx);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM3);
}

static void test_operating_baudrate_unset(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx, &mock_phy);

    /* No operating COM configured: a COM1 drop is left alone (legacy). */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    assert_int_equal(iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM1), 0);
    iolink_dll_process(&ctx);
    assert_int_equal(iolink_dll_get_baudrate(&ctx), IOLINK_BAUDRATE_COM1);
}

static void test_baudrate_recomputes_limits(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx, &mock_phy);
    ctx.pd_in_len_current = 28U;
    ctx.pd_out_len_current = 28U;
    ctx.od_width = 1U;

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    assert_int_equal(iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM3), 0);
    assert_int_equal(ctx.t_byte_limit_us, 16U * 4U);
    assert_int_equal(ctx.t_ren_limit_us, IOLINK_T_REN_US);
    assert_int_equal(ctx.min_cycle_time_us, 3500U);

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    assert_int_equal(iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM2), 0);
    assert_int_equal(ctx.t_byte_limit_us, 16U * 26U);
    assert_int_equal(ctx.t_ren_limit_us, IOLINK_T_REN_US);
    assert_int_equal(ctx.min_cycle_time_us, 20500U);

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    assert_int_equal(iolink_dll_set_baudrate(&ctx, IOLINK_BAUDRATE_COM1), 0);
    assert_int_equal(ctx.t_byte_limit_us, 16U * 208U);
    assert_int_equal(ctx.t_ren_limit_us, IOLINK_T_REN_US);
    /* COM1 needs ~163 ms, clamped to the 0.1 ms octet's 255-unit maximum. */
    assert_int_equal(ctx.min_cycle_time_us, 25500U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_baudrate_init),
        cmocka_unit_test(test_baudrate_switch),
        cmocka_unit_test(test_operating_baudrate_reassert),
        cmocka_unit_test(test_operating_baudrate_unset),
        cmocka_unit_test(test_baudrate_recomputes_limits),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
