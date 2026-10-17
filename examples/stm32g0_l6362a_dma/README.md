# STM32G0 + L6362A — whole-frame DMA transport

Reference integration of the iolinki device stack on an STM32G070 with an
L6362A (8110) transceiver using the **whole-frame DMA** ISR transport.

- Build defines: `IOLINK_ISR_RX_MODE=IOLINK_ISR_RX_FRAME`,
  `IOLINK_ISR_TX_MODE=IOLINK_ISR_TX_FRAME`.
- RX: DMA1 channel 1 (USART2_RX, circular) into a frame buffer; the USART
  IDLE interrupt delimits the frame. `rx_frame()` returns the whole frame
  zero-copy.
- TX: DMA1 channel 2 (USART2_TX); the transfer-complete interrupt releases
  `TX_EN` and re-arms RX. `tx_frame()` hands the whole reply at once.
- Echo suppression: RX DMA is disabled while the device transmits (the
  transceiver echoes the local transmission).
- Timebase: derived from SysTick (the STM32G070 has no TIM2).

`phy_l6362a_raw.c` / `.h` are the board adapter (copied from the IO-Link
firmware project). The CubeMX `main.c` / `app_iolink.c` glue is not duplicated
here; see the reference firmware project for the full startup.

## Wiring (as used by the reference board)

| Signal | Pin / peripheral |
| --- | --- |
| EN | PA0 |
| TX (USART2_TX -> C/Q) | PA2 / DMA1_Channel2 |
| RX (USART2_RX <- C/Q) | PA3 / DMA1_Channel1 |
| TX_EN | PA4 |
| OL_INT (overload / wake IRQ) | PA5 |
| RUN / ERR LED | PA7 / PA6 |
