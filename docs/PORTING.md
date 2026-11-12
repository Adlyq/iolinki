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

**Interface**: copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes `void* user` first. The transport hooks depend on the configured granularity: `rx_octet`/`tx_octet` in BYTE mode or `rx_frame`/`tx_frame` in FRAME mode, plus `tx_done` (TX BYTE only), `tx_busy` (TX FRAME, optional) and `int_active`. `detect_wakeup`, `set_cq_line`, `request_service`, `response_timer_start`/`response_timer_stop`, `stack_timer_start`/`stack_timer_stop` and `get_faults` are optional and may be NULL. `init`, `set_mode`, and `set_baudrate` are required. `request_service()` is the key hook for the ISR TX path: without it a main-context reply cannot be handed to the ISR unless the direct-send fallback is compiled in.

In RX FRAME mode, `rx_frame()` must return only a complete PHY-delimited
M-sequence. A two-octet header or another truncated prefix is not a frame and
must remain in the PHY until the configured end-of-frame condition is known.
The stack validates the M-sequence checksum and type after receiving the frame;
the PHY is responsible for frame completeness and must not use a partial frame
as a substitute for checksum validation.

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

### 2. Time Source

Supply a monotonic microsecond clock through
`iolink_device_config_t::time_us`. `iolink_device_init()` rejects NULL with
`IOLINK_DEVICE_INIT_ERR_TIME`. The DLL, ISR pump and event engine all use this
callback; because the data ISR calls it, it must be non-blocking and ISR-safe.

**Example** (FreeRTOS):
```c
static uint64_t app_time_us(void* user)
{
    (void) user;
    return (uint64_t) xTaskGetTickCount() * (1000000ULL / configTICK_RATE_HZ);
}
```

A 32-bit tick counter wraps, so this example is only monotonic if the port
extends it to 64 bits (e.g. by counting the wrap in the tick hook). A tick
rate that does not divide 1000000 also truncates the conversion; pick a rate
that divides it (100/1000/10000 Hz) or compute in nanoseconds.

**Example** (Bare Metal with SysTick):
```c
static volatile uint32_t g_tick_ms;

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static uint64_t app_time_us(void* user)
{
    (void) user;
    return (uint64_t) g_tick_ms * 1000ULL; /* replace with a hardware timer */
}
```

### 2.1 Concurrency primitives (graded)

The stack needs three kinds of asynchronous primitives. Each is **graded**, and
the selected concurrency profile decides which grade a port must provide. A
primitive the profile does not need is not referenced at all, so a port never
implements more than it uses. The contract lives in `include/iolinki/port.h`.

| Primitive | Grades | What the stack assumes |
| :--- | :--- | :--- |
| Atomic width `IOLINK_ATOMIC_UINT_T` (default `uint32_t`) | 8/16/32/64 bit | a naturally aligned value is read/written in one access (single-copy atomic); **no read-modify-write is assumed** |
| Barrier `IOLINK_FENCE()` | `COMPILER` / `DEVICE` / `SMP` | data written before a publish-word store is visible before it; a publish-word load is ordered before the data it publishes |
| Mutual exclusion `IOLINK_CRITICAL_ENTER/EXIT()` | none / IRQ mask / spinlock | mutual exclusion against the data interrupt; **nesting-safe**; **callable from the ISR**; bounded duration |

The stack has **one main-context caller**: there is no thread lock in the
contract. A port whose critical section also serialises tasks (an RTOS
scheduler lock) gains task exclusion for the short windows, but that does not
make concurrent API calls supported.

#### Profiles

Select one with `-DIOLINK_CONCURRENCY=...`:

> Define the profile for the **whole build** (the library archive and the
> application), for example through `CMAKE_C_FLAGS` or
> `target_compile_definitions(iolinki PUBLIC ...)`. A profile applied to the
> application only would make its headers disagree with the library's internal
> use of the primitives.

| Profile | Use case | Required by the stack |
| :--- | :--- | :--- |
| `IOLINK_CONCURRENCY_SINGLE` | host simulation, cooperative loop, one context | nothing (the barrier and the critical section collapse to no-ops) |
| `IOLINK_CONCURRENCY_ISR` (default) | one data ISR preempts one main loop (the dual-pump transport) | atomic width ≥ 32 bit + `COMPILER` barrier + the critical section |
| `IOLINK_CONCURRENCY_SMP` | the pump and the main loop may run on different cores | atomic width + `SMP` barrier + the critical section |

The RTOS tier was removed: an RTOS build uses `ISR` (or `SMP` on multiple
cores) and must still keep exactly one task calling the API. The stack derives
`IOLINK_NEED_FENCE` and `IOLINK_NEED_CRITICAL` from the profile. A missing or
invalid combination is a compile error with an actionable message, for example:

```
port.h:160:2: error: #error "IOLINK_CONCURRENCY=SMP requires IOLINK_FENCE_LEVEL=IOLINK_FENCE_SMP"
```

#### Barrier grades

`IOLINK_FENCE_LEVEL` is declared by the port (it describes the hardware, so it
cannot be inferred):

| Level | When | Port must provide |
| :--- | :--- | :--- |
| `IOLINK_FENCE_COMPILER` (default) | one core, no other bus master reads the published buffers | nothing |
| `IOLINK_FENCE_DEVICE` | a DMA/PHY reads a published buffer while the CPU writes it | `IOLINK_FENCE_DEVICE_IMPL()` (e.g. `__DMB`) |
| `IOLINK_FENCE_SMP` | the two contexts are two cores or two threads | `IOLINK_FENCE_SMP_IMPL()` (e.g. `__atomic_thread_fence(__ATOMIC_SEQ_CST)`) |

The publish-word helpers (`iolink_atomic_load()` / `iolink_atomic_store()`)
are a volatile access plus that barrier, so a port that needs real
acquire/release instructions selects the `SMP` grade (or a device barrier) and
supplies the matching `*_IMPL()` macro.

#### Critical-section contract

`iolink_critical_enter()` / `iolink_critical_exit()` must:

1. exclude the data interrupt (an IRQ mask, an RTOS IRQ lock or a spinlock).
   An RTOS scheduler lock only excludes interrupts at or below the RTOS
   syscall priority (FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`): the data
   interrupt must be configured at that level, or the port must mask it
   directly, otherwise the section does not exclude the pump;
2. be **nesting-safe**: the stack may enter while already inside one (a DLL
   state transition that resets the event queue), and a DLL state transition
   can itself be taken from the data ISR;
3. be **callable from interrupt context**;
4. stay short (a few instructions): the data interrupt must answer within t_A,
   which is ~43 µs at COM3.

The weak no-op defaults in `src/platform.c` keep the library linkable;
`iolink_device_init()` probes them and returns
`IOLINK_DEVICE_INIT_ERR_CRITICAL` when the profile needs a real implementation
but none was supplied. Profiles that do not need it (`SINGLE`) skip the probe
and never reference the symbols, so a port may omit them entirely. If your
toolchain cannot override weak symbols, exclude `src/platform.c` and provide
`iolink_platform_critical_sections_ready()` returning 0 (only valid when
`IOLINK_NEED_CRITICAL` is 0).

**Example (Cortex-M0+, no BASEPRI).** PRIMASK save/restore with a nesting depth,
as in `examples/stm32g0_l6362a_raw/phy_l6362a_raw.c`:

```c
void iolink_critical_enter(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (s_crit_depth == 0U) {
        s_crit_primask = mask;
    }
    s_crit_depth++;
}

void iolink_critical_exit(void)
{
    if (s_crit_depth != 0U) {
        s_crit_depth--;
        if (s_crit_depth == 0U) {
            __set_PRIMASK(s_crit_primask);
        }
    }
}
```

**Example (FreeRTOS).** The critical section is entered from both contexts, so
the task and ISR variants must be selected at run time:

```c
void iolink_critical_enter(void)
{
    if (xPortIsInsideInterrupt()) {
        s_isr_mask = taskENTER_CRITICAL_FROM_ISR();
    }
    else {
        taskENTER_CRITICAL();
    }
}
```

Never use the task-only `taskENTER_CRITICAL()` in the ISR path. On RT-Thread,
`rt_enter_critical()` (the scheduler lock) does **not** exclude interrupts and
is therefore not a valid implementation; use `rt_hw_interrupt_disable()`.
On POSIX, a signal handler must not take a mutex, which is one reason the
`SINGLE` profile (no critical section) is the right choice for a hosted port
that drives the pump from a signal handler.

#### Verification

- `tests/test_port_capabilities.c` compiles and runs under every profile.
- `tools/build_matrix.sh` builds the `SINGLE` and `SMP` profiles and checks
  that the `SINGLE` library archive has **no undefined
  `iolink_critical_*` symbols**.
- `./check_quality.sh` and the matrix are the delivery gate for any change that
  touches these switches.

### 2.2 Statistics counters

`IOLINK_STATS_MODE` selects how the DLL counters are shared between the two
contexts:

| Mode | Behaviour | Cost |
| :--- | :--- | :--- |
| `SHARED` (default) | one counter set, both contexts increment it; an increment that is preempted between its load and its store can be lost (diagnostics only) | none |
| `SPLIT` | the ISR keeps its own copy (`ctx->isr_stat`), `iolink_dll_get_stats()` returns the sum; no update is lost | a few bytes of RAM |

Neither mode takes a critical section for the counters. `SPLIT` is verified by
`tests/test_stats_split.c` and by the `stats_split` matrix configuration, which
runs the whole suite with the split counters.

### 2.3 Measuring the interrupt-off window

The critical-section contract is a bound, so measure it on the target instead
of reading the code:

- toggle a spare GPIO inside `iolink_critical_enter()`/`iolink_critical_exit()`
  and capture the widest pulse with a scope or logic analyser;
- or read `DWT->CYCCNT` on entry and keep the maximum delta in a variable the
  application prints;
- or use the trace unit (ETM/ITM) when the board has one.

Budget: the data interrupt must start the reply within t_A, about 43 us at
COM3 (one bit ~ 4.3 us). Keep the worst case well below that, and re-measure
after adding a call inside the section.

### 3. Persistent Storage (Required with Data Storage)

`iolink_device_config_t::storage` is declared and required when
`IOLINK_FEATURE_DATA_STORAGE` is 1; `iolink_device_init()` rejects a NULL or
incomplete backend with `IOLINK_DEVICE_INIT_ERR_STORAGE`. Offsets are relative to
the stack's fixed region layout, so add the physical base address inside the
callbacks.

The backend interface, region layout, sizing (`IOLINK_STORAGE_TOTAL_SIZE`) and a
complete EEPROM example are in [STORAGE.md](STORAGE.md).

## CMake Integration

### Option 1: Add Platform to iolinki CMake

Edit `CMakeLists.txt`:
```cmake
elseif(IOLINK_PLATFORM STREQUAL "STM32")
    target_sources(iolinki PRIVATE
        src/platform/stm32/phy_stm32.c
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

/* One task only: the stack has a single main-context caller. */
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

See the [Memory Usage Guide](MEMORY_GUIDE.md) for RAM/ROM sizing, the per-buffer
costs, compiler-flag advice and the context-size measurement snippet.

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

### Logging

The stack ships print points with a runtime sink: set
`iolink_device_config_t::log_write` and `log_level` (the `user` argument is
`config.user`), or call `iolink_device_set_log()` later. A zeroed sink is
silent. The library does not format; the callback owns `vsnprintf` and the
transport. ISR print points additionally require `IOLINK_LOG_ENABLE_ISR=1`.
See `include/iolinki/log.h` and `docs/API.md`.

The host `phy_virtual` example still uses `printf()` directly; a real port
routes the sink to its UART/RTOS logger instead:

```c
#include <stdarg.h>
#include <stdio.h>

static void port_log(void* user, iolink_log_level_t level, const char* format, ...)
{
    (void) user;
    (void) level;
    va_list args;
    va_start(args, format);
    (void) vprintf(format, args);
    va_end(args);
}

config.log_write = port_log;
config.log_level = IOLINK_LOG_LEVEL_INFO; /* SILENT disables */
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
