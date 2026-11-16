/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/isdu.h"
#include "iolinki/internal/dll_main.h"
#include "iolinki/iolink.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/data_storage.h"
#include <string.h>
#include <stdint.h>
#include "isdu_internal.h"

/**
 * @file isdu_ds.c
 * @brief ISDU 数据存储与 OD 表项服务。
 *
 * 数据存储上传/下载索引、DS 访问权限判定，以及应用对象
 * 字典表项（标准或扩展索引）的读写应答。
 */

/* -------------------------------------------------------------------------
 * 数据存储服务
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_DATA_STORAGE
/** @brief 处理数据存储索引：备份（读镜像）或恢复（应用镜像）。 */
static size_t isdu_append_ds_index_list(iolink_ds_ctx_t* ds, uint8_t* buf, size_t pos)
{
    const size_t n = iolink_ds_param_count(ds);
    for (size_t i = 0U; i < n; i++) {
        const iolink_od_entry_t* entry = iolink_ds_param_entry(ds, i);
        if (entry == NULL) {
            break;
        }
        if ((pos + 3U) > IOLINK_ISDU_BUFFER_SIZE) {
            break;
        }
        buf[pos++] = (uint8_t) (entry->index >> 8);
        buf[pos++] = (uint8_t) (entry->index & 0xFFU);
        buf[pos++] = entry->subindex;
    }
    if ((pos + 2U) <= IOLINK_ISDU_BUFFER_SIZE) {
        buf[pos++] = 0x00U; /* 终止标记 */
        buf[pos++] = 0x00U;
    }
    return pos;
}

/** @brief DS 写 subindex 0：旧的镜像恢复，或按只读拒绝。 */
static void isdu_ds_write_sub0(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
    if (isdu_cap(ctx, IOLINK_CAP_DS_LEGACY_SUB0)) {
        /* V1.1 之前的整镜像载荷（兼容位 IOLINK_CAP_DS_LEGACY_SUB0）。 */
        const int rc = iolink_ds_apply_image(ds, ctx->buffer, ctx->buffer_idx);
        if (rc != 0) {
            isdu_respond_error(ctx, (rc == -1) ? IOLINK_ISDU_ERROR_PARAM_SET_INVALID
                                               : IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
        }
        else {
            ctx->response_len = 0U; /* 写入已确认 */
        }
        return;
    }
    (void) ds;
    /* Table B.10：整条记录为只读；只有 DS_Command（subindex 1）
       可写。 */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

/** @brief DS 写 subindex 1：DS_Command。 */
static void isdu_ds_write_command(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
    if (ctx->buffer_idx == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);
        return;
    }
    const uint16_t locks = isdu_access_locks(ctx);
    const int ret = iolink_ds_handle_ds_command(ds, ctx->buffer[0], locks);
    if (ret == 0) {
        ctx->response_len = 0U;
    }
    else {
        isdu_respond_error(ctx, isdu_ds_result_error(ret, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL));
    }
}

/** @brief DS 读 subindex 0：拼接记录（或旧的镜像）。 */
static void isdu_ds_read_record(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
    if (isdu_cap(ctx, IOLINK_CAP_DS_LEGACY_SUB0)) {
        /* 旧的整镜像备份读取：直接流式写入 ISDU 应答缓冲，不常驻镜像。 */
        const int n = iolink_ds_serialize(ds, ctx->response_buf, ctx->response_size);
        ctx->response_len = (n > 0) ? (size_t) n : 0U;
        return;
    }
    /* Table B.10/B.11（SDCI_TC_0107）：sub0 是拼接记录：
       DS_Command、State_Property、Data_Storage_Size、Parameter_Checksum 与
       Index_List。 */
    if (!iolink_ds_downloading(ds)) {
        iolink_ds_refresh(ds);
    }
    size_t pos = 0U;
    ctx->response_buf[pos++] = 0x00U; /* DS_Command 读取：未规定 */
    ctx->response_buf[pos++] = iolink_ds_state_property(ds);
    isdu_write_u32_be(ctx->response_buf, &pos, iolink_ds_size(ds));
    isdu_write_u32_be(ctx->response_buf, &pos, iolink_ds_checksum(ds));
    pos = isdu_append_ds_index_list(ds, ctx->response_buf, pos);
    ctx->response_len = (uint8_t) pos;
}

/** @brief DS 读 subindex 1-5。 */
static void isdu_ds_read_subindex(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds, uint8_t sub)
{
    switch (sub) {
        case 1U:
            /* DS_Command 读取返回未规定的值（Table B.10）。Test Spec
               0108 要求一个取值在 0..5 范围内的字节。 */
            ctx->response_buf[0] = 0x00U;
            ctx->response_len = 1U;
            break;
        case 2U:
            ctx->response_buf[0] = iolink_ds_state_property(ds);
            ctx->response_len = 1U;
            break;
        case 3U: {
            size_t idx = 0U;
            isdu_write_u32_be(ctx->response_buf, &idx, iolink_ds_size(ds));
            ctx->response_len = (uint8_t) idx;
            break;
        }
        case 4U: {
            /* Table B.10：Parameter_Checksum 反映当前参数集。
               直接的（非下载）参数写入不会重建 DS
               镜像，因此在报告校验和之前刷新它；否则在
               ParamDownloadStore 之前读到的值会与之后的值
               不一致。 */
            if (!iolink_ds_downloading(ds)) {
                iolink_ds_refresh(ds);
            }
            size_t idx = 0U;
            isdu_write_u32_be(ctx->response_buf, &idx, iolink_ds_checksum(ds));
            ctx->response_len = (uint8_t) idx;
            break;
        }
        case 5U: {
            /* Index_List（Table B.11）：[index(2) subindex(1)]*n + 0x0000。 */
            const size_t pos = isdu_append_ds_index_list(ds, ctx->response_buf, 0U);
            ctx->response_len = (uint8_t) pos;
            break;
        }
        default:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            break;
    }
}

static void handle_data_storage(iolink_isdu_ctx_t* ctx)
{
    iolink_ds_ctx_t* ds = ctx->ds_ctx;
    const uint8_t sub = ctx->header.subindex;

    if (ds == NULL) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        if (sub == 0U) {
            isdu_ds_write_sub0(ctx, ds);
        }
        else if (sub == 1U) {
            isdu_ds_write_command(ctx, ds);
        }
        else {
            /* Table B.10：subindex 02-05 为只读。 */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        }
    }
    else if (sub == 0U) {
        isdu_ds_read_record(ctx, ds);
    }
    else {
        isdu_ds_read_subindex(ctx, ds, sub);
    }

    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

#endif /* IOLINK_FEATURE_DATA_STORAGE */

#if IOLINK_FEATURE_DATA_STORAGE
/**
 * @brief 10.6.11 / SDCI_TC_0325/0326：参数写入在活动的块上传期间
 * 被锁定，参数读取在活动的块下载期间被锁定。
 * SystemCommand 与 DataStorageIndex 仍可访问。
 *
 * @return 当已产生否定应答时返回 true。
 */
bool isdu_reject_ds_access(iolink_isdu_ctx_t* ctx)
{
    const uint16_t index = ctx->header.index;
    if ((ctx->ds_ctx == NULL) || (index == IOLINK_IDX_SYSTEM_COMMAND) ||
        (index == IOLINK_IDX_DATA_STORAGE) || (index == IOLINK_IDX_DIRECT_PARAMETERS_1) ||
        (index == IOLINK_IDX_DIRECT_PARAMETERS_2)) {
        return false;
    }
    const iolink_ds_ctx_t* ds = ctx->ds_ctx;
    const bool blocked = ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) &&
                          (ds->state == IOLINK_DS_STATE_UPLOADING)) ||
                         ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_READ) &&
                          (ds->state == IOLINK_DS_STATE_DOWNLOADING));
    if (!blocked) {
        return false;
    }
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL);
    return true;
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */

/* -------------------------------------------------------------------------
 * 索引路由与标准分发
 * ------------------------------------------------------------------------- */

/** @brief SystemCommand 索引：只写的命令字节。
 *
 * 读请求由表项访问标志在 @ref isdu_entry_allowed 中拒绝，因此这里只
 * 处理写入。 */
static void isdu_handle_system_command_index(iolink_isdu_ctx_t* ctx)
{
    if (ctx->buffer_idx == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    handle_system_command(ctx, ctx->buffer[0]);
}

/**
 * @brief 表项级访问控制：方向、访问锁与子索引上界。
 *
 * @return 允许继续处理时返回 true；否则已产生否定应答。
 */
static bool isdu_entry_allowed(iolink_isdu_ctx_t* ctx, const iolink_od_entry_t* entry)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        if ((entry->access & IOLINK_OD_ACCESS_WRITE) == 0U) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
            return false;
        }
        /* B.2.4：Parameter（写）访问锁保护所有标记了 LOCK_PARAM 的对象。 */
        if (((entry->access & IOLINK_OD_ACCESS_LOCK_PARAM) != 0U) &&
            ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U)) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
            return false;
        }
    }
    else if ((entry->access & IOLINK_OD_ACCESS_READ) == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
        return false;
    }

    if (ctx->header.subindex > entry->max_subindex) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return false;
    }
    return true;
}

/** @brief 通过表项读/写一个参数值（标签对象或应用后端）。 */
static void isdu_serve_od_value(iolink_isdu_ctx_t* ctx, const iolink_od_entry_t* entry)
{
    iolink_od_ctx_t od;
    od.app = ctx->od;
    od.self = ctx->od_self;

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
#if IOLINK_FEATURE_DATA_STORAGE
        /* 块下载期间 DS 参数先暂存；ParamDownloadEnd 先对整份镜像做
           无副作用预校验，全部通过后再逐条写入并持久化（10.3.2）。
           这不是跨参数事务：NVM 写入失败仍可能留下已生效的前缀记录。 */
        if (((entry->storage & IOLINK_OD_STORAGE_DS) != 0U) && (ctx->ds_ctx != NULL) &&
            iolink_ds_downloading(ctx->ds_ctx)) {
            const iolink_result_t staged = iolink_ds_stage_param(
                ctx->ds_ctx, entry->index, ctx->header.subindex, ctx->buffer, ctx->buffer_idx);
            if (!iolink_result_succeeded(staged)) {
                isdu_respond_result(ctx, staged);
                return;
            }
            ctx->response_len = 0U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
#endif
        const iolink_result_t result =
            iolink_od_write_value(&od, entry, ctx->header.subindex, ctx->buffer, ctx->buffer_idx);
        if (!iolink_result_succeeded(result)) {
            isdu_respond_result(ctx, result);
            return;
        }
        ctx->response_len = 0U;
    }
    else {
        const iolink_result_t result = iolink_od_read_value(&od, entry, ctx->header.subindex,
                                                            ctx->response_buf, ctx->response_size);
        if (!iolink_result_succeeded(result)) {
            isdu_respond_result(ctx, result);
            return;
        }
        ctx->response_len = (uint8_t) result.len;
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/**
 * @brief 按表项的内建处理器 ID 分发标准对象。
 *
 * 需要设备上下文的特殊对象（SystemCommand、DPP1/2、DeviceStatus、
 * DataStorage 等）保留独立处理器；其余对象统一走 OD 值层。表项是唯一
 * 的索引→处理器映射，因此新增标准对象不需要改动本函数。
 */
void isdu_serve_entry(iolink_isdu_ctx_t* ctx, const iolink_od_entry_t* entry)
{
    if (!isdu_entry_allowed(ctx, entry)) {
        return;
    }

    switch ((iolink_od_handler_t) entry->handler) {
        case IOLINK_OD_HANDLER_DIRECT_PARAM_1:
        case IOLINK_OD_HANDLER_DIRECT_PARAM_2:
            handle_direct_parameters(ctx);
            return;

        case IOLINK_OD_HANDLER_SYSTEM_COMMAND:
            isdu_handle_system_command_index(ctx);
            return;

        case IOLINK_OD_HANDLER_DATA_STORAGE:
#if IOLINK_FEATURE_DATA_STORAGE
            handle_data_storage(ctx);
#else
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
#endif
            return;

        case IOLINK_OD_HANDLER_ACCESS_LOCKS:
            handle_access_locks(ctx);
            return;

        case IOLINK_OD_HANDLER_PROFILE: {
            const iolink_device_info_t* info = isdu_info(ctx);
            if (info == NULL) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
                return;
            }
            isdu_read_profile_characteristic(ctx, info);
            return;
        }

        case IOLINK_OD_HANDLER_IDENT_STRING: {
            const iolink_device_info_t* info = isdu_info(ctx);
            if (info == NULL) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
                return;
            }
            if (!isdu_read_identification_string(ctx, info, entry)) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            }
            return;
        }

        case IOLINK_OD_HANDLER_ERROR_COUNT:
            isdu_read_error_count(ctx);
            return;

        case IOLINK_OD_HANDLER_DEVICE_STATUS:
            isdu_read_device_status(ctx);
            return;

        case IOLINK_OD_HANDLER_DETAILED_STATUS:
#if IOLINK_FEATURE_EVENTS
            handle_detailed_device_status(ctx);
#else
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
#endif
            return;

        case IOLINK_OD_HANDLER_PD_INPUT:
            isdu_read_process_data(ctx, true);
            return;

        case IOLINK_OD_HANDLER_PD_OUTPUT:
            isdu_read_process_data(ctx, false);
            return;

        case IOLINK_OD_HANDLER_ERROR_STATS:
            handle_error_stats(ctx);
            return;

        case IOLINK_OD_HANDLER_NONE:
            isdu_serve_od_value(ctx, entry);
            return;

        default:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return;
    }
}
