/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/* cfmakeraw() 是 BSD/GNU 扩展；需在任何 libc 头文件之前请求它，以便
   在严格的 libc 配置下也能看到其声明。 */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

/**
 * @file phy_virtual.c
 * @brief 由 POSIX 串口 / 伪终端支撑的虚拟 PHY。
 * @ingroup iolinki_phy_virtual
 *
 * 在主机文件描述符（TTY 或管道）之上实现 PHY API，使设备
 * 协议栈在开发和测试期间可以通过回环或虚拟串行链路
 * 进行验证。
 */

#include "iolinki/phy_virtual.h"

#if (IOLINK_ISR_RX_MODE != IOLINK_ISR_RX_BYTE) || (IOLINK_ISR_TX_MODE != IOLINK_ISR_TX_BYTE)
/*
 * 该 PHY 是字节流：它只实现 BYTE 钩子，无法交付或接受整帧。
 * 保留这些公开符号，使库和主机示例在 FRAME 构建中仍能链接，
 * 但那里没有可用的虚拟 PHY。需要可用 PHY 的测试必须保持在
 * BYTE 配置上。
 */
void iolink_phy_virtual_set_port(const char* port)
{
    (void) port;
}

const iolink_phy_api_t* iolink_phy_virtual_get(void)
{
    return NULL;
}

#else /* BYTE/BYTE */

#include <stdio.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

static int g_fd = -1;
static const char* g_port_path = NULL;

/* 协议栈能发送的最大 M-sequence（见 IOLINK_M_SEQ_MAX_LEN）。 */
#define VIRTUAL_TX_MAX (IOLINK_PD_OUT_MAX_SIZE + IOLINK_OD_MAX_SIZE + 2U)

static iolink_phy_mode_t g_mode = IOLINK_PHY_MODE_INACTIVE;
static uint8_t g_tx_buf[VIRTUAL_TX_MAX];
static uint8_t g_tx_len = 0U;
static uint8_t g_tx_active = 0U;

void iolink_phy_virtual_set_port(const char* port)
{
    g_port_path = port;
}

/** @brief 以 raw、非阻塞模式打开并配置后端串口。 */
static int virtual_init(void* user)
{
    (void) user;

    if (g_port_path == NULL) {
        printf("[PHY-VIRTUAL] Error: Port not set\n");
        return -1;
    }

    g_fd = open(g_port_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_fd < 0) {
        printf("[PHY-VIRTUAL] Error opening %s: %s\n", g_port_path, strerror(errno));
        return -1;
    }

    /* 若为 TTY，则配置为 raw 模式 */
    if (isatty(g_fd)) {
        struct termios tty;
        if (tcgetattr(g_fd, &tty) != 0) {
            printf("[PHY-VIRTUAL] Error from tcgetattr: %s\n", strerror(errno));
            return -1;
        }

        cfmakeraw(&tty);

        /* 设置超时：不阻塞 */
        tty.c_cc[VMIN] = 0U;
        tty.c_cc[VTIME] = 0U;

        if (tcsetattr(g_fd, TCSANOW, &tty) != 0) {
            printf("[PHY-VIRTUAL] Error from tcsetattr: %s\n", strerror(errno));
            return -1;
        }
        printf("[PHY-VIRTUAL] Initialized TTY connection to %s (fd=%d)\n", g_port_path, g_fd);
    }
    else {
        printf("[PHY-VIRTUAL] Initialized non-TTY connection to %s (fd=%d)\n", g_port_path, g_fd);
    }
    g_mode = IOLINK_PHY_MODE_INACTIVE;
    g_tx_len = 0U;
    g_tx_active = 0U;
    return 0;
}

/** @brief 记录请求的 PHY 模式（对虚拟链路无电气影响）。 */
static void virtual_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    /* int_active() 仅在 SDCI 中声明需要服务，这与真实收发器一致：
     * 在 SIO 中唤醒路径属于 detect_wakeup()，因此泵不得把 0x55
     * 标记当作数据 octet 消费。 */
    g_mode = mode;
    printf("[PHY-VIRTUAL] Mode set to: %d\n", (int) mode);
}

/** @brief 记录请求的波特率（对虚拟链路无影响）。 */
static void virtual_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    printf("[PHY-VIRTUAL] Baudrate set to: %d\n", (int) baudrate);
}

/** @brief 为 ISR 组帧引擎读取一个接收到的 octet（非阻塞）。 */
static int virtual_rx_octet(void* user, uint8_t* data, uint8_t* status)
{
    (void) user;

    if ((g_fd < 0) || (data == NULL) || (status == NULL)) {
        return 0;
    }

    ssize_t n = read(g_fd, data, 1);
    if (n == 1) {
        *status = IOLINK_PHY_STATUS_DATA_READY;
        return 1;
    }
    if ((n < 0) && (errno != EAGAIN) && (errno != EWOULDBLOCK)) {
        return -1;
    }
    return 0;
}

/** @brief 缓冲一个回复 octet；tx_done() 会刷新整帧。 */
static int virtual_tx_octet(void* user, uint8_t data)
{
    (void) user;

    if ((size_t) g_tx_len >= sizeof(g_tx_buf)) {
        return -1;
    }
    g_tx_buf[g_tx_len] = data;
    g_tx_len++;
    g_tx_active = 1U;
    return 0;
}

/** @brief 将缓冲的回复刷新到 PTY 并释放线路。 */
static void virtual_tx_done(void* user)
{
    (void) user;

    if (g_fd >= 0) {
        size_t offset = 0U;
        while (offset < (size_t) g_tx_len) {
            ssize_t n = write(g_fd, &g_tx_buf[offset], (size_t) g_tx_len - offset);
            if (n <= 0) {
                break;
            }
            offset += (size_t) n;
        }
    }
    g_tx_len = 0U;
    g_tx_active = 0U;
}

/** @brief 数据中断电平：发送进行中或已有接收 octet 可用。 */
static uint8_t virtual_int_active(void* user)
{
    (void) user;

    /* 只有 SDCI 使用组帧泵；在 SIO 中唤醒字节由 detect_wakeup()
     * 消费，因此在此声明需要服务会使启动失步。 */
    if (g_mode != IOLINK_PHY_MODE_SDCI) {
        return 0U;
    }
    if (g_tx_active != 0U) {
        return 1U;
    }
    if (g_fd < 0) {
        return 0U;
    }

    struct pollfd pfd;
    pfd.fd = g_fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    const int ready = poll(&pfd, 1, 0);
    if (ready <= 0) {
        return 0U;
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return 0U;
    }
    return ((pfd.revents & POLLIN) != 0) ? 1U : 0U;
}

/** @brief 请求 ISR 泵立即运行（主机示例每个周期轮询）。 */
static void virtual_request_service(void* user)
{
    (void) user;
}

#if IOLINK_PHY_FEATURE_WAKEUP
/** @brief 在缓冲输入中扫描 0x55 唤醒标记字节。 */
static int virtual_detect_wakeup(void* user)
{
    (void) user;

    if (g_fd < 0) {
        return 0;
    }

    /* 持续读取字节，直到找到 0x55 或数据耗尽 */
    uint8_t b;
    while (read(g_fd, &b, 1) > 0) {
        if (b == 0x55) {
            return 1;
        }
    }
    return 0;
}
#endif

static const iolink_phy_api_t g_phy_virtual = {
    .init = virtual_init,
    .set_mode = virtual_set_mode,
    .set_baudrate = virtual_set_baudrate,
    .rx_octet = virtual_rx_octet,
    .tx_octet = virtual_tx_octet,
    .tx_done = virtual_tx_done,
    .int_active = virtual_int_active,
    .request_service = virtual_request_service,
#if IOLINK_PHY_FEATURE_WAKEUP
    .detect_wakeup = virtual_detect_wakeup,
#endif
};

const iolink_phy_api_t* iolink_phy_virtual_get(void)
{
    return &g_phy_virtual;
}

#endif /* BYTE/BYTE */
