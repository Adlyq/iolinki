/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SWITCHING_SENSOR_H
#define SWITCHING_SENSOR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "iolinki/od.h"

/* 应用参数索引（设备专用 Extended Index 范围）。 */
#define SWITCHING_SENSOR_IDX_THRESHOLD 0x0100U
#define SWITCHING_SENSOR_IDX_HYSTERESIS 0x0101U
#define SWITCHING_SENSOR_IDX_INVERSION 0x0102U
#define SWITCHING_SENSOR_IDX_TEACH 0x0103U

#define SWITCHING_SENSOR_DEFAULT_THRESHOLD 5000U
#define SWITCHING_SENSOR_DEFAULT_HYSTERESIS 200U

typedef struct
{
    uint16_t threshold;
    uint16_t hysteresis;
    uint16_t sample;
    bool inverted;
    bool active;
    bool valid;
    uint8_t pd[3];
} switching_sensor_t;

void switching_sensor_init(switching_sensor_t* sensor);
bool switching_sensor_sample(switching_sensor_t* sensor, uint16_t hundredths, bool valid);

/* 应用值访问：供 OD 后端使用，不含任何协议语义。 */
bool switching_sensor_value_get(const switching_sensor_t* sensor, uint16_t index, uint32_t* value);
bool switching_sensor_value_set(switching_sensor_t* sensor, uint16_t index, uint32_t value);

/* 对象字典入口：应用参数 0x0100-0x0103 的唯一描述。 */
const iolink_od_entry_t* switching_sensor_od_entries(size_t* count);
void switching_sensor_od_init(iolink_od_app_t* app);

#endif
