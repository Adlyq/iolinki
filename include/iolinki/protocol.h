/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PROTOCOL_H
#define IOLINK_PROTOCOL_H

/**
 * @file protocol.h
 * @brief IO-Link 协议常量与定义（Spec V1.1.5）
 */

/**
 * @defgroup iolinki_protocol IO-Link 协议常量
 * @brief M 序列、Master Command、ISDU、索引、命令与状态常量。
 *
 * 这些常量划分到下面的子模块中，每个子模块对应 IO-Link 接口
 * 规范中的一张表或一种字段布局。
 */

/**
 * @defgroup iolinki_protocol_mseq M 序列类型长度
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_M_SEQ_TYPE0_LEN 2U  /**< Type 0 M 序列长度（字节）。 */
#define IOLINK_M_SEQ_HEADER_LEN 2U /**< M 序列头长度：MC + CKT。 */
#define IOLINK_M_SEQ_MIN_LEN 3U    /**< 最小 M 序列长度：MC + CKT + CK（Type 1/2）。 */
/** @} */

/**
 * @defgroup iolinki_protocol_mc Master Command（MC）定义
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_MC_RW_MASK 0x80U           /**< MC 字节中的读/写位掩码。 */
#define IOLINK_MC_COMM_CHANNEL_MASK 0x60U /**< MC 字节中的通信通道字段掩码。 */
#define IOLINK_MC_ADDR_MASK 0x1FU         /**< MC 字节中的地址字段掩码。 */

/** @brief 通信通道取值（A.1.2，Table A.1）。 */
#define IOLINK_MC_CHANNEL_PROCESS 0x00U   /**< 过程数据通道。 */
#define IOLINK_MC_CHANNEL_PAGE 0x20U      /**< Direct Parameter page 通道。 */
#define IOLINK_MC_CHANNEL_DIAGNOSIS 0x40U /**< 诊断（事件存储器）通道。 */
#define IOLINK_MC_CHANNEL_ISDU 0x60U      /**< ISDU 通道（FlowCTRL 位于地址位）。 */

#define IOLINK_MC_TRANSITION_COMMAND 0x0FU /**< 用于过渡命令的地址。 */

/** @brief CKT/CKS 中的 M 序列类型位（A.1.3，Table A.3）。 */
#define IOLINK_MSEQ_TYPE_MASK 0xC0U     /**< CKT/CKS 八位组中的类型字段掩码。 */
#define IOLINK_MSEQ_TYPE_0 0x00U        /**< Type 0。 */
#define IOLINK_MSEQ_TYPE_1 0x40U        /**< Type 1。 */
#define IOLINK_MSEQ_TYPE_2 0x80U        /**< Type 2。 */
#define IOLINK_MSEQ_TYPE_RESERVED 0xC0U /**< 保留类型。 */
#define IOLINK_MSEQ_CHECKSUM_MASK 0x3FU /**< CKT/CKS 八位组中的校验和字段掩码。 */

/** @brief ISDU 通道地址位携带的 ISDU FlowCTRL 取值（7.3.6.2，Table 52）。 */
#define IOLINK_FLOWCTRL_START 0x10U      /**< ISDU I-Service 的起始。 */
#define IOLINK_FLOWCTRL_IDLE 0x11U       /**< 无 ISDU 传输请求。 */
#define IOLINK_FLOWCTRL_IDLE2 0x12U      /**< 保留 / 无请求。 */
#define IOLINK_FLOWCTRL_ABORT 0x1FU      /**< 中止整个服务。 */
#define IOLINK_FLOWCTRL_COUNT_MASK 0x0FU /**< COUNT M 序列计数器掩码。 */

/** @brief 写入 Direct Parameter page 地址 0x00 的 MasterCommand 值（Table B.2）。 */
#define IOLINK_CMD_FALLBACK 0x5AU             /**< 过渡到 SIO（T_FBD，Table 43）。 */
#define IOLINK_CMD_MASTER_IDENT 0x95U         /**< MasterIdent：主站版本 > 1.0。 */
#define IOLINK_CMD_DEVICE_IDENT 0x96U         /**< DeviceIdent：检查 DPP 中变更的条目。 */
#define IOLINK_CMD_DEVICE_STARTUP 0x97U       /**< DeviceStartup：-> STARTUP。 */
#define IOLINK_CMD_PROCESS_DATA_OPERATE 0x98U /**< ProcessDataOutputOperate：PD 有效。 */
#define IOLINK_CMD_DEVICE_OPERATE 0x99U       /**< DeviceOperate：PD 无效，-> OPERATE。 */
#define IOLINK_CMD_DEVICE_PREOPERATE 0x9AU    /**< DevicePreoperate：-> PREOPERATE。 */
/** @} */

/**
 * @defgroup iolinki_protocol_isdu_service ISDU 服务 ID
 * @ingroup iolinki_protocol
 * @brief I-Service 半字节，IO-Link 规范 Table A.12（16 位索引 + 子索引形式）。
 * @{
 */
#define IOLINK_ISDU_SERVICE_READ 0x0BU  /**< ISDU 读服务。 */
#define IOLINK_ISDU_SERVICE_WRITE 0x03U /**< ISDU 写服务。 */
/** @} */

/**
 * @defgroup iolinki_protocol_indices 强制 ISDU 索引
 * @ingroup iolinki_protocol
 * @{
 */
/* VendorID 与 DeviceID 是 Direct Parameter page 1 的字段（八位组 0x07-0x0B），
 * 而非 ISDU 索引，因此没有对应它们的 IOLINK_IDX_* 常量。 */
#define IOLINK_IDX_DIRECT_PARAMETERS_1 0x0000U    /**< Direct Parameter page 1。 */
#define IOLINK_IDX_DIRECT_PARAMETERS_2 0x0001U    /**< Direct Parameter page 2。 */
#define IOLINK_IDX_SYSTEM_COMMAND 0x0002U         /**< System Command 索引。 */
#define IOLINK_IDX_DATA_STORAGE 0x0003U           /**< Data Storage 对象（参数映像）。 */
#define IOLINK_IDX_DEVICE_ACCESS_LOCKS 0x000CU    /**< Device Access Locks 索引。 */
#define IOLINK_IDX_PROFILE_CHARACTERISTIC 0x000DU /**< Profile 特性索引。 */
#define IOLINK_IDX_VENDOR_NAME 0x0010U            /**< 厂商名称索引。 */
#define IOLINK_IDX_VENDOR_TEXT 0x0011U            /**< 厂商文本索引。 */
#define IOLINK_IDX_PRODUCT_NAME 0x0012U           /**< 产品名称索引。 */
#define IOLINK_IDX_PRODUCT_ID 0x0013U             /**< 产品 ID 索引。 */
#define IOLINK_IDX_PRODUCT_TEXT 0x0014U           /**< 产品文本索引。 */
#define IOLINK_IDX_SERIAL_NUMBER 0x0015U          /**< 序列号索引。 */
#define IOLINK_IDX_HARDWARE_REVISION 0x0016U      /**< 硬件版本索引。 */
#define IOLINK_IDX_FIRMWARE_REVISION 0x0017U      /**< 固件版本索引。 */
#define IOLINK_IDX_APPLICATION_TAG 0x0018U        /**< 应用标签索引。 */
#define IOLINK_IDX_FUNCTION_TAG 0x0019U           /**< 功能标签索引。 */
#define IOLINK_IDX_LOCATION_TAG 0x001AU           /**< 位置标签索引。 */
#define IOLINK_IDX_PRODUCT_URI 0x001BU            /**< ProductURI（V1.1.4+；取决于 Profile）。 */
#define IOLINK_IDX_ERROR_COUNT 0x0020U            /**< ErrorCount 索引（2 个八位组）。 */
#define IOLINK_IDX_DEVICE_STATUS 0x0024U          /**< DeviceStatus 索引（1 个八位组）。 */
#define IOLINK_IDX_DETAILED_DEVICE_STATUS 0x0025U /**< DetailedDeviceStatus 索引（Table B.8）。 */
#define IOLINK_IDX_PROCESS_DATA_INPUT 0x0028U     /**< ProcessDataInput（最后一次有效 PD 输入）。 */
#define IOLINK_IDX_PROCESS_DATA_OUTPUT 0x0029U /**< ProcessDataOutput（最后一次有效 PD 输出）。 */
#define IOLINK_IDX_OFFSET_TIME 0x0030U         /**< Offset-Time（RecordT，可选）。 */
#define IOLINK_IDX_ERROR_STATS 0x0040U         /**< 厂商专用错误统计（0x0040+）。 */
/** @} */

/**
 * @defgroup iolinki_protocol_sys_cmd SystemCommand 取值（索引 0x0002，Table B.9）
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_CMD_PARAM_UPLOAD_START 0x01U   /**< ParamUploadStart（Table B.9）。 */
#define IOLINK_CMD_PARAM_UPLOAD_END 0x02U     /**< ParamUploadEnd（Table B.9）。 */
#define IOLINK_CMD_PARAM_DOWNLOAD_START 0x03U /**< ParamDownloadStart（Table B.9）。 */
#define IOLINK_CMD_PARAM_DOWNLOAD_END 0x04U   /**< ParamDownloadEnd（Table B.9）。 */
#define IOLINK_CMD_PARAM_DOWNLOAD_STORE 0x05U /**< ParamDownloadStore（Table B.9）。 */
#define IOLINK_CMD_PARAM_BREAK 0x06U          /**< ParamBreak（Table B.9）。 */

#define IOLINK_CMD_DEVICE_RESET 0x80U             /**< 设备复位（Table B.9）。 */
#define IOLINK_CMD_APPLICATION_RESET 0x81U        /**< 应用复位（Table B.9）。 */
#define IOLINK_CMD_RESTORE_FACTORY_SETTINGS 0x82U /**< 恢复出厂设置（Table B.9）。 */
#define IOLINK_CMD_BACK_TO_BOX 0x83U              /**< Back-to-box（Table B.9）。 */
/** @} */

/**
 * @defgroup iolinki_protocol_locks Device Access Locks（索引 0x000C）
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_LOCK_PARAM 0x01U       /**< Bit 0：参数（写）访问锁。 */
#define IOLINK_LOCK_DS 0x02U          /**< Bit 1：Data Storage 访问锁。 */
#define IOLINK_LOCK_LOCAL_PARAM 0x04U /**< Bit 2：本地参数化锁。 */
#define IOLINK_LOCK_LOCAL_UI 0x08U    /**< Bit 3：本地用户接口锁。 */
#define IOLINK_ACCESS_LOCKS_IMPLEMENTED \
    (IOLINK_LOCK_PARAM | IOLINK_LOCK_DS | IOLINK_LOCK_LOCAL_PARAM | IOLINK_LOCK_LOCAL_UI)
/** @} */

/**
 * @defgroup iolinki_protocol_isdu_err ISDU 错误码（0x80xx）
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_ISDU_ERROR_NONE 0x00U                   /**< 无错误。 */
#define IOLINK_ISDU_ERROR_IDX_NOT_AVAIL 0x11U          /**< 索引不可用（Table C.1，C.2.3）。 */
#define IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL 0x12U     /**< 请求的子索引不可用。 */
#define IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL 0x20U /**< 服务暂时不可用（Table C.1）。 */
#define IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_LOCAL_CTRL \
    0x21U /**< 服务不可用 - 本地控制（Table C.1）。 */
#define IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL \
    0x22U                                       /**< 服务不可用 - 设备控制（Table C.1）。 */
#define IOLINK_ISDU_ERROR_NOT_ACCESSIBLE 0x23U  /**< 对象访问被拒绝（Table C.1）。 */
#define IOLINK_ISDU_ERROR_WRITE_PROTECTED 0x23U /**< 访问被拒绝，Table C.1。 */
#define IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG                                               \
    0x30U                                             /**< 参数值超出范围（Table C.1）。 \
                                                       */
#define IOLINK_ISDU_ERROR_PAR_VAL_GT_LIM 0x31U        /**< 参数值高于上限（Table C.1）。 */
#define IOLINK_ISDU_ERROR_PAR_VAL_LT_LIM 0x32U        /**< 参数值低于下限（Table C.1）。 */
#define IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN 0x33U        /**< 值长度超长（Table C.1）。 */
#define IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN 0x34U        /**< 值长度不足（Table C.1）。 */
#define IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL 0x35U    /**< 命令功能不可用（Table C.1）。 */
#define IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL 0x36U /**< 功能暂时不可用（Table C.1）。 */
#define IOLINK_ISDU_ERROR_PARAM_SET_INVALID 0x40U     /**< PAR_SETINVALID（Table C.1）。 */
#define IOLINK_ISDU_ERROR_PARAM_INCONSISTENT 0x41U    /**< PAR_SETINCONSIST（Table C.1）。 */
#define IOLINK_ISDU_ERROR_SEGMENTATION 0x81U          /**< 分段错误（内部）。 */
#define IOLINK_ISDU_ERROR_APP_DEV_NOT_READY     \
    0x82U /**< 应用/设备未就绪（Table C.1）。 \
           */
/** @} */

/**
 * @defgroup iolinki_protocol_event 事件常量
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_EVENT_BIT_STATUS 0x80U /**< Type 1/2 中状态字节的 MSB（事件标志）。 */
/** @} */

/**
 * @defgroup iolinki_protocol_od On-Request Data（OD）常量
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_OD_LEN_8BIT 1U  /**< 1 字节 OD（Type 1_x）。 */
#define IOLINK_OD_LEN_16BIT 2U /**< 2 字节 OD（Type 2_x）。 */
#define IOLINK_OD_LEN_32BIT 4U /**< 4 字节 OD（Type 2_V 扩展）。 */
/** @} */

/**
 * @defgroup iolinki_protocol_device_status 设备状态标志
 * @ingroup iolinki_protocol
 * @{
 */
#define IOLINK_DEVICE_STATUS_OK 0x00U               /**< 设备运行正常（Table B.13）。 */
#define IOLINK_DEVICE_STATUS_MAINTENANCE 0x01U      /**< 需要维护（Table B.13）。 */
#define IOLINK_DEVICE_STATUS_OUT_OF_SPEC 0x02U      /**< 超出规格（Table B.13）。 */
#define IOLINK_DEVICE_STATUS_FUNCTIONAL_CHECK 0x03U /**< 功能检查（Table B.13）。 */
#define IOLINK_DEVICE_STATUS_FAILURE 0x04U          /**< 故障（Table B.13）。 */
/** @} */

#endif /* IOLINK_PROTOCOL_H */
