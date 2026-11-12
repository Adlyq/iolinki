/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_protocol_constants.c
 * @brief 将协议常量的数值固定到 IO-Link V1.1.5 的表格
 *        （A.1/A.3/A.10、B.8、B.9、C.1、D.1）。此处发生变更意味着
 *        线上编码已偏离规范。
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/protocol.h"
#include "iolinki/events.h"

/* Table A.3：CKT/CKS 中的 M-sequence 类型位。 */
static void test_msequence_type_bits(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_MSEQ_TYPE_MASK, 0xC0U);
    assert_int_equal(IOLINK_MSEQ_TYPE_0, 0x00U);
    assert_int_equal(IOLINK_MSEQ_TYPE_1, 0x40U);
    assert_int_equal(IOLINK_MSEQ_TYPE_2, 0x80U);
    assert_int_equal(IOLINK_MSEQ_CHECKSUM_MASK, 0x3FU);
    assert_int_equal(IOLINK_M_SEQ_HEADER_LEN, 2U);
    assert_int_equal(IOLINK_M_SEQ_MIN_LEN, 3U);
}

/* Table A.1：Master Command / 通道编码。 */
static void test_master_command_and_channels(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_MC_RW_MASK, 0x80U);
    assert_int_equal(IOLINK_MC_COMM_CHANNEL_MASK, 0x60U);
    assert_int_equal(IOLINK_MC_ADDR_MASK, 0x1FU);
    assert_int_equal(IOLINK_MC_CHANNEL_PROCESS, 0x00U);
    assert_int_equal(IOLINK_MC_CHANNEL_PAGE, 0x20U);
    assert_int_equal(IOLINK_MC_CHANNEL_DIAGNOSIS, 0x40U);
    assert_int_equal(IOLINK_MC_CHANNEL_ISDU, 0x60U);
    assert_int_equal(IOLINK_MC_TRANSITION_COMMAND, 0x0FU);

    /* Table 52：FlowCTRL 取值。 */
    assert_int_equal(IOLINK_FLOWCTRL_START, 0x10U);
    assert_int_equal(IOLINK_FLOWCTRL_IDLE, 0x11U);
    assert_int_equal(IOLINK_FLOWCTRL_ABORT, 0x1FU);
    assert_int_equal(IOLINK_FLOWCTRL_COUNT_MASK, 0x0FU);

    /* Table B.2：MasterCommand 编码。 */
    assert_int_equal(IOLINK_CMD_FALLBACK, 0x5AU);
    assert_int_equal(IOLINK_CMD_MASTER_IDENT, 0x95U);
    assert_int_equal(IOLINK_CMD_DEVICE_IDENT, 0x96U);
    assert_int_equal(IOLINK_CMD_DEVICE_STARTUP, 0x97U);
    assert_int_equal(IOLINK_CMD_PROCESS_DATA_OPERATE, 0x98U);
    assert_int_equal(IOLINK_CMD_DEVICE_OPERATE, 0x99U);
    assert_int_equal(IOLINK_CMD_DEVICE_PREOPERATE, 0x9AU);
}

/* Table A.12：ISDU 服务标识符。 */
static void test_isdu_service_ids(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_ISDU_SERVICE_READ, 0x0BU);
    assert_int_equal(IOLINK_ISDU_SERVICE_WRITE, 0x03U);
}

/* Table B.8：强制对象索引。 */
static void test_isdu_index_values(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_IDX_DIRECT_PARAMETERS_1, 0x0000U);
    assert_int_equal(IOLINK_IDX_DIRECT_PARAMETERS_2, 0x0001U);
    assert_int_equal(IOLINK_IDX_SYSTEM_COMMAND, 0x0002U);
    assert_int_equal(IOLINK_IDX_DATA_STORAGE, 0x0003U);
    /* Table B.8：0x0004-0x000B 为保留索引；VendorID/DeviceID 位于
     * Direct Parameter Page 1，且不对外暴露 ISDU 索引常量。 */
    assert_int_equal(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x000CU);
    assert_int_equal(IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x000DU);
    assert_int_equal(IOLINK_IDX_VENDOR_NAME, 0x0010U);
    assert_int_equal(IOLINK_IDX_VENDOR_TEXT, 0x0011U);
    assert_int_equal(IOLINK_IDX_PRODUCT_NAME, 0x0012U);
    assert_int_equal(IOLINK_IDX_PRODUCT_ID, 0x0013U);
    assert_int_equal(IOLINK_IDX_PRODUCT_TEXT, 0x0014U);
    assert_int_equal(IOLINK_IDX_SERIAL_NUMBER, 0x0015U);
    assert_int_equal(IOLINK_IDX_HARDWARE_REVISION, 0x0016U);
    assert_int_equal(IOLINK_IDX_FIRMWARE_REVISION, 0x0017U);
    assert_int_equal(IOLINK_IDX_APPLICATION_TAG, 0x0018U);
    assert_int_equal(IOLINK_IDX_FUNCTION_TAG, 0x0019U);
    assert_int_equal(IOLINK_IDX_LOCATION_TAG, 0x001AU);
    assert_int_equal(IOLINK_IDX_PRODUCT_URI, 0x001BU);
    assert_int_equal(IOLINK_IDX_ERROR_COUNT, 0x0020U);
    assert_int_equal(IOLINK_IDX_DEVICE_STATUS, 0x0024U);
    assert_int_equal(IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x0025U);
    assert_int_equal(IOLINK_IDX_PROCESS_DATA_INPUT, 0x0028U);
    assert_int_equal(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x0029U);
    assert_int_equal(IOLINK_IDX_OFFSET_TIME, 0x0030U);
}

/* Table B.9：SystemCommand 编码。 */
static void test_system_command_values(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_CMD_PARAM_UPLOAD_START, 0x01U);
    assert_int_equal(IOLINK_CMD_PARAM_UPLOAD_END, 0x02U);
    assert_int_equal(IOLINK_CMD_PARAM_DOWNLOAD_START, 0x03U);
    assert_int_equal(IOLINK_CMD_PARAM_DOWNLOAD_END, 0x04U);
    assert_int_equal(IOLINK_CMD_PARAM_DOWNLOAD_STORE, 0x05U);
    assert_int_equal(IOLINK_CMD_PARAM_BREAK, 0x06U);
    assert_int_equal(IOLINK_CMD_DEVICE_RESET, 0x80U);
    assert_int_equal(IOLINK_CMD_APPLICATION_RESET, 0x81U);
    assert_int_equal(IOLINK_CMD_RESTORE_FACTORY_SETTINGS, 0x82U);
    assert_int_equal(IOLINK_CMD_BACK_TO_BOX, 0x83U);
}

/* Table C.1：ISDU 错误附加码。 */
static void test_isdu_error_values(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_ISDU_ERROR_NONE, 0x00U);
    assert_int_equal(IOLINK_ISDU_ERROR_IDX_NOT_AVAIL, 0x11U);          /* IDX_NOTAVAIL */
    assert_int_equal(IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL, 0x12U);     /* SUBIDX_NOTAVAIL */
    assert_int_equal(IOLINK_ISDU_ERROR_SERVICE_TEMP_NOT_AVAIL, 0x20U); /* SERV_NOTAVAIL */
    assert_int_equal(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_LOCAL_CTRL,
                     0x21U); /* SERV_NOTAVAIL_LOCCTRL */
    assert_int_equal(IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL_DEV_CTRL,
                     0x22U);                                          /* SERV_NOTAVAIL_DEVCTRL */
    assert_int_equal(IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG, 0x30U);    /* PAR_VALOUTOFRNG */
    assert_int_equal(IOLINK_ISDU_ERROR_PAR_VAL_GT_LIM, 0x31U);        /* PAR_VALGTLIM */
    assert_int_equal(IOLINK_ISDU_ERROR_PAR_VAL_LT_LIM, 0x32U);        /* PAR_VALLTLIM */
    assert_int_equal(IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN, 0x33U);        /* VAL_LENOVRRUN */
    assert_int_equal(IOLINK_ISDU_ERROR_VAL_LEN_UNDRUN, 0x34U);        /* VAL_LENUNDRUN */
    assert_int_equal(IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL, 0x35U);    /* FUNC_NOTAVAIL */
    assert_int_equal(IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL, 0x36U); /* FUNC_UNAVAILTEMP */
    assert_int_equal(IOLINK_ISDU_ERROR_PARAM_SET_INVALID, 0x40U);     /* PAR_SETINVALID */
    assert_int_equal(IOLINK_ISDU_ERROR_PARAM_INCONSISTENT, 0x41U);    /* PAR_SETINCONSIST */
    assert_int_equal(IOLINK_ISDU_ERROR_APP_DEV_NOT_READY, 0x82U);     /* APP_DEVNOTRDY */

    /* Table C.1 只定义了一个“索引不可访问”错误码；协议栈使用的两个
     * 符号别名必须在数值上保持一致。 */
    assert_int_equal(IOLINK_ISDU_ERROR_NOT_ACCESSIBLE, 0x23U);
    assert_int_equal(IOLINK_ISDU_ERROR_WRITE_PROTECTED, 0x23U);
    assert_int_equal(IOLINK_ISDU_ERROR_NOT_ACCESSIBLE, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

/* 索引 0x000C：DeviceAccessLocks 位分配。 */
static void test_access_lock_bits(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_LOCK_PARAM, 0x01U);
    assert_int_equal(IOLINK_LOCK_DS, 0x02U);
    assert_int_equal(IOLINK_LOCK_LOCAL_PARAM, 0x04U);
    assert_int_equal(IOLINK_LOCK_LOCAL_UI, 0x08U);
}

/* Annex D：标准化的事件码范围和事件状态标志位。 */
static void test_event_code_ranges(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_EVENT_BIT_STATUS, 0x80U);

    assert_int_equal(IOLINK_EVENTCODE_NO_MALFUNCTION, 0x0000U);
    assert_int_equal(IOLINK_EVENTCODE_GENERAL_MALFUNCTION, 0x1000U);
    assert_int_equal(IOLINK_EVENTCODE_TEMPERATURE_OVERRUN, 0x4210U);
    assert_int_equal(IOLINK_EVENTCODE_HARDWARE_FAULT, 0x5000U);
    assert_int_equal(IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, 0x5111U);
    assert_int_equal(IOLINK_EVENTCODE_SHORT_CIRCUIT, 0x7710U);
    assert_int_equal(IOLINK_EVENTCODE_MAINTENANCE_REFILL, 0x8C41U);
    assert_int_equal(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, 0xFF91U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_msequence_type_bits),
        cmocka_unit_test(test_master_command_and_channels),
        cmocka_unit_test(test_isdu_service_ids),
        cmocka_unit_test(test_isdu_index_values),
        cmocka_unit_test(test_system_command_values),
        cmocka_unit_test(test_isdu_error_values),
        cmocka_unit_test(test_access_lock_bits),
        cmocka_unit_test(test_event_code_ranges),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
