#ifndef TEKO_PLATFORM_FILESYSTEM_H
#define TEKO_PLATFORM_FILESYSTEM_H

#include "../core/teko_core.h"

int teko_platform_read_file(const char *path, TekoAllocator allocator, TekoString *text);
int teko_platform_write_file(const char *path, TekoString text);
void teko_platform_free_string(TekoAllocator allocator, TekoString *text);

#endif
