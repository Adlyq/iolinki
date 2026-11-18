#include "iolinki/dll.h"

static void isr_helper(iolink_dll_main_t* ctx)
{
    ctx->shared.m_scalar = 1U;
}

static void main_helper(iolink_dll_main_t* ctx)
{
    ctx->shared.i_scalar = 1U;
}

static void dual_writes(iolink_dll_main_t* ctx)
{
    ctx->shared.m_scalar = 2U;
}

void iolink_dll_init(iolink_dll_main_t* ctx)
{
    ctx->shared.c_scalar = 0U;
}

void iolink_dll_process(iolink_dll_main_t* ctx)
{
    main_helper(ctx);
    dual_writes(ctx);
    ctx->shared.b_scalar = 1U;
}

void iolink_dll_process_isr(iolink_dll_main_t* ctx)
{
    isr_helper(ctx);
    dual_writes(ctx);
}

void iolink_device_process(iolink_dll_main_t* ctx)
{
    iolink_dll_process(ctx);
}

void iolink_device_process_isr(iolink_dll_main_t* ctx)
{
    iolink_dll_process_isr(ctx);
}
