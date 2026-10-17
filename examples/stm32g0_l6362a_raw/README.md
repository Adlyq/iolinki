# STM32G0 + L6362A raw-octet PHY (reference)

Reference raw-octet PHY adapter for the ST L6362A transceiver on an STM32G0.
It is copied from the 8110 8-channel board project and is **not built by the
default CMake**; use it as a starting point and adapt the pins, USART/DMA
instances and IRQ names to your board.

The L6362A is a byte-stream UART transceiver with no SPI status register, so this
adapter synthesizes the ISR-path contract:

| Library mode | L6362A implementation |
| --- | --- |
| `IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME` | USART RX DMA + IDLE line, whole-frame buffer (`rx_frame`) |
| `IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME` | USART TX DMA (`tx_frame`), transfer-complete releases TX_EN |
| `rx_octet` / `tx_octet` / `tx_done` | byte-ring fallback (`RX BYTE` / `TX BYTE`) |
| `int_active` | frame-end pending, RX octet waiting, or TX ready |
| `request_service` | `NVIC_SetPendingIRQ(IOD_UART_IRQn)` so the ISR pump transmits a staged main-context reply |

The library owns M-sequence framing, checksums, reply selection and the ISR
pump; this file contains no frame/checksum knowledge.

## Wiring (from the source header)

| MCU pin | Signal |
| --- | --- |
| PA0 | EN (transceiver enable, held high) |
| PA2 | USART2_TX -> L6362A C/Q input |
| PA3 | USART2_RX <- L6362A C/Q output |
| PA4 | TX_EN (asserted only while sending) |
| PA5 | OL_INT (overload / wake, falling edge) |

These are MCU pins, not verified header positions. Check the package, schematic
and shared pin functions before use. Connect the 24 V field side only to the
transceiver; never to MCU UART/GPIO pins.

## Required configuration

`config.h` (or the build) must select the two-pump transport and the
transmit-only policy:

```c
#define IOLINK_PHY_FEATURE_ISR_PUMP 1
#define IOLINK_PHY_FEATURE_ISR_TX_ONLY 1
#define IOLINK_ISR_RX_MODE IOLINK_ISR_RX_FRAME
#define IOLINK_ISR_TX_MODE IOLINK_ISR_TX_FRAME
```

The source asserts this contract with `_Static_assert`, so a build with the
wrong transport fails at compile time. When `IOLINK_PHY_FEATURE_ISR_TX_ONLY` is
set, the application must provide `request_service`; check it at init and fail
early (the 8110 application calls `Error_Handler()` if it is `NULL`).

## CubeMX / board dependencies

The file expects the generated `main.h` to provide:

- pin macros `EN_Pin/EN_GPIO_Port`, `TX_EN_Pin/TX_EN_GPIO_Port`,
  `OL_INT_Pin/OL_INT_GPIO_Port`, `UART_TX_*`;
- `IOD_UART`, `IOD_UART_IRQn`, `HAL_*`, `DMA1_Channel1/2`, `TIM3`;
- `Error_Handler()` (used by the TX ISR-context guard).

Wire the interrupt feeds from the CubeMX handlers:

```c
void USART2_IRQHandler(void) { phy_l6362a_raw_rx_irq(); }
void TIM3_IRQHandler(void)   { phy_l6362a_raw_gap_irq(); }
void DMA1_Channel2_IRQHandler(void) { phy_l6362a_raw_tx_dma_irq(); }
void EXTI4_15_IRQHandler(void)      { phy_l6362a_raw_ol_irq(); }
```

The application main loop only calls `iolink_device_process()`; the PHY IRQ
handlers call `iolink_device_process_isr()`. `L6362_TX_ASSERT_ISR()` (checks
`SCB->ICSR & SCB_ICSR_VECTACTIVE_Msk`) catches a regression that drives the TX
hooks from the main loop.

## Integration

1. Copy `phy_l6362a_raw.c` / `phy_l6362a_raw.h` into your board project.
2. Call `phy_l6362a_raw_hw_init()` before `iolink_device_init()`.
3. Set `config.phy = *phy_l6362a_raw_get()` and verify
   `config.phy.request_service != NULL` when `ISR_TX_ONLY` is enabled.
4. Route the four interrupt feeds above from your IRQ handlers.
5. Keep `iolink_device_process()` in the main loop and
   `iolink_device_process_isr()` in the USART/TIM/DMA/EXTI handlers.
