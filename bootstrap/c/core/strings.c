#include "teko_internal.h"

#include <string.h>

TekoString teko_string_from_cstr(const char *text) {
    TekoString result;
    result.data = text;
    result.length = text ? strlen(text) : 0;
    return result;
}

int teko_string_equal(const TekoString a, const TekoString b) {
    return a.length == b.length && (a.length == 0 || memcmp(a.data, b.data, a.length) == 0);
}

int teko_string_equal_cstr(const TekoString a, const char *b) {
    return teko_string_equal(a, teko_string_from_cstr(b));
}

TekoString teko_string_slice(const TekoString base, size_t offset, size_t length) {
    TekoString result;
    if (offset > base.length) {
        offset = base.length;
    }
    if (length > base.length - offset) {
        length = base.length - offset;
    }
    result.data = base.data + offset;
    result.length = length;
    return result;
}

TekoString teko_string_trim(TekoString value) {
    while (value.length && (*value.data == ' ' || *value.data == '\t' || *value.data == '\r' || *value.data == '\n')) {
        value.data++;
        value.length--;
    }
    while (value.length) {
        const char c = value.data[value.length - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        value.length--;
    }
    return value;
}

char *teko_copy_cstr(TekoContext *ctx, const TekoString value) {
    char *copy = (char *)teko_alloc(ctx, value.length + 1);
    if (!copy) {
        return 0;
    }
    if (value.length) {
        memcpy(copy, value.data, value.length);
    }
    copy[value.length] = 0;
    return copy;
}

static int ends_with(const TekoString value, const char *suffix) {
    const TekoString s = teko_string_from_cstr(suffix);
    if (value.length < s.length) {
        return 0;
    }
    return memcmp(value.data + value.length - s.length, s.data, s.length) == 0;
}

TekoSourceKind teko_source_kind_from_path(const TekoString path) {
    if (ends_with(path, ".struct.teko")) return TEKO_SOURCE_STRUCT;
    if (ends_with(path, ".enum.teko")) return TEKO_SOURCE_ENUM;
    if (ends_with(path, ".static.teko")) return TEKO_SOURCE_STATIC;
    if (ends_with(path, ".class.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".record.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".interface.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".iface.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".trait.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".meta.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".test.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".spec.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".bench.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".extension.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".ext.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".subtype.teko")) return TEKO_SOURCE_UNKNOWN;
    if (ends_with(path, ".teko")) return TEKO_SOURCE_TEKO;
    return TEKO_SOURCE_UNKNOWN;
}

TekoString teko_type_name_from_path(const TekoString path) {
    size_t start = path.length;
    size_t end = path.length;
    while (start > 0 && path.data[start - 1] != '/' && path.data[start - 1] != '\\') {
        start--;
    }
    if (ends_with(path, ".struct.teko")) end -= teko_string_from_cstr(".struct.teko").length;
    else if (ends_with(path, ".enum.teko")) end -= teko_string_from_cstr(".enum.teko").length;
    else if (ends_with(path, ".static.teko")) end -= teko_string_from_cstr(".static.teko").length;
    else if (ends_with(path, ".teko")) end -= teko_string_from_cstr(".teko").length;
    if (end < start) end = start;
    return teko_string_slice(path, start, end - start);
}

void sb_init(StringBuilder *sb, TekoContext *ctx) {
    sb->ctx = ctx;
    sb->data = 0;
    sb->length = 0;
    sb->capacity = 0;
}

void sb_append_n(StringBuilder *sb, const char *text, const size_t length) {
    if (!length) return;
    if (sb->length + length + 1 > sb->capacity) {
        size_t next = sb->capacity ? sb->capacity * 2 : 256;
        while (next < sb->length + length + 1) next *= 2;
        sb->data = (char *)teko_realloc(sb->ctx, sb->data, next);
        sb->capacity = next;
    }
    memcpy(sb->data + sb->length, text, length);
    sb->length += length;
    sb->data[sb->length] = 0;
}

void sb_append(StringBuilder *sb, const char *text) {
    sb_append_n(sb, text, strlen(text));
}

void sb_append_string(StringBuilder *sb, const TekoString text) {
    sb_append_n(sb, text.data, text.length);
}

void sb_append_char(StringBuilder *sb, const char c) {
    sb_append_n(sb, &c, 1);
}

TekoString sb_build(StringBuilder *sb) {
    TekoString result;
    if (!sb->data) {
        sb_append(sb, "");
    }
    result.data = sb->data;
    result.length = sb->length;
    sb->data = 0;
    sb->length = 0;
    sb->capacity = 0;
    return result;
}
