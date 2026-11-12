/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_OD_H
#define IOLINK_OD_H

#include "iolinki/config.h"
#include "iolinki/params.h"
#include "iolinki/result.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file od.h
 * @brief IO-Link 对象字典（OD）：参数知识的唯一事实源（P4）。
 *
 * 一个参数的全部已知信息只描述一次：
 *  - `index`/`subindex` 与数组上界；
 *  - datatype codec ID（长度/字节序/范围/错误码唯一的实现）；
 *  - 访问权限与访问锁位；
 *  - 写语义（STORE/TRIGGER/VOLATILE）与 storage 标志（VOLATILE/DS/NVM）；
 *  - 默认值与可选的 validate/on_change 钩子。
 *
 * ISDU 分发、数据存储成员判定与 IODD 生成都从这张表派生，不再各自
 * 维护一份参数清单。
 *
 * 标准对象（0x0000-0x003F 以及厂商错误统计 0x0040）由协议栈内建的
 * const 表描述；应用参数通过 @ref iolink_od_app_t 绑定自己的 const 表
 * 和 get/set 后端。新增应用参数 = 增加一行表项 + 一个 get/set 分支。
 */

/**
 * @defgroup iolinki_od 对象字典（OD）
 * @brief const 对象字典表、datatype codec 与参数值访问层。
 * @{
 */

/**
 * @brief datatype codec ID。
 *
 * 每个 ID 在 @ref iolink_od_codec_t 表中恰好有一条记录，描述线长、
 * 字节序与原生取值范围。长度/范围错误码只在该处产生。
 */
typedef enum
{
    IOLINK_OD_DT_U8 = 0U,           /**< UIntegerT8，1 字节大端。 */
    IOLINK_OD_DT_U16 = 1U,          /**< UIntegerT16，2 字节大端。 */
    IOLINK_OD_DT_U32 = 2U,          /**< UIntegerT32，4 字节大端。 */
    IOLINK_OD_DT_BOOL = 3U,         /**< BooleanT，1 字节，取值 0/1。 */
    IOLINK_OD_DT_OCTET_STRING = 4U, /**< OctetStringT，变长（上限由表项给出）。 */
    IOLINK_OD_DT_COUNT = 5U         /**< codec 表条目数（非数据类型）。 */
} iolink_od_datatype_t;

/** 对象可读。 */
#define IOLINK_OD_ACCESS_READ 0x01U
/** 对象可写。 */
#define IOLINK_OD_ACCESS_WRITE 0x02U
/** 写操作受 DeviceAccessLocks 的 Parameter 位保护（B.2.4）。 */
#define IOLINK_OD_ACCESS_LOCK_PARAM 0x04U
/** 只读对象。 */
#define IOLINK_OD_ACCESS_RO (IOLINK_OD_ACCESS_READ)
/** 读写对象。 */
#define IOLINK_OD_ACCESS_RW (IOLINK_OD_ACCESS_READ | IOLINK_OD_ACCESS_WRITE)
/** 读写对象，写操作受 Parameter 访问锁保护。 */
#define IOLINK_OD_ACCESS_RW_LOCKED (IOLINK_OD_ACCESS_RW | IOLINK_OD_ACCESS_LOCK_PARAM)
/** 只写对象。 */
#define IOLINK_OD_ACCESS_WO (IOLINK_OD_ACCESS_WRITE)
/** 只写对象，写操作受 Parameter 访问锁保护。 */
#define IOLINK_OD_ACCESS_WO_LOCKED (IOLINK_OD_ACCESS_WO | IOLINK_OD_ACCESS_LOCK_PARAM)

/** 写语义：写入落到后端，并在 storage 为 NVM 时持久化。 */
#define IOLINK_OD_WRITE_STORE 0U
/** 写语义：写入只改 RAM，不触发持久化。 */
#define IOLINK_OD_WRITE_VOLATILE 1U
/** 写语义：写入是动作触发（如 teach），值本身不保存。 */
#define IOLINK_OD_WRITE_TRIGGER 2U

/** storage 标志：不属于持久化集合。 */
#define IOLINK_OD_STORAGE_VOLATILE 0U
/** storage 标志：属于 Data Storage 参数集（10.3）。 */
#define IOLINK_OD_STORAGE_DS 1U
/** storage 标志：由参数管理器持久化到设备 NVM。 */
#define IOLINK_OD_STORAGE_NVM 2U

/** 表项标志：使用块（block）参数传输。 */
#define IOLINK_OD_FLAG_BLOCK 0x01U
/** 表项标志：值可流式读取（P8 预留）。 */
#define IOLINK_OD_FLAG_STREAM 0x02U

/**
 * @brief 内建处理器 ID。
 *
 * 标准对象与厂商错误统计需要设备上下文（事件/DLL/标识/直接参数），
 * 由 ISDU 层内建处理器执行；表项只保存处理器 ID，因此表保持 const
 * 且不依赖任何上下文。应用参数使用 @ref IOLINK_OD_HANDLER_BACKEND。
 */
typedef enum
{
    IOLINK_OD_HANDLER_BACKEND = 0U,         /**< 应用 get/set 后端。 */
    IOLINK_OD_HANDLER_TAG = 1U,             /**< 应用/功能/位置标签（参数管理器）。 */
    IOLINK_OD_HANDLER_SYSTEM_COMMAND = 2U,  /**< 0x0002 SystemCommand。 */
    IOLINK_OD_HANDLER_DATA_STORAGE = 3U,    /**< 0x0003 DataStorage。 */
    IOLINK_OD_HANDLER_ACCESS_LOCKS = 4U,    /**< 0x000C DeviceAccessLocks。 */
    IOLINK_OD_HANDLER_PROFILE = 5U,         /**< 0x000D ProfileCharacteristic。 */
    IOLINK_OD_HANDLER_IDENT_STRING = 6U,    /**< 0x0010-0x0017/0x001B 标识字符串。 */
    IOLINK_OD_HANDLER_ERROR_COUNT = 7U,     /**< 0x0020 ErrorCount。 */
    IOLINK_OD_HANDLER_DEVICE_STATUS = 8U,   /**< 0x0024 DeviceStatus。 */
    IOLINK_OD_HANDLER_DETAILED_STATUS = 9U, /**< 0x0025 DetailedDeviceStatus。 */
    IOLINK_OD_HANDLER_PD_INPUT = 10U,       /**< 0x0028 ProcessDataInput。 */
    IOLINK_OD_HANDLER_PD_OUTPUT = 11U,      /**< 0x0029 ProcessDataOutput。 */
    IOLINK_OD_HANDLER_ERROR_STATS = 12U,    /**< 0x0040 厂商错误统计。 */
    IOLINK_OD_HANDLER_DIRECT_PARAM_1 = 13U, /**< 0x0000 Direct Parameter page 1。 */
    IOLINK_OD_HANDLER_DIRECT_PARAM_2 = 14U  /**< 0x0001 Direct Parameter page 2。 */
} iolink_od_handler_t;

struct iolink_od_entry;
typedef struct iolink_od_entry iolink_od_entry_t;

/**
 * @brief 写值校验钩子：范围/交叉约束/长度例外。
 *
 * @param user  绑定后端的不透明指针（应用表为应用值，标准标签为 NULL）。
 * @param entry 被校验的表项。
 * @param data  待写入的线上字节。
 * @param len   @p data 的字节数。
 * @return 结果模型：0 接受；正方 ISDU ErrorCode 原样上送。
 */
typedef iolink_result_t (*iolink_od_validate_fn)(void* user, const iolink_od_entry_t* entry,
                                                 const uint8_t* data, size_t len);

/**
 * @brief 写后动作钩子（TRIGGER 写语义的唯一动作，或 STORE/VOLATILE 写后通知）。
 *
 * @param user  绑定后端的不透明指针。
 * @param entry 被写入的表项。
 * @param data  已接受的线上字节。
 * @param len   @p data 的字节数。
 * @return 结果模型：0 成功；正方 ISDU ErrorCode 原样上送。
 */
typedef iolink_result_t (*iolink_od_change_fn)(void* user, const iolink_od_entry_t* entry,
                                               const uint8_t* data, size_t len);

/**
 * @brief 一个对象字典表项。
 *
 * 设计约束：表示静态知识的字段全部为值语义，使整张表可以是 `const`
 * 并放在 ROM。运行期取值由 @ref iolink_od_app_t 的 get/set 后端或
 * 内建处理器完成。
 */
struct iolink_od_entry
{
    uint16_t index;                 /**< ISDU 索引。 */
    uint8_t subindex;               /**< 表项描述的子索引（单元素对象为 0）。 */
    uint8_t max_subindex;           /**< 可接受的最大子索引（0 = 仅 sub 0）。 */
    uint8_t datatype;               /**< @ref iolink_od_datatype_t codec ID。 */
    uint8_t access;                 /**< IOLINK_OD_ACCESS_* 位掩码。 */
    uint8_t write_semantics;        /**< IOLINK_OD_WRITE_*。 */
    uint8_t storage;                /**< IOLINK_OD_STORAGE_*。 */
    uint8_t flags;                  /**< IOLINK_OD_FLAG_*。 */
    uint8_t handler;                /**< @ref iolink_od_handler_t。 */
    uint8_t block_len;              /**< OctetString 长度上限/IODD fixedLength。 */
    uint32_t range_min;             /**< 标量下限（含）。 */
    uint32_t range_max;             /**< 标量上限（含）。 */
    uint32_t scalar_default;        /**< 标量默认值。 */
    const void* block_default;      /**< OctetString 默认值字节（可为 NULL）。 */
    const char* name;               /**< 对象名称（IODD/文档）。 */
    iolink_od_validate_fn validate; /**< 写校验钩子（可为 NULL）。 */
    iolink_od_change_fn on_change;  /**< 写后/触发钩子（可为 NULL）。 */
};

/**
 * @brief 带钩子的标量表项初始化宏。
 */
#define IOLINK_OD_SCALAR_EX(index_, datatype_, access_, semantics_, storage_, handler_, low_, \
                            high_, def_, name_, validate_, change_)                           \
    {.index = (index_),                                                                       \
     .subindex = 0U,                                                                          \
     .max_subindex = 0U,                                                                      \
     .datatype = (datatype_),                                                                 \
     .access = (access_),                                                                     \
     .write_semantics = (semantics_),                                                         \
     .storage = (storage_),                                                                   \
     .flags = 0U,                                                                             \
     .handler = (handler_),                                                                   \
     .block_len = 0U,                                                                         \
     .range_min = (low_),                                                                     \
     .range_max = (high_),                                                                    \
     .scalar_default = (def_),                                                                \
     .block_default = NULL,                                                                   \
     .name = (name_),                                                                         \
     .validate = (validate_),                                                                 \
     .on_change = (change_)}

/**
 * @brief 标量表项初始化宏（新增参数的一行写法）。
 */
#define IOLINK_OD_SCALAR(index_, datatype_, access_, semantics_, storage_, handler_, low_, high_, \
                         def_, name_)                                                             \
    IOLINK_OD_SCALAR_EX(index_, datatype_, access_, semantics_, storage_, handler_, low_, high_,  \
                        def_, name_, NULL, NULL)

/**
 * @brief 字节串表项初始化宏（新增参数的一行写法）。
 */
#define IOLINK_OD_STRING(index_, access_, semantics_, storage_, handler_, block_len_, name_) \
    {.index = (index_),                                                                      \
     .subindex = 0U,                                                                         \
     .max_subindex = 0U,                                                                     \
     .datatype = IOLINK_OD_DT_OCTET_STRING,                                                  \
     .access = (access_),                                                                    \
     .write_semantics = (semantics_),                                                        \
     .storage = (storage_),                                                                  \
     .flags = 0U,                                                                            \
     .handler = (handler_),                                                                  \
     .block_len = (block_len_),                                                              \
     .range_min = 0U,                                                                        \
     .range_max = 0U,                                                                        \
     .scalar_default = 0U,                                                                   \
     .block_default = NULL,                                                                  \
     .name = (name_),                                                                        \
     .validate = NULL,                                                                       \
     .on_change = NULL}

/**
 * @brief 读取一个参数值的后端。
 *
 * @param user  应用不透明指针
 * @param entry 命中的表项
 * @param out   接收线上字节的缓冲区
 * @param cap   @p out 的容量
 * @return 结果模型：成功时 `len` 为写出的字节数。
 */
typedef iolink_result_t (*iolink_od_read_fn)(void* user, const iolink_od_entry_t* entry,
                                             uint8_t* out, size_t cap);

/**
 * @brief 写入一个参数值的后端。
 *
 * 输入已经通过线长/范围/validate 校验，后端只负责应用取值。
 */
typedef iolink_result_t (*iolink_od_write_fn)(void* user, const iolink_od_entry_t* entry,
                                              const uint8_t* data, size_t len);

/**
 * @brief 应用对象字典：const 表 + get/set 后端。
 */
typedef struct
{
    const iolink_od_entry_t* entries; /**< 应用表项数组。 */
    size_t count;                     /**< 表项数量。 */
    iolink_od_read_fn read;           /**< 读值后端（可为 NULL）。 */
    iolink_od_write_fn write;         /**< 写值后端（可为 NULL）。 */
    void* user;                       /**< 传回后端与钩子的不透明指针。 */
} iolink_od_app_t;

/**
 * @brief 值访问上下文：把标准对象的参数后端与应用后端组合起来。
 */
typedef struct
{
    const iolink_od_app_t* app;  /**< 应用表与后端（可为 NULL）。 */
    iolink_params_ctx_t* params; /**< 标签对象的值后端（可为 NULL）。 */
} iolink_od_ctx_t;

/**
 * @brief datatype codec 描述。
 */
typedef struct
{
    uint8_t len;     /**< 定长字节数；0 表示变长（OctetString）。 */
    bool big_endian; /**< 线上字节序。 */
    uint32_t max;    /**< 该类型的原生上限。 */
} iolink_od_codec_t;

/** @brief 取 datatype 的 codec 描述；越界返回 NULL。 */
const iolink_od_codec_t* iolink_od_codec(uint8_t datatype);

/**
 * @brief 校验一个待写入值：线长、标量范围与表项 validate 钩子。
 *
 * @param entry 表项
 * @param user  钩子用户指针
 * @param data  线上字节
 * @param len   @p data 字节数
 * @return 0 接受；0x33 长度不符；0x30 越界；负值为内部错误。
 */
iolink_result_t iolink_od_check_value(const iolink_od_entry_t* entry, void* user,
                                      const uint8_t* data, size_t len);

/**
 * @brief 把标量线上字节解码为原生值。
 *
 * @return 成功时 `len` 为消耗的字节数；非标量或长度不符时返回错误。
 */
iolink_result_t iolink_od_decode(const iolink_od_entry_t* entry, const uint8_t* data, size_t len,
                                 uint32_t* value);

/**
 * @brief 把标量原生值编码为线上字节（大端）。
 *
 * @return 成功时 `len` 为写入的字节数。
 */
iolink_result_t iolink_od_encode(const iolink_od_entry_t* entry, uint32_t value, uint8_t* out,
                                 size_t cap);

/**
 * @brief 读取表项默认值的线上字节。
 */
iolink_result_t iolink_od_default(const iolink_od_entry_t* entry, uint8_t* out, size_t cap);

/** @brief 协议栈标准 OD 表项数量。 */
size_t iolink_od_count(void);

/** @brief 取第 @p i 个标准表项；越界返回 NULL。 */
const iolink_od_entry_t* iolink_od_at(size_t i);

/**
 * @brief 按索引查表项：先标准表，再应用表。
 *
 * 标准对象的索引是规范固定值，不允许被应用表覆盖；应用参数使用
 * 设备专用索引范围（0x0040-0x00FE、0x0100+）。
 *
 * @param app      应用表（可为 NULL）
 * @param index    ISDU 索引
 * @param subindex ISDU 子索引（用于多元素对象的精确匹配）
 * @return 命中的表项；未知索引返回 NULL
 */
const iolink_od_entry_t* iolink_od_find(const iolink_od_app_t* app, uint16_t index,
                                        uint8_t subindex);

/**
 * @brief 只在应用表中查找表项。
 *
 * ISDU 分发用它把"应用声明的索引"置于厂商服务之前。
 */
const iolink_od_entry_t* iolink_od_find_app(const iolink_od_app_t* app, uint16_t index,
                                            uint8_t subindex);

/** @brief 标准表中 storage 为 DS 的表项数量。 */
size_t iolink_od_ds_count(void);

/** @brief 取第 @p i 个 DS 表项；越界返回 NULL。 */
const iolink_od_entry_t* iolink_od_ds_at(size_t i);

/** @brief index/subindex 是否属于标准 DS 参数集。 */
bool iolink_od_is_ds(uint16_t index, uint8_t subindex);

/**
 * @brief 通过表项读取一个值。
 *
 * 标签对象经参数管理器，其余经应用 get 后端。
 */
iolink_result_t iolink_od_read_value(const iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                     uint8_t subindex, uint8_t* out, size_t cap);

/**
 * @brief 通过表项写入一个值。
 *
 * 顺序固定为：线长/范围/validate 钩子 -> 语义动作 -> on_change 钩子。
 * TRIGGER 语义不写后端，只执行 on_change。
 */
iolink_result_t iolink_od_write_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                      const uint8_t* data, size_t len);

/** @} */ /* end of iolinki_od */

#endif  // IOLINK_OD_H
