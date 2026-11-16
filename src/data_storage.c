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
#include "iolinki/od.h"
#include "iolinki/utils.h"
#include <string.h>

#if IOLINK_FEATURE_DATA_STORAGE

/* 无镜像缓冲时的流式检查块大小。只用于长度/校验和计算与无 workspace
 * 的持久化加载；设备构建总会绑定 workspace，因此不会限制实际参数长度。 */
#define IOLINK_DS_STREAM_CHUNK 32U

/** @brief 清除挂起的上传请求；标志变化时通知应用落盘。 */
static void ds_clear_upload_flag(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    const bool changed = ctx->ds_upload_flag;
    ctx->ds_upload_flag = false;
    if (changed && (ctx->flag_persist != NULL)) {
        ctx->flag_persist(ctx->flag_self, false);
    }
}

#define DS_RECORD_HEADER 4U /* index(2) + subindex(1) + length(1) */

/* DS 参数集不单独维护：集合 = 标准表 + 应用表中带对应 storage 位的表项。 */

/** @brief 标准表 + 应用表中带 @p mask 位的表项数量。 */
static size_t ds_entry_count(const iolink_ds_ctx_t* ctx, uint8_t mask)
{
    size_t n = 0U;

    for (size_t i = 0U; i < iolink_od_count(); i++) {
        const iolink_od_entry_t* entry = iolink_od_at(i);
        if ((entry != NULL) && ((entry->storage & mask) != 0U)) {
            n++;
        }
    }
    if ((ctx != NULL) && (ctx->od_app != NULL)) {
        for (size_t i = 0U; i < ctx->od_app->count; i++) {
            if ((ctx->od_app->entries[i].storage & mask) != 0U) {
                n++;
            }
        }
    }
    return n;
}

/** @brief 第 @p i 个带 @p mask 位的表项（标准表在前，应用表按表序）。 */
static const iolink_od_entry_t* ds_entry_at(const iolink_ds_ctx_t* ctx, size_t i, uint8_t mask)
{
    size_t seen = 0U;

    for (size_t j = 0U; j < iolink_od_count(); j++) {
        const iolink_od_entry_t* entry = iolink_od_at(j);
        if ((entry == NULL) || ((entry->storage & mask) == 0U)) {
            continue;
        }
        if (seen == i) {
            return entry;
        }
        seen++;
    }
    if ((ctx != NULL) && (ctx->od_app != NULL)) {
        for (size_t j = 0U; j < ctx->od_app->count; j++) {
            const iolink_od_entry_t* entry = &ctx->od_app->entries[j];
            if ((entry->storage & mask) == 0U) {
                continue;
            }
            if (seen == i) {
                return entry;
            }
            seen++;
        }
    }
    return NULL;
}

/** @brief 把暂存缓冲接到上下文（唯一设置 image 指针的位置）。 */
static void ds_attach_image(iolink_ds_ctx_t* ctx, uint8_t* buffer, size_t size)
{
    ctx->image = buffer;
    ctx->image_size = size;
    ctx->image_len = 0U;
    ctx->image_valid = false;
}

void iolink_ds_bind_workspace(iolink_ds_ctx_t* ctx, iolink_workspace_t* IOLINK_NULLABLE ws)
{
    if (ctx == NULL) {
        return;
    }
    if (ws != NULL) {
        ds_attach_image(ctx, ws->ds_stage, sizeof(ws->ds_stage));
    }
    else {
#if IOLINK_WORKSPACE_EMBEDDED
        ds_attach_image(ctx, ctx->scratch, sizeof(ctx->scratch));
#else
        /* 无内嵌兜底：解绑必须清空指针与容量，不能留下旧工作区的悬空指针。 */
        ds_attach_image(ctx, NULL, 0U);
#endif
    }
    /* 暂存缓冲只服务序列化/下载；持久化镜像在 bind_sources 之后加载。 */
}

void iolink_ds_init(iolink_ds_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_ds_ctx_t))) {
        return;
    }
    ctx->state = IOLINK_DS_STATE_IDLE;
#if IOLINK_WORKSPACE_EMBEDDED
    ds_attach_image(ctx, ctx->scratch, sizeof(ctx->scratch));
#endif
}

void iolink_ds_set_flag_persist(iolink_ds_ctx_t* ctx, iolink_ds_flag_persist_fn IOLINK_NULLABLE fn,
                                iolink_app_t* IOLINK_NULLABLE self)
{
    if (ctx == NULL) {
        return;
    }
    ctx->flag_persist = fn;
    ctx->flag_self = self;
}

void iolink_ds_bind_sources(iolink_ds_ctx_t* ctx, const iolink_od_app_t* IOLINK_NULLABLE app,
                            iolink_app_t* IOLINK_NULLABLE self)
{
    if (ctx == NULL) {
        return;
    }
    ctx->od_app = app;
    ctx->self = self;
}

/** @brief 组合表项的值访问上下文。 */
static iolink_od_ctx_t ds_od_ctx(iolink_ds_ctx_t* ctx)
{
    iolink_od_ctx_t od;
    od.app = ctx->od_app;
    od.self = ctx->self;
    return od;
}

/** @brief 读取参数：集合成员与值后端都来自对象字典。 */
static iolink_result_t ds_params_get(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                     uint8_t* buffer, size_t max_len)
{
    if (ctx == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    const iolink_od_entry_t* entry = iolink_od_find(ctx->od_app, index, subindex);
    if (entry == NULL) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    const iolink_od_ctx_t od = ds_od_ctx(ctx);
    return iolink_od_read_value(&od, entry, subindex, buffer, max_len);
}

/** @brief 写入参数：是否持久化由表项的写语义决定。 */
static iolink_result_t ds_params_set(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                     const uint8_t* data, size_t len)
{
    if (ctx == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    const iolink_od_entry_t* entry = iolink_od_find(ctx->od_app, index, subindex);
    if (entry == NULL) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    iolink_od_ctx_t od = ds_od_ctx(ctx);
    return iolink_od_write_value(&od, entry, subindex, data, len);
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

/** @brief 按记录顺序累加 Fletcher-32（与 iolink_ds_calc_checksum 等价）。 */
static void ds_accumulate(uint32_t* sum1, uint32_t* sum2, const uint8_t* data, size_t len)
{
    for (size_t i = 0U; i < len; ++i) {
        *sum1 = (*sum1 + (uint32_t) data[i]) % 65535U;
        *sum2 = (*sum2 + *sum1) % 65535U;
    }
}

static int ds_serialize_entries(iolink_ds_ctx_t* ctx, uint8_t* out, size_t cap,
                                uint8_t storage_mask, uint32_t* checksum_out)
{
    if (ctx == NULL) {
        return -1;
    }
    size_t limit = IOLINK_DS_IMAGE_MAX;
    if ((out != NULL) && (cap < limit)) {
        limit = cap;
    }

    size_t pos = 0U;
    uint32_t sum1 = 0U;
    uint32_t sum2 = 0U;
    const size_t count = ds_entry_count(ctx, storage_mask);
    for (size_t i = 0U; i < count; ++i) {
        const iolink_od_entry_t* entry = ds_entry_at(ctx, i, storage_mask);
        if (entry == NULL) {
            break;
        }
        const uint16_t index = entry->index;
        const uint8_t subindex = entry->subindex;
        if ((pos + DS_RECORD_HEADER) > limit) {
            return -1; /* 镜像放不下 */
        }
        size_t value_cap = limit - pos - DS_RECORD_HEADER;
        if ((entry->block_len != 0U) && (value_cap > (size_t) entry->block_len)) {
            value_cap = (size_t) entry->block_len;
        }

        uint8_t header[DS_RECORD_HEADER];
        header[0] = (uint8_t) (index >> 8);
        header[1] = (uint8_t) (index & 0xFFU);
        header[2] = subindex;

        if (out != NULL) {
            /* 直接把值读入输出缓冲尾部，再补记录头：无额外镜像副本。 */
            const iolink_result_t read_res =
                ds_params_get(ctx, index, subindex, &out[pos + DS_RECORD_HEADER], value_cap);
            size_t n = iolink_result_succeeded(read_res) ? (size_t) read_res.len : 0U;
            if (n > value_cap) {
                n = value_cap;
            }
            header[3] = (uint8_t) n;
            (void) memcpy(&out[pos], header, DS_RECORD_HEADER);
            ds_accumulate(&sum1, &sum2, &out[pos], DS_RECORD_HEADER + n);
            pos += DS_RECORD_HEADER + n;
        }
        else {
            /* 只统计：用小栈块读取；超过块大小的参数在无 workspace 的
             * 独立构建中不计入数据（设备构建总会绑定 workspace）。 */
            uint8_t chunk[IOLINK_DS_STREAM_CHUNK];
            size_t chunk_cap = value_cap;
            if (chunk_cap > sizeof(chunk)) {
                chunk_cap = sizeof(chunk);
            }
            const iolink_result_t read_res = ds_params_get(ctx, index, subindex, chunk, chunk_cap);
            size_t n = iolink_result_succeeded(read_res) ? (size_t) read_res.len : 0U;
            if (n > chunk_cap) {
                n = chunk_cap;
            }
            header[3] = (uint8_t) n;
            ds_accumulate(&sum1, &sum2, header, DS_RECORD_HEADER);
            ds_accumulate(&sum1, &sum2, chunk, n);
            pos += DS_RECORD_HEADER + n;
        }
    }

    *checksum_out = (sum2 << 16U) | sum1;
    return (int) pos;
}

int iolink_ds_serialize(iolink_ds_ctx_t* ctx, uint8_t* IOLINK_NULLABLE out, size_t cap)
{
    uint32_t checksum = 0U;

    const int n = ds_serialize_entries(ctx, out, cap, IOLINK_OD_STORAGE_DS, &checksum);
    if (n < 0) {
        return n;
    }
    ctx->image_len = (size_t) n;
    ctx->current_checksum = checksum;
    return n;
}

int iolink_ds_build_image(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    if (ctx->image != NULL) {
        const int n = iolink_ds_serialize(ctx, ctx->image, ctx->image_size);
        ctx->image_valid = (n >= 0);
        return n;
    }
    ctx->image_valid = false;
    return iolink_ds_serialize(ctx, NULL, 0U);
}

void iolink_ds_refresh(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    if ((ctx->image != NULL) && (ctx->image_size > 0U)) {
        /* 暂存缓冲只作为序列化的落点，不建立常驻快照：上传/校验和读取
         * 之后仍可随时从当前参数重新生成。 */
        (void) iolink_ds_serialize(ctx, ctx->image, ctx->image_size);
        ctx->image_valid = false;
        return;
    }
    (void) iolink_ds_serialize(ctx, NULL, 0U);
    ctx->image_valid = false;
}

/** @brief 当 index/subindex 属于 DS 集合（OD 表带 DS 位）时返回真。 */
static bool ds_param_known(const iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    if (ctx == NULL) {
        return false;
    }
    return iolink_od_is_ds(ctx->od_app, index, subindex);
}

int iolink_ds_factory_reset(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }
    if (iolink_ds_downloading(ctx)) {
        /* 下载暂存与参数镜像共用缓冲；复位优先，丢弃未提交的下载。 */
        (void) iolink_ds_abort(ctx);
    }
    iolink_od_ctx_t od;
    od.app = ctx->od_app;
    od.self = ctx->self;

    /* 复位 DS 集合：它是主站可备份/恢复的参数集，也是库当前唯一持有的
       集合标记。纯应用持久化参数由应用自行复位。 */
    /* `block_default` 可以为 NULL（没有默认值）；此时长度为 0，指针不会被
     * 解引用，但 `iolink_od_write_value()` 的契约要求非空，所以给一个有效
     * 对象。 */
    static const uint8_t k_empty_default = 0U;
    const size_t count = ds_entry_count(ctx, IOLINK_OD_STORAGE_DS);
    for (size_t i = 0U; i < count; i++) {
        const iolink_od_entry_t* entry = ds_entry_at(ctx, i, IOLINK_OD_STORAGE_DS);
        if (entry == NULL) {
            break;
        }
        if (entry->handler != (uint8_t) IOLINK_OD_HANDLER_NONE) {
            continue; /* 内建处理器（含标签）由设备/应用复位。 */
        }
        if (entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
            /* 走统一写路径，让应用通过 on_change 收到“已复位，请落盘”。 */
            const uint8_t* def =
                (entry->block_default != NULL) ? entry->block_default : &k_empty_default;
            const size_t def_len = (entry->block_default != NULL) ? (size_t) entry->block_len : 0U;
            (void) iolink_od_write_value(&od, entry, entry->subindex, def, def_len);
            continue;
        }
        uint8_t wire[4] = {0U};
        const iolink_result_t encoded =
            iolink_od_encode(entry, entry->scalar_default, wire, sizeof(wire));
        if (iolink_result_succeeded(encoded)) {
            (void) iolink_od_write_value(&od, entry, entry->subindex, wire, encoded.len);
        }
    }
    return 0;
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

    /* 第一阶段：已知参数的无副作用预校验。必须在写入任何参数之前完成，
       这样一条非法记录不会留下"前面已生效、后面失败"的半恢复状态。 */
    {
        iolink_app_t* IOLINK_NULLABLE hook_self = ctx->self;
        pos = 0U;
        while (pos < len) {
            uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
            uint8_t subindex = data[pos + 2U];
            size_t rlen = (size_t) data[pos + 3U];
            const iolink_od_entry_t* entry = NULL;
            if (ds_param_known(ctx, index, subindex)) {
                entry = iolink_od_find(ctx->od_app, index, subindex);
            }
            if ((entry != NULL) && !iolink_result_succeeded(iolink_od_check_value(
                                       entry, hook_self, &data[pos + DS_RECORD_HEADER], rlen))) {
                return -2; /* 已知参数拒绝了该值：尚未写入任何参数 */
            }
            pos += DS_RECORD_HEADER + rlen;
        }
    }

    /* 第二阶段：应用每条记录。未知索引会被跳过（替换设备可能未实现所有
       已备份的参数），而不是让恢复失败。*已知* 参数若拒绝该值，
       则使集合不一致。 */
    pos = 0U;
    while (pos < len) {
        uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
        uint8_t subindex = data[pos + 2U];
        size_t rlen = (size_t) data[pos + 3U];
        if (ds_param_known(ctx, index, subindex)) {
            if (!iolink_result_succeeded(
                    ds_params_set(ctx, index, subindex, &data[pos + DS_RECORD_HEADER], rlen))) {
                return -2; /* 已知参数拒绝了该值（预校验未覆盖的写入期失败） */
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
    iolink_ds_refresh(ctx);
    return 0;
}

const uint8_t* IOLINK_NULLABLE iolink_ds_get_image(iolink_ds_ctx_t* ctx,
                                                   size_t* IOLINK_NULLABLE out_len)
{
    if (ctx == NULL) {
        return NULL;
    }
    if (ctx->image == NULL) {
        if (out_len != NULL) {
            *out_len = 0U;
        }
        return NULL;
    }
    if (!ctx->image_valid || (ctx->image_len == 0U)) {
        if (iolink_ds_build_image(ctx) < 0) {
            if (out_len != NULL) {
                *out_len = 0U;
            }
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
        IOLINK_LOG_INFO(ctx->log, "ds check: master has no image -> upload request");
        ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    }
    else if (master_checksum != ctx->current_checksum) {
        /* 校验和不匹配 -> 下载请求（更新设备） */
        IOLINK_LOG_INFO(ctx->log, "ds check: checksum 0x%08lX != 0x%08lX -> download request",
                        (unsigned long) master_checksum, (unsigned long) ctx->current_checksum);
        ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
    else {
        IOLINK_LOG_DEBUG(ctx->log, "ds check: checksum 0x%08lX matches",
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
            /* 主站无数据 -> 刷新上传视图（长度/校验和；镜像按需流式生成）。 */
            iolink_ds_refresh(ctx);
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

        case IOLINK_DS_STATE_IDLE:
        /* 每个枚举项都必须显式列出：新增状态而不处理时，这里会漏掉。 */
        default:
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;
    }
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
    iolink_ds_refresh(ctx);
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
    IOLINK_LOG_INFO(ctx->log, "ds upload complete");
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
        IOLINK_LOG_WARN(ctx->log, "ds download rejected: inconsistent image");
        return -4; /* PAR_SETINCONSIST (0x8041) */
    }
    if (ctx->image_len > 0U) {
        if (ctx->image == NULL) {
            /* image_len 在 measure-only 序列化（out == NULL）时也会被写入，
               那时它是 DataStorageIndex 上报的参数集大小，不是暂存长度。
               没有缓冲就没有可提交的内容：显式失败，但不要清掉上报长度。 */
            ctx->state = IOLINK_DS_STATE_IDLE;
            IOLINK_LOG_WARN(ctx->log, "ds download end without staging buffer");
            return -4; /* PAR_SETINCONSIST (0x8041) */
        }
        rc = iolink_ds_apply_image(ctx, ctx->image, ctx->image_len);
    }
    ctx->state = IOLINK_DS_STATE_IDLE;
    if (rc == -1) {
        IOLINK_LOG_ERROR(ctx->log, "ds download failed: malformed image");
        return -5; /* PAR_SETINVALID (0x8040)：镜像格式错误 */
    }
    if (rc == -2) {
        IOLINK_LOG_ERROR(ctx->log, "ds download failed: parameter rejected");
        return -4; /* PAR_SETINCONSIST (0x8041)：参数被拒绝 */
    }
    ds_clear_upload_flag(ctx);
    IOLINK_LOG_INFO(ctx->log, "ds download complete");
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

int iolink_ds_handle_param_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks)
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

    /* 将 Table B.10 的 DS_Command 编码映射为 iolink_ds_handle_param_command()
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

    int ret = iolink_ds_handle_param_command(ctx, param_cmd, access_locks);

    /* DS_UPLOAD_FLAG 在每次上传/下载序列结束时复位
       （Table B.10）。 */
    if ((ret == 0) && ((command == 0x02U) || (command == 0x04U))) {
        ds_clear_upload_flag(ctx);
    }
    return ret;
}

bool iolink_ds_downloading(const iolink_ds_ctx_t* ctx)
{
    return (ctx != NULL) && (ctx->state == IOLINK_DS_STATE_DOWNLOADING);
}

iolink_result_t iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                                      const uint8_t* data, size_t len)
{
    if (!iolink_ds_downloading(ctx)) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if ((len > 0U) && !iolink_buf_is_valid(data, len)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    const iolink_od_entry_t* entry = NULL;
    if (iolink_od_is_ds(ctx->od_app, index, subindex)) {
        entry = iolink_od_find(ctx->od_app, index, subindex);
    }
    if (entry == NULL) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if ((entry->block_len > 0U) && (len > (size_t) entry->block_len)) {
        ctx->download_invalid = true;
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    }
    if ((ctx->image == NULL) || ((ctx->image_len + DS_RECORD_HEADER + len) > ctx->image_size)) {
        ctx->download_invalid = true;
        return iolink_result_error(IOLINK_RESULT_INVALID);
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
    ctx->image_valid = true; /* 下载暂存：完整记录集合，但尚未提交 */
    return iolink_result_ok(0U);
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
        case IOLINK_DS_STATE_IDLE:
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
    return ds_entry_count(ctx, IOLINK_OD_STORAGE_DS);
}

const iolink_od_entry_t* IOLINK_NULLABLE iolink_ds_param_entry(const iolink_ds_ctx_t* ctx, size_t i)
{
    if ((ctx == NULL) || (i >= ds_entry_count(ctx, IOLINK_OD_STORAGE_DS))) {
        return NULL;
    }
    return ds_entry_at(ctx, i, IOLINK_OD_STORAGE_DS);
}

void iolink_ds_request_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx != NULL) {
        ctx->ds_upload_flag = true;
        if (ctx->flag_persist != NULL) {
            ctx->flag_persist(ctx->flag_self, true);
        }
        /* 尺寸/校验和按当前参数集重建；没有独立的 DS 镜像需要落盘。 */
        iolink_ds_refresh(ctx);
        /* 该通知由设备泵在 ISDU 传输空闲后发布（10.4.2），
           而不是在命令响应内部发布。 */
        ctx->upload_event_pending = true;
    }
}

void iolink_ds_clear_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx != NULL) {
        ds_clear_upload_flag(ctx);
    }
}

#endif /* IOLINK_FEATURE_DATA_STORAGE */
