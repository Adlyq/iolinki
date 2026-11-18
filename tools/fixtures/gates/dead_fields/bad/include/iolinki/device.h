#ifndef FIXTURE_DEVICE_H
#define FIXTURE_DEVICE_H

#include "internal/dll_ctx.h"

typedef struct
{
    unsigned config_gain;
} iolink_device_config_t;

typedef struct
{
    iolink_dll_ctx_t dll;
    unsigned phy_error;
} iolink_device_ctx_t;

#endif /* FIXTURE_DEVICE_H */
