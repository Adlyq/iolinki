
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll_ctx.h"
#include "iolinki/phy.h"

/* Mock PHY 函数 */
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
static const iolink_phy_api_t mock_phy = {
    .init = mock_init, .set_mode = mock_set_mode, .set_baudrate = mock_set_baudrate};

static void test_baudrate_init(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    /* 初始化为 COM2 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);

    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), IOLINK_BAUDRATE_COM2);
}

static void test_baudrate_switch(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);

    /* 切换到 COM1 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    int ret = iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM1);
    assert_int_equal(ret, 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), IOLINK_BAUDRATE_COM1);

    /* 切换到 COM3 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    ret = iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM3);
    assert_int_equal(ret, 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), IOLINK_BAUDRATE_COM3);
}

static void test_operating_baudrate_reassert(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    assert_int_equal(iolink_dll_set_operating_baudrate(&ctx.main, IOLINK_BAUDRATE_COM3), 0);
    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), IOLINK_BAUDRATE_COM3);

    /* 恢复过程中降到 COM1 的动作，会在下一次主循环泵
     * 处理时由工作 COM 撤销。 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    assert_int_equal(iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM1), 0);
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    iolink_dll_process(&ctx.main);
    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), IOLINK_BAUDRATE_COM3);
}

static void test_operating_baudrate_unset(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);

    /* 未配置工作 COM：降到 COM1 后保持不变（旧行为）。 */
    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    assert_int_equal(iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM1), 0);
    iolink_dll_process(&ctx.main);
    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), IOLINK_BAUDRATE_COM1);
}

static void test_baudrate_recomputes_limits(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);
    ctx.main.pd_in_len_current = 28U;
    ctx.main.shared.pd_out_len_current = 28U;
    ctx.main.shared.od_width = 1U;

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM3);
    assert_int_equal(iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM3), 0);
    assert_int_equal(ctx.main.t_byte_limit_us, 16U * 4U);
    assert_int_equal(ctx.main.t_ren_limit_us, IOLINK_T_REN_US);
    assert_int_equal(ctx.main.min_cycle_time_us, 3500U);

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM2);
    assert_int_equal(iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM2), 0);
    assert_int_equal(ctx.main.t_byte_limit_us, 16U * 26U);
    assert_int_equal(ctx.main.t_ren_limit_us, IOLINK_T_REN_US);
    assert_int_equal(ctx.main.min_cycle_time_us, 20500U);

    expect_value(mock_set_baudrate, baudrate, IOLINK_BAUDRATE_COM1);
    assert_int_equal(iolink_dll_set_baudrate(&ctx.main, IOLINK_BAUDRATE_COM1), 0);
    assert_int_equal(ctx.main.t_byte_limit_us, 16U * 208U);
    assert_int_equal(ctx.main.t_ren_limit_us, IOLINK_T_REN_US);
    /* COM1 需要约 163 ms，被钳制到 0.1 ms 字节的 255 单位上限。 */
    assert_int_equal(ctx.main.min_cycle_time_us, 25500U);
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
