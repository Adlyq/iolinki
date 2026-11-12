/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file data_storage.c
 * @brief IO-Link 数据存储 (DS) 参数服务器。
 * @ingroup iolinki_data_storage
 *
 * 主站保存设备参数的不透明备份，以便替换单元能够自动恢复。
 * 上传时设备将其 DS 参数序列化为镜像（DS_Data，索引 0x0003）；
 * 下载时则应用从主站收到的镜像。
 * 每条记录的布局为：
 *   [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)]
 */

#include "iolinki/config.h"
#include "iolinki/data_storage.h"
#include "iolinki/params.h"
#include "iolinki/utils.h"
#include <string.h>

#if IOLINK_FEATURE_DATA_STORAGE

/** @brief 将 DS_UPLOAD_FLAG 持久化到标志区（10.4.2）。 */
static void ds_storage_save_flag(iolink_ds_ctx_t* ctx, uint8_t flag)
{
    if ((ctx != NULL) && (ctx->storage != NULL) && (ctx->storage->write != NULL)) {
        uint8_t value = (flag != 0U) ? 1U : 0U;
        (void) ctx->storage->write(ctx->storage->user, IOLINK_STORAGE_FLAG_BASE, &value, 1U);
    }
}

static void ds_storage_load_flag(iolink_ds_ctx_t* ctx)
{
    if ((ctx != NULL) && (ctx->storage != NULL) && (ctx->storage->read != NULL)) {
        uint8_t value = 0U;
        if (ctx->storage->read(ctx->storage->user, IOLINK_STORAGE_FLAG_BASE, &value, 1U) == 0) {
            /* 只有显式的 1 才请求上传；已擦除（0xFF）表示“无”。 */
            ctx->ds_upload_flag = (value == 1U);
        }
    }
}

static void ds_clear_upload_flag(iolink_ds_ctx_t* ctx)
{
    if ((ctx != NULL) && ctx->ds_upload_flag) {
        ctx->ds_upload_flag = false;
        ds_storage_save_flag(ctx, 0U);
    }
}

/** @brief 持久化 DS 镜像（magic + version + length + payload）。 */
static void ds_storage_save_image(iolink_ds_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->storage == NULL) || (ctx->storage->write == NULL)) {
        return;
    }
    uint8_t header[IOLINK_STORAGE_DS_HDR_SIZE];
    const uint32_t checksum = iolink_storage_checksum(ctx->image, ctx->image_len);
    header[0] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 24);
    header[1] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 16);
    header[2] = (uint8_t) (IOLINK_STORAGE_MAGIC >> 8);
    header[3] = (uint8_t) IOLINK_STORAGE_MAGIC;
    header[4] = (uint8_t) (IOLINK_STORAGE_VERSION & 0xFFU);
    header[5] = (uint8_t) (IOLINK_STORAGE_VERSION >> 8);
    header[6] = (uint8_t) (ctx->image_len >> 8);
    header[7] = (uint8_t) (ctx->image_len & 0xFFU);
    header[8] = (uint8_t) (checksum >> 24);
    header[9] = (uint8_t) (checksum >> 16);
    header[10] = (uint8_t) (checksum >> 8);
    header[11] = (uint8_t) checksum;
    (void) ctx->storage->write(ctx->storage->user, IOLINK_STORAGE_DS_BASE, header, sizeof(header));
    if (ctx->image_len > 0U) {
        (void) ctx->storage->write(ctx->storage->user,
                                   IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_HDR_SIZE, ctx->image,
                                   ctx->image_len);
    }
}

static void ds_storage_load_image(iolink_ds_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->storage == NULL) || (ctx->storage->read == NULL)) {
        return;
    }
    uint8_t header[IOLINK_STORAGE_DS_HDR_SIZE];
    if (ctx->storage->read(ctx->storage->user, IOLINK_STORAGE_DS_BASE, header, sizeof(header)) !=
        0) {
        return;
    }
    const uint32_t magic = ((uint32_t) header[0] << 24) | ((uint32_t) header[1] << 16) |
                           ((uint32_t) header[2] << 8) | (uint32_t) header[3];
    const uint16_t version = (uint16_t) (((uint16_t) header[5] << 8) | (uint16_t) header[4]);
    const size_t len = ((size_t) header[6] << 8) | (size_t) header[7];
    const uint32_t checksum = ((uint32_t) header[8] << 24) | ((uint32_t) header[9] << 16) |
                              ((uint32_t) header[10] << 8) | (uint32_t) header[11];
    if ((magic != IOLINK_STORAGE_MAGIC) || (version != (uint16_t) IOLINK_STORAGE_VERSION)) {
        return; /* 未初始化或布局不同。 */
    }
    if ((len == 0U) || (len > IOLINK_DS_IMAGE_MAX)) {
        return;
    }
    if (ctx->storage->read(ctx->storage->user, IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_HDR_SIZE,
                           ctx->image, len) == 0) {
        if (checksum != iolink_storage_checksum(ctx->image, len)) {
            return; /* 已损坏（位翻转 / 写入中断）。 */
        }
        ctx->image_len = len;
        ctx->current_checksum = iolink_ds_calc_checksum(ctx->image, len);
    }
}

/* 默认包含在数据存储集合中的参数（V1.1.5 设备标识参数，可写且因此
   值得备份）。应用可通过 iolink_ds_bind_param_access() 替换或扩展
   该集合。 */
static const iolink_ds_param_desc_t k_default_ds_params[] = {
    {IOLINK_IDX_APPLICATION_TAG, 0U, 0U},
    {IOLINK_IDX_FUNCTION_TAG, 0U, 0U},
    {IOLINK_IDX_LOCATION_TAG, 0U, 0U},
};

#define DS_PARAM_COUNT (sizeof(k_default_ds_params) / sizeof(k_default_ds_params[0]))
#define DS_RECORD_HEADER 4U /* index(2) + subindex(1) + length(1) */

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_storage_api_t* storage)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_ds_ctx_t))) {
        return;
    }
    ctx->storage = storage;
    ctx->state = IOLINK_DS_STATE_IDLE;
    /* 默认参数集：三个标准标签，通过传统 params 上下文（或调用方
       绑定的访问器）提供。 */
    ctx->params = k_default_ds_params;
    ctx->param_count = DS_PARAM_COUNT;
    ds_storage_load_image(ctx);
    ds_storage_load_flag(ctx);
}

void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx)
{
    if (ctx != NULL) {
        ctx->params_ctx = params_ctx;
    }
}

void iolink_ds_bind_param_access(iolink_ds_ctx_t* ctx, iolink_ds_param_read_fn read,
                                 iolink_ds_param_write_fn write, void* user,
                                 const iolink_ds_param_desc_t* params, size_t count)
{
    if (ctx == NULL) {
        return;
    }
    if (read != NULL) {
        ctx->read_param = read;
    }
    if (write != NULL) {
        ctx->write_param = write;
    }
    /* 仅在同时提供访问器时才采用调用方的 user 指针；只更改描述符集
       不得覆盖设备已绑定的 user。 */
    if ((read != NULL) || (write != NULL)) {
        ctx->param_user = user;
    }
    if ((params != NULL) && (count > 0U)) {
        ctx->params = params;
        ctx->param_count = count;
    }
}

void iolink_ds_set_param_desc(iolink_ds_ctx_t* ctx, const iolink_ds_param_desc_t* params,
                              size_t count)
{
    if ((ctx == NULL) || (params == NULL) || (count == 0U)) {
        return;
    }
    ctx->params = params;
    ctx->param_count = count;
}

/** @brief 通过绑定的访问器、params 上下文或传统全局集合
 * 读取参数。 */
static int ds_params_get(const iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                         uint8_t* buffer, size_t max_len)
{
    if ((ctx != NULL) && (ctx->read_param != NULL)) {
        return ctx->read_param(ctx->param_user, index, subindex, buffer, max_len);
    }
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_get(ctx->params_ctx, index, subindex, buffer, max_len);
    }
#if IOLINK_FEATURE_LEGACY_PARAMS
    return iolink_params_get(index, subindex, buffer, max_len);
#else
    (void) index;
    (void) subindex;
    (void) buffer;
    (void) max_len;
    return -1;
#endif
}

/** @brief 通过绑定的访问器、params 上下文或传统全局集合
 * 写入参数。 */
static int ds_params_set(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                         const uint8_t* data, size_t len, bool persist)
{
    if ((ctx != NULL) && (ctx->write_param != NULL)) {
        return ctx->write_param(ctx->param_user, index, subindex, data, len, persist);
    }
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_set(ctx->params_ctx, index, subindex, data, len, persist);
    }
#if IOLINK_FEATURE_LEGACY_PARAMS
    return iolink_params_set(index, subindex, data, len, persist);
#else
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return -1;
#endif
}

uint32_t iolink_ds_calc_checksum(const uint8_t* data, size_t len)
{
    /* 对序列化镜像计算 Fletcher-32（Table B.10 中的
       Parameter_Checksum 为 UIntegerT32）。主站将 DS 数据块及其校验和
       视为不透明（DS 与具体设备相关），因此一致性比较只需要一个
       稳定的设备本地校验和。 */
    uint32_t sum1 = 0U;
    uint32_t sum2 = 0U;
    if (!iolink_buf_is_valid(data, len)) {
        return 0U;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (sum1 + (uint32_t) data[i]) % 65535U;
        sum2 = (sum2 + sum1) % 65535U;
    }
    return (sum2 << 16U) | sum1;
}

int iolink_ds_build_image(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    size_t pos = 0U;
    for (size_t i = 0U; i < ctx->param_count; ++i) {
        const uint16_t index = ctx->params[i].index;
        const uint8_t subindex = ctx->params[i].subindex;

        if ((pos + DS_RECORD_HEADER) > IOLINK_DS_IMAGE_MAX) {
            return -1; /* 镜像放不下 */
        }
        /* 直接读入镜像尾部；可选的每参数 max_len
           限制取值长度。 */
        size_t cap = (size_t) IOLINK_DS_IMAGE_MAX - pos - DS_RECORD_HEADER;
        if ((ctx->params[i].max_len != 0U) && (cap > (size_t) ctx->params[i].max_len)) {
            cap = (size_t) ctx->params[i].max_len;
        }
        int n = ds_params_get(ctx, index, subindex, &ctx->image[pos + DS_RECORD_HEADER], cap);
        if (n < 0) {
            n = 0; /* 参数不存在 -> 空记录 */
        }
        else if ((size_t) n > cap) {
            n = (int) cap;
        }

        ctx->image[pos] = (uint8_t) (index >> 8);
        ctx->image[pos + 1U] = (uint8_t) (index & 0xFFU);
        ctx->image[pos + 2U] = subindex;
        ctx->image[pos + 3U] = (uint8_t) n;
        pos += DS_RECORD_HEADER + (size_t) n;
    }

    ctx->image_len = pos;
    ctx->current_checksum = iolink_ds_calc_checksum(ctx->image, pos);
    return (int) pos;
}

/** @brief 当 index/subindex 属于已配置的 DS 集合时返回真。 */
static bool ds_param_known(const iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    if (ctx == NULL) {
        return false;
    }
    for (size_t i = 0U; i < ctx->param_count; ++i) {
        if ((ctx->params[i].index == index) && (ctx->params[i].subindex == subindex)) {
            return true;
        }
    }
    return false;
}

int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || !iolink_buf_is_valid(data, len) || (len > IOLINK_DS_IMAGE_MAX)) {
        return -1;
    }

    /* 在应用任何内容之前校验完整的记录结构，确保截断或损坏的镜像
       绝不会部分覆盖参数。 */
    size_t pos = 0U;
    while (pos < len) {
        if ((pos + DS_RECORD_HEADER) > len) {
            return -1; /* 头部被截断 */
        }
        size_t rlen = (size_t) data[pos + 3U];
        if ((pos + DS_RECORD_HEADER + rlen) > len) {
            return -1; /* 数据被截断 */
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* 应用每条记录。未知索引会被跳过（替换设备可能未实现所有
       已备份的参数），而不是让恢复失败。*已知* 参数若拒绝该值，
       则使集合不一致。 */
    pos = 0U;
    while (pos < len) {
        uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
        uint8_t subindex = data[pos + 2U];
        size_t rlen = (size_t) data[pos + 3U];
        if (ds_param_known(ctx, index, subindex)) {
            if (ds_params_set(ctx, index, subindex, &data[pos + DS_RECORD_HEADER], rlen, true) !=
                0) {
                return -2; /* 已知参数拒绝了该值 */
            }
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* 恢复：从设备当前的实际参数重新序列化，使存储的镜像
       和校验和反映真实设备状态，而不是原始下载的内容。若镜像
       无法被忠实应用（例如跳过了未知索引），生成的校验和就与
       主站的不同，这会使下一次 iolink_ds_check() 重新请求下载，
       直到两侧一致。 */
    if (iolink_ds_build_image(ctx) < 0) {
        return -1;
    }
    ds_storage_save_image(ctx);
    return 0;
}

bool iolink_ds_verify(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return false;
    }
    return iolink_ds_calc_checksum(ctx->image, ctx->image_len) == ctx->current_checksum;
}

const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len)
{
    if (ctx == NULL) {
        return NULL;
    }
    if (ctx->image_len == 0U) {
        if (iolink_ds_build_image(ctx) < 0) {
            return NULL;
        }
    }
    if (out_len != NULL) {
        *out_len = ctx->image_len;
    }
    return ctx->image;
}

void iolink_ds_check(iolink_ds_ctx_t* ctx, uint32_t master_checksum)
{
    if (ctx == NULL) {
        return;
    }

    ctx->master_checksum = master_checksum;

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return;
    }

    if (master_checksum == 0U) {
        /* 主站无数据 -> 上传请求 */
        IOLINK_LOG_INFO(&ctx->log, "ds check: master has no image -> upload request");
        ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    }
    else if (master_checksum != ctx->current_checksum) {
        /* 校验和不匹配 -> 下载请求（更新设备） */
        IOLINK_LOG_INFO(&ctx->log, "ds check: checksum 0x%08lX != 0x%08lX -> download request",
                        (unsigned long) master_checksum, (unsigned long) ctx->current_checksum);
        ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
    else {
        IOLINK_LOG_DEBUG(&ctx->log, "ds check: checksum 0x%08lX matches",
                         (unsigned long) master_checksum);
    }
}

void iolink_ds_process(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    switch (ctx->state) {
        case IOLINK_DS_STATE_UPLOAD_REQ:
            /* 主站无数据 -> 为上传快照当前参数。 */
            (void) iolink_ds_build_image(ctx);
            ctx->state = IOLINK_DS_STATE_UPLOADING;
            break;

        case IOLINK_DS_STATE_UPLOADING:
            /* 保持 Uploading 直到 UploadEnd、ParamBreak、复位或
               中断性命令。不要在帧之间自动完成。 */
            break;

        case IOLINK_DS_STATE_DOWNLOAD_REQ:
            /* 主站指示不匹配 -> 设备将接收参数。 */
            ctx->state = IOLINK_DS_STATE_DOWNLOADING;
            break;

        case IOLINK_DS_STATE_DOWNLOADING:
            /* 保持 Downloading 直到 DownloadEnd 提交暂存镜像。 */
            break;

        default:
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;
    }
}

int iolink_ds_start_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return -1; /* 忙 */
    }

    (void) iolink_ds_build_image(ctx);
    ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    return 0;
}

int iolink_ds_start_download(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return -1; /* 忙 */
    }

    ctx->image_len = 0U; /* 清除之前暂存的镜像 */
    ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    return 0;
}

int iolink_ds_abort(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    /* 中止任何活动的 DS 操作。 */
    ctx->state = IOLINK_DS_STATE_IDLE;
    ctx->image_len = 0U;
    ctx->download_invalid = false;
    return 0;
}

/** @brief 0x01 UploadStart：中止活动下载，构建镜像并启动。 */
static int ds_cmd_upload_start(iolink_ds_ctx_t* ctx)
{
    /* 0327/0328/0330：新上传会中止活动下载并丢弃其暂存值；
       同时也会重新启动活动的上传。 */
    if ((ctx->state == IOLINK_DS_STATE_DOWNLOADING) ||
        (ctx->state == IOLINK_DS_STATE_DOWNLOAD_REQ)) {
        ctx->image_len = 0U;
    }
    (void) iolink_ds_build_image(ctx);
    ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    return 0;
}

/** @brief 0x02 UploadEnd：清除上传标志或结束交换。 */
static int ds_cmd_upload_end(iolink_ds_ctx_t* ctx)
{
    /* 0324：在 Idle 中收到结束命令属于意外。0322：DSIdle_2 是设置了
       DS_UPLOAD_FLAG 的 Idle；在其中结束会清除该标志。0330：下载期间
       的上传结束会丢弃暂存的下载值。 */
    if (ctx->state == IOLINK_DS_STATE_IDLE) {
        if (ctx->ds_upload_flag) {
            ds_clear_upload_flag(ctx);
            return 0;
        }
        return -6;
    }
    if ((ctx->state == IOLINK_DS_STATE_DOWNLOADING) ||
        (ctx->state == IOLINK_DS_STATE_DOWNLOAD_REQ)) {
        ctx->image_len = 0U;
    }
    ctx->state = IOLINK_DS_STATE_IDLE;
    ds_clear_upload_flag(ctx);
    IOLINK_LOG_INFO(&ctx->log, "ds upload complete");
    return 0;
}

/** @brief 0x03 DownloadStart：重置暂存镜像并进入 Downloading。 */
static int ds_cmd_download_start(iolink_ds_ctx_t* ctx)
{
    /* 0147/0328：新下载会重新启动活动下载并中止活动上传
       （上传没有可丢弃的暂存镜像）。 */
    ctx->image_len = 0U;
    ctx->download_invalid = false;
    ctx->state = IOLINK_DS_STATE_DOWNLOADING;
    return 0;
}

/** @brief 0x04 DownloadEnd：校验/应用暂存镜像并停止。 */
static int ds_cmd_download_end(iolink_ds_ctx_t* ctx)
{
    /* 0324：在 Idle 中收到结束命令属于意外。0322：在 DSIdle_2（设置了
       DS_UPLOAD_FLAG 的 Idle）中结束仅清除该标志。 */
    if (ctx->state == IOLINK_DS_STATE_IDLE) {
        if (ctx->ds_upload_flag) {
            ds_clear_upload_flag(ctx);
            return 0;
        }
        return -6;
    }
    if (ctx->state != IOLINK_DS_STATE_DOWNLOADING) {
        /* 有上传处于活动状态：丢弃它并停止。 */
        ctx->state = IOLINK_DS_STATE_IDLE;
        return 0;
    }

    int rc = 0;
    if (ctx->download_invalid) {
        ctx->download_invalid = false;
        ctx->image_len = 0U;
        ctx->state = IOLINK_DS_STATE_IDLE;
        IOLINK_LOG_WARN(&ctx->log, "ds download rejected: inconsistent image");
        return -4; /* PAR_SETINCONSIST (0x8041) */
    }
    if (ctx->image_len > 0U) {
        rc = iolink_ds_apply_image(ctx, ctx->image, ctx->image_len);
    }
    ctx->state = IOLINK_DS_STATE_IDLE;
    if (rc == -1) {
        IOLINK_LOG_ERROR(&ctx->log, "ds download failed: malformed image");
        return -5; /* PAR_SETINVALID (0x8040)：镜像格式错误 */
    }
    if (rc == -2) {
        IOLINK_LOG_ERROR(&ctx->log, "ds download failed: parameter rejected");
        return -4; /* PAR_SETINCONSIST (0x8041)：参数被拒绝 */
    }
    ds_clear_upload_flag(ctx);
    IOLINK_LOG_INFO(&ctx->log, "ds download complete");
    return 0;
}

/** @brief 0x06 ParamBreak：中止活动的数据存储操作。 */
static int ds_cmd_break(iolink_ds_ctx_t* ctx)
{
    if (ctx->state == IOLINK_DS_STATE_IDLE) {
        return -6;
    }
    return iolink_ds_abort(ctx);
}

int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks)
{
    if (ctx == NULL) {
        return -1;
    }

    /* 检查下载命令（写入设备）的访问锁 */
    if ((cmd == IOLINK_CMD_PARAM_DOWNLOAD_START) || (cmd == IOLINK_CMD_PARAM_DOWNLOAD_END)) {
        if ((access_locks & IOLINK_LOCK_DS) != 0U) {
            /* DS 逻辑已锁定 */
            return -2; /* 表示访问被拒绝（用户应映射为 ISDU 错误） */
        }
    }

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_START: /* 0x01 */
            return ds_cmd_upload_start(ctx);
        case IOLINK_CMD_PARAM_UPLOAD_END: /* 0x02 */
            return ds_cmd_upload_end(ctx);
        case IOLINK_CMD_PARAM_DOWNLOAD_START: /* 0x03 */
            return ds_cmd_download_start(ctx);
        case IOLINK_CMD_PARAM_DOWNLOAD_END: /* 0x04 */
            return ds_cmd_download_end(ctx);
        case IOLINK_CMD_PARAM_BREAK: /* 0x06 / ParamBreak (Table B.9) */
            return ds_cmd_break(ctx);
        default:
            return -3; /* 未知命令 */
    }
}

int iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t command, uint16_t access_locks)
{
    uint8_t param_cmd;

    if (ctx == NULL) {
        return -1;
    }

    /* 将 Table B.10 的 DS_Command 编码映射为 iolink_ds_handle_command()
       理解的传统 Param* 编码（DS_Command 0x05 = DS_Break）。 */
    switch (command) {
        case 0x01U: /* DS_UploadStart */
            param_cmd = IOLINK_CMD_PARAM_UPLOAD_START;
            break;
        case 0x02U: /* DS_UploadEnd */
            param_cmd = IOLINK_CMD_PARAM_UPLOAD_END;
            break;
        case 0x03U: /* DS_DownloadStart */
            param_cmd = IOLINK_CMD_PARAM_DOWNLOAD_START;
            break;
        case 0x04U: /* DS_DownloadEnd */
            param_cmd = IOLINK_CMD_PARAM_DOWNLOAD_END;
            break;
        case 0x05U: /* DS_Break */
            param_cmd = IOLINK_CMD_PARAM_BREAK;
            break;
        default:
            return -3; /* 保留 */
    }

    int ret = iolink_ds_handle_command(ctx, param_cmd, access_locks);

    /* DS_UPLOAD_FLAG 在每次上传/下载序列结束时复位
       （Table B.10）。 */
    if ((ret == 0) && ((command == 0x02U) || (command == 0x04U))) {
        ctx->ds_upload_flag = false;
    }
    return ret;
}

bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx)
{
    return (ctx != NULL) && (ctx->state == IOLINK_DS_STATE_DOWNLOADING);
}

int iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len)
{
    if (!iolink_ds_downloading(ctx)) {
        return -1;
    }
    if ((len > 0U) && !iolink_buf_is_valid(data, len)) {
        return -1;
    }
    const size_t n = iolink_ds_param_count(ctx);
    bool found = false;
    for (size_t i = 0U; i < n; i++) {
        iolink_ds_param_desc_t d;
        if (!iolink_ds_param_at(ctx, i, &d)) {
            break;
        }
        if ((d.index == index) && (d.subindex == subindex)) {
            found = true;
            if ((d.max_len > 0U) && (len > (size_t) d.max_len)) {
                ctx->download_invalid = true;
                return -2; /* VAL_LENOVRRUN */
            }
            break;
        }
    }
    if (!found) {
        return -1;
    }
    if ((ctx->image_len + DS_RECORD_HEADER + len) > IOLINK_DS_IMAGE_MAX) {
        ctx->download_invalid = true;
        return -1; /* 缓冲区已满 */
    }

    const size_t pos = ctx->image_len;
    ctx->image[pos] = (uint8_t) (index >> 8);
    ctx->image[pos + 1U] = (uint8_t) (index & 0xFFU);
    ctx->image[pos + 2U] = subindex;
    ctx->image[pos + 3U] = (uint8_t) len;
    if (len > 0U) {
        (void) memcpy(&ctx->image[pos + DS_RECORD_HEADER], data, len);
    }
    ctx->image_len = pos + DS_RECORD_HEADER + len;
    return 0;
}

uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx)
{
    uint8_t property = 0U;

    if (ctx == NULL) {
        return 0U;
    }
    switch (ctx->state) {
        case IOLINK_DS_STATE_UPLOAD_REQ:
        case IOLINK_DS_STATE_UPLOADING:
            property = 0x02U; /* 0b01 上传 */
            break;
        case IOLINK_DS_STATE_DOWNLOAD_REQ:
        case IOLINK_DS_STATE_DOWNLOADING:
            property = 0x04U; /* 0b10 下载 */
            break;
        default:
            property = 0x00U; /* 0b00 未激活 */
            break;
    }
    if (ctx->ds_upload_flag) {
        property |= 0x80U; /* DS_UPLOAD_FLAG */
    }
    return property;
}

uint32_t iolink_ds_size(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return (uint32_t) ctx->image_len;
}

uint32_t iolink_ds_checksum(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return ctx->current_checksum;
}

size_t iolink_ds_param_count(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return ctx->param_count;
}

bool iolink_ds_param_at(const iolink_ds_ctx_t* ctx, size_t i, iolink_ds_param_desc_t* out)
{
    if ((ctx == NULL) || (out == NULL) || (i >= ctx->param_count)) {
        return false;
    }
    *out = ctx->params[i];
    return true;
}

void iolink_ds_request_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx != NULL) {
        ctx->ds_upload_flag = true;
        ds_storage_save_flag(ctx, 1U);
        (void) iolink_ds_build_image(ctx);
        ds_storage_save_image(ctx);
        /* 该通知由设备泵在 ISDU 传输空闲后发布（10.4.2），
           而不是在命令响应内部发布。 */
        ctx->upload_event_pending = true;
    }
}

void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx != NULL) {
        ctx->ds_upload_flag = false;
        ds_storage_save_flag(ctx, 0U);
    }
}

#endif /* IOLINK_FEATURE_DATA_STORAGE */
