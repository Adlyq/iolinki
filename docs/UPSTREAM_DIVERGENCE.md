# UPSTREAM_DIVERGENCE.md

本文件记录 `own_dev` 相对 `upstream/develop` 的功能分歧，作为同步、cherry-pick 和向上游提 PR 的单一事实来源。

- 基线：`upstream/develop`（`aaefa3e`），`own_dev` 为 `0 behind / 99 ahead`。
- 上游的线层 conformance（A.1.6 校验、CKS 标志、CKT 分发、ISDU FlowCTRL/CHKPDU、Table 58 事件内存、T_REN/T_WU/T_DSIO/T_FBD）已合入 develop，我们继承，**不在本表重复**。
- 本表只列"我们 ahead、上游缺失或仍为旧值/非规范"的分歧。

## 分类图例

| 标记 | 含义 | 处理 | PR |
| :--- | :--- | :--- | :--- |
| `PR#1` | 无争议的规范强制纠偏 | 单独分支 port 到 upstream/develop 提 PR | #44 |
| `PR#2` | 语义/设计分歧 | 单独分支 + 设计说明提 PR | #45 |
| `PR#3` | PD 数据索引 + ProfileCharacteristic ArrayT | 单独分支提 PR | #46 |
| `记录` | 需先按 PDF 定性，暂不 PR | 只记录，待定 | — |
| `私有` | fork 自有（transport/storage/examples） | 不提 PR | — |

---

## 规范裁决（V1.1.5 核对结果）

按 PDF（`docs/IOL-Interface-Spec_10002_V1.1.5_Oct2025/`）逐条核对，**全部 18 个分歧点均采信我们的实现**（上游在每一处均为缺失或旧值/非规范）：

| # | 分歧点 | 规范依据 | 采信 |
|:-:|:---|:---|:-:|
| 1 | 保留索引 `0x0004-0x000B`；VendorID/DeviceID 不当 ISDU 索引 | Table B.8 `0x0004-0x000B Reserved` | 我们 |
| 2 | `ErrorCount 0x0020` | Table B.8 `0x0020 ErrorCount R 2 octets` | 我们 |
| 3 | `ProductURI 0x001B` | Table B.8 `0x001B Product-URI R max 100` | 我们 |
| 4 | `ProcessDataInput 0x0028` | Table B.8 `Read last valid Process Data from PDin` | 我们（PR#3） |
| 5 | `ProcessDataOutput 0x0029` | Table B.8 `Read last valid Process Data from PDout` | 我们（PR#3） |
| 6 | 只读标识对象拒写 | B.2.8–B.2.15 `read-only` + Table C.1 `0x23` | 我们 |
| 7 | 参数写访问锁 | B.2.4 `shall respond … access denied … if locked` | 我们 |
| 8 | 单元素 subindex≠0 | Table C.1 `0x80 0x12 SUBIDX_NOTAVAIL` | 我们 |
| 9 | fixedLength 截断 | B.2.8–B.2.19（64 / Serial 16 / URI 100 / Tag 32） | 我们 |
| 10 | `ProfileCharacteristic` ArrayT | Table B.8 `ArrayT of UIntegerT16` + B.2.5 条件 | 我们（PR#3） |
| 11 | SystemCommand 码 | Table B.9（`0x01-0x06`；`0x84-0x9F Reserved`；`0x83 Back-to-box`） | 我们 |
| 12 | `0x95/0x96/0x97` 是 MasterCommand | Table B.2（MasterIdent/DeviceIdent/DeviceStartup） | 我们 |
| 13 | DeviceStatus 0–4 + 优先级 | Table B.13（4=Failure prio 1 … 0=OK prio 5） | 我们 |
| 14 | DDS 语义（仅 appears 的 Error/Warning） | B.2.22 | 我们 |
| 15 | Event memory 冻结时机 | 7.2.1.17 `DL_EventTrigger … freezes the Event memory` | 我们 |
| 16 | appears/disappears + 1s 限流 + 50ms 最短 + 优先级 | 10.10.2 | 我们 |
| 17 | MinCycleTime 编码 | B.1.3 / Figure B.2（time base 7:6 + multiplier 5:0）+ Table B.3 | 我们 |
| 18 | DS subindex（DS_Command/State_Property/Size/Checksum） | Table B.10 | 我们（暂缓/另议） |

---

## 1. ISDU 索引级 conformance（`src/isdu.c`）

| 行为 | 上游（`upstream/develop`） | 我们（`own_dev`） | 分类 |
| :--- | :--- | :--- | :---: |
| 保留索引 `0x0004-0x000B`、`0x001C-0x001F` | 无显式保留，未知索引落 default `0x11` | `isdu.c:545` 注释 + default IDX_NOTAVAIL | `PR#1` |
| `VendorID/DeviceID` 当 ISDU 索引 | `protocol.h:101-102` 仍为 `0x000A/0x000B` 并服务（`isdu.c:458/466`） | `protocol.h:108-109` 标 legacy 待删，不服务 | `PR#1` |
| `ErrorCount 0x0020` | 常量有（`protocol.h:116`）但无 case；仅在 `ERROR_STATS=0x0040` 服务（`isdu.c:852`） | `8b793d3` + `isdu.c:567` | `PR#1` |
| `ProductURI 0x001B` | 无 | `c797f78` + `isdu.c:611` | `PR#1` |
| `ProcessDataOutput 0x0029` | 无常量、无 case | `b5c3711` + `isdu.c:732` | `PR#3` |
| `ProcessDataInput 0x0028` 语义 | 返回 1 字节 PD 长度描述符（`isdu.c:599`） | 返回最后有效 PD-in 数据（`a9251fb` + `isdu.c:712`） | `PR#3`（原 `记录`，规范已定性） |
| 只读标识对象拒绝 WRITE | 无类型检查，WRITE 会回读字符串（`isdu.c:484-507`） | `ad3475f` + `isdu.c:522-527` | `PR#1` |
| 参数写访问锁（Parameter lock） | `handle_access_locks` 不查锁（`isdu.c:765`） | `2071e28` + `isdu.c:506` | `PR#1` |
| 单元素对象 subindex≠0 | 无检查 | `a49e62d` + `isdu.c:540` | `PR#1` |
| 标识字符串 fixedLength 截断 | 只截 `sizeof(response_buf)` | `a8e8d22` | `PR#1` |
| `ProfileCharacteristic 0x000D` | 当作单元素 | ArrayT（sub 0/1，0 时 IDX_NOTAVAIL）`isdu.c:557` | `PR#3` |
| 未知索引写 → `0x8011` | default `0x11` | `b200e5d` | `PR#1` |

## 2. SystemCommand 码（`include/iolinki/protocol.h` + `src/isdu.c`）

| 行为 | 上游 | 我们 | 分类 |
| :--- | :--- | :--- | :---: |
| Param* 码（Table B.9） | 旧值：DownloadStart..UploadEnd 落在 `0x05-0x08` | `0x01/0x02` Upload、`0x03/0x04` Download、`0x05` Store、`0x06` Break（`protocol.h:138-157`） | `PR#1` |
| `0x84` | `protocol.h:144` 仍为 `SET_COMM_MODE 0x84`，当 no-op | 标记 `0x84-0x9F` 保留（`isdu.c:861`） | `PR#1` |
| `0x95/0x96/0x97` | 当 legacy DS 命令 | 当 MasterCommand（MasterIdent/DeviceIdent/DeviceStartup，`protocol.h:69-71`） | `PR#1` |
| 未知命令 → `0x8035` | develop 已含（`protocol.h:170`） | 一致 | 无分歧 |
| `0x11` 命名 | `SERVICE_NOT_AVAIL` | `9b73737` 别名 `IDX_NOTAVAIL` | `PR#1`（可选，别名） |

## 3. Events（`src/events.c`）

| 行为 | 上游 | 我们 | 分类 |
| :--- | :--- | :--- | :---: |
| 冻结时机 | 首次读时冻结（`events.c:119` "first readout freezes"） | 置 Event flag 时冻结（T3）`f901567` + `events.c:149` | `PR#2` |
| appears/disappears 生命周期 | 无（`event_qualifier(type)` 无 mode，恒 appears） | `f557be4` + `events.c:212` `iolink_events_process` | `PR#2` |
| 确认时只确认已呈现槽 | 无 frozen_slots 概念 | `3e38979` + `events.c:275-306` | `PR#2` |
| DDS 语义 | 读队列前 8 个（含消失） | 读 pending[] 中 Error/Warning（disappear-aware）`766bc8c` | `PR#2` |
| EventCode 去重 | — | `073141f` | `PR#1`（纯常量纠偏） |
| EventQualifier instance | 有（0x04 Application） | 一致 | 无分歧 |

## 4. DPP1 / 时序

| 行为 | 上游 | 我们 | 分类 |
| :--- | :--- | :--- | :---: |
| MinCycleTime 编码 | `page[0x02] = info->min_cycle_time` 原值直写（`isdu.c:985`） | 按 Table B.3 `iolink_cycle_time_encode`（`accf77c` + `isdu.c:1202`） | `PR#1` |
| M-seq capability 编码 | `direct_param_mseq_capability(m_seq)` 不含 OD 宽度 | 含 `od_width`（`isdu.c:1147`） | `PR#1`（依赖 8/32 OD 宽度） |

## 5. Data Storage（`src/data_storage.c` + `src/isdu.c:handle_data_storage`）

| 行为 | 上游 | 我们 | 分类 |
| :--- | :--- | :--- | :---: |
| DataStorageIndex subindex 02-05 | 无 | `12128ae` | `PR#2`（或暂缓，见 §7） |
| DS_Command（subindex 01） | 无 | `8b0878c` | `PR#2` |
| ParamDownloadStore（0x05） | 无 | `d947576` | `PR#2` |
| 块参数原子下载 | 无 | `8c0627b` | `PR#2` |
| Fletcher-32 校验 | 上游用 `3b66235` checksum 恢复（另有路线） | `fe938c8` 扩到 Fletcher-32 | `私有`/`PR#2`（路线冲突，另议） |
| 可绑定参数集 + 统一访问器 | 无 | `65c7a6a`、`35355f8` | `私有` |

## 6. 记录项（已裁决）

### `ProcessDataInput 0x0028` 语义（`已裁决 → PR#3`）

- 上游：返回 1 字节"PD 长度描述符"。
- 我们：返回"最后有效的 PD-in 数据"（`a9251fb`）。
- **规范裁决**：Table B.8 `0x0028 ProcessDataInput … Read last valid Process Data from PDin channel`——**采信我们**，已由 PR#3（#46）提交。

### `0x84` / `0x95-0x9A` 的分支口径

- 上游把 `0x95/0x96/0x97` 当 legacy DS 命令、`0x84` 当 SetCommunicationMode。
- 我们按 Table B.9 / Table B.2 把它们归为 MasterCommand / reserved。
- 属 PR#1（#44），规范依据见 Table B.9（`0x84-0x9F Reserved`）与 Table B.2。

## 7. fork 私有（不提 PR）

- ISR 双泵 transport（`feat(dll): shared transmit seam + opt-in ISR two-pump` 系列，`src/dll.c`、`phy.h`、`dll.h`）。
- 统一 NVM 存储后端 + 固定 persistent 区域布局（`feat(storage): unified NVM backend`）。
- DS 可绑定参数集/访问器（与上游 DS 路线不同）。
- `frame.c` 主站侧 helper（若删 streaming 后仅测试引用）。
- virtual_master、STM32G0+L6362A raw PHY 示例、文档。

---

## PR 拆分计划

| PR | 内容 | 对应提交 | 依赖/风险 |
| :--- | :--- | :--- | :--- |
| PR#1 | §1 + §2 + §4（索引/SysCmd/DPP1 编码/EventCode 去重） | `6f2f0df`、`b47d9d4`、`8b793d3`、`c797f78`、`b5c3711`、`ad3475f`、`2071e28`、`a49e62d`、`a8e8d22`、`b200e5d`、`accf77c`、`9b73737`、`073141f` | 需适配上游 reentrant 上下文与 storage API |
| PR#2 | §3（events freeze/disappears/DDS/ack-slots） | `f557be4`、`f901567`、`3e38979`、`766bc8c` | 依赖 PR#1 的索引层？events 与 DDS 耦合，见下 |
| 暂缓/另议 | §5 DS + Fletcher-32、`0x0028` 语义 | — | 先定 PDF 语义；DS 与上游 checksum 路线冲突 |

> PR#2 的 DDS 语义（读 pending[]）依赖我们 events 的 pending 生命周期；若 PR#1 先合、PR#2 后合，PR#2 需在 PR#1 之后 rebase。
>
> 所有 port 都需"按上游 API 重贴行为"，不是 `git cherry-pick` 直接可用——上游已重构为 reentrant 上下文（`iolink_device_info_get()` + 参数 ctx），我们的实现建立在自有 storage/params/DS 上。
