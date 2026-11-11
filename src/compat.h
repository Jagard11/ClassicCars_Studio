#ifndef STUDIO_COMPAT_H
#define STUDIO_COMPAT_H

/* Small Windows shims so the studio sources build with MinGW and MSVC.
 * POSIX builds include this header as a no-op.
 */

#if defined(_WIN32)

#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef S_ISDIR
#ifdef _S_IFDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#else
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#endif

#ifndef S_ISREG
#ifdef _S_IFREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#else
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#endif

#if defined(_MSC_VER)
#define strdup _strdup
#define unlink _unlink
#define access _access
#ifndef R_OK
#define R_OK 4
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct dirent {
    char d_name[260];
};

typedef struct StudioDir {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    int first;
    struct dirent ent;
} DIR;

static inline DIR *opendir(const char *name) {
    char pattern[1024];
    size_t n;
    DIR *dir;
    if (!name) return NULL;
    n = strlen(name);
    if (n + 3 >= sizeof pattern) return NULL;
    memcpy(pattern, name, n);
    if (n > 0 && pattern[n - 1] != '/' && pattern[n - 1] != '\\') pattern[n++] = '\\';
    pattern[n++] = '*';
    pattern[n] = '\0';
    dir = (DIR *)calloc(1, sizeof *dir);
    if (!dir) return NULL;
    dir->handle = FindFirstFileA(pattern, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir);
        return NULL;
    }
    dir->first = 1;
    return dir;
}

static inline struct dirent *readdir(DIR *dir) {
    size_t n;
    if (!dir) return NULL;
    if (dir->first) {
        dir->first = 0;
    } else if (!FindNextFileA(dir->handle, &dir->data)) {
        return NULL;
    }
    n = strlen(dir->data.cFileName);
    if (n >= sizeof dir->ent.d_name) n = sizeof dir->ent.d_name - 1;
    memcpy(dir->ent.d_name, dir->data.cFileName, n);
    dir->ent.d_name[n] = '\0';
    return &dir->ent;
}

static inline int closedir(DIR *dir) {
    if (!dir) return -1;
    if (dir->handle != INVALID_HANDLE_VALUE) FindClose(dir->handle);
    free(dir);
    return 0;
}
#endif

static inline char *studio_strndup(const char *s, size_t n) {
    size_t i = 0;
    char *out;
    if (!s) return NULL;
    while (i < n && s[i]) i++;
    out = (char *)malloc(i + 1);
    if (!out) return NULL;
    if (i) memcpy(out, s, i);
    out[i] = '\0';
    return out;
}

static inline struct tm *studio_gmtime_r(const time_t *t, struct tm *out) {
    if (!t || !out) return NULL;
    return gmtime_s(out, t) == 0 ? out : NULL;
}

#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define strndup studio_strndup
#define gmtime_r studio_gmtime_r

#endif

#endif
