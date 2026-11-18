# API 与集成指南

本文是 [API.md](API.md) 的集成向补充文档，覆盖构建与平台适配、ISR 传输机制、初始化
契约以及容易被忽略的当前限制。函数签名与错误码清单以 [API.md](API.md) 为精简参考，
最终权威是 `include/iolinki/` 下的头文件；当代码与文字描述冲突时，以
`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/` 中的 IO-Link V1.1.5 PDF 为准。

本栈使用 C99，基于上下文，无动态内存分配，且与硬件无关。
应用负责所有存储：上下文、配置、PHY 状态、缓冲区以及持久化；库不拥有 NVM，
参数与标志的落盘通过钩子交给应用。

## 1. 架构与执行模型

```
Application
  app callbacks, PD update/read, events, application OD
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

已安装的包（`cmake --install` 到某个前缀后）：

```cmake
find_package(iolinki CONFIG REQUIRED)
target_link_libraries(your_firmware PRIVATE iolinki::iolinki)
```

作为子目录：

```cmake
add_subdirectory(path/to/iolinki)
target_link_libraries(your_firmware PRIVATE iolinki::iolinki)
```

两种方式都通过目标导出 include 目录，不需要再手写 `include/`。安装到
`lib64`（RPM 系约定）时 CMake 的默认搜索路径可能不包含它，用
`-Diolinki_DIR=<prefix>/lib64/cmake/iolinki` 显式指定。作为子项目使用时，
优先使用 `add_subdirectory(path/to/iolinki EXCLUDE_FROM_ALL)`，避免把自带的示例/测试
加入默认构建；`-DBUILD_TESTING=OFF` 还可以额外关闭测试。默认的
`IOLINK_PLATFORM=LINUX` 构建会引入 `phy_virtual.c`；不要在 MCU 固件中使用
该配置。自定义目标可只编译需要的源文件：

```text
src/device.c src/dll.c src/dll_frame.c src/dll_sm.c src/dll_reply.c src/dll_api.c
src/dll_isr.c src/dll_isr_rx.c src/isdu.c src/isdu_cmd.c src/isdu_ds.c
src/events.c src/diag.c src/od.c src/data_storage.c src/device_info.c
src/capability.c src/profile.c src/crc.c
src/platform.c                    # 或你自己的临界区实现
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

`src/platform.c` 中的 weak no-op 只用于让库可链接。需要临界区的档位
（`IOLINK_NEED_CRITICAL` 非零，即 `IOLINK_CONCURRENCY` 不低于 `ISR`）下，
`iolink_device_init()` 会探测这两个钩子；如果仍在用 weak 默认实现，初始化返回
`IOLINK_DEVICE_INIT_ERR_CRITICAL`。实现必须可嵌套（`exit` 恢复对应 `enter` 保存的
屏蔽状态），并能在 ISR 双泵下保护库实际取锁的短操作：诊断条件表、事件队列的
读/clear、计数器清零、SIO 切换的应答槽清理与 ISR 相位复位
（见 `docs/CODE_FLOW.md` 第 12 节的临界区审计）。PD-in 发布不取锁：主上下文
用一次原子存储发布 `pd_pub`，数据 ISR 只加载一次。

`iolink_device_app_t::time_us` 是必需的。`iolink_device_init()` 在 `config.app`
为 NULL 时返回 `IOLINK_DEVICE_INIT_ERR_APP`，在 `app->time_us` 为 NULL 时返回
`IOLINK_DEVICE_INIT_ERR_TIME`，不会进入运行阶段。设置后，DLL、ISR 双泵和事件
引擎的所有时间戳都从该回调获取。

核心只通过应用提供的时间源取时，不调用平台时间函数；直接使用 `iolink_dll_*` 时，调用方必须在
`iolink_dll_init()` 后自行绑定 `ctx->shared.clock`（回调 + 实例指针，
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

static int  board_phy_init(iolink_app_t* self);
static void board_phy_set_mode(iolink_app_t* self, iolink_phy_mode_t mode);
static int  board_phy_rx_octet(iolink_app_t* self, uint8_t* data, uint8_t* status);
static int  board_phy_tx_octet(iolink_app_t* self, uint8_t data);
static void board_phy_tx_done(iolink_app_t* self);
static uint8_t board_phy_int_active(iolink_app_t* self);
static void board_phy_request_service(iolink_app_t* self);
static uint64_t board_time_us(iolink_app_t* self);

/* 应用自己的状态；库只看到 &g_app_state 这一个指针 */
struct iolink_app { uint8_t pd_in[2]; };
static struct iolink_app g_app_state;

static const iolink_phy_api_t g_phy = {
    .self = (iolink_app_t*) &g_app_state, /* 手写 vtable 在初始化器里写 self */
    .init = board_phy_init,               /* 最终 UART 速率 = IOLINK_COM_HZ（可在此或 set_mode(SDCI) 配置） */
    .set_mode = board_phy_set_mode,
    .rx_octet = board_phy_rx_octet,       /* RX BYTE mode */
    .tx_octet = board_phy_tx_octet,       /* TX BYTE mode */
    .tx_done = board_phy_tx_done,         /* TX BYTE mode only */
    .int_active = board_phy_int_active,
    .request_service = board_phy_request_service,
};

/* DS_UPLOAD_FLAG 落盘钩子；不提供则标志只保存在 RAM（见 7.1/7.3） */
static void app_ds_flag_persist(iolink_app_t* self, bool flag);

static const iolink_device_app_t g_app = {
    .self = (iolink_app_t*) &g_app_state,
    .time_us = board_time_us,              /* 必需；NULL 拒绝初始化 */
};

static const iolink_stack_config_t g_stack = {
    .m_seq_type = IOLINK_M_SEQ_TYPE_2_V,
    .pd_in_len = 2U,
    .pd_out_len = 2U,
    .od_width = 2U,
    .min_cycle_time = 20U,                /* 2.0 ms，0.1 ms 单位 */
};

static const iolink_device_info_t g_identity = {
    .vendor_name = "Acme Sensors",
    .vendor_text = "Acme Sensors GmbH",
    .product_name = "Acme IO-Link device",
    .product_id = "ACME-001",
    .serial_number = "0000000001",
    .hardware_revision = "1",
    .firmware_revision = "1.0.0",
    .vendor_id = 1234U,
    .device_id = 5678U,
    .min_cycle_time = 20U,
    .revision_id = IOLINK_REVISION_ID_1_1,
};

static const iolink_device_config_t g_config = {
    .phy = &g_phy,
    .stack = &g_stack,
    .app = &g_app,                        /* trait：实例指针 + 回调 */
    .device_info = &g_identity,           /* 必需；库没有默认身份 */
#if IOLINK_FEATURE_DATA_STORAGE
    .ds_flag_persist = app_ds_flag_persist,
#endif
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

`g_config`、`g_app` 必须在 `g_device` 的整个生命周期内保持有效。PHY 驱动状态
以及它们引用的所有缓冲区同样如此。

### 2.4 PHY 移植

vtable 的每个成员在类型上都是可选的（`IOLINK_NULLABLE`）：`iolink_caps_resolve()`
按"钩子是否存在"推导能力位，库只在对应能力位成立时调用。下表的"必需"指功能
可用性，不是类型约束；钩子存在但实现错误仍然是 bug，"缺失"只表示这个 PHY 不
提供该能力。

| 函数 | 是否必需 | 说明 |
| --- | --- | --- |
| `init` | 实际使用中必需 | 由 `iolink_device_init()` 调用一次；负返回值会中止初始化。允许为 NULL，但链路无法工作。 |
| `set_mode` | 实际使用中必需 | 切换 SDCI/SIO/INACTIVE。 |
| `rx_octet` | RX BYTE 模式 | 读到 octet 返回 1，无数据返回 0，出错返回负值。 |
| `rx_frame` | RX FRAME 模式 | 返回 PHY 持有的完整帧，在下一次调用前有效；不得返回两字节帧头或其他截断片段。 |
| `tx_octet` | TX BYTE 模式 | 送入一个应答 octet。 |
| `tx_frame` | TX FRAME 模式 | 发送完整应答，通常使用 DMA。 |
| `tx_done` | TX BYTE 模式 | 发完最后一字节后释放线路；RX BYTE 的 gap 也会用它释放接收引擎。TX FRAME 下该成员不存在，由 PHY 自己在传输完成中断里收尾。 |
| `tx_busy` | TX FRAME 可选 | 线路仍被占用时返回非 0；库据此推迟 ISR 应答，并由数据 ISR 在入口发现线路空闲时释放 PD-in 缓冲租约。为 NULL 时只能等下一次 capture 复用缓冲。 |
| `int_active` | ISR 泵需要 | 数据中断电平；`dll_isr_process()` 在它或 RX 能力缺失时直接返回，不泵送。 |
| `request_service` | ISR/SMP 档必需 | 主上下文暂存应答后唤醒数据 ISR。`ISR`/`SMP` 档下具备 TX 能力的 PHY 缺少它会被初始化拒绝；`SINGLE` 档没有数据 ISR，主上下文直接驱动 TX 钩子，此时它只在设置 `IOLINK_STACK_FLAG_REQUIRE_ISR_TX` 时必需。 |
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
`request_service()` 时，`iolink_device_init()` 返回 `IOLINK_DEVICE_INIT_ERR_PHY`。

`ISR`/`SMP` 档还要求所有具备 TX 能力的 PHY 提供 `request_service()`：主泵
只能暂存应答，发送必须由数据 ISR 完成，没有该钩子就没有任何路径能唤醒 ISR。
缺少它时 `iolink_device_init()` 同样返回 `IOLINK_DEVICE_INIT_ERR_PHY`。
只有 `SINGLE` 档没有数据 ISR，`dll_send()` 才在主上下文中直接驱动 TX 钩子
（该分支只在该档编译）。

## 3. 设备配置参考

### 3.1 `iolink_device_config_t`

| 字段 | 生命周期 / 契约 |
| --- | --- |
| `phy` | 指针，必须比上下文存活更久（通常取 `iolink_phy_*_get()`）。`phy->self` 由驱动在自己的 init 中写入。为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_NULL`。 |
| `stack` | 指针，必须比上下文存活更久；配置在初始化时读入上下文。为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_NULL`。 |
| `app` | 应用 trait（`iolink_device_app_t`）：实例指针 + 时钟/日志/生命周期回调的唯一来源。保留指针，必须比上下文存活更久。为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_APP`。 |
| `device_info` | 必填的身份来源，保留指针；为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_DEVICE_INFO`（库不提供默认身份）。其中的 `application_tag` 只是身份字段，标签索引由 OD 表提供。参见 8.1 节的身份限制。 |
| `ds_flag_persist` | 仅在 `IOLINK_FEATURE_DATA_STORAGE=1` 时声明；`DS_UPLOAD_FLAG` 变化时调用，`self` 为 `app->self`。为 NULL 时标志只保存在 RAM。 |
| `log_level` | 初始化时的日志阈值；`IOLINK_LOG_LEVEL_SILENT` 表示静默。日志回调在 `app->log_write`，初始化后不可更改。 |
| `od` | 可选的设备专用参数入口（`include/iolinki/od.h`）：const 表 + get/set 后端，ISDU 与 Data Storage 共用。 |
| `workspace` | 可选的传输 scratch 工作区（`include/iolinki/workspace.h`）。NULL 表示使用上下文内嵌 fallback；`IOLINK_WORKSPACE_EMBEDDED=0` 时必须提供，否则初始化返回 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`。 |

### 3.2 `iolink_stack_config_t`

| 字段 | 含义 |
| --- | --- |
| `m_seq_type` | 固定 M-sequence 类型，或用于可变 PD 的 `IOLINK_M_SEQ_TYPE_2_V`。 |
| `min_cycle_time` | 最小循环时间，单位 0.1 ms；在 DPP1 中按表 B.3 编码。 |
| `pd_in_len`, `pd_out_len` | 初始/声明的 PD 长度。 |
| `t_pd_us` | 上电后接受帧之前的延迟。 |
| `od_width` | OD octet 数（1/2/8/32），必须显式给出；0 返回 `IOLINK_DEVICE_INIT_ERR_OD_WIDTH`（库不按类型推导；需要 Table A.10 的默认值时用 `iolink_mseq_default_od_width()` 自己算）。 |
| `master_lost_cycles` | 判定 `master_lost` 前的丢失周期数；0 表示关闭该提示。 |
| `compat_flags` | `IOLINK_STACK_COMPAT_*` 兼容位，与构建期默认值按位或。 |
| `flags` | `IOLINK_STACK_FLAG_*`：`REQUIRE_ISR_TX`、`NO_SYSTEM_COMMAND`。 |
| `timing_enforce` | `IOLINK_STACK_TIMING_*`；`DEFAULT` 表示沿用构建档位默认值。 |
| `reserved` | 必须为 0。 |
| `event_min_appear_ms`, `event_min_repeat_ms` | 覆盖 A.6.4 的出现/重复窗口；0 表示沿用构建默认值。 |

传输速率（COM1/2/3）不在这个结构里：它是编译期 `IOLINK_COM`（`config.h`），
与 `IOLINK_COM_HZ`、位时间和 PHY 的 UART 配置一起折叠。设备只支持一个 COM
（V1.1.5 §5.3.3）。

初始化会用 `IOLINK_DEVICE_INIT_ERR_*` 拒绝非法组合（参见 `device.h`），包括
`phy`/`stack` 为 NULL（`IOLINK_DEVICE_INIT_ERR_NULL`）、`device_info` 为 NULL
（`IOLINK_DEVICE_INIT_ERR_DEVICE_INFO`）、`time_us` 为 NULL、平台临界区仍是 weak
默认实现、PD/OD 大小超过编译期缓冲区、固定布局不匹配、
`IOLINK_WORKSPACE_EMBEDDED=0` 但未提供工作区
（`IOLINK_DEVICE_INIT_ERR_WORKSPACE`）、应用对象字典表自检失败
（`IOLINK_DEVICE_INIT_ERR_OD`，见 4.6 节），以及能力位要求了 PHY 未提供的钩子
（`IOLINK_DEVICE_INIT_ERR_PHY`：`REQUIRE_ISR_TX` 缺少 `request_service()`；
`ISR`/`SMP` 档下具备 TX 能力却没有 `request_service()`；或 TX FRAME 构建设置
`IOLINK_PHY_FEATURE_TX_BUSY=1` 但 PHY 没有 `tx_busy()`）。
PHY `init` 失败也归到 `IOLINK_DEVICE_INIT_ERR_PHY`；驱动自己的错误码不会混进
这个枚举，需要用 `iolink_device_get_phy_error()` 读取（默认日志是静默的）。
该 getter 同时区分两种 `INIT_ERR_PHY`：非 0 表示驱动 `init` 真的失败了（重试
可能有意义），0 表示钩子/能力位这类永久配置错误（重试不会改变结果）。

`IOLINK_M_SEQ_TYPE_1_1` 属于旧版类型，初始化会以
`IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE` 拒绝。
`IOLINK_M_SEQ_TYPE_2_V` 是唯一允许 0…32 任意固定 PD 宽度的类型；宽度和所有
M-sequence 类型一样在初始化时确定，运行期不可改变。

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

节奏契约：主上下文至少每个 M-cycle 调用一次 `iolink_device_process()`；ISDU 写段和
计划命中但执行失败（例如线路忙）的帧只由它搬运。合法帧的即时应答完全由数据 ISR 负责：
**计划未命中即丢弃并计 `isr_plan_misses`**，主泵不再接手合法帧。因此应用可以长时间阻塞
（EEPROM 写入、固件升级等），M-sequence 应答不受影响：

- 主站收到的是 ISR 按计划表给出的应答，不依赖主泵节奏；
- 计划未命中的合法帧被丢弃（主站按自己的重试机制重发），不会产生迟到应答；
- ISDU 写请求在数据 ISR 内按 FlowCTRL 位置去重累计：重发段只回 ACK、不消耗新位置，
  因此主站重发风暴不会耗尽缓冲。写入口有**两个请求槽**：主泵解析一条已发布的请求时，
  ISR 可以继续把下一条累计进另一个槽。只有两个槽都不可用（一条已发布未被接管、另一条
  正在被解析）时，新 START 才覆盖那条已发布的请求并计入 `isr_od_overruns`——被覆盖的
  请求不会被执行。

排查主泵饥饿时读 `iolink_dll_get_stats()`：`isr_plan_misses`（合法帧计划未命中而被丢弃）、
`isr_mailbox_drops`（邮箱满）、`isr_od_overruns`（已收齐但未被接管的请求被新 START 覆盖）、
`t_pd_discards`（t_PD 窗口内丢弃的邮箱帧）、`reply_plan_overflows`（计划槽位耗尽）。
`isr_plan_misses` 持续增长说明计划表缺少某种合法帧形状；`isr_od_overruns`
持续增长说明主泵长时间未接管已收齐的请求（双槽只能吸收一条在途请求）。`isr_frames_consumed`（由 ISR 完全处理、不再进入邮箱的帧，含按规范有意不应答的转换/复位帧）
与 `isr_frames_handoff`（成功交给主泵的帧）给出两类终局的数量，可用来核对"ISR 接管 / 交给主泵"
的比例；被丢弃的帧见上列的丢弃计数（坏 ISDU 写计入 `isr_eager_rejects`）。

`iolink_device_init()` 使用 `config.workspace`（可为 NULL，表示使用上下文内嵌的
ISDU/DS scratch）；把 scratch 放进 DTCM 或外部 RAM 时在配置里指向工作区即可，工作区
必须比上下文存活更久。`iolink_device_get_workspace()` 返回当前绑定的工作区，使用
内嵌 fallback 时返回 NULL。`IOLINK_WORKSPACE_EMBEDDED=0` 时没有
fallback，必须提供工作区，否则初始化返回 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`。

### 4.2 过程数据

```c
iolink_device_pd_result_t iolink_device_pd_input_update(iolink_device_ctx_t* ctx,
                                                        const uint8_t* data, size_t len,
                                                        bool valid);
int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx,
                                 uint8_t* data, size_t len);
```

`pd_input_update()` 发布设备到主站的数据。`valid` 决定 CKS 中的 PD-invalid 位。
PD 长度是初始化属性（`iolink_stack_config_t::pd_in_len`），运行期不可改变：
`len` 必须等于配置的 PD-in 长度，否则返回 `IOLINK_DEVICE_PD_ERR_LEN`。
返回值是 `iolink_device_pd_result_t`：`IOLINK_DEVICE_PD_OK`、`_ERR_ARG`（ctx/data 为
NULL）、`_ERR_MODE`（回调路径已生效）、`_ERR_LEN`、`_ERR_BUSY`（所有 PD-in 缓冲区
都被 ISR 占用，并计入 `pd_in_skips`）。

> **规范的宽度约束（V1.1.5 Table B.1 / A.14）**：DPP1 0x05/0x06 是只读字段，
> `TYPE_2_V` 的 n/k 也是设备配置属性，由 DPP1 在启动阶段通告。因此 PD 宽度在
> 初始化时确定，库不提供运行期改宽接口；需要不同宽度时在 `iolink_stack_config_t`
> 里给出并重新初始化。

两条 PD-in 路径在同一构建内共存，按 `on_pd_input` 是否为 NULL 在初始化时选择：非 NULL
时置位 `IOLINK_CAP_PD_IN_CALLBACK`，DLL 每周期调用回调填充空闲缓冲区；为 NULL 时由
`iolink_device_pd_input_update()` 轮询发布。`iolink_device_init()` 不检查两条路径是否同时
提供，也不因混用而失败；回调路径生效后，轮询调用在运行期返回
`IOLINK_DEVICE_PD_ERR_MODE`，避免线上 PD 在回调输出与调用方数据之间跳变。
路径只由 `on_pd_input` 是否存在决定。

`pd_output_read()` 最多复制 `len` 字节，并返回实际复制的字节数；参数非法时返回负值。
`pd_out` 由同一主上下文中的帧分发写入，因此该函数直接复制，不取锁，也不进入平台临界区。

### 4.3 应用回调

通过 `config.app` 指向的 `iolink_device_app_t` 注册；没有单独注册函数。所有回调都在
主上下文运行，绝不在 ISR 中运行。

| 回调 | 调用时机 |
| --- | --- |
| `on_startup(self)` | 进入 STARTUP。 |
| `on_preoperate(self)`, `on_operate(self)` | 进入对应状态。 |
| `on_reset(self, type)` | SystemCommand 0x80/0x81 请求复位；`type` 为 `IOLINK_RESET_DEVICE` 或 `IOLINK_RESET_APPLICATION`。NULL = 不处理。 |
| `on_pd_input(self, data)` | 提供时（非 NULL），每个 OPERATE 周期调用；填写一个 ISR 未占用的 PD-in 缓冲区（长度 = 配置的 `pd_in_len`），返回值作为 PD-valid 标志。未提供时改用 `iolink_device_pd_input_update()`。 |
| `on_pd_output(self, data)` | 每个 OPERATE 周期调用，携带最新的主站输出数据（长度 = 配置的 `pd_out_len`）。 |
| `on_state_change(self, state)` | 当前 DLL 状态（电平式）：状态变化时调用一次，主上下文迁移立即投递，ISR 期间的多次迁移合并为最终状态；例外是 ISR 进入 STARTUP 的复位迁移，会先于最终状态单独投递。 |
| `on_master_lost(self)`, `on_master_online(self)` | 主站静默提示的边沿。 |

所有回调的 `self` 参数就是 `iolink_device_app_t::self`。

`on_pd_output` 收到的是指向上下文内部的指针，不能保留。

### 4.4 复位与状态查询

```c
iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx);
bool iolink_device_master_lost(const iolink_device_ctx_t* ctx);
iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx);
iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx);
iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx);
uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx);
uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx);
void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx,
                                 iolink_dll_stats_t* out_stats);
```

复位通过 `iolink_device_app_t::on_reset(self, type)` 投递，回调收到
`IOLINK_RESET_DEVICE`（System Command 0x80）或 `IOLINK_RESET_APPLICATION`（0x81）。
本栈不会自行复位 MCU。`master_lost` 只是给应用的提示，不会改变 DLL 状态。

`iolink_device_get_dll_stats()` 复制一份 `iolink_dll_stats_t` 快照。字段清单、类型与
含义以 [API.md 的「DLL 统计（裸 DLL 集成）」](API.md) 为唯一出处，本文件不重复；
§4.1 中的计数只说明它们与 ISR 节流策略的对应关系。

### 4.5 能力位

```c
iolink_caps_t iolink_device_capabilities(const iolink_device_ctx_t* ctx);

const iolink_build_caps_t* iolink_caps_build(void);
iolink_caps_t iolink_caps_resolve(const iolink_phy_api_t* phy, const iolink_stack_config_t* stack,
                                  const iolink_device_app_t* app,
                                  const iolink_build_caps_t* build);
bool iolink_caps_has(iolink_caps_t caps, iolink_caps_t bit);
bool iolink_caps_built(const iolink_build_caps_t* caps, uint32_t mask);
```

应用通常只需要读：`iolink_device_capabilities()` 返回本实例的运行时能力位
（`IOLINK_CAP_*`），`iolink_caps_build()` 返回只读的构建档位（`IOLINK_CAPB_*`、
ISR 粒度、并发/统计档位、PD-in 缓冲数）。实例能力在 `iolink_device_init()` 内由
`iolink_caps_resolve()` 一次性解析，因此运行期不需要再逐项判断 `phy->xxx != NULL`。
能力位**只在初始化时定义**：没有运行期覆盖接口，需要不同开关时在
`iolink_stack_config_t` 的 `compat_flags` / `flags` 里给出并重新初始化——应答计划表
以能力位为输入，初始化后不再重编译。四个 DeviceAccessLocks 位是例外：本库实现了
全部四个，`isdu_locks_implemented()` 恒为全部四位，没有配置项可以裁剪。

应用需要做什么：按端口实际能力设置 `stack.flags` / `stack.compat_flags`，而不是
依赖已降级为兼容宏的编译期开关；如果端口要求“所有应答由 ISR 发送”，确保 PHY 提供
`request_service()`，否则初始化返回 `IOLINK_DEVICE_INIT_ERR_PHY`。

### 4.6 对象字典

```c
typedef struct {
    const iolink_od_entry_t* entries;
    size_t count;
} iolink_od_app_t;

typedef struct {
    const iolink_od_app_t* app;
    iolink_app_t* self; /* 传给钩子与 HANDLER 后端；未设置时钩子收到 NULL */
} iolink_od_ctx_t;
```

参数只在对象字典里描述一次（索引/子索引、datatype codec、访问权限、写语义、
storage 标志、默认值与可选钩子），ISDU 分发、Data Storage 成员判定和 IODD 导出都从
这张表派生。标准对象（`0x0000-0x003F` 中除三个标签外的对象，以及厂商错误统计
`0x0040`）由内建 const 表提供；应用通过 `config.od` 绑定自己的 const 表。

应用需要做的是：用 `IOLINK_OD_SCALAR_STORAGE[_EX]` / `IOLINK_OD_BYTES_STORAGE[_EX]`
绑定应用变量，或用 `IOLINK_OD_SCALAR_HANDLER[_EX]` / `IOLINK_OD_BYTES_HANDLER[_EX]`
提供逐表项 `read`/`write` 函数（计算型对象）。表项只保存静态知识，运行期取值由
表项自己的后端完成，`iolink_od_ctx_t::self` 只传给钩子与 HANDLER 函数。索引归属按域决定：

- 规范固定索引（`< 0x0040`）由标准表提供、不可被应用表覆盖；唯一例外是标签
  `0x0018-0x001A`，它们的数据归应用（见 7.2 节）。
- 设备专用索引（`0x0040-0x00FE`、`0x0100+`）应用表优先，未声明时回落到内建标准表项
  （`0x0040` 错误统计），仍未命中返回 `0x80 0x11`。
- `iolink_od_find()` 实现同一规则，因此应用拥有的标签索引在应用表命中时优先于标准表。

`storage` 只有两个取值：`IOLINK_OD_STORAGE_VOLATILE`（不进 DS 集合）和
`IOLINK_OD_STORAGE_DS`（进入 DS 备份集）。库没有 NVM，写入成功后的落盘由表项的
`on_change` 钩子通知应用，格式与时机由应用决定（见 7.1 节）。

**初始化自检**：`IOLINK_FEATURE_VALIDATE`（默认 1）打开时，`iolink_device_init()`
在绑定子系统之前走一遍应用表，拒绝应用表项复用内建 handler、datatype/access/
semantics/backend 非法、storage 含未知位、`TRIGGER` 带 storage、可写字节串无
`block_len`、只读项带写钩子、范围/默认值矛盾、STORAGE/HANDLER 后端缺指针、重复
index/subindex，以及应用表占用规范索引（标签除外）。命中返回
`IOLINK_DEVICE_INIT_ERR_OD`（-13）。表项宏只做常量检查；协议期数据校验不受该开关影响。

完整示例见 `examples/switching_sensor/switching_sensor_od.c`。

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

写入口（`iolink_event_trigger`、`iolink_event_clear`、`iolink_events_process`、
`iolink_events_reset`、`iolink_events_memory_write`、`iolink_events_pop`、
`iolink_events_clear_diagnosis`）是主上下文专属：事件引擎只有一个写者，主上下文在
`in_isr` 置位时会被拒绝。只读入口（`iolink_events_pending`、`iolink_events_flag`、
`iolink_events_memory_read`、`iolink_events_peek`、`iolink_events_get_all`、
`iolink_events_get_dropped_count`）可从任一侧读取：ISR 看到的是双映像 + 发布索引
保证的完整一代。ISR 侧的写只有两处：`iolink_events_reset_request()`（登记复位，
由 `iolink_dll_process()` 应用）与 `iolink_diag_pulse_isr()` 的单字节 one-shot。

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

## 6. ISDU、标准索引与应用 OD 参数

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
| `0x0018-0x001A` | 应用/功能/位置标签 | 由应用 OD 表以字节串表项提供；未声明时索引不可用 |
| `0x001B` | ProductURI | 读；未配置时为空 |
| `0x0020` | ErrorCount | 读 |
| `0x0024` | DeviceStatus | 读 |
| `0x0025` | DetailedDeviceStatus | 读；取决于事件特性 |
| `0x0028`, `0x0029` | ProcessDataInput/Output | 读 |
| `0x0040-0x00FE`, `0x0100+` | 设备/厂商特定 | 应用 OD 表优先，未声明时回落到内建标准表项 |

分发顺序（`src/isdu.c` 的 `handle_standard_commands()`）：先做 DS 访问权限检查，然后按索引域分流。

- 规范索引（`< 0x0040`）：内建标准表优先，应用表只在内建表未命中时才生效，因此
  应用表无法遮蔽 `0x0002`/`0x0024` 等对象。标签 `0x0018-0x001A` 是唯一例外：
  应用表命中时优先，标准表中没有这三个对象。
- 设备专用索引（`0x0040-0x00FE`、`0x0100+`）：应用 OD 表 `iolink_od_find_app()` 优先；
  未声明时回落到内建标准表（`0x0040` 错误统计），最后返回 `0x80 0x11`。因此
  `0x0040` 内置错误统计对象只有在应用表未接管时才可达。

`0x0004-0x000B` 和 `0x001C-0x001F` 为保留区间，`0x00FF` 也是保留值。标准表没有这些
对象；开启 `IOLINK_FEATURE_VALIDATE` 时应用表也不能声明 `< 0x0040` 的索引（标签
除外），因此它们返回 `0x80 0x11`。VendorID/DeviceID **不是** ISDU 对象；
它们位于 DPP1 的 `0x07-0x0B` 字节。

字符串应答按对象限制长度（通常为 64 字节，SerialNumber 为 16，ProductURI 为 100）。
未知索引返回 `0x80 0x11`（索引不可用）；对已知只读对象的写请求返回
`0x80 0x23`。

当 `IOLINK_FEATURE_DATA_STORAGE=0` 时，索引 `0x0003` 返回 `0x80 0x11`。
当 `IOLINK_FEATURE_EVENTS=0` 时，索引 `0x0025` 返回 `0x80 0x11`。

### 6.2 底层 ISDU 传输

以下接口用于测试和自定义传输；正常应用应使用设备层：

```c
void iolink_isdu_init(iolink_isdu_ctx_t* ctx);
void iolink_isdu_bind_workspace(iolink_isdu_ctx_t* ctx, iolink_workspace_t* ws);
void iolink_isdu_bind_od(iolink_isdu_ctx_t* ctx, const iolink_od_app_t* app,
                         iolink_app_t* self);
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
就是 ISR 安全的。`iolink_isdu_od_write()` 是数据 ISR 的写入口：只按 FlowCTRL 位置
去重累计，不校验 CHKPDU、不解析、不执行；收齐后把 `{len, sel, gen}` 发布到上下文内
的发布字，主泵 `iolink_isdu_process()` 在**锁外**认领该槽并完成 CHKPDU 校验、解析与
执行（两个请求槽因此允许下一条请求在主泵执行期间继续累计）。直接使用引擎的测试可以
在同一上下文依次调用两者，其余函数均在主上下文调用。

## 7. 参数、Data Storage 与持久化

### 7.1 应用持久化契约

库不拥有 NVM：没有存储后端接口、没有区域布局、没有 magic/version，也没有保存/加载
函数。参数只在对象字典里描述一次，写入成功后由表项钩子通知应用；编码格式、存储介质
和写入时机都由应用决定。完整约定与示例见 [STORAGE.md](STORAGE.md)。

写路径（`iolink_od_write_value()`）固定为：线长/范围/`validate` → 语义动作 →
`on_change`。返回值使用 `iolink_result_t`（`include/iolinki/result.h`）：

- `code == 0`：成功，`len` 为读取的字节数（写操作为 0）；
- `code < 0`：内部错误，不进入线上协议（`IOLINK_RESULT_NOT_HANDLED` /
  `INVALID` / `IO`）；
- `code > 0`：ISDU ErrorCode（如 0x33 值长度超长），由 ISDU 应答边界原样上送。

应用需要做的是：

1. 给需要落盘的参数加 `on_change`。`IOLINK_OD_WRITE_STORE` 的写入只改应用变量
   （`STORAGE` 后端零 shadow）或调用 `HANDLER` 的 `write`；钩子返回成功才算写入完成，
   标量返回错误时会回滚到写前值，OctetString 不回滚（需要拒绝时用 `validate`）。
2. 在钩子里把已接受的 `data`/`len` 编码到自己的 NVM，或在钩子里排队延迟写入。
   `on_change` 只看到表项，不接收请求的子索引；需要区分数组元素时按
   `entry->subindex`/`max_subindex` 判断。
3. 在启动时用自己的持久化内容初始化参数变量（`iolink_device_init()` 之前或之后均可），
   再调用 `iolink_ds_refresh()` 让 `Data_Storage_Size`/`Parameter_Checksum` 反映恢复后
   的参数集。库不会读取 NVM，也不会在启动时恢复任何参数。
4. Data Storage 下载会逐条通过对象字典写回，因此每条记录都会触发对应表项的
   `on_change`。库不提供跨记录事务；需要原子性的应用应在钩子内合并或延迟落盘。
5. 出厂复位：`iolink_ds_factory_reset()` 只把 DS 集合中的应用表项恢复为表项默认值
   （`iolink_od_restore_value()`，不执行钩子），不写 NVM，也不处理非 DS 参数；应用
   负责复位其余参数并自行决定默认值是否落盘。

### 7.2 标签由应用提供

`0x0018` Application Tag、`0x0019` Function Tag、`0x001A` Location Tag 不在内建标准
表中，由应用用 `IOLINK_OD_BYTES_STORAGE[_EX]` 作为普通字节串参数提供：

```c
static char g_application_tag[32];
static uint8_t g_application_tag_len = 0U;

static const iolink_od_entry_t g_tag_entries[] = {
    IOLINK_OD_BYTES_STORAGE(0x0018, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
                            IOLINK_OD_STORAGE_DS, 32U, "ApplicationTag",
                            g_application_tag, &g_application_tag_len),
};
```

- `iolink_od_find()` 对这三个索引让应用表优先；应用未声明时索引返回
  `0x80 0x11`。
- `block_len` 同时是长度上限和 IODD 的 `fixedLength`；`str_len` 是读取时的权威
  长度，库只复制原始字节，不添加 NUL。
- 声明 `IOLINK_OD_STORAGE_DS` 后标签进入 DS 备份集；持久化仍由 `on_change` 通知
  应用完成（见 7.1 节）。
- `device_info.application_tag` 只是身份结构里的字段，不参与标签的 ISDU 读写。

### 7.3 Data Storage

```c
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);

void iolink_ds_init(iolink_ds_ctx_t* ctx);
void iolink_ds_bind_workspace(iolink_ds_ctx_t* ctx, iolink_workspace_t* ws);
void iolink_ds_bind_sources(iolink_ds_ctx_t* ctx, const iolink_od_app_t* app,
                            iolink_app_t* self);
void iolink_ds_process(iolink_ds_ctx_t* ctx);
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);
int  iolink_ds_abort(iolink_ds_ctx_t* ctx);
int  iolink_ds_handle_param_command(iolink_ds_ctx_t* ctx, uint8_t cmd,
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
int  iolink_ds_factory_reset(iolink_ds_ctx_t* ctx);
bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx);
uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx);
uint32_t iolink_ds_size(const iolink_ds_ctx_t* ctx);
uint32_t iolink_ds_checksum(const iolink_ds_ctx_t* ctx);
size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx);
const iolink_od_entry_t* iolink_ds_param_entry(const iolink_ds_ctx_t* ctx, size_t i);
void iolink_ds_request_upload(iolink_ds_ctx_t* ctx);
void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx);
```

当 `IOLINK_FEATURE_DATA_STORAGE=1` 时，`iolink_device_init()` 初始化 DS 上下文、绑定
`config.workspace`、绑定 `config.od` 并把 `config.ds_flag_persist` 接到 DS 标志钩子，
然后按当前参数集刷新 `Data_Storage_Size`/`Parameter_Checksum`。DS 上下文是纯 RAM
状态：`iolink_ds_init()` 只有一个参数，直接使用 `iolink_dll_*` 时也不需要提供任何
存储后端。

DS 是对象字典的投影：参数集合就是标准表与应用表中 `storage` 含
`IOLINK_OD_STORAGE_DS` 的表项，顺序固定为标准表在前、应用表按表序（设备层自动调用
`iolink_ds_bind_sources(ctx, config.od, config.app->self)`）。值全部来自表项后端，DS 上下文不保存参数
副本，也不写 NVM。

- **上传**：`iolink_ds_serialize()` 按记录流式序列化当前参数集，可直接写入 ISDU
  应答缓冲；上传内容与 `Parameter_Checksum` 都从当前值实时生成，不存在独立保存的
  DS 镜像。
- **下载**：块下载期间写请求由 `iolink_ds_stage_param()` 暂存；`ParamDownloadEnd`
  先对整份镜像做无副作用预校验（记录结构、线长、范围、表项 `validate`），全部通过
  后才逐条通过 `iolink_od_write_value()` 写回，每条记录触发该表项的 `on_change`。
  预校验失败时不会写入任何参数；逐条写回阶段仍可能失败，此时前缀记录已经生效，库
  不提供跨参数事务。未知索引会被跳过，以便替换设备缺少已备份参数时仍能恢复。
- **`DS_UPLOAD_FLAG`**：只保存在 RAM。`iolink_ds_request_upload()` 置位并调用
  `flag_persist(user, true)`，上传/下载序列结束或 `iolink_ds_clear_upload()` 清除时
  调用 `flag_persist(user, false)`。钩子为 NULL 表示仅 RAM。应用保存该标志，启动时
  若保存值为 true 则调用 `iolink_ds_request_upload()` 重新置位（该调用会再次触发
  一次钩子），使主站观察到待上传请求。
- **出厂复位**：`iolink_ds_factory_reset()` 只恢复 DS 集合中的应用表项，不写 NVM。

DS 映像不是常驻状态：上下文只持有指向暂存缓冲的指针（
`iolink_workspace_t::ds_stage`，或内嵌 fallback）。上传和 `Parameter_Checksum` 读取
走流式路径 `iolink_ds_serialize()`（`out == NULL` 时只统计长度与校验和）；
`iolink_ds_refresh()` 只刷新 `Data_Storage_Size`/`Parameter_Checksum` 而不物化镜像。
完整暂存缓冲只用于下载和 `iolink_ds_build_image()`。

序列化记录格式为：

```
[Index:2 大端][Subindex:1][Length:1][Data:Length]
```

`IOLINK_DS_IMAGE_MAX`（默认 256）限制一次序列化的大小；长度上限取表项的 `block_len`。
`iolink_device_process()` 会在 ISDU 空闲时调用 `iolink_ds_process()`；
`iolink_ds_downloading()` 暴露下载暂存状态。`DS_UPLOAD_FLAG`/`State_Property` 以及
`DS_UPLOAD_REQ` 通知用于告知主站：本地参数已变化，应进行备份。

## 8. 当前限制与注意事项

### 8.1 设备身份按上下文绑定

每个上下文的 API 为：

```c
int iolink_device_info_ctx_init(iolink_device_info_ctx_t* ctx,
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

- `config.device_info` 提供静态身份，`iolink_device_init()` 把它绑定到
  `ctx->device_info`；`0x0010+` 与 DPP1 从该上下文读取。**没有默认身份**：
  `config.device_info` 为 NULL 时初始化返回 `IOLINK_DEVICE_INIT_ERR_DEVICE_INFO`，
  `iolink_device_info_ctx_init()` 也拒绝 NULL 并返回负值。
- 运行时访问锁用 `iolink_device_info_ctx_set_access_locks()` / `_get_access_locks()`
  操作 `ctx->device_info`；ISDU 的参数锁检查读取同一个上下文。
- 可写参数（含三个标签）通过 `config.od` 的应用对象字典访问，ISDU 在初始化时绑定
  该表；库没有独立的参数上下文。
- 同一进程中的多个设备上下文各自独立，互不共享身份、访问锁或参数。

`iolink_device_info_ctx_set_application_tag()` 只更新身份上下文中的
`application_tag` 字段；`0x0018` 的读写由 OD 表项决定，两者互不联动。

### 8.2 `time_us` 与平台时间源

`iolink_device_app_t::time_us` 在 `iolink_device_init()` 中绑定到 DLL 和事件
上下文。该回调为必需项：`config.app` 为 NULL 返回 `IOLINK_DEVICE_INIT_ERR_APP`，
`app->time_us` 为 NULL 返回 `IOLINK_DEVICE_INIT_ERR_TIME`。绑定后，主循环、
数据 ISR 和事件引擎都使用该回调，核心不再调用平台时间函数。直接使用
`iolink_dll_*` 时，调用方需在初始化后自行绑定 `ctx->shared.clock`；事件引擎的
`ctx->clock` 可指向同一个实例（设备层就是这么做的），也可以指向独立时钟。
时间源必须在上下文生命周期内保持单调。

### 8.3 上下文绑定

`iolink_device_init()` 会把 `config.device_info` 和 `config.od` 绑定到 ISDU；
直接使用底层 `iolink_isdu_*` API 时，调用方需要自行设置 `isdu.device_info`、
`isdu.od`、`isdu.ds_ctx` 和 `isdu.direct_param_page2`。未绑定时的行为：身份索引
（`0x000D` 与 `0x0010-0x001B`）返回 `IDX_NOT_AVAIL`；DPP1 与 DPP2 返回
`IDX_NOT_AVAIL`（不会退回臆造的页内容）；应用参数与标签按空表处理（索引返回
`0x80 0x11`）；访问锁按空上下文处理（读为 0，写被忽略）；`0x0020`/`0x0024`/`0x0028`/
`0x0029` 不依赖身份，仍然可用。

### 8.4 回调与缓冲区生命周期

除非契约明确说明，回调不得保留指向协议栈内部缓冲区的指针（`rx_frame` 的有效期到下一次
调用；OD 表数组和 `config` 必须保持存活）。应用回调和 OD 后端在主上下文
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
| `IOLINK_FEATURE_VALIDATE` | 1 | 关闭初始化期配置自检；不再校验应用 OD 表（协议期数据校验仍生效）。 |
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

队列与暂存深度宏同样在 `config.h` 中用 `#ifndef` 保护，按需调整：

| 宏 | 默认值 | 约束与溢出的可观测计数 |
| --- | --- | --- |
| `IOLINK_MBX_SLOTS` | 4 | 帧邮箱深度；2 的幂且 ≥2。满时丢帧并计 `isr_mailbox_drops`。 |
| `IOLINK_REPLY_PLAN_SLOTS` | 48 | 应答计划槽位上限（4 个状态切片共用）。满时丢弃多余计划并计 `reply_plan_overflows`。 |

调小深度会省 RAM，但会增加对应计数器的丢弃；`IOLINK_MBX_SLOTS` 用掩码取模，取非
2 的幂会在编译期报错。ISDU 写请求复用现有请求缓冲（`IOLINK_ISDU_BUFFER_SIZE`），
没有独立的段数上限。

传输粒度由 `IOLINK_ISR_RX_MODE` 和 `IOLINK_ISR_TX_MODE` 选择（默认 `BYTE`，
DMA/整帧 PHY 使用 `FRAME`）。PHY 源文件可以用 `_Static_assert` 检查所需的模式组合。

## 10. 集成检查清单

1. 在 `config.app->time_us` 中提供单调、ISR 安全的微秒时钟，并实现可嵌套的
   `iolink_critical_enter/exit()`；初始化会拒绝 weak 默认临界区。
2. 为选定的 BYTE/FRAME 模式实现 PHY 回调，包括 `int_active` 和
   `request_service`。
3. 保证 `config`、PHY 状态和所有回调数据在上下文生命周期内有效。
4. 在 `config.device_info` 中提供静态身份；它是必填项，缺失时初始化失败（库不提供默认身份）。
5. 调用 `iolink_device_init()`，然后至少每个 M-cycle 调用一次
   `iolink_device_process()`，并从数据中断调用 `iolink_device_process_isr()`。
6. 选择一种 PD-in 来源：提供 `on_pd_input` 回调，或调用
   `iolink_device_pd_input_update()`。`iolink_device_init()` 不检查是否混用；回调路径
   一旦生效，轮询调用在运行期返回 `IOLINK_DEVICE_PD_ERR_MODE`。
7. 为需要跨复位保留的参数加 `on_change`，由应用编码并写入自己的 NVM；启动时把
   保存的值恢复进应用变量并调用 `iolink_ds_refresh()`。`IOLINK_WORKSPACE_EMBEDDED=0`
   时还要提供 `iolink_workspace_t`。
8. 在 `IOLINK_FEATURE_DATA_STORAGE=1` 时按需提供 `config.ds_flag_persist`，并自己
   保存/恢复 `DS_UPLOAD_FLAG`（为 NULL 表示仅 RAM）。
9. 通过 `config.od`（一行表项 + 后端）添加设备专用参数；需要标签时用
   `IOLINK_OD_BYTES_STORAGE[_EX]` 声明 `0x0018-0x001A`。
10. 使用真实主站验证时序、唤醒/SIO 行为和故障映射。

## 11. 其他公开头文件与适配器

正常应用使用 `device.h` 及上文列出的特性头文件。下表是 `include/iolinki/` 公开头文件的
函数清单，由 `tools/gen_docs.py` 从各头文件生成，不手改；签名与参数语义以头文件注释为准，
应用层 API 的精简参考见 [API.md](API.md)。本文件只保留集成所需的接线说明。

<!-- BEGIN GENERATED: public_api (tools/gen_docs.py) -->
| 头文件 | 公开函数 | 说明 |
| --- | --- | --- |
| `include/iolinki/device.h` | `iolink_device_init` | 按配置初始化设备上下文 |
| `include/iolinki/device.h` | `iolink_device_get_workspace` | 返回设备当前绑定的工作区 |
| `include/iolinki/device.h` | `iolink_device_get_phy_error` | 返回最近一次 `iolink_device_init()` 中 PHY `init` 回调的原始错误码 |
| `include/iolinki/device.h` | `iolink_device_process` | 运行设备状态机的一次迭代（主循环） |
| `include/iolinki/device.h` | `iolink_device_process_isr` | 双泵传输的 ISR 侧泵 |
| `include/iolinki/device.h` | `iolink_device_pd_input_update` | 更新输入过程数据（设备 -> 主站）；回调路径生效时返回 `IOLINK_DEVICE_PD_ERR_MODE` |
| `include/iolinki/device.h` | `iolink_device_capabilities` | 取本实例的能力位 |
| `include/iolinki/device.h` | `iolink_device_pd_output_read` | 读取最新的输出过程数据（主站 -> 设备） |
| `include/iolinki/device.h` | `iolink_device_get_events_ctx` | 获取事件处理上下文 |
| `include/iolinki/device.h` | `iolink_device_get_ds_ctx` | 获取数据存储上下文 |
| `include/iolinki/device.h` | `iolink_device_get_state` | 获取当前 DLL 状态 |
| `include/iolinki/device.h` | `iolink_device_master_lost` | 主站是否已静默达到配置的周期数 |
| `include/iolinki/device.h` | `iolink_device_get_phy_mode` | 获取当前 PHY 通信模式 |
| `include/iolinki/device.h` | `iolink_device_get_baudrate` | 获取当前通信波特率 |
| `include/iolinki/device.h` | `iolink_device_get_dll_stats` | 复制 DLL 统计计数器 |
| `include/iolinki/device.h` | `iolink_device_get_m_seq_type` | 获取协商后的 M-sequence 类型 |
| `include/iolinki/device.h` | `iolink_device_get_pd_in_len` | 获取输入过程数据长度 |
| `include/iolinki/device.h` | `iolink_device_get_pd_out_len` | 获取输出过程数据长度 |
| `include/iolinki/application.h` | — | 应用 trait 与回调（无独立函数） |
| `include/iolinki/device_info.h` | `iolink_device_info_ctx_init` | 初始化设备信息上下文 |
| `include/iolinki/device_info.h` | `iolink_device_info_ctx_get` | 返回上下文绑定的静态身份指针 |
| `include/iolinki/device_info.h` | `iolink_device_info_ctx_set_application_tag` | 在上下文上设置运行时应用标签（Index `0x0018`） |
| `include/iolinki/device_info.h` | `iolink_device_info_ctx_get_access_locks` | 从上下文获取设备访问锁（Index `0x000C`） |
| `include/iolinki/device_info.h` | `iolink_device_info_ctx_set_access_locks` | 在上下文上设置设备访问锁（Index `0x000C`） |
| `include/iolinki/dll.h` | `iolink_dll_timer_contract` | 取时序契约表（只读，进程生命周期内稳定） |
| `include/iolinki/dll.h` | `iolink_dll_timer_deadline` | 查询一个 timer 的绝对截止时间（0 = 未装载） |
| `include/iolinki/dll.h` | `iolink_dll_timer_armed` | 查询一个 timer 是否已装载 |
| `include/iolinki/dll.h` | `iolink_dll_init` | 初始化 DLL 主/ISR 上下文并绑定 PHY |
| `include/iolinki/dll.h` | `iolink_dll_process` | 处理字节级数据并执行状态机迁移 |
| `include/iolinki/dll.h` | `iolink_dll_publish_reply_plans` | 编译并发布应答计划表（主上下文调用，数据 ISR 静止） |
| `include/iolinki/dll.h` | `iolink_dll_expected_req_len` | MC 字节为主站命令字节时的期望请求长度 |
| `include/iolinki/dll.h` | `iolink_dll_process_isr` | 面向原始字节（单字节收发器）PHY 的 ISR 侧泵 |
| `include/iolinki/dll.h` | `iolink_dll_flush_state_callbacks` | 把当前 DLL 状态交付给状态变更钩子 |
| `include/iolinki/dll.h` | `iolink_dll_on_rx_timeout` | Timer A（t_A 单次定时）在无应答的情况下到期 |
| `include/iolinki/dll.h` | `iolink_dll_on_stack_tick` | Timer B 节拍：授权一个主上下文服务窗口 |
| `include/iolinki/dll.h` | `iolink_dll_take_service_window` | 消费 Timer B 的授权（1 = 用户代码可以运行） |
| `include/iolinki/dll.h` | `iolink_dll_get_pd_length` | 获取当前 PD 长度（初始化时固定） |
| `include/iolinki/dll.h` | `iolink_dll_get_phy_mode` | 获取当前工作模式 |
| `include/iolinki/dll.h` | `iolink_dll_request_back_to_box` | 请求执行 Back-to-box 序列（10.7.5） |
| `include/iolinki/dll.h` | `iolink_dll_get_baudrate` | 获取当前协商后的波特率（编译期 `IOLINK_COM`） |
| `include/iolinki/dll.h` | `iolink_dll_compute_min_cycle_time` | 按 M 序列定时计算 MinCycleTime（0.1 ms） |
| `include/iolinki/dll.h` | `iolink_dll_get_stats` | 获取 DLL 统计 |
| `include/iolinki/isdu.h` | `iolink_isdu_init` | 初始化 ISDU 引擎 |
| `include/iolinki/isdu.h` | `iolink_isdu_bind_workspace` | 绑定外部工作区 |
| `include/iolinki/isdu.h` | `iolink_isdu_bind_od` | 绑定应用对象字典 |
| `include/iolinki/isdu.h` | `iolink_isdu_bind_caps` | 绑定实例能力位 |
| `include/iolinki/isdu.h` | `iolink_isdu_process` | 处理 ISDU 引擎逻辑（主上下文） |
| `include/iolinki/isdu.h` | `iolink_isdu_execute_system_command` | 执行通过 Direct Parameter page 1（`0x0F`）提供的 SystemCommand |
| `include/iolinki/isdu.h` | `iolink_isdu_od_write` | 数据 ISR 的 ISDU 写入口：累计请求字节并发布收齐的请求 |
| `include/iolinki/isdu.h` | `iolink_isdu_od_read` | 在 ISDU 通道（C3）上生成设备应答的 OD 八位组 |
| `include/iolinki/isdu.h` | `iolink_isdu_snapshot_response` | 为双泵 ISR 传输快照当前读应答 |
| `include/iolinki/isdu.h` | `iolink_isdu_is_idle` | ISDU 传输是否空闲 |
| `include/iolinki/isdu.h` | `iolink_isdu_get_response_byte` | 取应用层 ISDU 应答载荷的下一个八位组 |
| `include/iolinki/isdu.h` | `iolink_isdu_direct_param_page1_octet` | 读取单个 Direct Parameter page 1 八位组 |
| `include/iolinki/isdu.h` | `iolink_isdu_dp_page1_read_range` | 读取 Direct Parameter page 1 的一段字节（单次 seqlock 窗口） |
| `include/iolinki/od.h` | `iolink_od_codec` | 取 datatype 的 codec 描述 |
| `include/iolinki/od.h` | `iolink_od_check_value` | 校验待写入值：线长、长度上限、范围与 validate 钩子 |
| `include/iolinki/od.h` | `iolink_od_decode` | 把标量线上字节解码为原生值 |
| `include/iolinki/od.h` | `iolink_od_encode` | 把标量原生值编码为线上字节（大端） |
| `include/iolinki/od.h` | `iolink_od_default` | 读取表项默认值的线上字节 |
| `include/iolinki/od.h` | `iolink_od_count` | 协议栈标准 OD 表项数量 |
| `include/iolinki/od.h` | `iolink_od_at` | 取第 i 个标准表项 |
| `include/iolinki/od.h` | `iolink_od_find` | 按索引查表项：规范索引走标准表，设备专用索引先查应用表 |
| `include/iolinki/od.h` | `iolink_od_find_app` | 只在应用表中按索引/子索引查找 |
| `include/iolinki/od.h` | `iolink_od_ds_count` | 标准表中 storage 为 DS 的表项数量 |
| `include/iolinki/od.h` | `iolink_od_ds_at` | 取第 i 个标准 DS 表项 |
| `include/iolinki/od.h` | `iolink_od_ds_count_app` | 应用表中 storage 为 DS 的表项数量 |
| `include/iolinki/od.h` | `iolink_od_ds_at_app` | 取应用表中第 i 个 DS 表项 |
| `include/iolinki/od.h` | `iolink_od_is_ds` | index/subindex 是否属于 DS 参数集（标准表优先，其次应用表） |
| `include/iolinki/od.h` | `iolink_od_read_value` | 通过表项读取一个值 |
| `include/iolinki/od.h` | `iolink_od_write_value` | 通过表项写入一个值 |
| `include/iolinki/od.h` | `iolink_od_restore_value` | 恢复一个值，不执行 validate/on_change/持久化 |
| `include/iolinki/data_storage.h` | `iolink_ds_init` | 初始化 DS 上下文（纯 RAM 状态，无存储后端） |
| `include/iolinki/data_storage.h` | `iolink_ds_bind_workspace` | 绑定外部工作区 |
| `include/iolinki/data_storage.h` | `iolink_ds_serialize` | 流式序列化当前 DS 参数集 |
| `include/iolinki/data_storage.h` | `iolink_ds_refresh` | 重新计算当前参数集的长度与校验和（不保存镜像） |
| `include/iolinki/data_storage.h` | `iolink_ds_bind_sources` | 绑定 DS 的参数来源 |
| `include/iolinki/data_storage.h` | `iolink_ds_calc_checksum` | 计算参数块的标准 32 位校验和（Fletcher-32） |
| `include/iolinki/data_storage.h` | `iolink_ds_process` | 处理 DS 状态转换与后台同步任务（ISDU 空闲时由设备层调用） |
| `include/iolinki/data_storage.h` | `iolink_ds_check` | 触发与主站的 DS 一致性检查 |
| `include/iolinki/data_storage.h` | `iolink_ds_abort` | 中止任何进行中的 DS 上传/下载并返回空闲状态 |
| `include/iolinki/data_storage.h` | `iolink_ds_handle_param_command` | 处理 Table B.9 SystemCommand 的 Param* 编码 |
| `include/iolinki/data_storage.h` | `iolink_ds_handle_ds_command` | 处理 DataStorageIndex DS_Command（Table B.10 subindex 01） |
| `include/iolinki/data_storage.h` | `iolink_ds_downloading` | 当前是否有块参数下载正在缓冲写入 |
| `include/iolinki/data_storage.h` | `iolink_ds_stage_param` | 将参数写入暂存到下载缓冲区（10.3.2） |
| `include/iolinki/data_storage.h` | `iolink_ds_build_image` | 将设备中由 DS 支持的参数序列化到映像缓冲区 |
| `include/iolinki/data_storage.h` | `iolink_ds_apply_image` | 应用从主站接收的参数映像（恢复） |
| `include/iolinki/data_storage.h` | `iolink_ds_get_image` | 返回当前序列化映像指针（按需构建） |
| `include/iolinki/data_storage.h` | `iolink_ds_factory_reset` | 把 DS 集合的表项恢复为表项默认值（仅 RAM） |
| `include/iolinki/data_storage.h` | `iolink_ds_state_property` | 读取 State_Property 八位组（Table B.10 subindex 02） |
| `include/iolinki/data_storage.h` | `iolink_ds_size` | 读取 Data_Storage_Size 值（Table B.10 subindex 03） |
| `include/iolinki/data_storage.h` | `iolink_ds_checksum` | 读取 Parameter_Checksum 值（Table B.10 subindex 04） |
| `include/iolinki/data_storage.h` | `iolink_ds_param_count` | DS 集合中的表项数量 |
| `include/iolinki/data_storage.h` | `iolink_ds_param_entry` | 取 DS 集合中第 i 个表项 |
| `include/iolinki/data_storage.h` | `iolink_ds_request_upload` | 发起数据存储上传请求（置位 DS_UPLOAD_FLAG，10.4.2） |
| `include/iolinki/data_storage.h` | `iolink_ds_clear_upload` | 清除待处理的数据存储上传请求（DS_UPLOAD_FLAG = false） |
| `include/iolinki/events.h` | `iolink_events_init` | 初始化事件上下文（设备层在初始化时调用） |
| `include/iolinki/events.h` | `iolink_event_trigger` | 触发一个诊断事件（Error/Warning 保持活动，Notification 单次） |
| `include/iolinki/events.h` | `iolink_event_clear` | 报告此前触发的 Error/Warning 事件已消失 |
| `include/iolinki/events.h` | `iolink_events_process` | 处理延迟的 A.6.4 生命周期状态转换 |
| `include/iolinki/events.h` | `iolink_events_clear_diagnosis` | 撤销所有活动的 Error/Warning（Table 101 的「Clear」） |
| `include/iolinki/events.h` | `iolink_events_get_dropped_count` | 因队列或 pending[] 已满而丢失的事件数量 |
| `include/iolinki/events.h` | `iolink_events_reset` | 丢弃所有已入队事件并重置生命周期跟踪 |
| `include/iolinki/events.h` | `iolink_events_pending` | 检查是否有待主站读取的事件 |
| `include/iolinki/events.h` | `iolink_events_flag` | 读取 CKS 第 7 位中发送的 Event 标志（7.3.8.2） |
| `include/iolinki/events.h` | `iolink_events_memory_read` | 读取 Table 58 事件存储器（诊断通道）的一个字节 |
| `include/iolinki/events.h` | `iolink_events_memory_write` | 写入 Table 58 事件存储器（诊断通道）的一个字节 |
| `include/iolinki/events.h` | `iolink_events_pop` | 弹出最旧的事件 |
| `include/iolinki/events.h` | `iolink_events_peek` | 查看最旧的事件而不弹出 |
| `include/iolinki/events.h` | `iolink_event_qualifier` | 由 EventType 和 MODE 生成 EventQualifier（A.6.4 / Figure A.24） |
| `include/iolinki/events.h` | `iolink_events_get_all` | 将所有待处理事件复制到缓冲区而不弹出它们 |
| `include/iolinki/diag.h` | `iolink_diag_registry_size` | 注册表条目数（Table D.1 覆盖范围） |
| `include/iolinki/diag.h` | `iolink_diag_registry_at` | 按序号读取注册表条目（测试与 IODD 导出用） |
| `include/iolinki/diag.h` | `iolink_diag_desc` | 按 EventCode 查找注册表条目 |
| `include/iolinki/diag.h` | `iolink_diag_src_desc` | 按来源取得其注册表条目 |
| `include/iolinki/diag.h` | `iolink_diag_src_code` | 来源对应的 EventCode |
| `include/iolinki/diag.h` | `iolink_diag_type_for` | 规范化 EventType：注册表命中时返回规范值，否则返回 fallback |
| `include/iolinki/diag.h` | `iolink_diag_status_for` | Table D.1 首选 DeviceStatus：未登记时按类型回落到 0/2/4 |
| `include/iolinki/diag.h` | `iolink_diag_init` | 初始化诊断条件缓存（由 `iolink_events_init()` 调用） |
| `include/iolinki/diag.h` | `iolink_diag_reset` | 清空条件缓存，保留时钟/窗口配置 |
| `include/iolinki/diag.h` | `iolink_diag_set_level` | 采样一个电平源 |
| `include/iolinki/diag.h` | `iolink_diag_pulse` | 在主上下文报告一个脉冲源事件 |
| `include/iolinki/diag.h` | `iolink_diag_pulse_isr` | ISR 侧脉冲发布：只写一个 one-shot 字节 |
| `include/iolinki/diag.h` | `iolink_diag_service` | 主上下文诊断服务：消费 ISR 脉冲并关闭 PULSE 保持窗口 |
| `include/iolinki/diag.h` | `iolink_diag_rearm` | 重新武装指定的电平来源 |
| `include/iolinki/diag.h` | `iolink_diag_device_status` | Table B.13 DeviceStatus，由条件表派生 |
| `include/iolinki/diag.h` | `iolink_diag_dds_snapshot` | Table B.14 DDS 记录快照，由条件表派生 |
| `include/iolinki/diag.h` | `iolink_diag_raise_code` | 应用级条件置位（`iolink_event_trigger()` 的薄封装） |
| `include/iolinki/diag.h` | `iolink_diag_clear_code` | 应用级条件释放（`iolink_event_clear()` 的薄封装） |
| `include/iolinki/diag.h` | `iolink_diag_edge_count` | 某来源累计的边沿/脉冲次数（统计投影） |
| `include/iolinki/capability.h` | `iolink_caps_build` | 取本镜像的构建档位（单例，只读） |
| `include/iolinki/capability.h` | `iolink_caps_resolve` | 解析一个设备实例的能力位 |
| `include/iolinki/capability.h` | `iolink_caps_has` | 测试能力位是否置位 |
| `include/iolinki/capability.h` | `iolink_caps_built` | 测试构建特性位是否置位 |
| `include/iolinki/workspace.h` | — | 传输 scratch 工作区（无独立函数） |
| `include/iolinki/port.h` | `iolink_critical_enter` | 进入可嵌套的平台临界区 |
| `include/iolinki/port.h` | `iolink_critical_exit` | 退出平台临界区，恢复进入时保存的屏蔽状态 |
| `include/iolinki/port.h` | `iolink_platform_critical_sections_ready` | 探测平台是否提供了真正的临界区钩子 |
| `include/iolinki/port.h` | `iolink_atomic_load` | 以获取语义读取发布字 |
| `include/iolinki/port.h` | `iolink_atomic_store` | 以释放语义写入发布字 |
| `include/iolinki/phy.h` | — | PHY vtable 与回调类型（无独立函数） |
| `include/iolinki/phy_tiol112.h` | `iolink_phy_tiol112_init` | 复制 IO 回调；驱动与 `io.user` 必须比设备上下文存活更久 |
| `include/iolinki/phy_tiol112.h` | `iolink_phy_tiol112_get` | 取 TIOL112 的 PHY vtable |
| `include/iolinki/phy_tiol112.h` | `iolink_phy_tiol112_fault` | 聚合 NFAULT：欠压、温度或短路 |
| `include/iolinki/phy_l6362a.h` | `iolink_phy_l6362a_init` | 初始化：把 vtable 的 `self` 绑定到 ctx（不能传 NULL） |
| `include/iolinki/phy_l6362a.h` | `iolink_phy_l6362a_get` | 取 L6362A 的 PHY vtable |
| `include/iolinki/phy_l6362a.h` | `iolink_phy_l6362a_overload` | 1 = 过载有效，0 = 无故障，-1 = 不可用 |
| `include/iolinki/phy_l6362a.h` | `iolink_phy_l6362a_fault` | 仅在 EN 请求为高时有效；聚合截止/UVLO/过温 |
| `include/iolinki/phy_virtual.h` | `iolink_phy_virtual_get` | 获取虚拟 PHY 提供者（对端为伪终端上的虚拟主站） |
| `include/iolinki/phy_virtual.h` | `iolink_phy_virtual_set_port` | 设置虚拟 PHY 的串口 |
| `include/iolinki/phy_generic.h` | `iolink_phy_generic_get` | 获取通用 PHY API（模板） |
| `include/iolinki/iolink.h` | `iolink_cycle_time_to_us` | 将 Table B.3 的周期时间八位组解码为微秒 |
| `include/iolinki/iolink.h` | `iolink_cycle_time_encode` | 将以 0.1 ms 为单位的周期时间编码为 Table B.3 八位组 |
| `include/iolinki/iolink.h` | `iolink_mseq_default_od_width` | M 序列类型的默认 OD 八位组数量 m（Table A.10） |
| `include/iolinki/iolink.h` | `iolink_mseq_operate_code` | 给定类型与 OD 宽度时返回 OPERATE M 序列代码（Table A.10） |
| `include/iolinki/crc.h` | `iolink_checksum6` | 计算 IO-Link 报文校验和（A.1.6） |
| `include/iolinki/crc.h` | `iolink_checksum6_from_acc` | 将已 XOR 累积的 A.1.6 种子压缩为 6 位校验和 |
| `include/iolinki/protocol.h` | — | 标准索引、ISDU 错误码与 SystemCommand 常量（无函数） |
| `include/iolinki/log.h` | `iolink_log_sink_init` | 初始化一个日志汇点 |
| `include/iolinki/result.h` | `iolink_result_ok` | 构造成功结果 |
| `include/iolinki/result.h` | `iolink_result_error` | 构造错误结果（负值内部错误或正值 ISDU ErrorCode） |
| `include/iolinki/result.h` | `iolink_result_succeeded` | 结果是否成功 |
| `include/iolinki/utils.h` | `iolink_buf_is_valid` | 校验（指针，长度）缓冲区对 |
| `include/iolinki/utils.h` | `iolink_ctx_zero` | 将上下文对象清零初始化 |
| `include/iolinki/config.h` | — | 编译期容量与特性宏（无函数） |
| `include/iolinki/types.h` | — | 公共类型与前置声明（无函数） |
| `include/iolinki/profile.h` | — | 构建档位链接期锚点宏（无函数） |
<!-- END GENERATED -->

### 11.1 裸 DLL 集成

直接使用 `dll.h` 时，用 `iolink_dll_init(&ctx.main, &ctx.isr, phy)` 初始化，然后在数据
ISR 静止时设置 `shared.clock`、`shared.m_seq_type`、`shared.od_width`、`shared.pd_in_len`、
`shared.pd_out_len_current` 与能力位，并调用一次 `iolink_dll_publish_reply_plans()`。
这些字段和应答计划都只在初始化时定义，之后 `iolink_dll_process()` 不会重发布。
该发布入口返回 `IOLINK_DLL_PUBLISH_OK`（0）表示表已发布；`ctx` 为 NULL、上下文未经
`iolink_dll_init()` 初始化或在数据 ISR 内调用时返回负的 `iolink_dll_publish_result_t`，
且不改动已有的表。状态布局拆在 `dll_main.h`（主上下文 + 共享区）、`dll_shared.h`
（双上下文共享）、`dll_isr.h`（ISR 独占）和 `dll_ctx.h`（单次分配容器）中；直接使用
DLL 时还要自行负责事件/ISDU/DS 集成。统计字段与 `iolink_dll_get_baudrate()` 的当前
签名见 [API.md 的「DLL 统计（裸 DLL 集成）」](API.md)。

### 11.2 收发器、PHY 模板与常量头

- `include/iolinki/phy_tiol112.h` / `phy_l6362a.h`：收发器驱动。用板级 IO 回调调用
  `iolink_phy_*_init()`，然后把对应的 `iolink_phy_*_get()` 传给设备配置。TIOL112 的
  `io.user` 与 L6362A 的 `io.board` 必须在设备上下文存活期间保持有效。
  `iolink_phy_tiol112_fault()` 与 `iolink_phy_l6362a_fault()` / `_overload()` 只报告
  聚合状态，不推断具体原因。
- `include/iolinki/phy_virtual.h`：主机/PTY 虚拟 PHY。只实现 BYTE 模式，不适用于
  MCU 固件。`include/iolinki/phy_generic.h` 是新 PHY 移植的参考模板。
- `include/iolinki/crc.h`：V1.1.5 A.1.6 校验和；`include/iolinki/protocol.h` 提供标准
  索引、ISDU 错误码、SystemCommand 与 M-sequence 常量，应使用这些常量而不是数字字面量。
- `include/iolinki/capability.h`、`od.h`、`workspace.h`、`diag.h`：分别见 §4.5、§4.6、
  §4.1 与 §5.2。
- `include/iolinki/port.h`：临界区与屏障原语。时间源通过
  `iolink_device_app_t::time_us` 提供；持久化由应用通过 OD 钩子完成。

## 12. 参考资料

- `docs/API.md` — 精简 API 参考。
- `docs/PORTING.md` — 平台和收发器移植说明。
- `docs/STORAGE.md` — 应用持久化契约与 Data Storage 细节。
- `docs/feature-trimming.md` — 特性裁剪契约。
- `docs/CODE_FLOW.md` — ISR 传输的中文代码流程说明。
- `examples/reference_device/` — 带 PHY 模拟器的最小应用。
- `examples/stm32g0_tiol112/` — TIOL112 MCU 适配器。
- `examples/stm32g0_l6362a_raw/` — raw octet L6362A 适配器及 ISR 接线。
- `examples/host_demo/` — 基于伪终端虚拟 PHY 的主机示例。
