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
#include "iolinki/config.h"
#include "iolinki/params.h"

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
 * @brief ISDU 服务上下文
 *
 * 持有非周期报文引擎的缓冲区与状态。
 */
typedef struct
{
    isdu_state_t state;                            /**< 当前状态机位置 */
    uint8_t buffer[IOLINK_ISDU_BUFFER_SIZE];       /**< 请求载荷缓冲区 */
    size_t buffer_idx;                             /**< 当前缓冲区中已采集的字节数 */
    iolink_isdu_header_t header;                   /**< 已解码的请求头 */
    uint8_t response_buf[IOLINK_ISDU_BUFFER_SIZE]; /**< 应答载荷缓冲区 */
    size_t response_idx;                           /**< 已从应答缓冲区发送的字节数 */
    size_t response_len;                           /**< 应答缓冲区中的总字节数 */

    /* 规范 ISDU 传输（C3，Table 52） */
    uint8_t req_buf[IOLINK_ISDU_BUFFER_SIZE + 6U]; /**< 原始 ISDU 请求八位组（含 CHKPDU） */
    /* 成帧应答，双缓冲：引擎构建到 resp_buf，而
     * 数据 ISR 读取最后发布的副本，因此交接无需复制，
     * 也无需临界区。 */
    uint8_t resp_store[2][IOLINK_ISDU_BUFFER_SIZE + 3U]; /**< 成帧应答缓冲区 */
    uint8_t* resp_buf;     /**< 正在构建的缓冲区（resp_store[resp_sel]） */
    uint8_t resp_sel;      /**< resp_buf 的索引 */
    uint8_t resp_pub;      /**< 最后一个完整（已发布）应答的索引 */
    size_t req_idx;        /**< req_buf 中已采集的八位组数 */
    size_t req_total;      /**< 由 Length/ExtLength 得出的预期 ISDU 总长度 */
    size_t resp_idx;       /**< 已从 resp_buf 发出的八位组数 */
    size_t resp_total;     /**< resp_buf 中的总八位组数 */
    uint8_t last_flowctrl; /**< 上一条 ISDU 报文的 FlowCTRL */
    bool flowctrl_seen;    /**< 自 START 以来已收到 FlowCTRL */
    bool repeat_pending;   /**< 上一个 FlowCTRL 为重复：忽略载荷 */
    uint8_t error_code;    /**< IO-Link ISDU 错误码（0x80XX） */

    /* 指向外部依赖的指针 */
    /* 用于设备专用索引 0x0040-0x00FE（Preferred Index）
     * 和 >= 0x0100（Extended Index）的可选应用服务。输出长度以容量
     * 传入；成功时返回零，或返回 ISDU 附加错误码。
     * 回调必须有界，且不得保留缓冲区指针。 */
    uint8_t (*vendor_service)(void* user, uint16_t index, uint8_t subindex, bool write,
                              const uint8_t* input, size_t length, uint8_t* output,
                              size_t* output_length);
    void* vendor_user;
    bool response_negative; /**< 由 isdu_respond_error 设置；用于区分 0x80 两八位组载荷。 */
    void* event_ctx;        /**< 诊断主机反向链接 */
    void* ds_ctx;           /**< 用于系统命令的 Data Storage 上下文 */
    void* dll_ctx;          /**< 用于统计访问的 DLL 上下文 */
    iolink_params_ctx_t* params_ctx; /**< 设备本地可写参数上下文 */
    void* direct_param_page2;        /**< 每设备的 Direct Parameter page 2 存储 */

    /* 系统命令标志 */
    bool reset_pending;     /**< 请求设备复位（0x80） */
    bool app_reset_pending; /**< 请求应用复位（0x81） */
    iolink_log_sink_t log;  /**< 日志汇点（默认静默；设备层绑定） */
} iolink_isdu_ctx_t;

/**
 * @brief 初始化 ISDU 引擎
 *
 * @param ctx 要初始化的 ISDU 上下文
 */
void iolink_isdu_init(iolink_isdu_ctx_t* ctx);

/**
 * @brief 处理 ISDU 引擎逻辑
 *
 * 请求被完整采集后执行服务分发。
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
 * @brief 送入一条在 ISDU 通道（C3）上收到的 On-request Data 报文。
 *
 * M 序列控制八位组的地址位携带 FlowCTRL
 * （Table 52）。START 复位请求缓冲区；随后是 COUNT 1,2,..,0,1,..；
 * 重复的 FlowCTRL 表示重复上一条报文（其载荷被忽略）；
 * 其他任何值都是 ISDUError（丢弃请求，返回 Idle）。
 * 当收到的八位组数等于 I-Service 八位组中声明的 Length
 * （或 ExtLength）时，请求完成；此时校验 CHKPDU。
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
 * @param ctx ISDU 上下文
 * @param flowctrl FlowCTRL 值（mc & 0x1F）
 * @param od_out [out] od_len 个八位组的缓冲区
 * @param od_len 要填充的 OD 八位组数量
 */
void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len);

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
 * @param busy [out] 应用应答仍在执行时为 true
 * @param dst [out] 接收成帧应答八位组的缓冲区
 * @param dst_size @p dst 的容量
 * @return uint8_t 复制的应答八位组数量（无/忙时为 0）
 */
uint8_t iolink_isdu_snapshot_response(const iolink_isdu_ctx_t* ctx, bool* busy, uint8_t* dst,
                                      size_t dst_size);

/**
 * @brief ISDU 传输是否空闲。
 *
 * 空闲表示没有请求正在被采集、执行或读出；设备
 * 层据此只在 ISDU 交互之间运行事件/DS 引擎。
 *
 * @param ctx ISDU 上下文（只读，可以为 NULL）
 * @return 引擎处于 ISDU_STATE_IDLE 时为 true
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
 * 构建当前的 Direct Parameter page 1 映像（Table B.1），并返回
 * 给定地址处的八位组。DLL 用它来应答启动时在 page
 * 通信通道上的 Type-0 读。
 *
 * @param ctx ISDU 上下文
 * @param addr Direct Parameter page 地址（0x00-0x0F）
 * @return uint8_t 八位组值；地址越界或上下文为 NULL 时返回 0
 */
uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr);

/** @} */ /* end of iolinki_isdu */

#endif  // IOLINK_ISDU_H
