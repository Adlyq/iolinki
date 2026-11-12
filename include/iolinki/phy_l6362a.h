/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOLINK_PHY_L6362A_H
#define IOLINK_PHY_L6362A_H
#include "iolinki/phy.h"
/* IN1 必须硬连线为低：CQ=IN2，RX=OUTI/Q（ST 表 14/15）。
 * EN 必须通过板上串联电阻驱动；DIAG 采样其 IC 侧。
 * init 将 EN 保持为低。uart_send_complete 抑制回显并等待最后一个
 * 停止位，带有限截止时间。consume_wakeup 消费 OL 下降沿 IRQ。
 * OL 是过载/电流限制，不是专用唤醒输出。 */
typedef struct
{
    void* user;
    int (*init)(void*);
    void (*set_enable)(void*, bool);
    void (*set_in2)(void*, bool);
    int (*uart_configure)(void*, uint32_t);
    int (*uart_send_complete)(void*, const uint8_t*, size_t);
    int (*uart_recv)(void*, uint8_t*);
    int (*consume_wakeup)(void*);
    bool (*read_ol)(void*);
    bool (*read_diag)(void*);
} iolink_l6362a_io_t;
typedef struct
{
    iolink_phy_api_t phy;
    iolink_l6362a_io_t io;
    iolink_phy_mode_t mode;
    uint32_t baud;
    int error;
    bool initialized, enabled, cq;
} iolink_l6362a_ctx_t;
int iolink_phy_l6362a_init(iolink_l6362a_ctx_t*, const iolink_l6362a_io_t*);
const iolink_phy_api_t* iolink_phy_l6362a_get(iolink_l6362a_ctx_t*);
/* 1=过载有效；0=无故障；-1=不可用。不推断短路/热故障原因。 */
int iolink_phy_l6362a_overload(const iolink_l6362a_ctx_t*);
/* DIAG 故障仅在 EN 请求为高时才有意义；否则为 -1。
 * 有效时聚合截止/UVLO/过温；无法识别具体原因。 */
int iolink_phy_l6362a_fault(const iolink_l6362a_ctx_t*);
#endif
