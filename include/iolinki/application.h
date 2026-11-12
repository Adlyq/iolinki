/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_APPLICATION_H
#define IOLINK_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/dll.h"

/**
 * @brief 在编译期选择 PD 输入路径。
 *
 * 0：应用在主循环中调用 iolink_device_pd_input_update()；
 *    on_pd_input 回调成员不存在（对它赋值会导致编译
 *    错误）。
 * 1：DLL 在每个 OPERATE 周期调用 on_pd_input() 填充一个空闲的 PD-in
 *    缓冲区；iolink_device_pd_input_update() 未被声明（调用它会导致
 *    编译/链接错误，参见 device.h 中的陷阱宏）。
 *
 * 混用两条路径是不可能的：未使用的符号/成员会从头文件中
 * 移除，而不是等到运行时才失败。
 */
#ifndef IOLINK_APP_PD_INPUT_CALLBACK
#define IOLINK_APP_PD_INPUT_CALLBACK 0
#endif

#if IOLINK_APP_PD_INPUT_CALLBACK
/**
 * @brief 回调配置的链接期标记。
 *
 * 仅由以 IOLINK_APP_PD_INPUT_CALLBACK=1 构建的库定义。应用
 * 必须使用相同的值（iolink_app_callbacks_t 的布局
 * 取决于它）。在应用翻译单元中引用该符号
 * （例如文件作用域的 `const void* const probe = &...;`）会将配置
 * 不匹配变为链接错误，而不是未定义行为。通过
 * add_subdirectory() 使用该库的应用会
 * 通过 PUBLIC 编译定义自动继承该设置。
 *
 * 该检查在构造上是单向的：以 =1 构建的应用
 * 链接轮询配置的库会失败，而以
 * =0 构建的应用无法引用（未声明的）标记来检测回调配置的
 * 库。让两侧都依赖 CMake 选项可避免这种情况。
 */
extern const uint8_t iolink_app_pd_input_callback_profile;
#endif

/**
 * @file application.h
 * @brief 用于过程数据的 IO-Link 应用层 API
 */

/**
 * @defgroup iolinki_application 应用层回调
 * @brief 应用生命周期与过程数据回调钩子。
 * @{
 */

/**
 * @brief 应用生命周期与过程数据回调。
 *
 * 任何字段都可以为 NULL，但当 IOLINK_APP_PD_INPUT_CALLBACK 为 1
 * 时，on_pd_input 是必需的（也是唯一的 PD 输入来源）；
 * 初始化会以 IOLINK_DEVICE_INIT_ERR_PD_INPUT 拒绝缺失的回调。
 * 每个回调都会收到以 iolink_device_config_t::user 传入的
 * 不透明 @c user 指针，因此应用无需全局变量即可
 * 访问自身状态。生命周期回调在 DLL 进入
 * 对应状态时触发一次。PD 回调在每次处理的
 * OPERATE 周期触发。@ref on_pd_output 报告从主站
 * 收到的输出过程数据（主站 -> 设备）。
 * 当 IOLINK_APP_PD_INPUT_CALLBACK 为 1 时，on_pd_input() 填充一个
 * 空闲的 PD-in 缓冲区（设备 -> 主站），其返回值成为 PD 有效标志。
 */
typedef struct
{
    void (*on_startup)(void* user);    /**< 已进入 STARTUP */
    void (*on_preoperate)(void* user); /**< 已进入 PREOPERATE */
    void (*on_operate)(void* user);    /**< 已进入 OPERATE */
#if IOLINK_APP_PD_INPUT_CALLBACK
    bool (*on_pd_input)(void* user, uint8_t* data, uint8_t len); /**< 填充输入 PD；
                                                                      false 表示无效 */
#endif
    void (*on_pd_output)(void* user, uint8_t* data, uint8_t len);  /**< 已收到输出 PD */
    void (*on_state_change)(void* user, iolink_dll_state_t state); /**< 基于电平通知时的
                                                                    当前 DLL 状态。
                                                                    在主上下文中运行，可能位于
                                                                    iolink_dll_process() 内部；
                                                                    在此处不得重入 iolink_*。 */
    void (*on_master_lost)(void* user);                            /**< 主站通信丢失 */
    void (*on_master_online)(void* user);                          /**< 主站通信恢复 */
} iolink_app_callbacks_t;

/** @} */ /* end of iolinki_application */

#endif  // IOLINK_APPLICATION_H
