/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file profile.c
 * @brief 构建档位符号：每个保留为编译期的开关只定义匹配当前档位的一个
 *        方向，供 include/iolinki/profile.h 的链接期断言使用。
 */

#include "iolinki/profile.h"

#if IOLINK_FEATURE_EVENTS
const uint8_t iolink_profile_events_on = 1U;
#else
const uint8_t iolink_profile_events_off = 0U;
#endif

#if IOLINK_FEATURE_DATA_STORAGE
const uint8_t iolink_profile_data_storage_on = 1U;
#else
const uint8_t iolink_profile_data_storage_off = 0U;
#endif

#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
const uint8_t iolink_profile_isr_rx_frame = 1U;
#else
const uint8_t iolink_profile_isr_rx_byte = 0U;
#endif

#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
const uint8_t iolink_profile_isr_tx_frame = 1U;
#else
const uint8_t iolink_profile_isr_tx_byte = 0U;
#endif

#if IOLINK_PHY_FEATURE_TX_BUSY
const uint8_t iolink_profile_tx_busy_on = 1U;
#else
const uint8_t iolink_profile_tx_busy_off = 0U;
#endif

#if IOLINK_CONCURRENCY == IOLINK_CONCURRENCY_SINGLE
const uint8_t iolink_profile_concurrency_single = 0U;
#elif IOLINK_CONCURRENCY == IOLINK_CONCURRENCY_SMP
const uint8_t iolink_profile_concurrency_smp = 2U;
#else
const uint8_t iolink_profile_concurrency_isr = 1U;
#endif

#if IOLINK_STATS_MODE == IOLINK_STATS_SPLIT
const uint8_t iolink_profile_stats_split = 1U;
#else
const uint8_t iolink_profile_stats_shared = 0U;
#endif
