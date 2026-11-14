/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file od.c
 * @brief 对象字典表、datatype codec 与参数值访问层。
 * @ingroup iolinki_od
 */

#include "iolinki/od.h"
#include "iolinki/protocol.h"
#include "od_internal.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * datatype codec 表：长度/字节序/原生范围的唯一实现
 * ------------------------------------------------------------------------- */

static const iolink_od_codec_t k_od_codecs[IOLINK_OD_DT_COUNT] = {
    {1U, 0xFFU},       /* U8 */
    {2U, 0xFFFFU},     /* U16 */
    {4U, 0xFFFFFFFFU}, /* U32 */
    {1U, 0x01U},       /* Bool */
    {0U, 0xFFFFFFFFU}, /* OctetString（变长） */
};

/* -------------------------------------------------------------------------
 * 标准对象表：强制索引与厂商错误统计的唯一事实源
 *
 * 表项顺序即 DS 参数集的序列化顺序（标签在前，与既有镜像一致）。
 * 0x0004-0x000B、0x001C-0x001F 为 Table B.8 保留索引，不建表项，
 * 因此读/写自然落到 IDX_NOT_AVAIL。
 * ------------------------------------------------------------------------- */

static const iolink_od_entry_t k_od_table[] = {
    /* 0x0000/0x0001：Direct Parameter page 1/2（16 字节映像，sub 0..16）。 */
    {.index = IOLINK_IDX_DIRECT_PARAMETERS_1,
     .subindex = 0U,
     .max_subindex = 16U,
     .datatype = IOLINK_OD_DT_OCTET_STRING,
     .access = IOLINK_OD_ACCESS_RO,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = 0U,
     .handler = IOLINK_OD_HANDLER_DIRECT_PARAM_1,
     .block_len = 16U,
     .name = "DirectParameters_1"},
    {.index = IOLINK_IDX_DIRECT_PARAMETERS_2,
     .subindex = 0U,
     .max_subindex = 16U,
     .datatype = IOLINK_OD_DT_OCTET_STRING,
     .access = IOLINK_OD_ACCESS_RW_LOCKED,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = 0U,
     .handler = IOLINK_OD_HANDLER_DIRECT_PARAM_2,
     .block_len = 16U,
     .name = "DirectParameters_2"},

    /* 0x0002：只写命令对象；写入是触发语义，值不保存。 */
    IOLINK_OD_BUILTIN_SCALAR(IOLINK_IDX_SYSTEM_COMMAND, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_WO,
                             IOLINK_OD_WRITE_TRIGGER, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_SYSTEM_COMMAND, 0U, 0xFFU, 0U, "SystemCommand"),

    /* 0x0003：DataStorage 记录；子索引由处理器校验（0..5）。 */
    {.index = IOLINK_IDX_DATA_STORAGE,
     .subindex = 0U,
     .max_subindex = 0xFFU,
     .datatype = IOLINK_OD_DT_OCTET_STRING,
     .access = IOLINK_OD_ACCESS_RW,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = IOLINK_OD_FLAG_BLOCK,
     .handler = IOLINK_OD_HANDLER_DATA_STORAGE,
     .block_len = 0U,
     .name = "DataStorage"},

    /* 0x000C：访问锁本身不受 Parameter 锁保护。 */
    IOLINK_OD_BUILTIN_SCALAR(IOLINK_IDX_DEVICE_ACCESS_LOCKS, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_ACCESS_LOCKS, 0U, 0xFFFFU, 0U, "DeviceAccessLocks"),

    /* 0x000D：ProfileCharacteristic 为 UIntegerT16 的 ArrayT（sub 0/1）。
       子索引边界由处理器校验：未配置 Profile 时优先回答 IDX_NOT_AVAIL。 */
    {.index = IOLINK_IDX_PROFILE_CHARACTERISTIC,
     .subindex = 0U,
     .max_subindex = 0xFFU,
     .datatype = IOLINK_OD_DT_U16,
     .access = IOLINK_OD_ACCESS_RO,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = 0U,
     .handler = IOLINK_OD_HANDLER_PROFILE,
     .block_len = 0U,
     .range_max = 0xFFFFU,
     .name = "ProfileCharacteristic"},

    /* 0x0010-0x0017：只读标识字符串。 */
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_VENDOR_NAME, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_IDENT_STRING, 64U,
                             "VendorName"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_VENDOR_TEXT, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_IDENT_STRING, 64U,
                             "VendorText"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_PRODUCT_NAME, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_IDENT_STRING, 64U,
                             "ProductName"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_PRODUCT_ID, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_IDENT_STRING, 64U,
                             "ProductID"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_PRODUCT_TEXT, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_IDENT_STRING, 64U,
                             "ProductText"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_SERIAL_NUMBER, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_IDENT_STRING, 16U, "SerialNumber"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_HARDWARE_REVISION, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_IDENT_STRING, 64U, "HardwareRevision"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_FIRMWARE_REVISION, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_IDENT_STRING, 64U, "FirmwareRevision"),

    /* 0x0018-0x001A：可写标签，属于 DS 参数集并写进参数镜像。 */
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_APPLICATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED,
                             IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_TAG,
                             32U, "ApplicationSpecificTag"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_FUNCTION_TAG, IOLINK_OD_ACCESS_RW_LOCKED,
                             IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_TAG,
                             32U, "FunctionTag"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_LOCATION_TAG, IOLINK_OD_ACCESS_RW_LOCKED,
                             IOLINK_OD_WRITE_STORE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_TAG,
                             32U, "LocationTag"),

    /* 0x001B：ProductURI 可选，取决于 Profile。 */
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_PRODUCT_URI, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_IDENT_STRING, 100U,
                             "ProductURI"),

    /* 0x0020：ErrorCount（自复位以来的错误计数，2 字节）。 */
    IOLINK_OD_BUILTIN_SCALAR(IOLINK_IDX_ERROR_COUNT, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_ERROR_COUNT, 0U, 0xFFFFU, 0U, "ErrorCount"),

    /* 0x0024：DeviceStatus（单字节）。 */
    IOLINK_OD_BUILTIN_SCALAR(IOLINK_IDX_DEVICE_STATUS, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_DEVICE_STATUS, 0U, 0xFFU, 0U, "DeviceStatus"),

    /* 0x0025：DetailedDeviceStatus（3 字节条目的 ArrayT，子索引由处理器校验）。 */
    {.index = IOLINK_IDX_DETAILED_DEVICE_STATUS,
     .subindex = 0U,
     .max_subindex = 0xFFU,
     .datatype = IOLINK_OD_DT_OCTET_STRING,
     .access = IOLINK_OD_ACCESS_RO,
     .write_semantics = IOLINK_OD_WRITE_VOLATILE,
     .storage = IOLINK_OD_STORAGE_VOLATILE,
     .flags = 0U,
     .handler = IOLINK_OD_HANDLER_DETAILED_STATUS,
     .block_len = 0U,
     .name = "DetailedDeviceStatus"},

    /* 0x0028/0x0029：最近一次有效的过程数据快照。 */
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_PROCESS_DATA_INPUT, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_PD_INPUT, 32U, "ProcessDataInput"),
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_PROCESS_DATA_OUTPUT, IOLINK_OD_ACCESS_RO,
                             IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                             IOLINK_OD_HANDLER_PD_OUTPUT, 32U, "ProcessDataOutput"),

    /* 0x0040：厂商错误统计（4 个大端计数器）。 */
    IOLINK_OD_BUILTIN_STRING(IOLINK_IDX_ERROR_STATS, IOLINK_OD_ACCESS_RO, IOLINK_OD_WRITE_VOLATILE,
                             IOLINK_OD_STORAGE_VOLATILE, IOLINK_OD_HANDLER_ERROR_STATS, 16U,
                             "VendorErrorStatistics"),
};

#define OD_TABLE_COUNT (sizeof(k_od_table) / sizeof(k_od_table[0]))

const iolink_od_codec_t* iolink_od_codec(uint8_t datatype)
{
    if (datatype >= (uint8_t) IOLINK_OD_DT_COUNT) {
        return NULL;
    }
    return &k_od_codecs[datatype];
}

size_t iolink_od_count(void)
{
    return OD_TABLE_COUNT;
}

const iolink_od_entry_t* iolink_od_at(size_t i)
{
    return (i < OD_TABLE_COUNT) ? &k_od_table[i] : NULL;
}

/** @brief 在给定表中查找表项：精确子索引优先，否则 sub 0 表项兜底。 */
static const iolink_od_entry_t* od_find_in(const iolink_od_entry_t* entries, size_t count,
                                           uint16_t index, uint8_t subindex)
{
    const iolink_od_entry_t* fallback = NULL;

    if (entries == NULL) {
        return NULL;
    }
    for (size_t i = 0U; i < count; i++) {
        if (entries[i].index != index) {
            continue;
        }
        if (entries[i].subindex == subindex) {
            return &entries[i];
        }
        if ((entries[i].subindex == 0U) && (fallback == NULL)) {
            fallback = &entries[i];
        }
    }
    return fallback;
}

const iolink_od_entry_t* iolink_od_find_app(const iolink_od_app_t* app, uint16_t index,
                                            uint8_t subindex)
{
    return (app != NULL) ? od_find_in(app->entries, app->count, index, subindex) : NULL;
}

/** @brief 索引是否属于设备专用范围（B.2.27 Preferred / Extended Index）。 */
static bool od_index_is_device_specific(uint16_t index)
{
    return ((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U);
}

const iolink_od_entry_t* iolink_od_find(const iolink_od_app_t* app, uint16_t index,
                                        uint8_t subindex)
{
    const iolink_od_entry_t* entry;

    if (od_index_is_device_specific(index)) {
        /* 设备专用索引由产品定义：应用表优先，标准表里同索引的内建
         * 厂商扩展（如 0x0040 错误统计）只作回落。 */
        entry = iolink_od_find_app(app, index, subindex);
        if (entry == NULL) {
            entry = od_find_in(k_od_table, OD_TABLE_COUNT, index, subindex);
        }
    }
    else {
        /* 规范固定索引不允许被应用表覆盖。 */
        entry = od_find_in(k_od_table, OD_TABLE_COUNT, index, subindex);
        if (entry == NULL) {
            entry = iolink_od_find_app(app, index, subindex);
        }
    }
    return entry;
}

size_t iolink_od_ds_count(void)
{
    size_t n = 0U;

    for (size_t i = 0U; i < OD_TABLE_COUNT; i++) {
        if ((k_od_table[i].storage & IOLINK_OD_STORAGE_DS) != 0U) {
            n++;
        }
    }
    return n;
}

const iolink_od_entry_t* iolink_od_ds_at(size_t i)
{
    size_t seen = 0U;

    for (size_t j = 0U; j < OD_TABLE_COUNT; j++) {
        if ((k_od_table[j].storage & IOLINK_OD_STORAGE_DS) == 0U) {
            continue;
        }
        if (seen == i) {
            return &k_od_table[j];
        }
        seen++;
    }
    return NULL;
}

size_t iolink_od_ds_count_app(const iolink_od_app_t* app)
{
    if (app == NULL) {
        return 0U;
    }
    size_t n = 0U;

    for (size_t i = 0U; i < app->count; i++) {
        if ((app->entries[i].storage & IOLINK_OD_STORAGE_DS) != 0U) {
            n++;
        }
    }
    return n;
}

const iolink_od_entry_t* iolink_od_ds_at_app(const iolink_od_app_t* app, size_t i)
{
    size_t seen = 0U;

    if (app == NULL) {
        return NULL;
    }
    for (size_t j = 0U; j < app->count; j++) {
        if ((app->entries[j].storage & IOLINK_OD_STORAGE_DS) == 0U) {
            continue;
        }
        if (seen == i) {
            return &app->entries[j];
        }
        seen++;
    }
    return NULL;
}

bool iolink_od_is_ds(const iolink_od_app_t* app, uint16_t index, uint8_t subindex)
{
    const iolink_od_entry_t* entry = od_find_in(k_od_table, OD_TABLE_COUNT, index, subindex);

    if (entry == NULL) {
        entry = iolink_od_find_app(app, index, subindex);
    }
    return (entry != NULL) && ((entry->storage & IOLINK_OD_STORAGE_DS) != 0U) &&
           (entry->subindex == subindex);
}

iolink_result_t iolink_od_decode(const iolink_od_entry_t* entry, const uint8_t* data, size_t len,
                                 uint32_t* value)
{
    const iolink_od_codec_t* codec;

    if ((entry == NULL) || (value == NULL) || (data == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    codec = iolink_od_codec(entry->datatype);
    if ((codec == NULL) || (codec->len == 0U)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if (len != (size_t) codec->len) {
        /* 唯一的长错误码来源：长度不符一律 0x33（与既有实现一致）。 */
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    }

    uint32_t decoded = 0U;
    for (size_t i = 0U; i < len; i++) {
        decoded = (decoded << 8U) | (uint32_t) data[i];
    }
    if ((decoded < entry->range_min) || (decoded > entry->range_max)) {
        /* 唯一的范围错误码来源。 */
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    }
    *value = decoded;
    return iolink_result_ok(codec->len);
}

iolink_result_t iolink_od_encode(const iolink_od_entry_t* entry, uint32_t value, uint8_t* out,
                                 size_t cap)
{
    const iolink_od_codec_t* codec;

    if ((entry == NULL) || (out == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    codec = iolink_od_codec(entry->datatype);
    if ((codec == NULL) || (codec->len == 0U)) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if ((cap < (size_t) codec->len) || (value > codec->max)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    for (uint8_t i = 0U; i < codec->len; i++) {
        out[i] = (uint8_t) (value >> (8U * (uint32_t) ((codec->len - 1U) - i)));
    }
    return iolink_result_ok(codec->len);
}

iolink_result_t iolink_od_check_value(const iolink_od_entry_t* entry, void* user,
                                      const uint8_t* data, size_t len)
{
    if (entry == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if ((data == NULL) && (len > 0U)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }

    if (entry->datatype != (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
        uint32_t ignored = 0U;
        const iolink_result_t decoded = iolink_od_decode(entry, data, len, &ignored);
        if (!iolink_result_succeeded(decoded)) {
            return decoded;
        }
    }
    else if ((entry->block_len > 0U) && (len > (size_t) entry->block_len)) {
        /* 块长度上限：与 DS 下载路径的 max_len 判定保持同一错误码。
           block_len == 0 表示变长且无上限（只读数组对象）。 */
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    }
    if (entry->validate != NULL) {
        return entry->validate(user, entry, data, len);
    }
    return iolink_result_ok((uint16_t) len);
}

iolink_result_t iolink_od_default(const iolink_od_entry_t* entry, uint8_t* out, size_t cap)
{
    if ((entry == NULL) || (out == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if (entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
        if (entry->block_default == NULL) {
            return iolink_result_ok(0U);
        }
        if (cap < (size_t) entry->block_len) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
        (void) memcpy(out, entry->block_default, entry->block_len);
        return iolink_result_ok(entry->block_len);
    }
    return iolink_od_encode(entry, entry->scalar_default, out, cap);
}

/** @brief 从 STORAGE 表项的应用变量复制线上字节。 */
static iolink_result_t od_storage_read(const iolink_od_entry_t* entry, uint8_t* out, size_t cap)
{
    const void* value = entry->u.storage.value;

    if (value == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if (entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
        if (entry->u.storage.str_len == NULL) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
        size_t n = (size_t) *entry->u.storage.str_len;
        if ((entry->block_len > 0U) && (n > (size_t) entry->block_len)) {
            n = (size_t) entry->block_len;
        }
        if (n > cap) {
            return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
        }
        if (n > 0U) {
            (void) memcpy(out, value, n);
        }
        return iolink_result_ok((uint16_t) n);
    }
    const iolink_od_codec_t* codec = iolink_od_codec(entry->datatype);
    if ((codec == NULL) || (cap < (size_t) codec->len)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    uint32_t raw = 0U;
    (void) memcpy(&raw, value, codec->len);
    return iolink_od_encode(entry, raw, out, cap);
}

/** @brief 把已校验的线上字节写入 STORAGE 表项的应用变量。 */
static iolink_result_t od_storage_apply(const iolink_od_entry_t* entry, const uint8_t* data,
                                        size_t len)
{
    if (entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
        if (entry->u.storage.str_len == NULL) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
        if ((entry->block_len > 0U) && (len > (size_t) entry->block_len)) {
            return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
        }
        if (len > 0U) {
            (void) memcpy((void*) entry->u.storage.value, data, len);
        }
        *entry->u.storage.str_len = (uint8_t) len;
        return iolink_result_ok(0U);
    }
    uint32_t raw = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &raw);
    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    const iolink_od_codec_t* codec = iolink_od_codec(entry->datatype);
    if (codec == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    (void) memcpy((void*) entry->u.storage.value, &raw, codec->len);
    return iolink_result_ok(0U);
}

/**
 * @brief 写入 STORAGE 表项，在 on_change 失败时回滚标量。
 *
 * 标量保存写前的原生字节（≤4 B）；OctetString 不做回滚（避免在主泵
 * 栈上复制最多 255 字节），拒绝语义应由 `validate` 钩子完成。
 */
static iolink_result_t od_storage_write(const iolink_od_entry_t* entry, void* user,
                                        const uint8_t* data, size_t len)
{
    const bool is_string = entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING;
    const iolink_od_codec_t* codec = NULL;
    uint8_t old_bytes[4] = {0U};

    if (entry->u.storage.value == NULL) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if (is_string) {
        if (entry->u.storage.str_len == NULL) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
    }
    else {
        codec = iolink_od_codec(entry->datatype);
        if (codec == NULL) {
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
        (void) memcpy(old_bytes, entry->u.storage.value, codec->len);
    }

    iolink_result_t result = od_storage_apply(entry, data, len);
    if (!iolink_result_succeeded(result)) {
        return result;
    }
    if (entry->on_change != NULL) {
        result = entry->on_change(user, entry, data, len);
        if (!iolink_result_succeeded(result)) {
            if (!is_string) {
                (void) memcpy((void*) entry->u.storage.value, old_bytes, codec->len);
            }
        }
    }
    return result;
}

/** @brief 标签写路径：写 RAM → on_change 通知。 */
static iolink_result_t od_tag_write(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                    uint8_t subindex, const uint8_t* data, size_t len, void* user)
{
    iolink_result_t result =
        iolink_params_ctx_write(ctx->params, entry->index, subindex, data, len);
    if (!iolink_result_succeeded(result)) {
        return result;
    }
    if (entry->on_change != NULL) {
        result = entry->on_change(user, entry, data, len);
    }
    return result;
}

iolink_result_t iolink_od_read_value(const iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                     uint8_t subindex, uint8_t* out, size_t cap)
{
    /* 子索引契约由 OD 层自己保证：调用方不必先查 max_subindex。 */
    if ((entry != NULL) && (subindex > entry->max_subindex)) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }
    if ((ctx == NULL) || (entry == NULL) || (out == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }

    iolink_result_t result;
    if (entry->handler == (uint8_t) IOLINK_OD_HANDLER_TAG) {
        if (ctx->params == NULL) {
            return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
        }
        result = iolink_params_ctx_read(ctx->params, entry->index, subindex, out, cap);
    }
    else if (entry->handler == (uint8_t) IOLINK_OD_HANDLER_NONE) {
        if (entry->backend == (uint8_t) IOLINK_OD_BACKEND_STORAGE) {
            result = od_storage_read(entry, out, cap);
        }
        else if ((entry->backend == (uint8_t) IOLINK_OD_BACKEND_HANDLER) && (ctx->app != NULL) &&
                 (entry->u.handler.read != NULL)) {
            result = entry->u.handler.read(ctx->app->user, entry, out, cap);
        }
        else {
            return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
        }
    }
    else {
        /* 内建处理器（事件/诊断/标识/直接参数）由 ISDU 层执行。 */
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if (!iolink_result_succeeded(result)) {
        return result;
    }
    if (entry->datatype != (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
        const iolink_od_codec_t* codec = iolink_od_codec(entry->datatype);
        if ((codec == NULL) || (result.len != codec->len)) {
            /* 后端返回的线长必须与表项 codec 一致。 */
            return iolink_result_error(IOLINK_RESULT_INVALID);
        }
    }
    else if (result.len > cap) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    return result;
}

iolink_result_t iolink_od_write_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                      uint8_t subindex, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (entry == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if (subindex > entry->max_subindex) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }

    void* user = (ctx->app != NULL) ? ctx->app->user : NULL;
    iolink_result_t result = iolink_od_check_value(entry, user, data, len);

    if (!iolink_result_succeeded(result)) {
        return result;
    }

    if (entry->write_semantics == (uint8_t) IOLINK_OD_WRITE_TRIGGER) {
        /* 触发语义：值不保存，动作本身由 on_change 实现。 */
        if (entry->on_change == NULL) {
            return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
        }
        return entry->on_change(user, entry, data, len);
    }

    if (entry->handler == (uint8_t) IOLINK_OD_HANDLER_TAG) {
        if (ctx->params == NULL) {
            return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
        }
        return od_tag_write(ctx, entry, subindex, data, len, user);
    }
    if (entry->handler == (uint8_t) IOLINK_OD_HANDLER_NONE) {
        if (entry->backend == (uint8_t) IOLINK_OD_BACKEND_STORAGE) {
            return od_storage_write(entry, user, data, len);
        }
        if ((entry->backend != (uint8_t) IOLINK_OD_BACKEND_HANDLER) || (ctx->app == NULL) ||
            (entry->u.handler.write == NULL)) {
            return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
        }
        result = entry->u.handler.write(ctx->app->user, entry, subindex, data, len);
    }
    else {
        /* 内建处理器（SystemCommand/DataStorage/事件/诊断）由 ISDU 层执行。 */
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if (!iolink_result_succeeded(result)) {
        return result;
    }
    if (entry->on_change != NULL) {
        result = entry->on_change(user, entry, data, len);
        if (!iolink_result_succeeded(result)) {
            return result;
        }
    }
    return iolink_result_ok(0U);
}

iolink_result_t iolink_od_restore_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                        uint8_t subindex, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (entry == NULL)) {
        return iolink_result_error(IOLINK_RESULT_INVALID);
    }
    if (subindex > entry->max_subindex) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    }
    if (entry->handler == (uint8_t) IOLINK_OD_HANDLER_TAG) {
        if (ctx->params == NULL) {
            return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
        }
        return iolink_params_ctx_write(ctx->params, entry->index, subindex, data, len);
    }
    if (entry->handler != (uint8_t) IOLINK_OD_HANDLER_NONE) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    if (entry->backend == (uint8_t) IOLINK_OD_BACKEND_STORAGE) {
        return od_storage_apply(entry, data, len);
    }
    if ((entry->backend == (uint8_t) IOLINK_OD_BACKEND_HANDLER) && (ctx->app != NULL) &&
        (entry->u.handler.write != NULL)) {
        return entry->u.handler.write(ctx->app->user, entry, subindex, data, len);
    }
    return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
}

/* -------------------------------------------------------------------------
 * 应用表自检：宏只能查常量交叉关系，指针关系在这里一次走表检查
 * ------------------------------------------------------------------------- */

#if IOLINK_FEATURE_VALIDATE

/** @brief 单个应用表项的自检；返回 0 或第一个问题。 */
static int od_validate_entry(const iolink_od_entry_t* entry)
{
    const bool writable = (entry->access & IOLINK_OD_ACCESS_WRITE) != 0U;

    if (entry->handler != (uint8_t) IOLINK_OD_HANDLER_NONE) {
        /* 应用表项只能使用自己的后端，不能复用内建处理器。 */
        return OD_VALIDATE_ERR_HANDLER_NONE;
    }
    if (entry->datatype >= (uint8_t) IOLINK_OD_DT_COUNT) {
        return OD_VALIDATE_ERR_DATATYPE;
    }
    if ((entry->access & (IOLINK_OD_ACCESS_READ | IOLINK_OD_ACCESS_WRITE)) == 0U) {
        return OD_VALIDATE_ERR_ACCESS;
    }
    if ((entry->write_semantics != (uint8_t) IOLINK_OD_WRITE_STORE) &&
        (entry->write_semantics != (uint8_t) IOLINK_OD_WRITE_VOLATILE) &&
        (entry->write_semantics != (uint8_t) IOLINK_OD_WRITE_TRIGGER)) {
        return OD_VALIDATE_ERR_SEMANTICS;
    }
    if ((entry->storage & (uint8_t) (~IOLINK_OD_STORAGE_DS)) != 0U) {
        return OD_VALIDATE_ERR_STORAGE_BITS;
    }
    if ((entry->write_semantics == (uint8_t) IOLINK_OD_WRITE_TRIGGER) && (entry->storage != 0U)) {
        return OD_VALIDATE_ERR_TRIGGER_STORAGE;
    }
    if (writable && (entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING) &&
        (entry->backend == (uint8_t) IOLINK_OD_BACKEND_STORAGE) && (entry->block_len == 0U)) {
        /* STORAGE 字节串的写入直接落到应用缓冲，没有上限会越界。 */
        return OD_VALIDATE_ERR_BYTES_UNBOUNDED;
    }
    if (!writable && ((entry->on_change != NULL) || (entry->validate != NULL))) {
        /* 只读对象永远走不到写路径，钩子是死配置。 */
        return OD_VALIDATE_ERR_RO_HOOK;
    }
    if (entry->datatype != (uint8_t) IOLINK_OD_DT_OCTET_STRING) {
        if (entry->range_min > entry->range_max) {
            return OD_VALIDATE_ERR_RANGE;
        }
        if ((entry->scalar_default < entry->range_min) ||
            (entry->scalar_default > entry->range_max)) {
            return OD_VALIDATE_ERR_DEFAULT_RANGE;
        }
    }
    if (entry->backend == (uint8_t) IOLINK_OD_BACKEND_STORAGE) {
        if (entry->u.storage.value == NULL) {
            return OD_VALIDATE_ERR_STORAGE_NULL;
        }
        if ((entry->datatype == (uint8_t) IOLINK_OD_DT_OCTET_STRING) &&
            (entry->u.storage.str_len == NULL)) {
            return OD_VALIDATE_ERR_STORAGE_NULL;
        }
    }
    else if (entry->backend == (uint8_t) IOLINK_OD_BACKEND_HANDLER) {
        if (((entry->access & IOLINK_OD_ACCESS_READ) != 0U) && (entry->u.handler.read == NULL)) {
            return OD_VALIDATE_ERR_HANDLER_READ;
        }
        if (writable && (entry->u.handler.write == NULL)) {
            return OD_VALIDATE_ERR_HANDLER_WRITE;
        }
    }
    else {
        return OD_VALIDATE_ERR_BACKEND;
    }
    return OD_VALIDATE_OK;
}

int od_validate_app(const iolink_od_app_t* app)
{
    if (app == NULL) {
        return OD_VALIDATE_OK;
    }
    if ((app->entries == NULL) && (app->count > 0U)) {
        return OD_VALIDATE_ERR_TABLE;
    }
    for (size_t i = 0U; i < app->count; i++) {
        const iolink_od_entry_t* entry = &app->entries[i];
        const int rc = od_validate_entry(entry);

        if (entry->index < 0x0040U) {
            /* 规范固定索引不允许被应用表占用；应用项在那里不可达。 */
            return OD_VALIDATE_ERR_STD_INDEX;
        }
        if (rc != OD_VALIDATE_OK) {
            return rc;
        }
        for (size_t j = 0U; j < i; j++) {
            if ((app->entries[j].index == entry->index) &&
                (app->entries[j].subindex == entry->subindex)) {
                return OD_VALIDATE_ERR_DUPLICATE;
            }
        }
    }
    return OD_VALIDATE_OK;
}

#endif /* IOLINK_FEATURE_VALIDATE */
