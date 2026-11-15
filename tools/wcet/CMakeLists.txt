# Copyright (C) 2026 Andrii Shylenko
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 独立小工程：在主机上直接编译 src/*.c（默认配置），用合成 PHY 驱动完整设备，
# 输出各场景的调用次数路径长度指标。测量的是调用次数，不是 CPU 周期；周期上界
# 必须用目标端 DWT（见 README.md）。
#
# 用法：cmake -S tools/wcet -B build/wcet && cmake --build build/wcet
#       ./build/wcet/wcet_harness

cmake_minimum_required(VERSION 3.16)
project(iolinki_wcet LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
endif()

set(IOLINKI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# 直接编译整个 src/，不经过 iolinki 静态库：默认特性开关、默认 BYTE/BYTE ISR
# 传输。新增 src 文件由 CONFIGURE_DEPENDS 自动拾取。
file(GLOB IOLINKI_SOURCES CONFIGURE_DEPENDS "${IOLINKI_ROOT}/src/*.c")

add_executable(wcet_harness wcet_harness.c ${IOLINKI_SOURCES})

target_include_directories(wcet_harness PRIVATE "${IOLINKI_ROOT}/include")
target_compile_options(wcet_harness PRIVATE -Wall -Wextra -Werror)
