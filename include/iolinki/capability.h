/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CAPABILITY_H
#define IOLINK_CAPABILITY_H

#include <stdbool.h>
#include <stdint.h>

#include "iolinki/application.h"
#include "iolinki/config.h"
#include "iolinki/iolink.h"
#include "iolinki/phy.h"

#include "iolinki/types.h"
IOLINK_ASSUME_NONNULL_BEGIN

/**
 * @file capability.h
 * @brief 能力表：构建档位（ROM 常量）+ 实例能力位（运行时）。
 *
 * 三层拆分：
 *  1. **构建档位** `iolink_build_caps_t`：这个镜像被编译成了什么
 *     （ISR 粒度、并发/统计模式、哪些特性被编译进来）。每个镜像一份，
 *     `const`，落 `.rodata`。
 *  2. **实例能力** `iolink_caps_t`：单个设备实例在 init 时从 PHY
 *     vtable 与设备配置解析出的位掩码。主要判定点唯一。
 *  3. **操作表**：可选实现表（OD 后端、应答计划、诊断源），
 *     "函数指针 NULL = 不支持"。
 *
 * 判定点规则：
 *  - 新增能力 = 一个位 + @ref iolink_caps_resolve 里一行探测
 *    +（可选）一行 init 校验；
 *  - 其余位置不再写 `phy->xxx != NULL`，统一走
 *    `iolink_caps_has()` / `dll_phy_has_*()`。
 */

/**
 * @defgroup iolinki_capability 能力表
 * @brief 构建档位与运行时实例能力的统一查询面。
 * @{
 */

/** @brief 能力表 ABI 版本；结构体形状变化时递增。 */
#define IOLINK_CAP_ABI 1U

/* =========================================================================
 * 构建档位取值
 * ========================================================================= */

/** @brief 构建档位的 ISR 接收粒度：逐字节。 */
#define IOLINK_CAP_ISR_RX_BYTE 0x01U
/** @brief 构建档位的 ISR 接收粒度：整帧。 */
#define IOLINK_CAP_ISR_RX_FRAME 0x02U
/** @brief 构建档位的 ISR 发送粒度：逐字节。 */
#define IOLINK_CAP_ISR_TX_BYTE 0x01U
/** @brief 构建档位的 ISR 发送粒度：整帧。 */
#define IOLINK_CAP_ISR_TX_FRAME 0x02U

/**
 * @brief 构建特性位（`iolink_build_caps_t::built`）。
 *
 * 只列"这个镜像里有没有这段实现"，与实例是否启用无关。
 */
#define IOLINK_CAPB_DATA_STORAGE ((uint32_t) 1U << 0)   /**< Data Storage 子系统 */
#define IOLINK_CAPB_EVENTS ((uint32_t) 1U << 1)         /**< 诊断事件引擎 */
#define IOLINK_CAPB_SYSTEM_COMMAND ((uint32_t) 1U << 2) /**< SystemCommand 处理 */
#define IOLINK_CAPB_ISR_LOG ((uint32_t) 1U << 3)        /**< ISR 内日志转发 */
#define IOLINK_CAPB_ISR_TX_ONLY ((uint32_t) 1U << 4)    /**< 构建期默认要求 ISR 发送 */

/**
 * @brief 一个镜像的构建档位描述。
 *
 * 字段全部来自编译期开关，`iolink_caps_build()` 返回唯一实例。
 * 消费者应把它当作**只读常量**：它在 `.rodata`，不在上下文里。
 */
typedef struct
{
    uint16_t abi;                   /**< @ref IOLINK_CAP_ABI */
    uint8_t isr_rx;                 /**< IOLINK_CAP_ISR_RX_BYTE / _FRAME */
    uint8_t isr_tx;                 /**< IOLINK_CAP_ISR_TX_BYTE / _FRAME */
    uint8_t concurrency;            /**< IOLINK_CONCURRENCY_* */
    uint8_t stats;                  /**< IOLINK_STATS_SHARED / _SPLIT */
    uint8_t pd_in_buffers;          /**< IOLINK_PD_IN_BUFFERS */
    uint8_t compat_defaults;        /**< IOLINK_STACK_COMPAT_* 构建期默认 */
    uint16_t event_min_appear_ms;   /**< 事件最短出现间隔默认值 */
    uint16_t event_min_repeat_ms;   /**< 同一事件重复上报间隔默认值 */
    uint8_t timing_enforce_default; /**< 1 = 默认强制 t_ren/t_cycle 检查 */
    uint8_t reserved;               /**< 对齐填充，必须为 0 */
    uint32_t built;                 /**< IOLINK_CAPB_* 位掩码 */
} iolink_build_caps_t;

/* =========================================================================
 * 实例能力位
 * ========================================================================= */

/** @brief 单个实例的能力位掩码。 */
typedef uint32_t iolink_caps_t;

/** @brief PHY 提供了构建档位所选的接收钩子。 */
#define IOLINK_CAP_RX (((iolink_caps_t) 1U) << 0)
/** @brief PHY 提供了构建档位所选的发送钩子。
 *
 * TX BYTE：`tx_octet` 非 NULL 即置位；`tx_done` 是可选的帧结束通知。
 * TX FRAME：`tx_frame` 非 NULL 即置位。
 */
#define IOLINK_CAP_TX (((iolink_caps_t) 1U) << 1)
/** @brief PHY 能观察 C/Q 唤醒脉冲（detect_wakeup 非 NULL）。 */
#define IOLINK_CAP_WAKEUP (((iolink_caps_t) 1U) << 2)
/** @brief PHY 提供手动 C/Q 控制（set_cq_line 非 NULL；DLL 无调用点）。 */
#define IOLINK_CAP_SIO_CQ (((iolink_caps_t) 1U) << 3)
/** @brief PHY 提供 tx_busy()，可观测整帧发送结束。 */
#define IOLINK_CAP_TX_BUSY (((iolink_caps_t) 1U) << 4)
/** @brief PHY 提供 request_service()，可由 ISR 泵送应答。 */
#define IOLINK_CAP_REQUEST_SERVICE (((iolink_caps_t) 1U) << 5)
/** @brief PHY 提供 get_faults()，可上报物理故障。 */
#define IOLINK_CAP_PHY_FAULTS (((iolink_caps_t) 1U) << 6)
/** @brief 应用提供 on_pd_input()：PD-in 走回调路径而不是轮询。 */
#define IOLINK_CAP_PD_IN_CALLBACK (((iolink_caps_t) 1U) << 7)
/** @brief SystemCommand 处理启用（构建有实现且实例未关闭）。 */
#define IOLINK_CAP_SYSTEM_COMMAND (((iolink_caps_t) 1U) << 8)
/** @brief 诊断事件引擎启用。 */
#define IOLINK_CAP_EVENTS (((iolink_caps_t) 1U) << 9)
/** @brief V1.0 STARTUP 阶段接受 DeviceOperate(0x99) 捷径。 */
#define IOLINK_CAP_COMPAT_V10_OP (((iolink_caps_t) 1U) << 10)
/** @brief OPERATE 阶段应答 Type-0 页读而不是复位。 */
#define IOLINK_CAP_COMPAT_T0_READ (((iolink_caps_t) 1U) << 11)
/** @brief DataStorageIndex 子索引 0 保留整镜像载荷。 */
#define IOLINK_CAP_DS_LEGACY_SUB0 (((iolink_caps_t) 1U) << 12)
/** @brief 端口要求所有应答都由 ISR 发送（必须有 request_service()）。 */
#define IOLINK_CAP_REQUIRE_ISR_TX (((iolink_caps_t) 1U) << 13)
/** @brief DeviceAccessLocks 位 0（参数写）已实现。 */
#define IOLINK_CAP_LOCK_PARAM (((iolink_caps_t) 1U) << 14)
/** @brief DeviceAccessLocks 位 1（Data Storage）已实现。 */
#define IOLINK_CAP_LOCK_DS (((iolink_caps_t) 1U) << 15)
/** @brief DeviceAccessLocks 位 2（本地参数化）已实现。 */
#define IOLINK_CAP_LOCK_LOCAL_PARAM (((iolink_caps_t) 1U) << 16)
/** @brief DeviceAccessLocks 位 3（本地用户接口）已实现。 */
#define IOLINK_CAP_LOCK_LOCAL_UI (((iolink_caps_t) 1U) << 17)

/** @brief 所有 DeviceAccessLocks 能力位。 */
#define IOLINK_CAP_LOCKS_ALL                                                    \
    (IOLINK_CAP_LOCK_PARAM | IOLINK_CAP_LOCK_DS | IOLINK_CAP_LOCK_LOCAL_PARAM | \
     IOLINK_CAP_LOCK_LOCAL_UI)

/**
 * @brief 取本镜像的构建档位（单例，只读）。
 * @return 指向 `.rodata` 中的常量表。
 */
const iolink_build_caps_t* iolink_caps_build(void);

/**
 * @brief 解析一个设备实例的能力位。
 *
 * 全库唯一的解析点：按构建档位选择要探测的 vtable 成员，并合并
 * 设备配置里的运行时开关。必须在绑定 PHY 之后、启用 ISR 之前调用。
 *
 * @param phy   绑定的 PHY vtable（可以为 NULL，表示无 PHY 能力）
 * @param stack 运行时协议栈配置（可以为 NULL，取默认）
 * @param app   应用 trait（可以为 NULL）
 * @param build 构建档位（NULL 表示 @ref iolink_caps_build()）
 * @return 能力位掩码
 */
iolink_caps_t iolink_caps_resolve(const iolink_phy_api_t* IOLINK_NULLABLE phy,
                                  const iolink_stack_config_t* IOLINK_NULLABLE stack,
                                  const iolink_device_app_t* IOLINK_NULLABLE app,
                                  const iolink_build_caps_t* IOLINK_NULLABLE build);

/**
 * @brief 测试能力位是否置位。
 * @param caps 能力位掩码
 * @param bit  单个 @ref iolink_caps_t 位
 */
static inline bool iolink_caps_has(iolink_caps_t caps, iolink_caps_t bit)
{
    return (caps & bit) != 0U;
}

/**
 * @brief 测试构建特性位是否置位。
 * @param caps 构建档位（NULL 视为无特性）
 * @param mask `IOLINK_CAPB_*` 位掩码
 */
static inline bool iolink_caps_built(const iolink_build_caps_t* caps, uint32_t mask)
{
    return (caps != NULL) && ((caps->built & mask) != 0U);
}

/** @} */ /* end of iolinki_capability */

IOLINK_ASSUME_NONNULL_END
#endif /* IOLINK_CAPABILITY_H */
