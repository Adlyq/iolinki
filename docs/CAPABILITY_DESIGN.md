# 能力表化设计稿（P6）

> 本文是 [EVOLUTION_ROADMAP.md](EVOLUTION_ROADMAP.md) §7 P6 的设计输入，也是 **DG3（结构配置清单定稿）**
> 的评审材料。路线图给出目标与决策，本文只回答"具体怎么分、分完矩阵长什么样、ROM 怎么量、
> 按什么顺序动"。两者冲突时以路线图的决策记录（D1–D19）为准，并回写路线图。
>
> 状态：**设计稿（未实现）**。依赖 P4（对象字典）与 P5（reply plan）落地——D13 要求矩阵收敛
> 必须等它们删掉一轮 `#if` 之后再做，否则审计对象会变。

## 1. 审计口径与结论摘要

### 1.1 口径

路线图的"198 处 `#if`"是**预处理条件出现的次数**，不是开关数量。按以下方式去重后才是本文的
审计对象（`grep` 覆盖 `src/`、`include/` 的 `#if/#ifdef/#ifndef/#elif`）：

| 类别 | 数量 | 处置 |
|---|---|---|
| 预处理条件（`#if` 126 + `#ifdef` 5 + `#ifndef` 67） | 198 | 路线图 §2.3 基线，与之对齐 |
| 减：include guard（`#ifndef IOLINK_*_H`） | −28 | 与能力无关 |
| 减：`#ifndef` 形式的默认值定义（`config.h` 33、`log.h` 1、`port.h` 4、其余 1） | −39 | 是"定义"不是"分支" |
| = 真正的条件分支 | 131 | 审计的物理对象 |
| 其中出现的 `IOLINK_*` 宏标识符（去重） | 49 | — |
| 减：档位取值常量（`IOLINK_CONCURRENCY_{SINGLE,ISR,SMP}` 3、`IOLINK_FENCE_{COMPILER,DEVICE,SMP}` 3、`IOLINK_ISR_{RX,TX}_{BYTE,FRAME}` 4、`IOLINK_STATS_{SHARED,SPLIT}` 2） | −12 | 编码，不是开关 |
| 减：派生量（`IOLINK_NEED_CRITICAL`、`IOLINK_NEED_FENCE`） | −2 | 由档位推导 |
| 减：端口实现宏（`IOLINK_FENCE_DEVICE_IMPL`、`IOLINK_FENCE_SMP_IMPL`） | −2 | 端口契约 |
| **实际能力开关** | **33** | 见 §3 |
| （另有 3 个非 `IOLINK_` 前缀的编译器/标准库探测（`__GNUC__`、`__CC_ARM`、`__STDC_VERSION__`），不计入） | — | 非能力开关 |
| 加：不在条件中但属同一决策面的旋钮（`IOLINK_PLATFORM`、`IOLINK_TINY_PROFILE`、`IOLINK_ACCESS_LOCKS_IMPLEMENTED`、`IOLINK_DIFF_TRACE`） | +4 | 见 §3 |
| **审计总项** | **37** | 保留 23 / 降级 12 / 退役 2 |

### 1.2 结论摘要

- **保留编译期 23 项**：其中布局型 10 项、中断/并发/统计/ISR 语义 7 项、规范常量 3 项、
  平台/端口/仪表 3 项。它们要么改 `sizeof(ctx)`，要么改 ISR 的代码路径与下界，运行时数据表达不了。
- **降级为运行时数据 12 项**：全部满足"不改变结构体布局、不改变 ISR 颗粒度"。
- **退役 2 项**（`IOLINK_MINIMAL_PROFILE`、`IOLINK_TINY_PROFILE`）：它们是组合包不是能力，
  并入结构配置后消失。
- **矩阵 22 → 4**（结构配置 × 平台），其中 1 个配置额外跑 Debug/Release 两个构建类型。
- **必须保留为编译期的，只剩一条判据**：该开关是否让某个 `struct` 的成员集合发生变化，
  或让 ISR 里被执行的指令集合发生变化。其余一律数据化。
- **ROM 收益不是默认结论**：`libiolinki.a` 的提取粒度是 object，且当前库构建未开
  `-ffunction-sections`，`--gc-sections` 在库内部基本无效；常量能力表本身是 GC 根，
  会把所有 handler 钉住。收益只能靠实测（§6）。
- **差分门会在本次收敛中缩小职责**：结构配置之间的等价性仍由它守；被降级的开关改由
  "期望帧"单元测试守，二者不能互相替代（§7.4）。

## 2. 分类判据

一项开关判为**保留编译期**，当且仅当命中下列任一条（每条都给出可验证的检查方式）：

| 判据 | 含义 | 检查方式 |
|---|---|---|
| L 布局 | 影响任一 `struct` 的成员集合或数组维度，从而改变 `sizeof` | `tests/test_ctx_sizes.c` 的断言会变；`offsetof` 变化 |
| I 中断语义 | 决定临界区/屏障/发布字是否存在，或决定 ISR 里是否出现某类指令 | `IOLINK_NEED_CRITICAL` 派生值变化；`nm -u` 出现/消失 `iolink_critical_*`（`build_matrix.sh` 已有该检查） |
| G ISR 颗粒度 | 决定字节级还是帧级收发、决定 ISR 的算法分支集合 | `dll_isr.h` 成员集合变化；`dll_isr.c` 的分支集合变化 |
| S 统计模式 | 决定计数器槽位是共享还是分上下文 | `iolink_dll_isr_stats_t` 是否内嵌进共享区 |
| K 规范常量 | 规范固定值，不是能力；编译期折叠进比较运算 | 无运行时替代价值，且改动它属于规范变更 |

判为**降级为运行时数据**的条件：不命中以上任何一条，且其取值只影响"走哪个已编译分支"。
这类开关的运行时载体统一进 §4 的能力表，**不再出现在 `#if` 里**。

## 3. 逐项分类表

证据列用 `文件:符号` 定位，不引用行号。

### 3.1 尺寸与布局（`include/iolinki/config.h` §1）

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_PD_IN_MAX_SIZE` | `dll.h:IOLINK_PD_REPLY_MAX` → `dll_shared.h:pd_in[][]` | 保留（L） | 直接决定 PD 应答缓冲二维数组的第二维，改值即改 `sizeof` |
| `IOLINK_PD_OUT_MAX_SIZE` | `dll.h:IOLINK_M_SEQ_MAX_LEN` → `dll_main.h:frame_buf`、`pd_out[]` | 保留（L） | 同上，帧组装缓冲按编译期最大值定尺寸 |
| `IOLINK_OD_MAX_SIZE` | `dll.h:IOLINK_PD_REPLY_MAX`、`IOLINK_M_SEQ_MAX_LEN` | 保留（L） | 同上 |
| `IOLINK_ISDU_BUFFER_SIZE` | `isdu.h:iolink_isdu_ctx_t.buffer/response_buf` | 保留（L） | ISDU 上下文 1488 B 的主体；`src/isdu.c` 另有 `> 238U` 的段限制断言 |
| `IOLINK_DS_IMAGE_MAX` | `storage.h:IOLINK_STORAGE_DS_*`（DS 分区尺寸） | 保留（L） | 同时决定存储分区布局与 DS 序列化上界 |
| `IOLINK_EVENT_QUEUE_SIZE` | `events.h:iolink_events_ctx_t.queue` | 保留（L） | 事件队列深度进结构体 |
| `IOLINK_EVENT_PENDING_MAX` | `events.h` 生命周期槽数组 | 保留（L） | 同时活动事件槽进结构体 |
| `IOLINK_EVENT_MIN_APPEAR_MS` | `src/events.c:iolink_events_process()` 的比较常量 | **降级** | 纯时间阈值，无布局效应；改由 `iolink_stack_config_t` 的事件节承载，默认值保持 50 |
| `IOLINK_EVENT_MIN_REPEAT_MS` | 同上（4 处比较） | **降级** | 同上，默认值保持 1000 |
| `IOLINK_TIMING_ENFORCE_DEFAULT` | `src/dll.c:iolink_dll_init()` 写入 `dll_main.h:enforce_timing` | **降级** | 字段已是运行时状态，宏只是它的初值；能力表提供默认值，宏删除 |

### 3.2 规范固定值（`config.h` §2）

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_T_WU_US` | `src/dll.c` 的 STARTUP 唤醒窗口 → `dll_main.h:wakeup_deadline_us` | 保留（K） | 规范常量（T_WU=80 µs）。P7 把它搬进 timer 表后成为表的初始值，仍不是能力 |
| `IOLINK_T_REN_US` | `src/dll.c:dll_get_t_ren_limit_us()` | 保留（K） | 规范上只有一个值（≤500 µs），`t_ren_override` 已提供运行时放宽通道 |
| `IOLINK_T_DSIO_MS` | `src/dll.c` 的 `dsio_deadline_ms` 赋值 | 保留（K） | 规范区间 60–300 ms；运行时值已在字段里，宏只是默认 |

### 3.3 功能裁剪（`config.h` §3.1）

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_MINIMAL_PROFILE` | `config.h` 便利包；`CMakeLists.txt` 覆盖四个特性项 | **退役** | 组合包不是能力；等价物是结构配置 C3（§5.1） |
| `IOLINK_TINY_PROFILE` | 同上 + 关事件 | **退役** | 同上 |
| `IOLINK_FEATURE_DATA_STORAGE` | `dll_main.h:ds`、`device.h:iolink_device_config_t.storage`、`data_storage.h` 全部声明 | 保留（L） | 删掉 `ds` 会让主上下文缩 376 B（`test_ctx_sizes.c` 实测值），且公开 API 集合随之中断 |
| `IOLINK_FEATURE_SYSTEM_COMMAND` | `src/isdu.c:handle_system_command()` 与 `#if !...` 的拒绝分支 | **降级** | 只换"处理"与"一律拒绝"两条已编译路径，无结构体、无 ISR 影响 |
| `IOLINK_FEATURE_EVENTS` | `dll_shared.h:events`（392 B）、`device.h:iolink_device_get_events_ctx()`、`events.h` 声明块 | 保留（L） | 删掉即改共享区布局与公开 API 集合 |
| `IOLINK_PHY_FEATURE_WAKEUP` | `phy.h:iolink_phy_api_t.detect_wakeup`、`dll_shared.h:dll_phy_has_wakeup()` | **降级** | 能力模型已经存在（NULL = 无能力）；把成员常驻、判定改位掩码即可，库侧上下文布局不变（`iolink_phy_api_t` 是一次性 ABI 变更，见 P6.2） |
| `IOLINK_PHY_FEATURE_SIO_CQ` | `phy.h:set_cq_line` | **降级** | 同上；且当前 DLL 无调用点，纯 vtable 形状问题 |
| `IOLINK_APP_PD_INPUT_CALLBACK` | `application.h:on_pd_input`、`device.h:pd_input_update` 陷阱宏、`device.c:device_config_validate()` | **降级** | 语义上就是"回调存在与否"：`on_pd_input != NULL` 选回调路径，否则轮询。常驻成员 = 一次性 ABI 变更，之后不再需要构建期分裂（代价见 §7.3） |

### 3.4 ISR 传输粒度（`config.h` §3.2）

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_ISR_RX_MODE` | `phy.h` 的 `rx_frame`/`rx_octet` 二选一、`dll_isr.h:isr_rx_buf` 存在性、`dll_shared.h:dll_phy_has_rx()` | 保留（G） | 既改 vtable 形状又改 ISR 独占上下文布局，还决定组帧算法在不在 ISR 里 |
| `IOLINK_ISR_TX_MODE` | `phy.h` 的 `tx_frame`/`tx_octet+tx_done`、`dll.h:IOLINK_PD_IN_BUFFERS` 的输入 | 保留（G） | 同上；且是缓冲区租约策略的输入 |
| `IOLINK_PHY_FEATURE_ISR_TX_ONLY` | `src/dll.c:dll_send()` 的 `#if !...` 回退段 | **降级** | 判据本来就在运行时：同一函数先查 `phy->request_service != NULL`，编译期开关只是把"无钩子 PHY"的失败方式从计数换成删代码。保留回退段，用能力位 `REQUIRE_ISR_TX` 做 init 校验 |
| `IOLINK_PHY_FEATURE_TX_BUSY` | `dll.h:IOLINK_PD_IN_BUFFERS`（3 或 2）、`device.c:device_config_validate()` | 保留（L） | 决定 PD-in 缓冲个数 → 共享区尺寸。P8 把缓冲搬进 workspace 后可改为运行时参数，属 P8 议题 |

### 3.5 并发与统计（`config.h` §3.3、`port.h`）

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_CONCURRENCY` | `port.h:IOLINK_NEED_FENCE/NEED_CRITICAL`、`device.c:device_config_validate()` | 保留（I） | 档位决定是否需要临界区与屏障；`SINGLE` 完全不引用 `iolink_critical_*`（矩阵已有 `nm -u` 断言） |
| `IOLINK_FENCE_LEVEL` | `port.h:IOLINK_FENCE()`、`IOLINK_FENCE_*_IMPL` 要求 | 保留（I） | 屏障强度是平台属性，且 `SMP` 档位强制系统范围屏障 |
| `IOLINK_ATOMIC_UINT_T` | `port.h`、`dll_shared.h:isr_isdu_pub`、`dll_isr.h:isr_isdu_adopted` | 保留（L/I） | 类型宽度进结构体，且 ISR 档位要求 ≥32 位（`port.h` 静态断言） |
| `IOLINK_STATS_MODE` | `dll.h:iolink_dll_isr_stats_t` 内嵌与否 + `IOLINK_STAT_ISR/TOTAL` 宏 | 保留（S/L） | SPLIT 往共享区加 16 B（`test_ctx_sizes.c` 3416 vs 3400）；读取口径也随之改变 |

### 3.6 协议兼容（`config.h` §3.4）

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_COMPAT_V10_STARTUP_OPERATE` | `src/dll.c` 两处 `#if !...`（STARTUP 阶段接受 0x99 捷径） | **降级** | 规范兼容位，无布局/ISR 影响；进 `iolink_stack_config_t` 的兼容位域 |
| `IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE` | `src/dll.c`、`src/dll_isr.c` 的 `#if !...` | **降级** | 同上；注意 ISR 侧也读它 → 必须由 init 解析成实例位，ISR 只见常量指针（§4.3） |
| `IOLINK_DS_LEGACY_IMAGE_SUB0` | `src/isdu.c` 两处 12+3n vs 整镜像载荷 | **降级** | 只影响组帧内容；随 P4 的对象字典项标志一起搬 |

### 3.7 日志、端口、平台、仪表

| 开关 | 证据 | 分类 | 理由 |
|---|---|---|---|
| `IOLINK_LOG_ENABLE_ISR` | `log.h:IOLINK_LOG_ISR_*` → `((void) 0)` 或转发 | 保留（G） | 决定 ISR 里是否出现一次函数指针调用与格式化求值；这是时序/ISR 颗粒度决策，不是数据 |
| `IOLINK_PORT_CONFIG_FILE` | `port.h` 的 `#include IOLINK_PORT_CONFIG_FILE` | 保留（I） | 端口注入机制本身，不是能力 |
| `IOLINK_PLATFORM` | `CMakeLists.txt` 的 cache 变量：选择构建哪些示例（`bare_metal_app` 与 host 示例集）与是否注册 host-only 测试 | 保留（平台轴） | 它不是 C 宏，真正的平台契约是应用强符号覆盖 `src/platform.c` 的弱实现与 `IOLINK_FENCE_*_IMPL`；矩阵用它区分"host 全套"与"最小目标集" |
| `IOLINK_DIFF_TRACE` | `tests/test_helpers.c` 的 mock PHY 追踪钩子、`tools/diff_matrix.sh` | 保留（仪表） | 测试仪表，非产品能力；不参与矩阵 |
| `IOLINK_ACCESS_LOCKS_IMPLEMENTED` | `include/iolinki/protocol.h`；`src/isdu.c` 用它拒绝未实现的锁位并作为 0x000C 的读值 | **降级** | 这是"本设备实现了哪些锁"的能力声明，硬编码在头文件里是错位置；随 P4 进对象字典项（`0x000C` 默认值）+ 能力位 |

## 4. 能力表结构草案

设计目标：**一个能力只有一个判定点**。今天 `dll_phy_has_rx/tx/wakeup()` 已经是这个思路，
但它们读的是编译期选定的 vtable 成员，且三个函数各写一遍 `#if`。P6 把它抬成统一模型。

### 4.1 三层拆分

```c
/* include/iolinki/capability.h —— 新增，P6.0 落地 */

/* 第一层：构建档位。每个固件镜像一份，const，落 .rodata。
 * 只描述"这个二进制被编译成了什么"，不描述硬件。 */
typedef struct
{
    uint16_t abi;         /* IOLINK_CAP_ABI；配合 sizeof 断言做 ABI 版本化 */
    uint8_t  isr_rx;      /* IOLINK_CAP_ISR_RX_BYTE | IOLINK_CAP_ISR_RX_FRAME */
    uint8_t  isr_tx;      /* 同上 */
    uint8_t  concurrency; /* SINGLE | ISR | SMP */
    uint8_t  stats;       /* SHARED | SPLIT */
    uint8_t  pd_in_buffers;
    uint8_t  isr_log;
    uint32_t built;       /* IOLINK_CAPB_DS | _EVENTS | _SYSTEM_COMMAND | ... */
} iolink_build_caps_t;

/* 第二层：实例能力。init 时从 PHY vtable 与设备配置解析，一个位掩码进 ctx。 */
typedef uint32_t iolink_caps_t;
#define IOLINK_CAP_RX              (1UL << 0)  /* 按构建档次探测到的接收钩子存在 */
#define IOLINK_CAP_TX              (1UL << 1)  /* 同上，发送侧 */
#define IOLINK_CAP_WAKEUP          (1UL << 2)
#define IOLINK_CAP_SIO_CQ          (1UL << 3)  /* vtable 有钩子；DLL 当前无调用点 */
#define IOLINK_CAP_TX_BUSY         (1UL << 4)
#define IOLINK_CAP_REQUEST_SERVICE (1UL << 5)
#define IOLINK_CAP_PHY_FAULTS      (1UL << 6)
#define IOLINK_CAP_PD_IN_CALLBACK  (1UL << 7)
#define IOLINK_CAP_SYSTEM_COMMAND  (1UL << 8)
#define IOLINK_CAP_EVENTS          (1UL << 9)
#define IOLINK_CAP_COMPAT_V10_OP   (1UL << 10)
#define IOLINK_CAP_COMPAT_T0_READ  (1UL << 11)
#define IOLINK_CAP_DS_LEGACY_SUB0  (1UL << 12)
#define IOLINK_CAP_REQUIRE_ISR_TX  (1UL << 13) /* 端口声明"我的 PHY 必须由 ISR 发" */
```

第三层是**操作表**（P4/P5 的产物，本文只定义查询面）：OD codec ops、ISDU 服务 ops、
ISR 泵 ops。规则统一为"函数指针 NULL = 不支持"，不再用宏表达存在性。

### 4.2 查询 API

```c
/* 只读、无分配、可在 ISR 调用 */
const iolink_build_caps_t* iolink_caps_build(void);                    /* 单例，ROM */
iolink_caps_t iolink_caps_instance(const iolink_device_ctx_t* ctx);     /* 实例视图 */
bool iolink_caps_has(iolink_caps_t caps, uint32_t bit);                 /* static inline */
bool iolink_caps_built(const iolink_build_caps_t* caps, uint32_t mask); /* 构建特性查询 */
```

解析函数是全库唯一允许出现"两面都看得见"的地方：

```c
/* src/capability.c: 唯一的解析点 */
iolink_caps_t iolink_caps_resolve(const iolink_device_config_t* cfg, const iolink_build_caps_t* b);
```

`resolve()` 内部按构建档位选择要探测哪个成员（`b->isr_rx == FRAME ? phy->rx_frame : phy->rx_octet`），
这是**唯一允许保留 vtable 相关 `#if` 的位置**。

### 4.3 与 `dll_phy_has_*` 的统一

不新增并行机制，而是把现有三个辅助函数改成薄委托（`dll_shared.h`）：

```c
static inline bool dll_phy_has_rx(const iolink_dll_shared_t* ctx)
{
    return (ctx != NULL) && iolink_caps_has(ctx->caps, IOLINK_CAP_RX);
}

static inline bool dll_phy_has_tx(const iolink_dll_shared_t* ctx)
{
    return (ctx != NULL) && iolink_caps_has(ctx->caps, IOLINK_CAP_TX);
}

static inline bool dll_phy_has_wakeup(const iolink_dll_shared_t* ctx)
{
    return (ctx != NULL) && iolink_caps_has(ctx->caps, IOLINK_CAP_WAKEUP);
}
```

`IOLINK_CAP_RX/TX` 在 `resolve()` 里按构建档位+NULL 判定一次性置位，于是
`dll_isr.c`/`dll.c` 里的 `#if IOLINK_PHY_FEATURE_*` 全部消失，而语义完全不变。
统一后的不变量：

- 新增能力 = **一个位 + 一行 resolve + （可选）一行 init 校验**；
- 判定点唯一：不允许在别处写 `phy->xxx != NULL`；
- ISR 内**不查位掩码**：init 时把（构建档位 × 运行时能力）折叠成
  `const iolink_isr_ops_t* ctx->isr_ops`，ISR 只做一次间接调用。位测试放在每字节路径上
  会给 P9 的 WCET 分析增加一个无意义的变体（间接调用的目标集合仍是 2–3 个有界实现）。

### 4.4 一致性校验（防"声明了但没编译进去"）

降级后的最大风险是：运行时能力位被置位，但对应实现被裁剪掉了，于是静默走空路径。
加一道 init 门（`device.c:device_config_validate()` 内，返回新的负码）：

```c
/* 例：实例声明了事件能力，但构建裁掉了事件引擎 */
if (iolink_caps_has(caps, IOLINK_CAP_EVENTS) && !iolink_caps_built(b, IOLINK_CAPB_EVENTS)) {
    return IOLINK_DEVICE_INIT_ERR_PROFILE; /* -16 */
}
```

该错误码可被单元测试直接命中，不依赖构建矩阵。

### 4.5 成本

| 项 | 增量 | 说明 |
|---|---|---|
| ROM | 约 16 B（build caps 常量）+ 每能力 1 行 resolve | 与"每个能力删掉整段代码"互抵，方向不定，靠 §6 实测 |
| RAM | +4 B（`ctx.caps`） | 可复用现有填充；`test_ctx_sizes.c` 需同步 |
| ISR | +1 次间接调用（`isr_ops`） | 比现状的 `#if` 少一个变体，WCET 集合更小 |
| 可读性 | `#if` 从 198 降到个位数 | 代价是"哪些路径被编译进去"从源码可见变为 ROM 表可见 |

## 5. 矩阵收敛清单

### 5.1 推荐的 4 个结构配置

矩阵的两条轴：**结构配置**（编译期决定布局与 ISR 形态）× **平台**。构建类型（Debug/Release）
不算配置，作为构建变体挂在 C1 上。

| # | 名称 | ISR 粒度 | 并发/统计 | 功能裁剪 | 平台 | 构建类型 | 测试 |
|---|---|---|---|---|---|---|---|
| C1 | `host_default` | BYTE/BYTE | ISR + COMPILER 屏障 / SHARED | 全开 | LINUX | Debug + Release | 全套 + 差分基线 |
| C2 | `host_frame` | FRAME/FRAME + `TX_BUSY` | ISR / SHARED | 全开 | LINUX | Debug | 编译 + FRAME fixture 测试 |
| C3 | `target_minimal` | BYTE/BYTE | SINGLE / SHARED | 无 DS、无事件、SystemCommand 运行时关 | BAREMETAL（host 编译器做构建门；尺寸用 arm-none-eabi `-Os` 另测，§6） | Release | 编译 + `nm -u` 断言 + 尺寸测量 |
| C4 | `host_smp_split` | BYTE/BYTE | SMP + SMP 屏障 / SPLIT | 全开 | LINUX | Debug | 全套（覆盖两种统计布局） |

覆盖关系：C1 覆盖 `SHARED` + 2 缓冲；C2 覆盖 FRAME 路径 + 2 缓冲（`tx_busy` 释放租约）；
C3 覆盖"最小布局 + 无临界区符号"（`build_matrix.sh` 已有 `nm -u` 断言，并入 C3）；
C4 覆盖 `SPLIT`（+16 B）与 SMP 屏障。

**22 → 4 的判别依据**：剩下的维度彼此正交且都必须被至少一个配置实例化——
{布局：全功能 / 最小}、{ISR：BYTE / FRAME}、{统计：SHARED / SPLIT}、{并发：ISR / SMP / SINGLE}、
{平台：host / 裸机}。四个配置刚好把每个维度的每个取值都碰到一次，且没有一个配置是纯重复。

### 5.2 逐个配置的去向

| 原配置 | 处置 | 如何被覆盖 | 测试改写 |
|---|---|---|---|
| `default` | → C1 | 不变 | 不变（差分门基线） |
| `release` | → C1 的 Release 变体 | 构建类型轴，不占配置名额 | 不变 |
| `isr_frame_frame` | → C2 | 合并进"整帧 + `tx_busy`"；非 `tx_busy` 的 3 缓冲布局由组合探针覆盖（§5.3） | 编译-only（现状已是） |
| `isr_frame_busy` | → C2 | 成为 C2 的定义 | `test_tx_busy` 的 fixture 库（`tests/CMakeLists.txt:iolink_tx_busy_fixture`）保留，改为链接 C2 的库 |
| `isr_frame_byte` | 组合探针 | 不再产出镜像 | `cc -fsyntax-only` 覆盖（§5.3） |
| `isr_byte_frame` | 组合探针 | 同上 | 同上 |
| `isr_tx_only` | 运行时 | `IOLINK_CAP_REQUIRE_ISR_TX` 位 + `phy.request_service == NULL` 的既有回退分支 | 新增 `test_phy_no_request_service.c`：构造无钩子 PHY，断言 `isr_tx_dropped` 与回退行为；原 `-DIOLINK_PHY_FEATURE_ISR_TX_ONLY=1` 的编译断言删除 |
| `minimal_profile` | → C3 | 结构配置即组合包 | `test_config_trim.c` 改为断言"C3 预设展开出的能力表内容"，不再断言宏 |
| `tiny_profile` | → C3 | 同上（C3 取 tiny 的最小面） | 同上 |
| `no_data_storage` | 保留编译期维度，不单独成配置 | 仅 C3 实例化"无 DS"；`DS 有/无 × 事件有/无` 的混合组合由组合探针覆盖 | `test_config_trim.c` 拆分：DS 关闭时断言 API 不存在（编译期），不再依赖矩阵配置 |
| `no_events` | 同上 | 仅 C3 | 同上 |
| `no_system_command` | 运行时 | `IOLINK_CAP_SYSTEM_COMMAND` 位；拒绝分支常驻 | 新增 `test_system_command_disabled.c`：运行时关位后逐条命令断言 `FUNC_NOTAVAIL`；`test_system_commands.c` 保持全开用例 |
| `no_wakeup` | 运行时 | `detect_wakeup == NULL` → `IOLINK_CAP_WAKEUP` 清零 | 由 `test_dll_isr`/`test_sio_fallback` 增加"无钩子 PHY"用例覆盖 |
| `no_sio_cq` | 运行时 | `set_cq_line == NULL` → 位清零 | 无调用点，只需能力查询断言 |
| `compat_v10` | 运行时 | `iolink_stack_config_t` 兼容位 | 新增 `test_compat_v10.c`：置位后按字节断言接受 0x99 捷径的应答；原差分门 `compat_v10` 允许差异条目随之删除（**注意 §7.4**） |
| `ds_legacy_sub0` | 运行时 | 同上（DS 子索引 0 兼容位） | `test_ds.c` 增加两组用例：置位/清零各断言 12+3n 与整镜像载荷 |
| `baremetal` | → C3 的平台轴 | 不变，成为 C3 定义的一部分 | 不变（编译-only） |
| `single_profile` | → C3 | 并发档位降级为"最小配置的取值" | `test_port_capabilities` 改由 C3 构建；`nm -u` 断言迁到 C3 |
| `smp_profile` | → C4 | 与 `stats_split` 合并 | `test_port_capabilities`、`test_stats_split` 由 C4 构建 |
| `stats_split` | → C4 | 同上 | `test_stats_split.c` 保留双模式 `#if`（它本来就按 `IOLINK_STATS_MODE` 分支） |
| `pd_input_callback` | 运行时 | `on_pd_input != NULL` 选路径 | `test_app_pd_callback.c` 不再需要 `iolink_pd_cb_fixture` 独立库；改为同一二进制内两组用例（有/无回调），并断言混用时的错误码 |
| `log_active` | 移出矩阵 | 改为 `tests/` 内的 fixture 库（与 `tx_busy`/`pd_cb` 同模式） | 新增 `iolink_log_isr_fixture`，`test_log.c` 已把 `test_log_isr_on.c` 编进同一可执行文件，改为链接该 fixture |

### 5.3 组合探针（保住覆盖面，不占用矩阵名额）

被移出矩阵、但在编译期仍合法的组合必须继续被**编译**（不是被链接/运行）。用一个廉价的
语法检查循环替代整镜像构建：

```
组合清单（4 个 ISR 粒度 × 2 个统计模式 × 4 个功能裁剪组合中未实例化的部分）
  → 对 dll.c / dll_isr.c / isdu.c / device.c 各跑一次 cc -fsyntax-only + 严格警告
```

实现的硬要求：探针只编译不链接，因此不能发现未定义符号缺失；作为补偿，每个组合仍需
至少一次 `sizeof` 断言（`test_ctx_sizes.c` 的宽度无关断言已提供骨架，按组合补精确值）。
探针失败必须是 CI 红，而不是"只记录"。

### 5.4 必须保留为编译期的清单（不可再降级）

| 项 | 不可降级的理由 | 若强行数据化的代价 |
|---|---|---|
| `IOLINK_ISR_RX_MODE`/`IOLINK_ISR_TX_MODE` | vtable 成员二选一 + `isr_rx_buf` 存在性 | 两条收发算法都编译进 ISR，WCET 取最坏，且每字节多一次分支 |
| `IOLINK_FEATURE_DATA_STORAGE` | 376 B 的主上下文字段 + 公开 API 集合 | 常驻 DS 上下文，最小配置失去意义 |
| `IOLINK_FEATURE_EVENTS` | 392 B 的共享区字段 + 公开 API 集合 | 同上 |
| `IOLINK_CONCURRENCY`/`IOLINK_FENCE_LEVEL`/`IOLINK_ATOMIC_UINT_T` | 决定是否引用临界区符号与屏障实现 | `SINGLE` 无法再证明"零临界区依赖" |
| `IOLINK_STATS_MODE` | 共享区字段存在性 | 所有配置付 +16 B |
| `IOLINK_PHY_FEATURE_TX_BUSY` | PD-in 缓冲个数 | 所有配置付 1 个缓冲（约 65 B） |
| 缓冲/队列尺寸 7 项 | `sizeof` 与存储分区布局 | 运行时变长需要动态内存（违反项目约束） |
| `IOLINK_LOG_ENABLE_ISR` | ISR 内是否出现 sink 调用 | 运行时关位仍要在 ISR 里判一次，换不到 ROM |

## 6. ROM / GC 测量方法

### 6.1 交付形态与 GC 粒度的事实

必须先对齐事实，否则收益预期会错：

1. CMake 构建产物是 `add_library(iolinki STATIC ...)` → `libiolinki.a`，每个 `src/*.c` 一个 object。
2. **当前库构建没有开 `-ffunction-sections`/`-fdata-sections`**（全仓库只有
   `examples/stm32g0_tiol112/build.sh` 开了）。这使 `--gc-sections` 在库内部**以 object 为粒度**，
   即路线图所说的"基本失效"成立。
3. 即便开了 `-ffunction-sections`，**常量能力表是 GC 根**：表里每个函数指针都会把对应实现
   钉住。所以"能力表 + gc"能回收的是"表里没列出的实现"，不是"表里列了但运行时没走的实现"。
4. `.a` 的提取粒度由链接期未定义符号决定：只要 `iolink_device_init` 被引用，`device.o` 进来，
   它引用的 `dll.o`、`isdu.o`… 依次进来，级联到几乎全部 object。**源码集成与 `.a` 在
   "全引用"场景下差异很小**，差异主要出现在"应用只用一小部分 API"时。

结论：ROM 收益的**唯一**可信来源是实测两形态 + 逐能力边际成本，不能靠推理。

### 6.2 两种交付形态的测法

以 `examples/stm32g0_tiol112` 的编译参数为基准（`-mcpu=cortex-m0plus -mthumb -Os`，
Cortex-M0+ 无 LTO），构造一个最小应用（只 `iolink_device_init` + `iolink_device_process`）：

```bash
# A) 源码集成：所有 .o 直接上链接行（现 examples 的做法）
arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -std=c99 -Os \
    -ffunction-sections -fdata-sections -Iinclude -c src/*.c
arm-none-eabi-gcc ... -Wl,--gc-sections -Wl,-Map=map-src.txt -o size-src.elf

# B) 静态库：同一个 .o 集合打成 .a，应用链接该 .a
arm-none-eabi-ar rcs libiolinki.a *.o
arm-none-eabi-gcc app.o -L. -liolinki -Wl,--gc-sections -Wl,-Map=map-ar.txt -o size-ar.elf
```

每个形态再各跑一次**不带** `--gc-sections` 的链接，得到 4 个数据点。只有这样才能把
"gc 没生效"与"代码本来就这么大"分开。

统计口径固定为：`arm-none-eabi-size -A` 的 `.text` + `.rodata` 之和（Flash），
`.data` + `.bss` 之和（RAM）；`-g` 关闭（debug 段不参与），`--file-prefix-map` 消除路径差异。

### 6.3 逐能力边际成本（决定"降级是否值得"的关键表）

降级一个开关会让原本被 `#if` 删除的分支常驻。要判断值不值，需要**单变量差分**：

```
对每个待降级能力 X：
  构建 A = C1 全开
  构建 B = C1 全开 + IOLINK_CAP_BUILD_<X>=OFF（仅供测量的内部开关）
  Δtext(X) = size(A) - size(B)
```

`IOLINK_CAP_BUILD_<X>` 是**测量专用**的构建开关（默认 ON），实现上就是原来的
`IOLINK_FEATURE_*` 换个名字保留一个版本；P10 审计后删除。建议保留，
否则以后无法回答"这个能力占多少 ROM"。

验收规则（建议写入 DG3）：

| 判据 | 阈值 | 不满足时的动作 |
|---|---|---|
| 单个降级能力的 Δtext | ≤ 全库 `.text` 的 0.5% | 改为保留编译期 |
| 全部降级能力的 Δtext 之和 | ≤ 全库 `.text` 的 3% | 分批降级，先做 Δtext 最小的 |
| `.data`/`.bss` | 目标构建（不含 `src/phy_virtual.c` 这个 host-only PHY）除 `src/platform.c:g_critical_default_used`（1 B）外必须为 0 | 立即失败（同时守住"无全局"约束） |
| `SINGLE` 配置的 `nm -u` | 不含 `iolink_critical_*` | 立即失败（已有检查，迁到 C3） |

### 6.4 基准与 CI 落法

新增两个文件（脚本用 Python，与 `tools/*.py` 的既有风格一致；`check_quality.sh` 的
shellcheck 列表不用改）：

**`tools/size_baseline.csv`** —— 人类可读、diff 友好、进版本库：

```csv
# 由 ./tools/size_matrix.py --update 生成；不要手改数字
# toolchain: arm-none-eabi-gcc 13.2.1 20231009
# opt: -Os; lto: off
config,delivery,gc,text,rodata,data,bss,text_tol
c1_full,source,on,0,0,0,0,256
c1_full,archive,on,0,0,0,0,256
c1_full,source,off,0,0,0,0,256
c3_minimal,source,on,0,0,0,0,256
c3_minimal,archive,on,0,0,0,0,256
cap_system_command_off,source,on,0,0,0,0,64
cap_pd_input_callback_off,source,on,0,0,0,0,64
```

**`tools/size_matrix.py`** —— 三个子命令：

- `--update`：重建并覆写基线（只在有意变更时手工执行，PR 里必须单独一个 commit）；
- `--check`：重建并与基线比较，`text` 增长超过 `text_tol` 即失败，缩小永远通过；
- `--table`：打印"配置 × 形态 × gc"的表格，作为 CD 工件上传（artifact）。

CI 落点（`.github/workflows/ci.yml` 新增 `size-baseline` job）：

1. 依赖 `gcc-arm-none-eabi`（`stm32g0-example` job 已安装，复用同一镜像/版本）；
2. 在 `build-matrix` 之后运行，先 `--check` 再 `--table`，`--table` 的 stdout 上传为 artifact；
3. **灵敏度自检**：`--check --selftest` 注入一条 +1024 B 的伪造基线条目，要求脚本返回非零
   （与差分门的"注入差异必须变红"同款要求，防止门本身失效）；
4. 工具链版本写进基线文件头注释；版本不匹配时 `--check` 警告而非失败（跨版本比数字无意义），
   但在同版本内严格比较。

本轮 P6 **不设 ROM 上限**（不知道基线），只设"相对基线不增长"与"Δtext 预算"两条。

## 7. 迁移顺序与风险

### 7.1 顺序（每一步都是可独立回滚的 PR）

| 步 | 内容 | 为什么在这个位置 | 验收 |
|---|---|---|---|
| P6.0 | 能力表脚手架：`capability.h/.c`、位定义、`resolve()`、`iolink_caps_*` API、`dll_phy_has_*` 改委托、`IOLINK_DEVICE_INIT_ERR_PROFILE` | 零行为变化，先建立"唯一判定点"，后面的降级才有落点 | 差分门绿；`#if` 数不变；`test_ctx_sizes` 只 +4 B |
| P6.1 | 值型降级：`COMPAT_*`、`DS_LEGACY_SUB0`、两个事件常量、`TIMING_ENFORCE_DEFAULT`、`SYSTEM_COMMAND` | 只换分支条件，不动布局；每个开关一条期望帧测试就能兜住 | 每项单独提交 + 差分门；新增 3 个测试 |
| P6.2 | vtable 能力统一：`WAKEUP`、`SIO_CQ`、`PD_INPUT_CALLBACK` | 一次性 ABI 变更（`iolink_phy_api_t` / `iolink_app_callbacks_t` 常驻成员），必须与版本号/`CHANGELOG` 同步 | 全量测试；`test_app_pd_callback` 合并进主库；ABI 变更记录 |
| P6.3 | `ISR_TX_ONLY` 降级 + `ACCESS_LOCKS` 进对象字典 | 依赖 P6.0 的能力位；`ACCESS_LOCKS` 依赖 P4 的表项 | 新测试：无 `request_service` 的 PHY |
| P6.4 | 矩阵收敛：改写 `build_matrix.sh`、`diff_matrix.sh` 的中性化配置表、组合探针、测试改写 | 必须等 P6.1–P6.3 全部落地，否则删配置会丢覆盖 | 4 配置全绿 + 探针绿 |
| P6.5 | 尺寸门落 CI（§6.4），跑出第一批 Δtext | 放在最后，因为前面的改动会改变基线 | `--check` + `--selftest` 绿 |
| P6.6 | 文档回写：`feature-trimming.md`、`MEMORY_GUIDE.md`、`API_INTEGRATION.md`、`PORTING.md` 的开关表 | 文档只在事实稳定后改，避免二次修正 | 文档数字与 `test_ctx_sizes` / 基线一致 |

### 7.2 哪些 `#if` 最后动

按"改错的代价"排序，**最后动**的是下面这四组（它们决定了 `sizeof` 或 ISR 的指令集合）：

1. `src/dll_isr.c` + `include/iolinki/dll_isr.h` 的 `IOLINK_ISR_RX_MODE`/`IOLINK_ISR_TX_MODE`
   （`dll_isr.c` 15 处）——ISR 算法与独占上下文布局；
2. `include/iolinki/dll.h:IOLINK_PD_IN_BUFFERS` 的 `TX_BUSY` 分支——缓冲个数；
3. `include/iolinki/dll.h:IOLINK_STATS_MODE` + `dll_shared.h:isr_stat`——共享区布局与统计口径；
4. `dll_shared.h:events`、`dll_main.h:ds` 的 `FEATURE_*` 守卫——最大的两块上下文。

这四组在 P6 里**不被删除**，只被折叠成结构配置的取值。真正被删掉的是 §3 标记"降级"的 12 项，
它们的 `#if` 集中在 `src/dll.c:dll_send()`、`src/dll.c` 与 `src/dll_isr.c` 的兼容分支、
`src/isdu.c:handle_system_command()`、`src/device.c:device_config_validate()`，删起来互不耦合。

**剩余 `#if` 预估**：`src/` 内降到约 25 处，其中 15 处在 `dll_isr.c`；`dll.c` + `isdu.c` +
`device.c` 三个核心文件合计只剩 5 处（`dll.c` 的统计模式与 `ISR_TX_MODE` 各 1、`device.c` 的
`NEED_CRITICAL` 与 `TX_BUSY` 各 1、`isdu.c` 的缓冲上限断言 1）。头文件内保留的是 vtable 形状
与被裁剪上下文的守卫。若 DG3 要求 `src/` 也到"个位数"，唯一手段是把 `dll_isr.c` 按粒度拆成
两个 TU（`dll_isr_byte.c` / `dll_isr_frame.c`），由 CMake 选源文件——
`#if` 变 0，代价是两份文件共享同一份前导（需用共享内部头约束，防止漂移）。
**建议先不拆**，等 P6.0 之后实测剩余数量，再在 DG3 上决定（见 §8 未决问题 6）。

### 7.3 "编译期删除 API" → 链接期断言

被降级的 `IOLINK_APP_PD_INPUT_CALLBACK` 不再删除 API，问题消失。但**保留为编译期**的
DS/事件开关仍然会删除 API 与结构体字段，调用方与库的 profile 不一致时行为未定义。
现有机制只有一个单向标记（`application.h:iolink_app_pd_input_callback_profile`），
只能检测一个开关，且只覆盖一个方向。

收口为一套通用机制（`include/iolinki/profile.h`）：

```c
/* 库侧：每个构建定义一个与 profile 同名的符号（名字即诊断信息） */
#if IOLINK_FEATURE_EVENTS
extern const uint8_t iolink_profile_events_on;
#else
extern const uint8_t iolink_profile_events_off;
#endif
/* 每个保留为编译期的布局开关各一对，命名规则 <开关>_<取值> */

/* 应用侧：一个 TU 里写一次 */
IOLINK_PROFILE_ANCHOR(iolink_app_profile); /* 展开为 used 的指针数组，引用全部符号 */
```

要点：

- **名字带方向**：链接错误直接打印 `undefined reference to 'iolink_profile_events_off'`，
  人和 AI 都能一眼看出"应用按 events=off 编译，库是 events=on"；
- `__attribute__((used))` 只是编译器层保险；`--gc-sections` 仍可能把未引用的
  `.rodata` 回收掉，因此锚点放进独立段 `.iolink_profile_anchor`，并在示例链接脚本里
  `KEEP(*(.iolink_profile_anchor))`（host 构建无 gc-sections，`used` 足够）；
- 非 GNU 编译器：`port.h` 已有 `__CC_ARM` 分支，这里补 IAR 的 `__root`；
- **负向探针进 CI**：`tools/check_profile_abi.sh`（照 `tools/check_ownership.sh` 的样式）
  用相反 profile 编译一个最小应用并链接库，断言链接失败。否则这套断言无法证明有效；
- 该机制是**开发期安全网**，不是安全边界：主机制仍是 `CMakeLists.txt` 里的 `PUBLIC` 编译定义。

### 7.4 差分门配置表的同步方式

`tools/diff_matrix.sh` 里的 `CONFIGS` 数组与 `tools/build_matrix.sh` 的配置表是**两份手抄**，
当前靠人守同步。收敛时这是最容易出事的地方，落三条硬规则：

1. **单一事实源**：配置表抽到 `tools/matrix_configs.sh`（被两个脚本 `source`），
   `diff_matrix.sh` 通过 `--list`/变量读取"哪些配置能跑测试"，不再硬编码数组；
2. **陈旧条目即失败**：`diff_matrix.sh` 启动时校验 `tools/diff_allowlist.txt` 里每个
   `配置|测试` 条目都能在配置表与测试二进制清单里找到，找不到直接红
   （现在删掉 `compat_v10` 后，该允许差异条目会变成永不命中的死条目，静默通过）；
3. **职责转移要显式记录**：被降级的开关退出差分门后，其行为**不再**被"跨配置比对"覆盖。
   补偿方式是在 C1 里写"期望帧"测试（`test_compat_v10.c` 等，直接断言字节序列），
   并在 `diff_matrix.sh` 头注释与 `docs/TESTING.md` 里各写一句"结构等价看差分门，
   运行期开关看期望帧测试"。这一步不做，收敛当天就会静默丢掉 `compat_v10` 的覆盖。

另外注意：差分门只跑 BYTE/BYTE 配置（FRAME 的 mock vtable 不存在）。C2 合并后
FRAME 路径仍然**没有行为等价门**，只有编译检查。这是 P6 之前就存在的缺口，
P6 只是把它从"4 个配置"缩到"1 个"，不新增风险；记录在 §8。

### 7.5 MISRA / 静态分析影响

| 影响面 | 具体后果 | 处置 |
|---|---|---|
| 死代码审计（Rule 2.1/2.2） | `#if` 删除的代码对静态分析不可见；降级后同样的代码变成"可达但永不执行"，`cppcheck --enable=style` 的 `unreachableCode` 可能新报 | 能力位必须真的被测试驱动到两个取值；无法覆盖的分支写 `// cppcheck-suppress` 并注明"能力位由构建保证"，与现有 `port.h` 的 cppcheck 抑制同风格 |
| 覆盖率门（G4） | 新增可达分支拉低 `tools/coverage_baseline.txt` 的实测值 → `coverage.sh --check` 先红，不是 MISRA 先红 | 每个降级开关的 PR 必须同时带来命中该分支的测试；否则基线只能下调，等于永久放弃覆盖 |
| Rule 2.5（未使用宏） | 退役 `MINIMAL/TINY` 减少 2 条；能力位宏全部被引用，不新增告警 | 正向收益 |
| Rule 17.7（非 void 返回值必须使用） | 操作表里的函数指针返回值被忽略时需要 `(void)`，属显式偏离 | 在 `CAPABILITY_DESIGN` 对应的实现 PR 里记录偏离项与理由 |
| Rule 10.x（隐式转换） | `1UL << n` 构造 `uint32_t` 掩码，在 `-Wconversion` 下必须显式窄化 | 用 `((uint32_t) 1U << n)` 形式，避免新增告警 |
| Rule 8.4/8.6（声明一致性） | 能力符号必须在单一头文件声明、单一实现定义 | `profile.h` / `capability.h` 各只有一处实现 |
| ISR 分析（P9） | ISR 内新增间接调用会改变 WCET 分析模型 | `isr_ops` 的目标集合必须有界（2–3 个实现），并在 P9 的 WCET 报告里按"最大目标"计 |

## 8. 未决问题

| # | 问题 | 影响 | 建议 / 触发时机 |
|---|---|---|---|
| 1 | **DG3：C1–C4 是否就是最终 4 个** | 矩阵脚本、CI 时长、文档 | 本稿推荐 4 个；若 SMP 没有目标平台需求，降为 3 个（C4 合并进 C1 的统计模式 = SHARED），但 `SPLIT` 布局就没人守 |
| 2 | `IOLINK_STATS_MODE` 是否值得保留双模式 | 保留 → 所有配置可选 +16 B；合并成 SPLIT → 默认配置 +16 B | 需要先量 `SPLIT` 的实际收益（丢一次自增的诊断价值 vs 16 B），属 DG3 子项 |
| 3 | `IOLINK_PHY_FEATURE_TX_BUSY` 是否并入 P8 的 workspace 参数 | 影响 `IOLINK_PD_IN_BUFFERS` 的编译期性质 | 建议留在 P6（编译期），P8 引入 workspace 时再参数化 |
| 4 | 两处"混合 ISR 粒度"组合（FRAME/BYTE、BYTE/FRAME）是否仍在支持范围 | 决定是否需要组合探针、是否需要文档声明"不支持" | 建议明确声明为"不推荐但可用"，用探针保编译 |
| 5 | ROM 预算阈值（0.5% / 3%）是否被接受 | 决定降级能否通过 | 需先跑出第一批 Δtext（P6.5）；若阈值过紧，改为"逐项申报"（每项在 PR 里列出 Δtext，总账在 CHANGELOG） |
| 6 | 剩余 `#if` 是否要拆 `dll_isr.c` 才能到"个位数" | 触及 ISR 文件结构，风险最高 | 建议 P6.0 后实测剩余数量再定；若拆，必须先建立"两个 TU 共享内部头"的防止漂移机制（如 `dll_isr_internal.h` + 交叉静态断言） |
| 7 | 事件两个时间常量降级到 `iolink_stack_config_t` 还是对象字典项 | 影响 P4 表项设计（它们是行为参数，不是可读参数） | 建议进 `stack_config`，不进 OD（OD 是主站可见对象，这两个是设备内部节流） |
| 8 | `IOLINK_CAP_BUILD_<X>` 测量开关的保留期 | 保留 → 多一套构建开关；删除 → 以后无法回答"这个能力占多少 ROM" | 建议保留到 P10 审计，届时连同 `feature-trimming.md` 一起收口 |
| 9 | 能力表 ABI 版本化方式（`abi` 字段 vs 纯链接期断言） | 影响跨版本混用时的诊断质量 | 建议两者都要：结构体内 `abi` 字段做运行期快速失败，符号名做链接期早期失败 |
| 10 | 32 位目标上的能力表尺寸验证 | `test_ctx_sizes.c` 现在只在 64 位主机断言精确值 | 与 P8 的"裁剪配置尺寸打印"合并处理；P6 只补宽度无关断言 |
