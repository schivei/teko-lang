// src/build/discover.c   (namespace 'teko::build')
//
// File-set enumeration + namespace mapping (crumb A2) — the SECOND step of project
// compilation. Walks the source tree, collects every `.tks`/`.tkt`, maps each to its
// namespace (LEGISLATION §181–188; namespaces = directories, source root invisible).
#include "discover.h"

#include <string.h>    // strlen, memcpy, strcmp
#include <stdlib.h>    // malloc, free, abort

// M.1 host-IO boundary: directory enumeration.
// On Windows, provide a minimal POSIX-compatible DIR/dirent shim using the
// Win32 FindFirstFileA/FindNextFileA/FindClose API so the walk() logic below
// is identical on all platforms.
#ifdef _WIN32
#include <windows.h>

typedef struct tk_dirent { char d_name[MAX_PATH]; } tk_dirent;
typedef struct {
    HANDLE          hFind;
    WIN32_FIND_DATAA wfd;
    bool            first;
    tk_dirent       dent;
} TK_DIR;

static TK_DIR *tk_opendir(const char *path) {
    // Append "\*" to search for all entries under path.
    size_t plen = strlen(path);
    if (plen + 3 > MAX_PATH) return NULL;   // path too long for Windows API
    char pattern[MAX_PATH];
    memcpy(pattern, path, plen);
    pattern[plen] = '\\'; pattern[plen + 1] = '*'; pattern[plen + 2] = '\0';
    TK_DIR *d = (TK_DIR *)malloc(sizeof *d);
    if (!d) return NULL;
    d->hFind = FindFirstFileA(pattern, &d->wfd);
    if (d->hFind == INVALID_HANDLE_VALUE) { free(d); return NULL; }
    d->first = true;
    return d;
}
static tk_dirent *tk_readdir(TK_DIR *d) {
    if (d->first) {
        d->first = false;
    } else if (!FindNextFileA(d->hFind, &d->wfd)) {
        return NULL;
    }
    size_t n = strlen(d->wfd.cFileName);
    if (n >= MAX_PATH) n = MAX_PATH - 1;
    memcpy(d->dent.d_name, d->wfd.cFileName, n);
    d->dent.d_name[n] = '\0';
    return &d->dent;
}
static void tk_closedir(TK_DIR *d) {
    if (d->hFind != INVALID_HANDLE_VALUE) FindClose(d->hFind);
    free(d);
}

#define DIR      TK_DIR
#define dirent   tk_dirent
#define opendir  tk_opendir
#define readdir  tk_readdir
#define closedir tk_closedir

#else
#include <dirent.h>    // opendir/readdir/closedir (POSIX)
#endif

// --- heap-owned strings (process-lifetime, arena-style — M.5) -----------------
//
// tk_str is a VIEW; discovery builds fresh paths/namespaces, so the bytes must be
// owned. We heap-allocate a NUL-terminated buffer (NUL kept for host-IO reuse) and
// return a str viewing its [0,len) bytes. Never freed — process lifetime (M.5);
// allocation failure aborts, like the rest of the tree.
static tk_str owned_str(const char *bytes, size_t len) {
    char *buf = tk_alloc(len + 1);
    if (buf == NULL) abort();
    memcpy(buf, bytes, len);
    buf[len] = '\0';
    return (tk_str){ .ptr = (const tk_byte *)buf, .len = len };
}

// is this name one of "." / ".." (skip — not real entries)?
static bool is_dot(const char *n) {
    return n[0] == '.' && (n[1] == '\0' || (n[1] == '.' && n[2] == '\0'));
}

// does `name` end with `.tks` or `.tkt`? (the Teko source/test extensions.)
static bool is_teko_source(const char *name) {
    size_t n = strlen(name);
    if (n < 4) return false;
    const char *ext = name + n - 4;
    return strcmp(ext, ".tks") == 0 || strcmp(ext, ".tkt") == 0;
}

static tk_source_files_result fail(const char *msg) {
    return (tk_source_files_result){ .ok = false, .as.error = tk_error_make(msg) };
}

// --- the recursive walk -------------------------------------------------------
//
// `dir`    — the directory to read, on disk (e.g. "src/lexer"); NUL-terminated.
// `ns`     — the namespace for files DIRECTLY in `dir` (e.g. "teko::lexer"); the
//            invariant carried down so the source root is already stripped.
// Files map to `ns`; each sub-directory `d` recurses with dir+"/"+d and ns+"::"+d.
// Returns the accumulated list (push CONSUMES/RETURNS — M.5).
//
// M.1 — CONTAINED HOST IO. opendir/readdir/closedir is the unsafe host edge, the
// same spirit as driver.c::tk_read_file: the filesystem is touched ONLY here, kept
// isolated; everything downstream works over the owned strs.
static bool walk(const char *dir, const char *ns,
                 tk_source_files *out) {
    DIR *d = opendir(dir);
    if (d == NULL) return false;

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        const char *name = e->d_name;
        if (is_dot(name)) continue;

        // child path on disk: dir + "/" + name.
        size_t dlen = strlen(dir), nlen = strlen(name);
        char *child = tk_alloc(dlen + 1 + nlen + 1);
        if (child == NULL) { closedir(d); abort(); }
        memcpy(child, dir, dlen);
        child[dlen] = '/';
        memcpy(child + dlen + 1, name, nlen);
        child[dlen + 1 + nlen] = '\0';

        // classify: directory → recurse with the extended namespace; file → collect.
        // We avoid d_type (not portable across all hosts) by re-opening as a dir:
        // opendir succeeds on a directory, fails on a regular file (M.1, one edge).
        DIR *sub = opendir(child);
        if (sub != NULL) {
            closedir(sub);
            // extend the namespace: ns + "::" + name.
            size_t nslen = strlen(ns);
            char *cns = tk_alloc(nslen + 2 + nlen + 1);
            if (cns == NULL) { tk_free0(child); closedir(d); abort(); }
            memcpy(cns, ns, nslen);
            cns[nslen] = ':'; cns[nslen + 1] = ':';
            memcpy(cns + nslen + 2, name, nlen);
            cns[nslen + 2 + nlen] = '\0';

            bool ok = walk(child, cns, out);
            tk_free0(cns);
            tk_free0(child);
            if (!ok) { closedir(d); return false; }
        } else {
            // a regular file — keep only .tks/.tkt, mapped to THIS dir's namespace.
            if (is_teko_source(name)) {
                tk_source_file sf = {
                    .path      = owned_str(child, dlen + 1 + nlen),
                    .namespace = owned_str(ns, strlen(ns)),
                };
                *out = tk_source_files_push(*out, sf);
            }
            tk_free0(child);
        }
    }

    closedir(d);
    return true;
}

// tk_discover — the entry. Seed the walk at `source` with the bare root `name` (so
// files directly under `source` get the bare root namespace, and the source root is
// invisible — never a namespace segment). name/source are str VIEWS, not necessarily
// NUL-terminated, so copy them to C strings for the host-IO boundary first.
tk_source_files_result tk_discover(tk_str name, tk_str source) {
    char *root_dir = tk_alloc(source.len + 1);
    if (root_dir == NULL) abort();
    memcpy(root_dir, source.ptr, source.len);
    root_dir[source.len] = '\0';

    char *root_ns = tk_alloc(name.len + 1);
    if (root_ns == NULL) { tk_free0(root_dir); abort(); }
    memcpy(root_ns, name.ptr, name.len);
    root_ns[name.len] = '\0';

    tk_source_files files = tk_source_files_empty();
    bool ok = walk(root_dir, root_ns, &files);

    tk_free0(root_dir);
    tk_free0(root_ns);

    if (!ok) {
        tk_source_files_free(files);
        return fail("discover: cannot open source directory");
    }
    return (tk_source_files_result){ .ok = true, .as.value = files };
}
