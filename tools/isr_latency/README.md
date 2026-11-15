# ISR 时延主机代理（tools/isr_latency）

`tools/isr_latency` 用合成 PHY 驱动 `iolink_dll_process_isr()` 的应答路径，
测量 **t_soft 的主机墙钟代理**：从帧末 `process_isr` 调用入口到首个
`tx_octet` 返回（首字节交付 PHY）的时间。它回答的问题是"重构前后 ISR
关键路径变长还是变短"，**不是**目标 MCU 上的绝对 t_A。

> **绝对值跨机器无意义。** 本工具只允许同机、同核心、同构建之间比较；
> 目标端周期上界必须用目标 DWT（`IOLINK_ISR_TIMING`），见
> `docs/CODE_FLOW.md` 的 ISR 分工表与计划文件。

## 与 t_A 的关系

```
t_A = t_hw_帧结束检测 + t_soft + t_hw_TX起播 ≤ 10 × Tbit
```

| 段 | COM3 预算 | 测量手段 |
| :--- | :--- | :--- |
| t_hw 帧结束检测 | ≤ 10 µs | 目标端 GPIO + 逻辑分析仪（暂缓） |
| **t_soft** | **≤ 20 µs** | 本工具（主机相对比较）+ 目标 DWT（绝对） |
| t_hw TX 起播 | ≤ 10 µs | 目标端 GPIO + 逻辑分析仪（暂缓） |

本工具的窗口在合成 PHY 上等于"整帧字节 + 帧结束 + 应答组装 + 首字节
发送"（BYTE 模式下一次 `process_isr` 调用完成这些工作，见
`dll_isr_drain()`）。另报 `call_*` 细分项（帧末单次调用时长）。

## 环境要求（横向比较的前提）

- **独占核心**：工具用 `sched_setaffinity` 钉核并在启动时验证；钉核失败
  直接退出。
- **固定频率**：`scaling_governor` 应为 `performance`，关闭 turbo。工具读取
  `/sys/.../cpuN/cpufreq/*` 并告警；容器内文件不可读时打 WARN。
- **实时优先级（尽力）**：尝试 `SCHED_FIFO`，无权限时降级为 `SCHED_OTHER`
  并告警；`--no-rt` 可跳过。
- 同一台机器、同一核心、同一构建（`Release`）下产生的基线才可比。
  基线 JSON 头记录 hostname / CPU 型号 / 内核 / 编译器 / 构建类型 / git /
  governor / 调度策略；`--check` 在 hostname 或 CPU 型号不一致时直接失败。

## 独占/降低干扰能做到什么程度

工具自身：`sched_setaffinity` 钉单核（失败即退出）、验证钉核生效、读取 SMT
兄弟列表与 `/sys/devices/system/cpu/isolated` 并写入基线 JSON；`--check`
要求机器、CPU 型号、核心三者一致。

宿主上可用的手段（本机实测）：

| 手段 | 状态 | 备注 |
| :--- | :--- | :--- |
| 钉核 + SMT 探测 | ✅ | 默认 cpu0（P 核，有 SMT 兄弟）；E 核实测慢约 1.5 倍且波动不降 |
| `run_isolated.sh`（钉频/关睿频/移 IRQ/禁深 C-state） | ✅ 需 sudo | 退出时逐项恢复，含失败与 Ctrl-C |
| `SCHED_FIFO` | ✅ 但需 root | 普通用户无 `CAP_SYS_NICE` 且 `RLIMIT_RTPRIO=0`，被拒 |
| `isolcpus` / `nohz_full` | ❌ 需重启 | 内核命令行 + reboot，才能消除 tick 与兄弟核竞争 |

真正的独占需要在宿主内核命令行加入并重启：

```
isolcpus=managed_irq,0,1 nohz_full=0,1 rcu_nocbs=0,1 irqaffinity=2-15
```

之后用 `--core 0` 并重新取基线。没有 isolcpus 时，`run_isolated.sh` 已把
频率、设备 IRQ、深 C-state 噪声降到最低；剩余 tick 由 min 门吸收。

## 噪声指标


输出表与 JSON 中有 `noise`（`median_ns / min_ns`）：接近 1.0 表示整轮几乎
无干扰；明显大于 1.0 说明本轮受到抢占，`--check` 会给出告警（硬门仍只看
min）。

## 运行

```bash
cmake -S tools/isr_latency -B build/isr_latency
cmake --build build/isr_latency --parallel

# 推荐：降低宿主干扰后取基线（需要 sudo；退出时自动恢复系统设置）
./tools/isr_latency/run_isolated.sh

# 降低干扰后与基线比较（门：min / call_min 不超过基线 × 1.05）
./tools/isr_latency/run_isolated.sh --check

# 不降低干扰的裸跑（仅用于快速自检）
./build/isr_latency/isr_latency --json tools/isr_latency_baseline.json
./build/isr_latency/isr_latency --check

# 只列场景
./build/isr_latency/isr_latency --list
```

基线 JSON（`tools/isr_latency_baseline.json`）是机器相关的本地产物，已在
`.gitignore` 中：它不进入提交，各机器各取各的基线。

参数：`--core N`（默认 0）、`--iters N`（默认 100000）、`--warmup N`
（默认 2000）、`--json PATH`、`--no-rt`、`--check [PATH]`。

`--check` 先加载基线（默认 `tools/isr_latency_baseline.json`），测量后只做
比较，**不会覆盖基线**；需要同时保存本次结果时显式给 `--json 其他路径`。

退出码：`0` 通过；`1` 门失败；`2` 基线机器不匹配/不可读；`3` 场景健全性
失败或写文件失败；`4` 用法/钉核失败。

## 场景

每个场景迭代都重新初始化上下文，保证每次测量初始状态一致；初始化开销在
测量窗口之外。

| 场景 | ISR 路径 | 说明 |
| :--- | :--- | :--- |
| `type0_page_read` | `IOLINK_REPLY_SRC_PAGE` | DPP1 整段复制（RAM 映像 + 单次 seqlock 窗口） |
| `type0_page_write_cks` | `SRC_NONE` + 短应答 | CKS-only 页写确认 |
| `type0_isdu_read_start` | `SRC_ISDU_SEG` | 读视图采纳 + 1 字节 OD |
| `type12_pd_read_tail` | `SRC_TEMPLATE` + `PD_TAIL` | OD 头 + 32B PD-in 长应答 |
| `type1_page_read_od32` | `SRC_PAGE` + `PD_TAIL` | OD 32 字节页读（32 次 DPP1 映像索引） |
| `isdu_write_segment` | `iolink_isdu_od_write` | 写段位置去重累计 |
| `plan_miss` | 未命中路径 | 计划表已失效时的合法帧：丢弃 + 计 `isr_plan_misses`，不入邮箱 |
| `invalid_frame` | 校验失败路径 | 坏 CKS，无应答 |

场景健全性（应答长度、`isr_plan_misses`、首字节）在测量前校验一次，不一致
立即退出，避免"看起来正常"的错误指标。

## 门

`--check` 要求：

- **硬门（min）**：每个场景的 `min_ns` 与 `call_min_ns` 满足
  `新值 ≤ 基线 × 1.05`。调度干扰只会把单次测量拉长、不会缩短，因此 min
  基本等于"无干扰时的真实路径长度"，是确定性代码最稳的估计量。
- **噪声告警（median）**：`median_ns` 超过基线 1.30 倍只告警（min 稳定
  说明是环境干扰）；若 min 与 median 同时升高则按失败处理。
- 迭代数与基线不一致时告警（统计量不再严格可比）。
- `p99`/`p95` 仅展示，不参与门。

重构每步后运行一次：只许降不许升；DPP1 索引化后 `type0_page_read` 与
`type1_page_read_od32` 显著下降。

## 已知局限

- 主机墙钟含 vDSO 时钟调用开销（约几十 ns），对纳秒级差异有噪声；用
  中位数 + p99 与 10 万次迭代压制。
- 热循环（分支预测/缓存已热）比真实中断入口乐观；本工具用于**相对比较**，
  不声称绝对值。
- 目标端周期数与主机 ns 无固定换算关系；不要用本工具推导 WCET。
