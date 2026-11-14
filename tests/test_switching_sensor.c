/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "switching_sensor.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

/* 应用逻辑：采样、迟滞、输出字节与无效读数。
 * 参数语义（0x0100-0x0103 的读写/范围/teach）由 test_od_app.c 通过对象
 * 字典入口覆盖，这里不再复制一份线上编解码。 */
static void test_sampling_hysteresis(void** state)
{
    (void) state;
    switching_sensor_t sensor;

    switching_sensor_init(&sensor);
    assert_false(switching_sensor_sample(&sensor, 4900, true)); /* 未达阈值 */
    assert_true(switching_sensor_sample(&sensor, 5000, true));  /* 越限导通 */
    assert_true(switching_sensor_sample(&sensor, 4900, true));  /* 迟滞内保持 */
    assert_false(switching_sensor_sample(&sensor, 4800, true)); /* 越迟滞关断 */
    assert_false(switching_sensor_sample(&sensor, 4700, true)); /* 持续关断 */
}

/* 反相输出：同一采样值下输出翻转（值层，不经协议）。 */
static void test_inverted_output(void** state)
{
    (void) state;
    switching_sensor_t sensor;

    switching_sensor_init(&sensor);
    assert_true(switching_sensor_value_set(&sensor, SWITCHING_SENSOR_IDX_INVERSION, 1U));
    (void) switching_sensor_sample(&sensor, 5100, true);
    assert_int_equal(sensor.pd[2], 2U); /* valid 位 + 反相后未激活 */
}

/* 无效读数清除输出与有效性标志。 */
static void test_invalid_reading_clears_output(void** state)
{
    (void) state;
    switching_sensor_t sensor;

    switching_sensor_init(&sensor);
    assert_false(switching_sensor_sample(&sensor, 6000, false));
    assert_int_equal(sensor.pd[2], 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sampling_hysteresis),
        cmocka_unit_test(test_inverted_output),
        cmocka_unit_test(test_invalid_reading_clears_output),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
