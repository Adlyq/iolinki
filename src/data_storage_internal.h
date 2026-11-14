/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DATA_STORAGE_INTERNAL_H
#define IOLINK_DATA_STORAGE_INTERNAL_H

#include "iolinki/config.h"

#if IOLINK_FEATURE_DATA_STORAGE && IOLINK_FEATURE_VALIDATE

#include "iolinki/data_storage.h"

/**
 * @file data_storage_internal.h
 * @brief PERSIST 参数镜像的初始化期容量自检（库内部）。
 *
 * 参数镜像缓冲固定为 `IOLINK_DS_IMAGE_MAX`；集合放不下时序列化会静默截断，
 * 把不完整的镜像写进 NVM。设备初始化用本文件在绑定子系统之前一次性算清。
 */

/** @brief PERSIST 参数镜像容量自检结果。 */
typedef enum
{
    DS_VALIDATE_OK = 0,
    DS_VALIDATE_ERR_IMAGE = -1 /**< 集合放不进 IOLINK_DS_IMAGE_MAX，或含无法定长的项。 */
} ds_validate_result_t;

/**
 * @brief 检查由对象字典派生的 PERSIST 集合能否装进参数镜像缓冲。
 *
 * @param app 应用对象字典（可为 NULL；标准表总是参与）。
 * @return ::DS_VALIDATE_OK 或 ::DS_VALIDATE_ERR_IMAGE。
 */
int ds_validate_persist(const iolink_od_app_t* app);

#endif /* IOLINK_FEATURE_DATA_STORAGE && IOLINK_FEATURE_VALIDATE */

#endif /* IOLINK_DATA_STORAGE_INTERNAL_H */
