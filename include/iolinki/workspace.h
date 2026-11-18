/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_WORKSPACE_H
#define IOLINK_WORKSPACE_H

#include "iolinki/config.h"
#include <stddef.h>
#include <stdint.h>

#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file workspace.h
 * @brief 协议栈传输缓冲工作区。
 *
 * 工作区把一次事务内的 scratch 缓冲与长期状态分开：
 *
 *  - ISDU 请求、载荷、应答与成帧缓冲只在一次 ISDU 事务内有效；
 *  - DS 镜像/下载暂存只在序列化、上传或下载期间有效。
 *
 * 长期状态（上下文、校验和、状态机、统计）仍留在各自的上下文结构里。
 * 调用方可以把工作区放在 DTCM、外部 RAM 或另一块单独的内存区，也可以
 * 让协议栈使用上下文内嵌的默认存储（不提供工作区时）。
 *
 * 工作区内容不跨事务保存，初始化时清零即可；`IOLINK_STATIC_ASSERT` 锁定
 * 每个子缓冲的容量，任何尺寸变化都必须同步 `tests/test_ctx_sizes.c` 与
 * `docs/MEMORY_GUIDE.md`。
 */

/**
 * @defgroup iolinki_workspace 工作区
 * @brief 可选的传输 scratch 缓冲容器。
 * @{
 */

/**
 * @brief ISDU 事务 scratch 缓冲。
 *
 * 四个缓冲分别对应传输层的请求八位组、解码后的载荷、应用层应答载荷
 * 以及最终成帧的应答。成帧应答只有**一份**：租约模型保证数据 ISR 在
 * 缓冲被重新构建前已经释放（见 `docs/MEMORY_GUIDE.md` 的租约规则）。
 */
typedef struct
{
    uint8_t request[IOLINK_ISDU_BUFFER_SIZE + 6U]; /**< 原始 ISDU 请求（含 CHKPDU） */
    uint8_t payload[IOLINK_ISDU_BUFFER_SIZE];      /**< 解码后的请求载荷 */
    uint8_t response[IOLINK_ISDU_BUFFER_SIZE];     /**< 应用层应答载荷 */
    uint8_t frame[IOLINK_ISDU_BUFFER_SIZE + 3U];   /**< 成帧应答（I-Service/Length/CHKPDU） */
} iolink_isdu_scratch_t;

/**
 * @brief 协议栈 scratch 工作区。
 *
 * 由调用方分配并保持有效；`iolink_device_config_t::workspace` 把它绑定到
 * 设备上下文。绑定后 ISDU/DS 不再使用上下文内嵌的默认缓冲。
 */
typedef struct
{
    iolink_isdu_scratch_t isdu; /**< ISDU 传输 scratch */
#if IOLINK_FEATURE_DATA_STORAGE
    uint8_t ds_stage[IOLINK_DS_IMAGE_MAX]; /**< DS 镜像序列化/下载暂存 */
#endif
} iolink_workspace_t;

/**
 * @brief 声明一个静态工作区实例。
 *
 * @param name C 变量名。
 */
#define IOLINK_WORKSPACE_DECLARE(name) iolink_workspace_t name

/** @} */ /* end of iolinki_workspace */

IOLINK_ASSUME_NONNULL_END
#endif /* IOLINK_WORKSPACE_H */
