/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef IOLINK_PHY_TIOL112_H
#define IOLINK_PHY_TIOL112_H
#include "iolinki/phy.h"

#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN

/* MCU 适配器契约。不含引脚号、OS、编译器扩展或全局状态。
 * init 将 EN 配置为低电平，并为低有效 WAKE 和 NFAULT 配置上拉/IRQ。
 * 失败时必须保持 EN 为低；其他回调只需在成功后才工作。
 * uart_configure 选择 UART 引脚复用、8 个数据位、偶校验、1 个停止位。
 * uart_send_complete 必须仅在最后一个停止位离开引脚之后才返回，
 * 同时抑制本地 RX 回显并设置有限超时。返回字节数或负值。
 * set_sio_tx 选择 GPIO 复用并设置 TIOL112 TX 引脚的原始电平。
 * consume_wakeup 原子地消费 IRQ 锁存的低有效 WAKE 事件。
 * read_nfault 返回原始引脚电平（true = 无故障）；可选。 */
/**
 * @brief 板级适配器状态（不透明）。
 *
 * 板级代码在自己的翻译单元里定义 `struct iolink_tiol112_board`，把引脚、
 * UART 句柄等放进去；驱动只保存并原样透传这个指针，从不访问其字段。
 */
typedef struct iolink_tiol112_board iolink_tiol112_board_t;

typedef struct
{
    iolink_tiol112_board_t* IOLINK_NULLABLE board; /**< 板级状态；单例适配器可留 NULL */
    int (*init)(iolink_tiol112_board_t* IOLINK_NULLABLE board);
    void (*set_enable)(iolink_tiol112_board_t* IOLINK_NULLABLE board, bool enabled);
    void (*set_sio_tx)(iolink_tiol112_board_t* IOLINK_NULLABLE board, bool high);
    int (*uart_configure)(iolink_tiol112_board_t* IOLINK_NULLABLE board, uint32_t baud);
    int (*uart_send_complete)(iolink_tiol112_board_t* IOLINK_NULLABLE board, const uint8_t* data,
                              size_t len);
    int (*uart_recv)(iolink_tiol112_board_t* IOLINK_NULLABLE board, uint8_t* byte);
    int (*consume_wakeup)(iolink_tiol112_board_t* IOLINK_NULLABLE board);
    bool (*IOLINK_NULLABLE read_nfault)(iolink_tiol112_board_t* IOLINK_NULLABLE board);
} iolink_tiol112_io_t;

typedef struct
{
    iolink_phy_api_t phy;
    iolink_tiol112_io_t io;
    iolink_phy_mode_t mode;
    uint32_t baud;
    uint8_t cq;
    int error;
    bool initialized;
} iolink_tiol112_ctx_t;

/* 复制 IO 回调；driver 和 io.user 的生命周期必须长于设备。 */
int iolink_phy_tiol112_init(iolink_tiol112_ctx_t* driver, const iolink_tiol112_io_t* io);
const iolink_phy_api_t* IOLINK_NULLABLE iolink_phy_tiol112_get(iolink_tiol112_ctx_t* driver);
/* 聚合 NFAULT：欠压、温度或短路。不推断原因。
 * 有效时返回 1，无故障返回 0，未初始化或引脚不可用时返回 -1。 */
int iolink_phy_tiol112_fault(const iolink_tiol112_ctx_t* driver);

IOLINK_ASSUME_NONNULL_END
#endif
