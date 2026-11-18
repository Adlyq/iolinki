#ifndef FIXTURE_CTX_MAIN_H
#define FIXTURE_CTX_MAIN_H

#include "dll_shared.h"

struct iolink_dll_main
{
    iolink_dll_shared_t shared;
    unsigned startup_pending;
};

typedef struct iolink_dll_main iolink_dll_main_t;

#endif /* FIXTURE_CTX_MAIN_H */
