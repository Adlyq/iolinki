/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include "iolinki/phy_tiol112.h"
#include "test_null.h"

/* 板级适配器状态由板级代码定义；测试里这一份就是 `struct iolink_tiol112_board`。 */
struct iolink_tiol112_board
{
    bool enabled, tx_high, fault_high, wake, ready;
    int result, init_result, config_result;
    unsigned baud;
};
static int init_board(iolink_tiol112_board_t* p)
{
    p->ready = p->init_result == 0;
    return p->init_result;
}
static void enable(iolink_tiol112_board_t* p, bool v)
{
    assert_true(p->ready);
    p->enabled = v;
}
static void sio_tx(iolink_tiol112_board_t* p, bool v)
{
    p->tx_high = v;
}
static int configure(iolink_tiol112_board_t* p, uint32_t baud)
{
    p->baud = baud;
    return p->config_result;
}
static int transmit(iolink_tiol112_board_t* board, const uint8_t* b, size_t n)
{
    assert_true(board->enabled);
    assert_non_null(b);
    return board->result < 0 ? board->result : (int) n;
}
static int receive(iolink_tiol112_board_t* p, uint8_t* b)
{
    (void) p;
    (void) b;
    return 0;
}
static int consume_wake(iolink_tiol112_board_t* b)
{
    int v = b->wake;
    b->wake = false;
    return v;
}
static bool fault_high(iolink_tiol112_board_t* p)
{
    return p->fault_high;
}

static void test_driver_modes_and_turnaround(void** state)
{
    (void) state;
    struct iolink_tiol112_board board = {.fault_high = true};
    const iolink_tiol112_io_t io = {
        .board = &board,
        .init = init_board,
        .set_enable = enable,
        .set_sio_tx = sio_tx,
        .uart_configure = configure,
        .uart_send_complete = transmit,
        .uart_recv = receive,
        .consume_wakeup = consume_wake,
        .read_nfault = fault_high,
    };
    iolink_tiol112_ctx_t driver;
    assert_int_equal(iolink_phy_tiol112_init(&driver, &io), 0);
    const iolink_phy_api_t* phy = iolink_phy_tiol112_get(&driver);
    assert_int_equal(phy->init(phy->self), 0);
    assert_false(board.enabled);
    phy->set_mode(phy->self, IOLINK_PHY_MODE_SIO);
    phy->set_cq_line(phy->self, 1);
    assert_true(board.enabled);
    assert_false(board.tx_high); /* TI 真值表：TX 为低时 CQ 为高 */
    phy->set_cq_line(phy->self, 0);
    assert_true(board.tx_high);
    phy->set_mode(phy->self, IOLINK_PHY_MODE_SDCI);
    assert_false(board.enabled);
    assert_int_equal(board.baud, IOLINK_COM_HZ);
    board.wake = true;
    assert_int_equal(phy->detect_wakeup(phy->self), 1);
    assert_int_equal(phy->detect_wakeup(phy->self), 0);
    assert_int_equal(iolink_phy_tiol112_fault(&driver), 0);
    board.fault_high = false;
    assert_int_equal(iolink_phy_tiol112_fault(&driver), 1);
    phy->set_mode(phy->self, IOLINK_PHY_MODE_INACTIVE);
    board.init_result = -3;
    assert_int_equal(phy->init(phy->self), -3);
    phy->set_mode(phy->self, IOLINK_PHY_MODE_SDCI);
    assert_false(board.enabled);
    board.init_result = 0;
    assert_int_equal(phy->init(phy->self), 0);
    board.config_result = -4;
    phy->set_mode(phy->self, IOLINK_PHY_MODE_SDCI);
    assert_false(board.enabled);
}

static void test_driver_rejects_incomplete_port(void** state)
{
    (void) state;
    iolink_tiol112_ctx_t driver;
    const iolink_tiol112_io_t io = {0};
    assert_int_equal(iolink_phy_tiol112_init(&driver, &io), -1);
    assert_int_equal(iolink_phy_tiol112_init(TEST_NULL(iolink_tiol112_ctx_t*), &io), -1);
    assert_null(iolink_phy_tiol112_get(TEST_NULL(iolink_tiol112_ctx_t*)));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_driver_modes_and_turnaround),
        cmocka_unit_test(test_driver_rejects_incomplete_port),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
