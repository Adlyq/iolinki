/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_ISDU_H
#define IOLINK_ISDU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iolinki/capability.h"
#include "iolinki/config.h"
#include "iolinki/od.h"
#include "iolinki/device_info.h"
#include "iolinki/port.h"
#include "iolinki/workspace.h"
#include "iolinki/types.h"

IOLINK_ASSUME_NONNULL_BEGIN

/* 反向链接只在这里当不透明句柄使用；定义在各自的模块头里。 */
struct iolink_ds_ctx;
struct iolink_events_ctx;
struct iolink_dll_main;

/**
 * @file isdu.h
 * @brief IO-Link 索引服务数据单元（ISDU）非周期报文
 */

/**
 * @defgroup iolinki_isdu ISDU 服务（非周期参数）
 * @brief 索引式非周期读/写服务引擎以及请求/应答处理。
 * @{
 */

/**
 * @brief ISDU 服务类型
 */
typedef enum
{
    IOLINK_ISDU_SERVICE_TYPE_READ = 0U, /**< 从索引/子索引读取 */
    IOLINK_ISDU_SERVICE_TYPE_WRITE = 1U /**< 写入索引/子索引 */
} iolink_isdu_service_type_t;

/**
 * @brief ISDU 服务头
 *
 * 定义非周期请求的寻址与元数据。
 */
typedef struct
{
    uint8_t type;     /**< 服务类型（Read/Write） */
    uint8_t length;   /**< 载荷长度（若 <= 232） */
    uint16_t index;   /**< 参数索引（0-65535） */
    uint8_t subindex; /**< 参数子索引（0-255） */
} iolink_isdu_header_t;

/**
 * @brief ISDU 引擎内部状态
 */
typedef enum
{
    ISDU_STATE_IDLE = 0U,            /**< 等待新的请求 */
    ISDU_STATE_HEADER_INITIAL = 1U,  /**< 收集请求头/分段 */
    ISDU_STATE_SERVICE_EXECUTE = 2U, /**< 分发到应用层 */
    ISDU_STATE_RESPONSE_READY = 3U   /**< 应答缓冲区已填充，等待取走 */
} isdu_state_t;

/**
 * @brief 写请求的入口暂存相位（数据 ISR 独占）。
 *
 * 写方向的 FlowCTRL 序与字节累计归数据 ISR：START 建相位、COUNT 推进、
 * 收齐后由 ISR 把 `{len, sel, gen}` 发布给主泵。主泵不再解析 ISR 正在写
 * 的任何字段，也不再改写本相位——因此这里没有"等待主泵复位"的状态。
 *
 * @ref IOLINK_ISDU_STAGE_COMPLETE 是**闩锁**：请求已发布、槽内容稳定，
 * 直到下一次 START（重新进入 RECEPTION）或 ABORT（回到 IDLE）才改变。
 * 它不表示"主泵尚未处理"，后者由发布字与 ack 序号的比较表达。
 */
typedef enum
{
    IOLINK_ISDU_STAGE_IDLE = 0U,      /**< 没有请求在累计；槽可复用 */
    IOLINK_ISDU_STAGE_RECEPTION = 1U, /**< ISR 正在累计写段 */
    IOLINK_ISDU_STAGE_COMPLETE = 2U   /**< 请求已收齐并发布，槽内容稳定 */
} iolink_isdu_stage_state_t;

/**
 * @name 写请求发布字（数据 ISR -> 主泵）
 *
 * ISR 在写 START 与请求收齐时各发布一次：把"哪个槽、多长、哪一代"打包成
 * 一个自然原子的字，主泵一次加载就得到一致的元组，不再与 ISR 共用累计
 * 字段。槽内容先写完，发布字后写（`IOLINK_FENCE()` + 单次原子存储）。
 *
 * 位布局：`len[7:0] | sel[8] | gen[23:9] | receiving[24]`。每次发布都把
 * `gen` 前进一格，因此"`gen != req_ack_gen`"精确表示"写入口有一代主泵
 * 尚未处理"。写正在累计时 `receiving` 置位、`len` 为 0：主泵既不解析也
 * 不 ack 这一代，读视图因此在整个写期间保持失效；收齐（或中止）后才发布
 * 可 ack 的一代，`len` 是请求字节数（0 = 中止/撤回），主泵只在 `len > 0`
 * 时解析槽内容。`gen` 是 16 位自由计数，连续错过 32768 次发布才会漏掉
 * 一格，后果只是少处理一条请求，绝不解析撕裂的数据。
 * @{
 */
#define IOLINK_ISDU_REQ_PUB_LEN(word) ((uint8_t) ((word) & 0xFFU))
#define IOLINK_ISDU_REQ_PUB_SEL(word) ((uint8_t) (((word) >> 8) & 0x01U))
#define IOLINK_ISDU_REQ_PUB_GEN(word) ((uint16_t) (((word) >> 9) & 0xFFFFU))
#define IOLINK_ISDU_REQ_PUB_RECEIVING(word) ((uint8_t) (((word) >> 25) & 0x01U))
#define IOLINK_ISDU_REQ_PUB_WORD(len, sel, gen, receiving)                                      \
    ((IOLINK_ATOMIC_UINT_T) (uint8_t) (len) | ((IOLINK_ATOMIC_UINT_T) ((sel) ? 1U : 0U) << 8) | \
     ((IOLINK_ATOMIC_UINT_T) (uint16_t) (gen) << 9) |                                           \
     ((IOLINK_ATOMIC_UINT_T) ((receiving) ? 1U : 0U) << 25))
/** @} */

/**
 * @brief ISDU 服务上下文
 *
 * 持有非周期报文引擎的缓冲区与状态。
 */
typedef struct
{
    isdu_state_t state; /**< 当前状态机位置 */

    volatile uint8_t stage_state; /**< 写入口暂存相位（ISR 独占；主泵从不读写） */

    /* 传输缓冲位于 `workspace`（或上下文内嵌 fallback）中。
     * 容量随缓冲一起记录，避免运行期再出现第二份尺寸常量。 */
    uint8_t* IOLINK_NULLABLE buffer; /**< 请求载荷缓冲区（解绑工作区时为 NULL） */
    size_t buffer_size;              /**< @ref buffer 的容量 */
    size_t buffer_idx;               /**< 当前缓冲区中已采集的字节数 */
    iolink_isdu_header_t header;     /**< 已解码的请求头 */

    uint8_t* IOLINK_NULLABLE response_buf; /**< 应答载荷缓冲区（解绑时为 NULL） */
    size_t response_size;                  /**< @ref response_buf 的容量 */
    size_t response_idx;                   /**< 已从应答缓冲区发送的字节数 */
    size_t response_len;                   /**< 应答缓冲区中的总字节数 */

    /* 规范 ISDU 传输（C3，Table 52）。
     *
     * 写入口是双槽 + 发布字：`req_base` 指向两槽请求缓冲的起点，槽 k 的
     * 地址 = `req_base + k * req_size`；`req_buf` 指向**当前累计槽**
     * （ISR 单写）。ISR 在 START 时选一个主泵既没有在解析、也不是
     * "已发布未 ack"的槽；收齐后发布 `req_pub`，主泵解析发布字指向的槽
     * （`req_busy_sel` 标记期间占用）。 */
    uint8_t* IOLINK_NULLABLE req_base; /**< 两槽请求缓冲的起点（绑定后不变） */
    uint8_t* IOLINK_NULLABLE req_buf;  /**< 请求八位组的当前累计槽（含 CHKPDU） */
    size_t req_size;                   /**< 单槽容量；也是两槽之间的步长 */
    /* 成帧应答，单缓冲 + 租约：引擎在缓冲空闲时构建，
     * 数据 ISR 在租约期内读取。`resp_store[0/1]` 是同一个缓冲的两个
     * 视图槽，只为保持 ISR 发布字中 sel 位的既有编码。 */
    uint8_t* IOLINK_NULLABLE resp_frame;    /**< 唯一成帧应答缓冲区 */
    uint8_t* IOLINK_NULLABLE resp_store[2]; /**< 两个视图槽（同一缓冲） */
    uint8_t* IOLINK_NULLABLE resp_buf;      /**< 正在构建的缓冲区 */
    uint8_t resp_sel;                       /**< resp_buf 的视图索引 */
    uint8_t resp_pub;                       /**< 最后一个完整（已发布）应答的视图索引 */
    size_t req_idx;                         /**< 当前累计槽中已采集的八位组数（ISR 单写） */
    size_t req_total; /**< 由 Length/ExtLength 得出的预期 ISDU 总长度（ISR 单写） */

    volatile IOLINK_ATOMIC_UINT_T req_pub; /**< 写请求发布字 `{len, sel, gen, receiving}`
                                                （ISR 单写） */
    volatile uint16_t req_ack_gen;         /**< 主泵已处理到的发布代号（主泵单写） */
    volatile uint8_t req_busy_sel;         /**< 主泵正在解析的槽；0xFF = 无（主泵单写） */
    uint8_t req_slot;                      /**< ISR 当前累计槽（0/1） */
    uint32_t req_overruns; /**< 已发布未接管的请求被新 START 覆盖的次数（ISR 单写） */

    size_t resp_idx;       /**< 已从 resp_buf 发出的八位组数 */
    size_t resp_total;     /**< resp_buf 中的总八位组数 */
    uint8_t last_flowctrl; /**< 上一条 ISDU 报文的 FlowCTRL（ISR 单写） */
    bool flowctrl_seen;    /**< 自 START 以来已收到 FlowCTRL（ISR 单写） */
    uint8_t error_code;    /**< IO-Link ISDU 错误码（0x80XX） */

    /* 指向外部依赖的指针 */
    bool response_negative; /**< 由 isdu_respond_error 设置；用于区分 0x80 两八位组载荷。 */
    struct iolink_events_ctx* IOLINK_NULLABLE event_ctx; /**< 诊断主机反向链接（可为 NULL） */
    struct iolink_ds_ctx* IOLINK_NULLABLE
        ds_ctx; /**< 系统命令用的 Data Storage 上下文（可为 NULL） */
    struct iolink_dll_main* IOLINK_NULLABLE dll_ctx; /**< 统计/页映像用的 DLL 上下文（可为 NULL） */
    iolink_device_info_ctx_t* IOLINK_NULLABLE device_info; /**< 设备标识上下文（可为 NULL） */
    const iolink_od_app_t* IOLINK_NULLABLE od; /**< 应用对象字典（表 + get/set 后端；可为 NULL） */
    iolink_app_t* IOLINK_NULLABLE od_self;     /**< 传给 OD 钩子/后端的应用上下文（可为 NULL） */
    uint8_t* IOLINK_NULLABLE
        direct_param_page2; /**< 每设备的 Direct Parameter page 2 存储（16 字节；可为 NULL） */

    /* Direct Parameter page 1（0x00-0x0F）RAM 映像。
     * 并发契约：主泵是唯一写者，数据 ISR 只读。
     * `dp_page1_seq` 是 seqlock 发布字：奇数表示主泵正在覆盖映像或映像已失效，
     * 偶数表示内容稳定。ISR 只做「读序号 → 读字节 → 再读序号」的校验，
     * 绝不重建整页；整页构建只发生在主泵上下文。 */
    uint8_t dp_page1[16];          /**< DPP1 映像（主泵写、ISR 读） */
    volatile uint8_t dp_page1_seq; /**< 奇数 = 更新中/失效；偶数 = 稳定 */
    const struct iolink_dll_main* IOLINK_NULLABLE
        dp_page1_src; /**< 上次构建映像时绑定的 DLL 上下文 */
    const iolink_device_info_ctx_t* IOLINK_NULLABLE
        dp_page1_info; /**< 上次构建映像时的设备信息上下文 */

    iolink_workspace_t* IOLINK_NULLABLE workspace; /**< 绑定的工作区；NULL = 使用内嵌 fallback */
    const iolink_caps_t* IOLINK_NULLABLE caps;     /**< 实例能力位；NULL = 全开（独立使用引擎时） */

    /* 系统命令标志 */
    bool reset_pending;                           /**< 请求设备复位（0x80） */
    bool app_reset_pending;                       /**< 请求应用复位（0x81） */
    const iolink_log_sink_t* IOLINK_NULLABLE log; /**< 日志汇点指针（NULL = 静默；设备层指向共享
                                      sink， 独立使用须自行指向调用方的 sink） */

#if IOLINK_WORKSPACE_EMBEDDED
    iolink_isdu_scratch_t scratch; /**< 未提供工作区时的默认 scratch */
#endif
} iolink_isdu_ctx_t;

/**
 * @brief 初始化 ISDU 引擎
 *
 * @param ctx 要初始化的 ISDU 上下文
 */
void iolink_isdu_init(iolink_isdu_ctx_t* ctx);

/**
 * @brief 绑定外部工作区。
 *
 * 绑定后 ISDU 传输缓冲指向 @p ws 的 `isdu` 区域，不再使用上下文内嵌的
 * fallback 存储。必须在 `iolink_isdu_init()` 之后调用；绑定不会清空
 * 传输状态。设备层由 `iolink_device_init()` 按 `config.workspace` 自动完成。
 *
 * 传输缓冲指针由数据 ISR 读取，因此绑定必须在数据泵静止时进行。从数据
 * ISR 内调用会被拒绝：指针与传输状态都不改变。
 *
 * @p ws 为 NULL 表示解绑：没有内嵌 fallback 的构建
 * （`IOLINK_WORKSPACE_EMBEDDED=0`）会显式清零全部缓冲指针与容量，
 * 之后 `iolink_isdu_process()` 不处理请求、`iolink_isdu_snapshot_response()`
 * 返回 0；实现不得保留旧工作区的指针。内嵌构建则回到上下文自带的
 * scratch 缓冲。
 *
 * @param ctx ISDU 上下文
 * @param ws  工作区（必须比 ctx 存活更久）；NULL 表示解绑或回到内嵌 fallback
 */
void iolink_isdu_bind_workspace(iolink_isdu_ctx_t* ctx, iolink_workspace_t* IOLINK_NULLABLE ws);

/**
 * @brief 绑定应用对象字典。
 *
 * 分发顺序固定为：规范索引走标准表，设备专用索引先查应用表、再回落到
 * 标准表内建项。应用表不能遮蔽规范固定索引。
 *
 * @param ctx  ISDU 上下文
 * @param app  应用表（必须在使用期间保持有效；NULL 解除绑定）
 * @param self 传给 OD 钩子与 HANDLER 后端的应用上下文（可为 NULL）
 */
void iolink_isdu_bind_od(iolink_isdu_ctx_t* ctx, const iolink_od_app_t* IOLINK_NULLABLE app,
                         iolink_app_t* IOLINK_NULLABLE self);

/**
 * @brief 绑定实例能力位。
 *
 * 设备层在 `iolink_caps_resolve()` 之后调用；独立使用 ISDU 引擎
 * （单元测试）时可以跳过，此时按"全部能力可用"处理。
 *
 * @param ctx  ISDU 上下文
 * @param caps 指向实例能力位的指针（必须比 ctx 存活更久）；NULL 解绑
 */
void iolink_isdu_bind_caps(iolink_isdu_ctx_t* ctx, const iolink_caps_t* IOLINK_NULLABLE caps);

/**
 * @brief 处理 ISDU 引擎逻辑（主上下文）。
 *
 * 若数据 ISR 已发布一条可认领的写请求（`req_pub` 的代号与 `req_ack_gen`
 * 不同、`len > 0`、且不处于"正在累计"），本函数认领该槽、校验 CHKPDU、
 * 解析请求头并复制载荷，然后执行服务分发并生成应答。整个过程**不加
 * 临界区**：ISR 不会写主泵正在解析的槽（`req_busy_sel`），主泵也不会写
 * ISR 的累计相位。慢速应用回调因而可以在这条路径上运行。
 *
 * 每次调用最多认领一条已发布请求，从而使 `req_busy_sel` 认领期间 ISR
 * 的选槽判定始终成立。
 *
 * @param ctx 要处理的 ISDU 上下文
 */
void iolink_isdu_process(iolink_isdu_ctx_t* ctx);

/**
 * @brief 执行通过 Direct Parameter page 1（0x0F）提供的 SystemCommand。
 *
 * 7.3.5.3：DPP1 的 SystemCommand 是供不支持 ISDU 的设备选用的
 * 替代方式，不提供肯定或否定应答。命令会被执行，
 * 任何已暂存的 ISDU 应答都会被丢弃。
 *
 * @param ctx ISDU 上下文
 * @param cmd SystemCommand 八位组（Table B.9）
 */
void iolink_isdu_execute_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd);

/**
 * @brief 数据 ISR 的 ISDU 写入口：累计请求字节并发布收齐的请求。
 *
 * 本函数只做「传输」：按 FlowCTRL 位置去重、累计原始请求八位组、
 * 判定声明长度是否收齐。它不解析头、不校验 CHKPDU、不执行服务——
 * 收齐后把 `{len, sel, gen}` 发布到 `req_pub`，由主泵在
 * @ref iolink_isdu_process 中锁外完成。
 *
 * 去重是**按位置**的（借鉴 ST ministack）：仅当 FlowCTRL 等于
 * `(last + 1) & 0x0F` 时推进写位置；重复段与过早的乱序段被忽略
 * （载荷丢弃，已累计字节不受影响），因此主站重发风暴不会消耗新缓冲，
 * 也不会破坏已收齐的请求。
 *
 * 选槽规则（ISR 单写）：START 先避开主泵正在解析的槽（`req_busy_sel`），
 * 再避开"已发布未 ack"的槽，然后发布 `len=0, receiving=1` 的一代——读
 * 视图因此失效，主泵也不会读到正在累计的半条请求。于是两条请求可以同时
 * 在途（主泵执行一条、ISR 累计下一条）；只有"已发布但主泵尚未接管"的
 * 请求会被新 START 取代：它不会执行，取代计入 `req_overruns`（经
 * `iolink_dll_get_stats()` 的 `isr_od_overruns` 可观测）——丢弃不是静默的。
 * ABORT 拒绝请求数据、释放累计相位、撤回尚未接管的请求，并让主泵
 * 复位读传输层（Table 52：传输返回 Idle）。IDLE/IDLE2 与保留值被忽略。
 *
 * @note 只在数据 ISR 上下文调用。
 *
 * @param ctx ISDU 上下文
 * @param flowctrl FlowCTRL 值（mc & 0x1F）
 * @param od 本条报文传入的 OD 八位组
 * @param od_len OD 八位组数量
 */
void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od,
                          uint8_t od_len);

/**
 * @brief 在 ISDU 通道（C3）上生成设备应答的 OD 八位组。
 *
 * 在应用应答尚未就绪时，对一次 START 读，设备
 * 用单个 Busy 八位组（0x01，Table A.14）应答。应答就绪后，
 * 成帧的 ISDU 八位组会在携带 COUNT 的多次读中相继送出。IDLE
 * 返回 Idle；ABORT 丢弃该服务。
 *
 * IDLE/IDLE2/ABORT 与读完成只复位读传输层：数据 ISR 已累计的写请求
 * （含刚收到的写 START）保持原样。
 *
 * @param ctx ISDU 上下文
 * @param flowctrl FlowCTRL 值（mc & 0x1F）
 * @param od_out [out] od_len 个八位组的缓冲区；仅当 @p od_len 为 0
 *               时可以为 NULL（用于只推进状态、不取字节的调用）
 * @param od_len 要填充的 OD 八位组数量
 */
void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* IOLINK_NULLABLE od_out,
                         uint8_t od_len);

/**
 * @brief 为双泵 ISR 传输快照当前读应答。
 *
 * ISR 从预先暂存的数据应答 ISDU 读，因此读操作无需往返
 * 主循环（t_A 必须保持在 10 个位时间以内）。主泵在
 * 所请求的服务改变状态时调用本函数：
 *
 * - 服务执行中 -> *busy = true，返回 0（ISR 应答 Busy 0x01）；
 * - 应答就绪   -> *busy = false，将 resp_buf[0..resp_total) 复制到
 *                 @p dst（最多 @p dst_size）并返回其长度；
 * - 空闲       -> *busy = false，返回 0（ISR 应答 NoService）。
 *
 * 只读且 ISR 安全（调用方必须使复制相对于
 * 数据中断是原子的）。
 *
 * @param ctx ISDU 上下文
 * @param busy [out] 应用应答仍在执行时为 true；可传 NULL 表示不需要该值
 * @param dst [out] 接收成帧应答八位组的缓冲区
 * @param dst_size @p dst 的容量
 * @return uint8_t 复制的应答八位组数量（无/忙时为 0）
 */
uint8_t iolink_isdu_snapshot_response(const iolink_isdu_ctx_t* ctx, bool* IOLINK_NULLABLE busy,
                                      uint8_t* dst, size_t dst_size);

/**
 * @brief ISDU 传输是否空闲。
 *
 * 空闲表示没有请求正在被采集、执行或读出：读状态机为 IDLE、数据 ISR
 * 没有正在累计的写请求，也没有"已发布但主泵尚未处理"的写请求。设备
 * 层据此只在 ISDU 交互之间运行事件/DS 引擎。
 *
 * @param ctx ISDU 上下文（只读）
 * @return 引擎处于 ISDU_STATE_IDLE 且写入口无在途请求时为 true
 */
bool iolink_isdu_is_idle(const iolink_isdu_ctx_t* ctx);

/**
 * @brief 取应用层 ISDU 应答载荷的下一个八位组。
 *
 * 线路成帧（I-Service/Length/CHKPDU）由传输层施加；此
 * 访问器只暴露载荷，以便调用方和测试检查
 * 处理函数的结果。否定应答产生 {0x80, AdditionalCode}。
 *
 * @param ctx ISDU 上下文
 * @param byte [out] 用于存放应答八位组的指针
 * @return int 产生了一个八位组时为 1，应答耗尽时为 0
 */
int iolink_isdu_get_response_byte(iolink_isdu_ctx_t* ctx, uint8_t* byte);

/**
 * @brief 读取单个 Direct Parameter page 1 八位组。
 *
 * 从主泵维护的 16 字节 RAM 映像（@ref iolink_isdu_ctx_t::dp_page1 ）取
 * 给定地址处的八位组（Table B.1），不再重建整页。DLL 用它来应答
 * 启动时在 page 通信通道上的 Type-0 读，数据 ISR 会对每个字节调用一次。
 *
 * 该函数可在 ISR 中调用：只有稳定副本会被返回。主泵正在更新映像时
 * 有限次重试读取序号，仍不一致则返回 0；ISR 侧不会重建整页。主泵侧
 * 在映像失效（或 MasterCycleTime 已变化）时先重建再读取。
 *
 * @note 一致性粒度是**单个八位组**。多字节页读请使用
 * @ref iolink_isdu_dp_page1_read_range （单次序号窗口内复制整段）。
 * DPP1 输入中只有 0x01（MasterCycleTime，主站写入）会在运行期变化；
 * 0x05/0x06 由初始化配置固定，0x02 只在初始化时重建。
 *
 * @param ctx ISDU 上下文
 * @param addr Direct Parameter page 地址（0x00-0x0F）
 * @return uint8_t 八位组值；地址越界、上下文为 NULL 或映像不可用时返回 0
 */
uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr);

/**
 * @brief 读取 Direct Parameter page 1 的一段字节（单次 seqlock 窗口）。
 *
 * 与逐字节访问器不同，整段在**同一个序号窗口**内复制；主泵在复制期间
 * 刷新映像时重试，因此返回的字节来自同一代，不会混入新旧两代。数据 ISR
 * 用它一次填充页读应答的全部载荷；主泵侧调用时同样先按需重建。
 *
 * 越界地址（addr + i >= 16）填 0。序号持续为奇数（主泵正在重建）或三次
 * 重试都冲突时，@p out 被清零并返回 false。
 *
 * @param ctx ISDU 上下文
 * @param addr 起始地址
 * @param out  输出缓冲（至少 @p len 字节）
 * @param len  字节数（0 时不做任何事）
 * @return true = 读到稳定的一代；false = 映像不可用（@p out 已清零）
 */
bool iolink_isdu_dp_page1_read_range(iolink_isdu_ctx_t* ctx, uint8_t addr, uint8_t* out,
                                     uint8_t len);

/** @} */ /* end of iolinki_isdu */

IOLINK_ASSUME_NONNULL_END

#endif  // IOLINK_ISDU_H
