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
#include "iolinki/storage.h"

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
 * @brief DS 引擎状态
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
 * @brief 一个由数据存储支持的参数（索引 + 子索引）。
 *
 * DS 参数集由这些描述符组成的数组描述。默认集合是
 * 三个可写的标识标签；应用可以替换或扩展它（例如加入厂商
 * 参数 0x0040+ / 0x0100+）。
 */
typedef struct
{
    uint16_t index;   /**< 参数索引。 */
    uint8_t subindex; /**< 参数子索引。 */
    uint8_t max_len;  /**< 值长度上限（0 = 无上限）。 */
} iolink_ds_param_desc_t;

/**
 * @brief 将单个 DS 参数读入缓冲区。
 *
 * @param user     参数访问的不透明用户指针。
 * @param index    参数索引。
 * @param subindex 参数子索引。
 * @param out      接收参数值的缓冲区。
 * @param cap      @p out 的容量（字节）。
 * @return int 读取的字节数 (>= 0)；参数不可用或被拒绝时
 *         返回负值。
 */
typedef int (*iolink_ds_param_read_fn)(void* user, uint16_t index, uint8_t subindex, uint8_t* out,
                                       size_t cap);

/**
 * @brief 写入单个 DS 参数。
 *
 * @param user     参数访问的不透明用户指针。
 * @param index    参数索引。
 * @param subindex 参数子索引。
 * @param data     参数值字节。
 * @param len      @p data 的长度（字节）。
 * @param persist  写入是否必须提交到非易失性存储器。
 * @return int 成功时返回 0，参数被拒绝时返回负值。
 */
typedef int (*iolink_ds_param_write_fn)(void* user, uint16_t index, uint8_t subindex,
                                        const uint8_t* data, size_t len, bool persist);

/**
 * @brief 数据存储引擎上下文
 *
 * 跟踪 DS 同步过程的状态与校验和。
 */
typedef struct
{
    iolink_ds_state_t state;              /**< 当前 DS 状态机位置 */
    const iolink_storage_api_t* storage;  /**< 绑定的存储实现 API */
    iolink_params_ctx_t* params_ctx;      /**< 设备本地可写参数上下文（绑定后使用） */
    void* param_user;                     /**< 参数回调使用的不透明用户指针 */
    iolink_ds_param_read_fn read_param;   /**< 统一的参数读取访问器 */
    iolink_ds_param_write_fn write_param; /**< 统一的参数写入访问器 */
    const iolink_ds_param_desc_t* params; /**< DS 参数描述符数组 */
    size_t param_count;                   /**< @ref params 中的条目数 */
    uint32_t current_checksum;            /**< 最近一次计算的本地参数校验和 */
    uint32_t master_checksum;             /**< 主站最近校验过的校验和 */
    bool ds_upload_flag;                  /**< DS_UPLOAD_REQ 挂起（State_Property 位 7） */
    bool upload_event_pending;            /**< DS_UPLOAD_REQ 通知推迟到 ISDU 空闲后发出 */
    uint8_t image[IOLINK_DS_IMAGE_MAX];   /**< 序列化的参数映像（DS_Data 0x0003） */
    size_t image_len;                     /**< @ref image 中的有效字节数 */
    bool download_invalid;                /**< 被拒绝的写入会使当前下载失效 */
    iolink_log_sink_t log;                /**< 日志汇点（默认静默；设备层绑定） */
} iolink_ds_ctx_t;

/**
 * @brief 初始化数据存储引擎
 *
 * @param ctx 要初始化的 DS 上下文
 * @param storage 存储钩子；仅在直接使用 DS 时可以为 NULL。设备
 *                层在编译启用 DS 时要求完整的后端。
 */
#if IOLINK_FEATURE_DATA_STORAGE
void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage);

/**
 * @brief 绑定用作 DS 参数源的可写参数上下文。
 *
 * @param ctx DS 上下文
 * @param params_ctx 要序列化/恢复的设备本地参数管理器上下文
 */
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);

/**
 * @brief 绑定统一的参数访问器与显式的参数描述符集合。
 *
 * 用调用方提供的描述符数组替换默认的仅标签集合，
 * 并让所有 DS 读写都经由 @p read / @p write 路由。厂商参数
 * （0x0040+ / 0x0100+）正是以这种方式加入数据存储集合。当 @p params
 * 为 NULL（或 @p count 为 0）时，描述符集合保持不变。
 *
 * 设备已经绑定了一对默认访问器，其中标准参数
 * 通过 params 上下文提供，其余参数通过
 * vendor_service 提供。仅需要不同 DS 索引列表的
 * 应用应当调用 iolink_ds_set_param_desc()，而不是替换访问器。
 *
 * @param ctx    DS 上下文
 * @param read   参数读取访问器（可为 NULL，表示保留当前访问器）
 * @param write  参数写入访问器（可为 NULL，表示保留当前访问器）
 * @param user   传回访问器的不透明指针。仅当
 *               @p read 或 @p write 非 NULL 时更新，因此传入 NULL 访问器会
 *               保留现有的用户指针。
 * @param params 描述符数组（可为 NULL）
 * @param count  @p params 中的描述符数量
 */
void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx, iolink_ds_param_read_fn read,
                                 iolink_ds_param_write_fn write, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count);

/**
 * @brief 仅替换数据存储参数描述符集合。
 *
 * 当前的读写访问器及其用户指针保持不变。已经
 * 通过 params 上下文或 vendor_service 提供参数的
 * 应用只需在此提供索引列表。
 *
 * @param ctx    DS 上下文
 * @param params 描述符数组（在 DS 的整个生命周期内必须保持有效）
 * @param count  @p params 中的描述符数量
 */
void iolink_ds_set_param_desc(iolink_ds_ctx_t* ctx, const iolink_ds_param_desc_t* params,
                              size_t count);

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
 * @brief 开始向主站上传参数（ParamUploadStart，0x01）
 *
 * @param ctx DS 上下文
 * @return int 成功时返回 0，DS 未初始化时返回负值
 */
int iolink_ds_start_upload(iolink_ds_ctx_t* ctx);

/**
 * @brief 开始从主站下载参数（ParamDownloadStart，0x03）
 *
 * @param ctx DS 上下文
 * @return int 成功时返回 0，DS 未初始化时返回负值
 */
int iolink_ds_start_download(iolink_ds_ctx_t* ctx);

/**
 * @brief 中止任何进行中的 DS 上传/下载并返回空闲状态。
 *
 * @param ctx DS 上下文
 * @return int 成功时返回 0
 */
int iolink_ds_abort(iolink_ds_ctx_t* ctx);

/**
 * @brief 处理标准 DS 系统命令
 *
 * @param ctx DS 上下文
 * @param cmd 系统命令（0x05-0x08）
 * @param access_locks 当前访问锁状态（索引 0x000C）
 * @return int 0：成功，-1：忙，-2：访问被拒绝，-3：未知
 */
int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);

/**
 * @brief 处理 DataStorageIndex DS_Command（Table B.10 subindex 01）。
 *
 * DS_Command 使用与旧版 SystemCommand Param* 集合不同的编码：
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
 * 生效；ParamDownloadEnd 会原子地提交这些写入。
 *
 * @param ctx      DS 上下文
 * @param index    参数索引
 * @param subindex 参数子索引
 * @param data     参数值字节
 * @param len      @p data 的长度（字节）
 * @return int 成功时返回 0，未处于下载状态或缓冲区已满时返回负值。
 */
int iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len);

/**
 * @brief 将设备中由 DS 支持的参数序列化到映像缓冲区。
 *
 * 从参数管理器读取 DS 集合中的每个参数，并将其打包
 * 到 @ref iolink_ds_ctx_t::image 中。同时更新 @ref iolink_ds_ctx_t::current_checksum。
 *
 * @param ctx DS 上下文
 * @return int 构建的映像字节数 (>= 0)，出错时返回负值。
 */
int iolink_ds_build_image(iolink_ds_ctx_t* ctx);

/**
 * @brief 应用从主站接收的参数映像（恢复）。
 *
 * 解析序列化的记录，并通过参数管理器将每个参数写回
 * （持久化到 NVM）。同时更新 @ref iolink_ds_ctx_t::current_checksum。
 *
 * @param ctx DS 上下文
 * @param data 序列化的映像字节
 * @param len  映像字节数
 * @return int 成功时返回 0，映像格式错误或被拒绝时返回负值。
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
 * @brief 校验已存储的映像与其记录的校验和是否一致。
 *
 * @param ctx DS 上下文
 * @return checksum(image) == current_checksum 时返回 true，否则返回 false。
 */
bool iolink_ds_verify(const iolink_ds_ctx_t* ctx);

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
 * @brief DS 参数描述符集合中的条目数。
 *
 * @param ctx DS 上下文
 * @return size_t 描述符数量。
 */
size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx);

/**
 * @brief 将描述符 @p i 复制到 @p out。
 *
 * @param ctx DS 上下文
 * @param i   描述符索引
 * @param out [out] 接收描述符（索引 + 子索引）
 * @return 当 @p i 在范围内时返回 true，否则返回 false。
 */
bool iolink_ds_param_at(const iolink_ds_ctx_t* ctx, size_t i, iolink_ds_param_desc_t* out);

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
