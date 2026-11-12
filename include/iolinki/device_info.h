/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DEVICE_INFO_H
#define IOLINK_DEVICE_INFO_H

#include <stdint.h>

/**
 * @file device_info.h
 * @brief 设备标识与强制 ISDU 索引
 *
 * 实现 IO-Link V1.1.5 规范所要求的强制索引。
 */

/**
 * @defgroup iolinki_device_info 设备标识
 * @brief 设备标识数据以及强制的身份 ISDU 索引。
 * @{
 */

/**
 * @brief 设备标识结构。
 *
 * 为你的特定设备配置这些值。每个字段对应一个强制或
 * 可选的 IO-Link 身份 ISDU 索引。
 */
typedef struct
{
    /* 强制 ID 索引（0x0010-0x0018） */
    const char* vendor_name;       /**< 厂商名称字符串（Index 0x0010）。 */
    const char* vendor_text;       /**< 厂商文本字符串（Index 0x0011）。 */
    const char* product_name;      /**< 产品名称字符串（Index 0x0012）。 */
    const char* product_id;        /**< 产品 ID 字符串（Index 0x0013）。 */
    const char* product_text;      /**< 产品文本字符串（Index 0x0014）。 */
    const char* serial_number;     /**< 序列号字符串（Index 0x0015）。 */
    const char* hardware_revision; /**< 硬件版本字符串（Index 0x0016）。 */
    const char* firmware_revision; /**< 固件版本字符串（Index 0x0017）。 */
    const char* product_uri;       /**< ProductURI 字符串（Index 0x001B，取决于 Profile）。 */
    const char* application_tag;   /**< 应用标签字符串（Index 0x0018，可选）。 */

    /* 设备 ID（Direct Parameter page 1，V1.1.5 Table B.1） */
    uint16_t vendor_id;              /**< Vendor ID（DPP1 0x07-0x08）。 */
    uint32_t device_id;              /**< Device ID（DPP1 0x09-0x0B，24 位）。 */
    uint16_t function_id;            /**< Function ID（ProfileCharacteristic 0x000D 的子字段）。 */
    uint16_t profile_characteristic; /**< Profile characteristic（Index 0x000D）。 */

    /* 系统信息 */
    uint8_t min_cycle_time;          /**< 最小周期时间（DPP1 0x02，Table B.3）。 */
    uint16_t revision_id;            /**< Revision ID（DPP1 0x04；0x0111 = 协议 v1.1）。 */
    uint8_t device_status;           /**< 设备状态（Index 0x0024）。 */
    uint16_t detailed_device_status; /**< 详细设备状态（Index 0x0025，可选）。 */

    /* 访问控制 */
    uint16_t access_locks; /**< 设备访问锁位掩码（Index 0x000C）。 */
} iolink_device_info_t;

/**
 * @brief 每设备的标识运行时上下文。
 *
 * 用可变的运行时值（应用标签、访问锁）和出厂默认值
 * 包装静态配置的身份。
 */
typedef struct
{
    const iolink_device_info_t* configured; /**< 静态配置的身份。 */
    iolink_device_info_t defaults;          /**< 出厂默认身份快照。 */
    char application_tag[33];               /**< 运行时应用标签（32 字节 + NUL）。 */
    uint16_t access_locks;                  /**< 运行时设备访问锁值。 */
} iolink_device_info_ctx_t;

/**
 * @brief 初始化设备信息上下文。
 * @param ctx         要初始化的上下文。
 * @param configured  要绑定的静态设备身份（必须保持有效）。
 */
void iolink_device_info_ctx_init(iolink_device_info_ctx_t* ctx,
                                 const iolink_device_info_t* configured);

/**
 * @brief 从上下文获取有效的设备身份。
 * @param ctx 设备信息上下文。
 * @return 指向有效设备身份的指针。
 */
const iolink_device_info_t* iolink_device_info_ctx_get(const iolink_device_info_ctx_t* ctx);

/**
 * @brief 在上下文上设置运行时应用标签（Index 0x0018）。
 * @param ctx  设备信息上下文。
 * @param tag  字符串数据（最多 32 字节）。
 * @param len  @p tag 的长度（字节）。
 * @return 成功时为 0，出错时为负值。
 */
int iolink_device_info_ctx_set_application_tag(iolink_device_info_ctx_t* ctx, const char* tag,
                                               uint8_t len);

/**
 * @brief 从上下文获取设备访问锁（Index 0x000C）。
 * @param ctx 设备信息上下文。
 * @return 16 位访问锁值。
 */
uint16_t iolink_device_info_ctx_get_access_locks(const iolink_device_info_ctx_t* ctx);

/**
 * @brief 在上下文上设置设备访问锁（Index 0x000C）。
 * @param ctx    设备信息上下文。
 * @param locks  16 位访问锁值。
 */
void iolink_device_info_ctx_set_access_locks(iolink_device_info_ctx_t* ctx, uint16_t locks);

/**
 * @brief 初始化设备信息
 * @param info 指向设备信息结构的指针
 */
void iolink_device_info_init(const iolink_device_info_t* info);

/**
 * @brief 获取设备信息
 * @return 指向设备信息结构的指针
 */
const iolink_device_info_t* iolink_device_info_get(void);

/**
 * @brief 设置应用标签（Index 0x18）
 * @param tag 字符串数据（最多 32 字节）
 * @param len 字符串长度
 * @return 成功时为 0，出错时为负值
 */
int iolink_device_info_set_application_tag(const char* tag, uint8_t len);

/**
 * @brief 获取设备访问锁（Index 0x000C）
 * @return 16 位访问锁值
 */
uint16_t iolink_device_info_get_access_locks(void);

/**
 * @brief 设置设备访问锁（Index 0x000C）
 * @param locks 16 位访问锁值
 */
void iolink_device_info_set_access_locks(uint16_t locks);

/** @} */ /* end of iolinki_device_info */

#endif  // IOLINK_DEVICE_INFO_H
