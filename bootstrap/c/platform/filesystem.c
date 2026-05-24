#include "filesystem.h"

#include <stdio.h>

int teko_platform_read_file(const char *path, const TekoAllocator allocator, TekoString *text) {
    text->data = 0;
    text->length = 0;

    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return 0;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }
    char* buffer = (char*)allocator.alloc(allocator.user, (size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return 0;
    }
    size_t read_count = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read_count != (size_t)size) {
        allocator.free(allocator.user, buffer);
        return 0;
    }
    buffer[size] = 0;
    text->data = buffer;
    text->length = (size_t)size;
    return 1;
}

int teko_platform_write_file(const char *path, const TekoString text) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    if (text.length && fwrite(text.data, 1, text.length, file) != text.length) {
        fclose(file);
        return 0;
    }
    fclose(file);
    return 1;
}

void teko_platform_free_string(const TekoAllocator allocator, TekoString *text) {
    if (text->data) {
        allocator.free(allocator.user, (void *)text->data);
    }
    text->data = 0;
    text->length = 0;
}
