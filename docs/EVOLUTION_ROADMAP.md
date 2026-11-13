# 架构演进路线图（数据驱动重构）

> **本文档是本重构计划唯一落盘事实源**。目标读者是"下一次打开仓库、没有对话上下文的执行者"
> （人或者 AI）。执行任何阶段前先通读第 1–6 节；每阶段完成后在 §7 勾选步骤并更新状态。
>
> 方案本身来自一次完整架构评审：结论是这套栈目前属于"能通过审查的正确实现"，远未达到
> "结构上不容易出错的实现"。复杂度不在协议本身，而在四类结构性债（§2）。

## 1. 终极目标与总排序

**终极形态**：协议栈 = 一张 const 对象字典 + 一张状态机转移表 + 一个 ISR 传输执行器 +
一张能力 ops 表。其余产物（IODD、文档、测试用例、traceability）全部从表派生。

总排序（理由详见 §5 各决策 D9–D16）：

```
P0 基线硬化 → P1 差分测试门(H₀) → P2 所有权分区(C) → P3 统一错误模型(E)
→ P4 对象字典(A) → P5 reply plan + ISR 执行器(B) → P6 能力表化(D)
→ P7 截止时间表(F) → P8 工作区分离(G) → P9 状态机表 + WCET 门(H) → P10 收尾(I)
```

一句话：**先建账目和安全网（P0/P1），再定边界（P2/P3），再做两次大语义搬迁（P4/P5），
再收敛配置与时间（P6/P7），最后做内存、证据与收尾（P8/P9/P10）。**

执行记录（2026-11，全部阶段完成）：P0–P10 已全部本地合并到 `own_dev`（无 PR），
随后按第三方 CR（`docs/CR_EVOLUTION_2026-11-12.md`）修正 M1–M9 并删除全部
向后兼容层。最终数据（64 位主机、默认缓冲）：device 4088 B、DLL 3776 B、
ISDU 1328 B、DS 408 B、events 504 B、workspace 1289 B（外置后 device 2792 B）；
84 项 ctest、4 个结构配置 + 7 个组合探针；差分门 2 配置逐字节一致、0 allowlist；
覆盖率下限 90.9%；P9 宿主层 WCET 门与 size/profile 门在 CI；`src/` `#if` 91、
`include/` 120。P6 能力表化的 ROM 成本（最小应用 +972 B / +4.5%）由 DG3 决议
**接受**。每阶段执行细节与验收记录见各阶段小节。

## 2. 背景：四类结构性债

### 2.1 协议知识散落多份

同一个参数集在四处独立存在，互相漂移：

| 位置 | 内容 |
|---|---|
| `src/isdu.c` `handle_mandatory_indices()` | 64 个硬编码 case，各自带长度/子索引规则 |
| 应用 vendor_service | 手写长度校验、大端转换、范围检查、ErrorCode（改造前典型：`switching_sensor_service()`，约 80 行只实现 4 个参数；该包装已在本次清理中删除，示例只保留 OD 表） |
| `iolink_ds_param_desc_t` | DS 可持久化参数列表（`include/iolinki/data_storage.h`） |
| `tools/iodd_gen.py` + device.json | IODD 的第四份描述 |

DS 判断"某索引是否属于 DS 集合"的方式尤其脆弱：`device_ds_read()`（`src/device.c`）
先试 params 上下文，失败再用 vendor_service 发一次"读"来探测——**用有副作用的回调当
元数据查询**。对象字典化之后（P4）此 hack 自然消亡：DS 成员 = 查表项 storage 标志。

### 2.2 跨上下文所有权只靠注释

`iolink_dll_ctx_t` 实测 3376 字节，主上下文状态、ISR 状态、握手标志、配置、统计全在
一个 struct 里，所有权只写在注释里。ISDU 用 `void*` 反向指针（event_ctx / ds_ctx /
dll_ctx），`dll.h` 反向 include events.h / isdu.h / data_storage.h —— 类型层面的循环依赖。
并发正确性目前是"文档承诺"，不是"类型约束"。

### 2.3 编译期组合爆炸

`src/ + include/` 共 198 处 `#if`（isdu.c 23、device.c 18、dll_isr.c 16、dll.c 12、
其余散落），`tools/build_matrix.sh` 维护 22 个配置。但 `include/iolinki/dll.h` 的
`dll_phy_has_rx/tx/wakeup()` 已在用运行时 NULL 判断做能力查询——运行时能力模型
可行且已有先例。

### 2.4 错误语义三套并存

1. `int` = 字节数或 -1/-2/-3（`iolink_params_ctx_get/set`）；
2. `uint8_t` = IO-Link ErrorCode（vendor_service 返回 0x11/0x30/0x33）；
3. 自有枚举（`iolink_device_init_result_t` 的 -1..-15）。

`isdu_respond_param_error()`（`src/isdu.c`）只映射 -2/-3，其余一律压成
`IDX_NOT_AVAIL`——vendor 层辛苦算出的 0x30（值超范围）在 params 路径上会丢失。

## 3. 现状基线（全部实测，2026-11）

| 项 | 值 | 备注 |
|---|---|---|
| `src/isdu.c` | 1863 行 | 传输状态机 + 服务分发 + DPP + DS + 系统命令混合 |
| `src/dll.c` / `src/dll_isr.c` | 2102 / 1158 行 | Table A.10/A.14 解读两份（`dll_isr_handle_type0`、`dll_isr_build_reply_od` vs `dll_handle_page_channel_read`、`dll_handle_operate_type0`、`dll_dispatch_od`） |
| `#if` 计数 | 198（src/+include/） | 分布见 §2.3 |
| 构建矩阵 | 22 配置 | `tools/build_matrix.sh`：default / release / isr_frame_frame / isr_frame_busy / isr_frame_byte / isr_byte_frame / isr_tx_only / minimal_profile / tiny_profile / no_data_storage / no_system_command / no_events / no_wakeup / no_sio_cq / compat_v10 / ds_legacy_sub0 / baremetal / single_profile / smp_profile / stats_split / pd_input_callback / log_active |
| `sizeof(iolink_device_ctx_t)` | 3712 B（改造前基线；config 双源收敛后 3688 B） | 嵌套 `dll` 3376；dll 内含 isdu 1488 / ds 376 / events 392——嵌套尺寸不可相加 |
| `sizeof(iolink_dll_ctx_t)` | 3376 B | 32 个 volatile 字段 |
| 文档偏差 | `docs/MEMORY_GUIDE.md` 写 "DLL context ~1.3 KB" | 与实测 3376 不一致，P0 修正 |
| 缓冲区副本 | ISDU `buffer/response_buf/req_buf/resp_store[2]`；PD 3 份应答缓冲；`isr_tx_payload[3]`；`frame_buf`、`isr_rx_buf`；DS image[256] | P8 目标 |
| 定时器 | `t_pd_deadline_us`、`fallback_deadline_ms`、`dsio_deadline_ms`（`src/dll.c`，含 `dll_apply_isr_deadline_requests`、`dll_check_dsio_deadline`、`dll_check_fallback_deadline`）等 | 混用 us/ms 与 32/64 位，P7 目标 |
| 测试资产 | 68 测试 + virtual master + `tests/conformance_traceability.csv` | P1 差分门的原料 |
| 测试助手 | `tests/test_helpers.c` 的 `g_test_params` / `g_test_device_info` 进程级单例 | P0 去全局化 |

## 4. 方案 A–I 摘要与阶段映射

每条的完整论证（证据、假设、代价）见评审记录；此处保留方向与已定修正，防上下文丢失。

| 方案 | 方向 | 关键设计点（评审修正） | 阶段 |
|---|---|---|---|
| A 对象字典 | 一张 const OD 表：ISDU/DS/IODD/SIO 参数集全部派生；应用只提供 get/set 后端 + 语义钩子 | 事实源必须是**表**，IODD 信息有损（无范围/写触发语义）不可作源；表项必须建模写语义分类（store/trigger/volatile）、访问锁（并入表，device_info 锁变派生查询）、block/stream 标志 | P4 |
| B 双泵收敛 | 主泵发布"应答计划"，ISR 只查 plan→取源→CKS→喂字节 | ISR **保留时序语义**（t_A 何时开始/回多少），不解释内容语义；必须有 plan 失效协议（防"写后立刻读"读旧值） | P5 |
| C 所有权分区 | 拆 `dll_main_t/dll_isr_t/dll_shared_t`，越界访问变编译错误 | **可见性 ≠ 布局**：类型按所有权拆，物理上仍单次分配 embed，`_Static_assert` 锁分区（保 section 放置与缓存局部性） | P2 |
| D 能力表化 | 开关分两类：保留编译期（结构/中断语义）；降级为数据（其余），靠函数指针 NULL + gc 回收 | 非 LTO 下静态库 `.a` 粒度使 gc-sections 基本失效——ROM 收益依赖交付形态，实测定论 | P6 |
| E 统一错误模型 | `iolink_result_t`；正 = ISDU ErrorCode，负 = 内部错误；只在最外层翻译 | 翻译点收敛两处（ISDU 组帧、`iolink_device_init` 返回）；`iolink_device_init_result_t` 并入同一表 | P3 |
| F 截止时间表 | 静态 timer 表 + 单一 next_deadline，process() 常数时间 | t_A/T_DSIO/T_FBD 是**边沿触发可重装**，表需 re-arm/cancel 语义；时钟源保持唯一 `dll_time_us()` | P7 |
| G 工作区分离 | 显式 `iolink_workspace_t`（调用方可提供），状态/缓冲分离，租约模型统一交接 | 租约先切 ISDU response 路径（消灭 `resp_store[2]`）；DS image 流式化依赖 P4 的 storage 标志 | P8 |
| H 机器可验证 | 状态机表化 + 差分门 + ISR WCET 门 + fixture 去全局化 | **virtual PHY 测不了 CPU WCET**——它只验证功能等价/帧序合法；周期上界用目标端 DWT cycle counter；纯表装不下连续条件，用"表管骨架 + 函数管动作 + 性质测试管不变量" | P1/P9 |
| I 小项 | 不透明上下文、config 双源收敛、诊断注册表 | 不透明 = size/align 宏 + placement init（C 固有取舍） | P0/P10 |

## 5. 已定技术决策（评审结论，推翻需在本节记录原因）

- **D1** 事实源格式：C 头为源，脚本导出 IODD（`tools/iodd_gen.py` 反转为"从表生成"）。
  最终格式在 DG2 定稿，倾向 C 表。
- **D2** IODD 信息有损（无 range/validate/写触发语义），任何情况下不得作为事实源。
- **D3** 可见性 ≠ 布局：拆类型但保持单次分配（embed + offsetof 断言），不搞三次独立分配。
- **D4** ISR 保留时序语义（t_A 起答、回多少字节），移除内容语义（M-sequence 解读只留主泵一份）。
- **D5** reply plan 必须有原子发布/失效协议（SPSC ring + 发布字）；OD 写路径失效对应 plan。
- **D6** OD 表项必须含：写语义分类（store/trigger/volatile）、访问锁位、storage 标志、
  block/stream 标志、datatype codec ID、默认值、validate/on_change 钩子。
- **D7** 错误结果码：负 = 内部错误，正 = ISDU ErrorCode 常量表域（uint8 范围足够）；
  翻译点只有两处。
- **D8** WCET 手段分离：CPU 周期上界 = 目标端 DWT；virtual PHY = 功能等价与帧序合法层。
- **D9** 安全网先行：差分门（P1）必须先于任何语义搬迁（P4/P5）存在。
- **D10** P2 在 P4/P5 前：机械重构、零行为变化；P5 的原子发布依赖 P2 的类型分区。
- **D11** P3 在 P4 前：OD 钩子从第一天就用最终结果类型，避免改两次签名。
- **D12** P4 在 P5 前：应答计划的内容来源于 OD 知识（哪些应答可预计算）。
- **D13** P6 在 P4/P5 后：它们会删掉大量 `#if`，矩阵收敛必须等它们落地。
- **D14** P7 在 P5 后：两者都改 `src/dll.c`，串行避免冲突。
- **D15** P8 依赖 P4（storage 标志）与 P5（租约/发布模型）。
- **D16** P9 在 P5/P7 后：状态机骨架稳定才值得表化。
- **D17** characterization tests 先行：P4/P5 重构前先用现有测试 + 差分门钉死现状行为，
  再做表驱动/计划化替换。
- **D18** vendor_service 是公开 API（双许可产品）：DG1 定破坏策略，倾向保留兼容 shim
  一个版本、examples 全量迁移。
- **D19** `examples/switching_sensor` 是 P4 的第一个迁移试点，用于量化"表吃掉多少
  boilerplate"。

## 6. 关键数据结构草案（实现阶段以本草案为起点，可细化）

```c
/* P3：统一结果。code: 0=OK, 负=内部错误, 正=ISDU ErrorCode */
typedef struct { int16_t code; uint16_t len; } iolink_result_t;

/* P4：对象字典表项 */
typedef struct {
    uint16_t index; uint8_t subindex;
    uint8_t  datatype;        /* codec 表 ID：U8/U16/U32/Bool/OctetString/... */
    uint8_t  access;          /* RO/RW/WO + 锁位 */
    uint8_t  write_semantics; /* STORE | TRIGGER | VOLATILE */
    uint8_t  storage;         /* VOLATILE | DS | NVM */
    uint8_t  flags;           /* block 传输标志等 */
    const void* def;          /* 默认值 */
    iolink_od_hook_t validate;  /* 范围/交叉约束，可 NULL */
    iolink_od_hook_t on_change; /* 副作用，可 NULL */
} iolink_od_entry_t;
/* 内建处理器：0x0002 SystemCommand、0x000D、0x0024 DeviceStatus、DPP1/2、强制索引 */

/* P5：应答计划（主泵预发布，ISR 执行） */
typedef struct {
    uint16_t match_mask, match_value; /* 哪些 M-sequence 组合命中 */
    const uint8_t* template; uint8_t len; uint8_t flags;
    uint8_t source;    /* pd_in 槽位 / 静态模板 / staged ISDU 段 */
    uint16_t deadline_us;
} iolink_reply_plan_t;
/* 发布/失效：SPSC ring + 原子发布字 */

/* P2：上下文分区（单次分配内嵌） */
typedef struct { /* 主泵拥有 */ } dll_main_t;
typedef struct { /* ISR 只碰 */ } dll_isr_t;
typedef struct { /* SPSC ring + 发布字 */ } dll_shared_t;
/* composition root 内嵌三者 + _Static_assert 锁 offsetof 分区 */

/* P7：截止时间表 */
typedef struct { uint64_t deadline; uint8_t id; } iolink_timer_t;
/* 静态数组 + next_deadline；re-arm/cancel 语义；时钟源唯一 dll_time_us() */

/* P8：工作区（调用方可提供，DTCM/外部 RAM） */
typedef struct { /* scratch/缓冲，租约模型 */ } iolink_workspace_t;
```

## 7. 阶段计划

每阶段格式：状态 / 目标 / 依赖 / 涉及文件 / 步骤（可勾选）/ 完成标志 / 风险。
所有阶段必须过 §8 通用验收门。

### P0 基线硬化（S，~1 周）——状态：已完成（本地验证；待提交）

- **目标**：把现状事实变成 CI 断言，清理小债，为后续一切提供可信基线。
- **依赖**：无。
- **涉及文件**：`docs/MEMORY_GUIDE.md`、`include/iolinki/device.h`（config 双源）、
  `tests/test_helpers.c` + 各测试文件、`tools/coverage.sh`、`.github/workflows/ci.yml`。
- **步骤**：
  - [x] sizeof CI 断言：dll=3376 / device=3688 / isdu=1488 / ds=376 / events=392
        （SPLIT 变体 +8）写入 `tests/test_ctx_sizes.c`（`IOLINK_STATIC_ASSERT`，
        64 位守卫 + 宽度无关嵌套不变量），尺寸变化必须显式改断言；
  - [x] 修正 MEMORY_GUIDE.md（"~1.3 KB" → 实测值），建立"文档数字由 CI 校验"机制；
  - [x] config 双源收敛：`ctx->config` 指针与 `stack_config` 拷贝只留一份；
  - [x] 测试助手去全局化：`g_test_params` / `g_test_device_info` 文件级单例改为
        "设备工厂"返回完整接线的 ctx，消灭声明顺序耦合；
  - [x] 覆盖率基线：`tools/coverage.sh` 产出基线数字，CI 非回归。
- **完成标志**：G1–G6 全绿；MEMORY_GUIDE 与代码一致；tests/ 无文件级 ctx 单例。
- **风险**：低。测试助手改动是机械替换，但会碰到多数测试文件，分 PR 提交。

### P1 差分测试门 H₀（M，1–2 周）——状态：已完成（本地验证；待提交）

- **实现方式**：不另写 C 夹具，而是给 mock PHY 增加 `IOLINK_DIFF_TRACE`
  帧追踪钩子，复用现有 70 个测试作为序列集；`tools/diff_matrix.sh` 通过
  `build_matrix.sh` 的配置表重建 6 个可跑测试的 BYTE/BYTE 配置，逐字节比较
  13 份设备应答 transcript。允许差异见 `tools/diff_allowlist.txt`
  （当前仅 compat_v10/test_dll，有规范/功能开关依据）。
- **已验证**：同配置两次运行 transcript 完全一致；6 配置 PASS；人为改动
  帧内容会被 `cmp` 捕获。

- **目标**：同一协议序列在所有结构配置下输出帧逐字节一致（除声明差异），进 CI。
  这是 P4/P5 大搬迁的降落伞。
- **依赖**：P0。
- **涉及文件**：新增 `tools/` 差分 runner + 序列集；`tests/` 场景提取；
  `.github/workflows/ci.yml`。
- **步骤**：
  - [x] 从现有 68 测试 + `tools/virtual_master/` 提取标准序列集：覆盖 DLL 状态转移、
        ISDU 读/写/错误路径、PD 流、DS 上传/下载、事件、fallback、系统命令；
  - [x] 差分 runner：序列在多个结构配置下运行，帧逐字节比对；
  - [x] 维护"允许差异清单"（统计字段、兼容开关影响等，逐项注明规范条款依据）；
  - [x] 灵敏度验证：人为注入已知差异，确认门会红；
  - [x] 接入 CI（matrix 之后运行）。
- **完成标志**：门在 CI 运行；注入差异可红；允许差异清单有依据；序列集随新测试自动扩展。
- **风险**：序列集覆盖不足 → 假阴性。缓解：从测试场景自动提取而非手写。

### P2 所有权分区 C（L，2–3 周）——状态：所有权分区已完成（CR 修复后提交）；`void*` 窄接口延后

> **落地实现（2026-11）**：`iolink_dll_shared_t`（共享区，含 `events`/`isdu`
> 子上下文与 `isr` 回指针）、`iolink_dll_main_t`（内嵌 shared + 主上下文字段）、
> `iolink_dll_isr_t`（ISR 独占）分别定义在 `include/iolinki/dll_shared.h`、
> `dll_main.h`、`dll_isr.h`；`dll_ctx.h` 定义单次分配容器
> `iolink_dll_ctx_t { main; isr; }`。`dll.c` 只包含 `dll_main.h`，引用 ISR 字段
> 是编译错误；`dll_isr.c` 同时包含两者；`device.h` 包含 `dll_ctx.h`（组装根）。
> 共享区的 `isr` 回指针在 `iolink_dll_init()` 绑定，使主上下文可以传递 ISR
> 句柄而无法解引用。`tools/check_ownership.sh` 把边界变成 CI 门禁
> （含负向编译探针）。P10.1 不透明设备上下文因此降级为可选项。

- **目标**：并发正确性从注释变成类型约束；头文件依赖图不再环形。
- **依赖**：P1。
- **涉及文件**：`include/iolinki/dll.h`、`include/iolinki/isdu.h`、`include/iolinki/events.h`、
  `include/iolinki/data_storage.h`、`src/dll.c`、`src/dll_isr.c`、`src/isdu.c`、
  `src/events.c`、`src/data_storage.c`、`src/device.c`、`src/dll_internal.h`（已有，扩展）、
  `src/params.c`。
- **步骤**：
  - [x] 头文件重划 public/internal 边界；dll.h 不再反向 include events/isdu/
        data_storage（拆到 `dll_shared.h` / `dll_main.h`）；`dll_phy_has_*` 移到
        `dll_shared.h` 并以共享区为参数；
  - [x] 拆 `iolink_dll_main_t` / `iolink_dll_isr_t` / `iolink_dll_shared_t`；
        `iolink_dll_ctx_t` 单次分配内嵌 `{ main; isr; }`；显式 typedef 断言
        锁共享区首成员与容器顺序（`dll_ctx.h`，与位宽无关）；尺寸契约同步（device 3712 B）；
  - [ ] `void*` 反向指针换窄接口（`iolink_diag_if_t` / `iolink_param_if_t` / ops 表）
        —— 延后到 P4/P5 的接口重构，避免与 OD/plan 改动二次冲突；
  - [x] 全程零行为变化：差分门逐字节一致（本阶段验证）。
- **完成标志**：ISR 头文件只暴露 `dll_isr_*`；主上下文引用 ISR 字段是编译错误；
  差分门逐字节一致；sizeof 变化（如有）同步 CI 断言。
- **风险**：include 图改动牵涉面大 → 分小 PR；MISRA 对函数指针表的约束需确认。

### P3 统一错误模型 E（M，1–2 周）——状态：已完成（本地验证；待提交；提前于 P2 执行）

- **执行说明**：P3 与 P2 无依赖冲突，先做 E 可让后续 OD/plan 从第一天使用最终
  结果类型。采用 DG1 推荐的兼容策略：新 `read/write` 为内部权威，旧
  `get/set` 保留一个版本的 shim；`iolink_device_init_result_t` 暂保留负码域
  （已是负值语义，合并进同一张表不改变行为）。

- **目标**：错误语义只有一种；vendor 层错误码不再被静默降级。
- **依赖**：P2。**入口决策 DG1**。
- **涉及文件**：`include/iolinki/params.h`、`include/iolinki/data_storage.h`、
  `include/iolinki/isdu.h`、`include/iolinki/device.h`、`src/params.c`、`src/isdu.c`、
  `src/data_storage.c`、`src/device.c`、`examples/`、`tests/`。
- **步骤**：
  - [x] 定义 `iolink_result_t` + ISDU ErrorCode 常量表；`iolink_device_init_result_t`
        保留负码域（记录为取舍：已是负值语义，合并表不改变行为）；
  - [x] params ctx、DS 回调迁移到结果模型；`vendor_service` 保持 ErrorCode 返回
        （即结果码正域，无需二次翻译）；
  - [x] 删除 `isdu_respond_param_error()` 的 -2/-3 特判，错误码由结果码携带；
  - [x] 边界翻译收敛：ISDU 组帧的 `isdu_respond_result()` 为唯一翻译点；
        init 结果保持既有负码域；
  - [x] examples 检查：现有示例未使用被改签名，无需迁移。
- **完成标志**：0x30 在 params 路径不再被压成 IDX_NOT_AVAIL（差分门 + 专项测试）；
  文档只讲一种语义。
- **风险**：API 破坏（用户已接受工程量）。vendor_service 返回 ErrorCode 语义保留
  （即结果码正域）。

### P4 对象字典 A（XL，3–4 周）——状态：已完成（本地合并；全量验证通过）

- **目标**：参数知识只有一份事实源（const OD 表）；ISDU/DS/IODD/文档全部派生；
  新增参数不改协议栈代码。
- **依赖**：P3。**出口决策 DG2**。
- **涉及文件**：新增 `include/iolinki/od.h` + `src/od.c`（表 + codec）；
  `src/isdu.c`（表驱动化，传输状态机保留）、`src/device.c`（删探测 hack）、
  `src/data_storage.c`（storage 标志查询）、`tools/iodd_gen.py`（反转为表驱动）、
  `examples/switching_sensor/`（试点迁移）、`tests/`。
- **步骤**：
  - [x] characterization tests：先用现有测试 + 差分门钉死 0x0002/0x000D/0x0024/DPP1/2/
        强制索引现状行为（tests/test_od_characterization.c）；
  - [x] 定义 `iolink_od_entry_t` + datatype codec 表（长度/字节序/范围/错误码唯一实现）；
  - [x] 内建处理器（SystemCommand/Profile/DeviceStatus/DPP1/2/强制索引），表留 hook 位；
  - [x] `handle_mandatory_indices()` 强制索引规则迁入表驱动（od.c），isdu.c 保留传输与
        内建触发器（迁移后 1916 行；CR 修正后 1944 行，净增；规则已单源化，
        行数不是有效指标）；
  - [x] DS 成员判断改表/描述符集合，删除"用 vendor_service 读探测"的 hack；
  - [x] IODD 生成反转：C 表 → IODD；device.json 仅作兼容输出；
  - [x] 迁移 switching_sensor（4 参数 → 4 行表项 + 2 个语义钩子）。
- **完成标志**：新增参数 = 一行表项（用测试证明）；isdu.c 行数显著下降；差分门全绿；
  生成的 IODD 与现状比对通过。
- **风险**：写触发语义（teach 类参数）必须靠 characterization tests 钉死；表占 ROM
  → size CI 观察。

### P5 reply plan + ISR 执行器 B（XL，3–4 周）——状态：已完成（本地合并；全量验证通过）

- **目标**：M-sequence 解读只存在一份（主泵）；ISR 收敛为执行器，可独立 WCET 分析。
- **依赖**：P4。
- **涉及文件**：`src/dll.c`、`src/dll_isr.c`、`include/iolinki/dll.h`、新增 plan 类型与
  发布原语（SPSC ring + 原子发布字）、`tests/test_dll_isr.c`。
- **步骤**：
  - [x] characterization tests 钉死现有 eager/延迟应答行为；
  - [x] `iolink_reply_plan_t` + 计划表（10 槽 × 24 B，共享区单表）；
  - [x] 主泵发布计划：M-seq 头、空/错误应答、静态值、PD、预分段 ISDU 应答；
  - [x] 计划失效协议：seqlock 发布 + ISDU 代数位 + 暂存写失效（写后读一致）；
  - [x] ISR 收敛为执行器：查 plan → 取源 → CKS → 喂字节；t_A 元数据保留、内容语义移除；
  - [x] 计划外请求回到主泵（eager=0）显式应答，回退语义写入头文件；
  - [x] WCET 基准：宿主层路径代理工具 `tools/wcet/` 已先行落地并接入 CI。
- **FRAME 配置说明**：`tools/diff_matrix.sh` 已记录 FRAME 不可纳入的实测原因
  （测试套件按 BYTE 布局构建、mock PHY 无 rx_frame/tx_frame），未伪造覆盖。
- **完成标志**：差分门全绿；ISR 代码量/分支数下降；最坏帧序下 plan 命中 100% 或
  回退严格等价（差分门证明）。
- **风险**：plan 失效窗口是最大正确性风险；发布/失效协议必须先于任何优化设计。

### P6 能力表化 D（L，2–3 周）——状态：已完成（本地合并；全量验证通过）

- **实现结果**：`iolink_build_caps_t` / `iolink_caps_t` + `iolink_caps_resolve()`
  单一解析点；12 项开关降级为运行时能力位；矩阵 22→4 结构配置
  （`tools/matrix_configs.sh` 单一事实源）+ 7 个语法探针 + profile 链接探针；
  `IOLINK_MINIMAL_PROFILE` / `IOLINK_TINY_PROFILE` 退役（定义它们是构建错误）。
- **实测**：`#if` src/ 94→84、include/ 117→116，`dll.c` 13→8；ROM 最小应用
  Cortex-M0+ `-Os` text 21812→22784（**+972 B，+4.5%**）、rodata +20 B；
  源码集成与 `.a`（非 LTO）差 0–4 B。**超出设计稿 3% 预算，DG3 决议接受**
  （2026-11 用户确认；能力表的可维护性收益优先于该 ROM 增量）。
- **验证**：79/79 测试、4 结构配置矩阵 + 7 探针、差分门 2 配置 0 allowlist、
  质量门、覆盖率 90.4%、WCET、size_matrix、profile ABI 全部通过；顺带修复
  `tools/coverage.sh` 复用旧构建树导致 gcovr 行归属错乱（假覆盖率回归）的问题。
- **CR 修正**：`IOLINK_DEVICE_INIT_ERR_PROFILE`（-17）在全部特性运行时化后不可达，
  已移除并保留 `REQUIRE_ISR_TX`/`TX_BUSY` → `ERR_PHY` 的可达校验。
- **剩余项**：ISR `isr_ops` 折叠（P5/P9 交叉）、`IOLINK_CAP_BUILD_*` 单变量
  测量开关（DG3 接受总额，故不实施）、嵌入式示例链接脚本的
  `KEEP(*(.iolink_profile_anchor))`、C2（FRAME）行为等价门、组合探针只编译不链接。
  profile 锚点本身已补齐 `IOLINK_WORKSPACE_EMBEDDED` 与 `IOLINK_PD_IN_BUFFERS`
  （9 个反向档位）。

- **目标**：22 配置矩阵收敛为 3–4 个结构配置；特性开关降级为数据。
- **依赖**：P5。**出口决策 DG3**。
- **涉及文件**：`include/iolinki/config.h`、能力表（新增）、全部 `src/`（#if 清理）、
  `tools/build_matrix.sh`、`tests/`（被降级特性的测试改运行时切换）。
- **步骤**：
  - [x] 能力表 const 结构 + capability 查询 API（`iolink_caps_resolve`）；
  - [x] 开关审计并降级 12 项（SYSTEM_COMMAND/WAKEUP/SIO_CQ/PD callback/
        ISR_TX_ONLY/3×compat/事件时间/时序强制/访问锁）；
  - [x] matrix 收敛为 4 结构配置 + 探针；差分门改为 C1 vs C1r + 期望帧测试；
  - [x] `IOLINK_APP_PD_INPUT_CALLBACK` 运行时化；DS/事件删除契约改链接期断言；
  - [x] ROM 实测（源码 vs `.a`）；**+972 B（+4.5%）超预算，DG3 已接受**。
- **完成标志（实测复核）**：`src/` + `include/` 的 `#if/#ifdef/#ifndef/#elif` 总量
  195（P6 前，00b07ef）→ 202（P6 合并，dfcd3c3）→ 207（P10 后）；本次 CR 清理后
  211（`src/` 91 + `include/` 120），仅 `#if` 为 133。核心文件里只有 `dll.c` 达到
  个位数；`isdu.c` 20、`dll_isr.c` 15、`device.c` 14 属结构布局/ISR 粒度守卫。
  **该量化目标未达成**，保留能力表化的收益（单一判定点、12 项运行时化），
  不再以 `#if` 数量作为验收条件。matrix 22→4；ROM 实测数据入档。
- **风险**：ROM 可能增长；`.a` 静态库非 LTO 下 gc-sections 基本失效 → 预期管理靠实测。

### P7 截止时间表 F（M，1–2 周）——状态：已完成（本地合并；全量验证通过）

- **目标**：定时器收敛为单表 + 单点比较；时序契约可枚举、可测试、可导出文档。
- **依赖**：P5。
- **涉及文件**：`src/dll.c`（定时器段）、`tests/test_deadline_rollover.c`（扩展）。
- **步骤**：
  - [x] `iolink_timer_t` 表 + 单一 next_deadline；re-arm/cancel 语义；
  - [x] 迁移 t_A/t_ren/t_cycle/t_PD/t_byte/T_DSIO/T_FBD/master_lost/idle timeout；
        时钟源保持唯一 `dll_time_us()`；
  - [x] 时序契约清单（timer 名 + 条款号）与 `tests/test_timer_table.c`。
- **完成标志**：process() 常数时间；rollover 单点处理；同刻多 timer 优先级规则定义并
  测试；差分绿。
- **风险**：ISR 读取 deadline 的原子性——复用 P2/P5 的发布字机制。

### P8 工作区分离 G（L，2–3 周）——状态：已完成（本地合并；全量验证通过）

- **目标**：状态与缓冲分离；内存数字可预测、全进 CI；小 MCU 可精确裁剪。
- **依赖**：P4、P5。**出口决策 DG4**。
- **涉及文件**：`include/iolinki/device.h`（workspace 声明）、`src/isdu.c`（response 租约）、
  `src/data_storage.c`（DS image 流式化）、`src/device.c`（composition root）、
  `docs/MEMORY_GUIDE.md`（重写）。
- **步骤**：
  - [x] `iolink_workspace_t` 由调用方提供（DTCM/外部 RAM），内嵌 fallback 保持兼容；
  - [x] ISDU response 路径租约模型，消灭 `resp_store[2]` 双缓冲副本（单一 frame + Busy 租约）；
  - [x] DS image 流式序列化（`iolink_ds_serialize`/`iolink_ds_refresh`），不再常驻 256 B；
  - [x] 内存 CI：尺寸契约与 MEMORY_GUIDE 同步（device 4088，外置 workspace 2792）；
  - [ ] 裁剪配置的尺寸打印进矩阵日志（仅记录，不设门）——由 `test_ctx_sizes`
        打印与 MEMORY_GUIDE 记录替代，未实现单独的矩阵日志行。
- **完成标志**：小 MCU 裁剪演示；差分绿；内存数字全进 CI。
- **风险**：MISRA 零拷贝证明 → 租约所有权规则文档化 + 静态断言辅助。

### P9 状态机表 + WCET 门 H（L，2–3 周）——状态：已完成（本地合并；目标端 DWT 门仍待硬件）

- **目标**："符合规范"变成机器证据。
- **依赖**：P5、P7。
- **涉及文件**：`src/dll.c` / `src/dll_isr.c`（状态机表化）、`tests/`（生成测试）、
  `tools/check_conformance_traceability.py`（联动）、`tools/bench_*`（WCET 基准）、
  CI 工作流。
- **步骤**：
  - [x] DLL 状态机表化（7×8）+ 生成测试/mermaid/CSV（tools/gen_dll_statemachine.py）；
  - [x] 宿主层路径门（tools/wcet/，CI）+ reply plan 状态戳失效/相位性质测试；
  - [x] 手段纪律已记录：virtual PHY 只做功能等价/帧序合法；目标端 DWT 门待硬件。
- **完成标志**：重构安全网完整；WCET 回归门工作；状态机表与 traceability 联动。
- **风险**：纯表装不下连续条件 → 表管骨架 + 函数管动作 + 性质测试管不变量。

### P10 小项收尾 + 审计 I（S，~1 周）——状态：已完成（本地合并；全量验证通过）

- **依赖**：全部。
- **涉及文件**：`include/iolinki/device.h`（不透明上下文）、`src/events.c`、
  `src/dll.c`（诊断）、`docs/MEMORY_GUIDE.md`、`docs/ARCHITECTURE.md`。
- **步骤**：
  - [x] 不透明上下文：按 P2 决策降级为"兄弟结构 + 内部头"，编译期字段隔离记录为可选；
  - [x] 诊断注册表：PHY faults / 事件队列 / 数据通路错误 / 本地参数变更 / DS 状态
        收敛到 `iolink_diag_desc_t` + `iolink_conditions`，DeviceStatus/DDS/0x1800 派生；
  - [x] 全库审计与尺寸更新（device 4072、DLL 3760、events 504；外置 workspace 2776）。
- **验证**：82/82 测试、4 结构配置矩阵、差分门、质量门、覆盖率 90.6%、
  WCET、size 28 项、profile ABI 7 个反向档位全部通过。
- **完成标志**：审计文档 + 最终数字。

## 8. 通用验收门与命令

任何阶段、任何 PR 合并前：

- G1 构建+测试：`cmake -B build && cmake --build build && (cd build && ctest --output-on-failure)`
- G2 质量：`./check_quality.sh`（Werror Debug+Release、cppcheck、clang-format、shellcheck、Doxygen）
- G3 矩阵：`./tools/build_matrix.sh`（P6 前 22 配置；P6 后为收敛后的结构配置）
- G4 覆盖率：`./tools/coverage.sh --check`（基线在 `tools/coverage_baseline.txt`）
- G5 差分门：P1 起全绿
- G6 提交规范：Conventional Commits + CHANGELOG `[Unreleased]`；
  `feature/*` 分支从 `own_dev` 切出，完成后**本地合并回 `own_dev`**（2026-11 用户
  指示不建 PR；GitHub 上的临时 PR/分支已清理）；不涉及 upstream
  （MAINTENANCE.md 规则不变）。

### 8.1 统一 CR 记录（2026-11）

新增单测按 P4 / P5+P7+P9 / P6+P8 三组并行 CR。首轮三组均 FAIL（P4 2 MAJOR；
P5/P7/P9 2 MAJOR；P6/P8 1 BLOCKER + 5 MAJOR），修复后 re-CR 三组全部 PASS。
修复提交：`18d0179`、`a8d595b`；P10 单测组首轮 FAIL（2 MAJOR）→ 修复后 PASS
（`c456c4f`）。已记录且接受的缺口：

- 外置工作区 + 单个 DS 参数 > `IOLINK_DS_STREAM_CHUNK`（32 B）时，流式长度/
  校验和与镜像路径不一致（实现注释已声明；需先定契约再补测试）；
- seqlock 两次读不一致的重试分支需要切点钩子，单线程宿主无法触发；
- t_A 预算只钉住 COM2=260，未覆盖 COM1/COM3 推导；
- C2（FRAME）仍无行为等价门，只有编译矩阵 + 语法探针；
- 状态机生成门禁覆盖 `state/event/enter_state`，动作函数或 clause 漂移由差分门兜底。

第三方 CR（`docs/CR_EVOLUTION_2026-11-12.md`）复核结论：9 个 MAJOR 全部属实，
已逐条修复；1 项 MINOR/TEST GAP 的推论不成立（`test_deadline_rollover.c` 虽只用
`assert`，但 `tests/CMakeLists.txt` 的 `-UNDEBUG` 让断言在 Release 配置下仍生效）。

| CR 项 | 处置 |
|---|---|
| M1 OD 写路径/索引契约 | 写路径透传 subindex；OD 层校验 `max_subindex`；OCTET_STRING 按 `block_len` 返回 0x33；索引按规范域/设备域分流（规范域标准优先，设备域应用表 → 厂商服务 → 内建扩展） |
| M2 应用 DS 成员 | `iolink_ds_bind_od()` + 标准表/应用表联合描述符集，`config->od` 自动纳入 |
| M3 workspace 解绑悬空 | `EMBEDDED=0` 时解绑清零全部指针与容量（ISDU/DS） |
| M4 DS 恢复非原子 | 两阶段：先对全部已知记录做无副作用校验，再逐条应用；注释与文档改为如实描述（NVM 失败仍可能部分生效） |
| M5 caps 变更旧计划 | `iolink_dll_invalidate_reply_plans()`；`iolink_device_set_capabilities()` 调用它 |
| M6 profile 锚点缺布局开关 | 增加 `workspace_embedded_*`、`pd_in_buffers_2/3`；反向探针精确到单个符号（9 档） |
| M7 退役组合包静默 | `config.h` `#error` + CMake `FATAL_ERROR`；文档与示例不再教用户定义 |
| M8 CONSUME 帧计时簿记 | 共享区 `isr_consumed_us/seq` 交接，主泵按帧时间补做 T_CYCLE/T_REN 与 `last_response_us` |
| M9 验收记录不可靠 | 本文件与 CHANGELOG/MEMORY_GUIDE/DIAGNOSTICS_DESIGN 按实测重写；`#if` 目标如实标注未达成 |
| MINOR | `diff_matrix` 失败即红、`build_matrix` 缺归档即红、`size_matrix` 增加 `.rodata` 门、timer 比较改回绕安全、CAP_TX 契约为 `tx_octet` 必选、`diag.c` 去重 include |

同时按“只保留最新 API”删除了兼容层：`iolink_params_ctx_get/set`、
`IOLINK_PHY_FEATURE_WAKEUP`/`_SIO_CQ`/`IOLINK_APP_PD_INPUT_CALLBACK`、
内部 `t_pd_deadline_us` 交接、示例的 `switching_sensor_service()`。

## 9. 决策门

| # | 时机 | 决策 | 倾向 |
|---|---|---|---|
| DG1 | P3 入口 | vendor_service API 破坏策略：兼容 shim vs 2.0 断代 | 曾保留一版 shim；2026-11 用户要求“不做向后兼容”，shim 已删除，examples 只保留 OD 路径 |
| DG2 | P4 出口 | 事实源格式：C 表 vs JSON 生成 | C 头为源，脚本导出 IODD |
| DG3 | P6 出口 | 结构配置清单定稿；是否接受能力表 ROM 成本 | 已定为 4 配置；**ROM 实测 +972 B（+4.5%）超 3% 预算，决议接受**（`IOLINK_CAP_BUILD_*` 逐项申报不执行） |
| DG4 | P8 出口 | 静态库交付形态下 GC 效果 | 实测数据定论 |

## 10. 里程碑

- M1 = P0 + P1：安全网就位
- M2 = P2 + P3：边界与错误语义定型
- M3 = P4：对象字典落地（最大单项）
- M4 = P5：双泵收敛
- M5 = P6 + P7：配置与时间收敛
- M6 = P8 + P9 + P10：内存、证据、收尾

## 11. 工作量与并行

单线总工作量约 20–27 周（S≈1 周、M≈1–2 周、L≈2–3 周、XL≈3–4 周）。
并行空间：P2（dll* 头/实现）与 P4（isdu*/od*）文件集基本不重叠，两人协作可并行，
总周期可压到 ~3–4 个月；其余阶段建议串行。

## 12. 项目约束（执行时必须遵守）

- 无动态内存；上下文式（无全局）；`<stdint.h>` 类型；负整数错误码（P3 后统一为
  `iolink_result_t` 约定）；MISRA C:2012 子集（`check_quality.sh` 管控）。
- 规范 V1.1.5（`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/`）是唯一事实源，
  代码/注释/文档与其冲突时以 PDF 为准。
- 分支/提交/PR 规则见 `AGENTS.md` 与 `docs/MAINTENANCE.md`；`upstream` 只读。
- 文档整洁规则：同一事实只保留一个权威出处；本文件不复制 AGENTS.md 已有规则，
  只记录重构计划特有的决策。
