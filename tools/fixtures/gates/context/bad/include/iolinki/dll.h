#ifndef FIXTURE_CTX_DLL_H
#define FIXTURE_CTX_DLL_H

#include "internal/dll_main.h"

void iolink_dll_init(iolink_dll_main_t* ctx);
void iolink_dll_process(iolink_dll_main_t* ctx);
void iolink_dll_process_isr(iolink_dll_main_t* ctx);
void iolink_device_process(iolink_dll_main_t* ctx);
void iolink_device_process_isr(iolink_dll_main_t* ctx);

#endif /* FIXTURE_CTX_DLL_H */
