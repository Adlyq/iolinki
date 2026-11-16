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
#include "iolinki/port.h"
#include <string.h>
#include <stdint.h>
#include "isdu_internal.h"

/**
 * @file isdu_cmd.c
 * @brief ISDU 标准索引、系统命令与直接参数页。
 *
 * 处理 B.2 固定索引（设备信息、过程数据、状态、统计）、
 * SystemCommand 及其子索引，以及 Direct Parameter 页。
 */

/* -------------------------------------------------------------------------
 * 强制索引
 * ------------------------------------------------------------------------- */

/** @brief B.2.5：0x000D 是 UIntegerT16 的 ArrayT（此处为单元素）。 */
void isdu_read_profile_characteristic(iolink_isdu_ctx_t* ctx, const iolink_device_info_t* info)
{
    if (info->profile_characteristic == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    if (ctx->header.subindex > 1U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }
    ctx->response_buf[0] = (uint8_t) (info->profile_characteristic >> 8);
    ctx->response_buf[1] = (uint8_t) (info->profile_characteristic & 0xFF);
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Table B.8/B.2.20：自复位以来的 2 字节错误计数。 */
void isdu_read_error_count(iolink_isdu_ctx_t* ctx)
{
    uint32_t total = 0U;
    if (ctx->dll_ctx != NULL) {
        iolink_dll_stats_t stats;
        iolink_dll_get_stats((const iolink_dll_main_t*) ctx->dll_ctx, &stats);
        uint64_t sum = (uint64_t) stats.crc_errors + (uint64_t) stats.framing_errors +
                       (uint64_t) stats.timeout_errors + (uint64_t) stats.timing_errors;
        total = (sum > 0xFFFFU) ? 0xFFFFU : (uint32_t) sum;
    }
    ctx->response_buf[0] = (uint8_t) (total >> 8);
    ctx->response_buf[1] = (uint8_t) (total & 0xFFU);
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief 将标识索引映射到其设备信息字符串（无则 NULL）。 */
static const char* isdu_identification_string(const iolink_device_info_t* info, uint16_t index)
{
    switch (index) {
        case IOLINK_IDX_VENDOR_NAME:
            return info->vendor_name;
        case IOLINK_IDX_VENDOR_TEXT:
            return info->vendor_text;
        case IOLINK_IDX_PRODUCT_NAME:
            return info->product_name;
        case IOLINK_IDX_PRODUCT_ID:
            return info->product_id;
        case IOLINK_IDX_PRODUCT_TEXT:
            return info->product_text;
        case IOLINK_IDX_SERIAL_NUMBER:
            return info->serial_number;
        case IOLINK_IDX_HARDWARE_REVISION:
            return info->hardware_revision;
        case IOLINK_IDX_FIRMWARE_REVISION:
            return info->firmware_revision;
        case IOLINK_IDX_PRODUCT_URI:
            return info->product_uri;
        default:
            return NULL;
    }
}

/**
 * @brief 将标识字符串读入应答缓冲区。
 *
 * @return 当对象没有字符串且未产生任何应答时
 *         返回 false（传输状态保持不变）。
 */
bool isdu_read_identification_string(iolink_isdu_ctx_t* ctx, const iolink_device_info_t* info,
                                     const iolink_od_entry_t* entry)
{
    const uint16_t index = entry->index;
    const char* str_data = isdu_identification_string(info, index);
    if (index == IOLINK_IDX_PRODUCT_URI && str_data == NULL) {
        /* Table B.8/B.2.19：可选，且以存在关联 Profile 为条件；
           没有 URI 时该对象不可用（Table C.1）。 */
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return true;
    }
    if (str_data == NULL) {
        return false;
    }

    /* Table B.8/B.2.8-B.2.19：StringT 的 fixedLength 由表项 block_len 给出
       （ProductURI 100、SerialNumber 16、其余 64）。 */
    size_t limit = (size_t) entry->block_len;
    size_t len_sz = strlen(str_data);
    if (len_sz > limit) {
        len_sz = limit;
    }
    if (len_sz > ctx->response_size) {
        len_sz = ctx->response_size;
    }
    (void) memcpy(ctx->response_buf, str_data, len_sz);
    ctx->response_len = (uint8_t) len_sz;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
    return true;
}

/** @brief Table B.8/B.2.23-B.2.24：最近一次有效的过程输入/输出数据。 */
void isdu_read_process_data(iolink_isdu_ctx_t* ctx, bool input)
{
    uint8_t len = 0U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_main_t* dll = (const iolink_dll_main_t*) ctx->dll_ctx;
        if (input) {
            /* B.2.23：该对象携带 PDin 通道的过程数据
               及其运行时长。PD 状态位（A.1.5）承载
               有效性；B.2.23 未定义其无效时的不同应答，
               因此按原样返回缓冲区（ST 参考实现行为）。 */
            len = dll->pd_in_len_current;
            if (len > IOLINK_PD_IN_MAX_SIZE) {
                len = (uint8_t) IOLINK_PD_IN_MAX_SIZE;
            }
            if (len > 0U) {
                const uint8_t* pdi =
                    &dll->shared.pd_in[dll->shared.pd_in_active][dll->shared.od_width];
                (void) memcpy(ctx->response_buf, pdi, len);
            }
        }
        else {
            /* B.2.24：PDout 通道同理。在收到第一帧 PDout 之前，
               返回按配置长度零初始化的缓冲区。 */
            len = dll->shared.pd_out_len_current;
            if (len > sizeof(dll->pd_out)) {
                len = (uint8_t) sizeof(dll->pd_out);
            }
            if (len > 0U) {
                (void) memcpy(ctx->response_buf, dll->pd_out, len);
            }
        }
    }
    ctx->response_len = len;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Table B.8/B.2.21：由诊断注册表派生的 DeviceStatus。 */
void isdu_read_device_status(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_EVENTS
    ctx->response_buf[0] = iolink_diag_device_status((iolink_events_ctx_t*) ctx->event_ctx);
#else
    ctx->response_buf[0] = 0U;
#endif
    ctx->response_len = 1U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/* -------------------------------------------------------------------------
 * SystemCommand 与数据存储上传辅助函数
 * ------------------------------------------------------------------------- */

/** @brief 当复位恢复了出厂参数集时清除 DS_UPLOAD_FLAG。 */
static void isdu_clear_ds_upload_req(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->ds_ctx != NULL) {
        iolink_ds_clear_upload((iolink_ds_ctx_t*) ctx->ds_ctx);
    }
#else
    (void) ctx;
#endif
}

/** @brief 当某命令在设备本地改变了数据存储参数集时，
 * 触发 DS_UPLOAD_REQ 事件（10.4.2）。 */
static void isdu_trigger_ds_upload_req(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_DATA_STORAGE
    if (ctx->ds_ctx != NULL) {
        /* 10.4.2：立即置位 DS_UPLOAD_FLAG（State_Property 位 7）。
           对应的通知由设备泵在
           当前 ISDU 应答被完整读取后发布，因此 Event 标志不会
           出现在 DS 命令应答中，也不会使主站在 ISDU 传输
           进行中插入诊断读取。 */
        iolink_ds_request_upload((iolink_ds_ctx_t*) ctx->ds_ctx);
        return;
    }
#endif
#if IOLINK_FEATURE_EVENTS
    if (ctx->event_ctx != NULL) {
        iolink_diag_raise_code((iolink_events_ctx_t*) ctx->event_ctx,
                               IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    }
#else
    (void) ctx;
#endif
}

/** @brief 执行 SystemCommand（复位、恢复出厂、数据存储命令）。 */
#if IOLINK_FEATURE_DATA_STORAGE
/** @brief 将 iolink_ds_* 返回码映射为 ISDU 错误（Table C.1）。 */
uint8_t isdu_ds_result_error(int ret, uint8_t fallback)
{
    switch (ret) {
        case -1:
            return IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL; /* 0x20 */
        case -2:
            return IOLINK_ISDU_ERROR_WRITE_PROTECTED; /* 0x23 */
        case -4:
            return IOLINK_ISDU_ERROR_PARAM_INCONSISTENT; /* 0x41 */
        case -5:
            return IOLINK_ISDU_ERROR_PARAM_SET_INVALID; /* 0x40 */
        case -6:
            return IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL; /* 0x36 */
        default:
            return fallback;
    }
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */

/** @brief Table 101：在复位命令中清除设备诊断/状态。 */
static void isdu_reset_diagnostics(iolink_isdu_ctx_t* ctx)
{
    if (ctx->dll_ctx != NULL) {
        iolink_dll_reset_diagnostics((iolink_dll_main_t*) ctx->dll_ctx);
    }
}

/** @brief Table 101：复位 ErrorCount 而不触碰事件引擎。 */
static void isdu_reset_error_counters(iolink_isdu_ctx_t* ctx)
{
    if (ctx->dll_ctx != NULL) {
        iolink_dll_reset_error_counters((iolink_dll_main_t*) ctx->dll_ctx);
    }
}

/** @brief 0x80-0x83：设备/应用复位、恢复出厂、back-to-box。 */
static void isdu_system_reset(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_DEVICE_RESET: /* 0x80 */
            /* Table 101：设备复位将 Diagnosis 与状态置为 "0"。 */
            isdu_reset_diagnostics(ctx);
            /* 置位复位标志，交由应用处理 */
            ctx->reset_pending = true;
            break;

        case IOLINK_CMD_APPLICATION_RESET: /* 0x81 */
            /* Table 101：应用复位不改变诊断信息。
               置位应用复位标志；参数集将发生变化，因此
               必须通过 DS_UPLOAD_REQ（10.4.2）提示主站重新上传。 */
            ctx->app_reset_pending = true;
            isdu_trigger_ds_upload_req(ctx);
            break;

        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82 */
            /* 将所有参数复位为出厂默认值。Table 177 要求
               DS_UPLOAD_FLAG = 0：主站保留其存储的参数集
               并重新下载，因此此处不请求上传。Table 101 的
               "Clear"：ErrorCount 被复位，且每个活动的 Error/Warning
               都以 "event disappears" 撤销，使主站可将其
               从诊断中移除。 */
            isdu_factory_reset(ctx);
            isdu_clear_ds_upload_req(ctx);
            isdu_reset_error_counters(ctx);
#if IOLINK_FEATURE_EVENTS
            if (ctx->event_ctx != NULL) {
                iolink_events_clear_diagnosis((iolink_events_ctx_t*) ctx->event_ctx);
            }
            else if (ctx->dll_ctx != NULL) {
                iolink_events_clear_diagnosis(&((iolink_dll_main_t*) ctx->dll_ctx)->shared.events);
            }
#endif
            break;

        case IOLINK_CMD_BACK_TO_BOX: /* 0x83 */
            /* 恢复交付值，然后在应答之后停止并禁用
               通信（10.7.5）。Table 178 还要求
               DS_UPLOAD_FLAG = 0；Table 101 将诊断与状态置为
               "0"。 */
            isdu_factory_reset(ctx);
            isdu_clear_ds_upload_req(ctx);
            if (ctx->dll_ctx != NULL) {
                iolink_dll_request_back_to_box((iolink_dll_main_t*) ctx->dll_ctx);
            }
            isdu_reset_diagnostics(ctx);
            break;

        default:
            break; /* 分发器只会将这四个命令路由到这里 */
    }
}

#if IOLINK_FEATURE_DATA_STORAGE
/**
 * @brief 0x01-0x06 数据存储命令。
 *
 * @return 当已产生错误应答时返回 true。
 */
static bool isdu_system_ds(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    if (ctx->ds_ctx == NULL) {
        return false; /* 无事可做：成功（空应答） */
    }
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;

    if (cmd == IOLINK_CMD_PARAM_BREAK) { /* 0x06 */
        /* 空闲状态下没有可中止的操作，因此这是一个意外命令
           （0324）。 */
        if (ds->state == IOLINK_DS_STATE_IDLE) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL);
            return true;
        }
        (void) iolink_ds_abort(ds);
        return false;
    }

    const uint16_t locks = isdu_access_locks(ctx);
    /* ParamDownloadStore 像 DownloadEnd 一样完成收尾，并额外触发
       DS_UPLOAD_REQ（10.4.2）。 */
    const bool store = (cmd == IOLINK_CMD_PARAM_DOWNLOAD_STORE);
    int ret = 0;
    if (store) {
        /* 0321/0322/0327：ParamDownloadStore 在 DSIdle_2 中有效。结束
           活动的下载，中止活动的上传，并始终请求
           后续的上传。 */
        if ((locks & IOLINK_LOCK_DS) != 0U) {
            ret = -2;
        }
        else if ((ds->state == IOLINK_DS_STATE_DOWNLOADING) ||
                 (ds->state == IOLINK_DS_STATE_DOWNLOAD_REQ)) {
            ret = iolink_ds_handle_param_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_END, locks);
        }
        else if ((ds->state == IOLINK_DS_STATE_UPLOADING) ||
                 (ds->state == IOLINK_DS_STATE_UPLOAD_REQ)) {
            (void) iolink_ds_abort(ds);
        }
    }
    else {
        ret = iolink_ds_handle_param_command(ds, cmd, locks);
    }

    if (ret != 0) {
        isdu_respond_error(ctx, isdu_ds_result_error(ret, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL));
        return true;
    }
    if (store) {
        isdu_trigger_ds_upload_req(ctx);
    }
    return false;
}
#endif /* IOLINK_FEATURE_DATA_STORAGE */

void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    IOLINK_LOG_INFO(ctx->log, "system command 0x%02X", (unsigned) cmd);
    /* 能力位关闭时，所有命令统一回 FUNC_NOTAVAIL。拒绝分支常驻，
     * 因此关位是运行时决定，不是构建期分裂。 */
    if (!isdu_cap_default_on(ctx, IOLINK_CAP_SYSTEM_COMMAND)) {
        (void) cmd;
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
        return;
    }

    switch (cmd) {
        case IOLINK_CMD_DEVICE_RESET:             /* 0x80 */
        case IOLINK_CMD_APPLICATION_RESET:        /* 0x81 */
        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82 */
        case IOLINK_CMD_BACK_TO_BOX:              /* 0x83 */
            isdu_system_reset(ctx, cmd);
            break;

            /* Table B.9：0x84-0x9F 为保留值（没有 SetCommunicationMode 命令）。 */

#if IOLINK_FEATURE_DATA_STORAGE
        /* 标准数据存储命令（0x01-0x06）。 */
        case IOLINK_CMD_PARAM_DOWNLOAD_START:
        case IOLINK_CMD_PARAM_DOWNLOAD_END:
        case IOLINK_CMD_PARAM_UPLOAD_START:
        case IOLINK_CMD_PARAM_UPLOAD_END:
        case IOLINK_CMD_PARAM_DOWNLOAD_STORE:
        case IOLINK_CMD_PARAM_BREAK: /* 0x06 */
            if (isdu_system_ds(ctx, cmd)) {
                return;
            }
            break;
#endif /* IOLINK_FEATURE_DATA_STORAGE */

        default:
            /* 不支持的命令值：FUNC_NOTAVAIL（Table C.1 / C.2.14）。 */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
            return;
    }

    /* 成功：空应答 */
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

void iolink_isdu_execute_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    if (ctx == NULL) {
        return;
    }
    handle_system_command(ctx, cmd);
    /* Direct Parameter page 1 上的 SystemCommand 不产生肯定或
       否定应答（7.3.5.3）；丢弃 ISDU 处理器暂存的任何内容。 */
    isdu_transport_reset(ctx);
}

/* -------------------------------------------------------------------------
 * 访问锁、详细状态、错误统计与直接参数
 * ------------------------------------------------------------------------- */

/** @brief 读或写 DeviceAccessLocks 参数（索引 0x000C）。 */
void handle_access_locks(iolink_isdu_ctx_t* ctx)
{
    /* B.2.4：仅允许通过 Subindex 0 访问 DeviceAccessLocks。 */
    if (ctx->header.subindex != 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_READ) {
        uint16_t locks = isdu_access_locks(ctx);
        ctx->response_buf[0] = (uint8_t) (locks >> 8);
        ctx->response_buf[1] = (uint8_t) (locks & 0xFF);
        ctx->response_len = 2U;
    }
    else {
        /* 写：更新访问锁。位 4..15 为保留位，应
           以 PAR_VALOUTOFRNG 拒绝（B.2.4 / SDCI_TC_0110）。 */
        if (ctx->buffer_idx >= 2U) {
            uint16_t new_locks = ((uint16_t) ctx->buffer[0] << 8) | ctx->buffer[1];
            if ((new_locks & (uint16_t) ~isdu_locks_implemented(ctx)) != 0U) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
                return;
            }
            iolink_device_info_ctx_t* info_ctx = isdu_info_ctx(ctx);
            if (info_ctx != NULL) {
                iolink_device_info_ctx_set_access_locks(info_ctx, new_locks);
            }
        }
        ctx->response_len = 0U;
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

#if IOLINK_FEATURE_EVENTS
/** @brief 处理 DetailedDeviceStatus（Table B.14）：3 字节条目的 ArrayT。
 *
 * 条目集合与 EventQualifier 都来自诊断注册表的单一派生点
 * （iolink_diag_dds_snapshot + iolink_event_qualifier），与 DeviceStatus
 * 使用同一条件过滤集。 */
void handle_detailed_device_status(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    if (ctx->event_ctx == NULL) {
        ctx->response_len = 0U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    iolink_events_ctx_t* event_ctx = (iolink_events_ctx_t*) ctx->event_ctx;

    /* 快照在临界区内完成；下面的格式化在锁外执行（应答缓冲区归主上下文
       所有）。条目集合 = “Event appears 已上线、disappears 尚未发出”的
       非 Notification 条件（B.2.22）。 */
    iolink_diag_dds_entry_t entries[IOLINK_EVENT_PENDING_MAX];
    const uint8_t n = iolink_diag_dds_snapshot(event_ctx, entries, IOLINK_EVENT_PENDING_MAX);

    /* B.2.22：符合规范的读取方式是 subindex 0，它返回完整的
       3 字节条目列表（"Subindex access is not supported"）。通过
       subindex 1..n 的元素访问作为设备扩展保留。 */
    const uint8_t sub = ctx->header.subindex;
    if ((sub == 0U) || (sub <= n)) {
        const uint8_t start = (sub == 0U) ? 0U : (uint8_t) (sub - 1U);
        const uint8_t end = (sub == 0U) ? n : (uint8_t) (start + 1U);
        uint8_t pos = 0U;
        for (uint8_t i = start; i < end; i++) {
            ctx->response_buf[pos++] = entries[i].qualifier;
            ctx->response_buf[pos++] = (uint8_t) (entries[i].code >> 8);
            ctx->response_buf[pos++] = (uint8_t) (entries[i].code & 0xFFU);
        }
        ctx->response_len = pos;
    }
    else {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

#endif /* IOLINK_FEATURE_EVENTS */

/** @brief 以大端序将 32 位值追加到缓冲区，并推进索引。 */
void isdu_write_u32_be(uint8_t* buf, size_t* idx, uint32_t value)
{
    buf[(*idx)++] = (uint8_t) ((value >> 24) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 16) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 8) & 0xFFU);
    buf[(*idx)++] = (uint8_t) (value & 0xFFU);
}

/** @brief 以四个大端 DLL 计数器形式返回厂商错误统计索引。 */
void handle_error_stats(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    if (ctx->header.subindex != 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    if (ctx->dll_ctx == NULL) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }

    iolink_dll_stats_t stats;
    iolink_dll_get_stats((const iolink_dll_main_t*) ctx->dll_ctx, &stats);

    size_t idx = 0U;
    isdu_write_u32_be(ctx->response_buf, &idx, stats.crc_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timeout_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.framing_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timing_errors);
    ctx->response_len = (uint8_t) idx;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief 返回 Direct Parameter page 2 存储（设备专用，地址 0x10-0x1F）。 */
static uint8_t* direct_param_page2(iolink_isdu_ctx_t* ctx)
{
    static uint8_t fallback_page2[16] = {0U};

    if ((ctx != NULL) && (ctx->direct_param_page2 != NULL)) {
        return (uint8_t*) ctx->direct_param_page2;
    }
    return fallback_page2;
}

/**
 * @brief 按 IO-Link V1.1.5 Figure B.5 编码过程数据长度（以字节计）。
 *
 * 不大于 2 字节时以位长度表示（BYTE=0）；更大时以字节表示（BYTE=1）。
 */
static uint8_t direct_param_encode_pd(uint8_t octets)
{
    if (octets == 0U) {
        return 0x00U;
    }
    if (octets <= 2U) {
        return (uint8_t) (octets * 8U); /* BYTE=0，长度以位计（8 或 16） */
    }
    return (uint8_t) (0x80U | (uint8_t) (octets - 1U)); /* BYTE=1，Length=octets-1 */
}

/**
 * @brief 构造 M-sequenceCapability 字节（Direct Parameter 地址 0x03，Figure B.3）。
 *
 * bit0 = 支持 ISDU，bits1-3 = OPERATE M-sequence 编码（Table A.10），
 * bits4-5 = PREOPERATE M-sequence 编码（Table A.8；此处 TYPE_0 = 0）。
 */
static uint8_t direct_param_mseq_capability(uint8_t m_seq_type, uint8_t od_width)
{
    uint8_t cap = 0x01U; /* 支持 ISDU */
    /* Table A.10 的 OPERATE 编码跟随实际 OD 宽度（1/2/8/32）。 */
    if (od_width == 0U) {
        od_width = iolink_mseq_default_od_width(m_seq_type);
    }
    const uint8_t operate_code = iolink_mseq_operate_code(m_seq_type, od_width);
    cap |= (uint8_t) ((operate_code & 0x07U) << 1);
    /* 本协议栈的 PREOPERATE M-sequence 为 TYPE_0（编码 0）-> bits 4-5 = 0。 */
    return cap;
}

/** @brief 从设备信息与 DLL 状态填充 16 字节的 Direct Parameter page 1。 */
static void build_direct_param_page1(iolink_isdu_ctx_t* ctx, uint8_t* page)
{
    const iolink_device_info_t* info = isdu_info(ctx);
    uint8_t pd_in = 0U;
    uint8_t pd_out = 0U;
    uint8_t m_seq = 0U;
    uint8_t od_width = 0U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_main_t* dll = (const iolink_dll_main_t*) ctx->dll_ctx;
        iolink_dll_get_pd_length(dll, &pd_in, &pd_out);
        m_seq = dll->shared.m_seq_type;
        od_width = dll->shared.od_width;
    }

    (void) memset(page, 0, 16);
    /* 0x01 MasterCycleTime 是主站在启动期间写入的值。 */
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_main_t* dll = (const iolink_dll_main_t*) ctx->dll_ctx;
        page[0x01] = dll->master_cycle_time;
    }
    {
        uint16_t min_cyc = (ctx->dll_ctx != NULL)
                               ? (uint16_t) iolink_dll_compute_min_cycle_time(
                                     (const iolink_dll_main_t*) ctx->dll_ctx)
                               : ((info != NULL) ? (uint16_t) info->min_cycle_time : 0U);
        /* 0x00 表示 "no MinCycleTime"；否则编码时间基准 | 乘数（Table B.3）。 */
        page[0x02] = (min_cyc == 0U) ? 0U : iolink_cycle_time_encode(min_cyc);
    }
    page[0x03] = direct_param_mseq_capability(m_seq, od_width); /* M-sequenceCapability */
    /* RevisionID 在 DPP1 0x04 中按一个八位组传输，MajorRev/MinorRev 各占半字节。 */
    page[0x04] = (info != NULL) ? info->revision_id : IOLINK_REVISION_ID_1_1;
    page[0x05] = direct_param_encode_pd(pd_in);  /* ProcessDataIn */
    page[0x06] = direct_param_encode_pd(pd_out); /* ProcessDataOut */
    if (info != NULL) {
        page[0x07] = (uint8_t) (info->vendor_id >> 8);            /* VendorID 高字节 */
        page[0x08] = (uint8_t) (info->vendor_id & 0xFFU);         /* VendorID 低字节 */
        page[0x09] = (uint8_t) ((info->device_id >> 16) & 0xFFU); /* DeviceID 第 2 字节 */
        page[0x0A] = (uint8_t) ((info->device_id >> 8) & 0xFFU);  /* DeviceID 第 1 字节 */
        page[0x0B] = (uint8_t) (info->device_id & 0xFFU);         /* DeviceID 第 0 字节 */
    }
    /* 0x0C-0x0E 保留（0）；0x0F SystemCommand（W）读出为 0。 */
}

/** @brief 当前调用是否处于可安全重建 DPP1 映像的主泵上下文。
 *
 * 数据 ISR 在 `iolink_dll_shared_t::in_isr` 置位期间运行；独立引擎
 * （未绑定 DLL）没有 ISR 读者。重建因此只发生在主泵侧，ISR 侧永远
 * 只读稳定副本。
 */
bool dp_page1_main_context(const iolink_isdu_ctx_t* ctx)
{
    const iolink_dll_main_t* dll = (const iolink_dll_main_t*) ctx->dll_ctx;
    return (dll == NULL) || (dll->shared.in_isr == 0U);
}

/** @brief 重建 DPP1 映像并按 seqlock 发布（主泵单写者）。
 *
 * 先在本地 16 字节缓冲上完成构建，再在序号奇数窗口内覆盖共享映像，
 * 最后置偶数表示稳定。ISR 只会看到完整的旧映像或完整的新映像；
 * 整个路径不使用动态内存。
 */
static void dp_page1_refresh(iolink_isdu_ctx_t* ctx)
{
    uint8_t page[16];

    build_direct_param_page1(ctx, page);
    ctx->dp_page1_seq = (uint8_t) (ctx->dp_page1_seq | 1U);
    IOLINK_FENCE(); /* 先标记"更新中"，再写映像 */
    (void) memcpy(ctx->dp_page1, page, sizeof(page));
    ctx->dp_page1_src = ctx->dll_ctx;
    ctx->dp_page1_info = (const void*) ctx->device_info;
    IOLINK_FENCE(); /* 映像写完后才发布为稳定 */
    ctx->dp_page1_seq = (uint8_t) (ctx->dp_page1_seq + 1U);
}

/** @brief 主泵侧：映像失效时重建。
 *
 * 失效标记（@ref iolink_isdu_dp_page1_invalidate 、初始化）总是触发重建。
 * MasterCycleTime 是主站运行期唯一可写的 DPP1 输入，这里额外比较一次
 * 该字节，使主泵读路径不依赖每个写入点都记得加失效标记；其余输入
 * （PD 长度、M-sequence 配置、设备标识）由失效标记覆盖。
 */
void dp_page1_ensure(iolink_isdu_ctx_t* ctx)
{
    const iolink_dll_main_t* dll = (const iolink_dll_main_t*) ctx->dll_ctx;
    bool stale = ((ctx->dp_page1_seq & 1U) != 0U);
    if ((ctx->dp_page1_src != ctx->dll_ctx) ||
        (ctx->dp_page1_info != (const void*) ctx->device_info)) {
        stale = true; /* 绑定/重绑上下文：映像必须按新上下文重建 */
    }
    if ((dll != NULL) && (ctx->dp_page1[0x01] != dll->master_cycle_time)) {
        stale = true;
    }
    if (stale) {
        dp_page1_refresh(ctx);
    }
}

void iolink_isdu_dp_page1_invalidate(iolink_isdu_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
    /* 主泵上下文立即重建，保证下一次 ISR 页读看到一致的新映像；
     * ISR 侧（理论上不会调用本函数）只标记失效，绝不重建整页。 */
    if (dp_page1_main_context(ctx)) {
        dp_page1_refresh(ctx);
    }
    else {
        ctx->dp_page1_seq = (uint8_t) (ctx->dp_page1_seq | 1U);
    }
}

uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr)
{
    if ((ctx == NULL) || (addr >= sizeof(ctx->dp_page1))) {
        return 0U;
    }

    /* 主泵侧按需刷新映像；ISR 侧绝不重建整页。 */
    if (dp_page1_main_context(ctx)) {
        dp_page1_ensure(ctx);
    }

    /* seqlock 读：读序号 → 取字节 → 再读序号校验；最多 3 次。
     * 映像字节通过 volatile 视图读取，不会被重排到序号检查之外。 */
    const volatile uint8_t* image = ctx->dp_page1;
    for (uint8_t attempt = 0U; attempt < 3U; attempt++) {
        const uint8_t seq = ctx->dp_page1_seq;
        if ((seq & 1U) != 0U) {
            continue; /* 主泵正在更新，或映像已失效 */
        }
        const uint8_t value = image[addr];
        if (ctx->dp_page1_seq == seq) {
            return value;
        }
    }
    return 0U;
}

bool iolink_isdu_dp_page1_read_range(iolink_isdu_ctx_t* ctx, uint8_t addr, uint8_t* out,
                                     uint8_t len)
{
    if ((ctx == NULL) || (out == NULL)) {
        return false;
    }
    if (len == 0U) {
        return true;
    }
    /* 主泵侧按需刷新；ISR 侧绝不重建整页。 */
    if (dp_page1_main_context(ctx)) {
        dp_page1_ensure(ctx);
    }

    const volatile uint8_t* image = ctx->dp_page1;
    for (uint8_t attempt = 0U; attempt < 3U; attempt++) {
        const uint8_t seq = ctx->dp_page1_seq;
        if ((seq & 1U) != 0U) {
            continue; /* 主泵正在更新，或映像已失效 */
        }
        for (uint8_t i = 0U; i < len; i++) {
            const uint16_t a = (uint16_t) addr + (uint16_t) i;
            out[i] = (a < (uint16_t) sizeof(ctx->dp_page1)) ? image[a] : 0U;
        }
        if (ctx->dp_page1_seq == seq) {
            return true; /* 整段来自同一代 */
        }
    }
    for (uint8_t i = 0U; i < len; i++) {
        out[i] = 0U;
    }
    return false;
}

/** @brief 读/写 Direct Parameter page 1（只读）与 page 2（设备专用）。 */
void handle_direct_parameters(iolink_isdu_ctx_t* ctx)
{
    bool page2 = (ctx->header.index == IOLINK_IDX_DIRECT_PARAMETERS_2);
    uint8_t sub = ctx->header.subindex;
    uint8_t* page2_storage = direct_param_page2(ctx);

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        if (!page2) {
            /* Page 1 为只读。 */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        }
        else if ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U) {
            /* Table B.2.4：Parameter 访问锁禁止对 page 2 的写。 */
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        }
        else if (sub == 0U) {
            size_t n = ctx->buffer_idx;
            if (n > 16U) {
                n = 16U;
            }
            (void) memcpy(page2_storage, ctx->buffer, n);
            ctx->response_len = 0U;
        }
        else if ((sub <= 16U) && (ctx->buffer_idx >= 1U)) {
            page2_storage[sub - 1U] = ctx->buffer[0];
            ctx->response_len = 0U;
        }
        else {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        }
    }
    else {
        uint8_t page[16];
        if (page2) {
            (void) memcpy(page, page2_storage, sizeof(page));
        }
        else {
            /* 主泵路径直接用同一个构建器（单一口径）；RAM 映像只服务
             * 数据 ISR 的页读快路径。 */
            build_direct_param_page1(ctx, page);
        }

        if (sub == 0U) {
            (void) memcpy(ctx->response_buf, page, sizeof(page));
            ctx->response_len = sizeof(page);
        }
        else if (sub <= 16U) {
            ctx->response_buf[0] = page[sub - 1U]; /* 未实现的字节读出为 0 */
            ctx->response_len = 1U;
        }
        else {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        }
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}
