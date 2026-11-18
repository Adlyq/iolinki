#ifndef FIXTURE_DLL_CTX_H
#define FIXTURE_DLL_CTX_H

#include "dll_main.h"
#include "dll_isr.h"

struct iolink_dll_ctx
{
    iolink_dll_main_t main;
    iolink_dll_isr_t isr;
};

typedef struct iolink_dll_ctx iolink_dll_ctx_t;

#endif /* FIXTURE_DLL_CTX_H */
