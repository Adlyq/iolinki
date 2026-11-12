/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/protocol.h"
#include "iolinki/isdu.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <string.h>
#include <stdint.h>

/**
 * @file isdu.c
 * @brief ISDU 服务引擎：非循环参数读/写处理。
 * @ingroup iolinki_isdu
 *
 * 实现 ISDU 分段状态机（字节收集、头部
 * 解析、序列跟踪与应答分段），并将标准索引与
 * 系统命令分发给设备参数、设备信息、
 * 事件、直接参数与数据存储处理器。
 *
 * ISDU 头部（2 或 3 字节）：
 * [7:4] Service（读/写）
 * [3:0] Length（值小于 15 时）
 * [ Index（16 位）]
 * [ Subindex（8 位）]
 */

/* -------------------------------------------------------------------------
 * 文件导览
 *
 *  - 传输、解析与共享辅助函数：chkpdu ... iolink_isdu_od_read
 *    （公开：iolink_isdu_init、iolink_isdu_od_write、iolink_isdu_od_read）
 *  - 共享应答辅助函数：isdu_respond_error
 *  - 强制索引：isdu_check_mandatory_write ... handle_mandatory_indices
 *  - SystemCommand 与数据存储上传辅助函数：isdu_clear_ds_upload_req ...
 *    （公开：iolink_isdu_execute_system_command）
 *  - 访问锁、详细状态、错误统计与直接参数：
 *    handle_access_locks ... handle_direct_parameters
 *  - 数据存储服务：isdu_append_ds_index_list ... isdu_reject_ds_access
 *  - 索引路由与标准分发：isdu_handle_vendor_index ...
 *  - 末尾公开 API：iolink_isdu_process、iolink_isdu_get_response_byte、
 *    iolink_isdu_snapshot_response
 * ------------------------------------------------------------------------- */

/* ... 头文件包含 ... */

/* 结构体已移至头文件 iolink_isdu_ctx_t */

/* -------------------------------------------------------------------------
 * 传输、解析与共享辅助函数
 * ------------------------------------------------------------------------- */

/** @brief CHKPDU：所有 ISDU 字节的异或，校验字节按 0 读取（A.5.6）。 */
static uint8_t chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

void iolink_isdu_init(iolink_isdu_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_isdu_ctx_t))) {
        return;
    }
    ctx->state = ISDU_STATE_IDLE;
    ctx->resp_sel = 0U;
    ctx->resp_pub = 0U;
    ctx->resp_buf = ctx->resp_store[0];
}

/** @brief 通过绑定的 params 上下文读取参数；未绑定则返回 -1。 */
static int isdu_params_get(const iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           uint8_t* buffer, size_t max_len)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_get(ctx->params_ctx, index, subindex, buffer, max_len);
    }
    (void) index;
    (void) subindex;
    (void) buffer;
    (void) max_len;
    return -1;
}

/** @brief 通过绑定的 params 上下文写入参数；未绑定则返回 -1。 */
static int isdu_params_set(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           const uint8_t* data, size_t len, bool persist)
{
#if IOLINK_FEATURE_DATA_STORAGE
    /* 块下载期间，写入会被缓冲，以便在
       ParamDownloadEnd（10.3.2）时原子提交，而非立即生效。 */
    if ((ctx != NULL) && (ctx->ds_ctx != NULL) &&
        iolink_ds_downloading((const iolink_ds_ctx_t*) ctx->ds_ctx)) {
        return iolink_ds_stage_param((iolink_ds_ctx_t*) ctx->ds_ctx, index, subindex, data, len);
    }
#endif
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_set(ctx->params_ctx, index, subindex, data, len, persist);
    }
    (void) index;
    (void) subindex;
    (void) data;
    (void) len;
    (void) persist;
    return -1;
}

#if IOLINK_FEATURE_SYSTEM_COMMAND
/** @brief 对绑定的参数上下文执行恢复出厂设置；未绑定则不做任何事。 */
static void isdu_params_factory_reset(iolink_isdu_ctx_t* ctx)
{
    /* 绑定时复位设备自身的参数上下文（如 device.c
       通过 ctx->params_ctx 所做的那样）；未绑定时没有全局回退。
       必须使用绑定的上下文，否则每个设备的应用标签会在
       Restore Factory Settings 命令之后仍然存在。 */
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        iolink_params_ctx_factory_reset(ctx->params_ctx);
    }
}
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */

/** @brief 绑定到该 ISDU 上下文的设备信息上下文（若有）。 */
static iolink_device_info_ctx_t* isdu_info_ctx(iolink_isdu_ctx_t* ctx)
{
    return ((ctx != NULL) && (ctx->params_ctx != NULL)) ? ctx->params_ctx->device_info : NULL;
}

/** @brief 该 ISDU 上下文的有效标识。 */
static const iolink_device_info_t* isdu_info(const iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_ctx_t* info_ctx =
        ((ctx != NULL) && (ctx->params_ctx != NULL)) ? ctx->params_ctx->device_info : NULL;
    return (info_ctx != NULL) ? iolink_device_info_ctx_get(info_ctx) : NULL;
}

/** @brief 该 ISDU 上下文的有效访问锁。 */
static uint16_t isdu_access_locks(const iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_ctx_t* info_ctx =
        ((ctx != NULL) && (ctx->params_ctx != NULL)) ? ctx->params_ctx->device_info : NULL;
    return (info_ctx != NULL) ? iolink_device_info_ctx_get_access_locks(info_ctx) : 0U;
}

static void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error);

/** @brief 将参数管理器错误映射为 ISDU 否定应答。 */
static void isdu_respond_param_error(iolink_isdu_ctx_t* ctx, int ret)
{
    uint8_t error = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    if (ret == -2) {
        error = IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN;
    }
    else if (ret == -3) {
        error = IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN;
    }
    isdu_respond_error(ctx, error);
}

/* handle_standard_commands() 定义在下方；传输层在收齐一个
   完整的 ISDU 请求后执行它。 */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx);

/** @brief 将 ISDU 请求传输层复位到空闲状态。 */
static void isdu_transport_reset(iolink_isdu_ctx_t* ctx)
{
    ctx->response_negative = false;
    ctx->state = ISDU_STATE_IDLE;
    ctx->req_idx = 0U;
    ctx->req_total = 0U;
    ctx->resp_idx = 0U;
    ctx->resp_total = 0U;
    ctx->buffer_idx = 0U;
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->flowctrl_seen = false;
    ctx->repeat_pending = false;
    ctx->error_code = IOLINK_ISDU_ERROR_NONE;
    ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
    ctx->header.length = 0U;
    ctx->header.index = 0U;
    ctx->header.subindex = 0U;
}

/** @brief 由最先收到的字节声明的 ISDU 总字节数（A.5.3/A.14）。
 *
 * ExtLength 字节尚未到达时返回 0。 */
static size_t isdu_declared_total(const uint8_t* buf, size_t have)
{
    if (have == 0U) {
        return 0U;
    }
    const uint8_t len = (uint8_t) (buf[0] & 0x0FU);
    if (len == 0U) {
        return 1U; /* No Service / Busy：单字节 */
    }
    if (len == 1U) {
        if (have < 2U) {
            return 0U; /* ExtLength 字节尚未收到 */
        }
        const uint8_t ext = buf[1];
        if ((ext >= 17U) && (ext <= 238U)) {
            return ext;
        }
        return 0U; /* 保留 / 无效 */
    }
    return len;
}

/**
 * @brief 解码完整的请求缓冲区并执行所寻址的服务。
 *
 * 从原始字节填充 ctx->header 与 ctx->buffer，然后通过
 * handle_standard_commands() 分发；处理器会留下 2 字节的否定标记
 * （response_buf[0]==0x80）或 response_buf/response_len 中的肯定载荷。
 */
/** @brief 将服务半字节解码为读/写头部类型。 */
static bool isdu_decode_service(iolink_isdu_ctx_t* ctx, uint8_t service)
{
    if ((service >= 1U) && (service <= 3U)) {
        ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_WRITE;
        return true;
    }
    if ((service >= 9U) && (service <= 11U)) {
        ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
        return true;
    }
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    return false;
}

/**
 * @brief 解析请求的 index/subindex 字段。
 *
 * 索引格式见 Table A.12/A.15。服务 0x1/0x9 = 8 位索引，0x2/0xA =
 * 8 位索引 + subindex，0x3/0xB = 16 位索引 + subindex。
 *
 * @return 当请求被截断并已产生否定应答时
 *         返回 false。
 */
static bool isdu_parse_index(iolink_isdu_ctx_t* ctx, const uint8_t* buf, size_t* pos,
                             size_t payload_end, uint8_t service)
{
    if ((buf[0] & 0x0FU) == 1U) {
        (*pos)++; /* 跳过 ExtLength 字节 */
    }

    const bool two_index = (service == 3U) || (service == 11U);
    const bool has_sub = (service == 2U) || (service == 3U) || (service == 10U) || (service == 11U);

    if (two_index) {
        if ((*pos + 1U) >= payload_end) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return false;
        }
        ctx->header.index = (uint16_t) ((uint16_t) buf[*pos] << 8);
        (*pos)++;
        ctx->header.index |= buf[(*pos)++];
    }
    else {
        if (*pos >= payload_end) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return false;
        }
        ctx->header.index = buf[(*pos)++];
    }

    if (has_sub) {
        if (*pos >= payload_end) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            return false;
        }
        ctx->header.subindex = buf[(*pos)++];
    }
    return true;
}

/** @brief 将数据载荷（CHKPDU 之前的全部内容）复制到缓冲区。 */
static void isdu_copy_payload(iolink_isdu_ctx_t* ctx, const uint8_t* buf, size_t pos, size_t total)
{
    if (pos < total) {
        const size_t data_end = total - 1U; /* 排除 CHKPDU */
        if (data_end > pos) {
            size_t n = data_end - pos;
            if (n > sizeof(ctx->buffer)) {
                n = sizeof(ctx->buffer);
            }
            (void) memcpy(ctx->buffer, &buf[pos], n);
            ctx->buffer_idx = n;
        }
    }
    ctx->header.length = (uint8_t) ctx->buffer_idx;
}

static void isdu_execute_request(iolink_isdu_ctx_t* ctx, size_t total)
{
    ctx->response_negative = false;
    const uint8_t* buf = ctx->req_buf;
    const uint8_t service = (uint8_t) (buf[0] >> 4);
    size_t pos = 1U;
    const size_t payload_end = total - 1U; /* CHKPDU 无法提供 index/subindex。 */

    ctx->buffer_idx = 0U;
    ctx->response_idx = 0U;
    ctx->header.subindex = 0U;
    ctx->header.length = 0U;

    if (!isdu_decode_service(ctx, service)) {
        return;
    }
    if (!isdu_parse_index(ctx, buf, &pos, payload_end, service)) {
        return;
    }
    isdu_copy_payload(ctx, buf, pos, total);

    IOLINK_LOG_DEBUG(&ctx->log, "isdu req idx=0x%04X sub=%u len=%u write=%u",
                     (unsigned) ctx->header.index, (unsigned) ctx->header.subindex,
                     (unsigned) ctx->header.length,
                     (unsigned) (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE));

    /* 已解析：执行由 iolink_isdu_process() 完成，从而保持
       传输层与应用分发可分离（且可测试）。 */
    ctx->state = ISDU_STATE_SERVICE_EXECUTE;
}

/** @brief 将处理器结果组帧到 ctx->resp_buf（肯定或否定，A.5/C.1）。 */
static void isdu_frame_response(iolink_isdu_ctx_t* ctx)
{
    const bool negative = ctx->response_negative;
    const bool write = (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE);
    size_t pos = 0U;

    if (negative) {
        ctx->resp_buf[pos++] = write ? 0x44U : 0xC4U; /* 写/读应答（-），长度 4 */
        ctx->resp_buf[pos++] = 0x80U;                 /* ErrorCode（APP_DEV） */
        ctx->resp_buf[pos++] = ctx->response_buf[1];  /* AdditionalCode */
    }
    else if (write) {
        ctx->resp_buf[pos++] = 0x52U; /* 写应答（+） */
    }
    else {
        const size_t data_len = ctx->response_len;
        const size_t direct_total = data_len + 2U; /* I-Service + data + CHKPDU */
        if (direct_total <= 15U) {
            ctx->resp_buf[pos++] = (uint8_t) (0xD0U | (uint8_t) direct_total);
            if (data_len > 0U) {
                (void) memcpy(&ctx->resp_buf[pos], ctx->response_buf, data_len);
                pos += data_len;
            }
        }
        else {
            /* A.5.3 / Figure A.19：I-Service+Length 字节、ExtLength、data、CHKPDU = data + 3。 */
            const size_t total = data_len + 3U;
            ctx->resp_buf[pos++] = 0xD1U;
            ctx->resp_buf[pos++] = (uint8_t) total;
            if (data_len > 0U) {
                (void) memcpy(&ctx->resp_buf[pos], ctx->response_buf, data_len);
                pos += data_len;
            }
        }
    }

    ctx->resp_buf[pos] = 0x00U;
    ctx->resp_buf[pos] = chkpdu(ctx->resp_buf, pos + 1U);
    pos++;
    ctx->resp_total = pos;
    ctx->resp_idx = 0U;
}

/** @brief 为尚无法应答的读 START 构造 Busy 应答字节（0x01）。 */
static void isdu_emit_busy(iolink_isdu_ctx_t* ctx)
{
    ctx->resp_buf[0] = 0x01U;
    ctx->resp_total = 1U;
    ctx->resp_idx = 0U;
}

void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od,
                          uint8_t od_len)
{
    if (ctx == NULL) {
        return;
    }

    /* Table 52：ABORT（0x1F）拒绝请求数据，并将传输层
       复位到空闲，与方向无关。IDLE/IDLE2 及保留值
       0x13..0x1E 不是写操作；它们由读路径处理。 */
    if (flowctrl == IOLINK_FLOWCTRL_ABORT) {
        isdu_transport_reset(ctx);
        return;
    }
    if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        return;
    }

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        isdu_transport_reset(ctx);
        /* 仅在上一应答已发布时才轮换组帧应答缓冲区；
         * 否则替换尚未发布的构建结果。因此 ISR 可能仍在
         * 服务的缓冲区永远不会被写入。 */
        if (ctx->resp_sel == ctx->resp_pub) {
            ctx->resp_sel = (uint8_t) (ctx->resp_sel ^ 1U);
            ctx->resp_buf = ctx->resp_store[ctx->resp_sel];
        }
        ctx->flowctrl_seen = true;
        ctx->last_flowctrl = flowctrl;
        ctx->state = ISDU_STATE_HEADER_INITIAL;
        ctx->req_total = 0U;
    }
    else {
        /* COUNT（0x00..0x0F）：必须跟在 START 之后。重复的 FlowCTRL 表示重发
           上一报文，其载荷被忽略（7.3.6.2）。 */
        if (!ctx->flowctrl_seen || (ctx->state == ISDU_STATE_IDLE)) {
            ctx->error_code = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
            isdu_transport_reset(ctx);
            return;
        }
        if (flowctrl == ctx->last_flowctrl) {
            ctx->repeat_pending = true;
            return;
        }
        const uint8_t expected = (uint8_t) ((ctx->last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        if (flowctrl != expected) {
            /* 结构违例：丢弃请求，进入空闲，下一次读
               应答 No Service（0x00）（Table 54 T13）。 */
            ctx->error_code = IOLINK_ISDU_ERROR_SEGMENTATION;
            isdu_transport_reset(ctx);
            return;
        }
        ctx->last_flowctrl = flowctrl;
        ctx->repeat_pending = false;
    }

    if (ctx->repeat_pending) {
        ctx->repeat_pending = false;
        return;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->req_idx < sizeof(ctx->req_buf)) {
            ctx->req_buf[ctx->req_idx++] = od[i];
        }
        if (ctx->req_total == 0U) {
            ctx->req_total = isdu_declared_total(ctx->req_buf, ctx->req_idx);
        }
        if ((ctx->req_total > 0U) && (ctx->req_idx >= ctx->req_total)) {
            break;
        }
    }

    if ((ctx->req_total > 0U) && (ctx->req_idx >= ctx->req_total)) {
        /* A.5.6：当所有 ISDU 字节的异或为零时 CHKPDU 有效。 */
        uint8_t x = 0U;
        for (size_t i = 0U; i < ctx->req_total; i++) {
            x ^= ctx->req_buf[i];
        }
        if (x != 0U) {
            /* A.5.6 / Table 84（SDCI_TC_0065）：CHKPDU 无效会使
               请求无效。丢弃它，返回空闲，并对下一次
               Read 请求以 I-Service 0（"No Service"）应答。不要为
               格式错误的请求合成否定 ISDU 应答。 */
            ctx->error_code = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
            isdu_transport_reset(ctx);
            return;
        }
        isdu_execute_request(ctx, ctx->req_total);
        if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            isdu_frame_response(ctx);
        }
    }
}

void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len)
{
    if (ctx == NULL) {
        return;
    }
    for (uint8_t i = 0U; i < od_len; i++) {
        od_out[i] = 0U;
    }

    if (flowctrl == IOLINK_FLOWCTRL_ABORT) {
        isdu_transport_reset(ctx);
        return;
    }
    if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2)) {
        isdu_transport_reset(ctx);
        return;
    }

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        /* 应答读取开始。若某服务正在执行且
           应用尚未应答，则应答 Busy（0x01）（Table A.14）。 */
        if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
            isdu_emit_busy(ctx);
        }
        else if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            /* 重启的读：从第一个应答字节重新开始 */
            ctx->resp_idx = 0U;
        }
        else {
            /* 空闲 / 无待处理服务：No Service（0x00）。 */
            ctx->resp_buf[0] = 0x00U;
            ctx->resp_total = 1U;
            ctx->resp_idx = 0U;
        }
        ctx->last_flowctrl = flowctrl;
        ctx->flowctrl_seen = true;
    }
    else if (flowctrl < IOLINK_FLOWCTRL_START) {
        /* 读操作上的 COUNT 按顺序继续应答。 */
        if (!ctx->flowctrl_seen) {
            return;
        }
        ctx->last_flowctrl = flowctrl;
    }
    else {
        /* 保留值不是有效的读 FlowCTRL。 */
        return;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->resp_idx < ctx->resp_total) {
            od_out[i] = ctx->resp_buf[ctx->resp_idx++];
        }
        else {
            od_out[i] = 0x00U;
        }
    }

    if ((ctx->state == ISDU_STATE_RESPONSE_READY) && (ctx->resp_idx >= ctx->resp_total)) {
        isdu_transport_reset(ctx);
    }
}

/* -------------------------------------------------------------------------
 * 共享应答辅助函数
 * ------------------------------------------------------------------------- */

/** @brief 以两字节否定应答终止 ISDU 服务。 */
static void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error)
{
    IOLINK_LOG_DEBUG(&ctx->log, "isdu error 0x%02X", (unsigned) error);
    ctx->response_negative = true;
    ctx->response_buf[0] = 0x80U;
    ctx->response_buf[1] = error;
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/* -------------------------------------------------------------------------
 * 强制索引
 * ------------------------------------------------------------------------- */

/**
 * @brief 拒绝对只读强制对象的写操作（WRITE）。
 *
 * @return 当已产生否定应答时返回 true。
 */
static bool isdu_check_mandatory_write(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        return false;
    }
    const uint16_t index = ctx->header.index;
    const bool writable_tag = (index == IOLINK_IDX_APPLICATION_TAG) ||
                              (index == IOLINK_IDX_FUNCTION_TAG) ||
                              (index == IOLINK_IDX_LOCATION_TAG);
    if (writable_tag) {
        /* Table B.2.4：置位 Parameter（写）访问锁后，除
           DeviceAccessLocks 外的每个读/写参数都受写保护。 */
        if ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U) {
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
            return true;
        }
        return false;
    }

    switch (index) {
        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
        case IOLINK_IDX_VENDOR_NAME:
        case IOLINK_IDX_VENDOR_TEXT:
        case IOLINK_IDX_PRODUCT_NAME:
        case IOLINK_IDX_PRODUCT_ID:
        case IOLINK_IDX_PRODUCT_TEXT:
        case IOLINK_IDX_SERIAL_NUMBER:
        case IOLINK_IDX_HARDWARE_REVISION:
        case IOLINK_IDX_FIRMWARE_REVISION:
        case IOLINK_IDX_PRODUCT_URI:
        case IOLINK_IDX_ERROR_COUNT:
        case IOLINK_IDX_DEVICE_STATUS:
        case IOLINK_IDX_PROCESS_DATA_INPUT:
        case IOLINK_IDX_PROCESS_DATA_OUTPUT:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
            return true;
        default:
            return false; /* 未知索引：由分发器返回 IDX_NOTAVAIL */
    }
}

/** @brief B.2.5：0x000D 是 UIntegerT16 的 ArrayT（此处为单元素）。 */
static void isdu_read_profile_characteristic(iolink_isdu_ctx_t* ctx,
                                             const iolink_device_info_t* info)
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
static void isdu_read_error_count(iolink_isdu_ctx_t* ctx)
{
    uint32_t total = 0U;
    if (ctx->dll_ctx != NULL) {
        iolink_dll_stats_t stats;
        iolink_dll_get_stats((const iolink_dll_ctx_t*) ctx->dll_ctx, &stats);
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
static bool isdu_read_identification_string(iolink_isdu_ctx_t* ctx,
                                            const iolink_device_info_t* info, uint16_t index)
{
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

    /* Table B.8/B.2.8-B.2.19：按对象定义的 StringT fixedLength 限制应答长度。
       ProductURI 最多允许 100 字节；SerialNumber 为 16。 */
    size_t limit = 64U;
    if (index == IOLINK_IDX_SERIAL_NUMBER) {
        limit = 16U;
    }
    else if (index == IOLINK_IDX_PRODUCT_URI) {
        limit = 100U;
    }
    size_t len_sz = strlen(str_data);
    if (len_sz > limit) {
        len_sz = limit;
    }
    if (len_sz > sizeof(ctx->response_buf)) {
        len_sz = sizeof(ctx->response_buf);
    }
    (void) memcpy(ctx->response_buf, str_data, len_sz);
    ctx->response_len = (uint8_t) len_sz;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
    return true;
}

/** @brief 从参数集中读取应用/功能/位置标签。 */
static void isdu_tag_read(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    const int res =
        isdu_params_get(ctx, index, 0U, ctx->response_buf, (size_t) IOLINK_ISDU_BUFFER_SIZE);
    if (res >= 0) {
        ctx->response_len = (uint8_t) res;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
    /* 失败时保持状态不变；随后的传输复位会丢弃
       该请求（与原实现的直落行为相同）。 */
}

/** @brief 将应用/功能/位置标签写入参数集。 */
static void isdu_tag_write(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    if (index == IOLINK_IDX_APPLICATION_TAG) {
        if (ctx->buffer_idx > 32U) {
            isdu_respond_param_error(ctx, -2);
            return;
        }
        const int ret = isdu_params_set(ctx, index, 0U, ctx->buffer, ctx->buffer_idx, true);
        if (ret == 0) {
            ctx->response_len = 0U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
        isdu_respond_param_error(ctx, ret);
        return;
    }

    if (isdu_params_set(ctx, index, 0U, ctx->buffer, ctx->buffer_idx, true) == 0) {
        ctx->response_len = 0U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }
    /* 持久化存储拒绝了该服务：以否定应答
       终止，而不是让主站持续轮询 Busy。 */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/** @brief 处理可写标识标签的读或写。 */
static void isdu_tag_service(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        isdu_tag_write(ctx, index);
    }
    else {
        isdu_tag_read(ctx, index);
    }
}

/** @brief Table B.8/B.2.23-B.2.24：最近一次有效的过程输入/输出数据。 */
static void isdu_read_process_data(iolink_isdu_ctx_t* ctx, bool input)
{
    uint8_t len = 0U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
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
                const uint8_t* pdi = &dll->pd_in[dll->pd_in_active][dll->od_width];
                (void) memcpy(ctx->response_buf, pdi, len);
            }
        }
        else {
            /* B.2.24：PDout 通道同理。在收到第一帧 PDout 之前，
               返回按配置长度零初始化的缓冲区。 */
            len = dll->pd_out_len_current;
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

/** @brief Table B.8/B.2.21：由事件严重程度得到 DeviceStatus。 */
static void isdu_read_device_status(iolink_isdu_ctx_t* ctx)
{
#if IOLINK_FEATURE_EVENTS
    ctx->response_buf[0] =
        iolink_events_get_highest_severity((iolink_events_ctx_t*) ctx->event_ctx);
#else
    ctx->response_buf[0] = 0U;
#endif
    ctx->response_len = 1U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief 处理强制标识/状态索引（供应商、产品、标签等）。 */
static void handle_mandatory_indices(iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_t* info = isdu_info(ctx);
    if (isdu_check_mandatory_write(ctx)) {
        return;
    }
    /* B.2.5：0x000D 是 ArrayT；此处处理的其他每个对象都是
       仅暴露 subindex 0 的单元素参数。对只读对象的
       WRITE 已在上文以 WRITE_PROTECTED 应答。 */
    if ((ctx->header.subindex != 0U) && (ctx->header.index != IOLINK_IDX_PROFILE_CHARACTERISTIC)) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    const uint16_t index = ctx->header.index;
    switch (index) {
            /* Table B.8：0x0004-0x000B 为保留索引。VendorID/DeviceID 位于
               Direct Parameter Page 1（0x07-0x0B），而不在 ISDU 索引中。因此对
               0x000A/0x000B 的读取会落到默认的 IDX_NOTAVAIL。 */

        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
            if (info == NULL) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
                return;
            }
            isdu_read_profile_characteristic(ctx, info);
            return;
        case IOLINK_IDX_ERROR_COUNT:
            isdu_read_error_count(ctx);
            return;
        case IOLINK_IDX_VENDOR_NAME:
        case IOLINK_IDX_VENDOR_TEXT:
        case IOLINK_IDX_PRODUCT_NAME:
        case IOLINK_IDX_PRODUCT_ID:
        case IOLINK_IDX_PRODUCT_TEXT:
        case IOLINK_IDX_SERIAL_NUMBER:
        case IOLINK_IDX_HARDWARE_REVISION:
        case IOLINK_IDX_FIRMWARE_REVISION:
        case IOLINK_IDX_PRODUCT_URI:
            if (info == NULL) {
                isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
                return;
            }
            (void) isdu_read_identification_string(ctx, info, index);
            return;
        case IOLINK_IDX_APPLICATION_TAG:
        case IOLINK_IDX_FUNCTION_TAG:
        case IOLINK_IDX_LOCATION_TAG:
            isdu_tag_service(ctx, index);
            return;
        case IOLINK_IDX_PROCESS_DATA_INPUT:
            isdu_read_process_data(ctx, true);
            return;
        case IOLINK_IDX_PROCESS_DATA_OUTPUT:
            isdu_read_process_data(ctx, false);
            return;
        case IOLINK_IDX_DEVICE_STATUS:
            isdu_read_device_status(ctx);
            return;

            /* IOLINK_IDX_DETAILED_DEVICE_STATUS（0x0025）在其前的
               handle_standard_commands() 中由 handle_detailed_device_status() 处理。 */

        default:
            isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
            return;
    }
}

/* -------------------------------------------------------------------------
 * SystemCommand 与数据存储上传辅助函数
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_SYSTEM_COMMAND
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
        iolink_event_trigger((iolink_events_ctx_t*) ctx->event_ctx,
                             IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
#else
    (void) ctx;
#endif
}
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */

/** @brief 执行 SystemCommand（复位、恢复出厂、数据存储命令）。 */
#if IOLINK_FEATURE_DATA_STORAGE
/** @brief 将 iolink_ds_* 返回码映射为 ISDU 错误（Table C.1）。 */
static uint8_t isdu_ds_result_error(int ret, uint8_t fallback)
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

#if IOLINK_FEATURE_SYSTEM_COMMAND
/** @brief Table 101：在复位命令中清除设备诊断/状态。 */
static void isdu_reset_diagnostics(iolink_isdu_ctx_t* ctx)
{
    if (ctx->dll_ctx != NULL) {
        iolink_dll_reset_diagnostics((iolink_dll_ctx_t*) ctx->dll_ctx);
    }
}

/** @brief Table 101：复位 ErrorCount 而不触碰事件引擎。 */
static void isdu_reset_error_counters(iolink_isdu_ctx_t* ctx)
{
    if (ctx->dll_ctx != NULL) {
        iolink_dll_reset_error_counters((iolink_dll_ctx_t*) ctx->dll_ctx);
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
            isdu_params_factory_reset(ctx);
            isdu_clear_ds_upload_req(ctx);
            isdu_reset_error_counters(ctx);
#if IOLINK_FEATURE_EVENTS
            if (ctx->event_ctx != NULL) {
                iolink_events_clear_diagnosis((iolink_events_ctx_t*) ctx->event_ctx);
            }
            else if (ctx->dll_ctx != NULL) {
                iolink_events_clear_diagnosis(&((iolink_dll_ctx_t*) ctx->dll_ctx)->events);
            }
#endif
            break;

        case IOLINK_CMD_BACK_TO_BOX: /* 0x83 */
            /* 恢复交付值，然后在应答之后停止并禁用
               通信（10.7.5）。Table 178 还要求
               DS_UPLOAD_FLAG = 0；Table 101 将诊断与状态置为
               "0"。 */
            isdu_params_factory_reset(ctx);
            isdu_clear_ds_upload_req(ctx);
            if (ctx->dll_ctx != NULL) {
                iolink_dll_request_back_to_box((iolink_dll_ctx_t*) ctx->dll_ctx);
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
            ret = iolink_ds_handle_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_END, locks);
        }
        else if ((ds->state == IOLINK_DS_STATE_UPLOADING) ||
                 (ds->state == IOLINK_DS_STATE_UPLOAD_REQ)) {
            (void) iolink_ds_abort(ds);
        }
    }
    else {
        ret = iolink_ds_handle_command(ds, cmd, locks);
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
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */

static void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    IOLINK_LOG_INFO(&ctx->log, "system command 0x%02X", (unsigned) cmd);
#if !IOLINK_FEATURE_SYSTEM_COMMAND
    (void) cmd;
    /* SystemCommand 支持已在编译时移除。 */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
#else
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
#endif /* IOLINK_FEATURE_SYSTEM_COMMAND */
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
static void handle_access_locks(iolink_isdu_ctx_t* ctx)
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
            if ((new_locks & (uint16_t) ~IOLINK_ACCESS_LOCKS_IMPLEMENTED) != 0U) {
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
/** @brief 处理 DetailedDeviceStatus（Table B.14）：3 字节条目的 ArrayT。 */
static void handle_detailed_device_status(iolink_isdu_ctx_t* ctx)
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

    /* 收集待处理的诊断条目：即 "Event appears" 已上报、
       且 "Event disappears" 尚未发出的那些事件。因此，已被清除的
       条件在其撤销仍等待最小出现窗口（A.6.4）期间
       保持列出，与主站的诊断视图一致；仍被延迟的
       出现不会被列出。仅在关中断时获取
       待处理表的快照；下面的格式化在解锁状态下执行
       （应答缓冲区归主上下文所有）。 */
    uint16_t codes[IOLINK_EVENT_PENDING_MAX];
    iolink_event_type_t types[IOLINK_EVENT_PENDING_MAX];
    uint8_t n = 0U;
    IOLINK_CRITICAL_ENTER();
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        const iolink_event_pending_t* p = &event_ctx->pending[i];
        if (p->active && p->reported && (p->type != IOLINK_EVENT_TYPE_NOTIFICATION)) {
            codes[n] = p->code;
            types[n] = p->type;
            n++;
        }
    }
    IOLINK_CRITICAL_EXIT();

    /* B.2.22：符合规范的读取方式是 subindex 0，它返回完整的
       3 字节条目列表（"Subindex access is not supported"）。通过
       subindex 1..n 的元素访问作为设备扩展保留。 */
    const uint8_t sub = ctx->header.subindex;
    if ((sub == 0U) || (sub <= n)) {
        const uint8_t start = (sub == 0U) ? 0U : (uint8_t) (sub - 1U);
        const uint8_t end = (sub == 0U) ? n : (uint8_t) (start + 1U);
        uint8_t pos = 0U;
        for (uint8_t i = start; i < end; i++) {
            /* Figure A.24：MODE 11 = appears，TYPE 10 = Warning / 11 = Error，
               INSTANCE 4 = Application。 */
            uint8_t qualifier = 0xC0U;
            qualifier |= (types[i] == IOLINK_EVENT_TYPE_ERROR) ? (0x03U << 4) : (0x02U << 4);
            qualifier |= 0x04U;
            ctx->response_buf[pos++] = qualifier;
            ctx->response_buf[pos++] = (uint8_t) (codes[i] >> 8);
            ctx->response_buf[pos++] = (uint8_t) (codes[i] & 0xFFU);
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
static void isdu_write_u32_be(uint8_t* buf, size_t* idx, uint32_t value)
{
    buf[(*idx)++] = (uint8_t) ((value >> 24) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 16) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 8) & 0xFFU);
    buf[(*idx)++] = (uint8_t) (value & 0xFFU);
}

/** @brief 以四个大端 DLL 计数器形式返回厂商错误统计索引。 */
static void handle_error_stats(iolink_isdu_ctx_t* ctx)
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
    iolink_dll_get_stats((const iolink_dll_ctx_t*) ctx->dll_ctx, &stats);

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
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        iolink_dll_get_pd_length(dll, &pd_in, &pd_out);
        m_seq = dll->m_seq_type;
        od_width = dll->od_width;
    }

    (void) memset(page, 0, 16);
    /* 0x01 MasterCycleTime 是主站在启动期间写入的值。 */
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        page[0x01] = dll->master_cycle_time;
    }
    {
        uint16_t min_cyc = (ctx->dll_ctx != NULL)
                               ? (uint16_t) iolink_dll_compute_min_cycle_time(
                                     (const iolink_dll_ctx_t*) ctx->dll_ctx)
                               : ((info != NULL) ? (uint16_t) info->min_cycle_time : 0U);
        /* 0x00 表示 "no MinCycleTime"；否则编码时间基准 | 乘数（Table B.3）。 */
        page[0x02] = (min_cyc == 0U) ? 0U : iolink_cycle_time_encode(min_cyc);
    }
    page[0x03] = direct_param_mseq_capability(m_seq, od_width); /* M-sequenceCapability */
    /* DPP1 RevisionID 是一字节的协议修订号。设备信息
       字段保存完整的 IODD iolinkRevision（V1.1 为 0x0111）；其低
       字节即在 DPP1 0x04 中传输的值。 */
    page[0x04] = (uint8_t) (((info != NULL) ? info->revision_id : 0x0111U) & 0xFFU);
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

uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr)
{
    uint8_t page[16];

    if ((ctx == NULL) || (addr >= sizeof(page))) {
        return 0U;
    }

    build_direct_param_page1(ctx, page);
    return page[addr];
}

/** @brief 读/写 Direct Parameter page 1（只读）与 page 2（设备专用）。 */
static void handle_direct_parameters(iolink_isdu_ctx_t* ctx)
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

/* -------------------------------------------------------------------------
 * 数据存储服务
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_DATA_STORAGE
/** @brief 处理数据存储索引：备份（读镜像）或恢复（应用镜像）。 */
static size_t isdu_append_ds_index_list(iolink_ds_ctx_t* ds, uint8_t* buf, size_t pos)
{
    const size_t n = iolink_ds_param_count(ds);
    for (size_t i = 0U; i < n; i++) {
        iolink_ds_param_desc_t d;
        if (!iolink_ds_param_at(ds, i, &d)) {
            break;
        }
        if ((pos + 3U) > IOLINK_ISDU_BUFFER_SIZE) {
            break;
        }
        buf[pos++] = (uint8_t) (d.index >> 8);
        buf[pos++] = (uint8_t) (d.index & 0xFFU);
        buf[pos++] = d.subindex;
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
#if IOLINK_DS_LEGACY_IMAGE_SUB0
    /* 旧的整镜像恢复（为线上兼容而保留）。 */
    const int rc = iolink_ds_apply_image(ds, ctx->buffer, ctx->buffer_idx);
    if (rc != 0) {
        isdu_respond_error(ctx, (rc == -1) ? IOLINK_ISDU_ERROR_PARAM_SET_INVALID
                                           : IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
    }
    else {
        ctx->response_len = 0U; /* 写入已确认 */
    }
#else
    (void) ds;
    /* Table B.10：整条记录为只读；只有 DS_Command（subindex 1）
       可写。 */
    isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
#endif
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
#if IOLINK_DS_LEGACY_IMAGE_SUB0
    /* 旧的整镜像备份读取。 */
    size_t len = 0U;
    const uint8_t* img = iolink_ds_get_image(ds, &len);
    if ((img == NULL) || (len > IOLINK_ISDU_BUFFER_SIZE)) {
        ctx->response_len = 0U;
    }
    else {
        if (len > 0U) {
            (void) memcpy(ctx->response_buf, img, len);
        }
        ctx->response_len = len;
    }
#else
    /* Table B.10/B.11（SDCI_TC_0107）：sub0 是拼接记录：
       DS_Command、State_Property、Data_Storage_Size、Parameter_Checksum 与
       Index_List。 */
    if (!iolink_ds_downloading(ds)) {
        (void) iolink_ds_build_image(ds);
    }
    size_t pos = 0U;
    ctx->response_buf[pos++] = 0x00U; /* DS_Command 读取：未规定 */
    ctx->response_buf[pos++] = iolink_ds_state_property(ds);
    isdu_write_u32_be(ctx->response_buf, &pos, iolink_ds_size(ds));
    isdu_write_u32_be(ctx->response_buf, &pos, iolink_ds_checksum(ds));
    pos = isdu_append_ds_index_list(ds, ctx->response_buf, pos);
    ctx->response_len = (uint8_t) pos;
#endif
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
                (void) iolink_ds_build_image(ds);
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
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
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
static bool isdu_reject_ds_access(iolink_isdu_ctx_t* ctx)
{
    const uint16_t index = ctx->header.index;
    if ((ctx->ds_ctx == NULL) || (index == IOLINK_IDX_SYSTEM_COMMAND) ||
        (index == IOLINK_IDX_DATA_STORAGE) || (index == IOLINK_IDX_DIRECT_PARAMETERS_1) ||
        (index == IOLINK_IDX_DIRECT_PARAMETERS_2)) {
        return false;
    }
    const iolink_ds_ctx_t* ds = (const iolink_ds_ctx_t*) ctx->ds_ctx;
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

/** @brief 通过应用钩子处理设备专用（厂商）索引。 */
static void isdu_handle_vendor_index(iolink_isdu_ctx_t* ctx)
{
    if ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) &&
        ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U)) {
        /* B.2.4：Parameter 锁禁止每个读/写设备参数
           （读仍然可用）。 */
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    if ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) && (ctx->ds_ctx != NULL) &&
        iolink_ds_downloading((const iolink_ds_ctx_t*) ctx->ds_ctx)) {
        const int ret = iolink_ds_stage_param((iolink_ds_ctx_t*) ctx->ds_ctx, ctx->header.index,
                                              ctx->header.subindex, ctx->buffer, ctx->buffer_idx);
        if (ret == 0) {
            ctx->response_len = 0U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
        else {
            isdu_respond_param_error(ctx, ret);
        }
        return;
    }
#endif
#if IOLINK_ISDU_BUFFER_SIZE > 238U
    size_t length = 235U;
#else
    size_t length = IOLINK_ISDU_BUFFER_SIZE;
#endif
    const size_t capacity = length;
    const uint8_t error =
        ctx->vendor_service(ctx->vendor_user, ctx->header.index, ctx->header.subindex,
                            ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE, ctx->buffer,
                            ctx->buffer_idx, ctx->response_buf, &length);
    if ((length > capacity) || (error != 0U)) {
        isdu_respond_error(ctx, (length > capacity) ? IOLINK_ISDU_ERROR_IDX_NOT_AVAIL : error);
        return;
    }
    ctx->response_len = (uint8_t) length;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief SystemCommand 索引：只写的命令字节。 */
static void isdu_handle_system_command_index(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        /* SystemCommand（Table B.8）为只写；读请求以
           IDX_NOT_ACCESSIBLE 应答（Table C.1）。事件通过
           诊断通道的事件存储读取（Table 58）。 */
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
        return;
    }
    if (ctx->buffer_idx == 0U) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    handle_system_command(ctx, ctx->buffer[0]);
}

/** @brief 将已执行的 ISDU 请求分发到其索引对应的处理器。 */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx)
{
    ctx->response_negative = false;
    /* B.2.27：Preferred Indices 0x0040-0x00FE 与
       从 0x0100 开始的 Extended Index 范围一样属于设备专用；两者都会到达应用的厂商
       服务。0x00FF 本身为保留值。 */
    const uint16_t index = ctx->header.index;
#if IOLINK_FEATURE_DATA_STORAGE
    if (isdu_reject_ds_access(ctx)) {
        return;
    }
#endif
    const bool device_specific = ((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U);
    if (device_specific && (ctx->vendor_service == NULL)) {
        IOLINK_LOG_DEBUG(&ctx->log, "vendor index 0x%04X ignored: no vendor_service",
                         (unsigned) index);
    }
    if (device_specific && (ctx->vendor_service != NULL)) {
        isdu_handle_vendor_index(ctx);
    }
    else if (index == IOLINK_IDX_SYSTEM_COMMAND) {
        isdu_handle_system_command_index(ctx);
    }
    else if ((index == IOLINK_IDX_DIRECT_PARAMETERS_1) ||
             (index == IOLINK_IDX_DIRECT_PARAMETERS_2)) {
        handle_direct_parameters(ctx);
    }
#if IOLINK_FEATURE_DATA_STORAGE
    else if (index == IOLINK_IDX_DATA_STORAGE) {
        handle_data_storage(ctx);
    }
#endif
    else if (index == IOLINK_IDX_DEVICE_ACCESS_LOCKS) {
        handle_access_locks(ctx);
    }
#if IOLINK_FEATURE_EVENTS
    else if (index == IOLINK_IDX_DETAILED_DEVICE_STATUS) {
        handle_detailed_device_status(ctx);
    }
#endif
    else if (index == IOLINK_IDX_ERROR_STATS) {
        handle_error_stats(ctx);
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
    else {
        handle_mandatory_indices(ctx);
    }
}

/* -------------------------------------------------------------------------
 * 公开 API
 * ------------------------------------------------------------------------- */

void iolink_isdu_process(iolink_isdu_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands(ctx);
        if (ctx->state != ISDU_STATE_RESPONSE_READY) {
            isdu_transport_reset(ctx);
        }
        else {
            isdu_frame_response(ctx);
            IOLINK_LOG_DEBUG(&ctx->log, "isdu rsp idx=0x%04X sub=%u neg=%u len=%u",
                             (unsigned) ctx->header.index, (unsigned) ctx->header.subindex,
                             (unsigned) ctx->response_negative, (unsigned) ctx->resp_total);
        }
    }
}

int iolink_isdu_get_response_byte(iolink_isdu_ctx_t* ctx, uint8_t* byte)
{
    if ((ctx == NULL) || (byte == NULL)) {
        return 0;
    }
    if (ctx->response_idx >= ctx->response_len) {
        return 0;
    }
    *byte = ctx->response_buf[ctx->response_idx++];
    return 1;
}

uint8_t iolink_isdu_snapshot_response(const iolink_isdu_ctx_t* ctx, bool* busy, uint8_t* dst,
                                      size_t dst_size)
{
    if (busy != NULL) {
        *busy = false;
    }
    if (ctx == NULL) {
        return 0U;
    }
    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        if (busy != NULL) {
            *busy = true;
        }
        return 0U;
    }
    if ((ctx->state != ISDU_STATE_RESPONSE_READY) || (dst == NULL) || (dst_size == 0U)) {
        return 0U;
    }
    size_t n = ctx->resp_total;
    if (n > dst_size) {
        n = dst_size;
    }
    /* isr_isdu_len 是 uint8_t；规范将组帧应答限制在 241 字节，
       因此该限制仅是防御性的。 */
    if (n > 255U) {
        n = 255U;
    }
    if (n > 0U) {
        (void) memcpy(dst, ctx->resp_buf, n);
    }
    return (uint8_t) n;
}

bool iolink_isdu_is_idle(const iolink_isdu_ctx_t* ctx)
{
    return (ctx != NULL) && (ctx->state == ISDU_STATE_IDLE);
}
