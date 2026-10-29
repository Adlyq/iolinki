/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CONFORMANCE_STORAGE_H
#define CONFORMANCE_STORAGE_H

#include <stdio.h>

#include "iolinki/storage.h"

/**
 * @file conformance_storage.h
 * @brief File-backed NVM used by the Linux conformance reference device.
 */

typedef struct
{
    char path[256];
    FILE* file;
    iolink_storage_api_t api;
} conformance_file_storage_t;

int conformance_file_storage_init(conformance_file_storage_t* storage, const char* path);
void conformance_file_storage_close(conformance_file_storage_t* storage);

#endif /* CONFORMANCE_STORAGE_H */
