/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_STORAGE_H
#define IOLINK_STORAGE_H

#include "iolinki/config.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @file storage.h
 * @brief 统一的非易失性存储后端与持久化区域布局。
 *
 * 应用通过 iolink_device_config_t::storage 恰好提供一个
 * iolink_storage_api_t。协议栈使用固定的编译期区域布局
 * （相对偏移）访问它；应用在回调内部自行添加
 * 物理基址（例如 EEPROM 地址）。
 */

/** @brief 持久化写入的提交策略。 */
typedef enum
{
    IOLINK_PERSIST_NONE = 0U, /**< 仅 RAM。 */
    IOLINK_PERSIST_SYNC = 1U, /**< 在调用返回前提交。 */
    IOLINK_PERSIST_ASYNC = 2U /**< 排队提交；由回调负责复制缓冲区。 */
} iolink_persist_t;

/**
 * @brief 统一的非易失性存储后端。
 *
 * 所有偏移都相对于应用所拥有的存储区域。对于 ASYNC 策略，
 * 回调必须非阻塞；对于 SYNC，回调必须同步完成访问。
 * @p user 是配置中的应用指针。
 */
typedef struct
{
    void* user; /**< 不透明的应用指针。 */
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
    int (*erase)(void* user, uint32_t offset, size_t len);
} iolink_storage_api_t;

/* --- 持久化区域布局（相对偏移） ------------------------------------------
 *
 * PARAMS | DS image | DS upload flag
 *
 * 布局在编译期固定，因此应用可以使用静态断言
 * 确定其 NVM 区域的大小：
 *     _Static_assert(sizeof(nvm_region) >= IOLINK_STORAGE_TOTAL_SIZE, "...");
 *
 * 版本规则：
 *  - IOLINK_STORAGE_MAGIC 标识由本协议栈写入的区域。记录布局
 *    （字段、顺序、大小）发生变化时，必须修改 magic，以免
 *    旧映像被新字段位置误解析。
 *  - IOLINK_STORAGE_VERSION 标识同一 magic 下的布局，任何保留
 *    magic 不变的布局变化都必须递增它。版本不匹配时会丢弃
 *    该记录（目前尚无迁移回调）。
 */

#define IOLINK_STORAGE_MAGIC 0x494F4C32UL /* "IOL2"：布局携带版本信息 */
#define IOLINK_STORAGE_VERSION 1U         /* 记录布局变化时递增 */

/**
 * @brief 持久化记录的 Fletcher-32 校验和。
 *
 * 检测 magic/version 无法发现的位翻转和撕裂写入。各和
 * 对 65535 取模，使结果能够放入 uint32_t。
 */
static inline uint32_t iolink_storage_checksum(const uint8_t* data, size_t len)
{
    uint32_t sum1 = 0U;
    uint32_t sum2 = 0U;
    if (data == NULL) {
        return 0U;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (sum1 + (uint32_t) data[i]) % 65535U;
        sum2 = (sum2 + sum1) % 65535U;
    }
    return (sum2 << 16U) | sum1;
}

/** @brief 持久化参数记录（三个标识标签）。 */
typedef struct
{
    uint32_t magic;           /**< IOLINK_STORAGE_MAGIC。 */
    uint16_t version;         /**< IOLINK_STORAGE_VERSION。 */
    uint16_t reserved;        /**< 对齐 / 预留未来使用。 */
    char application_tag[33]; /**< ApplicationSpecificTag，以 NUL 结尾。 */
    char function_tag[33];    /**< FunctionTag，以 NUL 结尾。 */
    char location_tag[33];    /**< LocationTag，以 NUL 结尾。 */
    uint32_t checksum;        /**< 对前面各字段计算的 Fletcher-32。 */
} iolink_params_nvm_t;

#define IOLINK_STORAGE_ALIGN4(x) (((x) + 3U) & ~3U)

#define IOLINK_STORAGE_PARAMS_BASE 0U
#define IOLINK_STORAGE_PARAMS_SIZE IOLINK_STORAGE_ALIGN4((uint32_t) sizeof(iolink_params_nvm_t))
#define IOLINK_STORAGE_DS_BASE IOLINK_STORAGE_PARAMS_SIZE
/** DS 头：magic(4) + version(2) + length(2) + checksum(4)，其后为映像。 */
#define IOLINK_STORAGE_DS_HDR_SIZE 12U
#define IOLINK_STORAGE_DS_SIZE (IOLINK_STORAGE_DS_HDR_SIZE + (uint32_t) IOLINK_DS_IMAGE_MAX)
#define IOLINK_STORAGE_FLAG_BASE (IOLINK_STORAGE_DS_BASE + IOLINK_STORAGE_DS_SIZE)
#define IOLINK_STORAGE_FLAG_SIZE 4U
#define IOLINK_STORAGE_TOTAL_SIZE \
    IOLINK_STORAGE_ALIGN4(IOLINK_STORAGE_FLAG_BASE + IOLINK_STORAGE_FLAG_SIZE)

/* DS 区域必须紧跟参数记录，且不得重叠。 */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(IOLINK_STORAGE_PARAMS_SIZE >= sizeof(iolink_params_nvm_t),
               "params region smaller than its record");
_Static_assert(IOLINK_STORAGE_TOTAL_SIZE >= IOLINK_STORAGE_FLAG_BASE + IOLINK_STORAGE_FLAG_SIZE,
               "storage region layout overflow");
#endif

#endif /* IOLINK_STORAGE_H */
