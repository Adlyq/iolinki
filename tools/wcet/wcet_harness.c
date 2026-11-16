/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file wcet_harness.c
 * @brief 主机侧路径长度代理基准：用合成 PHY 驱动完整设备。
 *
 * 这里输出的是**调用次数**（ISR 泵、rx_octet、tx_octet），用来观察路径
 * 长度是否回归，**不是 CPU 周期 WCET**。CPU 周期上界只能用目标端的 DWT
 * 周期计数器测量（见 README.md）；本工具的用途是：
 *  - 用确定性的合成 PHY 覆盖最坏帧序（最大 PD/OD、背靠背、ISDU 分块、
 *    事件诊断、SIO/fallback、无效帧）；
 *  - 把"每帧需要多少次中断泵/字节搬运"变成可比较、可设门的数字；
 *  - 在目标板上复用同一帧序注入逻辑，只把计数器换成 DWT。
 *
 * 所有时间来自虚拟时钟（固定步进），没有真实时间、没有随机数，因此同一
 * 场景重复运行的指标必须完全一致（`--repeat` 会强制校验）。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iolinki/config.h"
#include "iolinki/crc.h"
#include "iolinki/device.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/iolink.h"
#include "iolinki/phy.h"
#include "iolinki/protocol.h"

/* --- 场景参数 ----------------------------------------------------------- */

/** 主循环每次迭代推进的虚拟时间（us）。小于 COM2 的 t_A（约 338 us）。 */
#define WCET_TIME_STEP_US 50U
/** 单次 ISR 泵排空的调用上限；防止异常状态下无限自旋。 */
#define WCET_PUMP_LIMIT 8192U
#define WCET_RX_QUEUE_SIZE 8192U
#define WCET_TX_MAX 128U
#define WCET_FRAME_MAX 128U
/** 厂商 ISDU 索引（B.2.27：0x0040-0x00FE 为设备专用）。 */
#define WCET_VENDOR_PARAM_INDEX 0x0040U
#define WCET_VENDOR_PARAM_LEN 64U
/** VendorName 长度：跨 3 个 32 字节 OD 段，覆盖 ExtLength 分块读。 */
#define WCET_VENDOR_NAME_LEN 63U
/** T_FBD（3 × MinCycleTime = 3 ms）之后继续推进的步数（5 ms）。 */
#define WCET_FALLBACK_STEPS 100U

/**
 * @brief 一个场景的路径长度代理指标。
 *
 * 全部为调用次数，与编译器和目标架构无关，可在任意主机比较。
 */
typedef struct
{
    unsigned frames;            /**< 送入的主站帧数（含无效帧） */
    unsigned isr_pumps;         /**< iolink_device_process_isr() 调用次数 */
    unsigned isr_max_per_frame; /**< 单帧从开始到空闲的最大 ISR 泵次数 */
    unsigned rx_octets;         /**< rx_octet() 调用次数 */
    unsigned tx_octets;         /**< tx_octet() 调用次数（即发送字节数） */
    unsigned tx_frames;         /**< tx_done() 完成的应答帧数 */
    unsigned tx_max_per_frame;  /**< 单帧最大 tx_octet 次数 */
    unsigned main_pumps;        /**< iolink_device_process() 调用次数 */
} wcet_metrics_t;

/* --- 合成 PHY 状态 ------------------------------------------------------ */

typedef struct
{
    uint8_t rx[WCET_RX_QUEUE_SIZE];
    size_t rx_head;
    size_t rx_tail;
    unsigned gap_pending; /**< 线路空闲/帧结束标记个数 */
    unsigned err_pending; /**< 下一次 rx_octet 返回的接收错误个数 */
    uint8_t tx[WCET_TX_MAX];
    size_t tx_len;
    uint8_t tx_active;
    uint8_t service_pending; /**< request_service() 拉起的"数据中断待处理" */
    int wakeup;              /**< detect_wakeup() 的一次性唤醒标志 */
    iolink_phy_mode_t mode;
    iolink_baudrate_t baudrate;
} wcet_phy_state_t;

typedef struct
{
    iolink_device_ctx_t ctx;
    iolink_device_config_t cfg;
} wcet_dev_t;

static wcet_phy_state_t g_phy;
static wcet_dev_t g_dev;
static wcet_metrics_t g_m;
static uint64_t g_now_us;

static iolink_m_seq_type_t g_type;
static uint8_t g_pd_in_len;
static uint8_t g_pd_out_len;
static uint8_t g_od_width;

static uint8_t g_pd_in[IOLINK_PD_IN_MAX_SIZE];
static char g_vendor_name[WCET_VENDOR_NAME_LEN + 1U];
static iolink_device_info_t g_device_info;
static uint8_t g_vendor_param[WCET_VENDOR_PARAM_LEN];
static uint8_t g_vendor_param_len;

#define WCET_CHECK(cond, message)                                                        \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            (void) fprintf(stderr, "[wcet] %s:%d: %s\n", __FILE__, __LINE__, (message)); \
            return -1;                                                                   \
        }                                                                                \
    } while (0)

/* --- 平台钩子 ----------------------------------------------------------- */

/* 设备层拒绝弱定义的空临界区；主机基准是单线程的，空实现即可。 */
void iolink_critical_enter(void)
{
}

void iolink_critical_exit(void)
{
}

static const iolink_od_entry_t k_wcet_od_entries[] = {
    IOLINK_OD_BYTES_STORAGE(WCET_VENDOR_PARAM_INDEX, IOLINK_OD_ACCESS_RW, IOLINK_OD_WRITE_VOLATILE,
                            IOLINK_OD_STORAGE_VOLATILE, WCET_VENDOR_PARAM_LEN, "VendorParameter",
                            g_vendor_param, &g_vendor_param_len),
};

static iolink_od_app_t g_wcet_od = {
    .entries = k_wcet_od_entries,
    .count = sizeof(k_wcet_od_entries) / sizeof(k_wcet_od_entries[0]),
};

/* --- 合成 PHY 实现 ------------------------------------------------------ */

static uint64_t wcet_time_us(iolink_app_t* self)
{
    (void) self;
    return g_now_us;
}

static void wcet_phy_reset(void)
{
    (void) memset(&g_phy, 0, sizeof(g_phy));
    g_phy.mode = IOLINK_PHY_MODE_INACTIVE;
    g_phy.baudrate = IOLINK_BAUDRATE_COM2;
}

static int wcet_phy_init(iolink_app_t* self)
{
    (void) self;
    g_phy.tx_len = 0U;
    g_phy.tx_active = 0U;
    g_phy.mode = IOLINK_PHY_MODE_INACTIVE;
    return 0;
}

static void wcet_phy_set_mode(iolink_app_t* self, iolink_phy_mode_t mode)
{
    (void) self;
    g_phy.mode = mode;
}

static void wcet_phy_set_baudrate(iolink_app_t* self, iolink_baudrate_t baudrate)
{
    (void) self;
    g_phy.baudrate = baudrate;
}

/** @brief 逐字节交付已排队的请求；错误/帧结束标记优先于空闲。 */
static int wcet_phy_rx_octet(iolink_app_t* self, uint8_t* data, uint8_t* status)
{
    (void) self;
    g_m.rx_octets++;
    if (g_phy.rx_tail != g_phy.rx_head) {
        *data = g_phy.rx[g_phy.rx_tail];
        g_phy.rx_tail++;
        *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if (g_phy.err_pending != 0U) {
        g_phy.err_pending--;
        *status = 0U;
        return -1;
    }
    if (g_phy.gap_pending != 0U) {
        g_phy.gap_pending--;
        *status = IOLINK_PHY_STATUS_CHK;
        return 0;
    }
    *status = 0U;
    return 0;
}

/** @brief 收集应答字节：第一个字节开启一个新帧。 */
static int wcet_phy_tx_octet(iolink_app_t* self, uint8_t data)
{
    (void) self;
    if (g_phy.tx_active == 0U) {
        g_phy.tx_active = 1U;
        g_phy.tx_len = 0U;
    }
    if (g_phy.tx_len >= sizeof(g_phy.tx)) {
        return -1;
    }
    g_phy.tx[g_phy.tx_len] = data;
    g_phy.tx_len++;
    g_m.tx_octets++;
    return 0;
}

static void wcet_phy_tx_done(iolink_app_t* self)
{
    (void) self;
    if (g_phy.tx_active == 0U) {
        return; /* RX 间隔释放，并非应答结束 */
    }
    g_phy.tx_active = 0U;
    g_m.tx_frames++;
    if (g_phy.tx_len > g_m.tx_max_per_frame) {
        g_m.tx_max_per_frame = (unsigned) g_phy.tx_len;
    }
}

/** @brief 收发器数据中断电平：接收字节、帧结束、发送中或软件请求服务。 */
static uint8_t wcet_phy_int_active(iolink_app_t* self)
{
    (void) self;
    if (g_phy.service_pending != 0U) {
        return 1U;
    }
    if (g_phy.tx_active != 0U) {
        return 1U;
    }
    if ((g_phy.err_pending != 0U) || (g_phy.gap_pending != 0U)) {
        return 1U;
    }
    return (g_phy.rx_tail != g_phy.rx_head) ? 1U : 0U;
}

static void wcet_phy_request_service(iolink_app_t* self)
{
    (void) self;
    g_phy.service_pending = 1U;
}

static int wcet_phy_detect_wakeup(iolink_app_t* self)
{
    (void) self;
    if (g_phy.wakeup != 0) {
        g_phy.wakeup = 0;
        return 1;
    }
    return 0;
}

static const iolink_phy_api_t g_phy_api = {
    .self = NULL,
    .init = wcet_phy_init,
    .set_mode = wcet_phy_set_mode,
    .set_baudrate = wcet_phy_set_baudrate,
    .rx_octet = wcet_phy_rx_octet,
    .tx_octet = wcet_phy_tx_octet,
    .tx_done = wcet_phy_tx_done,
    .int_active = wcet_phy_int_active,
    .request_service = wcet_phy_request_service,
    .detect_wakeup = wcet_phy_detect_wakeup,
};

static const iolink_device_app_t g_app = {.time_us = wcet_time_us};

/* --- 设备与泵驱动 ------------------------------------------------------- */

static void wcet_env_init(void)
{
    for (size_t i = 0U; i < WCET_VENDOR_NAME_LEN; i++) {
        g_vendor_name[i] = (char) ('A' + (i % 26U));
    }
    g_vendor_name[WCET_VENDOR_NAME_LEN] = '\0';

    (void) memset(&g_device_info, 0, sizeof(g_device_info));
    g_device_info.vendor_name = g_vendor_name;
    g_device_info.vendor_text = "vendor";
    g_device_info.product_name = "wcet-harness";
    g_device_info.product_id = "wcet-host";
    g_device_info.product_text = "path-length proxy harness";
    g_device_info.serial_number = "0000000000000001";
    g_device_info.hardware_revision = "1";
    g_device_info.firmware_revision = "1";
    g_device_info.vendor_id = 0x0123U;
    g_device_info.device_id = 0x010203U;
    g_device_info.profile_characteristic = 0x0001U;
    g_device_info.revision_id = 0x0111U;
    g_device_info.min_cycle_time = 0U;
}

static void wcet_metrics_reset(void)
{
    (void) memset(&g_m, 0, sizeof(g_m));
    g_phy.rx_head = 0U;
    g_phy.rx_tail = 0U;
    g_phy.gap_pending = 0U;
    g_phy.err_pending = 0U;
    g_phy.service_pending = 0U;
    g_phy.tx_len = 0U;
    g_phy.tx_active = 0U;
}

static int wcet_device_setup(iolink_m_seq_type_t type, uint8_t pd_in_len, uint8_t pd_out_len,
                             uint8_t od_width, uint8_t min_cycle_time)
{
    (void) memset(&g_dev, 0, sizeof(g_dev));
    wcet_phy_reset();
    g_now_us = 0U;
    g_vendor_param_len = 0U;
    (void) memset(g_vendor_param, 0, sizeof(g_vendor_param));

    g_type = type;
    g_pd_in_len = pd_in_len;
    g_pd_out_len = pd_out_len;
    g_od_width = od_width;

    for (size_t i = 0U; i < sizeof(g_pd_in); i++) {
        g_pd_in[i] = (uint8_t) (0x30U + i);
    }

    g_dev.cfg.phy = g_phy_api;
    g_dev.cfg.phy.self = NULL;
    g_dev.cfg.stack.m_seq_type = type;
    g_dev.cfg.stack.min_cycle_time = min_cycle_time;
    g_dev.cfg.stack.pd_in_len = pd_in_len;
    g_dev.cfg.stack.pd_out_len = pd_out_len;
    g_dev.cfg.stack.od_width = od_width;
    g_dev.cfg.stack.baudrate = IOLINK_BAUDRATE_COM2;
    g_dev.cfg.stack.baudrate_set = false;
    g_dev.cfg.stack.master_lost_cycles = 0U;
    g_dev.cfg.app = &g_app;
    g_dev.cfg.device_info = &g_device_info;
    g_dev.cfg.od = &g_wcet_od;

    const int rc = iolink_device_init(&g_dev.ctx, &g_dev.cfg);
    if (rc != 0) {
        (void) fprintf(stderr, "[wcet] iolink_device_init() = %d\n", rc);
        return -1;
    }
    return 0;
}

/** @brief 主泵一次迭代 + 固定虚拟时间步进。 */
static void wcet_main_step(void)
{
    g_m.main_pumps++;
    iolink_device_process(&g_dev.ctx);
    g_now_us += WCET_TIME_STEP_US;
}

/**
 * @brief 把数据中断泵跑到空闲。
 *
 * 每轮先清掉 request_service() 拉起的标志，再调用 ISR 泵；只要
 * int_active() 仍报告有待处理工作就继续。每个 ISR 调用计一次
 * isr_pumps，这就是中断入口的路径长度代理。
 */
static void wcet_pump_isr(void)
{
    for (unsigned i = 0U; i < WCET_PUMP_LIMIT; i++) {
        g_phy.service_pending = 0U;
        g_m.isr_pumps++;
        iolink_device_process_isr(&g_dev.ctx);
        if (wcet_phy_int_active(NULL) == 0U) {
            return;
        }
    }
}

/** @brief 排入一帧的字节；每个帧步骤都从空闲线路开始。 */
static void wcet_feed(const uint8_t* frame, size_t len, bool gap)
{
    g_phy.rx_head = 0U;
    g_phy.rx_tail = 0U;
    g_phy.gap_pending = 0U;
    g_phy.err_pending = 0U;
    for (size_t i = 0U; i < len; i++) {
        if (g_phy.rx_head < sizeof(g_phy.rx)) {
            g_phy.rx[g_phy.rx_head] = frame[i];
            g_phy.rx_head++;
        }
    }
    if (gap) {
        g_phy.gap_pending = 1U;
    }
}

static void wcet_note_frame_pumps(unsigned pumps)
{
    if (pumps > g_m.isr_max_per_frame) {
        g_m.isr_max_per_frame = pumps;
    }
}

/** @brief 一个完整交换：送帧 -> 泵 ISR -> 主泵 -> 泵 ISR 排空应答。 */
static void wcet_exchange(const uint8_t* frame, size_t len, bool gap)
{
    wcet_feed(frame, len, gap);
    const unsigned before = g_m.isr_pumps;
    wcet_pump_isr();
    wcet_main_step();
    wcet_pump_isr();
    g_m.frames++;
    wcet_note_frame_pumps(g_m.isr_pumps - before);
}

/** @brief 背靠背突发：只泵 ISR，不运行主泵，让邮箱承受突发。 */
static void wcet_send_no_main(const uint8_t* frame, size_t len, bool gap)
{
    wcet_feed(frame, len, gap);
    const unsigned before = g_m.isr_pumps;
    wcet_pump_isr();
    g_m.frames++;
    wcet_note_frame_pumps(g_m.isr_pumps - before);
}

static void wcet_publish_pd(void)
{
    /* harness 不注册 on_pd_input，因此走轮询 API。 */
    (void) iolink_device_pd_input_update(&g_dev.ctx, g_pd_in, g_pd_in_len, true);
}

/* --- 帧构造 ------------------------------------------------------------- */

static uint8_t wcet_type_bits(iolink_m_seq_type_t type)
{
    switch (type) {
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            return IOLINK_MSEQ_TYPE_1;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_3:
        case IOLINK_M_SEQ_TYPE_2_4:
        case IOLINK_M_SEQ_TYPE_2_5:
        case IOLINK_M_SEQ_TYPE_2_V:
            return IOLINK_MSEQ_TYPE_2;
        default:
            return IOLINK_MSEQ_TYPE_0;
    }
}

/** @brief Type-0 帧：MC、CKT[, OD]。od_byte < 0 表示无 OD。 */
static size_t wcet_build_type0(uint8_t mc, int od_byte, uint8_t* frame)
{
    size_t len = IOLINK_M_SEQ_HEADER_LEN;
    frame[0] = mc;
    frame[1] = IOLINK_MSEQ_TYPE_0;
    if (od_byte >= 0) {
        frame[2] = (uint8_t) od_byte;
        len++;
    }
    frame[1] = (uint8_t) (IOLINK_MSEQ_TYPE_0 | iolink_checksum6(frame, len));
    return len;
}

/** @brief Type-1/2 帧：MC、CKT、PD-out、OD；CKT 的类型位先清零。 */
static size_t wcet_build_ms(uint8_t type_bits, uint8_t mc, const uint8_t* pd_out, size_t pd_out_len,
                            const uint8_t* od, size_t od_len, uint8_t* frame)
{
    size_t len = 0U;
    frame[0] = mc;
    frame[1] = type_bits;
    len = IOLINK_M_SEQ_HEADER_LEN;
    if (pd_out_len > 0U) {
        (void) memcpy(&frame[len], pd_out, pd_out_len);
        len += pd_out_len;
    }
    if (od_len > 0U) {
        (void) memcpy(&frame[len], od, od_len);
        len += od_len;
    }
    frame[1] = (uint8_t) (type_bits | iolink_checksum6(frame, len));
    return len;
}

/* --- ISDU over M-sequence 辅助 ----------------------------------------- */

static uint8_t wcet_chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c = (uint8_t) (c ^ octets[i]);
    }
    return c;
}

/** @brief 与 ISDU 引擎相同的响应总长推导（独立实现，不借用被测代码）。 */
static size_t wcet_isdu_declared_total(const uint8_t* buf, size_t have)
{
    if (have == 0U) {
        return 0U;
    }
    const uint8_t len = (uint8_t) (buf[0] & 0x0FU);
    if (len == 0U) {
        return 1U; /* No Service / Busy */
    }
    if (len == 1U) {
        if (have < 2U) {
            return 0U;
        }
        const uint8_t ext = buf[1];
        return ((ext >= 17U) && (ext <= 238U)) ? (size_t) ext : 0U;
    }
    return (size_t) len;
}

/**
 * @brief 把一段 ISDU 请求按 OD 宽度分块，用 M-sequence 写送入设备。
 *
 * 每个 M-sequence 的请求布局为 MC、CKT、PD-out、OD；不足 od_width 的
 * 末段用 0 填充（ISDU 引擎按声明的总长停止）。
 */
static int wcet_isdu_send_request(const uint8_t* req, size_t len)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t od[IOLINK_OD_MAX_SIZE];
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t flowctrl = IOLINK_FLOWCTRL_START;
    size_t off = 0U;

    (void) memset(pd_out, 0, sizeof(pd_out));
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > g_od_width) {
            chunk = g_od_width;
        }
        (void) memset(od, 0, sizeof(od));
        (void) memcpy(od, &req[off], chunk);
        const size_t frame_len =
            wcet_build_ms(wcet_type_bits(g_type), (uint8_t) (IOLINK_MC_CHANNEL_ISDU | flowctrl),
                          pd_out, g_pd_out_len, od, g_od_width, frame);
        wcet_exchange(frame, frame_len, false);
        off += chunk;
        flowctrl = (flowctrl == IOLINK_FLOWCTRL_START)
                       ? 1U
                       : (uint8_t) ((flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    return 0;
}

/**
 * @brief 按 FlowCTRL 序列读回一段 ISDU 应答，直到声明的总长收齐。
 *
 * @param out     应答八位组（含 I-Service/长度/CHKPDU）。
 * @param cap     缓冲区容量。
 * @param out_len 实际收集到的字节数。
 * @return 声明的总长；解析失败时为 -1。
 */
static int wcet_isdu_read_response(uint8_t* out, size_t cap, size_t* out_len)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t flowctrl = IOLINK_FLOWCTRL_START;
    size_t got = 0U;
    size_t total = 0U;

    for (unsigned guard = 0U; guard < 64U; guard++) {
        const size_t frame_len =
            wcet_build_ms(wcet_type_bits(g_type),
                          (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | flowctrl), NULL,
                          0U, NULL, 0U, frame);
        wcet_exchange(frame, frame_len, false);
        WCET_CHECK(g_phy.tx_len > (size_t) g_od_width, "ISDU 读应答短于 OD 宽度 + CKS");

        size_t chunk = (size_t) g_od_width;
        if ((total != 0U) && (got + chunk > total)) {
            chunk = total - got;
        }
        if (chunk > (cap - got)) {
            chunk = cap - got;
        }
        (void) memcpy(&out[got], g_phy.tx, chunk);
        got += chunk;

        if (total == 0U) {
            total = wcet_isdu_declared_total(out, got);
            if (total == 0U) {
                *out_len = got;
                return -1;
            }
        }
        if (got >= total) {
            *out_len = got;
            return (int) total;
        }
        flowctrl = (flowctrl == IOLINK_FLOWCTRL_START)
                       ? 1U
                       : (uint8_t) ((flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    *out_len = got;
    return -1;
}

/* --- 场景公共步骤 ------------------------------------------------------- */

/** @brief 唤醒 -> 过渡命令 -> 首条合法帧，把设备带到 OPERATE。 */
static int wcet_enter_operate(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t od[IOLINK_OD_MAX_SIZE];
    size_t len;

    g_phy.wakeup = 1;
    wcet_main_step();
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM,
               "唤醒后未进入 AWAITING_COMM");

    len = wcet_build_type0(IOLINK_MC_TRANSITION_COMMAND, -1, frame);
    wcet_exchange(frame, len, true);
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_ESTAB_COM,
               "过渡命令后未进入 ESTAB_COM");

    if (g_type == IOLINK_M_SEQ_TYPE_0) {
        len = wcet_build_type0(0x00U, -1, frame);
        wcet_exchange(frame, len, true);
    }
    else {
        (void) memset(pd_out, 0, sizeof(pd_out));
        (void) memset(od, 0, sizeof(od));
        len = wcet_build_ms(wcet_type_bits(g_type), 0x00U, pd_out, g_pd_out_len, od, g_od_width,
                            frame);
        wcet_exchange(frame, len, false);
    }
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_OPERATE,
               "首条合法帧后未进入 OPERATE");
    return 0;
}

/** @brief 建场景：初始化 + 进入 OPERATE + 清零指标（指标不含建立通信开销）。 */
static int wcet_scenario_start(iolink_m_seq_type_t type, uint8_t pd_in_len, uint8_t pd_out_len,
                               uint8_t od_width, uint8_t min_cycle_time)
{
    if (wcet_device_setup(type, pd_in_len, pd_out_len, od_width, min_cycle_time) != 0) {
        return -1;
    }
    if (wcet_enter_operate() != 0) {
        return -1;
    }
    wcet_metrics_reset();
    wcet_publish_pd();
    return 0;
}

/* --- 场景 1/2：最大 PD/OD 帧（TYPE_2_V，32/32/32） ---------------------- */

static int scenario_pd_max_write(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t od[IOLINK_OD_MAX_SIZE];

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_2_V, IOLINK_PD_IN_MAX_SIZE,
                                   IOLINK_PD_OUT_MAX_SIZE, IOLINK_OD_MAX_SIZE, 10U) == 0,
               "场景初始化失败");

    for (unsigned iter = 0U; iter < 8U; iter++) {
        for (size_t i = 0U; i < sizeof(pd_out); i++) {
            pd_out[i] = (uint8_t) (iter * 13U + i);
            od[i] = (uint8_t) (0xA0U + i);
        }
        const size_t len = wcet_build_ms(wcet_type_bits(g_type), 0x00U, pd_out, g_pd_out_len, od,
                                         g_od_width, frame);
        wcet_exchange(frame, len, false);
        WCET_CHECK(g_phy.tx_len == (size_t) (g_pd_in_len + 1U), "写应答应为 PD-in + CKS");
        for (size_t i = 0U; i < (size_t) g_pd_in_len; i++) {
            WCET_CHECK(g_phy.tx[i] == g_pd_in[i], "写应答中的 PD-in 内容不符");
        }
    }
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_OPERATE,
               "场景结束时不在 OPERATE");
    return 0;
}

static int scenario_pd_max_read(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_2_V, IOLINK_PD_IN_MAX_SIZE,
                                   IOLINK_PD_OUT_MAX_SIZE, IOLINK_OD_MAX_SIZE, 10U) == 0,
               "场景初始化失败");

    (void) memset(pd_out, 0, sizeof(pd_out));
    for (unsigned iter = 0U; iter < 8U; iter++) {
        /* Type-2 READ 请求仍携带 PD-out 字节：MC、CKT、PD-out。 */
        const size_t len = wcet_build_ms(wcet_type_bits(g_type),
                                         (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PROCESS),
                                         pd_out, g_pd_out_len, NULL, 0U, frame);
        wcet_exchange(frame, len, false);
        WCET_CHECK(g_phy.tx_len == (size_t) (g_od_width + g_pd_in_len + 1U),
                   "读应答应为 OD + PD-in + CKS");
        for (size_t i = 0U; i < (size_t) g_pd_in_len; i++) {
            WCET_CHECK(g_phy.tx[(size_t) g_od_width + i] == g_pd_in[i],
                       "读应答中的 PD-in 内容不符");
        }
    }
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_OPERATE,
               "场景结束时不在 OPERATE");
    return 0;
}

/* --- 场景 3：背靠背同类型帧 -------------------------------------------- */

static int scenario_burst_write(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t od[IOLINK_OD_MAX_SIZE];

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_2_V, IOLINK_PD_IN_MAX_SIZE,
                                   IOLINK_PD_OUT_MAX_SIZE, IOLINK_OD_MAX_SIZE, 10U) == 0,
               "场景初始化失败");

    (void) memset(od, 0, sizeof(od));
    for (unsigned round = 0U; round < 4U; round++) {
        for (unsigned i = 0U; i < 4U; i++) {
            for (size_t k = 0U; k < sizeof(pd_out); k++) {
                pd_out[k] = (uint8_t) (round * 4U + i + k);
            }
            const size_t len = wcet_build_ms(wcet_type_bits(g_type), 0x00U, pd_out, g_pd_out_len,
                                             od, g_od_width, frame);
            wcet_send_no_main(frame, len, false);
        }
        /* 一次主泵处理邮箱里排队的帧；ISR 已即时应答全部突发帧。 */
        wcet_main_step();
        wcet_pump_isr();
    }
    WCET_CHECK(g_m.tx_frames == 16U, "背靠背突发未对每帧都应答");
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_OPERATE,
               "场景结束时不在 OPERATE");
    return 0;
}

/* --- 场景 4：ISDU 分块读 ----------------------------------------------- */

static int scenario_isdu_read_chunked(void)
{
    uint8_t req[3];
    uint8_t resp[IOLINK_ISDU_BUFFER_SIZE];
    uint8_t expected[IOLINK_ISDU_BUFFER_SIZE];
    size_t resp_len = 0U;
    size_t expected_len = 0U;

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_1_V, 0U, 0U, 32U, 10U) == 0, "场景初始化失败");

    /* 8 位索引读请求（I-Service 0x9，Length 0x3）：0x0010 VendorName。 */
    (void) memset(req, 0, sizeof(req));
    req[0] = 0x93U;
    req[1] = (uint8_t) IOLINK_IDX_VENDOR_NAME;
    req[2] = wcet_chkpdu(req, sizeof(req));
    WCET_CHECK(wcet_isdu_send_request(req, sizeof(req)) == 0, "ISDU 读请求发送失败");

    const int total = wcet_isdu_read_response(resp, sizeof(resp), &resp_len);
    WCET_CHECK(total > 0, "ISDU 读应答无法解析");

    expected[0] = 0xD1U; /* 读应答（+）、ExtLength 形式 */
    expected[1] = (uint8_t) (WCET_VENDOR_NAME_LEN + 3U);
    (void) memcpy(&expected[2], g_vendor_name, WCET_VENDOR_NAME_LEN);
    expected_len = 2U + WCET_VENDOR_NAME_LEN + 1U; /* 头 + 数据 + CHKPDU */
    expected[expected_len - 1U] = 0x00U;
    expected[expected_len - 1U] = wcet_chkpdu(expected, expected_len);

    WCET_CHECK(resp_len == expected_len, "ISDU 读应答长度不符");
    WCET_CHECK(total == (int) expected_len, "ISDU 声明的总长不符");
    WCET_CHECK(memcmp(resp, expected, expected_len) == 0, "ISDU VendorName 内容不符");
    WCET_CHECK(g_m.tx_frames == 4U, "分块读应产生 1 个写 ACK + 3 个读应答");
    return 0;
}

/* --- 场景 5：ISDU 分块写 + 回读 ---------------------------------------- */

static int scenario_isdu_write_chunked(void)
{
    uint8_t req[1U + 1U + 1U + WCET_VENDOR_PARAM_LEN + 1U];
    uint8_t resp[IOLINK_ISDU_BUFFER_SIZE];
    uint8_t ack[IOLINK_ISDU_BUFFER_SIZE];
    size_t resp_len = 0U;
    size_t ack_len = 0U;

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_1_V, 0U, 0U, 32U, 10U) == 0, "场景初始化失败");

    /* 写请求：I-Service 0x11（写、8 位索引、ExtLength）、ExtLength、
       索引 0x40、64 字节数据、CHKPDU = 68 个八位组 -> 3 个 OD 段。 */
    (void) memset(req, 0, sizeof(req));
    req[0] = 0x11U;
    req[1] = (uint8_t) sizeof(req);
    req[2] = (uint8_t) WCET_VENDOR_PARAM_INDEX;
    for (size_t i = 0U; i < WCET_VENDOR_PARAM_LEN; i++) {
        req[3U + i] = (uint8_t) (0x80U + i);
    }
    req[sizeof(req) - 1U] = wcet_chkpdu(req, sizeof(req));
    WCET_CHECK(wcet_isdu_send_request(req, sizeof(req)) == 0, "ISDU 写请求发送失败");
    WCET_CHECK(g_m.tx_frames == 3U, "分块写应产生 3 个 ACK");

    /* 写应答是固定的 2 八位组：0x52 + CHKPDU。 */
    WCET_CHECK(wcet_isdu_read_response(ack, sizeof(ack), &ack_len) == 2, "写应答无法解析");
    WCET_CHECK(ack[0] == 0x52U, "写应答不是 0x52");

    /* 回读：8 位索引读请求 0x0040，再按 32 字节 OD 分块收 67 字节。 */
    (void) memset(req, 0, 3U);
    req[0] = 0x93U;
    req[1] = (uint8_t) WCET_VENDOR_PARAM_INDEX;
    req[2] = wcet_chkpdu(req, 3U);
    WCET_CHECK(wcet_isdu_send_request(req, 3U) == 0, "ISDU 回读请求发送失败");

    const int total = wcet_isdu_read_response(resp, sizeof(resp), &resp_len);
    WCET_CHECK(total == (int) (WCET_VENDOR_PARAM_LEN + 3U), "回读总长不符");
    WCET_CHECK(resp_len == (size_t) total, "回读收集长度不符");
    WCET_CHECK(resp[0] == 0xD1U, "回读应答头不是 0xD1");
    for (size_t i = 0U; i < WCET_VENDOR_PARAM_LEN; i++) {
        WCET_CHECK(resp[2U + i] == (uint8_t) (0x80U + i), "回读数据与写入不一致");
    }
    return 0;
}

/* --- 场景 6：事件触发后的诊断读 ---------------------------------------- */

static int scenario_event_diag_read(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t od[IOLINK_OD_MAX_SIZE];

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_1_V, 0U, 0U, 32U, 10U) == 0, "场景初始化失败");

#if IOLINK_FEATURE_EVENTS
    iolink_events_ctx_t* events = iolink_device_get_events_ctx(&g_dev.ctx);
    WCET_CHECK(events != NULL, "事件上下文不可用");
    iolink_event_trigger(events, IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD, IOLINK_EVENT_TYPE_ERROR);
    wcet_main_step(); /* events_process() 把事件搬进队列/事件存储器 */

    /* 诊断通道读地址 0：返回事件存储器前 32 字节。 */
    size_t len = wcet_build_ms(wcet_type_bits(g_type),
                               (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS), NULL,
                               0U, NULL, 0U, frame);
    wcet_exchange(frame, len, false);
    WCET_CHECK(g_phy.tx_len == (size_t) (g_od_width + 1U), "诊断读应答长度不符");
    WCET_CHECK((g_phy.tx[0] & IOLINK_EVENT_BIT_STATUS) != 0U, "诊断读未置事件标志");

    /* 写地址 0 确认事件（Table 60 T5）。 */
    (void) memset(od, 0, sizeof(od));
    len = wcet_build_ms(wcet_type_bits(g_type), (uint8_t) IOLINK_MC_CHANNEL_DIAGNOSIS, NULL, 0U, od,
                        g_od_width, frame);
    wcet_exchange(frame, len, false);
    WCET_CHECK(g_phy.tx_len == 1U, "诊断写应答应只含 CKS");

    /* 再次读取：事件标志与存储器都已清除。 */
    len = wcet_build_ms(wcet_type_bits(g_type),
                        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS), NULL, 0U, NULL,
                        0U, frame);
    wcet_exchange(frame, len, false);
    WCET_CHECK(g_phy.tx[0] == 0x00U, "确认后事件标志未清除");
#endif
    return 0;
}

/* --- 场景 7：SIO/fallback 边界 ----------------------------------------- */

static int scenario_sio_fallback(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    size_t len;

    /* TYPE_0 + MinCycleTime = 1 ms -> T_FBD = 3 ms。 */
    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_0, 0U, 0U, 1U, 10U) == 0, "场景初始化失败");

    len = wcet_build_type0((uint8_t) (IOLINK_MC_CHANNEL_PAGE | 0x00U), IOLINK_CMD_FALLBACK, frame);
    wcet_exchange(frame, len, true);
    WCET_CHECK(g_phy.tx_len == 1U, "FALLBACK 应答应只含 CKS");
    WCET_CHECK(iolink_device_get_phy_mode(&g_dev.ctx) == IOLINK_PHY_MODE_SDCI,
               "T_FBD 到期前不应离开 SDCI");

    /* 推进 5 ms（> T_FBD 3 ms）：设备回到 SIO。 */
    for (unsigned i = 0U; i < WCET_FALLBACK_STEPS; i++) {
        wcet_main_step();
    }
    WCET_CHECK(iolink_device_get_phy_mode(&g_dev.ctx) == IOLINK_PHY_MODE_SIO,
               "T_FBD 到期后未回到 SIO");

    /* 再次唤醒 -> AWAITING_COMM，然后一次 Type-0 页读启动探测。 */
    g_phy.wakeup = 1;
    wcet_main_step();
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_AWAITING_COMM,
               "再次唤醒未进入 AWAITING_COMM");

    len = wcet_build_type0((uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE), -1, frame);
    wcet_exchange(frame, len, true);
    WCET_CHECK(g_phy.tx_len == 2U, "启动探测应答应为 OD + CKS");
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_PREOPERATE,
               "启动探测未进入 PREOPERATE");
    return 0;
}

/* --- 场景 8：无效/错误帧 ----------------------------------------------- */

static int scenario_invalid_frames(void)
{
    uint8_t frame[WCET_FRAME_MAX];
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE];
    uint8_t od[IOLINK_OD_MAX_SIZE];
    iolink_dll_stats_t stats;

    WCET_CHECK(wcet_scenario_start(IOLINK_M_SEQ_TYPE_2_V, IOLINK_PD_IN_MAX_SIZE,
                                   IOLINK_PD_OUT_MAX_SIZE, IOLINK_OD_MAX_SIZE, 10U) == 0,
               "场景初始化失败");

    (void) memset(pd_out, 0x11, sizeof(pd_out));
    (void) memset(od, 0x22, sizeof(od));

    /* 1) 校验和损坏：CKT 的低 6 位翻转。 */
    size_t len =
        wcet_build_ms(wcet_type_bits(g_type), 0x00U, pd_out, g_pd_out_len, od, g_od_width, frame);
    frame[1] = (uint8_t) (frame[1] ^ 0x01U);
    const unsigned tx_before = g_m.tx_frames;
    wcet_exchange(frame, len, false);
    WCET_CHECK(g_m.tx_frames == tx_before, "校验和损坏的帧不应被应答");

    /* 2) 截断：只送 5 字节后线路空闲。 */
    len = wcet_build_ms(wcet_type_bits(g_type), 0x00U, pd_out, g_pd_out_len, od, g_od_width, frame);
    wcet_feed(frame, 5U, true);
    g_m.frames++;
    wcet_pump_isr();
    wcet_main_step();
    WCET_CHECK(g_m.tx_frames == tx_before, "截断帧不应被应答");

    /* 3) 接收错误：rx_octet() 返回 -1。 */
    g_phy.err_pending = 1U;
    wcet_pump_isr();
    WCET_CHECK(g_m.tx_frames == tx_before, "接收错误不应产生应答");

    /* 4) 保留的 M-sequence 类型位 0xC0。 */
    frame[0] = 0x00U;
    frame[1] = IOLINK_MSEQ_TYPE_RESERVED;
    frame[2] = 0x00U;
    wcet_feed(frame, 3U, true);
    g_m.frames++;
    wcet_pump_isr();
    wcet_main_step();
    WCET_CHECK(g_m.tx_frames == tx_before, "保留类型不应被应答");

    /* 5) 链路恢复：一条合法帧仍被应答。 */
    len = wcet_build_ms(wcet_type_bits(g_type), 0x00U, pd_out, g_pd_out_len, od, g_od_width, frame);
    wcet_exchange(frame, len, false);
    WCET_CHECK(g_m.tx_frames == (tx_before + 1U), "无效帧之后合法帧未被应答");
    WCET_CHECK(iolink_device_get_state(&g_dev.ctx) == IOLINK_DLL_STATE_OPERATE,
               "无效帧不应改变 OPERATE 状态");

    iolink_device_get_dll_stats(&g_dev.ctx, &stats);
    WCET_CHECK((stats.crc_errors + stats.framing_errors) >= 3U, "无效帧未计入 CRC/成帧错误统计");
    return 0;
}

/* --- 场景表与输出 ------------------------------------------------------- */

typedef struct
{
    const char* name;
    const char* description;
    int (*run)(void);
} wcet_scenario_t;

static const wcet_scenario_t g_scenarios[] = {
    {"pd_max_write", "TYPE_2_V 32B PD-in/out + 32B OD 的 PD 写帧", scenario_pd_max_write},
    {"pd_max_read", "TYPE_2_V 最大 PD/OD 的 PD 读帧", scenario_pd_max_read},
    {"burst_write", "4x4 背靠背同类型 PD 写帧（单次主泵）", scenario_burst_write},
    {"isdu_read_chunked", "63B VendorName 分块读（32B OD）", scenario_isdu_read_chunked},
    {"isdu_write_chunked", "64B 厂商参数分块写 + 回读", scenario_isdu_write_chunked},
    {"event_diag_read", "事件触发 -> 诊断读 -> 确认 -> 再读", scenario_event_diag_read},
    {"sio_fallback", "FALLBACK -> T_FBD -> SIO -> 重新唤醒", scenario_sio_fallback},
    {"invalid_frames", "坏校验/截断/接收错误/保留类型 + 恢复", scenario_invalid_frames},
};

static const char* wcet_rx_mode_name(void)
{
#if IOLINK_ISR_RX_MODE == IOLINK_ISR_RX_FRAME
    return "FRAME";
#else
    return "BYTE";
#endif
}

static const char* wcet_tx_mode_name(void)
{
#if IOLINK_ISR_TX_MODE == IOLINK_ISR_TX_FRAME
    return "FRAME";
#else
    return "BYTE";
#endif
}

static void wcet_print_config(FILE* out)
{
    (void) fprintf(out,
                   "config: isr_rx=%s isr_tx=%s pd_max=%u od_max=%u isdu_buf=%u "
                   "time_step=%uus\n",
                   wcet_rx_mode_name(), wcet_tx_mode_name(), (unsigned) IOLINK_PD_IN_MAX_SIZE,
                   (unsigned) IOLINK_OD_MAX_SIZE, (unsigned) IOLINK_ISDU_BUFFER_SIZE,
                   (unsigned) WCET_TIME_STEP_US);
}

static void wcet_print_header(FILE* out)
{
    (void) fprintf(out, "%-20s %7s %9s %8s %9s %8s %7s %6s %10s\n", "scenario", "frames",
                   "isr_pumps", "isr_max", "rx_octets", "tx_octets", "tx_frm", "tx_max",
                   "main_pumps");
}

static void wcet_print_row(FILE* out, const char* name, const wcet_metrics_t* m)
{
    (void) fprintf(out, "%-20s %7u %9u %8u %9u %8u %7u %6u %10u\n", name, m->frames, m->isr_pumps,
                   m->isr_max_per_frame, m->rx_octets, m->tx_octets, m->tx_frames,
                   m->tx_max_per_frame, m->main_pumps);
}

static void wcet_write_json(FILE* out, const wcet_metrics_t* results)
{
    (void) fprintf(out, "{\n");
    (void) fprintf(out, "  \"config\": {\n");
    (void) fprintf(out, "    \"isr_rx_mode\": \"%s\",\n", wcet_rx_mode_name());
    (void) fprintf(out, "    \"isr_tx_mode\": \"%s\",\n", wcet_tx_mode_name());
    (void) fprintf(out, "    \"time_step_us\": %u,\n", (unsigned) WCET_TIME_STEP_US);
    (void) fprintf(out, "    \"pd_in_max\": %u,\n", (unsigned) IOLINK_PD_IN_MAX_SIZE);
    (void) fprintf(out, "    \"od_max\": %u,\n", (unsigned) IOLINK_OD_MAX_SIZE);
    (void) fprintf(out, "    \"isdu_buffer\": %u\n", (unsigned) IOLINK_ISDU_BUFFER_SIZE);
    (void) fprintf(out, "  },\n");
    (void) fprintf(out, "  \"scenarios\": {\n");
    for (size_t i = 0U; i < (sizeof(g_scenarios) / sizeof(g_scenarios[0])); i++) {
        const wcet_metrics_t* m = &results[i];
        (void) fprintf(out,
                       "    \"%s\": {\"frames\": %u, \"isr_pumps\": %u, "
                       "\"isr_max_per_frame\": %u, \"rx_octets\": %u, \"tx_octets\": %u, "
                       "\"tx_frames\": %u, \"tx_max_per_frame\": %u, \"main_pumps\": %u}%s\n",
                       g_scenarios[i].name, m->frames, m->isr_pumps, m->isr_max_per_frame,
                       m->rx_octets, m->tx_octets, m->tx_frames, m->tx_max_per_frame, m->main_pumps,
                       (i + 1U < (sizeof(g_scenarios) / sizeof(g_scenarios[0]))) ? "," : "");
    }
    (void) fprintf(out, "  }\n");
    (void) fprintf(out, "}\n");
}

static int wcet_metrics_equal(const wcet_metrics_t* a, const wcet_metrics_t* b)
{
    return (memcmp(a, b, sizeof(*a)) == 0) ? 1 : 0;
}

static void wcet_usage(const char* argv0)
{
    (void) fprintf(stderr,
                   "用法: %s [--json FILE] [--repeat N] [--list]\n"
                   "  --json FILE   把指标写成 JSON（供 wcet_run.sh --check 比较）\n"
                   "  --repeat N    每个场景重复 N 次并要求指标完全一致（确定性校验）\n"
                   "  --list        只列出场景名与说明\n",
                   argv0);
}

int main(int argc, char** argv)
{
    const char* json_path = NULL;
    unsigned repeat = 1U;
    bool list_only = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            if ((i + 1) >= argc) {
                wcet_usage(argv[0]);
                return 2;
            }
            json_path = argv[++i];
        }
        else if (strcmp(argv[i], "--repeat") == 0) {
            if ((i + 1) >= argc) {
                wcet_usage(argv[0]);
                return 2;
            }
            repeat = (unsigned) strtoul(argv[++i], NULL, 10);
            if (repeat == 0U) {
                repeat = 1U;
            }
        }
        else if (strcmp(argv[i], "--list") == 0) {
            list_only = true;
        }
        else if ((strcmp(argv[i], "--help") == 0) || (strcmp(argv[i], "-h") == 0)) {
            wcet_usage(argv[0]);
            return 0;
        }
        else {
            (void) fprintf(stderr, "[wcet] 未知参数: %s\n", argv[i]);
            wcet_usage(argv[0]);
            return 2;
        }
    }

    wcet_env_init();

    const size_t count = sizeof(g_scenarios) / sizeof(g_scenarios[0]);
    if (list_only) {
        for (size_t i = 0U; i < count; i++) {
            (void) printf("%-20s %s\n", g_scenarios[i].name, g_scenarios[i].description);
        }
        return 0;
    }

    wcet_metrics_t results[sizeof(g_scenarios) / sizeof(g_scenarios[0])];
    wcet_print_config(stdout);
    wcet_print_header(stdout);

    for (size_t i = 0U; i < count; i++) {
        bool have_first = false;
        for (unsigned r = 0U; r < repeat; r++) {
            (void) memset(&g_m, 0, sizeof(g_m));
            const int rc = g_scenarios[i].run();
            if (rc != 0) {
                (void) fprintf(stderr, "[wcet] 场景 '%s' 失败 (repeat %u)\n", g_scenarios[i].name,
                               r + 1U);
                return 1;
            }
            if (!have_first) {
                results[i] = g_m;
                have_first = true;
            }
            else if (!wcet_metrics_equal(&results[i], &g_m)) {
                (void) fprintf(stderr, "[wcet] 场景 '%s' 指标不确定（repeat %u）\n",
                               g_scenarios[i].name, r + 1U);
                (void) fprintf(stderr, "  第 1 次: ");
                wcet_print_row(stderr, g_scenarios[i].name, &results[i]);
                (void) fprintf(stderr, "  第 %u 次: ", r + 1U);
                wcet_print_row(stderr, g_scenarios[i].name, &g_m);
                return 1;
            }
        }
        wcet_print_row(stdout, g_scenarios[i].name, &results[i]);
    }

    (void) printf(
        "\n注：以上为路径长度代理指标（调用次数），不是 CPU 周期 WCET；\n"
        "    周期上界须用目标端 DWT 周期计数器测量（见 tools/wcet/README.md）。\n");

    if (json_path != NULL) {
        FILE* out = fopen(json_path, "wb");
        if (out == NULL) {
            (void) fprintf(stderr, "[wcet] 无法写入 %s\n", json_path);
            return 2;
        }
        wcet_write_json(out, results);
        if (fclose(out) != 0) {
            (void) fprintf(stderr, "[wcet] 写入 %s 失败\n", json_path);
            return 2;
        }
    }
    return 0;
}
