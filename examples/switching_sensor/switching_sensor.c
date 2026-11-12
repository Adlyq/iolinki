/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "switching_sensor.h"
#include "iolinki/protocol.h"
#include <string.h>

void switching_sensor_init(switching_sensor_t* sensor)
{
    memset(sensor, 0, sizeof(*sensor));
    sensor->threshold = SWITCHING_SENSOR_DEFAULT_THRESHOLD;
    sensor->hysteresis = SWITCHING_SENSOR_DEFAULT_HYSTERESIS;
}

bool switching_sensor_sample(switching_sensor_t* sensor, uint16_t hundredths, bool valid)
{
    sensor->sample = hundredths;
    sensor->valid = valid;
    if (!valid) {
        sensor->active = false;
    }
    else if (hundredths >= sensor->threshold) {
        sensor->active = true;
    }
    else if (hundredths <= sensor->threshold - sensor->hysteresis) {
        sensor->active = false;
    }
    bool output = valid && (sensor->active != sensor->inverted);
    sensor->pd[0] = (uint8_t) (hundredths >> 8);
    sensor->pd[1] = (uint8_t) hundredths;
    sensor->pd[2] = valid ? (uint8_t) (2U | (output ? 1U : 0U)) : 0U;
    return output;
}

bool switching_sensor_value_get(const switching_sensor_t* sensor, uint16_t index, uint32_t* value)
{
    if ((sensor == NULL) || (value == NULL)) {
        return false;
    }
    switch (index) {
        case SWITCHING_SENSOR_IDX_THRESHOLD:
            *value = sensor->threshold;
            return true;
        case SWITCHING_SENSOR_IDX_HYSTERESIS:
            *value = sensor->hysteresis;
            return true;
        case SWITCHING_SENSOR_IDX_INVERSION:
            *value = sensor->inverted ? 1U : 0U;
            return true;
        default:
            return false;
    }
}

bool switching_sensor_value_set(switching_sensor_t* sensor, uint16_t index, uint32_t value)
{
    if (sensor == NULL) {
        return false;
    }
    switch (index) {
        case SWITCHING_SENSOR_IDX_THRESHOLD:
            sensor->threshold = (uint16_t) value;
            return true;
        case SWITCHING_SENSOR_IDX_HYSTERESIS:
            sensor->hysteresis = (uint16_t) value;
            return true;
        case SWITCHING_SENSOR_IDX_INVERSION:
            sensor->inverted = (value != 0U);
            return true;
        default:
            return false;
    }
}

/* -------------------------------------------------------------------------
 * 兼容包装：旧版 vendor_service 回调
 *
 * 新的集成绑定 `iolink_od_app_t`（见 switching_sensor_od.c），协议细节由
 * 栈内对象字典处理。本函数保留给既有调用点；它是一个独立翻译单元，
 * 因此这里保留最小限度的线上转换，不依赖栈内 OD 实现。
 * ------------------------------------------------------------------------- */

uint8_t switching_sensor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                                 const uint8_t* input, size_t length, uint8_t* output,
                                 size_t* output_length)
{
    switching_sensor_t* sensor = user;

    if (subindex != 0U) {
        return IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
    }
    if ((index < SWITCHING_SENSOR_IDX_THRESHOLD) || (index > SWITCHING_SENSOR_IDX_TEACH)) {
        return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    }
    const bool wide = (index <= SWITCHING_SENSOR_IDX_HYSTERESIS);
    const size_t width = wide ? 2U : 1U;

    if (write) {
        if (length != width) {
            return IOLINK_ISDU_ERROR_VAL_LEN_OVRRUN;
        }
        uint32_t value = 0U;
        for (size_t i = 0U; i < length; i++) {
            value = (value << 8U) | (uint32_t) input[i];
        }
        if ((index == SWITCHING_SENSOR_IDX_INVERSION) && (value > 1U)) {
            return IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG;
        }
        if ((index == SWITCHING_SENSOR_IDX_THRESHOLD) && ((uint16_t) value < sensor->hysteresis)) {
            return IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG;
        }
        if ((index == SWITCHING_SENSOR_IDX_HYSTERESIS) && ((uint16_t) value > sensor->threshold)) {
            return IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG;
        }
        if (index == SWITCHING_SENSOR_IDX_TEACH) {
            if ((value != 1U) || !sensor->valid || (sensor->sample < sensor->hysteresis)) {
                return IOLINK_ISDU_ERROR_PAR_VAL_OUT_OF_RNG;
            }
            sensor->threshold = sensor->sample;
        }
        else if (!switching_sensor_value_set(sensor, index, value)) {
            return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
        }
        if (output_length != NULL) {
            *output_length = 0U;
        }
        return 0U;
    }

    if (index == SWITCHING_SENSOR_IDX_TEACH) {
        return IOLINK_ISDU_ERROR_NOT_ACCESSIBLE;
    }
    if ((output == NULL) || (output_length == NULL) || (*output_length < width)) {
        return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    }
    uint32_t value = 0U;
    if (!switching_sensor_value_get(sensor, index, &value)) {
        return IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
    }
    output[0] = wide ? (uint8_t) (value >> 8U) : (uint8_t) value;
    if (wide) {
        output[1] = (uint8_t) value;
    }
    *output_length = width;
    return 0U;
}
