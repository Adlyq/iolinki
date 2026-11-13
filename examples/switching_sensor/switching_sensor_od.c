/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * 对象字典：应用参数的唯一描述。
 *
 * 长度/字节序/标量范围/ErrorCode 由栈内 src/od.c 的 codec 表统一实现；
 * 这里只声明 4 个参数、两个交叉约束钩子与一个 teach 动作。新增参数 =
 * 增加一行表项（外加应用值访问分支，如果它引入新的存储位置）。
 *
 * 本文件单独编译，使 switching_sensor.c 在只链接自身时仍可独立构建
 * （tools/test_iodd_cli.py 的 C/ctypes 对照测试）。
 */
#include "switching_sensor.h"
#include "iolinki/protocol.h"

/** @brief SP1 必须 >= 迟滞；迟滞必须 <= SP1。 */
/* 回调类型固定为 void* user；同一张表的 ss_teach 需要可变指针，不能改成 const。 */
// cppcheck-suppress constParameterCallback
static iolink_result_t ss_validate(void* user, const iolink_od_entry_t* entry, const uint8_t* data,
                                   size_t len)
{
    const switching_sensor_t* sensor = user;
    uint32_t value = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &value);

    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    if ((entry->index == SWITCHING_SENSOR_IDX_THRESHOLD) &&
        ((uint16_t) value < sensor->hysteresis)) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    }
    if ((entry->index == SWITCHING_SENSOR_IDX_HYSTERESIS) &&
        ((uint16_t) value > sensor->threshold)) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    }
    return iolink_result_ok(0U);
}

/** @brief teach：把当前有效测量值写为新的 SP1（写语义 TRIGGER）。 */
static iolink_result_t ss_teach(void* user, const iolink_od_entry_t* entry, const uint8_t* data,
                                size_t len)
{
    switching_sensor_t* sensor = user;

    (void) entry;
    (void) data;
    (void) len;
    if (!sensor->valid || (sensor->sample < sensor->hysteresis)) {
        return iolink_result_error((int16_t) IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG);
    }
    sensor->threshold = sensor->sample;
    return iolink_result_ok(0U);
}

/* 只读后端，但回调类型固定为 void* user，不能改成 const。 */
// cppcheck-suppress constParameterCallback
static iolink_result_t ss_od_read(void* user, const iolink_od_entry_t* entry, uint8_t* out,
                                  size_t cap)
{
    uint32_t value = 0U;

    if (!switching_sensor_value_get(user, entry->index, &value)) {
        /* 0x0103 为只写对象。 */
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    return iolink_od_encode(entry, value, out, cap);
}

static iolink_result_t ss_od_write(void* user, const iolink_od_entry_t* entry, uint8_t subindex,
                                   const uint8_t* data, size_t len)
{
    uint32_t value = 0U;
    const iolink_result_t decoded = iolink_od_decode(entry, data, len, &value);

    (void) subindex;
    if (!iolink_result_succeeded(decoded)) {
        return decoded;
    }
    if (!switching_sensor_value_set(user, entry->index, value)) {
        return iolink_result_error(IOLINK_RESULT_NOT_HANDLED);
    }
    return iolink_result_ok(0U);
}

/* 4 个参数 = 4 行表项。 */
static const iolink_od_entry_t k_switching_sensor_od[] = {
    IOLINK_OD_SCALAR_EX(SWITCHING_SENSOR_IDX_THRESHOLD, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                        IOLINK_OD_HANDLER_BACKEND, 0U, 0xFFFFU, SWITCHING_SENSOR_DEFAULT_THRESHOLD,
                        "Switch-on threshold x100", ss_validate, NULL),
    IOLINK_OD_SCALAR_EX(SWITCHING_SENSOR_IDX_HYSTERESIS, IOLINK_OD_DT_U16, IOLINK_OD_ACCESS_RW,
                        IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                        IOLINK_OD_HANDLER_BACKEND, 0U, 0xFFFFU, SWITCHING_SENSOR_DEFAULT_HYSTERESIS,
                        "Hysteresis x100", ss_validate, NULL),
    IOLINK_OD_SCALAR(SWITCHING_SENSOR_IDX_INVERSION, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_RW,
                     IOLINK_OD_WRITE_VOLATILE, IOLINK_OD_STORAGE_VOLATILE,
                     IOLINK_OD_HANDLER_BACKEND, 0U, 1U, 0U, "Invert switching output"),
    IOLINK_OD_SCALAR_EX(SWITCHING_SENSOR_IDX_TEACH, IOLINK_OD_DT_U8, IOLINK_OD_ACCESS_WO,
                        IOLINK_OD_WRITE_TRIGGER, IOLINK_OD_STORAGE_VOLATILE,
                        IOLINK_OD_HANDLER_BACKEND, 1U, 1U, 0U, "Teach current value: write 1", NULL,
                        ss_teach),
};

#define SS_OD_COUNT (sizeof(k_switching_sensor_od) / sizeof(k_switching_sensor_od[0]))

const iolink_od_entry_t* switching_sensor_od_entries(size_t* count)
{
    if (count != NULL) {
        *count = SS_OD_COUNT;
    }
    return k_switching_sensor_od;
}

void switching_sensor_od_init(iolink_od_app_t* app, switching_sensor_t* sensor)
{
    if ((app == NULL) || (sensor == NULL)) {
        return;
    }
    app->entries = k_switching_sensor_od;
    app->count = SS_OD_COUNT;
    app->read = ss_od_read;
    app->write = ss_od_write;
    app->user = sensor;
}
