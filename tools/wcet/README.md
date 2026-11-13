# WCET 路径长度代理基准（tools/wcet）

`tools/wcet` 是路线图 **P9 / D8** 的宿主层工具：用确定性的合成 PHY 驱动完整
设备（`iolink_device_init` / `process` / `process_isr`），把**最坏帧序**下的
调用次数变成可比较、可设门的数字。

> **测量的是路径长度代理，不是 CPU 周期 WCET。**
>
> 本工具输出 ISR 泵调用次数、`rx_octet` / `tx_octet` 调用次数等，只能说明
> "这段代码被调用了多少次"，不能说明"在目标 MCU 上花多少周期"。CPU 周期
> 上界必须用目标端 DWT 周期计数器测量（见下文「目标端 DWT 门」）。
> 手段纪律：virtual PHY 只验证功能等价与帧序合法，周期上界只用目标端 DWT。

## 运行

```bash
# 构建 + 运行全部场景，打印指标表
./tools/wcet_run.sh

# 与基线比较：任一指标比基线大即失败（只允许不增长）
./tools/wcet_run.sh --check

# 显式更新基线（代码/场景有意变化时）
./tools/wcet_run.sh --update

# 只构建，直接控制场景与输出
cmake -S tools/wcet -B build_wcet -DCMAKE_BUILD_TYPE=Release
cmake --build build_wcet --parallel
./build_wcet/wcet_harness --repeat 2 --json build_wcet/wcet_metrics.json
./build_wcet/wcet_harness --list
```

默认构建目录是 `build_wcet/`（已在 `.gitignore` 中）；可用
`--build-dir DIR` 或环境变量 `WCET_BUILD_DIR` 覆盖。构建直接编译 `src/*.c`，
使用仓库**默认配置**（`config.h` 缺省值：BYTE/BYTE ISR 传输、
`IOLINK_CONCURRENCY=ISR`、全部特性打开），不经过 `iolinki` 静态库，因此
不需要先构建主工程。

`--check` 内部用 `--repeat 2` 运行每个场景，两个实例的指标必须逐字段完全
一致；不一致会直接失败，这就是确定性约束的机器检查。脚本的所有模式都带
`--repeat 2`（`--check` 依赖它保证门的可比性）。

## 指标含义

| 指标 | 含义 | 为什么与 WCET 相关 |
| :--- | :--- | :--- |
| `frames` | 送入的主站帧数（含无效帧） | 场景规模的归一化基准 |
| `isr_pumps` | `iolink_device_process_isr()` 调用次数 | 每个调用都是一次真实的数据中断入口，直接决定 ISR 路径的固定开销倍数 |
| `isr_max_per_frame` | 单帧从开始到线路空闲的最大 ISR 泵次数 | 最坏单帧的 ISR 执行次数上界（路径长度意义上的） |
| `rx_octets` | `rx_octet()` 调用次数 | 逐字节成帧的接收侧搬运次数 |
| `tx_octets` | `tx_octet()` 调用次数，即发送字节数 | 逐字节应答的发送侧搬运次数 |
| `tx_frames` | `tx_done()` 完成的应答帧数 | 应答数量（每帧一次收尾） |
| `tx_max_per_frame` | 单帧最大 `tx_octet` 次数 | 最长应答帧的发送路径长度 |
| `main_pumps` | `iolink_device_process()` 调用次数 | 主循环侧路径长度（含场景内显式时间推进） |

合成 PHY 的 `int_active()` 报告"有待处理工作"，因此 `isr_pumps` 就是
"把这条线路服务到空闲需要多少次中断"；这比单纯统计字节数更接近真实的
中断预算。所有指标都是整数、与编译器/主机架构无关。

## 场景集

指标不含建立通信（唤醒 → 过渡命令 → 进入 OPERATE）的开销；`sio_fallback`
例外，它测量的就是模式切换边界本身。

| 场景 | 设备配置 | 覆盖点 | 帧数 |
| :--- | :--- | :--- | :--- |
| `pd_max_write` | TYPE_2_V，PD-in/out 32B，OD 32B | 最大 PD/OD 写请求（66B）与应答（33B） | 8 |
| `pd_max_read` | TYPE_2_V，PD-in/out 32B，OD 32B | 最大 PD 读请求（34B）与应答（65B） | 8 |
| `burst_write` | TYPE_2_V，同上 | 4×4 背靠背同类型帧，单次主泵；邮箱承受突发 | 16 |
| `isdu_read_chunked` | TYPE_1_V，OD 32B | 63B VendorName 的 ExtLength 分块读（3 个 OD 段） | 4 |
| `isdu_write_chunked` | TYPE_1_V，OD 32B | 64B 厂商参数分块写（68 个 ISDU 八位组）+ 写应答 + 回读校验 | 8 |
| `event_diag_read` | TYPE_1_V，OD 32B | 事件触发 → 诊断通道读（事件标志）→ 地址 0 确认 → 再读 | 3 |
| `sio_fallback` | TYPE_0，MinCycleTime 1ms | FALLBACK → T_FBD（3ms）→ SIO → 重新唤醒 → 页读启动探测 | 2 |
| `invalid_frames` | TYPE_2_V，同上 | 坏校验、截断帧、`rx_octet` 错误、保留类型，随后合法帧恢复 | 4 |

每个场景在结束前断言功能不变量（状态仍为 OPERATE、应答长度/内容正确、
事件标志按预期置位/清除、无效帧不产生应答等）。断言失败时 harness 以
非零码退出，不会输出"看起来正常"的指标。

## 如何加场景

1. 在 `tools/wcet/wcet_harness.c` 中新增 `static int scenario_xxx(void)`：
   - 用 `wcet_scenario_start(type, pd_in, pd_out, od, min_cycle_time)` 建场景；
   - 用 `wcet_exchange(frame, len, gap)` 跑一个完整交换，或用
     `wcet_send_no_main()` 构造背靠背突发；
   - 用 `WCET_CHECK(cond, "消息")` 断言功能不变量（失败返回 `-1`）；
   - 只用虚拟时钟与固定帧序：不允许真实时间、随机数或未初始化内存。
2. 在 `g_scenarios[]` 里登记名称、说明与函数指针。
3. 运行 `./tools/wcet_run.sh --update` 生成新基线，人工确认指标变化只来自
   新增场景，然后提交 `tools/wcet_baseline.json`。
4. 需要条件编译的场景（例如 `IOLINK_FEATURE_EVENTS=0`）用 `#if` 包住主体并
   返回 0，保持场景表稳定。

## CI 接入建议

主代理负责修改 `.github/`；推荐两条命令即可：

```yaml
- name: Build and run WCET path-length proxy
  run: ./tools/wcet_run.sh --check
```

- 把该步骤放在常规构建/单测之后、独立 job 中执行，避免与 `build/` 竞争；
  脚本自带独立构建目录 `build_wcet/`。
- `tools/wcet_baseline.json` 必须随代码提交。基线只能在**显式**运行
  `--update` 后更新，PR 中应说明指标变化原因（允许下降，禁止无解释增长）。
- 需要保留证据时，把 `build_wcet/wcet_metrics.json` 作为 artifact 上传。
- 该门不替代目标端 DWT 门；它只保证 ISR/主循环路径长度不回归。

## 目标端 DWT 门：接入方式与挂起条件

本工具解决"没有硬件时先落工具与基准"的部分；周期上界按 D8 在目标端完成：

1. **复用帧序**：把本文件的场景脚本（`wcet_feed` / `wcet_exchange` /
   `wcet_send_no_main` 的调用顺序）原样搬到目标板测试固件，只把合成 PHY
   换成板级 PHY（或保留合成 PHY 从 FIFO 喂入同一字节序列），保证主机与
   目标注入的是同一最坏帧序。
2. **测量点**：在 `DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk` 后，于
   `iolink_device_process_isr()` 入口读取 `DWT->CYCCNT`，出口再读一次，取
   差值；记录**最大值**而不是平均值。对每帧把所有 ISR 调用的差值求和，
   得到"单帧 ISR 周期"上界；主泵同理。
3. **上界进 CI**：目标端基线（每场景 `max_isr_cycles`、`max_frame_isr_cycles`、
   `max_main_cycles`）与主机基线分开存放，门语义同样"只允许不增长"。
   建议同时记录测量条件：MCU 型号、系统时钟、编译器和优化等级、链接脚本
   中 `src/` 的 section 放置。
4. **挂起条件**（不满足时该门应显式标记为 skipped，而不是给假绿）：
   - 没有可用的目标板 / 调试探针；
   - 时钟、编译器版本、优化等级或 `IOLINK_ISR_RX_MODE` /
     `IOLINK_ISR_TX_MODE` / `IOLINK_CONCURRENCY` 变化，旧周期基线失效；
   - DWT 不可用（内核不带 DWT 或已被调试器占用）；
   - 测量期间有更高优先级中断干扰且未纳入预算。
5. **报告纪律**：目标端数字只按"实测最大值"汇报，注明采样帧序与次数；
   主机侧 `tools/wcet` 的数字始终标注为路径长度代理，不得当作周期上界引用。

**当前（P9）挂起状态**：本环境没有目标板/调试探针，也没有 DWT 可用性证据，
因此目标端 DWT 门处于**显式挂起**状态（不是绿色通过）。恢复条件：上表
"挂起条件"全部不成立后，按第 1–3 步采样并提交独立的目标端基线。
P9 的 state × event 常量表把主泵分发从若干 `if` 链改为一次二维查表 +
函数指针调用，属于同阶路径变化；本工具的场景指标（`isr_pumps`、
`tx_octets`、`main_pumps` 等）在 P9 落地后保持不变，所以主机基线无需
`--update`。真正的周期影响仍只能由 DWT 门量化。

## 已知边界

- 不模拟电气时序与波特率：虚拟时钟固定步进 50 µs，只用于让库的软件
  定时器/超时路径可复现，不用于时序精度评估。
- 只覆盖 `IOLINK_ISR_RX_BYTE` / `IOLINK_ISR_TX_BYTE` 默认传输；FRAME 模式
  需要新的合成 PHY（交付整帧），目前未纳入。
- 指标对"帧序 + 库版本"敏感，不跨提交直接比较；跨提交比较一律走
  `--check` 与基线文件。
