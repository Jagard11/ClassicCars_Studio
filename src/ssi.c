#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "ssi.h"

#include "http.h"
#include "proxy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#define SSI_MAX_DEPTH 8
#define SSI_MAX_OUTPUT (16u * 1024u * 1024u)
#define SSI_MAX_FILE (8u * 1024u * 1024u)

#define SSI_MARK "<!--#include virtual=\""

typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
} Buf;

static int memcasecmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a;
    const unsigned char *y = b;
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char ca = x[i];
        unsigned char cb = y[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}

static int ends_with_ci(const char *s, size_t n, const char *ext) {
    size_t m = strlen(ext);
    if (n < m) return 0;
    return memcasecmp(s + (n - m), ext, m) == 0;
}

static const char *find_span(const char *hay, size_t hay_len, const char *needle) {
    size_t nlen = strlen(needle);
    size_t i;
    if (nlen == 0 || hay_len < nlen) return NULL;
    for (i = 0; i + nlen <= hay_len; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) return hay + i;
    }
    return NULL;
}

static int buf_reserve(Buf *b, size_t need) {
    char *p;
    size_t cap;
    if (need <= b->cap) return 0;
    cap = b->cap ? b->cap : 4096u;
    while (cap < need) {
        if (cap > (SIZE_MAX / 2)) return -1;
        cap *= 2;
    }
    p = realloc(b->data, cap);
    if (!p) return -1;
    b->data = p;
    b->cap = cap;
    return 0;
}

static int buf_append(Buf *b, const void *data, size_t n) {
    if (n > SSI_MAX_OUTPUT || b->len > SSI_MAX_OUTPUT - n) return -1;
    if (buf_reserve(b, b->len + n + 1) != 0) return -1;
    if (n && data) memcpy(b->data + b->len, data, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int has_ctl(const char *s) {
    const unsigned char *p;
    for (p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) return 1;
    }
    return 0;
}

static int blocked_prefix(const char *path, const char *pfx) {
    size_t n = strlen(pfx);
    if (strncmp(path, pfx, n) != 0) return 0;
    return path[n] == '\0' || path[n] == '/';
}

static int virtual_blocked(const char *path) {
    if (!path || path[0] != '/') return 1;
    if (has_ctl(path) || strstr(path, "..") || strchr(path, '\\')) return 1;
    if (strstr(path, ".htaccess")) return 1;
    if (blocked_prefix(path, "/data")) return 1;
    if (blocked_prefix(path, "/admin")) return 1;
    if (blocked_prefix(path, "/studio/state")) return 1;
    if (blocked_prefix(path, "/studio/src")) return 1;
    if (blocked_prefix(path, "/studio/build")) return 1;
    return 0;
}

static int comment_safe(const char *path) {
    if (!path || strstr(path, "--") || strchr(path, '<') || strchr(path, '>')) return 0;
    return !has_ctl(path);
}

static int append_note(Buf *b, const char *kind, const char *path) {
    char note[1200];
    size_t o = 0;
    const char *open = "<!-- SSI ";
    const char *close = " -->";
    size_t klen;
    size_t plen = 0;
    if (!kind) kind = "error";
    if (!comment_safe(path)) path = NULL;
    klen = strlen(kind);
    if (path) plen = strlen(path);
    if (strlen(open) + klen + plen + strlen(close) + 2 >= sizeof note) path = NULL;
    memcpy(note + o, open, strlen(open));
    o += strlen(open);
    memcpy(note + o, kind, klen);
    o += klen;
    if (path && path[0]) {
        note[o++] = ' ';
        memcpy(note + o, path, plen);
        o += plen;
    }
    memcpy(note + o, close, strlen(close));
    o += strlen(close);
    return buf_append(b, note, o);
}

static int path_join_url(char *dst, size_t dst_sz, const char *root, const char *url) {
    size_t nr;
    size_t nu;
    if (!dst || !root || !url || url[0] != '/') return -1;
    nr = strlen(root);
    while (nr > 1 && (root[nr - 1] == '/' || root[nr - 1] == '\\')) nr--;
    nu = strlen(url);
    if (nr + nu + 1 > dst_sz) return -1;
    memcpy(dst, root, nr);
    memcpy(dst + nr, url, nu + 1);
    return 0;
}

static int file_inside(const char *root, const char *fs) {
    char rr[PATH_MAX];
    char fr[PATH_MAX];
    size_t n;
#ifdef _WIN32
    size_t i;
    if (!_fullpath(rr, root, PATH_MAX) || !_fullpath(fr, fs, PATH_MAX)) return 0;
    for (i = 0; rr[i]; i++) {
        if (rr[i] == '\\') rr[i] = '/';
        if (rr[i] >= 'A' && rr[i] <= 'Z') rr[i] = (char)(rr[i] - 'A' + 'a');
    }
    for (i = 0; fr[i]; i++) {
        if (fr[i] == '\\') fr[i] = '/';
        if (fr[i] >= 'A' && fr[i] <= 'Z') fr[i] = (char)(fr[i] - 'A' + 'a');
    }
#else
    if (!realpath(root, rr) || !realpath(fs, fr)) return 0;
#endif
    n = strlen(rr);
    if (n > 1 && rr[n - 1] == '/') {
        rr[--n] = '\0';
    }
    if (strncmp(fr, rr, n) != 0) return 0;
    if (n == 1 && rr[0] == '/') return 1;
    return fr[n] == '\0' || fr[n] == '/';
}

static int ssi_splice_at(const char *input, size_t input_len,
                         const char *repo_root, const char *top, int depth,
                         char **out, size_t *out_len);

static int include_virtual(Buf *b, const char *virtual_url, const char *repo_root,
                           const char *top, int depth) {
    char raw[2048];
    char encoded[1024];
    char path[1024];
    const char *qmark;
    size_t plen;
    size_t n;
    char fs[PATH_MAX];
    struct stat st;

    if (!virtual_url) return append_note(b, "skipped", NULL);
    n = strlen(virtual_url);
    if (n >= sizeof raw) return append_note(b, "skipped", NULL);
    memcpy(raw, virtual_url, n + 1);
    while (raw[0] == ' ' || raw[0] == '\t') memmove(raw, raw + 1, strlen(raw));
    n = strlen(raw);
    while (n > 0 && (raw[n - 1] == ' ' || raw[n - 1] == '\t')) raw[--n] = '\0';
    if (strncmp(raw, "http://", 7) == 0 || strncmp(raw, "https://", 8) == 0) {
        return append_note(b, "blocked", NULL);
    }
    if (raw[0] != '/') {
        if (n + 2 >= sizeof raw) return append_note(b, "skipped", NULL);
        memmove(raw + 1, raw, n + 1);
        raw[0] = '/';
    }
    qmark = strchr(raw, '?');
    plen = qmark ? (size_t)(qmark - raw) : strlen(raw);
    if (plen == 0 || plen >= sizeof encoded) return append_note(b, "skipped", NULL);
    memcpy(encoded, raw, plen);
    encoded[plen] = '\0';
    if (http_url_decode(encoded, path, sizeof path) != 0) {
        return append_note(b, "blocked", NULL);
    }
    if (virtual_blocked(path)) return append_note(b, "blocked", path);

    if (ends_with_ci(path, strlen(path), ".php")) {
        char *body = NULL;
        size_t blen = 0;
        int status = 0;
        char ctype[128];
        if (!repo_root || path_join_url(fs, sizeof fs, repo_root, path) != 0) {
            return append_note(b, "missing", path);
        }
        if (stat(fs, &st) != 0 || !S_ISREG(st.st_mode) || !file_inside(repo_root, fs)) {
            return append_note(b, "missing", path);
        }
        ctype[0] = '\0';
        if (proxy_php_get(raw, top, &status, ctype, sizeof ctype, &body, &blen) != 0) {
            return append_note(b, "php-unavailable", path);
        }
        if (body && blen > 0) {
            int rc = buf_append(b, body, blen);
            free(body);
            return rc;
        }
        free(body);
        return 0;
    }

    if (ends_with_ci(path, strlen(path), ".html") ||
        ends_with_ci(path, strlen(path), ".htm") ||
        ends_with_ci(path, strlen(path), ".shtml")) {
        char *data = NULL;
        size_t len = 0;
        char *nested = NULL;
        size_t nlen = 0;
        int rc;
        if (!repo_root || path_join_url(fs, sizeof fs, repo_root, path) != 0) {
            return append_note(b, "missing", path);
        }
        if (stat(fs, &st) != 0 || !S_ISREG(st.st_mode)) {
            return append_note(b, "missing", path);
        }
        if (!file_inside(repo_root, fs)) return append_note(b, "blocked", path);
        if (http_read_file(fs, &data, &len, SSI_MAX_FILE) != 0) {
            return append_note(b, "unreadable", path);
        }
        rc = ssi_splice_at(data, len, repo_root, top, depth + 1, &nested, &nlen);
        free(data);
        if (rc != 0) {
            free(nested);
            return -1;
        }
        rc = buf_append(b, nested ? nested : "", nested ? nlen : 0);
        free(nested);
        return rc;
    }

    return append_note(b, "skipped", path);
}

static int ssi_splice_at(const char *input, size_t input_len,
                         const char *repo_root, const char *top, int depth,
                         char **out, size_t *out_len) {
    Buf b;
    size_t pos = 0;
    const char *mark = SSI_MARK;
    size_t mark_len = strlen(mark);

    memset(&b, 0, sizeof b);
    if (!out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    if (!input) {
        input = "";
        input_len = 0;
    }
    if (depth < 0) depth = 0;

    while (pos < input_len) {
        const char *found = find_span(input + pos, input_len - pos, mark);
        const char *q2;
        const char *end;
        const char *scan;
        char virtual_url[2048];
        size_t vlen;
        size_t rel;
        size_t end_index;

        if (!found) {
            if (buf_append(&b, input + pos, input_len - pos) != 0) {
                free(b.data);
                return -1;
            }
            break;
        }
        rel = (size_t)(found - input);
        if (rel > pos) {
            if (buf_append(&b, input + pos, rel - pos) != 0) {
                free(b.data);
                return -1;
            }
        }
        q2 = NULL;
        for (scan = found + mark_len; scan < input + input_len; scan++) {
            if (*scan == '"') {
                q2 = scan;
                break;
            }
        }
        end = NULL;
        if (q2) {
            scan = q2 + 1;
            while (scan < input + input_len &&
                   (*scan == ' ' || *scan == '\t' || *scan == '\r' || *scan == '\n')) {
                scan++;
            }
            if ((size_t)((input + input_len) - scan) >= 3 &&
                scan[0] == '-' && scan[1] == '-' && scan[2] == '>') {
                end = scan + 3;
            }
        }
        if (!q2 || !end) {
            if (buf_append(&b, found, 1) != 0) {
                free(b.data);
                return -1;
            }
            pos = rel + 1;
            continue;
        }
        vlen = (size_t)(q2 - (found + mark_len));
        end_index = (size_t)(end - input);
        if (depth >= SSI_MAX_DEPTH) {
            if (buf_append(&b, found, (size_t)(end - found)) != 0) {
                free(b.data);
                return -1;
            }
            pos = end_index;
            continue;
        }
        if (vlen >= sizeof virtual_url) {
            if (append_note(&b, "skipped", NULL) != 0) {
                free(b.data);
                return -1;
            }
            pos = end_index;
            continue;
        }
        memcpy(virtual_url, found + mark_len, vlen);
        virtual_url[vlen] = '\0';
        if (include_virtual(&b, virtual_url, repo_root, top, depth) != 0) {
            free(b.data);
            return -1;
        }
        pos = end_index;
    }

    if (!b.data) {
        b.data = calloc(1, 1);
        if (!b.data) return -1;
        b.len = 0;
    }
    *out = b.data;
    *out_len = b.len;
    return 0;
}

int ssi_splice(const char *input, size_t input_len,
               const char *repo_root,
               const char *top_level_page_uri,
               char **out, size_t *out_len) {
    return ssi_splice_at(input, input_len, repo_root, top_level_page_uri, 0, out, out_len);
}
