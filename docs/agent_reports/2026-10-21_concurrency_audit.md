# iolinki 并发模型深度审计报告

- 审计对象: 库层 (`src/dll.c` / `src/device.c` / `src/platform.c` / `include/iolinki/*.h`)、
  PHY 驱动层 (`examples/stm32g0_l6362a_raw/phy_l6362a_raw.c`)、
  并发控制测试 (`tests/test_phy_l6362a_raw.c` + `tests/mocks/stm32g0_l6362a/`)
- 审计基线: `94060bb` (own_dev, 已合并 upstream/develop), 测试全绿 (`test_phy_l6362a_raw` 在
  `build` 与 `build_isr` 均 PASS)
- 日期: 2026-10-21

---

## 1. 并发模型总览

三方参与者共享一份 `iolink_dll_ctx_t`(无动态内存、单上下文模型):

| 参与者 | 执行环境 | 写的关键状态 |
| --- | --- | --- |
| 主泵 `iolink_dll_process` | 主循环/线程 | `state`、`isdu`、`events`、`frame_buf`、PD 长度、计数器 |
| ISR 泵 `iolink_dll_process_isr` | 4 个中断向量 (USART2/TIM3/DMA/EXTI, 见 README) | mailbox、eager 回复、ISDU 写 staging、ISDU 读快照索引、`isr_tx_*`、`isr_master_activity` |
| PHY 中断 feed | USART2 (prio 0) > TIM3 (prio 1) > DMA (prio 2) | `s_rx_ready/err/last_index/frame`、`s_tx_in_progress`、`s_wake_latched`、DMA/USART 寄存器 |

保护原语(全部为"写者原子发布, 读者最后读标志"模式):

1. `iolink_critical_enter/exit`(本板: PRIMASK + 深度计数, `phy_l6362a_raw.c:588-604`)
2. 应用锁 `config->lock/unlock`(可选, `dll_send` 使用)
3. `volatile` 标志 + publish-last 次序(如 `isr_frame_ready`、`isr_tx_pending`)
4. 单写者分工(ISDU 由主泵独占, ISR 只写 staging 槽)

总体评价: 主-ISR 单向数据流设计良好(ISDU 单写者模型、PD-in 三缓冲、mailbox 稳定化拷贝是
真实优点, commit `2b61354` 的加固方向正确)。但**保护域不一致、ISR 泵可重入性未封闭、
若干"限时等待/单槽位延迟"的竞态窗口在真实工况下可触发**, 详见下文。

---

## 2. 发现清单

严重度: 🔴 High / 🟠 Medium / 🟡 Low / 🔵 Info

### 2.1 库层

#### A1 🔴 `dll_send()` 无锁兜底, 与其它发布路径不一致

- 位置: `src/dll.c:180-199`
- 现状: `dll_send` 只认 `ctx->lock`(即 `config->lock`), 为 NULL 时**不降级**到
  `iolink_critical_enter()`; 而 `device.c` 的 PD 发布 (`iolink_device_pd_input_update`、
  `iolink_device_process`、`iolink_device_pd_output_read`) 都有 `critical_enter` 兜底。
- 竞态: `isr_tx_next` 被两个分配器共享 —— 主泵 `dll_send` (line 183-184) 与 ISR
  `dll_isr_tx_alloc` (line 1867-1872)。无锁时两者可同时 `read-modify-write` 同一字节:
  双翻转后拿到**同一 payload 槽**, 主泵 `memcpy` 覆盖 ISR 正在 DMA 发送的缓冲; 同理
  `isr_tx_len` / `isr_tx_pending_slot` 撕裂。
- 放大因素: `src/platform.c:27-40` 的弱默认 `iolink_critical_enter` 是 **no-op**。裸机
  移植只要忘记提供真实临界区, 整个 ISR 传输路径 (mailbox 交接 `dll.c:1362-1376`、
  ISDU 快照 `dll_isdu_sync_snapshot`、PD 长度原子对 `iolink_dll_set_pd_length`) 全部无原子性,
  且编译期/链接期无任何告警。
- 工况: `config.lock == NULL` 的合法配置(校验只要求 lock/unlock 成对, `device.c:150`)。
- 建议: `dll_send` 在 `lock == NULL` 时使用 `iolink_critical_enter()`; 或 ISR 泵构建
  (`IOLINK_PHY_FEATURE_ISR_PUMP`) 下强制要求 lock。平台弱默认至少对 ISR 泵构建
  `#error`。

#### A2 🟠 master-lost 监督对 ISR 应答流量"失明", 慢速主窗口产生虚假翻转

- 位置: `src/dll.c:1241-1257` (计算) vs `dll.c:1357-1382` (mailbox 消费);
  `isr_master_activity` 仅在两处置位: Type-0 快速路径 `dll.c:1934` 与 RX timeout `dll.c:2550`。
- 事实 1: **Type-1/2 周期帧从不置 `isr_master_activity`** —— 稳态 OPERATE 流量完全依赖主泵
  消费 mailbox 后在 `dll_process_complete_frame` (line 964) 刷新时间戳。
- 事实 2: `master_lost` 计算 (line 1241) 在 mailbox 消费 (line 1357) **之前**执行。
- 事实 3: 主窗口被阻塞时, eager 路径仍从 ISR 应答进程通道帧(链路存活), 但 mailbox 单槽
  溢出, 后续帧被丢弃 (`isr_mailbox_drops++`), 活动时间戳不前进。
- 后果: 一次超过 `master_lost_cycles × 周期` 的主窗口阻塞(典型: 慢速 vendor ISDU 服务,
  如 NVM 写 50-100ms, 或应用 `on_pd_output` 回调阻塞), 会产生 **一次虚假的
  lost→online 翻转**: `on_master_lost` 误回调 + mailbox 丢帧计数增长。若应用在
  `on_master_lost` 中执行复位/降级动作, 后果超出诊断层面。
- 建议: ISR 泵对**每个**合法接收帧置 `isr_master_activity = 1`(不只 Type-0 快速路径);
  或把 master-lost 计算移到 mailbox 消费之后。

#### A3 🟠 `state_cb_pending`/`pending_state` 单槽位可被覆盖

- 位置: `src/dll.c:54-77` (ISR 侧延迟), `src/device.c:253-258` (主侧消费)
- 竞态: ISR 连续两次状态迁移发生在主窗口消费之前时, `pending_state` 被后一次覆盖,
  前一次迁移的 `on_state_change` 永久丢失。
- 工况: 主站建立通信的连发序列 —— 唤醒探测 → PREOPERATE → 转换命令 → ESTAB_COM →
  首帧 → OPERATE, 帧间隔为最小周期 (8110 上 ~1-4ms), 而主窗口可能正被慢速服务占用。
- 实际影响: `device_state_cb` 只对 STARTUP/PREOPERATE/OPERATE 有动作, 丢失的最坏情形是
  **PREOPERATE 紧随 OPERATE 被覆盖**(应用 `on_preoperate` 不被调用), 以及回调实参与
  `iolink_device_get_state()` 瞬时不一致。中等偏低, 但属于协议状态通知的正确性问题。
- 建议: 升级为小型 FIFO, 或消费时遍历 `state` 从 `pending_state` 到当前值补齐中间迁移。

#### A4 🟡 FRAME TX 模式下在途回复对 DLL 不可见

- 位置: `src/dll.c:2161-2163` (`dll_isr_start_reply` FRAME 分支置 `isr_tx_active = 0`);
  `dll.c:1150-1162` (`dll_restore_operating_baudrate` 的等待守卫只查
  `isr_tx_active`/`isr_tx_pending`)
- 事实: TX FRAME 模式 `isr_tx_active` **恒为 0** —— 正在 DMA 发送的回复不被守卫覆盖。
- 后果: COM1 恢复→COM3 的 `dll_restore_operating_baudrate`, 或 MasterCommand 触发的
  `set_baudrate`/`set_sio_mode`, 会调用 PHY `set_baudrate`/`set_mode`, 后者在临界区内
  中止在途 DMA (`phy_l6362a_raw.c:296-309`) → 回复被截断 → 主站 CRC 重试自愈, 但
  `last_response_us`/t_REN 统计对该帧无效, 且守卫注释与实现不符。
- 建议: FRAME 模式由 PHY 提供 `tx_done` 或查询接口反映在途状态, 或守卫直接询问 PHY
  (`int_active` 含 TX-ready 语义), 或至少修正注释。

#### A5 🔵 保护域不一致 (RTOS 集成隐患)

- `dll_send` 用应用锁, 而 mailbox 消费 (`dll.c:1362`) 与 ISDU staging drain
  (`dll.c:1200`) 用 `iolink_critical_enter` (PRIMASK)。单核裸机等价; 但 `platform.h:36`
  明确声明支持 RTOS 覆盖 —— PRIMASK 不保护**线程间**并发, 双线程同时调
  `iolink_dll_process` 时 drain/counters/state 无保护。需在文档明确"单主上下文"约束,
  或在 RTOS 移植上要求 lock 覆盖所有入口。

#### A6 ✅ (审计确认安全) PD-in 三缓冲发布协议

- `device_pd_in_free_index` (`device.c:222-232`) 的无锁读取经推演安全: ISR 的捕获
  (`dll_isr_start_reply`, `dll.c:2113-2159`) 只针对 `pd_in_active`, 而主泵填充的是既非
  active 也非 captured 的缓冲; 填充完成才发布 `pd_in_active`。`pd_in_captured` 仅在波特率
  切换/SIO 时复位, 保守持有旧缓冲, 无正确性问题。

### 2.2 PHY 驱动层 (`phy_l6362a_raw.c`)

#### B1 🔴 ISR 泵四上下文可重入缺口

- 位置: README 要求 USART/TIM3/DMA/EXTI 四个 handler 均调用 `iolink_device_process_isr()`;
  优先级 USART2=0 > TIM3=1 > DMA=2; Cortex-M0+ **无 BASEPRI**。
- 触达路径: mailbox 满时泵在 `rx_frame` 之前 break (`dll.c:2272-2274`), 已置位的
  `s_rx_ready` 留给**下一次**任意上下文的泵调用消费 —— 包括 TIM3/DMA/EXTI 低优先级上下文。
  此时 USART RTOF 可**嵌套抢占**低优先级泵的帧处理:
  - `dll_isr_tx_alloc` (`isr_tx_next` 双翻转) 在嵌套中被二次执行 → 同槽位覆盖;
  - 嵌套的 `l6362_tx_frame` 200us 限时等待超时 → 拆除外层正在进行的 DMA (见 B2);
  - FRAME 路径 "CKS 在 DMA 启动后计算" (`dll.c:2172-2181`) 写入已死缓冲。
- 实际表现: 帧丢失、截断回复、主站重试自愈; 未观察到内存破坏, 但存在未被建模的
  交错组合。`in_isr` 只是标志, 不是重入守卫。
- 建议: 泵只允许在 USART IRQ (最高优先级) 运行, 其余 feed 通过
  `NVIC_SetPendingIRQ(IOD_UART_IRQn)` 转发; 或加静态重入守卫 + 退出时重新 pend。

#### B2 🟠 `l6362_tx_frame` 200us 有界等待超时后静默拆除在途传输

- 位置: `phy_l6362a_raw.c:496-513`
- 事实: 等待 `phy_txen_get()` 的预算是 200us; 8110 在 COM3 的 31 字节回复需
  ~1.48ms >> 200us。超时后无条件 `CCR = 0` 重配置 DMA —— 前一回复被截断, 且**无 busy
  错误返回给 DLL**, DLL 认为回复已发出。
- 触发: 主泵在 ISR 传输中 stage 新回复(主站违反周期/主窗口异常), 或 B1 的嵌套泵。
- 建议: 超时返回 -1 (busy), 由 DLL 走 `isr_tx_dropped` 路径; 或按 PHY 实际状态 (TC) 判定。

#### B3 🟡 真实硬件上临界区会吞掉 RTOF —— SIO 重入期唤醒帧可丢失

- 位置: `UartConfigure` 的 `ICR` 清除 (line 157) 在 `l6362_set_mode`/`set_baudrate`
  临界区内执行; RTOR=1bit ≈ 4.3us @COM3 与临界区 ~3us 同量级。
- 推演: 主站唤醒帧末字节落在临界区内 → RTOF 硬件置位 → `ICR` 写入清除 RTOCF →
  PRIMASK 释放后延迟向量运行, 读 ISR 无 RTOF → 帧不发布 → `detect_wakeup` 返回 0。
- 后果: 唤醒丢失, 主站按 T_DWU 重发自愈(延迟一次唤醒周期); 不破坏内存/协议状态。
- 注意: `test_isr_rx_during_set_mode` 断言了强属性(帧必存活), 其通过依赖 mock 假象(见 C2)。

#### B4 🟡 `s_rx_linear` 单影射缓冲的理论覆盖窗口

- 位置: `phy_l6362a_raw.c:80-87, 802-814`
- wrap 线性化写入唯一 `s_rx_linear`; 若低优先级上下文的泵正拷贝上一线性化帧时被嵌套
  RTOF 再次线性化覆盖, mailbox 帧损坏。推演: 覆盖需要环形缓冲再前进 ~250 字节 ≈ 10ms
  @COM3, 而拷贝窗口仅 ~µs 级 —— **实际不可达**, 建议注释声明该不变式。

#### B5 🔵 FRAME 模式 CHK 状态无消费者

- `dll_isr_process` 对 `rx_frame` 的 status 直接 `(void) st` (`dll.c:2281`): 校验/奇偶错误
  仅通过 CRC 失败呈现; `s_rx_chk` 在 FRAME 模式永不递增 (`test_rx_frame:266` 钉住了该设计)。
  可观测性弱, 属设计取舍。

#### B6 🔵 ISR 内的 `iolink_time_get_us` 依赖 SysTick 优先级

- `phy_l6362a_raw.c:605-631`: 若 SysTick 优先级 ≥ USART2, 泵内 `HAL_GetTick` 冻结, 有界
  等待的截止时钟失效(仅靠硬件事件推进)。8110 上 USART=prio 0, 需在 HAL 配置中确认
  `TICK_INT_PRIORITY`。do-while 重试循环本身安全。mock 不建模 tick 推进, 无测试覆盖。

### 2.3 测试层 (`tests/test_phy_l6362a_raw.c` + mock)

#### C1 🟠 mock 无 NVIC 优先级/嵌套模型 —— B1 类竞态不可表达

- `mock_request_irq` (`mock_stm32g0.c:77-89`) 仅在 PRIMASK 置位时延迟, 模型为
  "main-vs-ISR 延迟", 无法表达"USART 抢占 TIM3/DMA 上下文泵"的 ISR-vs-ISR 嵌套。
  `deliver_pending_irqs` 按枚举顺序投递, 无优先级概念。三个 `test_isr_*_deferred_*`
  测试对 main-vs-ISR 交错是良好覆盖, 但泵的重入风险在框架内无法测试。

#### C2 🟠 `injected_rx_irq` 强制重写 ISR 标志, 掩盖 ICR 清除语义

- `test_phy_l6362a_raw.c:78-86`: 调用向量前强制 `IOD_UART->ISR = USART_ISR_RTOF`。
  真实硬件上被延迟的向量读到的 ISR 中 RTOCF 已被临界区内的 `UartConfigure` 清除(B3)。
  对比: `injected_tx_dma_irq` (line 88-100) **手工建模了** IFCR→ISR 清除语义 —— 两个注入
  handler 保真度不对称。`test_isr_rx_during_set_mode` 的"帧必存活"结论对硅片过于乐观。

#### C3 🟡 未覆盖路径

- `l6362_tx_frame` 200us 超时/在途 DMA 拆除 (B2);
- OL IRQ 在 set_mode/set_baudrate 期间的到达 (`s_wake_latched` 共享);
- 传输中第二次 `tx_frame` 的碰撞;
- `L6362_OCTET_TIMEOUT_US` 等待超时后的 TX_EN 释放路径 (tc 永不到达的情形)。

#### C4 🟡 测试只覆盖 PHY 驱动, 不覆盖 PHY×DLL 泵整合

- `rx_frame → mailbox 发布次序`、eager 回复的缓冲稳定性、`request_service` 与自然数据
  中断的竞争, 在本测试中仅能靠读代码验证。库级 ISR 测试 (`test_dll_isr.c` 等) 同样由
  `test_io_cycle` 顺序驱动双泵 (`test_helpers.c:266-282`), 无真实交错模型。

#### C5 🔵 正面项

- mock 的 trigger/pend/`mock_set_primask` 延迟投递模型对 main-vs-ISR 交错表达清晰;
  `test_tx_frame_requires_isr_context` 钉住 `L6362_TX_ASSERT_ISR`;
  `test_platform_hooks` 覆盖临界区嵌套与不平衡退出;
  `test_baudrate_change_aborts_tx` / `test_rx_frame_wraps` 覆盖了中止与 wrap 线性化主路径。

---

## 3. 实际使用工况推演 (8110, COM3, 28B PD, Type 2_V, 1B OD)

| 工况 | 触发发现 | 表现 |
| --- | --- | --- |
| 稳态 OPERATE + 慢速 vendor ISDU 服务 (NVM 写 50-100ms, `iolink_isdu_process` 阻塞主窗口) | A2 | 主站周期帧由 ISR eager 应答(链路存活), 但 mailbox 丢帧计数增长, 服务结束后一次虚假 `on_master_lost`→`on_master_online` 翻转 |
| 主站建立通信连发 (探测→PREOPERATE→转换→首帧) 与主窗口阻塞重叠 | A3 | `on_preoperate`/中间状态回调可能丢失 |
| FALLBACK MasterCommand / 波特率恢复 COM1→COM3 时恰有回复在途 | A4 + B2 | 回复被 DMA 中止截断, 主站 CRC 重试自愈, t_REN 统计失真 |
| SIO 重入期间主站唤醒帧 (RTOR=1bit 窗口) | B3 | 唤醒帧丢失一次, 主站 T_DWU 重发自愈; 测试断言强于硬件行为 |
| mailbox 满 + t_A 看门狗/低优先级 feed 消费遗留帧 | B1 | 泵嵌套: 双分配槽覆盖、DMA 拆除、截断回复; 协议层重试自愈, 无内存破坏观察 |
| 移植未提供 app lock 或未覆盖平台临界区 | A1 | `isr_tx_next`/payload 槽/`isr_tx_len` 竞争, 回复损坏或丢帧, 无告警 |

**总体判断**: 未发现导致内存破坏或链路不可恢复的确定性缺陷; 所有高风险项的共同特征是
**竞态窗口依赖"主站违反周期/主窗口阻塞/中断优先级交错"组合触发, 且协议层 (CRC 重试、
T_DWU 重发) 能自愈**。真正的风险点在于诊断与回调正确性 (A2/A3) 与移植配置陷阱 (A1)。

---

## 4. 整改优先级建议

1. **P0** A1: `dll_send` 锁兜底 + 平台弱默认对 ISR 泵构建强制报错 —— 一行级修复, 消除
   配置陷阱。
2. **P1** B1 + B2: 泵收敛到单一 IRQ 上下文 (其余 feed 转 pend USART IRQ); `l6362_tx_frame`
   超时返回 busy。
3. **P2** A2: 每个合法 ISR 接收帧置 `isr_master_activity`, 或 master-lost 计算后移。
4. **P2** A3: `pending_state` 单槽 → 小型 FIFO 或按当前 state 补齐。
5. **P3** A4: FRAME TX 在途状态对 DLL 可见 (守卫询问 PHY), 修正注释。
6. **P3** 测试增强: mock 增加优先级/嵌套模型 (或至少文档化局限); `injected_rx_irq` 建模
   ICR 清除语义; 补 B2 超时路径与 OL-IRQ 交错用例。
