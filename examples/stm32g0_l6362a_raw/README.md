# STM32G0 + L6362A raw-octet PHY (reference)

Reference raw-octet PHY adapter for the ST L6362A transceiver on an STM32G0.
It is copied from the 8110 8-channel board project and is **not built as
firmware by the default CMake**; use it as a starting point and adapt the pins,
USART/DMA instances and IRQ names to your board. The source itself is compiled
on the host by `tests/test_phy_l6362a_raw.c` through the CubeMX/HAL mock under
`tests/mocks/stm32g0_l6362a/`.

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

### Verified 8110 configuration

The 8-channel 8110 board is a Type 2_V device with 28-byte PD in/out and a
one-octet OD. Its trimmed configuration is:

```c
/* Capacity: M_SEQ_MAX_LEN = 28 + 1 + 2 = 31 */
#define IOLINK_PD_IN_MAX_SIZE  28
#define IOLINK_PD_OUT_MAX_SIZE 28
#define IOLINK_OD_MAX_SIZE     1
#define IOLINK_ISDU_BUFFER_SIZE 64   /* strings are 64 octets; no ProductURI/DS */

/* Feature profile: no Data Storage, no SystemCommand, no events/diagnostics */
#define IOLINK_MINIMAL_PROFILE 1
#define IOLINK_FEATURE_EVENTS 0
#define IOLINK_PHY_FEATURE_WAKEUP 1
#define IOLINK_PHY_FEATURE_LEGACY_DIAG 0
#define IOLINK_PHY_FEATURE_ISR_PUMP 1
#define IOLINK_PHY_FEATURE_ISR_TX_ONLY 1
#define IOLINK_ISR_RX_MODE IOLINK_ISR_RX_FRAME
#define IOLINK_ISR_TX_MODE IOLINK_ISR_TX_FRAME
```

The raw ISDU transport buffers carry fixed headroom (`req_buf +6`,
`resp_buf +3`, ISR snapshot `+3`), so `IOLINK_ISDU_BUFFER_SIZE = 64` still
serves a 64-octet identification string (67-octet framed response).

Measured with the board CMake build (Debug, 31 KiB flash / 8 KiB RAM target):

| Step | text | bss |
| --- | ---: | ---: |
| Synced upstream baseline | 31348 | 8984 |
| + compile-time feature removal (events/diag off) | 29948 | 8936 |
| + 28/1/64 sizing and RX-FRAME buffer guards | 29652 | **7568** |

Hardware check: after flashing, RTT reports `[STATE] 4` (OPERATE) and 28-byte
`[PDO]` traffic.

Disabled features fail the build: their public declarations are compiled out, so
calling e.g. `iolink_ds_init`, `iolink_events_flag` or `iolink_params_get`
produces an implicit-declaration error (or an undefined reference if the
compiler only warns). Wire-visible objects that cannot be removed at compile
time answer a negative response instead: `0x0003` (Data Storage) and `0x0025`
(DetailedDeviceStatus) return `0x80 0x11` when disabled.

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
