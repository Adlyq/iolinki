#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/phy.h"

/* Mock PHY 函数 */
static int mock_init(iolink_app_t* IOLINK_NULLABLE self)
{
    (void) self;
    return 0;
}
static void mock_set_mode(iolink_app_t* IOLINK_NULLABLE self, iolink_phy_mode_t mode)
{
    (void) self;
    (void) mode;
}
static void mock_set_baudrate(iolink_app_t* IOLINK_NULLABLE self, iolink_baudrate_t baudrate)
{
    (void) self;
    check_expected(baudrate);
}
static const iolink_phy_api_t mock_phy = {
    .init = mock_init, .set_mode = mock_set_mode, .set_baudrate = mock_set_baudrate};

/* COM 是编译期常量：init 按 IOLINK_COM 配置一次 PHY，之后不可更改。 */
static void test_baudrate_is_compiled_in(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_value(mock_set_baudrate, baudrate, (iolink_baudrate_t) (IOLINK_COM - 1));
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);

    assert_int_equal(iolink_dll_get_baudrate(&ctx.main), (iolink_baudrate_t) (IOLINK_COM - 1));
    assert_int_equal(ctx.main.t_byte_limit_us, 16U * (1000000U / IOLINK_COM_HZ));
}

/* MinCycleTime 随编译期 COM 折叠；PD/OD 长度是 init 常量。 */
static void test_baudrate_min_cycle_time(void** state)
{
    (void) state;
    iolink_dll_ctx_t ctx;

    expect_any(mock_set_baudrate, baudrate);
    iolink_dll_init(&ctx.main, &ctx.isr, &mock_phy);
    ctx.main.shared.pd_in_len = 28U;
    ctx.main.shared.pd_out_len_current = 28U;
    ctx.main.shared.od_width = 1U;

#if IOLINK_COM == 3
    assert_int_equal(iolink_dll_compute_min_cycle_time(&ctx.main), 35U);
#elif IOLINK_COM == 2
    assert_int_equal(iolink_dll_compute_min_cycle_time(&ctx.main), 205U);
#else
    /* COM1 需要约 163 ms，被钳制到 0.1 ms 字节的 255 单位上限。 */
    assert_int_equal(iolink_dll_compute_min_cycle_time(&ctx.main), 255U);
#endif
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_baudrate_is_compiled_in),
        cmocka_unit_test(test_baudrate_min_cycle_time),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
