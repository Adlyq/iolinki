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
 * iolink_storage_api_t。协议栈只写两个区域：
 *
 *     PARAMS（参数记录镜像） | DS upload flag
 *
 * 参数值以对象字典 `PERSIST` 表项为准，序列化后写进 PARAMS 区；
 * Data Storage 映像与校验和按需从对象字典生成，不单独落盘。应用在
 * 回调内部自行添加物理基址（例如 EEPROM 地址）。
 */

/**
 * @brief 统一的非易失性存储后端。
 *
 * 所有偏移都相对于应用所拥有的存储区域。@p write 必须在返回前完成
 * （或自行复制缓冲）；@p user 是配置中的应用指针。
 */
typedef struct
{
    void* user; /**< 不透明的应用指针。 */
    int (*read)(void* user, uint32_t offset, uint8_t* buf, size_t len);
    int (*write)(void* user, uint32_t offset, const uint8_t* buf, size_t len);
} iolink_storage_api_t;

/* --- 持久化区域布局（相对偏移） ------------------------------------------
 *
 * PARAMS | DS upload flag
 *
 * 布局在编译期固定，因此应用可以使用静态断言
 * 确定其 NVM 区域的大小：
 *     _Static_assert(sizeof(nvm_region) >= IOLINK_STORAGE_TOTAL_SIZE, "...");
 *
 * 版本规则：
 *  - IOLINK_STORAGE_MAGIC 标识由本协议栈写入的区域；记录布局
 *    （字段、顺序、大小）变化时必须修改 magic，避免旧映像被新
 *    字段位置误解析。
 *  - IOLINK_STORAGE_VERSION 标识同一 magic 下的布局；版本不匹配时
 *    丢弃该记录（目前没有迁移回调）。
 */

#define IOLINK_STORAGE_MAGIC 0x494F4C33UL /* "IOL3"：单参数镜像布局 */
#define IOLINK_STORAGE_VERSION 1U         /* 记录布局变化时递增 */

/**
 * @brief 持久化记录的 Fletcher-32 校验和。
 *
 * 检测 magic/version 无法发现的位翻转和撕裂写入。各和对 65535
 * 取模，使结果能够放入 uint32_t。
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

#define IOLINK_STORAGE_ALIGN4(x) (((x) + 3U) & ~3U)

/** @brief 记录镜像头：magic(4) + version(2) + length(2) + checksum(4)。 */
#define IOLINK_STORAGE_HDR_SIZE 12U

#define IOLINK_STORAGE_PARAMS_BASE 0U
#define IOLINK_STORAGE_PARAMS_SIZE \
    IOLINK_STORAGE_ALIGN4((uint32_t) IOLINK_STORAGE_HDR_SIZE + (uint32_t) IOLINK_DS_IMAGE_MAX)
#define IOLINK_STORAGE_FLAG_BASE IOLINK_STORAGE_PARAMS_SIZE
#define IOLINK_STORAGE_FLAG_SIZE 4U
#define IOLINK_STORAGE_TOTAL_SIZE \
    IOLINK_STORAGE_ALIGN4(IOLINK_STORAGE_FLAG_BASE + IOLINK_STORAGE_FLAG_SIZE)

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(IOLINK_STORAGE_PARAMS_SIZE >= IOLINK_STORAGE_HDR_SIZE,
               "params region smaller than its header");
_Static_assert(IOLINK_STORAGE_TOTAL_SIZE >= IOLINK_STORAGE_FLAG_BASE + IOLINK_STORAGE_FLAG_SIZE,
               "storage region layout overflow");
#endif

#endif /* IOLINK_STORAGE_H */
