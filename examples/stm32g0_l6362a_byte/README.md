# STM32G0 + L6362A — byte-oriented ISR transport

Reference integration of the iolinki device stack on an STM32G070 with an
L6362A (8110) transceiver using the **byte-oriented** ISR transport.

- Build defines: `IOLINK_ISR_RX_MODE=IOLINK_ISR_RX_BYTE`,
  `IOLINK_ISR_TX_MODE=IOLINK_ISR_TX_BYTE` (the library defaults).
- RX: one USART2 RXNE interrupt per octet; the frame end is a byte-gap timer
  (`TIM3`, ~13 bit times). `rx_octet()` reports `CHK` on the gap.
- TX: one octet per USART2 TC interrupt; `tx_octet()`/`tx_done()`.
- Wake-up: SIO -> SDCI on the transceiver INT/OL line or RX activity.

`phy_l6362a_raw.c` / `.h` are the board adapter (copied from the IO-Link
firmware project). The CubeMX `main.c` / `app_iolink.c` glue is not duplicated
here; see the reference firmware project for the full startup (HAL init,
`MX_GPIO_Init`, `MX_USART2_UART_Init`, `MX_IoLink_Init`).

## Wiring (as used by the reference board)

| Signal | Pin |
| --- | --- |
| EN | PA0 |
| TX (USART2_TX -> C/Q) | PA2 |
| RX (USART2_RX <- C/Q) | PA3 |
| TX_EN | PA4 |
| OL_INT (overload / wake IRQ) | PA5 |
| RUN / ERR LED | PA7 / PA6 |
