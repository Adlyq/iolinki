# 持久化与数据存储（DS）

本文说明参数值如何由应用持久化，以及 Data Storage（DS）参数集如何从对象字典
（OD）生成、校验和恢复。库不拥有任何非易失性存储（NVM）：掉电保存的介质、
格式、校验和写入时机都由应用决定。

## 1. 职责边界

| 职责 | 归属 |
| --- | --- |
| 参数值的 RAM 副本与 OD 表 | 应用：表项直接绑定应用变量 |
| NVM 布局、记录格式、写入时机 | 应用：在表项 `on_change` 中处理 |
| DS 参数集成员判定 | 库：表项的 `IOLINK_OD_STORAGE_DS` 位 |
| DS 上传内容的序列化、下载的预校验与应用 | 库 |
| `DS_UPLOAD_FLAG` 的保存 | 应用：`iolink_device_config_t::ds_flag_persist` |

库没有存储后端接口，也没有固定的 NVM 区域布局；`iolink_device_config_t` 只包含
运行期配置。参数表项（`IOLINK_OD_SCALAR_STORAGE` / `IOLINK_OD_BYTES_STORAGE` 及其
`_EX` 变体）把值直接落在应用变量上，因此应用本地修改同一个变量后，主站立刻能读到
新值。

### 1.1 写后通知 `on_change`

每次通过 ISDU 或 DS 写入成功后，库调用表项的 `on_change`：

```c
typedef iolink_result_t (*iolink_od_change_fn)(iolink_app_t* self, const iolink_od_entry_t* entry,
                                               const uint8_t* data, size_t len);
```

| 项目 | 约定 |
| --- | --- |
| 调用时机 | 值已写入后端之后（DS 下载推迟到应用阶段，见 4.2）；`IOLINK_OD_WRITE_TRIGGER` 表项不写后端，钩子就是动作本身 |
| 参数 | `self` 来自设备实例（`iolink_device_app_t::self`）；`data`/`len` 是线上字节（大端） |
| 返回值 | 0 表示成功；正的 ISDU ErrorCode 原样上送主站；负值也形成否定应答（普通 ISDU 写为 0x11，DS 下载使整次下载以 0x41 失败） |
| 失败处理 | STORAGE 标量回滚到写前值；OctetString 不回滚 |
| 子索引 | 钩子看不到请求的子索引；多元素对象按 `entry->subindex`/`entry->max_subindex` 自行区分 |
| 执行上下文 | 主上下文（`iolink_device_process()` 的 ISDU 服务），不在数据 ISR 中 |

因为 OctetString 不回滚，需要在写入前拒绝的约束应放在 `validate` 钩子里。应用可在
`on_change` 中立即写 NVM，也可只置脏标志、在主循环合并写入；库不关心具体策略。

上电时应用把 NVM 中的值填回自己的变量（包括标签缓冲）。在 `iolink_device_init()`
之前填回即可直接生效；在之后填回则要调用 `iolink_ds_refresh()`，让
`Data_Storage_Size`/`Parameter_Checksum` 反映恢复后的参数集。库不读取 NVM，也没有
“加载持久化镜像”的入口。

### 1.2 建议的保存策略

- 逐次写入：在 `on_change` 中直接写一条记录，返回非 0 让主站知道保存失败。
- 合并写入：`on_change` 只更新脏标志，主循环统一提交；提交失败无法再回滚已经生效
  的 RAM 值，只能由应用自行标记参数集不一致。
- DS 下载期间 `on_change` 只在整份镜像应用时逐条触发（见第 4 节），因此一次下载
  会产生多次通知；需要“一次下载一次写”的应用应自行合并。

## 2. storage 位与写语义

表项的 `storage` 字段只有两个取值：

| 值 | 含义 |
| --- | --- |
| `IOLINK_OD_STORAGE_VOLATILE`（0） | 不属于 DS 参数集 |
| `IOLINK_OD_STORAGE_DS` | 属于 DS 参数集（10.3） |

该位只决定 DS 成员资格，不决定是否落盘。带 DS 位的表项由主站备份和恢复；不带
DS 位的表项仍会在写入后调用 `on_change`，应用同样可以自行保存。

写语义与持久化相互独立：

| `write_semantics` | 库的行为 |
| --- | --- |
| `IOLINK_OD_WRITE_STORE` | 写入后端，然后调用 `on_change` |
| `IOLINK_OD_WRITE_VOLATILE` | 同上；两者在库内的执行路径相同，仅表示表项语义 |
| `IOLINK_OD_WRITE_TRIGGER` | 不写后端，`on_change` 即动作；编译期禁止带 storage 位 |

DS 成员按 `index + subindex` 精确匹配（`iolink_od_is_ds()`）。多元素对象要让每个
元素都参与 DS 时，为每个子索引各声明一个带 DS 位的表项。

## 3. 三个可写标签 0x0018-0x001A

ApplicationTag、FunctionTag、LocationTag 由应用提供，标准表不包含这三个索引。
`iolink_od_find()` 对这三个索引先查应用表，因此应用表项拥有它们的数据和默认值。
未提供表项时，主站读写这三个索引会收到否定应答，DS 的 `Index_List` 中也不会出现
它们。`iolink_device_info_t::application_tag` 不参与 0x0018 的应答，索引的数据只来自
应用 OD 表项。

标签是普通的字节串 STORAGE 表项，声明 DS 位即可自动进入 DS 集合：

```c
#define APP_TAG_MAX 32U

static uint8_t g_application_tag[APP_TAG_MAX];
static uint8_t g_application_tag_len;
static uint16_t g_setpoint;

static iolink_result_t nvm_write_record(uint16_t index, const uint8_t* data, size_t len);

static iolink_result_t parameter_changed(iolink_app_t* self, const iolink_od_entry_t* entry,
                                         const uint8_t* data, size_t len)
{
    (void) self;
    if (nvm_write_record(entry->index, data, len) != 0) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
    }
    return iolink_result_ok(0U);
}

static const iolink_od_entry_t k_app_entries[] = {
    IOLINK_OD_BYTES_STORAGE_EX(0x0018U, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_STORE,
                               IOLINK_OD_STORAGE_DS, APP_TAG_MAX, "ApplicationTag",
                               g_application_tag, &g_application_tag_len, NULL,
                               parameter_changed),
    IOLINK_OD_SCALAR_STORAGE_EX(0x0050U, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                                IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_DS, 0U, 1000U, 500U,
                                "Setpoint", &g_setpoint, NULL, parameter_changed),
};
```

`block_len` 是该表项允许的最大字节数，也是编译期缓冲区大小检查的依据。标量写入时
若 `on_change` 返回错误，RAM 值会回滚；如果应用不能保证 NVM 写入成功，应让
`on_change` 返回错误，而不是静默吞掉失败。

## 4. DS 是 OD 的投影

DS 参数集完全由 OD 表派生：

- 标准表中 `storage` 含 `IOLINK_OD_STORAGE_DS` 的表项在前（当前标准表没有 DS
  成员）；
- 应用表中带 DS 位的表项按表序追加，包括三个标签；
- 顺序固定，不接受显式描述符列表。

`iolink_ds_param_count()` / `iolink_ds_param_entry()` 暴露该集合，DS 的
`Index_List` 也由它生成。

### 4.1 序列化与上传

上传内容按记录实时生成：

```
[Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)]
```

`Parameter_Checksum` 是对整段序列化字节计算的 Fletcher-32，
`Data_Storage_Size` 是序列化后的字节数。两者都从当前参数值实时计算，不占用
NVM。`iolink_ds_serialize()` 把内容流式写入调用方缓冲（整包读取时是 ISDU 应答
缓冲），设备不保存常驻 DS 镜像。序列化受调用方缓冲容量限制，放不下时返回负值；
下载暂存另受 `ds_stage` 的 `IOLINK_DS_IMAGE_MAX` 限制。

工作区 `iolink_workspace_t::ds_stage`（大小 `IOLINK_DS_IMAGE_MAX`）是校验和刷新、
镜像重算和下载暂存的落点；没有外部工作区时使用 DS 上下文内嵌的 scratch
（`IOLINK_WORKSPACE_EMBEDDED=1`）。DS 上下文不保存常驻镜像，只保留状态、缓冲指针
（或内嵌 scratch），尺寸见 [MEMORY_GUIDE.md](MEMORY_GUIDE.md)。

`iolink_ds_check()` 需要主站的校验和，由应用在主站比较后调用：主站没有备份
（校验和 0）时进入上传请求状态，不一致时进入下载请求状态，相等时保持空闲。

### 4.2 下载：预校验后再应用

块下载期间（DownloadStart 之后、DownloadEnd 之前），针对 DS 表项的 ISDU 写不会
立即改变应用值，而是作为记录追加到 `ds_stage`。暂存阶段只做长度和容量检查：记录
超过表项 `block_len` 时该次写以 `VAL_LEN_OVRRUN`（0x33）拒绝，暂存区放不下时以
内部错误拒绝；两种情况都把整份下载标记为无效。

1. 结构校验：`DownloadEnd`（或 `ParamDownloadStore`）先遍历整份镜像，截断或长度
   字段越界时以 `PAR_SETINVALID`（0x40）拒绝，不写入任何参数；此前被暂存拒绝过的
   下载以 `PAR_SETINCONSIST`（0x41）结束。`image_len` 在 measure-only 序列化
   （`out == NULL`）时也会被写入，那时它是上报的 `Data_Storage_Size` 而不是暂存
   长度，所以"长度非零"不蕴含存在暂存缓冲；没有缓冲时以 `PAR_SETINCONSIST`
   （0x41）结束，并且不改动上报长度。
2. 无副作用预校验：对每条已知记录调用 `iolink_od_check_value()`（线长、范围、
   `validate` 钩子）。任一记录被拒绝时以 `PAR_SETINCONSIST`（0x41）结束，仍不写入
   任何参数。
3. 应用：全部通过后按记录顺序调用 `iolink_od_write_value()`，逐条写后端并触发
   `on_change`。未知索引被跳过（替换设备可能没有备份中的全部参数）。
4. 收尾：从设备当前值重新序列化，刷新 `Data_Storage_Size` 与
   `Parameter_Checksum`。若结果与主站不同，下一次 `iolink_ds_check()` 会再次请求
   下载，直到两侧一致。

这不是跨参数事务：预校验覆盖值、长度和 `validate`，但第 3 阶段中某个 `on_change`
或后端写入失败时，前面已经应用的记录仍然生效，主站收到的是 `PAR_SETINCONSIST`
（0x41）。需要严格原子性的应用应在自己的保存层做双缓冲或提交标记。

### 4.3 出厂复位

`Restore factory settings`（0x82）和 `Back-to-box`（0x83）把 DS 集合中的应用表项
恢复为表项默认值（标量为 `scalar_default`，字节串为 `block_default`）。恢复使用
`iolink_od_restore_value()`，不执行 `validate`/`on_change`，也不写 NVM，库也不会
另行通知应用发生了复位。主站在复位后会重新下载它保存的参数集，届时应用通过
`on_change` 获得保存机会；要在主站重新下载前就把默认值落盘的应用需要自己的检测或
提交机制。两个命令之后库清除 `DS_UPLOAD_FLAG`。

## 5. DS_UPLOAD_FLAG 持久化钩子

`DS_UPLOAD_FLAG` 表示设备本地参数集发生变化、等待主站上传（`State_Property` 的
位 7）。该标志的 RAM 副本由库维护，掉电保存由应用完成：

```c
typedef void (*iolink_ds_flag_persist_fn)(iolink_app_t* self, bool flag);
```

在 `iolink_device_config_t::ds_flag_persist` 中绑定，`self` 固定为
`iolink_device_app_t::self`；传 NULL 表示该标志只存 RAM。直接驱动 DS 上下文时用
`iolink_ds_set_flag_persist(ctx, fn, self)` 绑定同一钩子。

| 事件 | 标志 | 钩子 |
| --- | --- | --- |
| `iolink_ds_request_upload()`（Application reset 0x81、SystemCommand ParamDownloadStore 0x05、应用本地改动） | 置位 | 总是以 `true` 调用 |
| UploadEnd（DS_Command 0x02）成功结束 | 清除 | 仅在标志确实变化时以 `false` 调用 |
| DownloadEnd（DS_Command 0x04 / SystemCommand ParamDownloadEnd 0x04）成功结束 | 清除 | 同上 |
| Restore factory settings（0x82）、Back-to-box（0x83） | 清除 | 同上 |

ParamDownloadStore 先完成 DownloadEnd（清除），再置位并请求上传，因此最终状态是
“待上传”。钩子返回 `void`，保存失败无法上报给主站；实现应保证幂等，并按应用的
可靠性要求决定是否在写入后回读校验。库触发的钩子调用发生在主上下文。

库从不读取 NVM。上电后如果应用保存的标志为 `true`，可调用
`iolink_ds_request_upload()` 把它恢复到 RAM 并登记待发送的 `DS_UPLOAD_REQ`；该调用
会再次以 `true` 触发钩子，因此钩子必须能承受重复写入。

```c
static void app_ds_flag_store(iolink_app_t* self, bool flag)
{
    app_t* app = (app_t*) self;
    (void) app_nvm_write(app, APP_NVM_DS_FLAG, &flag, sizeof(flag));
}

static const iolink_device_config_t g_cfg = {
    /* ... */
    .ds_flag_persist = app_ds_flag_store,
    .app = &g_trait, /* trait 的 self 指向 g_app */
};

/* iolink_device_init() 之后 */
bool pending = false;
if (app_nvm_read(&g_app, APP_NVM_DS_FLAG, &pending, sizeof(pending)) && pending) {
    iolink_ds_request_upload(iolink_device_get_ds_ctx(&g_ctx));
}
```

主站自己发起的 ISDU 写同样会调用 `on_change`；应用只在本地改动 DS 参数时调用
`iolink_ds_request_upload()`，不要在 `on_change` 里无条件请求上传，否则 DS 下载会
反复触发新一轮上传。

## 6. 初始化与配置校验

- `iolink_ds_init(ctx)` 只接收 DS 上下文；工作区、OD 来源和 flag 钩子由
  `iolink_device_init()` 按配置绑定。
- 启用 `IOLINK_FEATURE_VALIDATE` 时，`iolink_device_init()` 校验应用 OD 表：标准
  索引被占用、`index/subindex` 重复、后端指针缺失、TRIGGER 带 storage 位、STORAGE
  后端的可写字节串没有长度上限等问题会返回 `IOLINK_DEVICE_INIT_ERR_OD`。
- 关闭 `IOLINK_FEATURE_DATA_STORAGE` 时，DS 引擎、`ds_flag_persist` 字段和 0x0003
  服务都不存在；应用仍可把标签声明为普通 OD 表项。

```c
static iolink_workspace_t g_workspace;
static iolink_od_app_t g_app_od = {.entries = k_app_entries,
                                   .count = sizeof(k_app_entries) / sizeof(k_app_entries[0])};
```

`IOLINK_WORKSPACE_EMBEDDED=0` 的构建必须提供工作区，否则初始化返回
`IOLINK_DEVICE_INIT_ERR_WORKSPACE`；此时 DS 的下载暂存和校验和计算都依赖
`g_workspace.ds_stage`。
