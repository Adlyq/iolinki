#ifndef FIXTURE_DLL_SHARED_H
#define FIXTURE_DLL_SHARED_H

typedef struct iolink_dll_shared
{
    unsigned live_field;
    unsigned ghost_field;
    unsigned sink_field;
    unsigned probe_field;
} iolink_dll_shared_t;

#endif /* FIXTURE_DLL_SHARED_H */
