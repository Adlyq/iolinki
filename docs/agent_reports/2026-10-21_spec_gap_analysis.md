# iolinki 规范差距 / 正确性 / 一致性 分析报告

- 分析对象: `src/dll.c` / `src/frame.c` / `src/isdu.c` / `src/events.c` / `src/params.c` /
  `src/data_storage.c` / `include/iolinki/*.h`
- 参照基准:
  1. 库内 V1.1.5 规范 PDF `docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/IOL-Interface-Spec_10002_V1.1.5_Oct2025.pdf`(AGENTS.md 指定的唯一权威)
  2. ST 官方 mini-stack 逆向: `~/CLionProjects/st_mini_stack_逆向/6362|6364/inc/{iolinkstack.h,stackbase.h,stackevents.h}` + 样例工程
  3. 睿思行远 18 篇协议解读: `~/Downloads/IO-Link_睿思行远/*.md`
- 日期: 2026-10-21
- 严重度: 🔴 High(线缆级正确性) / 🟠 Medium(功能缺失或语义错误) / 🟡 Low(常量卫生/死代码/注释)

> 说明: 本报告定位的是"规范差距"与"正确性"问题;与 `docs/agent_reports/2026-10-21_concurrency_audit.md`
> (并发模型)互补。已被 `docs/SPEC_CONFORMANCE.md` 明确声明为"已知/延迟"的条目会在对应位置标注
> `[已在 SPEC_CONFORMANCE.md 记录]`,以免重复劳动。

---

## 1. 结论摘要

1. **流式(默认)传输下,读 M-sequence 永远无法收满** → 设备不响应循环读,属于最严重的线缆级缺陷(§2.2)。
2. **帧格式对所有 M-sequence 类型一刀切**,只有 TYPE_2_V 与规范 Figure A.14 相符,TYPE_1_1/1_2/1_V/2_1/2_2 均不符(§2.1)。
3. **DPP1 MinCycleTime 未按 Table B.3 编码**(§2.3)、**MseqCap 广告与实际线上行为不符**(§2.4)。
4. **DataStorage(0x0003)子索引 01–05 全部缺失**,校验和用 16-bit Fletcher 而非规范要求的 32-bit CRC/版本计数器(§2.8)。
5. **参数块管理(PM)状态机整个缺失**,`ParamDownloadStore(0x05)` 定义了但从未处理(§2.9)。
6. 若干 ISDU 错误码取错值 / 缺失(§2.5)、DeviceStatus 语义映射错误(§2.6)、事件确认误删未展示事件(§2.7)。
7. 与 ST mini-stack 相比缺少:SIO 驱动模式(NPN/PNP/inactive)、Transparent UART 模式、事件 NVM 备份/恢复、传感器读取周期调度(§3)。
8. 常量与注释存在多处自相矛盾(§4)。

---

## 2. 正确性 Bug(与 V1.1.5 PDF 直接冲突)

### 2.1 🔴 帧格式一刀切,仅 TYPE_2_V 符合规范

`src/dll.c:146-152`(`dll_expected_req_len`)对 Type 1/2 一律按 `MC(1) + CKT(1) + PDout(n) + OD(m)` 收帧;
实际组帧在 `dll.c:601-720`(注释 `dll.c:670-674` 明确说这是 Type-2_V 布局)。对照 Annex A:

| 类型 | 规范 Figure/Table | 规范请求布局 | 库的实际处理 | 结论 |
|---|---|---|---|---|
| TYPE_1_1 | Fig A.6 | `MC\|CKT\|PD`,`CKS` 回;无 OD | `od_len=1`(dll.c:779/device.c:76),等 OD 字节 | ❌ |
| TYPE_1_2 | Fig A.7 / Table A.10 "1\|2\|0\|0" | 无 PD,OD=2 双向 | 按 PD + OD 宽 1 处理 | ❌ |
| TYPE_1_V | Fig A.8 / Table A.10 "6\|8\|0\|0" | 无 PD,OD=8/32 | 按 PD + OD 宽 1 处理 | ❌ |
| TYPE_2_1/2_2 | Table A.10 / Fig A.9/A.10 | 仅 PDin(无 PDout) | 每个请求都期望 PDout | ❌ |
| TYPE_2_V | Fig A.14 | `PDout\|OD`(写)/`OD\|PDin\|CKS`(读) | 一致 | ✅ |

流式版 OD 长度固定 1/2(dll.c:779),8/32 只在 `IOLINK_PHY_FEATURE_ISR_PUMP` 下可达(device.c:89-91)。

### 2.2 🔴 流式传输下"读请求"永远收不满

`dll_expected_req_len`(dll.c:146-152)不区分 R/W 位,对所有 Type 1/2 帧返回 `2 + pd_out + od_len`,
被 `dll.c:1442` 使用、`dll.c:1459` 判定完成。按规范(A.14):读请求不带 OD 字节;Type 1 读请求甚至只有
`MC|CKT` 两字节。因此设备会等待永远不来的字节 → 命中 16-bit 字节间超时(dll.c:1331-1346)→
帧被丢弃 → **从不响应循环读**。

测试全绿的原因是 `tests/test_helpers.c:351-357` 按"实现布局"造帧(读也带 OD),测试自洽但不合规。
ISR 传输对读/写做了区分(`dll.c:1649`:`od_req = write ? od_width : 0`),但 Type 1 读仍假设请求带 PDout。

### 2.3 🟠 DPP1 MinCycleTime(0x02)未按 Table B.3 编码

`src/isdu.c:1095-1097` 把 `iolink_dll_compute_min_cycle_time()` 返回的 0.1ms 原始单位直接写入
`page[0x02]`。已核对 PDF Table B.3(p.261):

| 编码 | Time base | 计算 | 范围 |
|---|---|---|---|
| 00 | 0.1ms | Multiplier × 0.1ms | 0.4 – 6.3ms |
| 01 | 0.4ms | 6.4ms + Multiplier × 0.4ms | 6.4 – 31.6ms |
| 10 | 1.6ms | 32.0ms + Multiplier × 1.6ms | 32.0 – 132.8ms |
| 11 | Reserved | — | — |

原始值仅在 0x04–0x3F 有效。COM1 或大 PD(COM2 下 18ms → 180 → 0xB4)会被主站解码成 115.2ms。
库里有解码器 `dll_decode_cycle_time_ticks`(dll.c:901-916)却**没有编码器**;也无 0.4ms 下限钳位
(`iolink_dll_compute_min_cycle_time` 的 TYPE_0@COM3 可算出 ~0x02=200µs,落入 reserved 区间)。

### 2.4 🟠 MseqCap(DPP1 0x03)广告与实际线上行为不符

`src/isdu.c:1046-1076`(`direct_param_mseq_capability`)非 ISR 分支硬编码:
`1_2→code1`(2 OD 无 PD)、`1_V→code6`(8 OD 无 PD)、`2_V→code5`(OD 2 变长 PD)。但 DLL 实际:
`1_2/1_V` 收发 OD 宽 1 且带 PD(dll.c:779)。主站按广告发包,设备无法正确组帧。
ISR 版从真实 `od_width` 推导(isdu.c:1051-1054),但 `od_width` 默认 `od_len=1`,1_2 设备仍会
广告 code1(OD=2)却只交换 1 字节。此外 DPP1 的 PDIn/PDOut(isdu.c:1100-1101)始终按配置 PD 长度编码,
对 1_2/1_V(PD=0)造成页面自相矛盾。

### 2.5 🟠 ISDU 错误码取错值 / 缺失

- `IOLINK_ISDU_ERROR_BUSY = 0x30`(protocol.h:182)被当作负响应发出(`isdu.c:815,838,851`)。
  Table C.1 中 `0x30 = PAR_VALOUTOFRNG`;busy 只能是单字节 `0x01`(Table A.14)。`[已在 SPEC_CONFORMANCE.md 记录]`
- `IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL = 0x11`(protocol.h:176-177)实为 `IDX_NOTAVAIL`,
  在 ~15 处被当通用错误使用(`isdu.c:193,211,223,382,478,602,632,662,822,995,1196,1265,1287`)。
- 缺失(protocol.h:174-187):`0x21 SERV_NOTAVAIL_LOCCTRL`、`0x22 SERV_NOTAVAIL_DEVCTRL`、
  `0x31 PAR_VALGTLIM`、`0x32 PAR_VALLTLIM`、`0x33 VAL_LENOVRRUN`、`0x34 VAL_LENUNDRUN`、
  `0x36 FUNC_UNAVAILTEMP`、`0x82 APP_DEVNOTRDY`(规范 10.3.2 Table 96 明确需要)。
- `IOLINK_ISDU_ERROR_SEGMENTATION 0x81`(protocol.h:187)与 vendor-specific 0x8100+ 前缀冲突,
  仅存于 `ctx->error_code`、从不发出,无害但易误导。

### 2.6 🟠 DeviceStatus(0x0024)/DetailedDeviceStatus(0x0025)语义错误

- `events.c:205-239`(`iolink_events_get_highest_severity`)把 Error→3、Notification→1。Table B.13 定义
  0=OK、1=Maintenance、2=Out-of-Spec、**3=Functional-Check、4=Failure**。因此:Error 应报 4 却报 3;
  Functional-Check 从不产生;Notification 反而抬高状态。`protocol.h:223-227` 同样把 3 命名为 "Failure" 并漏掉 4。
- `isdu.c:909-963`(0x0025)把 Notification 事件也纳入;规范 B.2.22 只要求 "Error or Warning + MODE appears";
  其 RecordT 子索引结构(Table B.14)被忽略,整块返回;无 disappears 条目。

### 2.7 🟠 事件确认(StatusCode 写)误删未展示事件

`events.c:143-148`:confirm 时 `pop(min(count,6))`。若冻结期间新事件入队,confirm 时刻的 `count`
大于冻结时展示的槽位数,从未被主站读到的事件被静默丢弃(默认 `IOLINK_EVENT_QUEUE_SIZE=4` 时仅特定路径触发)。
另有:冻结时机在首次主站读时(events.c:121-125),而 Table 60 T3 要求置 EventFlag 时即冻结。

### 2.8 🟠 DataStorage(0x0003)子索引缺失 + 校验和不符合规范

- `isdu.c:1191-1230`(`handle_data_storage`)把整个 index 当一块不透明 blob:读任意子索引→整图,
  写→应用整图。缺失 Table B.10 全部子索引:
  `01 DS_Command`、`02 State_Property`(含 DS_UPLOAD_FLAG bit7,只读)、`03 Data_Storage_Size`(UInt32,≤2048)、
  `04 Parameter_Checksum`(UInt32 CRC/版本计数器)、`05 Index_List`。
- 睿思行远第 18 篇的 CheckMemSize(读 03 03)/Checksum(读 03 04)/StateProperty(读 03 02)流程无法走通。
- 校验和为 Fletcher-16(data_storage.c:81-96);规范要求 32-bit CRC 签名或版本计数器(10.4.8/Annex K)。
- 默认 `IOLINK_DS_IMAGE_MAX=256`(data_storage.h:37-39)合法但无法上报 size(因子索引 03 不存在)。
- DS 锁(bit1)未在 index 0x0003 写路径强制(isdu.c:1203-1212 不检查 `IOLINK_LOCK_DS`);上传也不受锁约束。

### 2.9 🟠 参数块管理(PM)状态机缺失 + SystemCommand 语义错误

- `IOLINK_CMD_PARAM_DOWNLOAD_STORE 0x05`(protocol.h:140)**定义了但 `handle_system_command` 无对应 case**
  (isdu.c:779-877)→ 落入 default → `0x8035 FUNC_NOTAVAIL`。规范 10.3.2(Figure 86)要求。
- 块参数写入无缓冲、无 DownloadEnd 原子校验、无回滚:Param* 命令被误路由进 DS 引擎
  (isdu.c:804-830 → `iolink_ds_handle_command`,data_storage.c:300-358),把 PM(10.3)与 DS(10.4/11.4)机制混为一谈。
- `0x83` 被实现为"恢复出厂默认"(isdu.c:795-798);规范 10.7.5 要求 **Back-to-box**:恢复交付值并
  **停止/禁止通信直至重新上电**(≥3 主站周期延迟、驱动禁用)。
- 遗留 `0x95/0x96` 被接受为 DS 上传/下载(isdu.c:833,847),但 Table B.9 中 0x84–0x9F 为 Reserved。
  注释 "0x95 -> 0x07 Start Upload" 中 0x07 也是 Reserved(UploadStart 实为 0x01)。

### 2.10 🟠 写通道 FlowCTRL ABORT 被吞

`isdu.c:323-325`(`iolink_isdu_od_write` 开头)对 `flowctrl >= IOLINK_FLOWCTRL_IDLE` 直接 return,
吞掉了 0x1F(ABORT)。Table 52 要求 ABORT 时设备拒绝已收请求数据并复位传输,与方向无关。当前只能靠
下一个写 START(isdu.c:327-333)恢复。读通道的 ABORT 正确复位(isdu.c:407-410)。

---

## 3. 与 ST mini-stack 对比的欠缺

ST 逆向头文件(`stackbase.h` / `stackevents.h` / `iolinkstack.h`)暴露的能力与 iolinki 现状对照:

| ST 能力 | ST 出处 | iolinki 现状 |
|---|---|---|
| SIO 驱动模式 `DRIVE_MODE_PUSH_PULL/NPN/PNP/INACTIVE` | stackbase.h:73-81 | 仅 `set_cq_line`(phy.h:235),无 NPN/PNP/inactive 概念 🟡 |
| Transparent UART 模式(`StackSuspendUart/ResumeUart/PhyIntHandler`) | stackbase.h:308-312 | 无 🟡 |
| `StackBaseSetMinCycTime(m,n,baud,m_seq_time_usec,idle,min)` 含 idle 输入 | stackbase.h:226 | `iolink_dll_compute_min_cycle_time` 无 m/n/idle 输入,公式硬编码(dll.c:1489-1509) 🟡 |
| 事件 NVM 备份/恢复(`StackEventsReplaceFromBackup/SetTriggerRestore`) | stackbase.h:294-298 | 无(事件掉电即失) 🟡 |
| 传感器读取周期调度(`StackBaseSensorsReadPeriod` + `CanRunUserCode/CanRunUserSensorCode`) | stackbase.h:236/198-200 | 无,完全交给应用 🟡 |
| 热关断温度设置 `StackBaseSetThermalShutdownTemp` | stackbase.h:224 | 无(依赖 PHY 驱动自行配置) 🟡 |
| LED 控制 `StackBaseSet/GetLedLevel` | stackbase.h:184-186 | 无(应用层职责,影响小) 🔵 |
| ISDU 便捷 API `StackISDU8/16Bit/StrData{Read,Write}` | stackbase.h:282-292 | 由 `iolink_params_ctx_{get,set}` 覆盖 ✅ |
| 运行时 SIO/IO-Link 切换 `StackBaseSetStackMode/GetStackMode` | stackbase.h:202-204 | `iolink_phy_mode_t` + `set_mode` 覆盖 ✅ |
| ISDU 状态机 `HandlerState`(IDLE/RECEPTION/PROCESSING/TRANSMISSION) | stackbase.h:84-96 | 有等价状态,但 `ISDU_STATE_BUSY` 从未赋值(见 §4.7) 🟡 |
| M-seq capability 支持 OP code 0/1/4/5/6/7 + PREOP code 0/1/2/3 | iolinkstack.h:160-186 | PREOP 仅 Type 0(code 0);流式版 OP code 固定 0/1/5/6(见 §2.4) 🟠 |
| 事件码全集(含 0x1800/0x1801/0x189C DIAG 等旧 V1.1.2 定义) | stackevents.h:47-165 | events.h 采用 V1.1.5 Table D.1 集(0x18xx 已改 vendor-specific);两套版本不同,见 §4.1 🟡 |

> 关于事件码版本差异:ST 头文件按 V1.1.2 定义 `UNDER_VOLTAGE=0x1800/DEVICE_OVERTEMP=0x1801/DIAG=0x189C`;
> V1.1.5 PDF Table D.1 已把 `0x1800–0x18FF` 划为 **Vendor specific**。iolinki 采用 V1.1.5 是对的,
> 但 protocol.h 里残留旧 V1.1.2 语义的 `IOLINK_EVENT_CODE_COMM_ERR_*`(§4.1)。

---

## 4. 内部不一致(常量/注释/死代码)

### 4.1 🔴 事件码定义互相打架

- `protocol.h:203-205`:`COMM_ERR_GENERAL 0x1800`、`COMM_ERR_FRAMING 0x1801`、`COMM_ERR_CRC 0x1803`
- `events.h:48-51`:`COMM_CRC 0x1801`、`COMM_TIMEOUT 0x1802`、`COMM_FRAMING 0x1803`、`COMM_TIMING 0x1804`

同一数值(0x1801/0x1803)在不同文件被赋予不同名称;且 0x1800–0x18FF 在 V1.1.5 是 vendor-specific,
protocol.h 的 "comm error" 命名已过时。

### 4.2 🔴 `events.h` 内部值冲突(同一数值多个语义)

- `IOLINK_EVENT_APP_TEMP_UNDERFLOW 0x5110`(events.h:54)= `IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN 0x5110`(events.h:80)
- `IOLINK_EVENT_HW_GENERAL_FAULT 0x6000`(events.h:59)= `IOLINK_EVENTCODE_SOFTWARE_FAULT 0x6000`(events.h:82)
- `IOLINK_EVENT_HW_SENSOR_FAULT 0x6320`(events.h:61)= `IOLINK_EVENTCODE_PARAMETER_ERROR 0x6320`(events.h:83)

### 4.3 🟡 协议版本标注混乱

`protocol.h:14` 写 "Spec V1.1.2",而 AGENTS.md 与 SPEC_CONFORMANCE.md 均声明以 V1.1.5 PDF 为准。

### 4.4 🟡 DS 命令文档/宏值冲突

- `data_storage.h:158` 注释 "System Command 0x95"(实为 MASTER_IDENT)。
- `IOLINK_CMD_PARAM_UPLOAD 0x95`(protocol.h:153)= `IOLINK_CMD_MASTER_IDENT 0x95`(protocol.h:69);
  `IOLINK_CMD_PARAM_DOWNLOAD 0x96` = `IOLINK_CMD_DEVICE_IDENT 0x96`。
- 注释 "0x97 / Standard Break"(data_storage.c:350、isdu.c:860)但 ParamBreak 实为 0x06(protocol.h:155)。

### 4.5 🟡 0xFF91 DS_UPLOAD_REQ 从不触发

`iolink_event_classify` 只分类(events.c:266),DS 引擎 `iolink_ds_check` 仅置 `UPLOAD_REQ` 状态
(data_storage.c:210-212)却从不 `iolink_event_trigger(...,0xFF91)`。规范 10.4.2 要求在本地参数变更 /
Application Reset / ParamDownloadStore 时产生该事件。

### 4.6 🟡 死代码 / 未接线的配置项

- `IOLINK_OPERATE_TIMEOUT_MS`(config.h:256-258)定义未使用;master-lost 默认关闭(0)且触发后仅置标志,
  **不回 SIO**(dll.c:1241-1257, device.c:265-274)。
- `IOLINK_EVENT_MIN_APPEAR_MS`(config.h:190-192)、`IOLINK_OD_EVENT_MODE`(config.h:289-290)定义了但
  src/ 无引用 → 事件"消失/单次"模式未实现(§2.7 关联)。
- `retry_count` / `max_retries` 死字段(dll.h:256-258);`ISDU_STATE_BUSY` 从未赋值;
  `repeat_pending` 块(isdu.c:358-361)死代码;`IOLINK_ISDU_CTRL_START/LAST`(protocol.h:82-83)旧控制字遗留、无引用。
- `iolink_ds_check` / `iolink_ds_process` 无生产代码调用(仅 tests/test_ds.c)。
- `iolink_dll_on_rx_timeout` 在流式版是空操作(dll.c:2544-2560,主体在 `#if ISR_PUMP`)。
- `iolink_dll_prepare_cyclic_reply` 是文档化 no-op(dll.c:1621-1624);`cyclic_reply_precached` 遗留标志(dll.h:118)。
- 死常量 `IOLINK_MC_TRANSITION_COMMAND 0x0F`(protocol.h:50)是非规范构造,处理分支(dll.c:1004/1039/1439/1646)不回应。

### 4.7 🟡 其他

- DPP1 0x0F(SystemCommand)页写未处理(page 写分发只处理 0x01,dll.c:496-499)。
- 遗留宏 `IOLINK_IDX_VENDOR_ID/DEVICE_ID`(protocol.h:106-107)保留(AGENTS.md 已声明待删除)。
- `dll.h:112` 注释 "OD length (1 or 2 bytes)" 已过期(存在 1/2/8/32 的 `od_width`)。
- 0x000C/0x000D/DPP2 的 RecordT 子索引被忽略(isdu.c:887-905,527-533)。
- 短访问锁写入(0x000C,<2 字节)被静默 ACK,应报 `0x8034 VAL_LENUNDRUN`(isdu.c:897-900)。
- 0x4000–0xFFFF 全部路由到 vendor service(isdu.c:1241),未区分 profile/safety/wireless/reserved 区间。
- `iolink_dll_compute_min_cycle_time` 公式注释 "reply_octets*14 + request_octets*12" 基于 Type-2_V 布局,
  对其它类型(§2.1)不成立。

---

## 5. 睿思行远文章印证的行为点(已实现 ✓)

以下行为与睿思行远解读一致,已在 iolinki 中正确实现(用于区分"缺失"与"已达标"):

- 唤醒后按 COM3→COM2→COM1 探测,主站间隔 500ms、3 次重试 → 设备侧被动匹配速率(第 5 篇);iolinki 固定单一 COM,符合 §7.3.2.2("Device shall support only one")。
- Fallback 命令后 ≤500ms 切 SIO(第 5 篇);iolinki `3×min_cycle, cap 500ms`(dll.c:362-371/2010-2016)✅。
- PREOP→OP 时主站发 `DL_Write(0x01, MasterCycleTime)`(第 9 篇);iolinki 页写 addr 0x01 已处理(dll.c:496-499)✅。
- MC bit7=R/W、bit6-5=通道、低 5 位地址/FlowCTRL(第 7 篇);iolinki `IOLINK_MC_*` 常量正确(protocol.h:40-48)✅。
- CKT checksum 种子 0x52、6-bit 压缩(第 7 篇);iolinki `iolink_checksum6`(crc.c:36-71)已核对 ✅。
- ISDU FlowCtrl 计数递增/重发、I-Service 读写命令(第 11 篇);iolinki 写侧正确,读侧重发缺失(§2.2/§5-FlowCTRL)⚠️。
- 事件 6 槽 ×3 字节 + 1 StatusCode = 19 字节,EventFlag 置 CKS bit7,读取时冻结内存(第 12 篇);
  iolinki 内存布局/标志正确,冻结时机略偏(§2.7)⚠️。
- MinCycleTime/MasterCycleTime timebase|multiplier 编码(第 10 篇);iolinki 有解码无编码(§2.3)⚠️。
- DS:查询 Size(03 03)、StateProperty bit7(03 02)、Checksum(03 04),≤2048 字节(第 18 篇);iolinki 子索引全缺(§2.8)❌。
- 参数块整体校验/回滚(第 16 篇);iolinki PM 状态机缺失(§2.9)❌。

---

## 6. 修复优先级建议

| 优先级 | 项 | 严重度 |
|---|---|---|
| P0 | §2.2 读请求收帧 + §2.1 分类型帧布局(核心线缆级正确性) | 🔴 |
| P0 | §2.3 MinCycleTime B.3 编码(含 0.4ms 下限钳位) | 🟠 |
| P1 | §2.4 MseqCap 与实际布局对齐 | 🟠 |
| P1 | §2.8 DS 子索引 01–05 + 32-bit 校验和 | 🟠 |
| P1 | §2.9 块参数 PM 状态机(含 0x05 DownloadStore、0x83 Back-to-box) | 🟠 |
| P1 | §2.5 ISDU 错误码修正/补齐(0x30/0x11 误用,0x21/22/31-36/82) | 🟠 |
| P2 | §2.6 DeviceStatus/DDS 语义、§2.7 事件确认/冻结 | 🟠 |
| P2 | §2.10 写通道 ABORT 复位 | 🟠 |
| P3 | §4 常量冲突清理(events.h 0x5110/0x6000/0x6320、protocol.h 事件码、0x95/0x96 宏) | 🟡 |
| P3 | §3 SIO 驱动模式 / transparent UART / 事件 NVM 备份(按需) | 🟡 |

---

## 7. 附录:关键规范条文核对记录

- **Table B.3**(p.261):MinCycleTime timebase|multiplier,原始 0.1ms 仅 0.4–6.3ms;0x40 起必须编码 → §2.3 依据。
- **Table D.1**(p.283):`0x1800–0x18FF = Vendor specific`、`0x1000 = General malfunction` → §4.1 依据。
- **Table D.2**(p.284):`0x1800 = No Device (Port)` → 与 device 侧 vendor-specific 用法靠 SOURCE 位区分 → events.h 注释正确。
- **Table A.14**:Length=0 "No service";Length=1 "Device busy"(单字节 0x01);ExtLength 窗口 17–238;length 永不为 16 → isdu.c 实现正确(与旧规范"15 表示 ext"不同,勿混淆)。
- **Table C.1**:0x8011/0x8012/0x8020/0x8023/0x8030(PAR_VALOUTOFRNG)/0x8035/0x8040/0x8041 → §2.5 依据。
- **Table B.13**:DeviceStatus 0=OK,1=Maintenance,2=Out-of-Spec,3=Functional-Check,4=Failure → §2.6 依据。
- **Table B.10**:DataStorageIndex 子索引 01 DS_Command / 02 State_Property / 03 Size(UInt32) / 04 Checksum(UInt32) / 05 Index_List → §2.8 依据。
- **Table B.9**:0x01 UploadStart / 0x02 UploadEnd / 0x03 DownloadStart / 0x04 DownloadEnd / 0x05 DownloadStore / 0x06 Break;0x84–0x9F Reserved → §2.9 依据。

---

## 8. 修复状态（后续更新）

> 本报告为初始分析。以下项目已在后续提交中修复；未列出的仍按第 6 节优先级处理。

### 已修复

- §2.3 MinCycleTime B.3 编码 + 0.4 ms 下限。
- §2.4 MseqCap（ISR 版由 `od_width` 推导；TYPE_1_2 默认 2 OD）。
- §2.5 ISDU 错误码（busy → 0x20、补齐 Table C.1、写通道 ABORT）。
- §2.6 DeviceStatus/DDS（B.13 映射、过滤 Notification）。
- §2.7 事件确认只确认已展示槽位；内存改为 EventFlag 置位时冻结（Table 60 T3）。
- §2.8 DS 子索引 01-05、Fletcher-32、Index_List、State_Property。
- §2.9 PM/SystemCommand（块参数缓冲 + 原子提交、`ParamDownloadStore` 0x05、0x83 Back-to-box）。
- §2.10 写通道 ABORT。
- §4 常量冲突（`APP_*`/`HW_*` 重复、0x18xx 旧别名）。
- 额外：0x000C/0x000D 子索引、DPP1 0x0F SystemCommand、DS 下载错误分类（0x8040/0x8041）、事件 appears/disappears 生命周期、统一 NVM 后端（callback-only、固定布局、magic/version、Fletcher-32）、DS 参数访问改进（`set_param_desc`/`max_len`）。

### 仍未修复

- 流式传输的帧格式/读请求（§2.1/§2.2，仅流式版；ISR 版已按类型区分读/写）。
- TYPE_1_2/1_V 的“无 PD”语义（ISR 帧布局仍假设 PDout；当前 2_V 不受影响）。
- MasterIdent/DeviceIdent（0x95/0x96）未做 ident 校验（规范为可选特性）。
- 与 ST 对比的差异化能力：SIO 驱动模式（NPN/PNP）、transparent UART、事件 NVM 备份、传感器读取周期调度。
- t_A 注释已确认为笔误（13 bit 是 byte-gap，不是 t_A 上限；规范 1–10 bit）。
