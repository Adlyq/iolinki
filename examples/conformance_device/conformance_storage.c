/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "conformance_storage.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static int storage_read(void* user, uint32_t offset, uint8_t* buf, size_t len)
{
    conformance_file_storage_t* storage = (conformance_file_storage_t*) user;
    if ((storage == NULL) || (storage->file == NULL) || (buf == NULL)) {
        return -1;
    }
    if (fseek(storage->file, (long) offset, SEEK_SET) != 0) {
        return -1;
    }
    const size_t n = fread(buf, 1U, len, storage->file);
    if (n < len) {
        (void) memset(&buf[n], 0, len - n);
    }
    return 0;
}

static int storage_write(void* user, uint32_t offset, const uint8_t* buf, size_t len)
{
    conformance_file_storage_t* storage = (conformance_file_storage_t*) user;
    if ((storage == NULL) || (storage->file == NULL) || (buf == NULL)) {
        return -1;
    }
    if (fseek(storage->file, (long) offset, SEEK_SET) != 0) {
        return -1;
    }
    if (fwrite(buf, 1U, len, storage->file) != len) {
        return -1;
    }
    return (fflush(storage->file) == 0) ? 0 : -1;
}

int conformance_file_storage_init(conformance_file_storage_t* storage, const char* path)
{
    if ((storage == NULL) || (path == NULL) || (path[0] == '\0')) {
        return -1;
    }
    (void) memset(storage, 0, sizeof(*storage));
    if (strlen(path) >= sizeof(storage->path)) {
        return -1;
    }
    (void) strcpy(storage->path, path);
    storage->file = fopen(path, "r+b");
    if (storage->file == NULL) {
        storage->file = fopen(path, "w+b");
    }
    if (storage->file == NULL) {
        return -1;
    }
    storage->api.user = storage;
    storage->api.read = storage_read;
    storage->api.write = storage_write;
    return 0;
}

void conformance_file_storage_close(conformance_file_storage_t* storage)
{
    if ((storage != NULL) && (storage->file != NULL)) {
        (void) fclose(storage->file);
        storage->file = NULL;
    }
}
