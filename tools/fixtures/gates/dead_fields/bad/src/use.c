#include "iolinki/device.h"

void fixture_live(iolink_dll_shared_t* shared)
{
    shared->live_field = 1U;
    (void) shared->live_field;
}

void fixture_sink(iolink_dll_shared_t* shared)
{
    shared->sink_field = 1U;
}

void fixture_probe(iolink_dll_shared_t* shared)
{
    shared->probe_field = 1U;
}

void fixture_config(iolink_device_config_t* config)
{
    (void) config->config_gain;
}

void fixture_ctx(iolink_device_ctx_t* ctx)
{
    ctx->phy_error = 0;
    (void) ctx->phy_error;
    (void) ctx->dll.main.main_counter;
    ctx->dll.isr.isr_counter = 1U;
    (void) ctx->dll.isr.isr_counter;
}
