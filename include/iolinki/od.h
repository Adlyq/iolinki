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
#include "iolinki/result.h"
#include "iolinki/types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file od.h
 * @brief IO-Link 对象字典（OD）：参数知识的唯一事实源。
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

/** 写语义：写入落到后端；持久化由表项的 on_change 通知应用负责。 */
#define IOLINK_OD_WRITE_STORE 0U
/** 写语义：写入只改 RAM。 */
#define IOLINK_OD_WRITE_VOLATILE 1U
/** 写语义：写入是动作触发（如 teach），值本身不保存。 */
#define IOLINK_OD_WRITE_TRIGGER 2U

/** storage 位掩码：不属于 Data Storage 参数集。 */
#define IOLINK_OD_STORAGE_VOLATILE 0U
/** storage 位掩码：属于 Data Storage 参数集（10.3）。 */
#define IOLINK_OD_STORAGE_DS (1U << 0U)

/** 表项标志：使用块（block）参数传输。 */
#define IOLINK_OD_FLAG_BLOCK 0x01U
/** 表项标志：值可流式读取（预留）。 */
#define IOLINK_OD_FLAG_STREAM 0x02U

/**
 * @brief 内建处理器 ID。
 *
 * 标准对象与厂商错误统计需要设备上下文（事件/DLL/标识/直接参数），
 * 由 ISDU 层内建处理器执行；表项只保存处理器 ID，因此表保持 const
 * 且不依赖任何上下文。应用参数使用 @ref IOLINK_OD_HANDLER_NONE ，并由
 * 表项自己的 @ref iolink_od_entry_t::backend 取值。
 */
typedef enum
{
    IOLINK_OD_HANDLER_NONE = 0U,            /**< 非内建：使用表项的应用后端。 */
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
 * 钩子只看到表项，请求的子索引不在参数列表中。多元素对象的子索引
 * 校验由 @ref iolink_od_check_value() 与 ISDU 层的上界检查负责；需要
 * 区分元素的钩子应按 @ref iolink_od_entry_t::subindex 或
 * @ref iolink_od_entry_t::max_subindex 自行判定，而不能依赖请求值。
 *
 * @param self  应用上下文（`iolink_device_app_t::self`）；标准表项没有钩子，不使用它。
 * @param entry 被校验的表项。
 * @param data  待写入的线上字节。
 * @param len   @p data 的字节数。
 * @return 结果模型：0 接受；正方 ISDU ErrorCode 原样上送。
 */
typedef iolink_result_t (*iolink_od_validate_fn)(iolink_app_t* self, const iolink_od_entry_t* entry,
                                                 const uint8_t* data, size_t len);

/**
 * @brief 写后动作钩子（TRIGGER 写语义的唯一动作，或 STORE/VOLATILE 写后通知）。
 *
 * 与 @ref iolink_od_validate_fn 一样只看到表项，不接收请求的子索引。
 * 对 STORAGE 标量，钩子返回错误时变量回滚到写前值；OctetString 不做
 * 回滚（避免在栈上复制整块），需要在写入前拒绝时使用 validate。
 *
 * @param self  应用上下文。
 * @param entry 被写入的表项。
 * @param data  已接受的线上字节。
 * @param len   @p data 的字节数。
 * @return 结果模型：0 成功；正方 ISDU ErrorCode 原样上送。
 */
typedef iolink_result_t (*iolink_od_change_fn)(iolink_app_t* self, const iolink_od_entry_t* entry,
                                               const uint8_t* data, size_t len);

/**
 * @brief 读取一个参数值的后端（HANDLER 表项）。
 *
 * @param self  应用上下文（`iolink_device_app_t::self`）。
 * @param entry 命中的表项。
 * @param out   接收线上字节的缓冲区。
 * @param cap   @p out 的容量。
 * @return 结果模型：成功时 `len` 为写出的字节数。
 */
typedef iolink_result_t (*iolink_od_read_fn)(iolink_app_t* self, const iolink_od_entry_t* entry,
                                             uint8_t* out, size_t cap);

/**
 * @brief 写入一个参数值的后端（HANDLER 表项）。
 *
 * 输入已经通过线长/范围/validate 校验，后端只负责应用取值。
 *
 * @param self     应用上下文。
 * @param entry    命中的表项（其 @c subindex 是表项声明的元素，
 *                 不一定等于请求的子索引）。
 * @param subindex 请求的子索引；多元素对象用它区分元素。
 * @param data     已校验的线上字节。
 * @param len      @p data 的字节数。
 * @return 结果模型：成功时 `len` 为 0。
 */
typedef iolink_result_t (*iolink_od_write_fn)(iolink_app_t* self, const iolink_od_entry_t* entry,
                                              uint8_t subindex, const uint8_t* data, size_t len);

/**
 * @brief 应用表项的值后端。
 *
 * 每个应用表项恰好选择一种：
 *  - STORAGE：直接读写应用变量（零 shadow，单一事实源）；
 *  - HANDLER：调用表项自己的 read/write 函数指针（计算型对象）。
 *
 * 内建表项（@ref iolink_od_handler_t 非 NONE）不使用本字段。
 */
typedef enum
{
    IOLINK_OD_BACKEND_STORAGE = 0U, /**< 绑定应用变量。 */
    IOLINK_OD_BACKEND_HANDLER = 1U  /**< 逐表项函数指针。 */
} iolink_od_backend_t;

/**
 * @brief 标量 codec 的线上长度（编译期常量）。
 *
 * 只用于表项初始化的自检；OctetString 返回 0（长度由 `block_len` 与
 * 当前长度变量决定）。
 */
#define IOLINK_OD_DT_LEN(datatype_)            \
    (((datatype_) == IOLINK_OD_DT_U8)     ? 1U \
     : ((datatype_) == IOLINK_OD_DT_U16)  ? 2U \
     : ((datatype_) == IOLINK_OD_DT_U32)  ? 4U \
     : ((datatype_) == IOLINK_OD_DT_BOOL) ? 1U \
                                          : 0U)

/**
 * @brief 可放进初始化器表达式的编译期断言（结果为 0）。
 *
 * 条件不成立时匿名位段宽度为负，编译立即失败；条件成立时整个表达式
 * 的值为 0。用于表项宏里的 `flags` 初始化位置。
 */
#define IOLINK_OD_ASSERT_EXPR(condition_) \
    (0U * sizeof(struct { int iolink_od_check_ : ((condition_) ? 1 : -1); }))

/**
 * @brief 表项语义/storage 交叉关系的编译期一致性。
 *
 * TRIGGER 只执行动作、不保存值，因此不能声明任何 storage 位。
 *
 * 只比较常量宏参数，可放进 @ref IOLINK_OD_ASSERT_EXPR 的位段宽度。
 */
#define IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_) \
    (((semantics_) != IOLINK_OD_WRITE_TRIGGER) || ((storage_) == IOLINK_OD_STORAGE_VOLATILE))

/** @brief 可写字节串必须给出长度上限（否则 STORAGE 写入会越过应用缓冲）。 */
#define IOLINK_OD_ASSERT_BYTES_BOUND(access_, block_len_) \
    (!(((access_) & IOLINK_OD_ACCESS_WRITE) != 0U) || ((block_len_) > 0U))

/**
 * @brief 一个对象字典表项。
 *
 * 设计约束：表示静态知识的字段全部为值语义，使整张表可以是 `const`
 * 并放在 ROM。运行期取值由表项的 @ref iolink_od_entry_t::backend 或
 * 内建处理器完成；应用表项只有一种后端（STORAGE 或 HANDLER）。
 */
struct iolink_od_entry
{
    uint16_t index;          /**< ISDU 索引。 */
    uint8_t subindex;        /**< 表项描述的子索引（单元素对象为 0）。 */
    uint8_t max_subindex;    /**< 可接受的最大子索引（0 = 仅 sub 0）。 */
    uint8_t datatype;        /**< @ref iolink_od_datatype_t codec ID。 */
    uint8_t access;          /**< IOLINK_OD_ACCESS_* 位掩码。 */
    uint8_t write_semantics; /**< IOLINK_OD_WRITE_*。 */
    uint8_t storage;         /**< IOLINK_OD_STORAGE_*。 */
    uint8_t flags;           /**< IOLINK_OD_FLAG_*。 */
    uint8_t handler;         /**< 内建处理器 ID；应用表项为 NONE。 */
    uint8_t backend;         /**< @ref iolink_od_backend_t （仅 handler == NONE）。 */
    union
    {
        struct
        {
            uint8_t* IOLINK_NULLABLE
                value; /**< 应用变量地址（标量或字节串缓冲），按字节视图读写。 */
            uint8_t* IOLINK_NULLABLE str_len; /**< OctetString 当前长度（标量置 NULL）。 */
        } storage;
        struct
        {
            iolink_od_read_fn IOLINK_NULLABLE read;   /**< 读值函数（STORAGE 表项为 NULL）。 */
            iolink_od_write_fn IOLINK_NULLABLE write; /**< 写值函数（只读对象可为 NULL）。 */
        } handler;
    } u;
    uint8_t block_len;                              /**< OctetString 长度上限/IODD fixedLength。 */
    uint32_t range_min;                             /**< 标量下限（含）。 */
    uint32_t range_max;                             /**< 标量上限（含）。 */
    uint32_t scalar_default;                        /**< 标量默认值。 */
    const uint8_t* IOLINK_NULLABLE block_default;   /**< OctetString 默认值字节（可为 NULL）。 */
    const char* name;                               /**< 对象名称（IODD/文档）。 */
    iolink_od_validate_fn IOLINK_NULLABLE validate; /**< 写校验钩子（可为 NULL）。 */
    iolink_od_change_fn IOLINK_NULLABLE on_change;  /**< 写后/触发钩子（可为 NULL）。 */
};

/**
 * @brief 带钩子的标量表项初始化宏。
 */
#define IOLINK_OD_BUILTIN_SCALAR_EX(index_, datatype_, access_, semantics_, storage_, handler_,  \
                                    low_, high_, def_, name_, validate_, change_)                \
    {.index = (index_),                                                                          \
     .subindex = 0U,                                                                             \
     .max_subindex = 0U,                                                                         \
     .datatype = (datatype_),                                                                    \
     .access = (access_),                                                                        \
     .write_semantics = (semantics_),                                                            \
     .storage = (storage_),                                                                      \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR(IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_)), \
     .handler = (handler_),                                                                      \
     .backend = IOLINK_OD_BACKEND_HANDLER,                                                       \
     .block_len = 0U,                                                                            \
     .range_min = (low_),                                                                        \
     .range_max = (high_),                                                                       \
     .scalar_default = (def_),                                                                   \
     .block_default = NULL,                                                                      \
     .name = (name_),                                                                            \
     .validate = (validate_),                                                                    \
     .on_change = (change_)}

/**
 * @brief 标量表项初始化宏（新增参数的一行写法）。
 */
#define IOLINK_OD_BUILTIN_SCALAR(index_, datatype_, access_, semantics_, storage_, handler_, low_, \
                                 high_, def_, name_)                                               \
    IOLINK_OD_BUILTIN_SCALAR_EX(index_, datatype_, access_, semantics_, storage_, handler_, low_,  \
                                high_, def_, name_, NULL, NULL)

/**
 * @brief 带钩子的字节串表项初始化宏。
 */
#define IOLINK_OD_BUILTIN_STRING_EX(index_, access_, semantics_, storage_, handler_, block_len_, \
                                    name_, validate_, change_)                                   \
    {.index = (index_),                                                                          \
     .subindex = 0U,                                                                             \
     .max_subindex = 0U,                                                                         \
     .datatype = IOLINK_OD_DT_OCTET_STRING,                                                      \
     .access = (access_),                                                                        \
     .write_semantics = (semantics_),                                                            \
     .storage = (storage_),                                                                      \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR(IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_)), \
     .handler = (handler_),                                                                      \
     .backend = IOLINK_OD_BACKEND_HANDLER,                                                       \
     .block_len = (block_len_),                                                                  \
     .range_min = 0U,                                                                            \
     .range_max = 0U,                                                                            \
     .scalar_default = 0U,                                                                       \
     .block_default = NULL,                                                                      \
     .name = (name_),                                                                            \
     .validate = (validate_),                                                                    \
     .on_change = (change_)}

/**
 * @brief 字节串表项初始化宏（新增参数的一行写法）。
 */
#define IOLINK_OD_BUILTIN_STRING(index_, access_, semantics_, storage_, handler_, block_len_,    \
                                 name_)                                                          \
    {.index = (index_),                                                                          \
     .subindex = 0U,                                                                             \
     .max_subindex = 0U,                                                                         \
     .datatype = IOLINK_OD_DT_OCTET_STRING,                                                      \
     .access = (access_),                                                                        \
     .write_semantics = (semantics_),                                                            \
     .storage = (storage_),                                                                      \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR(IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_)), \
     .handler = (handler_),                                                                      \
     .backend = IOLINK_OD_BACKEND_HANDLER,                                                       \
     .block_len = (block_len_),                                                                  \
     .range_min = 0U,                                                                            \
     .range_max = 0U,                                                                            \
     .scalar_default = 0U,                                                                       \
     .block_default = NULL,                                                                      \
     .name = (name_),                                                                            \
     .validate = NULL,                                                                           \
     .on_change = NULL}

/* --- 应用表项：直接绑定应用变量（零 shadow） --- */

/**
 * @brief 标量表项绑定应用变量（带钩子）。
 *
 * `ptr_` 指向应用变量，宽度在编译期与 `datatype_` 的 codec 长度比较；
 * 不一致直接编译失败。标量写入使用 codec 解码后按原生宽度存储，应用
 * 本地写同一个变量即可被主站读到。
 */
#define IOLINK_OD_SCALAR_STORAGE_EX(index_, datatype_, access_, semantics_, storage_, low_, high_, \
                                    def_, name_, ptr_, validate_, change_)                         \
    {.index = (index_),                                                                            \
     .subindex = 0U,                                                                               \
     .max_subindex = 0U,                                                                           \
     .datatype = (datatype_),                                                                      \
     .access = (access_),                                                                          \
     .write_semantics = (semantics_),                                                              \
     .storage = (storage_),                                                                        \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR((sizeof(*(ptr_)) == IOLINK_OD_DT_LEN(datatype_)) &&  \
                                              IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_)),   \
     .handler = IOLINK_OD_HANDLER_NONE,                                                            \
     .backend = IOLINK_OD_BACKEND_STORAGE,                                                         \
     .u.storage = {.value = (uint8_t*) (ptr_), .str_len = NULL},                                   \
     .block_len = 0U,                                                                              \
     .range_min = (low_),                                                                          \
     .range_max = (high_),                                                                         \
     .scalar_default = (def_),                                                                     \
     .block_default = NULL,                                                                        \
     .name = (name_),                                                                              \
     .validate = (validate_),                                                                      \
     .on_change = (change_)}

/**
 * @brief 标量表项绑定应用变量（无钩子）。
 */
#define IOLINK_OD_SCALAR_STORAGE(index_, datatype_, access_, semantics_, storage_, low_, high_, \
                                 def_, name_, ptr_)                                             \
    IOLINK_OD_SCALAR_STORAGE_EX(index_, datatype_, access_, semantics_, storage_, low_, high_,  \
                                def_, name_, ptr_, NULL, NULL)

/**
 * @brief 字节串表项绑定应用缓冲（带钩子）。
 *
 * `buf_` 必须是数组左值：`sizeof(buf_) >= block_len_` 在编译期检查。
 * `len_ptr_` 指向当前长度变量，写入后更新；读取返回该长度。指针型缓冲
 * （malloc/外部内存）请改用 @ref IOLINK_OD_BYTES_HANDLER 。
 */
#define IOLINK_OD_BYTES_STORAGE_EX(index_, access_, semantics_, storage_, block_len_, name_, buf_, \
                                   len_ptr_, validate_, change_)                                   \
    {.index = (index_),                                                                            \
     .subindex = 0U,                                                                               \
     .max_subindex = 0U,                                                                           \
     .datatype = IOLINK_OD_DT_OCTET_STRING,                                                        \
     .access = (access_),                                                                          \
     .write_semantics = (semantics_),                                                              \
     .storage = (storage_),                                                                        \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR((sizeof(buf_) >= (block_len_)) &&                    \
                                              IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_) &&  \
                                              IOLINK_OD_ASSERT_BYTES_BOUND(access_, block_len_)),  \
     .handler = IOLINK_OD_HANDLER_NONE,                                                            \
     .backend = IOLINK_OD_BACKEND_STORAGE,                                                         \
     .u.storage = {.value = (uint8_t*) (buf_), .str_len = (len_ptr_)},                             \
     .block_len = (block_len_),                                                                    \
     .range_min = 0U,                                                                              \
     .range_max = 0U,                                                                              \
     .scalar_default = 0U,                                                                         \
     .block_default = NULL,                                                                        \
     .name = (name_),                                                                              \
     .validate = (validate_),                                                                      \
     .on_change = (change_)}

/**
 * @brief 字节串表项绑定应用缓冲（无钩子）。
 */
#define IOLINK_OD_BYTES_STORAGE(index_, access_, semantics_, storage_, block_len_, name_, buf_, \
                                len_ptr_)                                                       \
    IOLINK_OD_BYTES_STORAGE_EX(index_, access_, semantics_, storage_, block_len_, name_, buf_,  \
                               len_ptr_, NULL, NULL)

/* --- 应用表项：每项函数指针（计算型对象） --- */

/**
 * @brief 标量表项使用表项自己的 read/write 函数（带钩子）。
 */
#define IOLINK_OD_SCALAR_HANDLER_EX(index_, datatype_, access_, semantics_, storage_, low_, high_, \
                                    def_, name_, read_, write_, validate_, change_)                \
    {.index = (index_),                                                                            \
     .subindex = 0U,                                                                               \
     .max_subindex = 0U,                                                                           \
     .datatype = (datatype_),                                                                      \
     .access = (access_),                                                                          \
     .write_semantics = (semantics_),                                                              \
     .storage = (storage_),                                                                        \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR(IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_)),   \
     .handler = IOLINK_OD_HANDLER_NONE,                                                            \
     .backend = IOLINK_OD_BACKEND_HANDLER,                                                         \
     .u.handler = {.read = (read_), .write = (write_)},                                            \
     .block_len = 0U,                                                                              \
     .range_min = (low_),                                                                          \
     .range_max = (high_),                                                                         \
     .scalar_default = (def_),                                                                     \
     .block_default = NULL,                                                                        \
     .name = (name_),                                                                              \
     .validate = (validate_),                                                                      \
     .on_change = (change_)}

/**
 * @brief 标量表项使用表项自己的 read/write 函数（无钩子）。
 */
#define IOLINK_OD_SCALAR_HANDLER(index_, datatype_, access_, semantics_, storage_, low_, high_, \
                                 def_, name_, read_, write_)                                    \
    IOLINK_OD_SCALAR_HANDLER_EX(index_, datatype_, access_, semantics_, storage_, low_, high_,  \
                                def_, name_, read_, write_, NULL, NULL)

/**
 * @brief 字节串表项使用表项自己的 read/write 函数（带钩子）。
 */
#define IOLINK_OD_BYTES_HANDLER_EX(index_, access_, semantics_, storage_, block_len_, name_,     \
                                   read_, write_, validate_, change_)                            \
    {.index = (index_),                                                                          \
     .subindex = 0U,                                                                             \
     .max_subindex = 0U,                                                                         \
     .datatype = IOLINK_OD_DT_OCTET_STRING,                                                      \
     .access = (access_),                                                                        \
     .write_semantics = (semantics_),                                                            \
     .storage = (storage_),                                                                      \
     .flags = (uint8_t) IOLINK_OD_ASSERT_EXPR(IOLINK_OD_ASSERT_SEMANTICS(semantics_, storage_)), \
     .handler = IOLINK_OD_HANDLER_NONE,                                                          \
     .backend = IOLINK_OD_BACKEND_HANDLER,                                                       \
     .u.handler = {.read = (read_), .write = (write_)},                                          \
     .block_len = (block_len_),                                                                  \
     .range_min = 0U,                                                                            \
     .range_max = 0U,                                                                            \
     .scalar_default = 0U,                                                                       \
     .block_default = NULL,                                                                      \
     .name = (name_),                                                                            \
     .validate = (validate_),                                                                    \
     .on_change = (change_)}

/**
 * @brief 字节串表项使用表项自己的 read/write 函数（无钩子）。
 */
#define IOLINK_OD_BYTES_HANDLER(index_, access_, semantics_, storage_, block_len_, name_, read_, \
                                write_)                                                          \
    IOLINK_OD_BYTES_HANDLER_EX(index_, access_, semantics_, storage_, block_len_, name_, read_,  \
                               write_, NULL, NULL)

/**
 * @brief 应用对象字典：const 表。
 *
 * 表项自己携带值后端（STORAGE/HANDLER）；钩子与 HANDLER 函数指针
 * 收到的上下文来自设备实例（`iolink_od_ctx_t::self`），因此同一张
 * 表可以被多个实例共享。
 */
typedef struct
{
    const iolink_od_entry_t* IOLINK_NULLABLE entries; /**< 应用表项数组（空表可为 NULL）。 */
    size_t count;                                     /**< 表项数量。 */
} iolink_od_app_t;

/**
 * @brief 值访问上下文：应用表 + 实例上下文。
 */
typedef struct
{
    const iolink_od_app_t* IOLINK_NULLABLE app; /**< 应用表与后端（可为 NULL）。 */
    iolink_app_t* IOLINK_NULLABLE self;         /**< 传给钩子与 HANDLER 后端的应用上下文
                                                     （可为 NULL，钩子需自行处理）。 */
} iolink_od_ctx_t;

/**
 * @brief datatype codec 描述。
 *
 * IO-Link 线上字节序固定为大端（A.5），因此 codec 不保存字节序字段：
 * 编解码只有一份大端实现，避免出现不可达的小端分支。
 */
typedef struct
{
    uint8_t len;  /**< 定长字节数；0 表示变长（OctetString）。 */
    uint32_t max; /**< 该类型的原生上限。 */
} iolink_od_codec_t;

/** @brief 取 datatype 的 codec 描述；越界返回 NULL。 */
const iolink_od_codec_t* IOLINK_NULLABLE iolink_od_codec(uint8_t datatype);

/**
 * @brief 校验一个待写入值：线长、OctetString 长度上限、标量范围与
 *        表项 validate 钩子。
 *
 * `IOLINK_OD_DT_OCTET_STRING` 表项在 @ref iolink_od_entry_t::block_len
 * 非 0 时把 @p len 超过上限视为长度错误（0x33）；`block_len == 0`
 * 表示不限制（例如 0x0025 这类只读数组对象）。
 *
 * @param entry 表项
 * @param self  应用上下文
 * @param data  线上字节
 * @param len   @p data 字节数
 * @return 0 接受；0x33 长度不符；0x30 越界；负值为内部错误。
 */
iolink_result_t iolink_od_check_value(const iolink_od_entry_t* entry,
                                      iolink_app_t* IOLINK_NULLABLE self, const uint8_t* data,
                                      size_t len);

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
const iolink_od_entry_t* IOLINK_NULLABLE iolink_od_at(size_t i);

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
const iolink_od_entry_t* IOLINK_NULLABLE iolink_od_find(const iolink_od_app_t* IOLINK_NULLABLE app,
                                                        uint16_t index, uint8_t subindex);

/**
 * @brief 只在应用表中查找表项。
 *
 * ISDU 分发用它把"应用声明的设备专用索引"置于内建回落项之前。
 * 标准索引的分发不使用它：@ref iolink_od_find 保证标准表优先。
 */
const iolink_od_entry_t* IOLINK_NULLABLE
iolink_od_find_app(const iolink_od_app_t* IOLINK_NULLABLE app, uint16_t index, uint8_t subindex);

/** @brief 标准表中 storage 为 DS 的表项数量。 */
size_t iolink_od_ds_count(void);

/** @brief 取第 @p i 个标准 DS 表项；越界返回 NULL。 */
const iolink_od_entry_t* IOLINK_NULLABLE iolink_od_ds_at(size_t i);

/** @brief 应用表中 storage 为 DS 的表项数量；@p app 为 NULL 时返回 0。 */
size_t iolink_od_ds_count_app(const iolink_od_app_t* IOLINK_NULLABLE app);

/** @brief 取第 @p i 个应用 DS 表项；越界返回 NULL。 */
const iolink_od_entry_t* IOLINK_NULLABLE
iolink_od_ds_at_app(const iolink_od_app_t* IOLINK_NULLABLE app, size_t i);

/**
 * @brief index/subindex 是否属于 DS 参数集（标准表优先，其次应用表）。
 *
 * @param app      应用表（可为 NULL）
 * @param index    ISDU 索引
 * @param subindex ISDU 子索引
 */
bool iolink_od_is_ds(const iolink_od_app_t* IOLINK_NULLABLE app, uint16_t index, uint8_t subindex);

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
 * TRIGGER 语义不写后端，只执行 on_change。@p subindex 原样传给
 * 参数管理器或应用写钩子；validate/on_change 只看到表项（见
 * @ref iolink_od_validate_fn 的说明）。
 */
iolink_result_t iolink_od_write_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                      uint8_t subindex, const uint8_t* data, size_t len);

/**
 * @brief 恢复一个值，不执行 validate/on_change/持久化。
 *
 * 供参数镜像加载与出厂复位使用：数据在写入时已经校验并持久化，
 * 恢复阶段只负责把值放回后端。
 *
 * @param ctx      值访问上下文
 * @param entry    表项
 * @param subindex 请求的子索引
 * @param data     线上字节
 * @param len      @p data 的字节数
 * @return 结果模型
 */
iolink_result_t iolink_od_restore_value(iolink_od_ctx_t* ctx, const iolink_od_entry_t* entry,
                                        uint8_t subindex, const uint8_t* data, size_t len);

/** @} */ /* end of iolinki_od */

IOLINK_ASSUME_NONNULL_END

#endif  // IOLINK_OD_H
