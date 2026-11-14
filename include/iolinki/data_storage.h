/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DATA_STORAGE_H
#define IOLINK_DATA_STORAGE_H

#include "iolinki/config.h"
#include "iolinki/od.h"
#include "iolinki/result.h"
#include "iolinki/workspace.h"

#include "iolinki/protocol.h"
#include "iolinki/params.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file data_storage.h
 * @brief 用于参数备份与恢复的 IO-Link 数据存储（DS）
 */

/**
 * @defgroup iolinki_data_storage 数据存储（DS）
 * @brief 参数映像序列化、校验和比较以及上传/下载服务。
 * @{
 */

/*
 * 序列化的数据存储参数映像的最大大小（IOLINK_DS_IMAGE_MAX，默认值由
 * config.h 提供，此处不再重复定义）。
 *
 * 必须容纳每一条由 DS 支持的参数记录。每条记录的布局如下：
 * [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)]。
 */

/**
 * @brief 数据存储（DS）引擎状态
 *
 * 管理空闲、上传和下载状态之间的转换。
 */
typedef enum
{
    IOLINK_DS_STATE_IDLE = 0U,         /**< 无活动的 DS 操作 */
    IOLINK_DS_STATE_UPLOAD_REQ = 1U,   /**< 主站请求参数上传 */
    IOLINK_DS_STATE_UPLOADING = 2U,    /**< 参数上传进行中 */
    IOLINK_DS_STATE_DOWNLOAD_REQ = 3U, /**< 主站请求参数下载 */
    IOLINK_DS_STATE_DOWNLOADING = 4U,  /**< 参数下载进行中 */
} iolink_ds_state_t;

/**
 * @brief 数据存储引擎上下文
 *
 * 跟踪 DS 同步过程的状态与校验和。参数集合与值访问都来自对象
 * 字典：标准表 + 应用 OD 表中 storage 为 `IOLINK_OD_STORAGE_DS` 的项。
 */
typedef struct
{
    iolink_ds_state_t state;         /**< 当前 DS 状态机位置 */
    iolink_params_ctx_t* params_ctx; /**< 标准标签对象的值后端（可为 NULL） */
    const iolink_od_app_t* od_app;   /**< 应用 OD 表（可为 NULL，表示只有标准表） */
    uint32_t current_checksum;       /**< 最近一次计算的本地参数校验和 */
    uint32_t master_checksum;        /**< 主站最近校验过的校验和 */
    bool ds_upload_flag;             /**< DS_UPLOAD_REQ 挂起（State_Property 位 7） */
    bool upload_event_pending;       /**< DS_UPLOAD_REQ 通知推迟到 ISDU 空闲后发出 */
    /* 镜像不再常驻状态结构。序列化交给工作区中的暂存缓冲（上传可
     * 直接流式写入调用方缓冲），这里只保留指针与容量。 */
    uint8_t* image;        /**< 序列化/下载暂存缓冲（工作区或内嵌 fallback） */
    size_t image_size;     /**< @ref image 的容量 */
    size_t image_len;      /**< @ref image 中的有效字节数 */
    bool image_valid;      /**< image 是否保存了当前参数的完整快照 */
    bool download_invalid; /**< 被拒绝的写入会使当前下载失效 */
    iolink_log_sink_t log; /**< 日志汇点（默认静默；设备层绑定） */
#if IOLINK_WORKSPACE_EMBEDDED
    uint8_t scratch[IOLINK_DS_IMAGE_MAX]; /**< 未提供工作区时的默认暂存缓冲 */
#endif
} iolink_ds_ctx_t;

/**
 * @brief 初始化数据存储引擎
 *
 * @param ctx 要初始化的 DS 上下文
 */
#if IOLINK_FEATURE_DATA_STORAGE
void iolink_ds_init(iolink_ds_ctx_t* ctx);

/**
 * @brief 绑定外部工作区。
 *
 * 绑定后 DS 的序列化/下载暂存使用 @p ws 的 `ds_stage` 区域，不再使用
 * 上下文内嵌的 fallback 缓冲。必须在 `iolink_ds_init()` 之后调用。
 * 设备层由 `iolink_device_init()` 按 `config.workspace` 自动完成。
 *
 * @p ws 为 NULL 表示解绑：没有内嵌 fallback 的构建（`IOLINK_WORKSPACE_EMBEDDED=0`）
 * 会把镜像指针与容量显式清零并丢弃 `image_valid`，之后
 * `iolink_ds_get_image()`/`iolink_ds_build_image()` 不会再访问旧工作区。
 * 内嵌构建则回到上下文自带的 scratch 缓冲。
 *
 * @param ctx DS 上下文
 * @param ws  工作区（必须比 ctx 存活更久）；NULL 表示解绑或回到内嵌 fallback
 */
void iolink_ds_bind_workspace(iolink_ds_ctx_t* ctx, iolink_workspace_t* ws);

/**
 * @brief 流式序列化当前 DS 参数集。
 *
 * 按记录 `[Index(2)][Subindex(1)][Length(1)][Data]` 逐条写入 @p out，
 * 同时更新 @ref iolink_ds_ctx_t::current_checksum。不要求上下文持有完整
 * 镜像：@p out 可以是 ISDU 应答缓冲、调用方缓冲，或 NULL（只统计长度/
 * 校验和）。
 *
 * @param ctx DS 上下文
 * @param out 目标缓冲（NULL 表示只计算）
 * @param cap @p out 的容量
 * @return int 序列化字节数；参数集放不下或读取失败时返回负值。
 */
int iolink_ds_serialize(iolink_ds_ctx_t* ctx, uint8_t* out, size_t cap);

/**
 * @brief 重新计算当前参数集的长度与校验和（不保存镜像）。
 *
 * 上传前的快照语义由流式序列化路径承担；本函数只刷新
 * @ref iolink_ds_ctx_t::image_len 与
 * @ref iolink_ds_ctx_t::current_checksum，用于 Data_Storage_Size /
 * Parameter_Checksum 的读取。
 *
 * @param ctx DS 上下文
 */
void iolink_ds_refresh(iolink_ds_ctx_t* ctx);

/**
 * @brief 绑定 DS 的参数来源。
 *
 * DS 的参数集合与值访问都来自对象字典：
 *  - 标准表（三个标识标签）通过 @p params_ctx 取值；
 *  - 应用表 @p app 中 storage 为 `IOLINK_OD_STORAGE_DS` 的项自动进入
 *    集合，值由表项后端提供。
 *
 * 集合顺序固定为"标准表在前，应用表按表序"，不接受显式描述符列表。
 *
 * @param ctx        DS 上下文
 * @param app        应用 OD 表（可为 NULL，表示只有标准表）
 * @param params_ctx 标准标签对象的值后端（可为 NULL）
 */
void iolink_ds_bind_sources(iolink_ds_ctx_t* ctx, const iolink_od_app_t* app,
                            iolink_params_ctx_t* params_ctx);

/**
 * @brief 计算参数块的标准 32 位校验和（Fletcher-32）
 *
 * 用于 DS 比较操作，以检测主站与设备之间的一致性。
 * Table B.10 要求 Parameter_Checksum 为 UIntegerT32；10.4.8 允许
 * 使用 CRC 或校验和签名等修订标识。
 *
 * @param data 指向参数数据结构的指针
 * @param len 数据的长度（字节）
 * @return uint32_t Fletcher-32 校验和 (0x00000000-0xFFFFFFFE)
 */
uint32_t iolink_ds_calc_checksum(const uint8_t* data, size_t len);

/**
 * @brief 处理数据存储引擎逻辑
 *
 * 处理状态转换与后台同步任务。
 * 在 ISDU 传输空闲时，iolink_device_process() 每个周期都会调用它；
 * 仅在直接驱动 DS 上下文时才需要自行调用。
 *
 * @param ctx 要处理的 DS 上下文
 */
void iolink_ds_process(iolink_ds_ctx_t* ctx);

/**
 * @brief 触发与主站的 DS 一致性检查
 *
 * 通常由 ISDU 引擎在主站请求比较时触发。
 *
 * @param ctx DS 上下文
 * @param master_checksum IO-Link 主站提供的 32 位校验和
 */
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum);

/**
 * @brief 中止任何进行中的 DS 上传/下载并返回空闲状态。
 *
 * @param ctx DS 上下文
 * @return int 成功时返回 0
 */
int iolink_ds_abort(iolink_ds_ctx_t* ctx);

/**
 * @brief 处理 Table B.9 SystemCommand 的 Param* 编码。
 *
 * 0x01 UploadStart … 0x06 ParamBreak；驱动与
 * @ref iolink_ds_handle_ds_command 相同的状态机。
 *
 * @param ctx DS 上下文
 * @param cmd 系统命令（0x01-0x06）
 * @param access_locks 当前访问锁状态（索引 0x000C）
 * @return int 0：成功，-1：忙，-2：访问被拒绝，-3：未知
 */
int iolink_ds_handle_param_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);

/**
 * @brief 处理 DataStorageIndex DS_Command（Table B.10 subindex 01）。
 *
 * DS_Command 的编码与 SystemCommand 的 Param* 集合不同：
 * 0x01 UploadStart, 0x02 UploadEnd, 0x03 DownloadStart, 0x04 DownloadEnd,
 * 0x05 DS_Break（0x06-0xFF 保留）。它驱动同一状态机，并在
 * 上传/下载序列结束时清除 DS_UPLOAD_FLAG。
 *
 * @param ctx DS 上下文
 * @param command DS_Command 八位组
 * @param access_locks 当前访问锁状态（索引 0x000C）
 * @return int 0：成功，-1：忙，-2：访问被拒绝，-3：未知/保留
 */
int iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t command, uint16_t access_locks);

/**
 * @brief 当前是否有块参数下载正在缓冲写入。
 *
 * @param ctx DS 上下文
 * @return true 表示处于下载缓冲状态。
 */
bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx);

/**
 * @brief 将参数写入暂存到下载缓冲区（10.3.2）。
 *
 * 在块下载期间（ParamDownloadStart 之后、ParamDownloadEnd 之前），
 * 参数写入会先缓冲到映像中，而不是立即
 * 生效；ParamDownloadEnd 先对整份镜像做校验，全部通过后再逐条应用。
 *
 * @param ctx      DS 上下文
 * @param index    参数索引
 * @param subindex 参数子索引
 * @param data     参数值字节
 * @param len      @p data 的长度（字节）
 * @return 结果模型：成功 `{0, 0}`；未处于下载状态
 *         ::IOLINK_RESULT_NOT_HANDLED；值长度超长以正 0x33 返回。
 */
iolink_result_t iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                      const uint8_t* data, size_t len);

/**
 * @brief 将设备中由 DS 支持的参数序列化到映像缓冲区。
 *
 * 从对象字典读取 DS 集合中的每个参数，并将其打包
 * 到 @ref iolink_ds_ctx_t::image 中。同时更新 @ref iolink_ds_ctx_t::current_checksum。
 *
 * @param ctx DS 上下文
 * @return int 构建的映像字节数 (>= 0)，出错时返回负值。
 */
int iolink_ds_build_image(iolink_ds_ctx_t* ctx);

/**
 * @brief 应用从主站接收的参数映像（恢复）。
 *
 * 解析序列化的记录，先对全部"已知参数"调用表项的 validate 钩子
 * （无副作用），全部通过后再逐条通过对象字典写回。
 *
 * 这不是跨参数的事务：预校验覆盖值本身与长度，预校验失败时不会写入
 * 任何参数。应用通过各表项的 `on_change` 收到落盘通知，库不写 NVM。
 *
 * @param ctx DS 上下文
 * @param data 序列化的映像字节
 * @param len  映像字节数
 * @return int 成功时返回 0，映像格式错误返回 -1，已知参数被拒绝返回 -2。
 */
int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);

/**
 * @brief 获取当前序列化的参数映像（按需构建）。
 *
 * @param ctx DS 上下文
 * @param out_len [out] 接收映像长度（字节）
 * @return const uint8_t* 指向映像缓冲区的指针，出错时返回 NULL。
 */
const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len);

/**
 * @brief 把 PERSIST 表项恢复为表项默认值（仅 RAM）。
 *
 * 标签（0x0018-0x001A）由参数上下文复位，本函数跳过它们。持久化由应用
 * 在 reset 后自行决定；库不写 NVM。
 *
 * @param ctx DS 上下文
 * @return int 0 成功。
 */
int iolink_ds_factory_reset(iolink_ds_ctx_t* ctx);

/**
 * @brief 读取 State_Property 八位组（Table B.10 subindex 02）。
 *
 * 位 1-2 编码 DS 状态（Inactive/Upload/Download）；位 7 是
 * DS_UPLOAD_FLAG（表示存在待处理的数据存储上传请求）。
 *
 * @param ctx DS 上下文
 * @return uint8_t State_Property 值。
 */
uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx);

/**
 * @brief 读取 Data_Storage_Size 值（Table B.10 subindex 03）。
 *
 * @param ctx DS 上下文
 * @return uint32_t 存储该参数集所需的八位组数。
 */
uint32_t iolink_ds_size(const iolink_ds_ctx_t* ctx);

/**
 * @brief 读取 Parameter_Checksum 值（Table B.10 subindex 04）。
 *
 * @param ctx DS 上下文
 * @return uint32_t 当前参数集校验和（Fletcher-32）。
 */
uint32_t iolink_ds_checksum(const iolink_ds_ctx_t* ctx);

/**
 * @brief DS 集合中的表项数量。
 *
 * 集合由对象字典派生：标准表中 storage 为 DS 的项在前，应用表中
 * storage 为 DS 的项按表序追加。
 *
 * @param ctx DS 上下文
 * @return size_t 表项数量。
 */
size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx);

/**
 * @brief 取 DS 集合中第 @p i 个表项。
 *
 * @param ctx DS 上下文
 * @param i   集合索引
 * @return const iolink_od_entry_t* 表项指针；越界或未绑定时返回 NULL。
 */
const iolink_od_entry_t* iolink_ds_param_entry(const iolink_ds_ctx_t* ctx, size_t i);

/**
 * @brief 发起数据存储上传请求（置位 DS_UPLOAD_FLAG，10.4.2）。
 *
 * 当参数集在本地发生变化（teach-in、Application Reset、
 * ParamDownloadStore）时调用。主站会观察 State_Property 的第 7 位和/或
 * DS_UPLOAD_REQ 事件；该标志在上传/下载结束时清除。
 *
 * @param ctx DS 上下文
 */
void iolink_ds_request_upload(iolink_ds_ctx_t* ctx);

/**
 * @brief 清除待处理的数据存储上传请求（DS_UPLOAD_FLAG = false）。
 *
 * @param ctx DS 上下文
 */
void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx);
#endif /* IOLINK_FEATURE_DATA_STORAGE */

/** @} */ /* end of iolinki_data_storage */

#endif  // IOLINK_DATA_STORAGE_H
