/* SPDX-License-Identifier: GPL-3.0-or-later */

/**
 * @file conformance_device_od.c
 * @brief 一致性示例设备的应用对象字典（参数值的唯一扩展入口）。
 *
 * 0x0100-0x0113 是产品定义的设备专用参数：索引、线上类型、长度约束、
 * 访问锁与 DS 成员关系都只在这里描述一次。读取/写入值由后端在
 * @ref conformance_device_t 上完成，ISDU 与 DS 共用同一条路径。
 */

#include "conformance_device.h"

#include <string.h>

#include "iolinki/od.h"

/** @brief 固定长度字节串的校验：超长 0x33、不足 0x34。 */
static iolink_result_t exact_length(const iolink_od_entry_t* entry, size_t len)
{
    if (len > (size_t) entry->block_len) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    }
    if (len < (size_t) entry->block_len) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);
    }
    return iolink_result_ok((uint16_t) len);
}

static iolink_result_t validate_config1(void* user, const iolink_od_entry_t* entry,
                                        const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    return exact_length(entry, len);
}

static iolink_result_t validate_config3(void* user, const iolink_od_entry_t* entry,
                                        const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    /* block_len 已拒绝超长；这里只要求至少 1 字节。 */
    if (len == 0U) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN);
    }
    (void) entry;
    return iolink_result_ok((uint16_t) len);
}

static iolink_result_t validate_config4(void* user, const iolink_od_entry_t* entry,
                                        const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    return exact_length(entry, len);
}

static const iolink_od_entry_t k_conformance_od[] = {
    IOLINK_OD_STRING_EX(CONFORMANCE_CONFIG1_INDEX, IOLINK_OD_ACCESS_RW_LOCKED,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND,
                        CONFORMANCE_CONFIG1_MAX, "Config1", validate_config1, NULL),
    IOLINK_OD_SCALAR(CONFORMANCE_CONFIG2_INDEX, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW_LOCKED,
                     IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND, 0U,
                     0xFFFFU, 0U, "Config2"),
    IOLINK_OD_STRING_EX(CONFORMANCE_CONFIG3_INDEX, IOLINK_OD_ACCESS_RW_LOCKED,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND,
                        CONFORMANCE_CONFIG3_MAX, "Config3", validate_config3, NULL),
    IOLINK_OD_STRING_EX(CONFORMANCE_CONFIG4_INDEX0, IOLINK_OD_ACCESS_RW_LOCKED,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND,
                        CONFORMANCE_CONFIG4_SIZE, "Config4_0", validate_config4, NULL),
    IOLINK_OD_STRING_EX(CONFORMANCE_CONFIG4_INDEX1, IOLINK_OD_ACCESS_RW_LOCKED,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND,
                        CONFORMANCE_CONFIG4_SIZE, "Config4_1", validate_config4, NULL),
    IOLINK_OD_STRING_EX(CONFORMANCE_CONFIG4_INDEX2, IOLINK_OD_ACCESS_RW_LOCKED,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND,
                        CONFORMANCE_CONFIG4_SIZE, "Config4_2", validate_config4, NULL),
    IOLINK_OD_STRING_EX(CONFORMANCE_CONFIG4_INDEX3, IOLINK_OD_ACCESS_RW_LOCKED,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_DS, IOLINK_OD_HANDLER_BACKEND,
                        CONFORMANCE_CONFIG4_SIZE, "Config4_3", validate_config4, NULL),
};

/** @brief 把一个字节串复制到调用方缓冲；容量不足仍报 0x33。 */
static iolink_result_t copy_block(uint8_t* out, size_t cap, const uint8_t* src, size_t len)
{
    if (out == NULL) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
    if (cap < len) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN);
    }
    if (len > 0U) {
        (void) memcpy(out, src, len);
    }
    return iolink_result_ok((uint16_t) len);
}

// cppcheck-suppress constParameterCallback
static iolink_result_t conformance_od_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                           size_t cap)
{
    const conformance_device_t* app = (const conformance_device_t*) user;

    if (app == NULL) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
    switch (entry->index) {
        case CONFORMANCE_CONFIG1_INDEX:
            return copy_block(out, cap, app->config1, app->config1_len);
        case CONFORMANCE_CONFIG2_INDEX:
            return iolink_od_encode(entry, app->config2, out, cap);
        case CONFORMANCE_CONFIG3_INDEX:
            return copy_block(out, cap, app->config3, app->config3_len);
        default:
            break;
    }
    if ((entry->index >= CONFORMANCE_CONFIG4_INDEX0) &&
        (entry->index < (CONFORMANCE_CONFIG4_INDEX0 + CONFORMANCE_CONFIG4_COUNT))) {
        const size_t slot = (size_t) (entry->index - CONFORMANCE_CONFIG4_INDEX0);
        return copy_block(out, cap, app->config4[slot], app->config4_len[slot]);
    }
    return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
}

static iolink_result_t conformance_od_write(void* user, const iolink_od_entry_t* entry,
                                            uint8_t subindex, const uint8_t* data, size_t len)
{
    conformance_device_t* app = (conformance_device_t*) user;

    (void) subindex;
    if (app == NULL) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_IDX_NOT_AVAIL);
    }
    switch (entry->index) {
        case CONFORMANCE_CONFIG1_INDEX:
            (void) memcpy(app->config1, data, CONFORMANCE_CONFIG1_MAX);
            app->config1_len = CONFORMANCE_CONFIG1_MAX;
            return iolink_result_ok(0U);
        case CONFORMANCE_CONFIG2_INDEX: {
            uint32_t value = 0U;
            const iolink_result_t decoded = iolink_od_decode(entry, data, len, &value);
            if (!iolink_result_succeeded(decoded)) {
                return decoded;
            }
            app->config2 = (uint16_t) value;
            return iolink_result_ok(0U);
        }
        case CONFORMANCE_CONFIG3_INDEX:
            (void) memcpy(app->config3, data, len);
            app->config3_len = len;
            return iolink_result_ok(0U);
        default:
            break;
    }
    if ((entry->index >= CONFORMANCE_CONFIG4_INDEX0) &&
        (entry->index < (CONFORMANCE_CONFIG4_INDEX0 + CONFORMANCE_CONFIG4_COUNT))) {
        const size_t slot = (size_t) (entry->index - CONFORMANCE_CONFIG4_INDEX0);
        (void) memcpy(app->config4[slot], data, CONFORMANCE_CONFIG4_SIZE);
        app->config4_len[slot] = CONFORMANCE_CONFIG4_SIZE;
        return iolink_result_ok(0U);
    }
    return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
}

void conformance_od_init(iolink_od_app_t* app, conformance_device_t* device)
{
    if ((app == NULL) || (device == NULL)) {
        return;
    }
    app->entries = k_conformance_od;
    app->count = sizeof(k_conformance_od) / sizeof(k_conformance_od[0]);
    app->read = conformance_od_read;
    app->write = conformance_od_write;
    app->user = device;
}
