/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/protocol.h"
#include "iolinki/isdu.h"
#include "iolinki/internal/dll_main.h"
#include "iolinki/iolink.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/data_storage.h"
#include "iolinki/port.h"
#include "iolinki/utils.h"
#include <string.h>
#include <stdint.h>
#include "isdu_internal.h"

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
 * 能力位查询
 * ------------------------------------------------------------------------- */

/** @brief 测试"构建自带"能力位；未绑定能力表时视为开（单元测试直用引擎）。 */
bool isdu_cap_default_on(const iolink_isdu_ctx_t* ctx, iolink_caps_t bit)
{
    return (ctx->caps == NULL) || iolink_caps_has(*ctx->caps, bit);
}

/** @brief 测试"运行时可选"能力位；未绑定能力表时视为关（严格 V1.1）。 */
IOLINK_MAYBE_UNUSED bool isdu_cap(const iolink_isdu_ctx_t* ctx, iolink_caps_t bit)
{
    return (ctx->caps != NULL) && iolink_caps_has(*ctx->caps, bit);
}

/** @brief 本实例实现了哪些 DeviceAccessLocks 位（0x000C 写校验的可写掩码）。 */
uint16_t isdu_locks_implemented(const iolink_isdu_ctx_t* ctx)
{
    uint16_t mask = 0U;
    if (isdu_cap_default_on(ctx, IOLINK_CAP_LOCK_PARAM)) {
        mask |= IOLINK_LOCK_PARAM;
    }
    if (isdu_cap_default_on(ctx, IOLINK_CAP_LOCK_DS)) {
        mask |= IOLINK_LOCK_DS;
    }
    if (isdu_cap_default_on(ctx, IOLINK_CAP_LOCK_LOCAL_PARAM)) {
        mask |= IOLINK_LOCK_LOCAL_PARAM;
    }
    if (isdu_cap_default_on(ctx, IOLINK_CAP_LOCK_LOCAL_UI)) {
        mask |= IOLINK_LOCK_LOCAL_UI;
    }
    return mask;
}

/* -------------------------------------------------------------------------
 * 文件导览
 *
 *  - 传输、解析与共享辅助函数：chkpdu ... iolink_isdu_od_read
 *    （公开：iolink_isdu_init、iolink_isdu_od_write、iolink_isdu_od_read）
 *  - 共享应答辅助函数：isdu_respond_error
 *  - 表驱动分发：isdu_entry_allowed ... isdu_serve_entry（OD 表见 src/od.c）
 *  - SystemCommand 与数据存储上传辅助函数：isdu_clear_ds_upload_req ...
 *    （公开：iolink_isdu_execute_system_command）
 *  - 访问锁、详细状态、错误统计与直接参数：
 *    handle_access_locks ... handle_direct_parameters
 *  - 数据存储服务：isdu_append_ds_index_list ... isdu_reject_ds_access
 *  - 索引路由与标准分发：handle_standard_commands ...
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

/** @brief 把一段 scratch 缓冲接到上下文（唯一设置缓冲指针的位置）。 */
static void isdu_attach_scratch(iolink_isdu_ctx_t* ctx, iolink_isdu_scratch_t* scratch)
{
    ctx->buffer = scratch->payload;
    ctx->buffer_size = sizeof(scratch->payload);
    ctx->response_buf = scratch->response;
    ctx->response_size = sizeof(scratch->response);
    ctx->req_buf = scratch->request;
    ctx->req_size = sizeof(scratch->request);
    ctx->resp_frame = scratch->frame;
    /* 租约模型只有一份成帧缓冲：两个视图槽指向同一块内存，sel 位
     * 仍按既有发布字编码交替，但不再复制第二份应答。 */
    ctx->resp_store[0] = scratch->frame;
    ctx->resp_store[1] = scratch->frame;
    ctx->resp_buf = scratch->frame;
}

#if !IOLINK_WORKSPACE_EMBEDDED
/** @brief 解绑：清零全部传输缓冲指针与容量，不留旧工作区的悬空指针。 */
static void isdu_clear_scratch(iolink_isdu_ctx_t* ctx)
{
    ctx->buffer = NULL;
    ctx->buffer_size = 0U;
    ctx->response_buf = NULL;
    ctx->response_size = 0U;
    ctx->req_buf = NULL;
    ctx->req_size = 0U;
    ctx->resp_frame = NULL;
    ctx->resp_store[0] = NULL;
    ctx->resp_store[1] = NULL;
    ctx->resp_buf = NULL;
}
#endif

void iolink_isdu_bind_workspace(iolink_isdu_ctx_t* ctx, iolink_workspace_t* ws)
{
    if (ctx == NULL) {
        return;
    }
    ctx->workspace = ws;
    if (ws != NULL) {
        isdu_attach_scratch(ctx, &ws->isdu);
        return;
    }
#if IOLINK_WORKSPACE_EMBEDDED
    isdu_attach_scratch(ctx, &ctx->scratch);
#else
    /* 无内嵌兜底：解绑必须是显式的，不能继续指向已失效的工作区。 */
    isdu_clear_scratch(ctx);
#endif
}

void iolink_isdu_init(iolink_isdu_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_isdu_ctx_t))) {
        return;
    }
    ctx->state = ISDU_STATE_IDLE;
    ctx->resp_sel = 0U;
    ctx->resp_pub = 0U;
    /* 清零后的映像没有内容：标记失效，由第一次主泵读取/处理重建。 */
    iolink_isdu_dp_page1_invalidate(ctx);
#if IOLINK_WORKSPACE_EMBEDDED
    isdu_attach_scratch(ctx, &ctx->scratch);
#endif
}

void iolink_isdu_bind_od(iolink_isdu_ctx_t* ctx, const iolink_od_app_t* app)
{
    if (ctx != NULL) {
        ctx->od = app;
    }
}

void iolink_isdu_bind_caps(iolink_isdu_ctx_t* ctx, const iolink_caps_t* caps)
{
    if (ctx != NULL) {
        ctx->caps = caps;
    }
}

/** @brief 对绑定的 DS 上下文执行恢复出厂设置；未绑定则不做任何事。 */
void isdu_factory_reset(iolink_isdu_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }
#if IOLINK_FEATURE_DATA_STORAGE
    /* 应用参数恢复默认值（三个标签也由应用的 DS 表项承担）。 */
    if (ctx->ds_ctx != NULL) {
        (void) iolink_ds_factory_reset((iolink_ds_ctx_t*) ctx->ds_ctx);
    }
#endif
}

/** @brief 绑定到该 ISDU 上下文的设备信息上下文（若有）。 */
iolink_device_info_ctx_t* isdu_info_ctx(iolink_isdu_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->device_info : NULL;
}

/** @brief 该 ISDU 上下文的有效标识。 */
const iolink_device_info_t* isdu_info(const iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_ctx_t* info_ctx = (ctx != NULL) ? ctx->device_info : NULL;
    return (info_ctx != NULL) ? iolink_device_info_ctx_get(info_ctx) : NULL;
}

/** @brief 该 ISDU 上下文的有效访问锁。 */
uint16_t isdu_access_locks(const iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_ctx_t* info_ctx = (ctx != NULL) ? ctx->device_info : NULL;
    return (info_ctx != NULL) ? iolink_device_info_ctx_get_access_locks(info_ctx) : 0U;
}

void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error);

/** @brief 将统一结果翻译为 ISDU 否定应答。
 *
 * 这是内部错误语义与线上协议之间的唯一翻译点：
 * 正值是 ISDU ErrorCode，原样上送；负值（内部错误/
 * 未处理）统一压成 IDX_NOT_AVAIL。 */
void isdu_respond_result(iolink_isdu_ctx_t* ctx, iolink_result_t result)
{
    isdu_respond_error(ctx,
                       (result.code > 0) ? (uint8_t) result.code : IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
}

/* handle_standard_commands() 定义在下方；传输层在收齐一个
   完整的 ISDU 请求后执行它。 */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx);

/**
 * @brief 复位写入口暂存状态（ISR 上下文安全；不触碰主泵独占字段）。
 *
 * 只清 req_buf 的累计位置与 FlowCTRL 水位，并把 stage_state 置回 IDLE。
 * 主泵的 `state`/`header`/响应缓冲不在此复位，避免 ISR 与主泵争用。
 */
static void isdu_stage_reset(iolink_isdu_ctx_t* ctx)
{
    ctx->req_idx = 0U;
    ctx->req_total = 0U;
    ctx->flowctrl_seen = false;
    ctx->stage_state = IOLINK_ISDU_STAGE_IDLE;
}

/** @brief 将 ISDU 请求传输层复位到空闲状态（主泵上下文）。 */
void isdu_transport_reset(iolink_isdu_ctx_t* ctx)
{
    ctx->response_negative = false;
    ctx->state = ISDU_STATE_IDLE;
    ctx->resp_idx = 0U;
    ctx->resp_total = 0U;
    ctx->buffer_idx = 0U;
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->error_code = IOLINK_ISDU_ERROR_NONE;
    ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
    ctx->header.length = 0U;
    ctx->header.index = 0U;
    ctx->header.subindex = 0U;
    isdu_stage_reset(ctx);
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
            if (n > ctx->buffer_size) {
                n = ctx->buffer_size;
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

    /* 已解析：执行由 iolink_isdu_process() 完成，从而保持
       传输层与应用分发可分离（且可测试）。 */
    ctx->state = ISDU_STATE_SERVICE_EXECUTE;
}

/** @brief 租约开始：在覆盖成帧缓冲前，把 ISR 视图显式标记为 Busy。
 *
 * 单份成帧缓冲意味着主泵写缓冲时，ISR 不得再把它当成"当前应答"。
 * DLL 的发布字已经是唯一交接点，这里在写缓冲之前先发布
 * `{len=0, busy=1}`：ISR 看到 Busy/无服务后不会再读取缓冲内容，
 * 随后的完成应答由 `dll_isdu_sync_snapshot()` 连同代数字一起发布。
 * 独立引擎（`dll_ctx == NULL`）没有 ISR 读者，直接返回。
 */
static void isdu_lease_begin(iolink_isdu_ctx_t* ctx)
{
    iolink_dll_main_t* dll = (iolink_dll_main_t*) ctx->dll_ctx;
    if (dll == NULL) {
        return;
    }
    const IOLINK_ATOMIC_UINT_T busy = IOLINK_ISDU_PUB_WORD(0U, 1U, 0U);
    if (iolink_atomic_load(&dll->shared.isr_isdu_pub) != busy) {
        iolink_atomic_store(&dll->shared.isr_isdu_pub, busy);
    }
}

/** @brief 将处理器结果组帧到 ctx->resp_buf（肯定或否定，A.5/C.1）。 */
static void isdu_frame_response(iolink_isdu_ctx_t* ctx)
{
    const bool negative = ctx->response_negative;
    const bool write = (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE);
    size_t pos = 0U;

    isdu_lease_begin(ctx);

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
    isdu_lease_begin(ctx);
    ctx->resp_buf[0] = 0x01U;
    ctx->resp_total = 1U;
    ctx->resp_idx = 0U;
}

void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od,
                          uint8_t od_len)
{
    if ((ctx == NULL) || (ctx->buffer == NULL) || (od == NULL)) {
        return;
    }

    /* Table 52：ABORT（0x1F）拒绝请求数据，并将传输层
       复位到空闲，与方向无关。IDLE/IDLE2 及保留值
       0x13..0x1E 不是写操作；它们由读路径处理。 */
    if (flowctrl == IOLINK_FLOWCTRL_ABORT) {
        isdu_stage_reset(ctx);
        ctx->stage_state = IOLINK_ISDU_STAGE_ABORT;
        return;
    }
    if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        return;
    }

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        isdu_stage_reset(ctx);
        /* 仅在上一应答已发布时才轮换组帧应答缓冲区；
         * 否则替换尚未发布的构建结果。 */
        if (ctx->resp_sel == ctx->resp_pub) {
            ctx->resp_sel = (uint8_t) (ctx->resp_sel ^ 1U);
            ctx->resp_buf = ctx->resp_store[ctx->resp_sel];
        }
        ctx->stage_state = IOLINK_ISDU_STAGE_RECEPTION;
        ctx->flowctrl_seen = true;
        ctx->last_flowctrl = flowctrl;
    }
    else {
        /* COUNT（0x00..0x0F）：只在写请求进行中、且是「下一个期望段」时
           才接受。重复段（重发）与过早的乱序段都照常 ACK，但不修改
           缓冲——重发不会消耗新位置，乱序也不会破坏已累计的请求；
           主站补齐缺失段后自然恢复。 */
        if ((ctx->stage_state != IOLINK_ISDU_STAGE_RECEPTION) || !ctx->flowctrl_seen) {
            return;
        }
        const uint8_t expected = (uint8_t) ((ctx->last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        if (flowctrl != expected) {
            return;
        }
        ctx->last_flowctrl = flowctrl;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->req_idx < ctx->req_size) {
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
        /* 累计完成：主泵在 iolink_isdu_process() 中校验 CHKPDU 并执行。 */
        ctx->stage_state = IOLINK_ISDU_STAGE_COMPLETE;
    }
}

void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len)
{
    if ((ctx == NULL) || (ctx->buffer == NULL)) {
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
        /* 应答读取开始。若某服务正在执行（或写请求已收齐、等待主泵
           接管），应用尚未应答，则应答 Busy（0x01）（Table A.14）。 */
        if ((ctx->state == ISDU_STATE_SERVICE_EXECUTE) ||
            (ctx->stage_state == IOLINK_ISDU_STAGE_COMPLETE)) {
            isdu_emit_busy(ctx);
        }
        else if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            /* 重启的读：从第一个应答字节重新开始 */
            ctx->resp_idx = 0U;
        }
        else {
            /* 空闲 / 无待处理服务：No Service（0x00）。 */
            isdu_lease_begin(ctx);
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
void isdu_respond_error(iolink_isdu_ctx_t* ctx, uint8_t error)
{
    IOLINK_LOG_DEBUG(&ctx->log, "isdu error 0x%02X", (unsigned) error);
    ctx->response_negative = true;
    ctx->response_buf[0] = 0x80U;
    ctx->response_buf[1] = error;
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief 将已执行的 ISDU 请求分发到其索引对应的处理器。 */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx)
{
    ctx->response_negative = false;
    /* B.2.27：Preferred Indices 0x0040-0x00FE 与从 0x0100 开始的
       Extended Index 范围属于设备专用；未在应用 OD 表中声明时回落到
       标准表内建项。0x00FF 本身为保留值。 */
    const uint16_t index = ctx->header.index;
#if IOLINK_FEATURE_DATA_STORAGE
    if (isdu_reject_ds_access(ctx)) {
        return;
    }
#endif
    /* 索引归属由一处决定：
     *  - 规范固定索引（< 0x0040）：标准表优先，应用表不得遮蔽
     *    0x0002/0x0024 等对象；
     *  - 设备专用索引（0x0040-0x00FE、0x0100+）：应用表优先，未声明时
     *    回落到标准表里的内建厂商扩展（0x0040 错误统计）。 */
    const bool device_specific = ((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U);
    const iolink_od_entry_t* entry = NULL;

    if (device_specific) {
        entry = iolink_od_find_app(ctx->od, index, ctx->header.subindex);
        if (entry == NULL) {
            entry = iolink_od_find(NULL, index, ctx->header.subindex);
        }
    }
    else {
        entry = iolink_od_find(ctx->od, index, ctx->header.subindex);
    }
    if (entry == NULL) {
        isdu_respond_error(ctx, IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
        return;
    }
    isdu_serve_entry(ctx, entry);
}

/* -------------------------------------------------------------------------
 * 公开 API
 * ------------------------------------------------------------------------- */

/** @brief 校验并解析 ISR 累计完成的写请求（调用方持临界区）。 */
static bool isdu_apply_staged_request(iolink_isdu_ctx_t* ctx)
{
    uint8_t x = 0U;
    for (size_t i = 0U; i < ctx->req_total; i++) {
        x ^= ctx->req_buf[i];
    }
    if (x != 0U) {
        /* A.5.6 / Table 84：CHKPDU 无效使请求无效；丢弃请求并回到空闲。
         * 不要为格式错误的请求合成否定 ISDU 应答。 */
        ctx->error_code = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
        isdu_transport_reset(ctx);
        return false;
    }
    isdu_execute_request(ctx, ctx->req_total);
    ctx->stage_state = IOLINK_ISDU_STAGE_IDLE; /* 把 req_buf 交还 ISR */
    return true;
}

void iolink_isdu_process(iolink_isdu_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* DPP1 映像失效时由主泵在本上下文重建；ISR 侧只读稳定副本，
     * 因此这里与查询/失效入口一样按上下文门控。 */
    if (dp_page1_main_context(ctx)) {
        dp_page1_ensure(ctx);
    }

    if (ctx->buffer == NULL) {
        return;
    }

    /* 快路径：没有待接管的写请求时不进临界区（PHY 服务钩子必须能在
     * 区外运行）。COMPLETE/ABORT 的权威判定在临界区内重读一次，
     * 避免 ISR 在「读到 COMPLETE」与「进入临界区」之间开始新的 START，
     * 让主泵用被改写的 req_buf/req_total 解析一条撕裂的请求。 */
    uint8_t stage = ctx->stage_state;
    bool applied = false;
    if ((stage == IOLINK_ISDU_STAGE_COMPLETE) || (stage == IOLINK_ISDU_STAGE_ABORT)) {
        IOLINK_CRITICAL_ENTER();
        stage = ctx->stage_state;
        if (stage == IOLINK_ISDU_STAGE_ABORT) {
            isdu_transport_reset(ctx);
        }
        else if (stage == IOLINK_ISDU_STAGE_COMPLETE) {
            applied = isdu_apply_staged_request(ctx);
        }
        IOLINK_CRITICAL_EXIT();
    }

    if (applied) {
        IOLINK_LOG_DEBUG(&ctx->log, "isdu req idx=0x%04X sub=%u len=%u write=%u",
                         (unsigned) ctx->header.index, (unsigned) ctx->header.subindex,
                         (unsigned) ctx->header.length,
                         (unsigned) (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE));
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
    if ((ctx->state != ISDU_STATE_RESPONSE_READY) || (dst == NULL) || (dst_size == 0U) ||
        (ctx->resp_buf == NULL)) {
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
    return (ctx != NULL) && (ctx->state == ISDU_STATE_IDLE) &&
           (ctx->stage_state == IOLINK_ISDU_STAGE_IDLE);
}
