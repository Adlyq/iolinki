/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file capability.c
 * @brief 能力表：构建档位常量与唯一的实例能力解析点。
 * @ingroup iolinki_capability
 */

#include "iolinki/capability.h"

#include "iolinki/protocol.h"

/** @brief 编译期 ISR 粒度到能力表取值的映射（唯一事实源）。 */
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
#define IOLINK_CAP_BUILD_ISR_RX IOLINK_CAP_ISR_RX_FRAME
#else
#define IOLINK_CAP_BUILD_ISR_RX IOLINK_CAP_ISR_RX_BYTE
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
#define IOLINK_CAP_BUILD_ISR_TX IOLINK_CAP_ISR_TX_FRAME
#else
#define IOLINK_CAP_BUILD_ISR_TX IOLINK_CAP_ISR_TX_BYTE
#endif

/** @brief 构建期特性位（唯一事实源）。 */
#if IOLINK_FEATURE_DATA_STORAGE
#define IOLINK_CAP_BUILT_DATA_STORAGE IOLINK_CAPB_DATA_STORAGE
#else
#define IOLINK_CAP_BUILT_DATA_STORAGE ((uint32_t) 0U)
#endif

#if IOLINK_FEATURE_EVENTS
#define IOLINK_CAP_BUILT_EVENTS IOLINK_CAPB_EVENTS
#else
#define IOLINK_CAP_BUILT_EVENTS ((uint32_t) 0U)
#endif

#if IOLINK_FEATURE_SYSTEM_COMMAND
#define IOLINK_CAP_BUILT_SYSTEM_COMMAND IOLINK_CAPB_SYSTEM_COMMAND
#else
#define IOLINK_CAP_BUILT_SYSTEM_COMMAND ((uint32_t) 0U)
#endif

#if IOLINK_LOG_ENABLE_ISR
#define IOLINK_CAP_BUILT_ISR_LOG IOLINK_CAPB_ISR_LOG
#else
#define IOLINK_CAP_BUILT_ISR_LOG ((uint32_t) 0U)
#endif

#if IOLINK_PHY_FEATURE_ISR_TX_ONLY
#define IOLINK_CAP_BUILT_ISR_TX_ONLY IOLINK_CAPB_ISR_TX_ONLY
#else
#define IOLINK_CAP_BUILT_ISR_TX_ONLY ((uint32_t) 0U)
#endif

/** @brief 构建期兼容位默认值。 */
#if IOLINK_COMPAT_V10_STARTUP_OPERATE
#define IOLINK_CAP_COMPAT_V10_DEFAULT IOLINK_STACK_COMPAT_V10_STARTUP_OPERATE
#else
#define IOLINK_CAP_COMPAT_V10_DEFAULT 0U
#endif

#if IOLINK_COMPAT_TYPE0_PAGE_READ_OPERATE
#define IOLINK_CAP_COMPAT_T0_DEFAULT IOLINK_STACK_COMPAT_TYPE0_PAGE_READ_OPERATE
#else
#define IOLINK_CAP_COMPAT_T0_DEFAULT 0U
#endif

#if IOLINK_DS_LEGACY_IMAGE_SUB0
#define IOLINK_CAP_COMPAT_DS_LEGACY_DEFAULT IOLINK_STACK_COMPAT_DS_LEGACY_IMAGE_SUB0
#else
#define IOLINK_CAP_COMPAT_DS_LEGACY_DEFAULT 0U
#endif

/** @brief 本镜像的构建档位（唯一实例，落 `.rodata`）。 */
static const iolink_build_caps_t g_iolink_build_caps = {
    .abi = (uint16_t) IOLINK_CAP_ABI,
    .isr_rx = (uint8_t) IOLINK_CAP_BUILD_ISR_RX,
    .isr_tx = (uint8_t) IOLINK_CAP_BUILD_ISR_TX,
    .concurrency = (uint8_t) IOLINK_CONCURRENCY,
    .stats = (uint8_t) IOLINK_STATS_MODE,
    .pd_in_buffers = (uint8_t) IOLINK_PD_IN_BUFFERS,
    .compat_defaults = (uint8_t) (IOLINK_CAP_COMPAT_V10_DEFAULT | IOLINK_CAP_COMPAT_T0_DEFAULT |
                                  IOLINK_CAP_COMPAT_DS_LEGACY_DEFAULT),
    .event_min_appear_ms = (uint16_t) IOLINK_EVENT_MIN_APPEAR_MS,
    .event_min_repeat_ms = (uint16_t) IOLINK_EVENT_MIN_REPEAT_MS,
    .timing_enforce_default = (uint8_t) IOLINK_TIMING_ENFORCE_DEFAULT,
    .reserved = 0U,
    .built = (uint32_t) (IOLINK_CAP_BUILT_DATA_STORAGE | IOLINK_CAP_BUILT_EVENTS |
                         IOLINK_CAP_BUILT_SYSTEM_COMMAND | IOLINK_CAP_BUILT_ISR_LOG |
                         IOLINK_CAP_BUILT_ISR_TX_ONLY),
};

/* 布局契约：能力表是 ABI 的一部分，字段宽度变化必须改 ABI 版本。 */
IOLINK_STATIC_ASSERT(sizeof(iolink_build_caps_t) == 20U, "build caps layout changed: bump ABI");

const iolink_build_caps_t* iolink_caps_build(void)
{
    return &g_iolink_build_caps;
}

iolink_caps_t iolink_caps_resolve(const iolink_phy_api_t* phy, const iolink_stack_config_t* stack,
                                  const iolink_app_callbacks_t* app,
                                  const iolink_build_caps_t* build)
{
    iolink_caps_t caps = 0U;
    uint8_t compat;
    uint8_t flags = 0U;

    if (build == NULL) {
        build = &g_iolink_build_caps;
    }
    compat = build->compat_defaults;
    if (stack != NULL) {
        compat |= stack->compat_flags;
        flags = stack->flags;
    }

    /* 构建特性（实例是否启用）。 */
    if (iolink_caps_built(build, IOLINK_CAPB_SYSTEM_COMMAND) &&
        ((flags & IOLINK_STACK_FLAG_NO_SYSTEM_COMMAND) == 0U)) {
        caps |= IOLINK_CAP_SYSTEM_COMMAND;
    }
    if (iolink_caps_built(build, IOLINK_CAPB_EVENTS)) {
        caps |= IOLINK_CAP_EVENTS;
    }

    /* 兼容位（构建默认值 | 运行时配置）。 */
    if ((compat & IOLINK_STACK_COMPAT_V10_STARTUP_OPERATE) != 0U) {
        caps |= IOLINK_CAP_COMPAT_V10_OP;
    }
    if ((compat & IOLINK_STACK_COMPAT_TYPE0_PAGE_READ_OPERATE) != 0U) {
        caps |= IOLINK_CAP_COMPAT_T0_READ;
    }
    if ((compat & IOLINK_STACK_COMPAT_DS_LEGACY_IMAGE_SUB0) != 0U) {
        caps |= IOLINK_CAP_DS_LEGACY_SUB0;
    }

    /* 端口声明：所有应答必须由 ISR 发送。 */
    if (((flags & IOLINK_STACK_FLAG_REQUIRE_ISR_TX) != 0U) ||
        (iolink_caps_built(build, IOLINK_CAPB_ISR_TX_ONLY))) {
        caps |= IOLINK_CAP_REQUIRE_ISR_TX;
    }

    /* 本库实现了全部四个访问锁；实例可以后续清除某一位。 */
    caps |= IOLINK_CAP_LOCKS_ALL;

    /* vtable 探测：按构建档位选择要观察的成员。这是全库唯一允许
     * 与 ISR 粒度耦合的判定点（vtable 形状仍是编译期契约）。 */
    if (phy != NULL) {
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
        if (phy->rx_frame != NULL) {
#else
        if (phy->rx_octet != NULL) {
#endif
            caps |= IOLINK_CAP_RX;
        }

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
        if (phy->tx_frame != NULL) {
            caps |= IOLINK_CAP_TX;
        }
        if (phy->tx_busy != NULL) {
            caps |= IOLINK_CAP_TX_BUSY;
        }
#else
        /* TX BYTE 泵只需要 tx_octet()：它逐字节喂送并以自身状态判断完成，
         * tx_done() 只是可选的"帧结束"通知（库不依赖它释放 PD-in 租约，
         * 见 dll_isr_tx_step）。 */
        if (phy->tx_octet != NULL) {
            caps |= IOLINK_CAP_TX;
        }
#endif

        if (phy->detect_wakeup != NULL) {
            caps |= IOLINK_CAP_WAKEUP;
        }
        if (phy->set_cq_line != NULL) {
            caps |= IOLINK_CAP_SIO_CQ;
        }
        if (phy->request_service != NULL) {
            caps |= IOLINK_CAP_REQUEST_SERVICE;
        }
        if (phy->get_faults != NULL) {
            caps |= IOLINK_CAP_PHY_FAULTS;
        }
    }

    /* PD-in 路径就是"回调存在与否"。 */
    if ((app != NULL) && (app->on_pd_input != NULL)) {
        caps |= IOLINK_CAP_PD_IN_CALLBACK;
    }

    return caps;
}
