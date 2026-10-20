/**
  ******************************************************************************
  * @file    phy_l6362a_raw.h
  * @brief   iolinki raw-octet PHY adapter for the 8110 L6362A transceiver.
  *
  * The L6362A is a byte-stream UART transceiver (no SPI status register), so
  * this adapter synthesizes the ISR-path contract (IOLINK_ISR_RX_MODE /
  * IOLINK_ISR_TX_MODE):
  *   - RX BYTE : rx_octet from a ring delimited by a byte-gap time-out (TIM3);
  *   - RX FRAME: rx_frame from a DMA + USART IDLE whole-frame buffer;
  *   - TX BYTE : tx_octet / tx_done (one octet per USART TC interrupt);
  *   - TX FRAME: tx_frame via DMA (transfer complete -> tx_done).
  *
  * The iolinki library owns M-sequence framing, reply selection and the ISR
  * pump (dll.c / device.c); this file contains no frame/checksum knowledge.
  ******************************************************************************
  */

#ifndef PHY_L6362A_RAW_H
#define PHY_L6362A_RAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "iolinki/phy.h"

/**
  * @brief  PHY API to pass into iolink_device_config_t.phy.
  */
const iolink_phy_api_t *phy_l6362a_raw_get(void);

/**
  * @brief  Select the COM the PHY brings up at (call before hw_init).
  */
void phy_l6362a_raw_set_initial_baudrate(iolink_baudrate_t baudrate);

/**
  * @brief  One-time hardware bring-up: TIM3 (byte-gap
  *         timer), USART2 and the transceiver enable. Call before
  *         iolink_device_init().
  * @retval 0 on success, negative on error.
  */
int phy_l6362a_raw_hw_init(void);

/** @brief 1 if the L6362A INT/OL line is currently asserted, else 0. */
uint8_t phy_l6362a_raw_int_line_active(void);

/** @brief Read the PHY diagnostic counters (may be NULL). */
void phy_l6362a_raw_get_counters(uint32_t *tx_bytes, uint32_t *rx_bytes,
                                 uint32_t *rx_chk);

/* Interrupt feeds (called from stm32g0xx_it.c) */
void phy_l6362a_raw_rx_irq(void);        /**< USART2 RXNE(IDLE)/TC */
void phy_l6362a_raw_gap_irq(void);       /**< TIM3 byte-gap time-out (RX BYTE) */
void phy_l6362a_raw_tx_dma_irq(void);    /**< DMA1 ch2 complete (TX FRAME) */
void phy_l6362a_raw_ol_irq(void);        /**< OL_INT EXTI (wake latch) */

/* iolinki platform hooks (override the weak defaults in platform.c) */
void     iolink_critical_enter(void);
void     iolink_critical_exit(void);
uint64_t iolink_time_get_us(void);
uint32_t iolink_time_get_ms(void);
int      iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len);
int      iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PHY_L6362A_RAW_H */
