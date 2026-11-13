# Copyright (C) 2026 Andrii Shylenko
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 独立小工程：在主机上直接编译 src/*.c（默认配置），用合成 PHY 驱动
# iolink_dll_process_isr() 的应答路径，测量 t_soft 主机代理（墙钟 ns）。
# 测量的是主机墙钟时延，不是目标周期：绝对值跨机器无意义，只做同机、
# 同核心、同构建的相对比较。目标周期上界用目标端 DWT（见 README.md）。
#
# 用法：cmake -S tools/isr_latency -B build_isr_latency
#       cmake --build build_isr_latency --parallel
#       ./build_isr_latency/isr_latency --json tools/isr_latency_baseline.json

cmake_minimum_required(VERSION 3.16)
project(iolinki_isr_latency LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

set(IOLINKI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# 直接编译整个 src/，不经过 iolinki 静态库：默认特性开关、默认 BYTE/BYTE
# ISR 传输。新增 src 文件由 CONFIGURE_DEPENDS 自动拾取。
file(GLOB IOLINKI_SOURCES CONFIGURE_DEPENDS "${IOLINKI_ROOT}/src/*.c")

add_executable(isr_latency latency_harness.c ${IOLINKI_SOURCES})

target_include_directories(isr_latency PRIVATE "${IOLINKI_ROOT}/include")
target_compile_options(isr_latency PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(isr_latency PRIVATE
    IOLINKI_ROOT="${IOLINKI_ROOT}"
    ISR_LATENCY_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
