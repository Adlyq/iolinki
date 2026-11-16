/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file latency_harness.c
 * @brief 主机 ISR 时延代理：测量 iolink_dll_process_isr() 应答路径的墙钟时延。
 *
 * 测量窗口与 t_soft 口径一致：
 *   起点 = 帧末 process_isr 调用入口（合成 PHY 以 CHK/字节计数定界）
 *   终点 = 首个 tx_octet 返回（首字节交付 PHY；无应答帧取调用返回点）
 * 另报细分项 call_ns = 帧末 process_isr 单次调用时长（纯组装段）。
 *
 * 结果只在同机、同核心、同构建之间可比：本工具启动时钉核心
 * （sched_setaffinity）、校验 governor 为 performance、快照 CPU 频率、
 * 尽力 SCHED_FIFO，并把机器信息写入基线 JSON 头；--check 模式在机器
 * 不一致时直接失败。详见 README.md。
 */

#define _GNU_SOURCE

#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "iolinki/crc.h"
#include "iolinki/internal/dll_ctx.h"
#include "iolinki/iolink.h"
#include "iolinki/isdu.h"
#include "iolinki/protocol.h"

#define RX_MAX 80U
#define TX_MAX 80U
#define MAX_SCENARIOS 16U
#define MAX_BASELINE_SCENARIOS 32U
#define DEFAULT_BASELINE "tools/isr_latency_baseline.json"

/* ------------------------------------------------------------------ */
/* 合成 PHY 与测量状态（单线程，无易失需求）                           */
/* ------------------------------------------------------------------ */

static iolink_dll_ctx_t s_dll;
static uint64_t s_now_us;

static uint8_t s_rx_q[RX_MAX];
static size_t s_rx_n;
static size_t s_rx_i;
static uint8_t s_chk_pending;

static uint8_t s_tx_q[TX_MAX];
static size_t s_tx_n;
static uint8_t s_tx_active;

static int s_measure_armed;
static int s_t1_set;
static uint64_t s_t1_ns;

static uint64_t now_ns(void)
{
    struct timespec ts;
    (void) clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ((uint64_t) ts.tv_sec * 1000000000ULL) + (uint64_t) ts.tv_nsec;
}

static uint64_t plan_time_us(iolink_app_t* self)
{
    (void) self;
    return s_now_us;
}

static int lat_rx_octet(iolink_app_t* self, uint8_t* data, uint8_t* status)
{
    (void) self;
    *status = 0U;
    if (s_rx_i < s_rx_n) {
        *data = s_rx_q[s_rx_i++];
        return 1;
    }
    if (s_chk_pending != 0U) {
        s_chk_pending = 0U;
        *status = IOLINK_PHY_STATUS_CHK;
    }
    return 0;
}

static int lat_tx_octet(iolink_app_t* self, uint8_t data)
{
    (void) self;
    (void) data;
    if (s_tx_active == 0U) {
        s_tx_n = 0U;
        s_tx_active = 1U;
    }
    if (s_tx_n < sizeof(s_tx_q)) {
        s_tx_q[s_tx_n++] = data;
    }
    if ((s_measure_armed != 0) && (s_t1_set == 0)) {
        s_t1_ns = now_ns();
        s_t1_set = 1;
    }
    return 0;
}

static void lat_tx_done(iolink_app_t* self)
{
    (void) self;
    s_tx_active = 0U;
}

static uint8_t lat_int_active(iolink_app_t* self)
{
    const iolink_dll_ctx_t* ctx = (const iolink_dll_ctx_t*) self;
    return ((s_rx_i < s_rx_n) || (s_chk_pending != 0U) || (ctx->main.shared.isr_tx_active != 0U))
               ? 1U
               : 0U;
}

static const iolink_phy_api_t s_phy = {
    .self = (iolink_app_t*) &s_dll,
    .rx_octet = lat_rx_octet,
    .tx_octet = lat_tx_octet,
    .tx_done = lat_tx_done,
    .int_active = lat_int_active,
};

/* ------------------------------------------------------------------ */
/* 场景                                                               */
/* ------------------------------------------------------------------ */

typedef void (*scenario_init_fn)(void);

typedef struct
{
    const char* name;
    scenario_init_fn init;
    uint8_t frame[8];
    uint8_t len;
    uint8_t expect_tx;   /* 健全性：完整排空后的应答字节数 */
    uint8_t expect_miss; /* 健全性：isr_plan_misses 期望值 */
    uint8_t check_first; /* 1 = 校验首个应答字节 == expect_first */
    uint8_t expect_first;
} scenario_t;

static void harness_reset(void)
{
    s_rx_n = 0U;
    s_rx_i = 0U;
    s_chk_pending = 0U;
    s_tx_n = 0U;
    s_tx_active = 0U;
}

static void init_preop(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_PREOPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_0;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 0U;
    s_dll.main.pd_valid = true;
    harness_reset();
    iolink_dll_publish_reply_plans(&s_dll.main);
}

static void init_op_t1_od32(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_1_V;
    s_dll.main.shared.od_width = 32U;
    s_dll.main.shared.pd_in_len = 0U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 0U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    harness_reset();
    iolink_dll_publish_reply_plans(&s_dll.main);
}

static void init_op_t2_pd32(void)
{
    (void) memset(&s_dll, 0, sizeof(s_dll));
    iolink_dll_init(&s_dll.main, &s_dll.isr, &s_phy);
    s_dll.main.shared.clock.time_us = plan_time_us;
    s_dll.main.shared.state = IOLINK_DLL_STATE_OPERATE;
    s_dll.main.shared.m_seq_type = IOLINK_M_SEQ_TYPE_2_V;
    s_dll.main.shared.od_width = 1U;
    s_dll.main.shared.pd_in_len = 32U;
    s_dll.main.shared.pd_out_len_current = 0U;
    s_dll.main.pd_in_len_current = 32U;
    s_dll.main.pd_valid = true;
    s_dll.main.shared.pd_in_active = 0U;
    for (uint8_t i = 0U; i < 32U; i++) {
        s_dll.main.shared.pd_in[0][1U + i] = (uint8_t) (0x5AU + i);
    }
    harness_reset();
    iolink_dll_publish_reply_plans(&s_dll.main);
}

static void init_preop_isdu_read(void)
{
    init_preop();
    /* 已完成的 4 字节应答作为当前读视图（与 test_reply_plan 相同契约）。 */
    s_dll.main.shared.isdu.state = ISDU_STATE_RESPONSE_READY;
    s_dll.main.shared.isdu.resp_buf[0] = 0xD4U;
    s_dll.main.shared.isdu.resp_buf[1] = 0x12U;
    s_dll.main.shared.isdu.resp_buf[2] = 0x34U;
    s_dll.main.shared.isdu.resp_buf[3] = 0xF2U;
    s_dll.main.shared.isdu.resp_total = 4U;
    iolink_dll_process(&s_dll.main); /* 发布读视图快照 */
}

/** @brief PREOPERATE，但计划表已失效：任何合法帧都按"计划未命中"丢弃。 */
static void init_preop_no_plans(void)
{
    init_preop();
    s_dll.main.shared.reply_plan_seq = (IOLINK_ATOMIC_UINT_T) 0U;
}

static scenario_t s_scenarios[MAX_SCENARIOS];
static uint32_t s_scenario_count;

/** @brief 补全 CKT：类型位 | A.1.6 CKS（数据字节须已就位）。 */
static void finish_frame(uint8_t* frame, uint8_t len)
{
    frame[1] = (uint8_t) (frame[1] & IOLINK_MSEQ_TYPE_MASK);
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
}

static void build_scenarios(void)
{
    scenario_t* sc;

    sc = &s_scenarios[0];
    sc->name = "type0_page_read";
    sc->init = init_preop;
    sc->frame[0] = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U);
    sc->frame[1] = IOLINK_MSEQ_TYPE_0;
    sc->len = 2U;
    sc->expect_tx = 2U;
    sc->expect_miss = 0U;
    sc->check_first = 0U;

    sc = &s_scenarios[1];
    sc->name = "type0_page_write_cks";
    sc->init = init_preop;
    sc->frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_PAGE | 0x01U);
    sc->frame[1] = IOLINK_MSEQ_TYPE_0;
    sc->frame[2] = 0x4FU;
    sc->len = 3U;
    sc->expect_tx = 1U;
    sc->expect_miss = 0U;
    sc->check_first = 0U;

    sc = &s_scenarios[2];
    sc->name = "type0_isdu_read_start";
    sc->init = init_preop_isdu_read;
    sc->frame[0] = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START);
    sc->frame[1] = IOLINK_MSEQ_TYPE_0;
    sc->len = 2U;
    sc->expect_tx = 2U;
    sc->expect_miss = 0U;
    sc->check_first = 1U;
    sc->expect_first = 0xD4U;

    sc = &s_scenarios[3];
    sc->name = "type12_pd_read_tail";
    sc->init = init_op_t2_pd32;
    sc->frame[0] = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PROCESS);
    sc->frame[1] = IOLINK_MSEQ_TYPE_2;
    sc->len = 2U;
    sc->expect_tx = 34U;
    sc->expect_miss = 0U;
    sc->check_first = 0U;

    sc = &s_scenarios[4];
    sc->name = "type1_page_read_od32";
    sc->init = init_op_t1_od32;
    sc->frame[0] = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE);
    sc->frame[1] = IOLINK_MSEQ_TYPE_1;
    sc->len = 2U;
    sc->expect_tx = 33U;
    sc->expect_miss = 0U;
    sc->check_first = 0U;

    sc = &s_scenarios[5];
    sc->name = "isdu_write_segment";
    sc->init = init_op_t2_pd32;
    sc->frame[0] = (uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START);
    sc->frame[1] = IOLINK_MSEQ_TYPE_2;
    sc->frame[2] = 0x14U;
    sc->len = 3U;
    sc->expect_tx = 33U; /* Type-1/2 写应答：[PD-in 32B]CKS */
    sc->expect_miss = 0U;
    sc->check_first = 0U;

    sc = &s_scenarios[6];
    sc->name = "plan_miss";
    sc->init = init_preop_no_plans;
    sc->frame[0] = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U);
    sc->frame[1] = IOLINK_MSEQ_TYPE_0;
    sc->len = 2U;
    sc->expect_tx = 0U;
    sc->expect_miss = 1U;
    sc->check_first = 0U;

    sc = &s_scenarios[7];
    sc->name = "invalid_frame";
    sc->init = init_preop;
    sc->frame[0] = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PAGE | 0x01U);
    sc->frame[1] = IOLINK_MSEQ_TYPE_0;
    sc->len = 2U;
    sc->expect_tx = 0U;
    sc->expect_miss = 0U; /* 无效帧不计入计划未命中 */
    sc->check_first = 0U;

    s_scenario_count = 8U;
    for (uint32_t i = 0U; i < s_scenario_count; i++) {
        finish_frame(s_scenarios[i].frame, s_scenarios[i].len);
    }
    /* 坏校验：完成后再破坏 CKS。 */
    s_scenarios[7].frame[1] ^= 0x80U;
}

/* ------------------------------------------------------------------ */
/* 测量                                                               */
/* ------------------------------------------------------------------ */

static void measure_once(const scenario_t* sc, uint64_t* win_ns, uint64_t* call_ns)
{
    sc->init();
    (void) memcpy(s_rx_q, sc->frame, sc->len);
    s_rx_n = sc->len;
    s_rx_i = 0U;
    s_chk_pending = 1U;
    s_t1_set = 0;
    s_measure_armed = 1;

    const uint64_t t0 = now_ns();
    iolink_dll_process_isr(&s_dll.main);
    const uint64_t t_call = now_ns();

    /* BYTE 模式若把首字节留到后续泵拍，最多补 96 拍。 */
    uint32_t guard = 0U;
    while ((s_t1_set == 0) && (s_dll.main.shared.isr_tx_active != 0U) && (guard < 96U)) {
        iolink_dll_process_isr(&s_dll.main);
        guard++;
    }
    const uint64_t t1 = (s_t1_set != 0) ? s_t1_ns : t_call;
    s_measure_armed = 0;

    *win_ns = t1 - t0;
    *call_ns = t_call - t0;
}

/** @brief 健全性：场景行为与预期一致（应答长度、miss 计数、首字节）。 */
static int scenario_sanity(const scenario_t* sc)
{
    sc->init();
    (void) memcpy(s_rx_q, sc->frame, sc->len);
    s_rx_n = sc->len;
    s_rx_i = 0U;
    s_chk_pending = 1U;
    iolink_dll_process_isr(&s_dll.main);
    uint32_t guard = 0U;
    while ((s_dll.main.shared.isr_tx_active != 0U) && (guard < 96U)) {
        iolink_dll_process_isr(&s_dll.main);
        guard++;
    }
    if (s_tx_n != sc->expect_tx) {
        (void) fprintf(stderr, "sanity [%s]: tx=%zu expected=%u\n", sc->name, s_tx_n,
                       (unsigned) sc->expect_tx);
        return 1;
    }
    if (s_dll.main.shared.isr_plan_misses != sc->expect_miss) {
        (void) fprintf(stderr, "sanity [%s]: misses=%u expected=%u\n", sc->name,
                       (unsigned) s_dll.main.shared.isr_plan_misses, (unsigned) sc->expect_miss);
        return 1;
    }
    if ((sc->check_first != 0U) && (s_tx_q[0] != sc->expect_first)) {
        (void) fprintf(stderr, "sanity [%s]: first=0x%02X expected=0x%02X\n", sc->name, s_tx_q[0],
                       sc->expect_first);
        return 1;
    }
    return 0;
}

static int cmp_u64(const void* a, const void* b)
{
    const uint64_t x = *(const uint64_t*) a;
    const uint64_t y = *(const uint64_t*) b;
    return (x > y) ? 1 : ((x < y) ? -1 : 0);
}

typedef struct
{
    uint64_t median;
    uint64_t p99;
    uint64_t p95;
    uint64_t vmin;
    uint64_t vmax;
    uint64_t call_median;
    uint64_t call_p99;
    uint64_t call_min;
} scenario_stats_t;

static void stats_of(const uint64_t* a, uint32_t n, uint64_t* median, uint64_t* p99, uint64_t* p95,
                     uint64_t* vmin, uint64_t* vmax)
{
    uint64_t* sorted = (uint64_t*) malloc((size_t) n * sizeof(uint64_t));
    if (sorted == NULL) {
        (void) fprintf(stderr, "out of memory\n");
        exit(3);
    }
    (void) memcpy(sorted, a, (size_t) n * sizeof(uint64_t));
    qsort(sorted, (size_t) n, sizeof(uint64_t), cmp_u64);
    *median = sorted[n / 2U];
    *p99 = sorted[((uint64_t) (n - 1U) * 99U) / 100U];
    *p95 = sorted[((uint64_t) (n - 1U) * 95U) / 100U];
    *vmin = sorted[0U];
    *vmax = sorted[n - 1U];
    free(sorted);
}

static void measure_scenario(const scenario_t* sc, uint32_t warmup, uint32_t iters,
                             scenario_stats_t* out)
{
    uint64_t* win = (uint64_t*) malloc((size_t) iters * sizeof(uint64_t));
    uint64_t* call = (uint64_t*) malloc((size_t) iters * sizeof(uint64_t));
    if ((win == NULL) || (call == NULL)) {
        (void) fprintf(stderr, "out of memory\n");
        exit(3);
    }
    for (uint32_t i = 0U; i < warmup; i++) {
        uint64_t w;
        uint64_t c;
        measure_once(sc, &w, &c);
    }
    for (uint32_t i = 0U; i < iters; i++) {
        measure_once(sc, &win[i], &call[i]);
    }
    stats_of(win, iters, &out->median, &out->p99, &out->p95, &out->vmin, &out->vmax);
    uint64_t call_p95;
    uint64_t call_max;
    stats_of(call, iters, &out->call_median, &out->call_p99, &call_p95, &out->call_min, &call_max);
    free(win);
    free(call);
}

/* ------------------------------------------------------------------ */
/* 环境与机器信息                                                     */
/* ------------------------------------------------------------------ */

typedef struct
{
    char hostname[64];
    char cpu_model[128];
    char kernel[96];
    char compiler[96];
    char build_type[16];
    char git[64];
    char governor[16];
    char sched[16];
    int core;
    int smt;      /* 1 = 该核有 SMT 兄弟，0 = 无，-1 = 未知 */
    int isolated; /* 1 = 在 /sys/devices/system/cpu/isolated 中，-1 = 未知 */
    long freq_min_khz;
    long freq_max_khz;
    long freq_before_khz;
    long freq_after_khz;
    uint32_t iters;
    uint32_t warmup;
} env_info_t;

static void read_git_describe(char* out, size_t n)
{
    const char* cmd = "git -C \"" IOLINKI_ROOT "\" describe --always --dirty 2>/dev/null";
    FILE* fp = popen(cmd, "r");
    out[0] = '\0';
    if (fp == NULL) {
        return;
    }
    if (fgets(out, (int) n, fp) == NULL) {
        out[0] = '\0';
    }
    (void) pclose(fp);
    out[strcspn(out, "\n")] = '\0';
}

static void read_cpu_model(char* out, size_t n)
{
    FILE* fp = fopen("/proc/cpuinfo", "r");
    char line[256];
    out[0] = '\0';
    if (fp == NULL) {
        return;
    }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "model name", 10U) == 0) {
            const char* colon = strchr(line, ':');
            if (colon != NULL) {
                const char* v = colon + 1;
                while ((*v == ' ') || (*v == '\t')) {
                    v++;
                }
                (void) snprintf(out, n, "%s", v);
                out[strcspn(out, "\n")] = '\0';
                break;
            }
        }
    }
    (void) fclose(fp);
}

static int read_cpu_file_int(int core, const char* leaf, long* out);

/** @brief 当前频率：cpuinfo_cur_freq 不可用时退回 scaling_cur_freq。 */
static int read_cur_freq_khz(int core, long* out)
{
    if (read_cpu_file_int(core, "cpuinfo_cur_freq", out) == 0) {
        return 0;
    }
    return read_cpu_file_int(core, "scaling_cur_freq", out);
}

static int read_cpu_file_int(int core, const char* leaf, long* out)
{
    char path[96];
    char buf[64];
    (void) snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/%s", core, leaf);
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        return -1;
    }
    if (fgets(buf, sizeof(buf), fp) == NULL) {
        (void) fclose(fp);
        return -1;
    }
    (void) fclose(fp);
    *out = strtol(buf, NULL, 10);
    return 0;
}

static void read_governor(int core, char* out, size_t n)
{
    char path[96];
    (void) snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor",
                    core);
    FILE* fp = fopen(path, "r");
    if (fp == NULL) {
        (void) snprintf(out, n, "unavailable");
        return;
    }
    if (fgets(out, (int) n, fp) == NULL) {
        (void) snprintf(out, n, "unavailable");
    }
    (void) fclose(fp);
    out[strcspn(out, "\n")] = '\0';
}

/** @brief 钉核心并验证；失败即中止（钉核是测量有效性的前提）。 */
static void pin_core(int core)
{
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(core, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        (void) fprintf(stderr, "sched_setaffinity(core %d) failed: %s\n", core, strerror(errno));
        exit(4);
    }
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) != 0) {
        (void) fprintf(stderr, "sched_getaffinity failed: %s\n", strerror(errno));
        exit(4);
    }
    if ((CPU_COUNT(&set) != 1) || (CPU_ISSET(core, &set) == 0)) {
        (void) fprintf(stderr, "affinity verification failed: not pinned to core %d alone\n", core);
        exit(4);
    }
}

/** @brief 解析 "0-3,5,7-9" 形式的 CPU 列表，返回 @p cpu 是否在列表中。 */
static int cpu_in_list(const char* list, int cpu)
{
    const char* p = list;
    while (*p != '\0') {
        if ((*p >= '0') && (*p <= '9')) {
            char* end = NULL;
            const long lo = strtol(p, &end, 10);
            long hi = lo;
            p = end;
            if (*p == '-') {
                p++;
                hi = strtol(p, &end, 10);
                p = end;
            }
            if ((cpu >= lo) && (cpu <= hi)) {
                return 1;
            }
        }
        else {
            p++;
        }
    }
    return 0;
}

/** @brief 统计 CPU 列表中的核数（用于识别 SMT 兄弟）。 */
static int cpu_list_count(const char* list)
{
    int n = 0;
    const char* p = list;
    while (*p != '\0') {
        if ((*p >= '0') && (*p <= '9')) {
            char* end = NULL;
            const long lo = strtol(p, &end, 10);
            long hi = lo;
            p = end;
            if (*p == '-') {
                p++;
                hi = strtol(p, &end, 10);
                p = end;
            }
            n += (int) (hi - lo + 1L);
        }
        else {
            p++;
        }
    }
    return n;
}

static void read_topology(env_info_t* e, int core)
{
    char path[96];
    char buf[64];
    e->smt = -1;
    e->isolated = -1;
    (void) snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%d/topology/thread_siblings_list", core);
    FILE* fp = fopen(path, "r");
    if (fp != NULL) {
        if (fgets(buf, sizeof(buf), fp) != NULL) {
            e->smt = (cpu_list_count(buf) > 1) ? 1 : 0;
        }
        (void) fclose(fp);
    }
    fp = fopen("/sys/devices/system/cpu/isolated", "r");
    if (fp != NULL) {
        e->isolated = 0;
        if (fgets(buf, sizeof(buf), fp) != NULL) {
            e->isolated = cpu_in_list(buf, core);
        }
        (void) fclose(fp);
    }
}

static void collect_env(env_info_t* e, int core, uint32_t iters, uint32_t warmup)
{
    struct utsname uts;
    (void) memset(e, 0, sizeof(*e));
    if (gethostname(e->hostname, sizeof(e->hostname)) != 0) {
        (void) snprintf(e->hostname, sizeof(e->hostname), "unknown");
    }
    read_cpu_model(e->cpu_model, sizeof(e->cpu_model));
    if (uname(&uts) == 0) {
        (void) snprintf(e->kernel, sizeof(e->kernel), "%.40s %.40s", uts.sysname, uts.release);
    }
    (void) snprintf(e->compiler, sizeof(e->compiler), "gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__,
                    __GNUC_PATCHLEVEL__);
    (void) snprintf(e->build_type, sizeof(e->build_type), "%s", ISR_LATENCY_BUILD_TYPE);
    read_git_describe(e->git, sizeof(e->git));
    read_topology(e, core);
    read_governor(core, e->governor, sizeof(e->governor));
    if (read_cur_freq_khz(core, &e->freq_before_khz) != 0) {
        e->freq_before_khz = -1L;
    }
    if (read_cpu_file_int(core, "scaling_min_freq", &e->freq_min_khz) != 0) {
        e->freq_min_khz = -1L;
    }
    if (read_cpu_file_int(core, "scaling_max_freq", &e->freq_max_khz) != 0) {
        e->freq_max_khz = -1L;
    }
    e->core = core;
    e->iters = iters;
    e->warmup = warmup;
    (void) snprintf(e->sched, sizeof(e->sched), "%s", "SCHED_OTHER");
}

static void try_fifo(env_info_t* e)
{
    struct sched_param sp;
    sp.sched_priority = 1;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) == 0) {
        (void) snprintf(e->sched, sizeof(e->sched), "%s", "SCHED_FIFO");
    }
    else {
        (void) fprintf(stderr, "warning: SCHED_FIFO unavailable (%s), running SCHED_OTHER\n",
                       strerror(errno));
    }
}

/* ------------------------------------------------------------------ */
/* JSON 基线与门                                                      */
/* ------------------------------------------------------------------ */

typedef struct
{
    char name[48];
    uint64_t median_ns;
    uint64_t p99_ns;
    uint64_t min_ns;
    uint64_t call_median_ns;
    uint64_t call_p99_ns;
    uint64_t call_min_ns;
    int present;
} baseline_entry_t;

typedef struct
{
    char hostname[64];
    char cpu_model[128];
    int core;
    uint32_t iters;
    baseline_entry_t entry[MAX_BASELINE_SCENARIOS];
    uint32_t count;
} baseline_t;

static int parse_json_string(const char* line, const char* key, char* out, size_t n)
{
    char pat[64];
    (void) snprintf(pat, sizeof(pat), "\"%s\": \"", key);
    const char* p = strstr(line, pat);
    if (p == NULL) {
        return -1;
    }
    p += strlen(pat);
    size_t i = 0U;
    while ((p[i] != '\0') && (p[i] != '"') && (i + 1U < n)) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return 0;
}

static int parse_json_num(const char* line, const char* key, uint64_t* out)
{
    char pat[64];
    (void) snprintf(pat, sizeof(pat), "\"%s\": ", key);
    const char* p = strstr(line, pat);
    if (p == NULL) {
        return -1;
    }
    p += strlen(pat);
    *out = (uint64_t) strtoull(p, NULL, 10);
    return 0;
}

static int load_baseline(const char* path, baseline_t* b)
{
    FILE* fp = fopen(path, "r");
    char line[256];
    (void) memset(b, 0, sizeof(*b));
    if (fp == NULL) {
        return -1;
    }
    baseline_entry_t* cur = NULL;
    while (fgets(line, sizeof(line), fp) != NULL) {
        char buf[160];
        uint64_t num = 0U;
        if (parse_json_string(line, "hostname", buf, sizeof(buf)) == 0) {
            (void) snprintf(b->hostname, sizeof(b->hostname), "%.63s", buf);
        }
        else if (parse_json_string(line, "cpu_model", buf, sizeof(buf)) == 0) {
            (void) snprintf(b->cpu_model, sizeof(b->cpu_model), "%.127s", buf);
        }
        else if (parse_json_num(line, "core", &num) == 0) {
            b->core = (int) num;
        }
        else if (parse_json_num(line, "iters", &num) == 0) {
            b->iters = (uint32_t) num;
        }
        else if (parse_json_string(line, "name", buf, sizeof(buf)) == 0) {
            if (b->count < MAX_BASELINE_SCENARIOS) {
                cur = &b->entry[b->count++];
                (void) snprintf(cur->name, sizeof(cur->name), "%.47s", buf);
            }
        }
        else if ((cur != NULL) && (parse_json_num(line, "median_ns", &num) == 0)) {
            cur->median_ns = num;
        }
        else if ((cur != NULL) && (parse_json_num(line, "p99_ns", &num) == 0)) {
            cur->p99_ns = num;
        }
        else if ((cur != NULL) && (parse_json_num(line, "min_ns", &num) == 0)) {
            cur->min_ns = num;
        }
        else if ((cur != NULL) && (parse_json_num(line, "call_median_ns", &num) == 0)) {
            cur->call_median_ns = num;
        }
        else if ((cur != NULL) && (parse_json_num(line, "call_p99_ns", &num) == 0)) {
            cur->call_p99_ns = num;
        }
        else if ((cur != NULL) && (parse_json_num(line, "call_min_ns", &num) == 0)) {
            cur->call_min_ns = num;
        }
    }
    (void) fclose(fp);
    return 0;
}

static void write_json(const char* path, const env_info_t* e, const scenario_stats_t* stats)
{
    FILE* fp = fopen(path, "w");
    if (fp == NULL) {
        (void) fprintf(stderr, "cannot write %s: %s\n", path, strerror(errno));
        exit(3);
    }
    (void) fprintf(fp, "{\n");
    (void) fprintf(fp, "  \"tool\": \"isr_latency\",\n");
    (void) fprintf(fp, "  \"version\": 1,\n");
    (void) fprintf(fp, "  \"machine\": {\n");
    (void) fprintf(fp, "    \"hostname\": \"%s\",\n", e->hostname);
    (void) fprintf(fp, "    \"cpu_model\": \"%s\",\n", e->cpu_model);
    (void) fprintf(fp, "    \"kernel\": \"%s\",\n", e->kernel);
    (void) fprintf(fp, "    \"compiler\": \"%s\",\n", e->compiler);
    (void) fprintf(fp, "    \"build_type\": \"%s\",\n", e->build_type);
    (void) fprintf(fp, "    \"core\": %d,\n", e->core);
    (void) fprintf(fp, "    \"smt\": %d,\n", e->smt);
    (void) fprintf(fp, "    \"isolated\": %d,\n", e->isolated);
    (void) fprintf(fp, "    \"freq_min_khz\": %ld,\n", e->freq_min_khz);
    (void) fprintf(fp, "    \"freq_max_khz\": %ld,\n", e->freq_max_khz);
    (void) fprintf(fp, "    \"governor\": \"%s\",\n", e->governor);
    (void) fprintf(fp, "    \"sched\": \"%s\",\n", e->sched);
    (void) fprintf(fp, "    \"git\": \"%s\",\n", e->git);
    (void) fprintf(fp, "    \"iters\": %u\n", e->iters);
    (void) fprintf(fp, "  },\n");
    (void) fprintf(fp, "  \"scenarios\": [\n");
    for (uint32_t i = 0U; i < s_scenario_count; i++) {
        (void) fprintf(fp, "    {\n");
        (void) fprintf(fp, "      \"name\": \"%s\",\n", s_scenarios[i].name);
        (void) fprintf(fp, "      \"median_ns\": %" PRIu64 ",\n", stats[i].median);
        (void) fprintf(fp, "      \"p99_ns\": %" PRIu64 ",\n", stats[i].p99);
        (void) fprintf(fp, "      \"p95_ns\": %" PRIu64 ",\n", stats[i].p95);
        (void) fprintf(fp, "      \"min_ns\": %" PRIu64 ",\n", stats[i].vmin);
        (void) fprintf(fp, "      \"max_ns\": %" PRIu64 ",\n", stats[i].vmax);
        (void) fprintf(fp, "      \"call_median_ns\": %" PRIu64 ",\n", stats[i].call_median);
        (void) fprintf(fp, "      \"call_p99_ns\": %" PRIu64 ",\n", stats[i].call_p99);
        (void) fprintf(fp, "      \"call_min_ns\": %" PRIu64 ",\n", stats[i].call_min);
        (void) fprintf(fp, "      \"median_over_min_pct\": %" PRIu64 "\n",
                       (stats[i].vmin != 0U)
                           ? (((stats[i].median * 100U) + (stats[i].vmin / 2U)) / stats[i].vmin)
                           : 0U);
        (void) fprintf(fp, "    }%s\n", (i + 1U < s_scenario_count) ? "," : "");
    }
    (void) fprintf(fp, "  ]\n");
    (void) fprintf(fp, "}\n");
    (void) fclose(fp);
}

/** @brief 硬门：min 是确定性代码最稳的估计量，允许上浮 5%。 */
static int gate_min(uint64_t cur, uint64_t base)
{
    return (cur * 100ULL) <= (base * 105ULL);
}

/** @brief 噪声检查：中位数受调度干扰，超过基线 30% 只告警。 */
static int median_sanity(uint64_t cur, uint64_t base)
{
    return (cur * 100ULL) <= (base * 130ULL);
}

static int check_baseline(const baseline_t* b, const scenario_stats_t* stats, uint32_t iters)
{
    int fail = 0;
    if ((b->iters != 0U) && (b->iters != iters)) {
        (void) fprintf(stderr,
                       "warning: baseline used %u iterations, current run uses %u; p99 "
                       "comparison is noisy\n",
                       b->iters, iters);
    }
    for (uint32_t i = 0U; i < b->count; i++) {
        const baseline_entry_t* be = &b->entry[i];
        int found = 0;
        for (uint32_t j = 0U; j < s_scenario_count; j++) {
            if (strcmp(be->name, s_scenarios[j].name) != 0) {
                continue;
            }
            found = 1;
            const scenario_stats_t* st = &stats[j];
            const int ok =
                gate_min(st->vmin, be->min_ns) && gate_min(st->call_min, be->call_min_ns);
            if (!ok) {
                (void) fprintf(stderr,
                               "FAIL %-22s min %" PRIu64 "->%" PRIu64 " ns  call_min %" PRIu64
                               "->%" PRIu64 " ns",
                               be->name, be->min_ns, st->vmin, be->call_min_ns, st->call_min);
                if (!median_sanity(st->median, be->median_ns)) {
                    (void) fprintf(stderr,
                                   "  (median also high: %" PRIu64 "->%" PRIu64
                                   " ns - environment noise or real regression)",
                                   be->median_ns, st->median);
                }
                (void) fprintf(stderr, "\n");
                fail = 1;
            }
            else if (!median_sanity(st->median, be->median_ns)) {
                (void) fprintf(stderr,
                               "warning: %-22s median %" PRIu64 "->%" PRIu64
                               " ns (min stable) - environment noise suspected\n",
                               be->name, be->median_ns, st->median);
            }
            break;
        }
        if (!found) {
            (void) fprintf(stderr, "WARN baseline scenario '%s' not in current run\n", be->name);
        }
    }
    return fail;
}

/* ------------------------------------------------------------------ */
/* 主流程                                                             */
/* ------------------------------------------------------------------ */

static void usage(const char* argv0)
{
    (void) fprintf(stderr,
                   "usage: %s [--core N] [--iters N] [--warmup N] [--json PATH]\n"
                   "             [--no-rt] [--check [PATH]] [--list]\n"
                   "  --core N      pin to CPU core N (default 0)\n"
                   "  --iters N     measured iterations per scenario (default 100000)\n"
                   "  --warmup N    warmup iterations per scenario (default 2000)\n"
                   "  --json PATH   write baseline JSON to PATH\n"
                   "                (default tools/isr_latency_baseline.json)\n"
                   "  --no-rt       do not attempt SCHED_FIFO\n"
                   "  --check PATH  compare against baseline PATH\n"
                   "                (default tools/isr_latency_baseline.json);\n"
                   "                the baseline is not overwritten\n"
                   "  --list        list scenarios and exit\n",
                   argv0);
    exit(4);
}

int main(int argc, char** argv)
{
    int core = 0;
    uint32_t iters = 100000U;
    uint32_t warmup = 2000U;
    const char* json_path = DEFAULT_BASELINE;
    const char* check_path = NULL;
    int json_explicit = 0;
    int no_rt = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--core") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
            }
            core = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--iters") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
            }
            iters = (uint32_t) strtoul(argv[++i], NULL, 10);
            if (iters == 0U) {
                iters = 1U;
            }
        }
        else if (strcmp(argv[i], "--warmup") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
            }
            warmup = (uint32_t) strtoul(argv[++i], NULL, 10);
        }
        else if (strcmp(argv[i], "--json") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
            }
            json_path = argv[++i];
            json_explicit = 1;
        }
        else if (strcmp(argv[i], "--no-rt") == 0) {
            no_rt = 1;
        }
        else if (strcmp(argv[i], "--check") == 0) {
            if ((i + 1 < argc) && (argv[i + 1][0] != '-')) {
                check_path = argv[++i];
            }
            else {
                check_path = DEFAULT_BASELINE;
            }
        }
        else if (strcmp(argv[i], "--list") == 0) {
            build_scenarios();
            for (uint32_t k = 0U; k < s_scenario_count; k++) {
                (void) printf("%s\n", s_scenarios[k].name);
            }
            return 0;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
        }
        else {
            usage(argv[0]);
        }
    }

    build_scenarios();
    s_now_us = 1000000ULL;
    pin_core(core);

    env_info_t env;
    collect_env(&env, core, iters, warmup);
    if (no_rt == 0) {
        try_fifo(&env);
    }

    (void) printf("host: %s | cpu: %s | core: %d | gov: %s | sched: %s | git: %s | iters: %u\n",
                  env.hostname, env.cpu_model, env.core, env.governor, env.sched, env.git,
                  env.iters);
    if (strcmp(env.governor, "performance") != 0) {
        (void) fprintf(stderr, "warning: governor is '%s', not 'performance'; results may drift\n",
                       env.governor);
    }
    if (env.freq_before_khz < 0L) {
        (void) fprintf(stderr,
                       "warning: current CPU frequency unavailable (intel_pstate?); cannot "
                       "verify clock stability\n");
    }
    if (env.smt == 1) {
        (void) fprintf(stderr,
                       "note: core %d shares an SMT sibling; the remaining noise comes from "
                       "that sibling. Host-side isolcpus/cpuset would remove it.\n",
                       core);
    }
    if (env.isolated == 0) {
        (void) fprintf(stderr,
                       "note: no isolated CPUs configured; the core is pinned but not "
                       "exclusively reserved (min-based gate absorbs the interference).\n");
    }

    baseline_t base;
    int have_base = 0;
    if (check_path != NULL) {
        if (load_baseline(check_path, &base) != 0) {
            (void) fprintf(stderr, "cannot read baseline %s\n", check_path);
            return 2;
        }
        have_base = 1;
    }

    scenario_stats_t stats[MAX_SCENARIOS];
    for (uint32_t i = 0U; i < s_scenario_count; i++) {
        const scenario_t* sc = &s_scenarios[i];
        if (scenario_sanity(sc) != 0) {
            return 3;
        }
        measure_scenario(sc, warmup, iters, &stats[i]);
        (void) printf(
            "%-22s median %7" PRIu64 " ns  p99 %7" PRIu64 " ns  p95 %7" PRIu64 " ns  min %6" PRIu64
            "  max %7" PRIu64
            "  noise %5.2fx"
            " | call: median %7" PRIu64 "  p99 %7" PRIu64 " ns\n",
            sc->name, stats[i].median, stats[i].p99, stats[i].p95, stats[i].vmin, stats[i].vmax,
            (stats[i].vmin != 0U) ? ((double) stats[i].median / (double) stats[i].vmin) : 0.0,
            stats[i].call_median, stats[i].call_p99);
    }

    if (read_cur_freq_khz(core, &env.freq_after_khz) != 0) {
        env.freq_after_khz = -1L;
    }
    if ((env.freq_before_khz > 0L) && (env.freq_after_khz > 0L) &&
        (env.freq_after_khz != env.freq_before_khz)) {
        (void) fprintf(stderr, "warning: CPU frequency drifted %ld -> %ld kHz during run\n",
                       env.freq_before_khz, env.freq_after_khz);
    }

    /* 检查模式默认不覆盖基线；只有显式 --json 才落盘本次结果。 */
    if ((check_path == NULL) || (json_explicit != 0)) {
        write_json(json_path, &env, stats);
    }

    if (have_base != 0) {
        const int machine_ok = (strcmp(base.hostname, env.hostname) == 0) &&
                               (strcmp(base.cpu_model, env.cpu_model) == 0) &&
                               (base.core == env.core);
        if (!machine_ok) {
            (void) fprintf(stderr,
                           "baseline mismatch (baseline: %s / %s / core %d, current: %s / %s / "
                           "core %d): comparison invalid, re-baseline on this machine and core\n",
                           base.hostname, base.cpu_model, base.core, env.hostname, env.cpu_model,
                           env.core);
            return 2;
        }
        const int fail = check_baseline(&base, stats, env.iters);
        if (fail != 0) {
            (void) fprintf(stderr, "gate: FAIL (some scenario regressed beyond +5%%)\n");
            return 1;
        }
        (void) printf("gate: PASS (all scenarios within +5%% of baseline)\n");
    }
    return 0;
}
