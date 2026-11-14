/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "reference_device.h"
#include "test_helpers.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"

static void test_reference_device_payload(void** state)
{
    (void) state;
    reference_device_t app;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(reference_device_init(&app, &g_phy_mock, iolink_test_time_us), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&app.device), 3);
    assert_int_equal(iolink_device_get_pd_out_len(&app.device), 1);
    move_to_operate_ctx(&app.device);
    /* TYPE_2 过程通道 READ：MC + CKT + PD-out（OD 宽度 2 不参与读请求）。
       请求长度由配置决定，整帧传输下多送字节会被判为成帧错误。 */
    uint8_t frame[3] = {0x80, IOLINK_MSEQ_TYPE_2, 1};
    frame[1] |= iolink_checksum6(frame, sizeof(frame));
    for (size_t i = 0; i < sizeof(frame); i++) {
        mock_phy_feed_byte(frame[i]);
    }
    mock_phy_feed_chk();
    mock_phy_set_send_result(6);
    test_io_cycle(&app.device);
    assert_tx_len(6);
    assert_int_equal(reference_device_tick(&app, true), 0);
    assert_true(app.led);
    assert_int_equal(app.counter, 1);
    assert_int_equal(app.input[0], 0);
    assert_int_equal(app.input[1], 1);
    assert_int_equal(app.input[2], 1);
    assert_true(iolink_events_pending(iolink_device_get_events_ctx(&app.device)));
    app.counter = 65535;
    assert_int_equal(reference_device_tick(&app, false), 0);
    assert_int_equal(app.counter, 0);
    assert_int_equal(app.input[2], 0);
}

static void test_reference_device_rejects_missing_transport(void** state)
{
    (void) state;
    reference_device_t app;
    assert_int_equal(reference_device_init(NULL, &g_phy_mock, iolink_test_time_us), -1);
    assert_int_equal(reference_device_init(&app, NULL, iolink_test_time_us), -1);
    assert_int_equal(reference_device_tick(NULL, false), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reference_device_payload),
        cmocka_unit_test(test_reference_device_rejects_missing_transport),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
