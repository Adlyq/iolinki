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
`iolink_dll_init()` 后自行绑定 `ctx->shared.time_us`（及 `shared.time_user`，
其中 `ctx` 是 `iolink_dll_main_t*`；初始化需要同时传入 ISR 对象）。未绑定时
内部计时读数为 0。

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

/* Required while IOLINK_FEATURE_DATA_STORAGE=1; see section 7.2. */
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
| `request_service` | 推荐 | 主上下文暂存应答后唤醒数据 ISR。设置 `IOLINK_STACK_FLAG_REQUIRE_ISR_TX`（或构建默认 `IOLINK_PHY_FEATURE_ISR_TX_ONLY=1`）时必需，缺少会在初始化阶段被拒绝。 |
| `response_timer_start/stop` | 可选 | 硬件 t_A 定时器。为 NULL 时使用软件时间戳。 |
| `stack_timer_start` | 可选 | 周期性的主上下文服务窗口（Timer B 到期时调用 `iolink_dll_on_stack_tick()`）。 |
| `detect_wakeup` | 可选 | 存在时 DLL 从 SIO 开始，并负责唤醒/SDCI 转换。 |
| `set_cq_line` | 可选 | SIO 推挽输出。 |
| `get_faults` | 可选 | 物理故障 API（`IOLINK_PHY_FAULT_*`）：DLL 按边沿上报映射事件；NULL 时诊断静默。 |

当 `detect_wakeup == NULL` 时，DLL 认为 PHY 自己管理线路状态，并直接从 SDCI 开始。
这对自行执行唤醒握手的收发器很重要。该行为完全由运行时根据 `detect_wakeup`
进行的推断决定，没有额外的编译期开关。

“所有应答只能由 ISR 发送”是运行时能力位 `IOLINK_CAP_REQUIRE_ISR_TX`，由
`iolink_stack_config_t::flags` 的 `IOLINK_STACK_FLAG_REQUIRE_ISR_TX` 打开，构建期
`IOLINK_PHY_FEATURE_ISR_TX_ONLY=1` 只是它的默认值。该位打开而 PHY 没有
`request_service()` 时，`iolink_device_init()` 返回 `IOLINK_DEVICE_INIT_ERR_PHY`
（DLL 内部另有计数 `dll.isr_tx_dropped` 的防御路径，正常配置不会走到）。
该位关闭时，没有 `request_service()` 的 PHY 会获得从调用者上下文直接发送的回退路径；
这只适用于主机测试，不适用于必须满足 t_A 的真实设备。

## 3. 设备配置参考

### 3.1 `iolink_device_config_t`

| 字段 | 生命周期 / 契约 |
| --- | --- |
| `phy` | 初始化时复制。`phy.user` 只传给 PHY 回调；与 `user` 相互独立。 |
| `stack` | 复制到上下文中。 |
| `app_callbacks` | 保留指针；必须比上下文存活更久。可以为 NULL；`on_pd_input` 非 NULL 时选择 PD-in 回调路径。 |
| `device_info` | 身份来源和初始应用标签（Application Tag）。参见 8.1 节的身份限制。 |
| `storage` | 仅在 `IOLINK_FEATURE_DATA_STORAGE=1` 时声明，且为必需项；`read`/`write` 必须非 NULL。 |
| `time_us` | 必需的应用时钟。DLL、ISR 双泵和事件引擎都用它计时；回调必须单调、非阻塞、ISR 安全。为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_TIME`。 |
| `user` | 传给 `time_us`、`log_write`、`vendor_service` 以及全部应用回调的透明指针。 |
| `log_write`, `log_level` | 可选运行时日志汇点与阈值；NULL / `IOLINK_LOG_LEVEL_SILENT` 表示静默。 |
| `od` | 可选的应用对象字典表（`include/iolinki/od.h`）。声明过的设备专用索引优先于 `vendor_service`。 |
| `vendor_service` | 可选的应用 ISDU 处理回调；参见第 6 节。 |
| `workspace` | 可选的传输 scratch 工作区（`include/iolinki/workspace.h`）。NULL 表示使用上下文内嵌 fallback；`IOLINK_WORKSPACE_EMBEDDED=0` 时必须提供，否则初始化返回 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`。 |

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
| `compat_flags` | `IOLINK_STACK_COMPAT_*` 兼容位，与构建期默认值按位或。 |
| `flags` | `IOLINK_STACK_FLAG_*`：`REQUIRE_ISR_TX`、`NO_SYSTEM_COMMAND`。 |
| `timing_enforce` | `IOLINK_STACK_TIMING_*`；`DEFAULT` 表示沿用构建档位默认值。 |
| `reserved` | 必须为 0。 |
| `event_min_appear_ms`, `event_min_repeat_ms` | 覆盖 A.6.4 的出现/重复窗口；0 表示沿用构建默认值。 |

初始化会用 `IOLINK_DEVICE_INIT_ERR_*` 拒绝非法组合（参见 `device.h`），包括
`time_us` 为 NULL、平台临界区仍是 weak 默认实现、Data Storage 已启用但 `storage`
缺失或 `read`/`write` 为空、PD/OD 大小超过编译期缓冲区、固定布局不匹配、
`IOLINK_WORKSPACE_EMBEDDED=0` 但未提供工作区（`IOLINK_DEVICE_INIT_ERR_WORKSPACE`），
以及能力位要求了 PHY 未提供的钩子（`IOLINK_DEVICE_INIT_ERR_PHY`：`REQUIRE_ISR_TX`
缺少 `request_service()`，或 TX FRAME 构建设置 `IOLINK_PHY_FEATURE_TX_BUSY=1`
但 PHY 没有 `tx_busy()`）。
PHY `init` 的负返回值会原样向上传播。

`IOLINK_M_SEQ_TYPE_1_1` 属于旧版类型，初始化会以
`IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE` 拒绝。
`IOLINK_M_SEQ_TYPE_2_V` 是唯一可以通过 `iolink_device_set_pd_length()` 在运行时
修改 PD 长度的类型。

## 4. 设备公开 API

### 4.1 生命周期与泵

```c
int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);
iolink_workspace_t* iolink_device_get_workspace(const iolink_device_ctx_t* ctx);
void iolink_device_process(iolink_device_ctx_t* ctx);
void iolink_device_process_isr(iolink_device_ctx_t* ctx);
```

应由且仅由一个主上下文周期性调用 `iolink_device_process()`。只能从 PHY 数据中断调用
`iolink_device_process_isr()`。同一上下文上并发运行这两个函数是不安全的。

`iolink_device_init()` 使用 `config.workspace`（可为 NULL，表示使用上下文内嵌的
ISDU/DS scratch）；把 scratch 放进 DTCM 或外部 RAM 时在配置里指向工作区即可，工作区
必须比上下文存活更久。`iolink_device_get_workspace()` 返回当前绑定的工作区，使用
内嵌 fallback 时返回 NULL。`IOLINK_WORKSPACE_EMBEDDED=0` 时没有
fallback，必须提供工作区，否则初始化返回 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`。

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
接受不超过配置最大值的任意长度；`TYPE_0` 不携带 PD，但允许调用该函数。参数非法或
所有 PD-in 缓冲区都被 ISR 占用时返回负值。

两条 PD-in 路径在同一构建内共存：`on_pd_input` 非 NULL 时初始化选择回调路径并置位
`IOLINK_CAP_PD_IN_CALLBACK`，此后 `pd_input_update()` 返回
`IOLINK_DEVICE_INIT_ERR_PD_INPUT`（-14），避免线上 PD 在回调输出与调用方数据之间
跳变；`on_pd_input` 为 NULL 时用轮询 API 发布。路径只由 `on_pd_input` 是否存在决定。

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
| `on_pd_input(user, data, len)` | 提供时（非 NULL），每个 OPERATE 周期调用；填写一个 ISR 未占用的 PD-in 缓冲区，返回值作为 PD-valid 标志。未提供时改用 `iolink_device_pd_input_update()`。 |
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

### 4.5 能力位

```c
iolink_caps_t iolink_device_capabilities(const iolink_device_ctx_t* ctx);
iolink_caps_t iolink_device_set_capabilities(iolink_device_ctx_t* ctx, iolink_caps_t caps);

const iolink_build_caps_t* iolink_caps_build(void);
iolink_caps_t iolink_caps_resolve(const iolink_phy_api_t* phy, const iolink_stack_config_t* stack,
                                  const iolink_app_callbacks_t* app,
                                  const iolink_build_caps_t* build);
bool iolink_caps_has(iolink_caps_t caps, iolink_caps_t bit);
bool iolink_caps_built(const iolink_build_caps_t* caps, uint32_t mask);
```

应用通常只需要读：`iolink_device_capabilities()` 返回本实例的运行时能力位
（`IOLINK_CAP_*`），`iolink_caps_build()` 返回只读的构建档位（`IOLINK_CAPB_*`、
ISR 粒度、并发/统计档位、PD-in 缓冲数）。实例能力在 `iolink_device_init()` 内由
`iolink_caps_resolve()` 一次性解析，因此运行期不需要再逐项判断 `phy->xxx != NULL`。

`iolink_device_set_capabilities()` 只接受运行时开关位（兼容位、SystemCommand、
四个访问锁位）。与 PHY vtable、构建档位或 `on_pd_input` 绑定的位会按当前配置重新
解析，调用方无法伪造；构建时未编入的 `IOLINK_CAP_EVENTS` /
`IOLINK_CAP_SYSTEM_COMMAND` 始终保持清零。返回实际生效的掩码。

应用需要做什么：按端口实际能力设置 `stack.flags` / `stack.compat_flags`，而不是
依赖已降级为兼容宏的编译期开关；如果端口要求“所有应答由 ISR 发送”，确保 PHY 提供
`request_service()`，否则初始化返回 `IOLINK_DEVICE_INIT_ERR_PHY`。

### 4.6 对象字典

```c
typedef struct {
    const iolink_od_entry_t* entries;
    size_t count;
    iolink_od_read_fn read;
    iolink_od_write_fn write;
    void* user;
} iolink_od_app_t;
```

参数只在对象字典里描述一次（索引/子索引、datatype codec、访问权限、写语义、
storage 标志、默认值与可选钩子），ISDU 分发、Data Storage 成员判定和 IODD 导出都从
这张表派生。标准对象（`0x0000-0x003F` 与厂商错误统计 `0x0040`）由内建 const 表提供；
应用通过 `config.od` 绑定自己的 const 表和后端。

应用需要做的是：用 `IOLINK_OD_SCALAR` / `IOLINK_OD_SCALAR_EX` / `IOLINK_OD_STRING`
增加一行表项，并在 `read` / `write` 后端里增加对应分支（表项只保存静态知识，运行期
取值走注册了 `IOLINK_OD_HANDLER_BACKEND` 的后端）。索引归属按域决定：规范索引（`< 0x0040`）由标准表提供、不可被应用表覆盖；设备专用索引
（`0x0040-0x00FE`、`0x0100+`）应用表优先，未声明时才交给 `vendor_service`。完整示例见
`examples/switching_sensor/switching_sensor_od.c`。

## 5. 事件

### 5.1 事件引擎

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
uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count);
uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr);
void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value);
```

`iolink_device_process()` 会在 ISDU 传输空闲时调用 `iolink_events_process()`，
因此正常集成无需自行调用。使用 `iolink_event_trigger()` 触发错误/警告，
使用 `iolink_event_clear()` 撤销；通知事件仅发送一次。事件引擎实现
出现/消失配对，在 `IOLINK_EVENT_MIN_APPEAR_MS`（默认 50 ms）内抑制
消失事件，并把同一事件代码的重复上报限制为 `IOLINK_EVENT_MIN_REPEAT_MS`
（默认 1000 ms）。这两个值只是构建默认值，可用
`iolink_stack_config_t::event_min_appear_ms` / `event_min_repeat_ms` 按实例覆盖
（0 表示沿用默认值）。

队列深度为 `IOLINK_EVENT_QUEUE_SIZE`（默认 4）；同时处于活动状态的错误/警告
代码由 `IOLINK_EVENT_PENDING_MAX`（默认 8）跟踪。诊断通道事件内存（表 58）由协议栈
自动处理；事件标志通过 CKS bit 7 暴露，并由主站写入 StatusCode 时清除。

标准化 EventCode 优先使用 `events.h` 中的 `IOLINK_EVENTCODE_*` 常量。
`IOLINK_EVENT_COMM_*` 以及 `0x1800-0x18FF` 属于厂商特定的设备事件，
不是编号相似的主站/端口事件。

### 5.2 诊断注册表

```c
bool iolink_diag_set_level(iolink_events_ctx_t* events, iolink_diag_src_t src, bool level);
void iolink_diag_pulse(iolink_events_ctx_t* events, iolink_diag_src_t src);
void iolink_diag_pulse_isr(iolink_events_ctx_t* events, iolink_diag_src_t src);
void iolink_diag_service(iolink_events_ctx_t* events, uint32_t now_ms);
void iolink_diag_raise_code(iolink_events_ctx_t* events, uint16_t code);
void iolink_diag_clear_code(iolink_events_ctx_t* events, uint16_t code);
uint8_t iolink_diag_device_status(iolink_events_ctx_t* events);
uint8_t iolink_diag_dds_snapshot(iolink_events_ctx_t* events,
                                 iolink_diag_dds_entry_t* out, uint8_t max);
```

EventCode、`0x0024` DeviceStatus 与 `0x0025` DetailedDeviceStatus 都从同一张 const
注册表派生，应用不需要也不应该另外维护一份状态：注册表提供规范 EventType 与表 D.1
首选 DeviceStatus，RAM 里只缓存来源事实（ISR one-shot、PULSE 最后发生时间、边沿
计数），条件生命周期仍由事件内存决定。`IOLINK_DIAG_SRC_PHY_*` 是电平源，用
`iolink_diag_set_level()` 每周期采样；`IOLINK_DIAG_SRC_COMM_*` 是脉冲源，用
`iolink_diag_pulse()` 报告，ISR 中只能用 `iolink_diag_pulse_isr()`（单字节写入），
再由 `iolink_diag_service()` 在主上下文消费（`iolink_dll_process()` 已经代劳，
应用无需自行调用）。应用自定义事件用
`iolink_diag_raise_code()` / `iolink_diag_clear_code()`，它们分别包装
`iolink_event_trigger()` / `iolink_event_clear()`。

应用需要做的是：接好 PHY 的 `get_faults()`（DLL 会按边沿送入电平源）或在应用层
直接调用上述函数；不要自己计算 DeviceStatus 或拼 DDS，直接读
`iolink_diag_device_status()` / `iolink_diag_dds_snapshot()`，它们与线上应答
`0x0024`/`0x0025` 完全一致。`IOLINK_FEATURE_EVENTS=0` 时整个模块消失。

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
| `0x0040-0x00FE`, `0x0100+` | 设备/厂商特定 | 应用 OD 表优先，未声明时路由到 `vendor_service` |

分发顺序（`src/isdu.c` 的 `handle_standard_commands()`）：先做 DS 访问权限检查，然后按索引域分流。

- 规范索引（`< 0x0040`）：内建标准表优先，应用表只在内建表未命中时才生效，因此
  应用表无法遮蔽 `0x0002`/`0x0024` 等对象。
- 设备专用索引（`0x0040-0x00FE`、`0x0100+`）：应用 OD 表 `iolink_od_find_app()` 优先；
  未声明时交给 `vendor_service`；仍未命中才回落到内建标准表（`0x0040` 错误统计），
  最后返回 `0x80 0x11`。因此应用表声明过的厂商索引不会到达 `vendor_service`，而
  `0x0040` 内置错误统计对象只有在应用表与 `vendor_service` 都没有接管时才可达。

`0x0004-0x000B` 和 `0x001C-0x001F` 为保留区间，`0x00FF` 也为保留值
（不会路由到 vendor service）。VendorID/DeviceID **不是** ISDU 对象；
它们位于 DPP1 的 `0x07-0x0B` 字节。

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

- 注册后，`0x0040-0x00FE` 以及 `>= 0x0100` 中未被应用 OD 表声明的索引会调用该回调
  （应用表优先）；`0x0040` 也走同一规则，内置错误统计对象只在应用表与该回调都
  未接管时可达。
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
void iolink_isdu_bind_workspace(iolink_isdu_ctx_t* ctx, iolink_workspace_t* ws);
void iolink_isdu_bind_od(iolink_isdu_ctx_t* ctx, const iolink_od_app_t* app);
void iolink_isdu_bind_caps(iolink_isdu_ctx_t* ctx, const iolink_caps_t* caps);
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

直接使用引擎时需自行绑定工作区、应用 OD 表与能力位（设备层已在初始化时完成）；
未绑定能力位时按“全部能力可用”处理，未绑定工作区时使用内嵌 fallback
（`IOLINK_WORKSPACE_EMBEDDED=0` 时为未绑定状态）。
只要调用者保证复制过程相对数据中断是原子的，`iolink_isdu_snapshot_response()`
就是 ISR 安全的。其他函数均在主上下文调用。

## 7. 参数、Data Storage 与持久化

### 7.1 参数管理器

```c
void iolink_params_ctx_init(iolink_params_ctx_t* ctx,
                            iolink_device_info_ctx_t* device_info);
void iolink_params_bind_storage(iolink_params_ctx_t* ctx,
                                const iolink_storage_api_t* storage);
iolink_result_t iolink_params_ctx_read(const iolink_params_ctx_t* ctx, uint16_t index,
                                       uint8_t subindex, uint8_t* buffer, size_t max_len);
iolink_result_t iolink_params_ctx_write(iolink_params_ctx_t* ctx, uint16_t index,
                                        uint8_t subindex, const uint8_t* data,
                                        size_t len, iolink_persist_t persist);
void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx);
```

上下文 API 管理三个可写标签，内部统一使用 `iolink_result_t`
（`include/iolinki/result.h`）：

- `code == 0`：成功，`len` 为读取的字节数（写操作为 0）；
- `code < 0`：内部错误，不进入线上协议（`IOLINK_RESULT_NOT_HANDLED` /
  `INVALID` / `IO`）；
- `code > 0`：ISDU ErrorCode（如 0x33 值长度超长），由 ISDU 应答边界原样上送。

当 `persist != IOLINK_PERSIST_NONE` 时，写操作只有在存储写入成功后才应用修改。
恢复出厂设置函数返回 void，无法报告存储失败。

### 7.2 存储后端

```c
typedef struct {
    void* user;
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
} iolink_storage_api_t;
```

- 通过 `config.storage` 只提供一个后端。
- offset 相对于应用拥有的存储区域；物理基址应在回调内部自行加上。
- 成功返回 0，失败返回非零。持久化只使用 `read`/`write`。
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
void iolink_ds_bind_workspace(iolink_ds_ctx_t* ctx, iolink_workspace_t* ws);
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);
void iolink_ds_bind_od(iolink_ds_ctx_t* ctx, const iolink_od_app_t* app);
void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx,
                                 iolink_ds_param_read_fn read,
                                 iolink_ds_param_write_fn write,
                                 iolink_ds_param_check_fn check, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count);
/* read/write/check 回调返回 iolink_result_t（见 §7.1 的结果语义）。 */
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
int  iolink_ds_serialize(iolink_ds_ctx_t* ctx, uint8_t* out, size_t cap);
void iolink_ds_refresh(iolink_ds_ctx_t* ctx);
int  iolink_ds_build_image(iolink_ds_ctx_t* ctx);
int  iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);
iolink_result_t iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index,
                                      uint8_t subindex, const uint8_t* data, size_t len);
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

DS 镜像不是上下文中的常驻数组：上下文只持有指向暂存缓冲的指针（
`iolink_workspace_t::ds_stage`，或内嵌 fallback）。上传和 `Parameter_Checksum` 读取
走流式路径 `iolink_ds_serialize()`（可直接写入 ISDU 应答缓冲，`out == NULL` 时只统计
长度与校验和）；`iolink_ds_refresh()` 只刷新 `Data_Storage_Size`/`Parameter_Checksum`
而不物化镜像。完整暂存缓冲只用于下载和 `iolink_ds_build_image()`。

默认 DS 参数集由 OD 表派生：标准表中 storage 为 `IOLINK_OD_STORAGE_DS` 的表项
在前，设备 `config.od` 应用表中同类项按表序追加（设备层自动调用
`iolink_ds_bind_od()`）。因此应用参数只要声明 `IOLINK_OD_STORAGE_DS` 就进入
Data Storage，无需再手工维护描述符；`IOLINK_OD_STORAGE_VOLATILE` 项不进入。
描述符的 `max_len` 取表项 `block_len`。

序列化记录格式为：

```
[Index:2 大端][Subindex:1][Length:1][Data:Length]
```

`IOLINK_DS_IMAGE_MAX`（默认 256）限制镜像和持久化 DS 区域的大小。添加厂商参数有两种
方式：用 `iolink_ds_set_param_desc()` 替换描述符列表（默认访问器已经把未知索引路由到
`vendor_service`），或用 `iolink_ds_bind_param_access()` 安装完全自定义的访问器对。
描述符数组必须比上下文存活更久。

`iolink_device_process()` 会在 ISDU 空闲时调用 `iolink_ds_process()`。块下载通过
`iolink_ds_stage_param()` 暂存写操作，并在 `ParamDownloadEnd` 先对整份镜像做无副作用
预校验（`check` 回调），全部通过后再逐条写入并持久化；预校验失败时不会写入任何参数。
这不是跨参数事务，NVM 写入失败仍可能留下已生效的前缀记录。
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

身份与参数全部按上下文绑定，没有进程级全局状态：

- `config.device_info` 提供静态身份（含 Application Tag 默认值），`iolink_device_init()`
  把它绑定到 `ctx->device_info`；`0x0010+` 与 DPP1 从该上下文读取。
- 运行时访问锁用 `iolink_device_info_ctx_set_access_locks()` / `_get_access_locks()`
  操作 `ctx->device_info`；ISDU 的参数锁检查读取同一个上下文。
- 可写参数通过 `ctx->params`（`iolink_params_ctx_*`）访问，ISDU 在初始化时绑定该上下文。
- 同一进程中的多个设备上下文各自独立，互不共享身份、访问锁或参数。

### 8.2 `time_us` 与平台时间源

`iolink_device_config_t::time_us` 在 `iolink_device_init()` 中绑定到 DLL 和事件
上下文。该回调为必需项：NULL 会在初始化阶段被拒绝并返回
`IOLINK_DEVICE_INIT_ERR_TIME`。绑定后，主循环、数据 ISR 和事件引擎都使用该回调，
核心不再调用平台时间函数。直接使用 `iolink_dll_*` 时，调用方需在初始化后自行绑定
`ctx->time_us`/`time_user`。时间源必须在上下文生命周期内保持单调。

### 8.3 上下文绑定

`iolink_device_init()` 会把 `config.device_info` 和内部 `params` 上下文绑定到 ISDU；
直接使用底层 `iolink_isdu_*` API 时，调用方需要自行设置 `isdu.params_ctx`/
`isdu.ds_ctx`。未绑定时的行为：身份索引（`0x000D` 与 `0x0010-0x001B`）返回
`IDX_NOT_AVAIL`；参数与访问锁路径按空上下文处理（读返回 -1、写锁被忽略）；
`0x0020`/`0x0024`/`0x0028`/`0x0029` 不依赖身份，仍然可用。

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
| `IOLINK_WORKSPACE_EMBEDDED` | 1 | 上下文不再内嵌 ISDU/DS scratch，必须提供工作区（否则 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`）。 |

运行期能力的解析来源：

| 能力位 | 解析来源 |
| --- | --- |
| `IOLINK_CAP_WAKEUP` | `detect_wakeup` 是否为 NULL |
| `IOLINK_CAP_SIO_CQ` | `set_cq_line` 是否为 NULL |
| `IOLINK_CAP_PD_IN_CALLBACK` | `on_pd_input` 是否为 NULL |
| `IOLINK_CAP_REQUIRE_ISR_TX` | 构建档位 + `request_service` |
| `IOLINK_CAP_COMPAT_*` | 构建档位的兼容位默认值，实例可在 `iolink_stack_config_t::compat_flags` 覆盖 |

`IOLINK_PHY_FEATURE_ISR_TX_ONLY` 仍提供 `IOLINK_STACK_FLAG_REQUIRE_ISR_TX` 的构建
默认值，`IOLINK_PHY_FEATURE_TX_BUSY` 仍决定 TX FRAME 构建是否要求 `tx_busy()`。

裁剪配置：

裁剪只通过两条正交的途径：

- 结构裁剪：显式关闭 `IOLINK_FEATURE_DATA_STORAGE` / `IOLINK_FEATURE_EVENTS`；
- 行为裁剪：运行时能力位（见 `capability.h` 与 [feature-trimming](feature-trimming.md)）。

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
4. 身份索引或 DPP1 需要正确时，在 `config.device_info` 中提供静态身份；传 NULL 时使用内置默认身份（`k_default_info`）。
5. 调用 `iolink_device_init()`，然后周期性
   调用 `iolink_device_process()`，并从数据中断调用 `iolink_device_process_isr()`。
6. 选择一种 PD-in 来源：提供 `on_pd_input` 回调，或调用
   `iolink_device_pd_input_update()`。两者同一次初始化只能选一个；选了回调后轮询调用
   会返回 `IOLINK_DEVICE_INIT_ERR_PD_INPUT`。
7. 在 `IOLINK_FEATURE_DATA_STORAGE=1` 时提供完整存储后端（`read`/`write` 非空），
   并用静态断言检查 NVM 区域大小；`IOLINK_WORKSPACE_EMBEDDED=0` 时还要提供
   `iolink_workspace_t`。
8. 通过 `config.od`（推荐，一行表项 + 后端分支）和/或 `vendor_service` 添加厂商参数。
9. 使用真实主站验证时序、唤醒/SIO 行为和故障映射。

## 11. 其他公开头文件与适配器

正常应用使用 `device.h` 及上文列出的特性头文件。其余公开头文件用于底层传输和板级支持：

- `include/iolinki/dll.h`：`iolink_dll_init/process/process_isr()`、
  `iolink_dll_expected_req_len()`、
  模式/波特率/PD 获取函数以及 `iolink_dll_stats_t`。
  状态布局拆在 `dll_main.h`（主上下文 + 共享区）、`dll_shared.h`（双上下文共享）、
  `dll_isr.h`（ISR 独占）和 `dll_ctx.h`（单次分配容器）中；直接使用 DLL 时按
  `iolink_dll_init(&ctx.main, &ctx.isr, phy)` 初始化。
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
- `include/iolinki/capability.h`：构建档位 `iolink_build_caps_t` 与实例能力位
  `IOLINK_CAP_*`/`IOLINK_CAPB_*` 的查询面，见 §4.5。
- `include/iolinki/od.h`：对象字典表项、datatype codec 与参数值访问层，见 §4.6。
- `include/iolinki/workspace.h`：可选的 ISDU/DS 传输 scratch 工作区，见 §4.1。
- `include/iolinki/diag.h`：诊断来源注册表与 DeviceStatus/DDS 派生，见 §5.2。
- `include/iolinki/port.h`：临界区与屏障原语。时间源通过
  `iolink_device_config_t::time_us` 提供；持久化统一通过 `config.storage`
  完成。

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
