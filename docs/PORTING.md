# Platform Porting Guide

## What a port includes

A port is an `iolink_phy_api_t` plus the application that owns the stack context. The host demo is the build this repository exercises. An IAR EWARM build for STM32G0B1RE plus TI TIOL112 is not verified here. Firmware update and secure boot stay in the device.

## Overview

For the current context-based PHY API and a working transceiver control
implementation, start with [TIOL112 integration](hardware/TIOL112.md) and
[the reference device](../examples/reference_device/README.md). The older snippets
below predate the current callback signatures and are conceptual illustrations;
do not copy them unchanged. In particular, a millisecond-derived microsecond
clock and a one-millisecond polling delay do not establish physical IO-Link timing.

iolinki is designed to be portable across different platforms and RTOSes. This guide explains how to port the stack to your target platform.

## Platform Abstraction Layers

### 0. Context-based API (Architecture)
The stack uses a context-based API to avoid global state and ensure reentrancy.
All public API functions take a context pointer (e.g., `iolink_dll_ctx_t*`).
When porting, ensure your application manages this context storage (stack or static).

### 1. PHY Layer (Required)

The PHY layer provides hardware abstraction for UART communication.

**Location**: `include/iolinki/phy.h`

**Interface**: copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes `void* user` first. The transport hooks depend on the configured granularity: `rx_octet`/`tx_octet` in BYTE mode or `rx_frame`/`tx_frame` in FRAME mode, plus `tx_done` and `int_active`. `detect_wakeup`, `set_cq_line`, `request_service`, `response_timer_start`/`response_timer_stop`, `stack_timer_start`/`stack_timer_stop`, `get_voltage_mv`, `is_short_circuit` and `get_faults` are optional and may be NULL. `init`, `set_mode`, and `set_baudrate` are required. `request_service()` is the key hook for the ISR TX path: without it a main-context reply cannot be handed to the ISR unless the direct-send fallback is compiled in.

**Implementation Steps**:

1. Create `src/platform/<your_platform>/phy_<your_platform>.c`
2. Implement all required PHY functions
3. Export PHY API structure

**Example** (STM32 HAL, BYTE mode). `init`, `set_mode`, and `set_baudrate` are still required and must take `void* user` first. Do not invent transceiver register addresses.

```c
static int stm32_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    UART_HandleTypeDef* uart = user;
    *status = 0U;
    if (HAL_UART_Receive(uart, data, 1, 0) != HAL_OK) {
        return 0;
    }
    return 1;
}

static int stm32_tx_octet(void* user, uint8_t data)
{
    UART_HandleTypeDef* uart = user;
    if (HAL_UART_Transmit(uart, &data, 1, 10) != HAL_OK) {
        return -1;
    }
    return 0;
}
```

### 2. Time Utilities (Required)

Provide timing functions for your platform.

**Location**: `src/platform/<platform>/time_utils.c`

**Interface**:
```c
uint32_t iolink_time_get_ms(void);
uint64_t iolink_time_get_us(void);
```

**Example** (FreeRTOS):
```c
#include "iolinki/time_utils.h"
#include "FreeRTOS.h"
#include "task.h"

uint32_t iolink_time_get_ms(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

uint64_t iolink_time_get_us(void) {
    return iolink_us_from_ms(iolink_time_get_ms());
}
```

**Example** (Bare Metal with SysTick):
```c
static volatile uint32_t g_tick_ms = 0;

void SysTick_Handler(void) {
    g_tick_ms++;
}

uint32_t iolink_time_get_ms(void) {
    return g_tick_ms;
}

uint64_t iolink_time_get_us(void) {
    return iolink_us_from_ms(g_tick_ms);
}
```

### 3. Persistent Storage (Optional)

Parameters, the Data Storage image and the upload flag share one unified NVM
backend, supplied through `iolink_device_config_t::storage`.

**Interface** (`include/iolinki/storage.h`):
```c
typedef struct
{
    void* user;
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
    int (*erase)(void* user, uint32_t offset, size_t len);
} iolink_storage_api_t;
```

The stack passes **relative** offsets inside a fixed region layout; the
application adds its physical base address. Size your NVM area with
`IOLINK_STORAGE_TOTAL_SIZE` and assert it at compile time:

```c
static uint8_t g_nvm[IOLINK_STORAGE_TOTAL_SIZE];
_Static_assert(sizeof(g_nvm) >= IOLINK_STORAGE_TOTAL_SIZE, "NVM region too small");
```

**Example** (EEPROM mapped at `EEPROM_BASE`):
```c
static int eeprom_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    (void) user;
    return (EEPROM_Read(EEPROM_BASE + offset, buf, (uint16_t) len) == 0) ? 0 : -1;
}

static int eeprom_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    (void) user;
    return (EEPROM_Write(EEPROM_BASE + offset, buf, (uint16_t) len) == 0) ? 0 : -1;
}

static int eeprom_erase(void* user, uint32_t offset, size_t len)
{
    (void) user;
    return (EEPROM_Erase(EEPROM_BASE + offset, (uint16_t) len) == 0) ? 0 : -1;
}

static const iolink_storage_api_t g_storage_eeprom = {
    .user = NULL,
    .read = eeprom_read,
    .write = eeprom_write,
    .erase = eeprom_erase,
};
```

## CMake Integration

### Option 1: Add Platform to iolinki CMake

Edit `CMakeLists.txt`:
```cmake
elseif(IOLINK_PLATFORM STREQUAL "STM32")
    target_sources(iolinki PRIVATE
        src/platform/stm32/phy_stm32.c
        src/platform/stm32/time_utils.c
    )
    target_link_libraries(iolinki PRIVATE stm32_hal)
endif()
```

Build (the branch above must be added to `CMakeLists.txt` first; the shipped
CMake only handles `LINUX` and `BAREMETAL`):
```bash
cmake -B build -DIOLINK_PLATFORM=STM32
cmake --build build
```

### Option 2: Use iolinki as Submodule

In your project's `CMakeLists.txt`:
```cmake
add_subdirectory(external/iolinki)

add_executable(my_app
    src/main.c
    src/phy_custom.c
)

target_link_libraries(my_app PRIVATE iolinki)
```

## RTOS Integration

### FreeRTOS

```c
#include "FreeRTOS.h"
#include "task.h"
#include "iolinki/device.h"

static iolink_device_ctx_t g_ctx;
static const iolink_device_config_t g_cfg = {.phy = g_phy_stm32};

void iolink_task(void *pvParameters) {
    if (iolink_device_init(&g_ctx, &g_cfg) != 0) {
        /* handle init error */
    }

    while (1) {
        iolink_device_process(&g_ctx);
        vTaskDelay(pdMS_TO_TICKS(1));  // 1ms cycle
    }
}

int main(void) {
    xTaskCreate(iolink_task, "IOLink", 512, NULL, 2, NULL);
    vTaskStartScheduler();
}
```

### Bare Metal

```c
#include "iolinki/device.h"

static iolink_device_ctx_t g_ctx;
static const iolink_device_config_t g_cfg = {.phy = g_phy_baremetal};

int main(void) {
    // Initialize SysTick for 1ms interrupt
    SysTick_Config(SystemCoreClock / 1000);

    if (iolink_device_init(&g_ctx, &g_cfg) != 0) {
        /* handle init error */
    }

    while (1) {
        iolink_device_process(&g_ctx);
        __WFI();  // Wait for interrupt
    }
}

void SysTick_Handler(void) {
    g_tick_ms++;
    // Could call iolink_device_process(&g_ctx) here instead
}
```

## Memory Requirements

For detailed RAM/ROM calculations and stack depth analysis, please refer to the [Memory Usage Guide](MEMORY_GUIDE.md).

### Quick Summary

- **Minimum RAM**: ~300 bytes (with optimized config)
- **Minimum Flash**: ~5 KB

### Configuration

The stack can be tuned via `include/iolinki/config.h`. Override the defaults by defining these macros in your build system (e.g., `-DIOLINK_ISDU_BUFFER_SIZE=64`):

- `IOLINK_ISDU_BUFFER_SIZE`: Size of ISDU transfer buffers.
- `IOLINK_EVENT_QUEUE_SIZE`: Number of events to queue.
- `IOLINK_PD_IN_MAX_SIZE`: Max process data input size.

## Hardware Requirements

### Minimum MCU Specs

- **CPU**: 16 MHz+ ARM Cortex-M0 or equivalent
- **RAM**: 4 KB minimum
- **Flash**: 16 KB minimum
- **Peripherals**: 1x UART with configurable baudrate

### Recommended MCU Families

- **STM32**: F0, F1, F4, L4, G0 series
- **NXP**: LPC, Kinetis, i.MX RT series
- **Nordic**: nRF52, nRF53 series
- **Microchip**: SAM, PIC32 series

## Debugging

### Enable Debug Logging

The core stack does not log. The host `phy_virtual` example uses `printf()`;
replace it with platform-specific logging in a real port:

```c
// In phy_stm32.c
#ifdef IOLINK_DEBUG
#define IOLINK_LOG(...) printf(__VA_ARGS__)
#else
#define IOLINK_LOG(...)
#endif
```

### Common Issues

1. **No communication**: Check UART baudrate and pin configuration
2. **CRC errors**: Verify byte order and timing
3. **Stack overflow**: Increase task stack size
4. **Timing issues**: Ensure `iolink_device_process(ctx)` is called every 1ms

## Testing on New Platform

1. **Build test**: Verify compilation
2. **PHY test**: Test UART loopback
3. **CRC test**: Run CRC unit tests
4. **Integration test**: Use Virtual Master
5. **Hardware test**: Connect to real IO-Link Master

## Example Ports

See `src/platform/` for reference implementations:
- `linux/` - POSIX-based (development)
- `baremetal/` - No-OS example

## Contributing Ports

If you port iolinki to a new platform, please contribute back:

1. Create `src/platform/<platform>/`
2. Implement PHY and time utilities
3. Add CMake support
4. Document hardware requirements
5. Submit pull request

## Support

For porting assistance, open an issue on GitHub with:
- Target platform/MCU
- RTOS (if any)
- Compiler toolchain
- Error messages or build logs
