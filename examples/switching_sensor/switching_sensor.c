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

