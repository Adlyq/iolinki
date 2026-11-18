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
All public API functions take a context pointer (e.g., `iolink_dll_main_t*`
for DLL calls or `iolink_device_ctx_t*` at the device layer).
When porting, ensure your application manages this context storage (stack or static).

### 1. PHY Layer (Required)

The PHY layer provides hardware abstraction for UART communication.

**Location**: `include/iolinki/phy.h`

**Interface**: copy `iolink_phy_api_t` from `include/iolinki/phy.h`. Every callback takes `iolink_app_t* self` first (the application instance; the library forwards it unchanged and it may be NULL for a hook that does not use it). Every member is optional: `iolink_caps_resolve()` derives the capability bits from the hooks that are present, and the library calls a hook only when the matching bit is set. In practice a usable PHY provides the transport hooks of the configured granularity (`rx_octet`/`tx_octet` in BYTE mode, `rx_frame`/`tx_frame` in FRAME mode), `int_active` when the ISR pump is used (`dll_isr_process()` returns immediately without it), and `init`/`set_mode` when the hardware needs them — each is called only if present. `tx_done` (TX BYTE), `tx_busy` (TX FRAME), `detect_wakeup`, `set_cq_line`, the timer hooks and `get_faults` stay optional.

`tx_busy()` (TX FRAME) is the only way the stack learns that a frame has left the line. The data ISR calls it on entry: a zero releases the PD-in response buffer lease of the previous reply, a non-zero defers an eager reply to the main-loop pump. It must therefore be read-only, non-blocking and callable from the data ISR. The main pump never polls it. Without the hook the end of a TX FRAME transfer is not observable, the captured buffer stays leased until the next capture, and `IOLINK_PD_IN_BUFFERS` becomes three so the main pump still has a slot to fill; set `IOLINK_PHY_FEATURE_TX_BUSY=1` for a FRAME PHY that provides it, and `iolink_device_init()` then rejects a PHY without the hook.

`request_service()` is required for every PHY that has TX capability in the `ISR` and `SMP` profiles: the main context only stages the reply and this hook must raise the data interrupt that sends it. `iolink_device_init()` returns `IOLINK_DEVICE_INIT_ERR_PHY` for a TX-capable PHY without it (a `REQUIRE_ISR_TX` port has the same requirement). Only the `SINGLE` profile has no data ISR: it drives the TX hooks from the main context when the hook is absent. There the hook stays optional (it is called when present) and becomes mandatory only when the port sets `IOLINK_STACK_FLAG_REQUIRE_ISR_TX`, so something still pumps the staged reply.

The transmission rate is the build-time `IOLINK_COM` (1/2/3, `include/iolinki/config.h`), not a
stack-config field: an IO-Link Device supports exactly one COM. The PHY's `init()` must
configure the UART from `IOLINK_COM_HZ` in `init()` or when switching to SDCI
in `set_mode()` (the built-in drivers use the latter); there is no runtime
rate-change callback.

In RX FRAME mode, `rx_frame()` must return only a complete PHY-delimited
M-sequence. A two-octet header or another truncated prefix is not a frame and
must remain in the PHY until the configured end-of-frame condition is known.
The stack validates the M-sequence checksum and type after receiving the frame;
the PHY is responsible for frame completeness and must not use a partial frame
as a substitute for checksum validation.

**Implementation Steps**:

1. Create `src/platform/<your_platform>/phy_<your_platform>.c`
2. Implement the hooks your transport and hardware need (every member of
   `iolink_phy_api_t` is optional; see 2.1 for what a usable PHY provides)
3. Export PHY API structure

**Example** (STM32 HAL, BYTE mode). Every callback takes `iolink_app_t* self` first. Do not invent transceiver register addresses.

```c
static int stm32_rx_octet(iolink_app_t* self, uint8_t* data, uint8_t* status)
{
    struct my_app* app = (struct my_app*) self;
    UART_HandleTypeDef* uart = app->uart;
    *status = 0U;
    if (HAL_UART_Receive(uart, data, 1, 0) != HAL_OK) {
        return 0;
    }
    return 1;
}

static int stm32_tx_octet(iolink_app_t* self, uint8_t data)
{
    struct my_app* app = (struct my_app*) self;
    UART_HandleTypeDef* uart = app->uart;
    if (HAL_UART_Transmit(uart, &data, 1, 10) != HAL_OK) {
        return -1;
    }
    return 0;
}
```

### 2. Time Source

Supply a monotonic microsecond clock through
`iolink_device_app_t::time_us` (inside the `config.app` trait). `iolink_device_init()`
rejects a missing trait with
`IOLINK_DEVICE_INIT_ERR_APP` (no trait) or `IOLINK_DEVICE_INIT_ERR_TIME` (no
clock). The DLL, ISR pump and event engine all use this
callback; because the data ISR calls it, it must be non-blocking and ISR-safe.

**Example** (FreeRTOS):
```c
static uint64_t app_time_us(iolink_app_t* self)
{
    (void) self;
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

static uint64_t app_time_us(iolink_app_t* self)
{
    (void) self;
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
| `IOLINK_CONCURRENCY_SINGLE` | host simulation, cooperative loop, one context | nothing (the barrier and the critical section collapse to no-ops); replies are driven from the main context |
| `IOLINK_CONCURRENCY_ISR` (default) | one data ISR preempts one main loop (the dual-pump transport) | atomic width ≥ 32 bit + `COMPILER` barrier + the critical section; `request_service()` for a TX-capable PHY |
| `IOLINK_CONCURRENCY_SMP` | the pump and the main loop may run on different cores | atomic width + `SMP` barrier + the critical section; `request_service()` for a TX-capable PHY |

`request_service()` is what makes the dual pump reachable from the main context:
without it a staged reply is never sent in `ISR`/`SMP`, so `iolink_device_init()`
rejects such a PHY instead of degrading silently.

The RTOS tier was removed: an RTOS build uses `ISR` (or `SMP` on multiple
cores) and must still keep exactly one task calling the API. The stack derives
`IOLINK_NEED_FENCE` and `IOLINK_NEED_CRITICAL` from the profile. An inconsistent
profile/fence pair is a compile error with an actionable message, for example:

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

#### PD-in publish contract

Process input reaches the data ISR through one publish word per update. The main
context fills a buffer that is neither the published one nor the one the ISR has
captured, then writes `shared.m_pd_pub` (`{active, valid, gen}`) with
`IOLINK_FENCE()` plus a single atomic store. The data ISR loads that word once
per reply and uses the same generation for the buffer it captures and for the
CKS PD-valid bit, so the hand-off needs no critical section and a port provides
no primitive for it beyond the atomic width and barrier grades above. Two
consequences: the word must be naturally aligned (the `IOLINK_ATOMIC_UINT_T`
contract), and at the `DEVICE`/`SMP` grades the barrier must really order the
buffer writes before the store, because the PHY/DMA streams the PD bytes from
that buffer while the main context prepares the next one.

`0x98`/`0x99` (PROCESS_DATA_OPERATE / DEVICE_OPERATE) override the valid bit
from the data ISR through a second single-writer word, `shared.b_pd_valid_cmd`,
tagged with the `m_pd_pub` generation it applies to. The override therefore lives
exactly one generation: the next application update restores the value the
application published, and the ISR drops the stale command word on its next
entry. Ports do not need to do anything for it; it is one more 32-bit word in
the shared region.

#### Critical-section contract

`iolink_critical_enter()` / `iolink_critical_exit()` must:

1. exclude the data interrupt (an IRQ mask, an RTOS IRQ lock or a spinlock).
   An RTOS scheduler lock only excludes interrupts at or below the RTOS
   syscall priority (FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY`): the data
   interrupt must be configured at that level, or the port must mask it
   directly, otherwise the section does not exclude the pump;
2. be **nesting-safe**: `exit` restores what the matching `enter` captured, so
   a section may be taken while interrupts are already masked. The library's
   own call sites are flat — no section is entered from inside another;
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

#### Interrupt priority: data interrupt vs Timer A/B

`iolink_dll_process_isr()` (the data interrupt) owns the RX assembly state
(`isr_rx_buf`, `isr_rx_cnt`, `isr_rx_acc`, `isr_rx_expect`); Timer A
(`iolink_dll_on_rx_timeout()`) and Timer B (`iolink_dll_on_stack_tick()`) are
separate entry points into the same DLL context. The timeout handler resets
`isr_rx_cnt`/`isr_rx_expect` inside one short critical section, but that only
makes the pair atomic against a pump that *it* preempts: a timeout vector at a
higher priority than the data interrupt can still interrupt the pump between two
received octets and resynchronise the stream on a half frame, which the library
cannot detect.

Therefore: **configure the data interrupt and Timer A/B at the same priority, or
route them through the same vector** (one handler that dispatches on the flag).
If the MCU cannot do either, the port must serialise the entries itself: for
example mask Timer A while the pump runs and apply the pending timeout from the
data-interrupt handler after `iolink_dll_process_isr()` returns, or move the
timeout service into the same handler. Never let Timer A preempt the data pump
in the middle of an M-sequence.

#### Verification

- `tests/test_port_capabilities.c` runs in the `ISR` (`host_default`) and `SMP`
  (`host_smp_split`) rows of the matrix. `SINGLE` is built as a library row and
  as a compile-only probe (`target_minimal`, `single_no_critical`), and no test
  binary runs there.
- `tools/build_matrix.sh` builds the `SINGLE` and `SMP` profiles and checks
  that the `SINGLE` library archive has **no undefined
  `iolink_critical_*` symbols**.
- `./tools/gates.sh build test` and `./tools/gates.sh matrix` are the delivery
  gate for any change that touches these switches.

### 2.2 Statistics counters

`IOLINK_STATS_MODE` selects how the DLL counters are shared between the two
contexts:

| Mode | Behaviour | Cost |
| :--- | :--- | :--- |
| `SHARED` (default) | one counter set, both contexts increment it; an increment that is preempted between its load and its store can be lost (diagnostics only) | none |
| `SPLIT` | the ISR keeps its own counter set (`shared.b_isr_stat`), `iolink_dll_get_stats()` returns the sum | one counter set of RAM |

Neither mode takes a critical section for the counters. `SPLIT` removes the
two-writer increment on a single counter by giving each context its own set, so
a counter update is no longer lost to a preempted load/store pair. It is **not**
a general concurrency fix: `iolink_dll_get_stats()` still reads the other
context's set without a lock, so a snapshot can mix values from different
moments, and every other shared field (state, activity timestamps, publish
words, staging slots) keeps the single-writer and barrier rules described above.
`SPLIT` is verified by `tests/test_stats_split.c` and by the `host_smp_split`
matrix configuration, which runs the whole suite with split counters and SMP
barriers.

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

### 3. 参数持久化（应用负责）

库不提供 NVM 后端，也不规定记录格式或区域布局：`iolink_device_config_t` 中没有
存储回调。参数通过 OD 表项直接绑定应用变量，库在每次写入生效后调用表项的
`on_change`，应用据此决定是否以及何时写自己的 NVM（立即、合并或延迟）。上电时
应用先把 NVM 中的值填回这些变量（包括标签缓冲），再调用 `iolink_device_init()`；
库不读取 NVM。

DS 是 `IOLINK_OD_STORAGE_DS` 表项的投影：上传内容由当前值实时序列化，不占用
NVM。`DS_UPLOAD_FLAG` 的掉电保存由 `iolink_device_config_t::ds_flag_persist`
交给应用，传 NULL 表示该标志只存 RAM。三个标签 0x0018-0x001A 也由应用以
`IOLINK_OD_BYTES_STORAGE[_EX]` 表项提供。

`on_change` 的返回值、标量回滚规则、DS 下载的两阶段语义和 flag 钩子的触发时机
见 [STORAGE.md](STORAGE.md)。

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

target_link_libraries(my_app PRIVATE iolinki::iolinki)
```

### Option 3: Native IDE (IAR, Keil, vendor IDE)

The stack is C99 and can be added as sources to a native IDE project; there is
no verified EWARM board project. Use `include/` as the include path and the core
source list from `CMakeLists.txt`, replacing the Linux time/platform sources with
your own. For a physical target exclude `phy_virtual.c` and the Linux sources;
if you supply strong platform hooks, exclude `platform.c` too and provide
`iolink_platform_critical_sections_ready()` returning 0. Provide the time source
through `config.app->time_us`, the critical-section symbols declared in
`include/iolinki/port.h`, and the parameter persistence hooks (`on_change` on the
OD entries and `iolink_device_config_t::ds_flag_persist`) in the application. Keep
every library and application unit on the same configuration defines and make the
startup/linker files match the actual MCU; a successful GCC build is not evidence
of IAR compatibility.

## RTOS Integration

### FreeRTOS

```c
#include "FreeRTOS.h"
#include "task.h"
#include "iolinki/device.h"

static iolink_device_ctx_t g_ctx;
static const iolink_stack_config_t g_stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                                               .od_width = 2U};
static const iolink_device_info_t g_identity = {.vendor_id = 1234U, .device_id = 5678U,
    .revision_id = IOLINK_REVISION_ID_1_1}; /* 必填，见 API_INTEGRATION */
/* od_width 必须显式给出 1/2/8/32；0 会被初始化拒绝。 */
static const iolink_device_config_t g_cfg = {
    .phy = &g_phy_stm32, .stack = &g_stack, .app = &g_app, .device_info = &g_identity};

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
static const iolink_stack_config_t g_stack = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
                                               .od_width = 2U};
static const iolink_device_info_t g_identity = {.vendor_id = 1234U, .device_id = 5678U,
    .revision_id = IOLINK_REVISION_ID_1_1}; /* 必填，见 API_INTEGRATION */
static const iolink_device_config_t g_cfg = {
    .phy = &g_phy_baremetal, .stack = &g_stack, .app = &g_app, .device_info = &g_identity};

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

The ROM/RAM baseline in `tools/size_baseline.atfe.csv` is measured with the Arm
Toolchain for Embedded (ATFE: clang + llvm, `--target=arm-none-eabi`,
`-mcpu=cortex-m0plus -mthumb -Os`, `-nostdlib`) because that is the compiler the
reference firmware is built with. `IOLINKI_SIZE_TOOLCHAIN=gcc` reproduces the
GNU-toolchain numbers from `tools/size_baseline.csv` instead. Numbers from the
two are not comparable: the gate only compares a measurement against the
baseline of its own toolchain.

The baseline is a **ROM budget, not a diagnostics budget**: the matrix is built
with `-DIOLINK_LOG_COMPILE_LEVEL=0`, so log messages and their format strings are
compiled out. A device that keeps logging pays for the strings by choice — see
`IOLINK_LOG_COMPILE_LEVEL` in `iolinki/log.h` for the compile-time level and
`iolink_log_sink_t` for the runtime one.

## Hardware Requirements

### Minimum MCU Specs

There is no single RAM/Flash minimum: the numbers depend on the feature set and
on where the transport scratch lives. Use the measured tables instead of a
headline figure.

- **CPU**: 16 MHz+ ARM Cortex-M0 or equivalent
- **RAM**: one `iolink_device_ctx_t` (5120 bytes on a 64-bit host with the
  default buffers) plus the application's own buffers and task stack; the
  external-workspace build moves the 1551-byte scratch out of the context. See
  [MEMORY_GUIDE.md](MEMORY_GUIDE.md).
- **Flash**: the full stack and the trimmed minimal build are measured per
  configuration in `tools/size_baseline.atfe.csv` (and
  `tools/size_baseline.csv` for the GNU toolchain); read the row for your
  configuration and leave headroom. Both baselines are built with
  `-DIOLINK_LOG_COMPILE_LEVEL=0`, so logging strings are extra.
- **Peripherals**: 1x UART fixed to the build-time `IOLINK_COM` rate, plus the
  data-interrupt source when the ISR pump is used

### Recommended MCU Families

- **STM32**: F0, F1, F4, L4, G0 series
- **NXP**: LPC, Kinetis, i.MX RT series
- **Nordic**: nRF52, nRF53 series
- **Microchip**: SAM, PIC32 series

## Debugging

### Logging

The stack ships print points with a runtime sink: set
`iolink_device_app_t::log_write` (its `self` argument is `app->self`) and
`config.log_level` at initialisation. A zeroed sink is silent, and the sink is
fixed once `iolink_device_init()` returns — a later `iolink_device_set_log()`
call is a no-op because the data ISR reads the same sink fields. The library does
not format; the callback owns `vsnprintf` and the transport. ISR print points
additionally require `IOLINK_LOG_ENABLE_ISR=1`. See `include/iolinki/log.h` and
`docs/API.md`.

The host `phy_virtual` example still uses `printf()` directly; a real port
routes the sink to its UART/RTOS logger instead:

```c
#include <stdarg.h>
#include <stdio.h>

struct iolink_app { int unused; };
static struct iolink_app g_app_state;

static void port_log(iolink_app_t* self, iolink_log_level_t level, const char* format, ...)
{
    (void) self;
    (void) level;
    va_list args;
    va_start(args, format);
    (void) vprintf(format, args);
    va_end(args);
}

static const iolink_device_app_t app = {
    .self = (iolink_app_t*) &g_app_state,
    .time_us = app_time_us,
    .log_write = port_log,
};
config.app = &app;
config.log_level = IOLINK_LOG_LEVEL_INFO; /* SILENT disables */
```

### Common Issues

1. **No communication**: Check UART baudrate (`IOLINK_COM_HZ`) and pin configuration
2. **CRC errors**: Verify byte order and timing
3. **Stack overflow**: Increase task stack size (see [MEMORY_GUIDE.md](MEMORY_GUIDE.md))
4. **Timing issues**: Call `iolink_device_process(ctx)` from exactly one task, often
   enough for the configured `iolink_stack_config_t::min_cycle_time`; the 1 ms
   delay in the host examples is a convenience, not a COM3 budget

## Testing on New Platform

1. **Build test**: Verify compilation, then run `./tools/build_matrix.sh` if the
   port introduces a switch or an `#if`
2. **PHY test**: Test UART loopback
3. **CRC test**: Run CRC unit tests
4. **Integration test**: Use Virtual Master
5. **Hardware test**: Connect to real IO-Link Master

Which gates a change must pass is listed in [TESTING.md](TESTING.md); the compile
matrix is mandatory for any new `IOLINK_*` switch or `#if`.

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
