# API 与集成指南

本文是 [API.md](API.md) 的集成向补充文档，覆盖公开契约、必要的构建与平台适配、
ISR 传输机制，以及容易被忽略的当前限制。权威声明位于 `include/iolinki/` 下的头文件；
当代码与文字描述冲突时，以 `docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/` 中的
IO-Link V1.1.5 PDF 为准。

本栈使用 C99，基于上下文，无动态内存分配，且与硬件无关。
应用负责所有存储：上下文、配置、PHY 状态、缓冲区以及持久化后端。

## 1. 架构与执行模型

```
Application
  app callbacks, PD update/read, events, vendor ISDU
        |
iolink_device_*                  include/iolinki/device.h
        |
iolink_dll_* / iolink_isdu_*     include/iolinki/dll.h, isdu.h
        |
iolink_phy_api_t                 include/iolinki/phy.h
        |
transceiver / MCU driver
```

两个执行上下文协作使用同一个 `iolink_device_ctx_t`：

- **主上下文**调用 `iolink_device_process()`，负责协议逻辑、ISDU/服务、参数、
  事件、Data Storage 以及应用回调。
- **数据 ISR** 调用 `iolink_device_process_isr()`，负责对收到的 M-sequence 进行
  组帧、把请求发布到邮箱，并发送已经暂存的应答。它不运行任何应用/协议逻辑。

有效请求必须在 t_A（10 个位时间）内得到应答。请保持 ISR 泵足够短，并在真实硬件上
接好 `phy->request_service()`，使主上下文产生的应答由 ISR 发送，而不是由主循环发送。

## 2. 最小集成

### 2.1 CMake

```cmake
add_subdirectory(path/to/iolinki)
target_link_libraries(your_firmware PRIVATE iolinki)
target_include_directories(your_firmware PRIVATE path/to/iolinki/include)
```

`iolinki` 不会导出接口 include 目录，因此需要显式添加 `include/` 路径。作为子项目使用时，
优先使用 `add_subdirectory(path/to/iolinki EXCLUDE_FROM_ALL)`，避免把自带的示例/测试
加入默认构建；`-DBUILD_TESTING=OFF` 还可以额外关闭测试。默认的
`IOLINK_PLATFORM=LINUX` 构建会引入 `phy_virtual.c`；不要在 MCU 固件中使用
该配置。自定义目标可只编译需要的源文件：

```text
src/device.c src/dll.c src/dll_isr.c src/isdu.c src/events.c src/params.c
src/data_storage.c src/device_info.c src/crc.c
src/platform.c                    # or your own critical-section implementation
src/phy_<your_transceiver>.c
```

MCU 构建中应排除 `src/phy_virtual.c` 和 Linux 平台源文件。

构建并测试主机配置：

```bash
cmake -B build && cmake --build build
(cd build && ctest --output-on-failure)
```

### 2.2 平台符号与时间源

`iolink_critical_enter()` / `iolink_critical_exit()` 必须由平台提供**真实实现**：

```c
void iolink_critical_enter(void);
void iolink_critical_exit(void);
```

`src/platform.c` 中的 weak no-op 只用于让库可链接。`iolink_device_init()` 会探测
这两个钩子；如果仍在用 weak 默认实现，初始化返回
`IOLINK_DEVICE_INIT_ERR_CRITICAL`。实现必须可嵌套，并能在 ISR 双泵下保护邮箱、
ISDU 快照和应答暂存区。

`iolink_device_config_t::time_us` 是必需的。`iolink_device_init()` 在回调为
NULL 时直接返回 `IOLINK_DEVICE_INIT_ERR_TIME`，不会进入运行阶段。设置后，DLL、
ISR 双泵和事件引擎的所有时间戳都从该回调获取。

核心不再调用平台时间函数；直接使用 `iolink_dll_*` 时，调用方必须在
`iolink_dll_init()` 后自行绑定 `ctx->time_us`（及 `time_user`）。未绑定时内部
计时读数为 0。

`time_us` 会在主循环和数据 ISR 中调用，因此必须单调、非阻塞、可重入，并在
上下文生命周期内保持有效。毫秒时间用于事件/状态逻辑，微秒时间用于帧计时以及
t_A/t_ren 计时。若从毫秒计数器推导微秒，精度不足以支持硬件时序；在 MCU 上应
提供真实的微秒定时器。

ISR 双泵传输要求临界区可嵌套（例如 Cortex-M 上的 PRIMASK/BASEPRI 保存恢复）。
库不提供也不使用线程锁：API 只允许一个主上下文调用者，因此临界区只需挡数据 ISR。
RTOS 端口即使把临界区实现为调度锁，也不能据此从多个任务并发调用库。

### 2.3 最小配置

```c
#include "iolinki/device.h"

static iolink_device_ctx_t g_device;

static int  board_phy_init(void* user);
static void board_phy_set_mode(void* user, iolink_phy_mode_t mode);
static void board_phy_set_baudrate(void* user, iolink_baudrate_t baud);
static int  board_phy_rx_octet(void* user, uint8_t* data, uint8_t* status);
static int  board_phy_tx_octet(void* user, uint8_t data);
static void board_phy_tx_done(void* user);
static uint8_t board_phy_int_active(void* user);
static void board_phy_request_service(void* user);
static uint64_t board_time_us(void* user);

static const iolink_phy_api_t g_phy = {
    .init = board_phy_init,
    .set_mode = board_phy_set_mode,
    .set_baudrate = board_phy_set_baudrate,
    .rx_octet = board_phy_rx_octet,       /* RX BYTE mode */
    .tx_octet = board_phy_tx_octet,       /* TX BYTE mode */
    .tx_done = board_phy_tx_done,         /* TX BYTE mode only */
    .int_active = board_phy_int_active,
    .request_service = board_phy_request_service,
};

/* Required while IOLINK_FEATURE_DATA_STORAGE=1; see section 5. */
static const iolink_storage_api_t g_storage;

static const iolink_device_config_t g_config = {
    .phy = g_phy,
    .stack = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
        .pd_in_len = 2U,
        .pd_out_len = 2U,
        .od_width = 1U,
        .min_cycle_time = 20U,            /* 2.0 ms, 0.1 ms units */
    },
    .time_us = board_time_us,             /* required; NULL rejects init */
#if IOLINK_FEATURE_DATA_STORAGE
    .storage = &g_storage,                /* required; read/write must be set */
#endif
    .user = NULL,
};

int app_init(void)
{
    /* Replace NULL with the application identity; see section 8.1. */
    iolink_device_info_init(NULL);
    return iolink_device_init(&g_device, &g_config);
}

void app_main_loop(void)
{
    iolink_device_process(&g_device);
}

void data_irq_handler(void)
{
    iolink_device_process_isr(&g_device);
}
```

`g_config` 必须在 `g_device` 的整个生命周期内保持有效。`app_callbacks`、`storage`、
PHY 驱动状态以及它们引用的所有缓冲区同样如此。

### 2.4 PHY 移植

所需回调取决于配置的 ISR 粒度：

| 函数 | 是否必需 | 说明 |
| --- | --- | --- |
| `init` | 实际使用中必需 | 由 `iolink_device_init()` 调用一次；负返回值会中止初始化。允许为 NULL，但链路无法工作。 |
| `set_mode` | 实际使用中必需 | 切换 SDCI/SIO/INACTIVE。 |
| `set_baudrate` | 实际使用中必需 | COM1/2/3。 |
| `rx_octet` | RX BYTE 模式 | 读到 octet 返回 1，无数据返回 0，出错返回负值。 |
| `rx_frame` | RX FRAME 模式 | 返回 PHY 持有的完整帧，在下一次调用前有效；不得返回两字节帧头或其他截断片段。 |
| `tx_octet` | TX BYTE 模式 | 送入一个应答 octet。 |
| `tx_frame` | TX FRAME 模式 | 发送完整应答，通常使用 DMA。 |
| `tx_done` | TX BYTE 模式 | 发完最后一字节后释放线路；RX BYTE 的 gap 也会用它释放接收引擎。TX FRAME 下该成员不存在，由 PHY 自己在传输完成中断里收尾。 |
| `tx_busy` | TX FRAME 可选 | 线路仍被占用时返回非 0；库据此推迟 ISR 应答并在主泵释放 PD-in 缓冲。为 NULL 时只能等下一次 capture 复用缓冲。 |
| `int_active` | 必需 | 数据中断电平；用于门控 ISR 泵。 |
| `request_service` | 推荐 | 主上下文暂存应答后唤醒数据 ISR。当 `IOLINK_PHY_FEATURE_ISR_TX_ONLY=1` 时必需。 |
| `response_timer_start/stop` | 可选 | 硬件 t_A 定时器。为 NULL 时使用软件时间戳。 |
| `stack_timer_start/stop` | 可选 | 周期性的主上下文服务窗口。 |
| `detect_wakeup` | 可选 | 存在时 DLL 从 SIO 开始，并负责唤醒/SDCI 转换。 |
| `set_cq_line` | 可选 | SIO 推挽输出。 |
| `get_faults` | 可选 | 首选的物理故障 API（`IOLINK_PHY_FAULT_*`）：DLL 按边沿上报映射事件。 |
| `get_voltage_mv`, `is_short_circuit` | 可选，旧版 | 仅在 `get_faults == NULL` 时查询。 |

当 `detect_wakeup == NULL` 时，DLL 认为 PHY 自己管理线路状态，并直接从 SDCI 开始。
这对自行执行唤醒握手的收发器很重要。该行为完全由运行时根据 `detect_wakeup`
进行的推断决定，没有额外的编译期开关。

当 `IOLINK_PHY_FEATURE_ISR_TX_ONLY=0` 时，没有 `request_service()` 的 PHY 会获得
从调用者上下文直接发送的旧版回退路径。这只适用于主机测试，不适用于必须满足 t_A 的
真实设备。把该宏设为 1 后，缺少该钩子会导致应答被丢弃，并递增
`dll.isr_tx_dropped`。

## 3. 设备配置参考

### 3.1 `iolink_device_config_t`

| 字段 | 生命周期 / 契约 |
| --- | --- |
| `phy` | 初始化时复制。`phy.user` 会传给每个回调。 |
| `stack` | 复制到上下文中。 |
| `app_callbacks` | 保留指针；必须比上下文存活更久。可以为 NULL。 |
| `device_info` | 身份来源和初始应用标签（Application Tag）。参见 8.1 节的身份限制。 |
| `storage` | 仅在 `IOLINK_FEATURE_DATA_STORAGE=1` 时声明，且为必需项；`read`/`write` 必须非 NULL。 |
| `time_us` | 必需的应用时钟。DLL、ISR 双泵和事件引擎都用它计时；回调必须单调、非阻塞、ISR 安全。为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_TIME`。 |
| `user` | 传给 PHY 回调和 vendor service 的透明指针。 |
| `vendor_service` | 可选的应用 ISDU 处理回调；参见第 6 节。 |

### 3.2 `iolink_stack_config_t`

| 字段 | 含义 |
| --- | --- |
| `m_seq_type` | 固定 M-sequence 类型，或用于可变 PD 的 `IOLINK_M_SEQ_TYPE_2_V`。 |
| `min_cycle_time` | 最小循环时间，单位 0.1 ms；在 DPP1 中按表 B.3 编码。 |
| `pd_in_len`, `pd_out_len` | 初始/声明的 PD 长度。 |
| `t_pd_us` | 上电后接受帧之前的延迟。 |
| `od_width` | OD octet 数（1/2/8/32）；0 表示根据 M-sequence 类型推导。 |
| `baudrate`, `baudrate_set` | 当 `baudrate_set` 为 true 时，初始化阶段应用的 COM。 |
| `master_lost_cycles` | 判定 `master_lost` 前的丢失周期数；0 表示关闭该提示。 |

初始化会用 `IOLINK_DEVICE_INIT_ERR_*` 拒绝非法组合（参见 `device.h`），包括
`time_us` 为 NULL、平台临界区仍是 weak 默认实现、Data Storage 已启用但 `storage`
缺失或 `read`/`write` 为空、PD/OD 大小超过编译期缓冲区、固定布局不匹配、
`IOLINK_APP_PD_INPUT_CALLBACK=1` 但未提供 `on_pd_input`，以及
`IOLINK_PHY_FEATURE_TX_BUSY=1` 但 PHY 未提供 `tx_busy`。
PHY `init` 的负返回值会原样向上传播。

`IOLINK_M_SEQ_TYPE_1_1` 属于旧版类型，当前传输层会拒绝。
`IOLINK_M_SEQ_TYPE_2_V` 是唯一可以通过 `iolink_device_set_pd_length()` 在运行时
修改 PD 长度的类型。

## 4. 设备公开 API

### 4.1 生命周期与泵

```c
int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);
void iolink_device_process(iolink_device_ctx_t* ctx);
void iolink_device_process_isr(iolink_device_ctx_t* ctx);
```

应由且仅由一个主上下文周期性调用 `iolink_device_process()`。只能从 PHY 数据中断调用
`iolink_device_process_isr()`。同一上下文上并发运行这两个函数是不安全的。

### 4.2 过程数据

```c
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx,
                                  const uint8_t* data, size_t len, bool valid);
int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx,
                                 uint8_t* data, size_t len);
int iolink_device_set_pd_length(iolink_device_ctx_t* ctx,
                                uint8_t pd_in_len, uint8_t pd_out_len);
```

`pd_input_update()` 发布设备到主站的数据。`valid` 决定 CKS 中的 PD-invalid 位。
对于 `TYPE_0` 以外的固定 M-sequence 类型，长度必须等于配置的最大值；`TYPE_2_V`
接受不超过配置最大值的任意长度；`TYPE_0` 不携带 PD，但允许调用该函数。当
`IOLINK_APP_PD_INPUT_CALLBACK=1` 时，该函数**不会被声明**（调用会编译/链接失败），
PD-in 路径只由 `on_pd_input()` 提供。

`pd_output_read()` 最多复制 `len` 字节，并返回实际复制的字节数；参数非法时返回负值。
`pd_out` 由同一主上下文中的帧分发写入，因此该函数直接复制，不取锁，也不进入平台临界区。

`set_pd_length()` 对非法参数返回 -1，对 `TYPE_2_V` 以外的任何 M-sequence 类型返回 -2。

### 4.3 应用回调

通过 `config.app_callbacks` 注册；没有单独注册函数。所有回调都在主上下文运行，绝不在
ISR 中运行。

| 回调 | 调用时机 |
| --- | --- |
| `on_startup(user)` | 进入 STARTUP。 |
| `on_preoperate(user)`, `on_operate(user)` | 进入对应状态。 |
| `on_pd_input(user, data, len)` | 当 `IOLINK_APP_PD_INPUT_CALLBACK=1` 时，每个 OPERATE 周期调用；返回值作为 PD-valid 标志。 |
| `on_pd_output(user, data, len)` | 每个 OPERATE 周期调用，携带最新的主站输出数据。 |
| `on_state_change(user, state)` | 当前 DLL 状态（电平式）：状态变化时调用一次，主上下文迁移立即投递，ISR 期间的多次迁移合并为最终状态；例外是 ISR 进入 STARTUP 的复位迁移，会先于最终状态单独投递。 |
| `on_master_lost(user)`, `on_master_online(user)` | 主站静默提示的边沿。 |

所有回调的 `user` 参数就是 `iolink_device_config_t.user`。

`on_pd_output` 收到的是指向上下文内部的指针，不能保留。

### 4.4 复位与状态查询

```c
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx,
                                     iolink_reset_handler_t handler);
iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx);
bool iolink_device_master_lost(const iolink_device_ctx_t* ctx);
iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx);
iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx);
iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx);
uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx);
uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx);
void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx,
                                 iolink_dll_stats_t* out_stats);
void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable);
void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us);
```

复位处理回调会收到 `IOLINK_RESET_DEVICE`（System Command 0x80）或
`IOLINK_RESET_APPLICATION`（0x81）。本栈不会自行复位 MCU。`master_lost` 只是给应用的
提示，不会改变 DLL 状态或波特率。

`iolink_dll_stats_t` 提供 CRC、超时、帧错误、时序错误、t_ren/t_cycle/t_byte/t_pd
违规、重试以及物理故障计数器。

## 5. 事件

```c
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code,
                          iolink_event_type_t type);
void iolink_event_clear(iolink_events_ctx_t* ctx, uint16_t code);
void iolink_events_process(iolink_events_ctx_t* ctx);
void iolink_events_reset(iolink_events_ctx_t* ctx);
bool iolink_events_pending(const iolink_events_ctx_t* ctx);
bool iolink_events_flag(const iolink_events_ctx_t* ctx);
bool iolink_events_pop(iolink_events_ctx_t* ctx, iolink_event_t* event);
bool iolink_events_peek(const iolink_events_ctx_t* ctx, iolink_event_t* event);
uint8_t iolink_events_get_highest_severity(iolink_events_ctx_t* ctx);
uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count);
uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr);
void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value);
iolink_event_type_t iolink_event_classify(uint16_t code);
```

`iolink_device_process()` 会在 ISDU 传输空闲时调用 `iolink_events_process()`，
因此正常集成无需自行调用。使用 `iolink_event_trigger()` 触发错误/警告，
使用 `iolink_event_clear()` 撤销；通知事件仅发送一次。事件引擎实现
出现/消失配对，在 `IOLINK_EVENT_MIN_APPEAR_MS`（默认 50 ms）内抑制
消失事件，并把同一事件代码的重复上报限制为 `IOLINK_EVENT_MIN_REPEAT_MS`
（默认 1000 ms）。

队列深度为 `IOLINK_EVENT_QUEUE_SIZE`（默认 4）；同时处于活动状态的错误/警告
代码由 `IOLINK_EVENT_PENDING_MAX`（默认 8）跟踪。诊断通道事件内存（表 58）由协议栈
自动处理；事件标志通过 CKS bit 7 暴露，并由主站写入 StatusCode 时清除。

标准化 EventCode 优先使用 `events.h` 中的 `IOLINK_EVENTCODE_*` 常量。
`IOLINK_EVENT_COMM_*` 以及 `0x1800-0x18FF` 属于厂商特定的设备事件，
不是编号相似的主站/端口事件。

## 6. ISDU、标准索引与 vendor service

### 6.1 协议栈支持的标准对象

| 索引 | 对象 | 访问 |
| --- | --- | --- |
| `0x0000` | 直接参数页 1 | 读；第 1 页只读 |
| `0x0001` | 直接参数页 2 | 读/写，受参数锁约束 |
| `0x0002` | SystemCommand | 只写 |
| `0x0003` | Data Storage | 读/写；取决于特性开关 |
| `0x000C` | DeviceAccessLocks | 读/写 |
| `0x000D` | ProfileCharacteristic | 读；取决于 Profile 支持情况 |
| `0x0010-0x0017` | 厂商/产品/版本字符串 | 读；写请求会被拒绝 |
| `0x0018-0x001A` | 应用/功能/位置标签 | 读/写；持久化 |
| `0x001B` | ProductURI | 读；未配置时为空 |
| `0x0020` | ErrorCount | 读 |
| `0x0024` | DeviceStatus | 读 |
| `0x0025` | DetailedDeviceStatus | 读；取决于事件特性 |
| `0x0028`, `0x0029` | ProcessDataInput/Output | 读 |
| `0x0040-0x00FE`, `0x0100+` | 设备/厂商特定 | 路由到 `vendor_service` |

`0x0004-0x000B` 和 `0x001C-0x001F` 为保留区间，`0x00FF` 也为保留值
（不会路由到 vendor service）。VendorID/DeviceID **不是** ISDU 对象；
它们位于 DPP1 的 `0x07-0x0B` 字节；库不再暴露旧版
`IOLINK_IDX_VENDOR_ID`/`IOLINK_IDX_DEVICE_ID` 常量。

字符串应答按对象限制长度（通常为 64 字节，SerialNumber 为 16，ProductURI 为 100）。
未知索引返回 `0x80 0x11`（索引不可用）；对已知只读对象的写请求返回
`0x80 0x23`。

当 `IOLINK_FEATURE_DATA_STORAGE=0` 时，索引 `0x0003` 返回 `0x80 0x11`。
当 `IOLINK_FEATURE_EVENTS=0` 时，索引 `0x0025` 返回 `0x80 0x11`。

### 6.2 vendor service 契约

```c
uint8_t (*vendor_service)(void* user, uint16_t index, uint8_t subindex,
                          bool write, const uint8_t* input, size_t length,
                          uint8_t* output, size_t* output_length);
```

- 注册后，`0x0040-0x00FE` 以及 `>= 0x0100` 会调用该回调。它拦截整个范围，
  包括 `0x0040`（只有在未注册 vendor service 时，内置的错误统计对象才可达）。
- 进入时，`*output_length` 是输出容量。成功时应把它设置为实际写入的字节数。
  如果上报长度大于容量，服务会以 `IOLINK_ISDU_ERROR_IDX_NOT_AVAIL`（0x8011）拒绝。
- 成功返回 0；失败返回 `IOLINK_ISDU_ERROR_*` 附加错误码。非零返回会产生
  `0x80 <code>`。
- 写操作中，`input`/`length` 是收到的负载。读操作中 `length` 为 0；
  `input` 可能仍是内部缓冲区的非 NULL 指针（Data Storage 桥接路径会传 NULL），
  因此 `length == 0` 时绝不能解引用它。ISDU 读操作中 `output` 通常非 NULL，
  但 Data Storage 写路径会以 `output == NULL` 调用回调，需要兼容。
- 回调在主上下文运行，必须有界，并且不能保留 `input`/`output` 指针。
- 设置参数锁后，该范围内的写操作会在调用回调前被拒绝。

### 6.3 底层 ISDU 传输

以下接口用于测试和自定义传输；正常应用应使用设备层：

```c
void iolink_isdu_init(iolink_isdu_ctx_t* ctx);
void iolink_isdu_process(iolink_isdu_ctx_t* ctx);
void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl,
                          const uint8_t* od, uint8_t od_len);
void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl,
                         uint8_t* od_out, uint8_t od_len);
uint8_t iolink_isdu_snapshot_response(const iolink_isdu_ctx_t* ctx, bool* busy,
                                      uint8_t* dst, size_t dst_size);
bool iolink_isdu_is_idle(const iolink_isdu_ctx_t* ctx); /* NULL -> false */
int iolink_isdu_get_response_byte(iolink_isdu_ctx_t* ctx, uint8_t* byte);
uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr);
void iolink_isdu_execute_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd);
```

只要调用者保证复制过程相对数据中断是原子的，`iolink_isdu_snapshot_response()`
就是 ISR 安全的。其他函数均在主上下文调用。

## 7. 参数、Data Storage 与持久化

### 7.1 参数管理器

```c
void iolink_params_ctx_init(iolink_params_ctx_t* ctx,
                            iolink_device_info_ctx_t* device_info);
void iolink_params_bind_storage(iolink_params_ctx_t* ctx,
                                const iolink_storage_api_t* storage);
int iolink_params_ctx_get(const iolink_params_ctx_t* ctx, uint16_t index,
                          uint8_t subindex, uint8_t* buffer, size_t max_len);
int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index,
                          uint8_t subindex, const uint8_t* data, size_t len,
                          iolink_persist_t persist);
void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx);
```

上下文 API 管理三个可写标签。`get` 返回字节数或负错误码；当
`persist != IOLINK_PERSIST_NONE` 时，`set` 只有在存储写入成功后才应用修改。
恢复出厂设置函数返回 void，无法报告存储失败。

旧版 `iolink_params_*` 函数仅在 `IOLINK_FEATURE_LEGACY_PARAMS=1` 时存在，
并操作进程级状态。新代码应使用上下文 API。

### 7.2 存储后端

```c
typedef struct {
    void* user;
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
    int (*erase)(void* user, uint32_t offset, size_t len);
} iolink_storage_api_t;
```

- 通过 `config.storage` 只提供一个后端。
- offset 相对于应用拥有的存储区域；物理基址应在回调内部自行加上。
- 成功返回 0，失败返回非零。持久化需要 `read`/`write`；核心当前不会调用 `erase`。
- `ASYNC` 表示回调自行负责必要的缓冲区复制；`SYNC` 必须在返回前完成提交。

区域布局（`include/iolinki/storage.h`）：

```
PARAMS | DS image | DS upload flag
```

`IOLINK_STORAGE_TOTAL_SIZE` 是所需大小。用静态断言确定 NVM 大小：

```c
static uint8_t g_nvm[IOLINK_STORAGE_TOTAL_SIZE];
_Static_assert(sizeof(g_nvm) >= IOLINK_STORAGE_TOTAL_SIZE, "NVM too small");
```

记录包含 `IOLINK_STORAGE_MAGIC`、`IOLINK_STORAGE_VERSION` 和 Fletcher-32 校验和。
magic/version/校验和不匹配会被视为未初始化并丢弃记录；目前没有迁移回调。
布局变更必须提升 magic 或 version。

### 7.3 Data Storage

```c
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage);
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);
void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx,
                                 iolink_ds_param_read_fn read,
                                 iolink_ds_param_write_fn write, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count);
void iolink_ds_set_param_desc(iolink_ds_ctx_t* ctx,
                              const iolink_ds_param_desc_t* params, size_t count);
void iolink_ds_process(iolink_ds_ctx_t* ctx);
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);
int  iolink_ds_start_upload(iolink_ds_ctx_t* ctx);
int  iolink_ds_start_download(iolink_ds_ctx_t* ctx);
int  iolink_ds_abort(iolink_ds_ctx_t* ctx);
int  iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd,
                              uint16_t access_locks);
int  iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t command,
                                 uint16_t access_locks);
int  iolink_ds_build_image(iolink_ds_ctx_t* ctx);
int  iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);
int  iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           const uint8_t* data, size_t len);
const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len);
uint32_t iolink_ds_calc_checksum(const uint8_t* data, size_t len);
bool iolink_ds_verify(const iolink_ds_ctx_t* ctx);
bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx);
uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx);
uint32_t iolink_ds_size(const iolink_ds_ctx_t* ctx);
uint32_t iolink_ds_checksum(const iolink_ds_ctx_t* ctx);
size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx);
bool iolink_ds_param_at(const iolink_ds_ctx_t* ctx, size_t i,
                        iolink_ds_param_desc_t* out);
void iolink_ds_request_upload(iolink_ds_ctx_t* ctx);
void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx);
```

当 `IOLINK_FEATURE_DATA_STORAGE=1` 时，`iolink_device_init()` 会用必需的
`config.storage` 初始化 DS 上下文、加载已持久化的镜像/标志并重新绑定默认访问器。
`storage` 缺失或 `read`/`write` 为空会在初始化阶段被拒绝。启动时若不存在已持久化
的镜像，会重新构建镜像。直接使用 `iolink_dll_*` 时才允许 `iolink_ds_init(..., NULL)`
的 RAM-only 用法。

序列化记录格式为：

```
[Index:2 大端][Subindex:1][Length:1][Data:Length]
```

`IOLINK_DS_IMAGE_MAX`（默认 256）限制镜像和持久化 DS 区域的大小。添加厂商参数有两种
方式：用 `iolink_ds_set_param_desc()` 替换描述符列表（默认访问器已经把未知索引路由到
`vendor_service`），或用 `iolink_ds_bind_param_access()` 安装完全自定义的访问器对。
描述符数组必须比上下文存活更久。

`iolink_device_process()` 会在 ISDU 空闲时调用 `iolink_ds_process()`。块下载通过
`iolink_ds_stage_param()` 暂存写操作，并在 `ParamDownloadEnd` 原子提交；
`iolink_ds_downloading()` 暴露该状态。`DS_UPLOAD_FLAG`/`State_Property` 以及
`DS_UPLOAD_REQ` 通知用于告知主站：本地参数已变化，应进行备份。

## 8. 当前限制与注意事项

### 8.1 设备身份是进程级的

每个上下文的 API 为：

```c
void iolink_device_info_ctx_init(iolink_device_info_ctx_t* ctx,
                                 const iolink_device_info_t* configured);
const iolink_device_info_t* iolink_device_info_ctx_get(
    const iolink_device_info_ctx_t* ctx);
int iolink_device_info_ctx_set_application_tag(iolink_device_info_ctx_t* ctx,
                                               const char* tag, uint8_t len);
uint16_t iolink_device_info_ctx_get_access_locks(
    const iolink_device_info_ctx_t* ctx);
void iolink_device_info_ctx_set_access_locks(iolink_device_info_ctx_t* ctx,
                                             uint16_t locks);
```

全局包装函数（`iolink_device_info_init/get/set_application_tag/get_access_locks/
set_access_locks`）操作同一个进程级上下文。

身份相关路径（强制字符串索引和直接参数页 1）会调用
`iolink_device_info_get()`，读取的是进程级旧版上下文。运行时访问锁对象和
参数锁检查也是如此。因此：

- `config.device_info` 提供初始应用标签（Application Tag），但本身不会让 `0x0010+` 或 DPP1
  上报该身份。
- 在初始化前用 `iolink_device_info_init(&identity)` 注册身份（并把同一个对象传给
  `config.device_info` 以提供应用标签默认值）。
- `iolink_device_info_ctx_set_access_locks()` 只更新每个上下文的结构。ISDU 的
  参数锁检查读取的是旧版全局状态，因此在单设备场景下，如果运行时锁必须
  生效，应使用 `iolink_device_info_set_access_locks()`；只调用上下文设置函数不够。
- 同一进程中的多个设备上下文共享身份和访问锁。在该路径改为按上下文保存之前，
  不要假设多设备身份完全独立。

### 8.2 `time_us` 与平台时间源

`iolink_device_config_t::time_us` 在 `iolink_device_init()` 中绑定到 DLL 和事件
上下文。该回调为必需项：NULL 会在初始化阶段被拒绝并返回
`IOLINK_DEVICE_INIT_ERR_TIME`。绑定后，主循环、数据 ISR 和事件引擎都使用该回调，
核心不再调用平台时间函数。直接使用 `iolink_dll_*` 时，调用方需在初始化后自行绑定
`ctx->time_us`/`time_user`。时间源必须在上下文生命周期内保持单调。

### 8.3 旧版全局状态

当 `IOLINK_FEATURE_LEGACY_PARAMS=1`（默认）时，存在进程级参数和 device-info
包装函数。它们适合单实例测试，但不适合可重入或多实例场景。

### 8.4 回调与缓冲区生命周期

除非契约明确说明，回调不得保留指向协议栈内部缓冲区的指针（`rx_frame` 的有效期到下一次
调用；DS 描述符数组和 `config` 必须保持存活）。应用回调和 `vendor_service` 在主上下文
运行；不要阻塞，也不要递归回调同一上下文。

### 8.5 关闭的特性

特性裁剪会同时移除公开声明和实现。调用已关闭的 API 会在编译/链接阶段失败。
无法移除的线上可见对象则返回否定应答（特性关闭时 `0x0003` 和 `0x0025` 返回
`0x80 0x11`）。

## 9. 构建期裁剪

在包含 `config.h` 之前或通过构建系统设置特性宏：

| 宏 | 默认值 | 为 0 时的效果 |
| --- | --- | --- |
| `IOLINK_FEATURE_DATA_STORAGE` | 1 | 无 DS 引擎/ISDU 索引；`0x0003` 返回索引不可用。 |
| `IOLINK_FEATURE_SYSTEM_COMMAND` | 1 | SystemCommand 返回功能不可用。 |
| `IOLINK_FEATURE_EVENTS` | 1 | 无事件引擎/DetailedDeviceStatus/事件标志；`0x0025` 返回索引不可用。 |
| `IOLINK_FEATURE_LEGACY_PARAMS` | 1 | 移除旧版全局参数 API。 |
| `IOLINK_PHY_FEATURE_WAKEUP` | 1 | 移除 `detect_wakeup` 字段。 |
| `IOLINK_PHY_FEATURE_SIO_CQ` | 1 | 移除 `set_cq_line` 字段。 |
| `IOLINK_PHY_FEATURE_LEGACY_DIAG` | 1 | 移除旧版电压/短路钩子。 |

CMake 配置档：

- `IOLINK_MINIMAL_PROFILE`：关闭 DS、SystemCommand、旧版参数、唤醒、SIO C/Q 和
  旧版诊断；保留事件。
- `IOLINK_TINY_PROFILE`：在最小配置基础上再关闭事件。

缓冲区大小宏包括 `IOLINK_PD_IN_MAX_SIZE`、`IOLINK_PD_OUT_MAX_SIZE`、
`IOLINK_OD_MAX_SIZE`、`IOLINK_ISDU_BUFFER_SIZE`、`IOLINK_DS_IMAGE_MAX`、
`IOLINK_EVENT_QUEUE_SIZE` 和 `IOLINK_EVENT_PENDING_MAX`。`IOLINK_ISDU_BUFFER_SIZE`
必须足够容纳最大字符串以及组帧余量；raw octet 传输会为请求额外增加 6 字节，为应答额外
增加 3 字节。

传输粒度由 `IOLINK_ISR_RX_MODE` 和 `IOLINK_ISR_TX_MODE` 选择（默认 `BYTE`，
DMA/整帧 PHY 使用 `FRAME`）。PHY 源文件可以用 `_Static_assert` 检查所需的模式组合。

## 10. 集成检查清单

1. 在 `config.time_us` 中提供单调、ISR 安全的微秒时钟，并实现可嵌套的
   `iolink_critical_enter/exit()`；初始化会拒绝 weak 默认临界区。
2. 为选定的 BYTE/FRAME 模式实现 PHY 回调，包括 `int_active` 和
   `request_service`。
3. 保证 `config`、PHY 状态和所有回调数据在上下文生命周期内有效。
4. 如果身份索引或 DPP1 必须正确，用 `iolink_device_info_init()` 注册身份。
5. 调用 `iolink_device_init()`，然后周期性调用 `iolink_device_process()`，
   并从数据中断调用 `iolink_device_process_isr()`。
6. PD 只能通过 `iolink_device_pd_input_update()` 或 `on_pd_input` 回调之一提供，
   不能同时使用。
7. 在 `IOLINK_FEATURE_DATA_STORAGE=1` 时提供完整存储后端（`read`/`write` 非空），
   并用静态断言检查 NVM 区域大小。
8. 通过 `vendor_service` 和/或 DS 描述符集合添加厂商参数。
9. 使用真实主站验证时序、唤醒/SIO 行为和故障映射。

## 11. 其他公开头文件与适配器

正常应用使用 `device.h` 及上文列出的特性头文件。其余公开头文件用于底层传输和板级支持：

- `include/iolinki/dll.h`：`iolink_dll_init/process/process_isr()`、
  `iolink_dll_expected_req_len()`、
  模式/波特率/PD 获取函数以及 `iolink_dll_stats_t`。
  设备层已经完成这些接线；只有实现自定义传输时才直接使用，并且需要自行负责
  事件/ISDU/DS 集成。
- `include/iolinki/phy_tiol112.h` 和 `phy_l6362a.h`：收发器驱动。用板级 IO 回调调用
  `iolink_phy_*_init()`，然后把 `*iolink_phy_*_get()` 传给设备配置。驱动和 IO 的
  `user` 存储必须比设备上下文存活更久。`iolink_phy_tiol112_fault()` 和
  `iolink_phy_l6362a_fault()/overload()` 报告聚合状态，不会臆测具体原因。
- `include/iolinki/phy_virtual.h`：主机/PTY 虚拟 PHY。只实现 BYTE 模式，
  不适用于 MCU 固件。
- `include/iolinki/phy_generic.h`：新 PHY 移植的参考模板。
- `include/iolinki/crc.h`：用于 V1.1.5 A.1.6 校验和的
  `iolink_checksum6()` / `iolink_checksum6_from_acc()`。
- `include/iolinki/protocol.h`：标准索引、ISDU 错误码、SystemCommand 和
  M-sequence 常量。应使用这些常量而不是数字字面量。
- `include/iolinki/platform.h`：临界区。时间源通过
  `iolink_device_config_t::time_us` 提供。旧版 `iolink_nvm_*` 接口已移除；
  持久化统一通过 `config.storage` 完成。

## 12. 参考资料

- `docs/API.md` — 精简 API 参考。
- `docs/PORTING.md` — 平台和收发器移植说明。
- `docs/STORAGE.md` — 持久化和 Data Storage 细节。
- `docs/feature-trimming.md` — 特性裁剪契约。
- `docs/CODE_FLOW.md` — ISR 传输的中文代码流程说明。
- `examples/reference_device/` — 带 PHY 模拟器的最小应用。
- `examples/stm32g0_tiol112/` — TIOL112 MCU 适配器。
- `examples/stm32g0_l6362a_raw/` — raw octet L6362A 适配器及 ISR 接线。
- `examples/host_demo/` — 基于伪终端虚拟 PHY 的主机示例。
