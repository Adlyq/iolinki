#ifndef FIXTURE_DLL_MAIN_H
#define FIXTURE_DLL_MAIN_H

#include "dll_shared.h"

struct iolink_dll_main
{
    iolink_dll_shared_t shared;
    unsigned main_counter;
};

typedef struct iolink_dll_main iolink_dll_main_t;

#endif /* FIXTURE_DLL_MAIN_H */
