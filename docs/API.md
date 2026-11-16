# API 参考

本栈基于上下文（没有单例）：应用分配一个 `iolink_device_ctx_t` 并把它传给每次调用。
只要每个实例使用自己的上下文，多个设备实例可以共存。权威声明见
`include/iolinki/device.h`、`include/iolinki/application.h`、`include/iolinki/events.h`、
`include/iolinki/isdu.h`、`include/iolinki/data_storage.h`、
`include/iolinki/workspace.h`、`include/iolinki/capability.h`、
`include/iolinki/od.h` 和 `include/iolinki/diag.h`。
`include/iolinki/dll.h` 是高级/底层接口（帧泵、定时器表、统计）；只写设备
逻辑时不要包含它。上下文布局头在 `include/iolinki/internal/`，随库安装但
只供公共头编译使用。

库不拥有非易失存储（NVM）：参数值、标签和 Data Storage 标志的落盘由应用负责，
库只通过钩子通知变化，见“应用持久化契约”。

## 核心 API

### 初始化

```c
#include "iolinki/device.h"

int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);
iolink_workspace_t* iolink_device_get_workspace(const iolink_device_ctx_t* ctx);
```

根据 `config` 初始化协议栈。`config` 及其指向的所有对象必须在 `ctx` 的整个生命周期内
保持有效。

**返回值**：成功返回 `0`，配置无效返回负的 `iolink_device_init_result_t`
（`IOLINK_DEVICE_INIT_ERR_*`）。PHY `init` 回调失败映射为
`IOLINK_DEVICE_INIT_ERR_PHY`；驱动自己的错误码用
`iolink_device_get_phy_error()` 读取（默认日志是静默的）。

#### 工作区

`iolink_workspace_t`（见 `include/iolinki/workspace.h`）保存 ISDU 请求/载荷/应答/成帧
scratch；编译进 Data Storage 时还包含 DS 序列化暂存区。它不保存长生命周期状态，
因此可以放在 DTCM 或外部 RAM，也不需要跨事务存活。

- `IOLINK_WORKSPACE_EMBEDDED=1`（默认）：`config.workspace` 可选。为 `NULL` 时
  `iolink_device_init()` 使用上下文内嵌的 scratch，`iolink_device_get_workspace()`
  返回 `NULL`。
- `IOLINK_WORKSPACE_EMBEDDED=0`：没有内嵌 fallback。`config.workspace` 必须非
  `NULL`，否则 `iolink_device_init()` 返回 `IOLINK_DEVICE_INIT_ERR_WORKSPACE`。

`iolink_device_get_workspace()` 返回当前绑定到实例的工作区；使用内嵌 fallback 时返回
`NULL`。`IOLINK_WORKSPACE_DECLARE(name)` 声明一个静态实例。

#### 设备配置

```c
typedef struct {
    const iolink_phy_api_t* phy;                 /* PHY 驱动 vtable（phy->self 由驱动写入） */
    const iolink_stack_config_t* stack;          /* 协议设置；必须比上下文存活更久 */
    const iolink_device_app_t* app;              /* 应用 trait：实例指针 + 回调；NULL 拒绝初始化 */
    const iolink_device_info_t* device_info;     /* 必填；NULL 拒绝初始化 */
#if IOLINK_FEATURE_DATA_STORAGE
    iolink_ds_flag_persist_fn ds_flag_persist;   /* DS_UPLOAD_FLAG 落盘钩子；NULL = 仅 RAM */
#endif
    iolink_log_level_t log_level;            /* 运行时日志阈值；SILENT（0）关闭 */
    const iolink_od_app_t* od;               /* 可选的应用对象字典 */
    iolink_workspace_t* workspace;           /* 可选 scratch；NULL = 内嵌 fallback */
} iolink_device_config_t;
```

`app` 是唯一的应用上下文来源（见下面的 trait）。`ds_flag_persist` 收到的是
`app->self` 和新的标志值；应用在自己的 NVM 中保存它。

#### 协议栈配置

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /* Type 0/1_2/1_V/2_1..2_5/2_V */
    uint8_t min_cycle_time;         /* 最小循环时间，0.1 ms 单位 */
    uint8_t pd_in_len;              /* Process Data Input 长度（字节） */
    uint8_t pd_out_len;             /* Process Data Output 长度（字节） */
    uint32_t t_pd_us;               /* 上电延迟（t_pd），微秒 */
    uint8_t od_width;               /* OD 八位组数 1/2/8/32；必填，0 拒绝初始化 */
    iolink_baudrate_t baudrate;     /* baudrate_set 为 true 时使用的 COM */
    bool baudrate_set;              /* false 保持 COM2 默认值 */
    uint8_t master_lost_cycles;     /* 判定 master_lost 的丢失周期数；0 = 关闭 */
    uint8_t compat_flags;           /* IOLINK_STACK_COMPAT_* 位，与构建默认值按位或 */
    uint8_t flags;                  /* IOLINK_STACK_FLAG_* 位 */
    uint8_t timing_enforce;         /* IOLINK_STACK_TIMING_*；DEFAULT = 构建档位 */
    uint8_t reserved;               /* 必须为 0 */
    uint16_t event_min_appear_ms;   /* 事件最小出现窗口（ms）；0 = 构建默认值 */
    uint16_t event_min_repeat_ms;   /* 同代码重复窗口（ms）；0 = 构建默认值 */
} iolink_stack_config_t;
```

`min_cycle_time` 按 0.1 ms 单位原样保存，只在构建 Direct Parameter Page 1 时按表 B.3
编码。

`compat_flags` / `flags` / `timing_enforce` 是能力表的运行时部分：`iolink_device_init()`
执行 `iolink_caps_resolve()` 时把它们与构建期默认值合并。`event_min_appear_ms` /
`event_min_repeat_ms` 覆盖 A.6.4 的构建期窗口；`0` 保持构建默认值。

**M-Sequence 类型**：
- `IOLINK_M_SEQ_TYPE_0`：1 个 OD 八位组，无 PD。
- `IOLINK_M_SEQ_TYPE_1_1`：旧版交叠类型；V1.1.5 设备拒绝该类型（表 A.10 注 3），
  返回 `IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE`。
- `IOLINK_M_SEQ_TYPE_1_2`：2 个 OD 八位组，无 PD。
- `IOLINK_M_SEQ_TYPE_1_V`：8 或 32 个 OD 八位组，无 PD。
- `IOLINK_M_SEQ_TYPE_2_1` / `2_2`：1 / 2 字节 PD-in + 1 个 OD。
- `IOLINK_M_SEQ_TYPE_2_3` / `2_4`：1 / 2 字节 PD-out + 1 个 OD。
- `IOLINK_M_SEQ_TYPE_2_5`：1 字节 PD-in + 1 字节 PD-out + 1 个 OD。
- `IOLINK_M_SEQ_TYPE_2_V`：可变 PD（0..32）+ 1/2/8/32 个 OD。

固定 PD/OD 布局在 `iolink_device_init()` 中强制检查；不匹配返回
`IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT`。

#### 示例

```c
#include "iolinki/device.h"

extern const iolink_phy_api_t g_phy_virtual;
extern const iolink_device_info_t g_identity;

static iolink_device_ctx_t g_ctx;
static uint64_t g_time_us; /* 由你的微秒定时器推进 */

struct iolink_app { uint8_t pd_in[3]; };    /* 应用自己的状态，库不访问 */
static struct iolink_app g_app_state;

static uint64_t board_time_us(iolink_app_t* self) {
    (void) self;
    return g_time_us;
}

static const iolink_device_app_t g_app = {
    .self = (iolink_app_t*) &g_app_state,
    .time_us = board_time_us,
};

static const iolink_stack_config_t g_stack = {
    .m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .od_width = 2U,
    .pd_in_len = 3, .pd_out_len = 1};

static const iolink_device_config_t g_cfg = {
    .phy = &g_phy_virtual,
    .stack = &g_stack,
    .app = &g_app,
    .device_info = &g_identity,
};

int main(void) {
    if (iolink_device_init(&g_ctx, &g_cfg) != 0) {
        return -1;
    }
    for (;;) {
        iolink_device_process(&g_ctx);
        /* 休眠约 1 ms */
    }
}
```

#### 初始化自检

`IOLINK_FEATURE_VALIDATE`（默认 1）是初始化期配置自检的总开关。打开时
`iolink_device_init()` 在绑定任何子系统之前检查应用对象字典表，命中问题返回
`IOLINK_DEVICE_INIT_ERR_OD`，而不是等到第一次 ISDU 访问才失败。检查项包括：

- 表项 `handler` 非 `IOLINK_OD_HANDLER_NONE`（应用表不能复用内建处理器）；
- `datatype`、`access`、`write_semantics`、`backend` 取值非法；
- `storage` 含 `IOLINK_OD_STORAGE_DS` 之外的位，或 `TRIGGER` 语义带 storage 位；
- 可写 `STORAGE` 字节串没有 `block_len` 上限；
- 只读表项带 `validate`/`on_change` 钩子；
- `range_min > range_max`，或 `scalar_default` 不在范围内；
- `STORAGE` 后端缺值指针（字节串还缺长度指针），`HANDLER` 后端缺可读项的
  `read` 或缺可写项的 `write`；
- 重复的 index/subindex；
- 应用表占用规范固定索引（`< 0x0040`，三个标签 `0x0018-0x001A` 除外）。

表项宏在编译期检查常量之间的同类关系；本开关只覆盖指针关系与跨表项关系，协议期
的数据校验始终生效。表稳定后可以关闭它以省下这段 `.text`。

### 主循环与 ISR 泵

```c
void iolink_device_process(iolink_device_ctx_t* ctx);      /* 主循环 */
void iolink_device_process_isr(iolink_device_ctx_t* ctx);  /* 数据 ISR */
```

协议栈使用双泵传输：`iolink_device_process()` 运行协议逻辑，必须周期性调用
（例如每 1 ms 一次）；`iolink_device_process_isr()` 由 PHY 数据中断调用，负责对请求
成帧并发送应答。

### 过程数据

```c
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data,
                                  size_t len, bool valid);
int iolink_device_pd_output_read(const iolink_device_ctx_t* ctx, uint8_t* data, size_t len);
```

`pd_input_update` 发布 Device-to-Master 过程数据（`valid` 驱动 CKS 的 PD-invalid 位）。
两条 PD-in 路径在同一构建内共存：非 NULL 的 `on_pd_input` 在初始化时选择回调路径并
置位实例位 `IOLINK_CAP_PD_IN_CALLBACK`；否则应用用本函数轮询发布。选择回调路径后，
`pd_input_update` 返回 `IOLINK_DEVICE_INIT_ERR_PD_INPUT`，而不是静默覆盖回调输出。
`pd_output_read` 返回最新的 Master-to-Device 数据。

### 能力

```c
iolink_caps_t iolink_device_capabilities(const iolink_device_ctx_t* ctx);
iolink_caps_t iolink_device_set_capabilities(iolink_device_ctx_t* ctx, iolink_caps_t caps);

const iolink_build_caps_t* iolink_caps_build(void);
iolink_caps_t iolink_caps_resolve(const iolink_phy_api_t* phy, const iolink_stack_config_t* stack,
                                  const iolink_device_app_t* app,
                                  const iolink_build_caps_t* build);
bool iolink_caps_has(iolink_caps_t caps, iolink_caps_t bit);
bool iolink_caps_built(const iolink_build_caps_t* caps, uint32_t mask);
```

能力表分两层。`iolink_build_caps_t`（`iolink_caps_build()`，`const` 位于 `.rodata`）
描述本镜像的编译结果：ISR RX/TX 粒度（`IOLINK_CAP_ISR_RX_*` / `IOLINK_CAP_ISR_TX_*`）、
并发/统计档位、PD-in 缓冲数以及 `IOLINK_CAPB_*` 构建特性位（`DATA_STORAGE`、`EVENTS`、
`SYSTEM_COMMAND`、`ISR_LOG`、`ISR_TX_ONLY`）。`iolink_caps_t` 是绑定 PHY 后由
`iolink_caps_resolve()` 一次性解析的实例掩码，运行期通过 `iolink_device_capabilities()`
读取。

实例位：`IOLINK_CAP_RX`、`_TX`、`_WAKEUP`、`_SIO_CQ`、`_TX_BUSY`、
`_REQUEST_SERVICE`、`_PHY_FAULTS`（由 PHY vtable 探测）、`_PD_IN_CALLBACK`、
`_SYSTEM_COMMAND`、`_EVENTS`、`_COMPAT_V10_OP`、`_COMPAT_T0_READ`、`_DS_LEGACY_SUB0`、
`_REQUIRE_ISR_TX`，以及四个 DeviceAccessLocks 位 `_LOCK_PARAM`、`_LOCK_DS`、
`_LOCK_LOCAL_PARAM`、`_LOCK_LOCAL_UI`（合称 `IOLINK_CAP_LOCKS_ALL`）。

`iolink_device_set_capabilities()` 只接受对运行时开关（兼容位、SystemCommand、访问锁）
的修改。与 PHY vtable、构建档位或 `on_pd_input` 绑定的位会按当前配置重新推导，调用方
无法伪造；未编译进相应子系统时 `IOLINK_CAP_EVENTS` 和 `IOLINK_CAP_SYSTEM_COMMAND`
保持清零。函数返回实际生效的掩码。

`iolink_device_init()` 用 `IOLINK_DEVICE_INIT_ERR_PHY` 拒绝两种不一致组合：
`IOLINK_CAP_REQUIRE_ISR_TX` 但 PHY 没有 `request_service()` 钩子，以及 TX FRAME 构建
在 `IOLINK_PHY_FEATURE_TX_BUSY=1` 下没有 `tx_busy()` 钩子。

### 复位处理

```c
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler);
```

在 SystemCommand `0x80`（`IOLINK_RESET_DEVICE`）/ `0x81`（`IOLINK_RESET_APPLICATION`）
时调用。协议栈不会自行重启设备。

## PHY 层 API

从 `include/iolinki/phy.h` 复制 `iolink_phy_api_t`。每个回调的第一个参数都是
`iolink_app_t* self`（应用实例，库原样转发；驱动在自己的回调里转回自己的状态类型）。
`phy->self` 由提供 vtable 的一方写入自己的上下文：内置驱动在 `iolink_phy_*_init()`
里写，手写 vtable 就在初始化器里写。应用不得覆盖它（覆盖后驱动会把错误的指针转回
自己的状态）。若应用希望一个实例只有一个指针，就把驱动上下文作为
`struct iolink_app` 的第一个成员（偏移 0）并令 `app->self = (iolink_app_t*) &app`：
两者地址相同。分开存放时各有一个指针，驱动仍能正常工作，但两者不是同一对象。

`init`、`set_mode` 和 `set_baudrate` 为必需。传输钩子取决于配置的粒度：
BYTE 模式使用 `rx_octet`/`tx_octet`，FRAME 模式使用 `rx_frame`/`tx_frame`，外加
`int_active`。`tx_done`（TX BYTE）是可选的完成通知：TX 能力由 `tx_octet` 解析，钩子
缺省时库自行完成帧。`tx_busy`（TX FRAME）、`detect_wakeup`、`set_cq_line` 和
`get_faults` 可以为 `NULL`。

`IOLINK_BAUDRATE_COM1`、`COM2`、`COM3` 分别是 0、1、2，对应 4.8、38.4 和
230.4 kbit/s；它们不是波特率编号本身。

```c
static int my_phy_rx_octet(iolink_app_t* self, uint8_t* data, uint8_t* status) {
    (void)self;
    (void)data;
    (void)status;
    return 0;
}

static int my_phy_tx_octet(iolink_app_t* self, uint8_t data) {
    (void)self;
    (void)data;
    return 0;
}
```

## 应用层 API

```c
typedef struct {
    iolink_app_t* self;                          /* 实例指针，原样传给每个回调 */
    uint64_t (*time_us)(iolink_app_t* self);     /* 必需；DLL/ISR/事件共用 */
    void (*log_write)(iolink_app_t* self, iolink_log_level_t level, const char* format, ...);
    void (*on_startup)(iolink_app_t* self);
    void (*on_preoperate)(iolink_app_t* self);
    void (*on_operate)(iolink_app_t* self);
    bool (*on_pd_input)(iolink_app_t* self, uint8_t* data, uint8_t len); /* 非 NULL 选择回调路径 */
    void (*on_pd_output)(iolink_app_t* self, uint8_t* data, uint8_t len);
    void (*on_state_change)(iolink_app_t* self, iolink_dll_state_t state);
    void (*on_master_lost)(iolink_app_t* self);
    void (*on_master_online)(iolink_app_t* self);
} iolink_device_app_t;
```

`iolink_app_t` 是应用在自己翻译单元里定义的不完整类型（`types.h` 只做前置声明），
因此把别的指针赋给 `self` 需要显式强转，`void*` 时代的静默传错变成编译期诊断。
每个回调都收到同一个 `self`，应用不需要借助全局变量访问自己的状态。
通过 `iolink_device_config_t.app` 注册，没有单独的注册函数。`on_pd_input`
填写一个空闲 PD-in 缓冲，其返回值成为 PD-valid 标志；保持 `NULL` 表示应用改用
`iolink_device_pd_input_update()` 发布 PD-in。

**示例**：

```c
struct iolink_app { uint8_t pd[3]; };

static void on_operate(iolink_app_t* self) { struct iolink_app* a = (struct iolink_app*) self; }
static void on_pd_output(iolink_app_t* self, uint8_t* data, uint8_t len) { (void)self; (void)data; (void)len; }

static const iolink_device_app_t g_app = {
    .self = (iolink_app_t*) &g_app_state,
    .time_us = board_time_us,
    .on_operate = on_operate,
    .on_pd_output = on_pd_output,
};
/* ... 在 iolink_device_config_t 中设置 .app = &g_app ... */
```

## 对象字典 API

对象字典（`include/iolinki/od.h`）是每个参数的唯一描述：索引/子索引、datatype codec、
访问权限、写语义、storage 类别、默认值以及可选的 `validate`/`on_change` 钩子。ISDU
分发、Data Storage 成员判定和 IODD 导出都从它派生。

```c
struct iolink_od_entry {
    uint16_t index;
    uint8_t subindex;
    uint8_t max_subindex;
    uint8_t datatype;         /* IOLINK_OD_DT_* */
    uint8_t access;           /* IOLINK_OD_ACCESS_* */
    uint8_t write_semantics;  /* IOLINK_OD_WRITE_STORE / _VOLATILE / _TRIGGER */
    uint8_t storage;          /* IOLINK_OD_STORAGE_VOLATILE / _DS */
    uint8_t flags;            /* IOLINK_OD_FLAG_* */
    uint8_t handler;          /* 内建处理器 ID；应用表项为 IOLINK_OD_HANDLER_NONE */
    uint8_t backend;          /* IOLINK_OD_BACKEND_STORAGE / _HANDLER */
    union {
        struct {
            uint8_t* value;    /* 应用变量地址（字节视图，读写） */
            uint8_t* str_len;  /* OctetString 当前长度 */
        } storage;
        struct {
            iolink_od_read_fn read;
            iolink_od_write_fn write;
        } handler;
    } u;
    uint8_t block_len;
    uint32_t range_min;
    uint32_t range_max;
    uint32_t scalar_default;
    const uint8_t* block_default;
    const char* name;
    iolink_od_validate_fn validate;
    iolink_od_change_fn on_change;
};
typedef struct iolink_od_entry iolink_od_entry_t;

typedef struct {
    const iolink_od_entry_t* entries; /* 应用表项数组 */
    size_t count;                     /* 表项数量 */
} iolink_od_app_t;

const iolink_od_entry_t* iolink_od_find(const iolink_od_app_t* app, uint16_t index,
                                        uint8_t subindex);
const iolink_od_entry_t* iolink_od_find_app(const iolink_od_app_t* app, uint16_t index,
                                            uint8_t subindex);
iolink_result_t iolink_od_read_value(const iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                     uint8_t subindex, uint8_t* out, size_t cap);
iolink_result_t iolink_od_write_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                      const uint8_t* data, size_t len);
iolink_result_t iolink_od_check_value(const iolink_od_entry_t* entry, iolink_app_t* self,
                                      const uint8_t* data, size_t len);
iolink_result_t iolink_od_restore_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                        uint8_t subindex, const uint8_t* data, size_t len);
```

标准对象（`0x0000-0x003F` 中除三个标签外的对象，以及 `0x0040` 的厂商错误统计）位于
库内建的 `const` 表中，由协议栈处理器提供；应用通过 `config.od` 增加自己的参数。
表项是纯值语义，因此整张表可以保持 `const` 放在 ROM。每个应用表项只有一种值后端：
`STORAGE`（指向应用变量，零 shadow）或 `HANDLER`（逐表项 `read`/`write` 函数指针，
用于计算值）。

**增加一个参数**只需要加一行表项。绑定变量的标量：

```c
static uint16_t g_threshold = 500U;

static const iolink_od_entry_t g_od_entries[] = {
    IOLINK_OD_SCALAR_STORAGE(0x0050, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_DS, 0U, 1000U, 500U,
                             "Threshold", &g_threshold),
};

static const iolink_od_app_t g_od = {
    .entries = g_od_entries,
    .count = sizeof(g_od_entries) / sizeof(g_od_entries[0]),
};
/* ... 在 iolink_device_config_t 中设置 .od = &g_od ... */
```

`IOLINK_OD_SCALAR_STORAGE` / `IOLINK_OD_BYTES_STORAGE` 绑定应用变量并在编译期检查其
宽度；`_HANDLER` 变体改为接收一对 `read`/`write` 函数，`_EX` 后缀追加
`validate`/`on_change` 钩子。字节串需要一个数组和一个 `uint8_t` 长度变量，库只按
长度复制原始字节，不添加 NUL。

索引归属规则：

- 规范固定索引（`< 0x0040`）由标准表提供，应用表不能遮蔽；唯一例外是三个标签
  `0x0018-0x001A`，它们的数据归应用。
- 设备专用索引（`0x0040-0x00FE`、`0x0100+`）先查应用表，未声明时回落到内建标准
  表项（`0x0040` 错误统计）。
- `0x0004-0x000B` 与 `0x001C-0x001F` 是保留区间，`0x00FF` 也是保留值；标准表没有
  这些对象，开启 `IOLINK_FEATURE_VALIDATE` 时应用表也不能声明 `< 0x0040` 的索引
  （标签除外），因此它们返回 `0x80 0x11`。

`iolink_od_write_value()` 接收请求的子索引，强制 `max_subindex`，并以上限 `block_len`
拒绝超长 OctetString（`0x33`）。写入顺序固定为：线长/范围/`validate` 钩子 → 语义
动作 → `on_change` 钩子。`TRIGGER` 语义只执行 `on_change`，值本身不保存。标量的
`on_change` 返回错误时变量回滚到写前值；OctetString 不回滚，需要在写入前拒绝时使用
`validate`。`iolink_od_restore_value()` 跳过 `validate`/`on_change`，供下载恢复和
出厂复位使用。

### 标签由应用提供

`0x0018` Application Tag、`0x0019` Function Tag、`0x001A` Location Tag 不在标准表中；
应用用 `IOLINK_OD_BYTES_STORAGE[_EX]` 把三个标签作为普通字节串参数提供：

```c
static char g_application_tag[32];
static uint8_t g_application_tag_len = 0U;

static const iolink_od_entry_t g_tag_entries[] = {
    IOLINK_OD_BYTES_STORAGE(0x0018, IOLINK_OD_ACCESS_RW_LOCKED, IOLINK_OD_WRITE_STORE,
                            IOLINK_OD_STORAGE_DS, 32U, "ApplicationTag", g_application_tag,
                            &g_application_tag_len),
};
```

`block_len` 同时是长度上限和 IODD 的 `fixedLength`；长度变量是读取时的权威长度。
应用未声明某个标签时，该索引返回 `0x80 0x11`（索引不可用）。`device_info` 中的
`application_tag` 只是身份结构里的一个字段，不参与标签的 ISDU 读写；要提供
`0x0018` 必须在 OD 表中声明表项。

完整表（含校验与触发语义）见 `examples/switching_sensor/switching_sensor_od.c`。

## 事件 API

```c
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type);
void iolink_event_clear(iolink_events_ctx_t* ctx, uint16_t code);
void iolink_events_process(iolink_events_ctx_t* ctx);   /* 由 iolink_device_process 驱动 */
bool iolink_events_pending(const iolink_events_ctx_t* ctx);
```

`iolink_device_process()` 在 ISDU 传输空闲时每周期调用 `iolink_events_process()`，
发出延迟的“Event disappears”条目。只有直接驱动事件上下文时才需要自行调用，见
`events.h`。`iolink_event_trigger()` 通过诊断注册表（`diag.h`）规范化事件类型，这也
是填充 DeviceStatus 和 DetailedDeviceStatus 的同一来源。A.6.4 的出现/重复窗口默认取
`IOLINK_EVENT_MIN_APPEAR_MS` / `IOLINK_EVENT_MIN_REPEAT_MS`，可用
`iolink_stack_config_t::event_min_appear_ms` / `event_min_repeat_ms` 按实例覆盖
（`0` 保持构建默认值）。

**事件类型**：

```c
typedef enum {
    IOLINK_EVENT_TYPE_NOTIFICATION = 0,
    IOLINK_EVENT_TYPE_WARNING = 1,
    IOLINK_EVENT_TYPE_ERROR = 2
} iolink_event_type_t;
```

**示例**：

```c
iolink_event_trigger(iolink_device_get_events_ctx(&g_ctx), IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN,
                     IOLINK_EVENT_TYPE_WARNING);
```

## 诊断 API

`include/iolinki/diag.h` 是 EventCode、DeviceStatus（`0x0024`）和
DetailedDeviceStatus（`0x0025`）的唯一派生点。一张 `const` ROM 注册表描述每个诊断
来源（规范 EventCode 与 EventType、表 D.1 首选 DeviceStatus、电平/脉冲行为、保持
窗口）；一小块 RAM 缓存保存来源事实（ISR one-shot、最后发生时间、边沿计数）。条件的
生命周期仍以事件内存为准，因此 DeviceStatus、DDS 和 `0x1800-0x18FF` 通信事件都是
同一批条件的投影。

```c
bool iolink_diag_set_level(iolink_events_ctx_t* events, iolink_diag_src_t src, bool level);
void iolink_diag_pulse(iolink_events_ctx_t* events, iolink_diag_src_t src);
void iolink_diag_pulse_isr(iolink_events_ctx_t* events, iolink_diag_src_t src);
void iolink_diag_service(iolink_events_ctx_t* events, uint32_t now_ms);
void iolink_diag_raise_code(iolink_events_ctx_t* events, uint16_t code);
void iolink_diag_clear_code(iolink_events_ctx_t* events, uint16_t code);
uint8_t iolink_diag_device_status(iolink_events_ctx_t* events);
uint8_t iolink_diag_dds_snapshot(iolink_events_ctx_t* events, iolink_diag_dds_entry_t* out,
                                 uint8_t max);
```

来源分为 `IOLINK_DIAG_SRC_PHY_*`（欠压、过压、过温、短路）和 `IOLINK_DIAG_SRC_COMM_*`
（CRC、超时、帧错误、时序）。`set_level` 用于每周期采样一次的电平源，`pulse`/
`pulse_isr` 用于脉冲源；`pulse_isr` 只写一个 one-shot 字节，可从数据 ISR 安全调用，
`service` 在主上下文消费它（DLL 在 `iolink_dll_process()` 中调用，应用无需自行调用）。
`raise_code`/`clear_code` 是 `iolink_event_trigger()` / `iolink_event_clear()` 的应用级
包装，并用注册表规范化事件类型。

DeviceStatus 和 DDS 都不缓存：每次读取都从条件表重新计算，因此
`iolink_device_process()` 不需要维护并行的状态镜像。`IOLINK_FEATURE_EVENTS=0` 时整个
模块消失：`0x0025` 回答索引不可用，`0x0024` 返回 0。

## Data Storage API

```c
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);

void iolink_ds_init(iolink_ds_ctx_t* ctx);
void iolink_ds_set_flag_persist(iolink_ds_ctx_t* ctx, iolink_ds_flag_persist_fn fn,
                                iolink_app_t* self);
void iolink_ds_bind_workspace(iolink_ds_ctx_t* ctx, iolink_workspace_t* ws);
void iolink_ds_bind_sources(iolink_ds_ctx_t* ctx, const iolink_od_app_t* app,
                            iolink_app_t* self);
void iolink_ds_process(iolink_ds_ctx_t* ctx);                  /* 由 iolink_device_process 驱动 */
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);
int  iolink_ds_abort(iolink_ds_ctx_t* ctx);
int  iolink_ds_handle_param_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);
int  iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t command, uint16_t access_locks);
int  iolink_ds_serialize(iolink_ds_ctx_t* ctx, uint8_t* out, size_t cap);
void iolink_ds_refresh(iolink_ds_ctx_t* ctx);
int  iolink_ds_build_image(iolink_ds_ctx_t* ctx);
int  iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);
int  iolink_ds_factory_reset(iolink_ds_ctx_t* ctx);
```

DS 上下文由 `iolink_device_init()` 初始化（仅在 `IOLINK_FEATURE_DATA_STORAGE=1` 时
可用），用 `iolink_device_get_ds_ctx()` 访问。`ds_flag_persist` 由设备层绑定到
`config.ds_flag_persist`，并把 `app->self` 同时交给持久化钩子和 OD 钩子。

DS 的语义是**对象字典的投影**：参数集合就是标准表与应用表中 `storage` 含
`IOLINK_OD_STORAGE_DS` 的表项（设备层自动绑定 `iolink_device_config_t.od`），集合
顺序固定为标准表在前、应用表按表序。值全部来自表项后端，DS 上下文不保存参数副本，
也不写 NVM：

- **上传**：库按记录流式序列化当前参数集写入 ISDU 应答缓冲，同时更新
  `Parameter_Checksum`。
- **下载**：先通过 `iolink_ds_stage_param()` 暂存写操作；`ParamDownloadEnd` 对整份
  镜像做无副作用预校验（线长、范围和表项 `validate`），全部通过后才逐条通过
  `iolink_od_write_value()` 写回，每条记录写回后触发该表项的 `on_change`。
- **`DS_UPLOAD_FLAG`**：只保存在 RAM。置位和清除时库调用 `flag_persist`，由应用决定
  是否、何时写入自己的 NVM。`NULL` 表示仅 RAM。
- **出厂复位**：`iolink_ds_factory_reset()` 只把 DS 集合中的应用表项恢复为表项默认
  值（`iolink_od_restore_value()`，不执行钩子），不写 NVM，也不处理非 DS 参数。

预校验不是跨参数事务：预校验失败时不会写入任何参数，但逐条写回阶段仍有失败风险，
此时前缀记录已经生效。库不提供跨记录事务，需要原子性的应用应自行在 `on_change` 中
合并或延迟落盘。

序列化记录格式为：

```
[Index:2 大端][Subindex:1][Length:1][Data:Length]
```

序列化映像不是常驻状态：上下文只持有指向暂存缓冲的指针
（`iolink_workspace_t::ds_stage`，或内嵌 fallback）。上传和 `Parameter_Checksum`
读取走流式路径 `iolink_ds_serialize()`（可直接写入 ISDU 应答缓冲，`out == NULL` 时
只统计长度与校验和）；`iolink_ds_refresh()` 只刷新 `Data_Storage_Size`/
`Parameter_Checksum` 而不物化镜像。完整暂存缓冲只用于下载和
`iolink_ds_build_image()`。`IOLINK_DS_IMAGE_MAX`（默认 256）限制一次序列化的大小。

`iolink_ds_check()` 需要主站提供的校验和，协议栈无法自行获取，因此必须由应用或
主站侧集成驱动。

## 应用持久化契约

库不拥有 NVM，也不规定存储格式。持久化完全由应用负责（完整约定与示例见
[STORAGE.md](STORAGE.md)）：

1. **参数值**：表项声明 `IOLINK_OD_WRITE_STORE` 时，写入只落到后端（应用变量）；
   落盘时机和格式由应用决定。表项的 `on_change` 钩子在写入成功后调用（`TRIGGER`
   语义下它是动作本身），应用在此通知后自行编码并写入自己的 NVM。
2. **DS 备份**：Data Storage 的上传/下载是主站与设备之间的备份机制，不是本地
   持久化。下载写回每条记录都会触发 `on_change`，应用可以立即落盘或延迟合并。
   库不把参数镜像写入 NVM，也不在启动时恢复任何值。
3. **DS_UPLOAD_FLAG**：通过 `config.ds_flag_persist` 通知应用；应用保存该标志并在
   启动时读取。若保存的值为 true，应用调用 `iolink_ds_request_upload()` 重新置位
   （该调用会再次触发一次钩子），使主站观察到待上传请求。
4. **标签**：三个标签是普通应用参数（见“标签由应用提供”），声明
   `IOLINK_OD_STORAGE_DS` 后进入 DS 备份集，持久化仍由应用在 `on_change` 中完成。
5. **启动恢复**：库不读取任何 NVM，也不提供加载入口。应用在启动时把自己保存的值
   写回 OD 表项指向的变量（或 HANDLER 后端），或用 `iolink_od_restore_value()` 写回
   表项，再调用 `iolink_ds_refresh()` 让 `Data_Storage_Size`/`Parameter_Checksum`
   反映恢复后的参数集。
6. **出厂复位**：`iolink_ds_factory_reset()` 只改 RAM；非 DS 参数由应用自行复位，
   默认值的落盘同样由应用决定。

`on_change` 只看到表项和已接受的线上字节，不接收请求的子索引；需要区分元素时按
`entry->subindex`/`max_subindex` 判断。返回正值为 ISDU ErrorCode，会被原样上送；
返回错误时标量回滚，字节串不回滚。

## 错误码

```c
typedef enum {
    IOLINK_DEVICE_INIT_OK = 0,
    IOLINK_DEVICE_INIT_ERR_NULL = -1,
    IOLINK_DEVICE_INIT_ERR_MSEQ_TYPE = -2,
    IOLINK_DEVICE_INIT_ERR_PD_IN_LEN = -3,
    IOLINK_DEVICE_INIT_ERR_PD_OUT_LEN = -4,
    IOLINK_DEVICE_INIT_ERR_OD_WIDTH = -5,
    IOLINK_DEVICE_INIT_ERR_OD_CAPACITY = -6,
    IOLINK_DEVICE_INIT_ERR_BAUDRATE = -7,
    IOLINK_DEVICE_INIT_ERR_MSEQ_LAYOUT = -8,
    IOLINK_DEVICE_INIT_ERR_TIME = -9,       /* time_us 为 NULL */
    IOLINK_DEVICE_INIT_ERR_CRITICAL = -10,  /* 仍在使用 weak 临界区默认实现 */
    IOLINK_DEVICE_INIT_ERR_PD_INPUT = -11,  /* 回调路径上调用轮询更新 */
    IOLINK_DEVICE_INIT_ERR_PHY = -12,       /* 必需的 PHY 钩子为 NULL 或 init 失败 */
    IOLINK_DEVICE_INIT_ERR_WORKSPACE = -13, /* IOLINK_WORKSPACE_EMBEDDED=0 且没有工作区 */
    IOLINK_DEVICE_INIT_ERR_OD = -14,        /* 应用对象字典表存在未定义/自相矛盾配置 */
    IOLINK_DEVICE_INIT_ERR_APP = -15,       /* config.app == NULL（缺少应用 trait） */
    IOLINK_DEVICE_INIT_ERR_DEVICE_INFO = -16, /* config.device_info == NULL（设备没有默认身份） */
} iolink_device_init_result_t;
```

ISDU 附加错误码（`IOLINK_ISDU_ERROR_*`，例如 `IDX_NOTAVAIL` `0x11`、
`SUBINDEX_NOT_AVAIL` `0x12`、`WRITE_PROTECTED`/访问拒绝 `0x23`、
`FUNCTION_NOT_AVAIL` `0x35`）定义在 `include/iolinki/protocol.h`。

## 平台抽象

`iolink_device_app_t::time_us` 是 `iolink_device_init()` 的必需项（`NULL` 以
`IOLINK_DEVICE_INIT_ERR_TIME` 拒绝），协议栈用它完成全部 DLL、ISR 和事件计时。没有
单独的平台时间 API；设备层只创建一个 `iolink_clock_t`（回调 + 实例指针，
`shared.clock`），事件引擎的 `ctx->clock` 指向它。直接使用 DLL 的调用方必须在
`iolink_dll_init()` 之后自行绑定 `ctx->shared.clock`；直接使用事件引擎的调用方把
`ctx->clock` 指向自己的 `iolink_clock_t`（`NULL` 读作时间 0）。临界区
使用 `include/iolinki/port.h` 中的 `iolink_critical_enter()` /
`iolink_critical_exit()`；`iolink_device_init()` 以 `IOLINK_DEVICE_INIT_ERR_CRITICAL`
拒绝库自带的 weak no-op 默认实现。

## 构建配置

所有编译期开关、档位和容量覆盖见 [feature-trimming.md](feature-trimming.md)。传输粒度
由 `IOLINK_ISR_RX_MODE` / `IOLINK_ISR_TX_MODE` 选择（BYTE 或 FRAME）。初始化期配置
自检由 `IOLINK_FEATURE_VALIDATE`（默认 1）控制。

协议兼容开关 `IOLINK_COMPAT_V10_STARTUP_OPERATE`、
`IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE` 和 `IOLINK_DS_LEGACY_IMAGE_SUB0` 是运行时位的
构建期默认值：实际行为按实例通过 `iolink_stack_config_t::compat_flags` 选择。
`IOLINK_FEATURE_*`、`IOLINK_PHY_FEATURE_ISR_TX_ONLY` 和
`IOLINK_PHY_FEATURE_TX_BUSY` 在编译期生效。唤醒检测、SIO C/Q 和 PD-input 路径没有
构建开关：唯一的选择依据是 PHY vtable 和 `on_pd_input` 是否存在。

## 日志

协议栈包含打印点，但不包含输出实现。每个上下文携带一个 `iolink_log_sink_t`
（回调 + user 指针 + 运行时级别），因此由应用决定输出去向和详细程度：

```c
#include <stdarg.h>
#include <stdio.h>

static void my_log(iolink_app_t* self, iolink_log_level_t level, const char* format, ...)
{
    (void) self;
    (void) level;
    va_list args;
    va_start(args, format);
    (void) vfprintf(stderr, format, args);   /* 或 RTOS/UART logger */
    va_end(args);
}

/* 在应用的 trait 中提供回调 */
static const iolink_device_app_t g_app = {.self = (iolink_app_t*) &g_ctx_state, .log_write = my_log};
config.app = &g_app;
config.log_level = IOLINK_LOG_LEVEL_INFO;
```

`IOLINK_LOG_LEVEL_SILENT`（默认）或 trait 的 `log_write == NULL` 保持静默。
`iolink_device_set_log(ctx, write, level)` 在运行期更换汇点；`self` 参数仍为
`app->self`。设备只有一个 sink（`shared.log`），ISDU、DS 和事件上下文只保存指向它
的指针，因此一次 set_log 就更新全部模块；直接使用这些模块的调用方把 `ctx->log`
指向自己的 sink 即可，NULL 表示静默。

库不格式化也不缓存任何内容：回调收到 printf 风格的格式串和参数，只有通过运行时
阈值时才求值。ISR 打印点使用单独的 `IOLINK_LOG_ISR_*` 系列，只有
`IOLINK_LOG_ENABLE_ISR` 为 1 时才编译进去；ISR 中使用的汇点必须非阻塞且 ISR 安全。

## 线程安全

每个上下文只有一个主上下文调用者：数据 ISR（`iolink_device_process_isr`）可以抢占
该调用者，但其他线程或任务不得进入 API。库不获取应用线程锁；它唯一使用的互斥是
CPU 级的 `iolink_critical_*`（IRQ 屏蔽）。也不要用自己的锁包裹入口：临界区粒度由
库自己掌握。`iolink_device_init()` 会验证平台临界区是真实实现。多个设备实例通过为
每个实例使用独立的 `iolink_device_ctx_t` 来实现，各自拥有自己的主上下文调用者和
数据 ISR。
