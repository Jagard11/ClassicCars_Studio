#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include "config.h"
#include "gallery.h"
#include "settings.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#ifndef _MSC_VER
#include <dirent.h>
#endif
#else
#include <dirent.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "compat.h"

#define CAPTION_EQUALS_ERROR "Captions cannot contain '=' because image_data.txt uses it as the delimiter."
#define CAPTION_CONTROL_ERROR "Captions cannot contain line breaks or control characters."
#define BRAND_NAME "Dick Shappy Vintage Auto Sales"

typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
} Buf;

typedef struct StrList {
    char **items;
    size_t count;
    size_t cap;
} StrList;

static void set_err(char *err, size_t err_sz, const char *message) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", message ? message : "");
    }
}

static void buf_init(Buf *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static void buf_free(Buf *buf) {
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

static int buf_reserve(Buf *buf, size_t extra) {
    size_t need;
    size_t cap;
    char *next;

    if (extra > (size_t)-1 - buf->len - 1) {
        return -1;
    }
    need = buf->len + extra + 1;
    if (need <= buf->cap) {
        return 0;
    }
    cap = buf->cap ? buf->cap : 128;
    while (cap < need) {
        if (cap > ((size_t)-1) / 2) {
            cap = need;
            break;
        }
        cap *= 2;
    }
    next = (char *)realloc(buf->data, cap);
    if (!next) {
        return -1;
    }
    buf->data = next;
    buf->cap = cap;
    return 0;
}

static int buf_append(Buf *buf, const void *text, size_t len) {
    if (buf_reserve(buf, len) != 0) {
        return -1;
    }
    if (len) {
        memcpy(buf->data + buf->len, text, len);
    }
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 0;
}

static int buf_puts(Buf *buf, const char *text) {
    return buf_append(buf, text ? text : "", text ? strlen(text) : 0);
}

static int buf_putc(Buf *buf, char ch) {
    return buf_append(buf, &ch, 1);
}

static int buf_printf(Buf *buf, const char *fmt, ...) {
    va_list ap;
    va_list copy;
    int need;
    char *scratch;

    va_start(ap, fmt);
    va_copy(copy, ap);
    need = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (need < 0) {
        va_end(ap);
        return -1;
    }
    scratch = (char *)malloc((size_t)need + 1);
    if (!scratch) {
        va_end(ap);
        return -1;
    }
    vsnprintf(scratch, (size_t)need + 1, fmt, ap);
    va_end(ap);
    if (buf_append(buf, scratch, (size_t)need) != 0) {
        free(scratch);
        return -1;
    }
    free(scratch);
    return 0;
}

static char *buf_steal(Buf *buf) {
    char *data = buf->data ? buf->data : strdup("");
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    return data;
}

static void sl_init(StrList *list) {
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static void sl_free(StrList *list) {
    size_t i;
    if (!list) {
        return;
    }
    for (i = 0; i < list->count; i++) {
        free(list->items[i]);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int sl_has(const StrList *list, const char *text) {
    size_t i;
    if (!list || !text) {
        return 0;
    }
    for (i = 0; i < list->count; i++) {
        if (strcmp(list->items[i], text) == 0) {
            return 1;
        }
    }
    return 0;
}

static int sl_add(StrList *list, const char *text) {
    char *copy;
    char **next;
    if (!text) {
        return -1;
    }
    copy = strdup(text);
    if (!copy) {
        return -1;
    }
    if (list->count == list->cap) {
        size_t cap = list->cap ? list->cap * 2 : 16;
        next = (char **)realloc(list->items, cap * sizeof(*next));
        if (!next) {
            free(copy);
            return -1;
        }
        list->items = next;
        list->cap = cap;
    }
    list->items[list->count++] = copy;
    return 0;
}

static int sl_add_unique(StrList *list, const char *text) {
    if (!text || !text[0] || sl_has(list, text)) {
        return 0;
    }
    return sl_add(list, text);
}

static int cmp_cstr(const void *a, const void *b) {
    const char *sa = *(char *const *)a;
    const char *sb = *(char *const *)b;
    return strcmp(sa, sb);
}

static void sl_sort(StrList *list) {
    if (list->count > 1) {
        qsort(list->items, list->count, sizeof(char *), cmp_cstr);
    }
}

static char *read_file(const char *path, size_t *out_len) {
    FILE *file;
    long size;
    char *data;
    size_t got;

    file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    data = (char *)malloc((size_t)size + 1);
    if (!data) {
        fclose(file);
        return NULL;
    }
    got = fread(data, 1, (size_t)size, file);
    fclose(file);
    if (got != (size_t)size) {
        free(data);
        return NULL;
    }
    data[size] = '\0';
    if (out_len) {
        *out_len = (size_t)size;
    }
    return data;
}

static char *fold_crlf(const char *text, size_t len, size_t *out_len) {
    char *out = (char *)malloc(len + 1);
    size_t i;
    size_t w = 0;
    if (!out) {
        return NULL;
    }
    for (i = 0; i < len; i++) {
        if (text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') {
            continue;
        }
        out[w++] = text[i];
    }
    out[w] = '\0';
    if (out_len) {
        *out_len = w;
    }
    return out;
}

static int atomic_write(const char *path, const void *data, size_t len) {
    char *tmp;
    FILE *file;
    size_t wrote;

    tmp = (char *)malloc(strlen(path) + 32);
    if (!tmp) {
        return -1;
    }
#ifdef _WIN32
    snprintf(tmp, strlen(path) + 32, "%s.tmp-%lu", path, (unsigned long)_getpid());
#else
    snprintf(tmp, strlen(path) + 32, "%s.tmp-%d", path, (int)getpid());
#endif
    file = fopen(tmp, "wb");
    if (!file) {
        free(tmp);
        return -1;
    }
    wrote = fwrite(data, 1, len, file);
    if (wrote != len || fflush(file) != 0) {
        fclose(file);
        unlink(tmp);
        free(tmp);
        return -1;
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        fclose(file);
        unlink(tmp);
        free(tmp);
        return -1;
    }
    if (fclose(file) != 0) {
        unlink(tmp);
        free(tmp);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
#else
    if (rename(tmp, path) != 0) {
#endif
        unlink(tmp);
        free(tmp);
        return -1;
    }
    free(tmp);
    return 0;
}

static int write_if_changed(const char *path, const void *data, size_t len, int fold, int *wrote) {
    size_t old_len = 0;
    char *old = read_file(path, &old_len);
    int same = 0;

    if (wrote) {
        *wrote = 0;
    }
    if (old) {
        if (!fold) {
            same = old_len == len && (len == 0 || memcmp(old, data, len) == 0);
        } else {
            size_t a_len = 0;
            size_t b_len = 0;
            char *a = fold_crlf(old, old_len, &a_len);
            char *b = fold_crlf((const char *)data, len, &b_len);
            same = a && b && a_len == b_len && (a_len == 0 || memcmp(a, b, a_len) == 0);
            free(a);
            free(b);
        }
        free(old);
    }
    if (same) {
        return 0;
    }
    if (atomic_write(path, data, len) != 0) {
        return -1;
    }
    if (wrote) {
        *wrote = 1;
    }
    return 0;
}

static int file_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path) {
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *join_path(const char *a, const char *b) {
    size_t na = strlen(a);
    size_t nb = strlen(b);
    int slash = na > 0 && a[na - 1] != '/';
    char *out = (char *)malloc(na + nb + 2);
    if (!out) {
        return NULL;
    }
    memcpy(out, a, na);
    if (slash) {
        out[na] = '/';
        memcpy(out + na + 1, b, nb + 1);
    } else {
        memcpy(out + na, b, nb + 1);
    }
    return out;
}

static char *join_path3(const char *a, const char *b, const char *c) {
    char *ab = join_path(a, b);
    char *abc;
    if (!ab) {
        return NULL;
    }
    abc = join_path(ab, c);
    free(ab);
    return abc;
}

static char *realpath_dup(const char *path) {
#ifdef _WIN32
    char buf[4096];
    DWORD n;
    size_t i;
    char *out;
    if (!path) return NULL;
    n = GetFullPathNameA(path, (DWORD)sizeof buf, buf, NULL);
    if (n == 0 || n >= sizeof buf) return NULL;
    for (i = 0; buf[i]; i++) {
        if (buf[i] == '\\') buf[i] = '/';
        else if (buf[i] >= 'A' && buf[i] <= 'Z') buf[i] = (char)(buf[i] - 'A' + 'a');
    }
    out = (char *)malloc(strlen(buf) + 1);
    if (!out) return NULL;
    memcpy(out, buf, strlen(buf) + 1);
    return out;
#else
    return realpath(path, NULL);
#endif
}

static int path_within(const char *root, const char *path) {
    char *root_real = realpath_dup(root);
    char *path_real = realpath_dup(path);
    size_t n;
    int ok = 0;

    if (root_real && path_real) {
        n = strlen(root_real);
        if (strncmp(path_real, root_real, n) == 0 && (path_real[n] == '\0' || path_real[n] == '/')) {
            ok = 1;
        }
    }
    free(root_real);
    free(path_real);
    return ok;
}

int gallery_section_ok(const char *section) {
    return section && (strcmp(section, "sales") == 0 || strcmp(section, "articles") == 0 ||
                       strcmp(section, "awards") == 0 || strcmp(section, "projects") == 0);
}

int gallery_valid_slug(const char *slug) {
    size_t i;
    size_t n;
    if (!slug) {
        return 0;
    }
    n = strlen(slug);
    if (n == 0 || n > 512 || strcmp(slug, ".") == 0 || strcmp(slug, "..") == 0) {
        return 0;
    }
    if (strstr(slug, "..")) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)slug[i];
        if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\') {
            return 0;
        }
    }
    return 1;
}

static int valid_image_name(const char *name) {
    size_t i;
    size_t n;
    if (!name) {
        return 0;
    }
    n = strlen(name);
    if (n == 0 || n > 512 || strcmp(name, ".") == 0 || strcmp(name, "..") == 0 || strstr(name, "..")) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\') {
            return 0;
        }
    }
    return 1;
}

static const char *last_slash(const char *text, size_t len) {
    size_t i = len;
    while (i > 0) {
        i--;
        if (text[i] == '/') {
            return text + i;
        }
    }
    return NULL;
}

static int ends_with_ci(const char *text, const char *suffix) {
    size_t n = strlen(text);
    size_t m = strlen(suffix);
    if (n < m) {
        return 0;
    }
    return strcasecmp(text + n - m, suffix) == 0;
}

static int is_image_name(const char *name) {
    return ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") || ends_with_ci(name, ".png") ||
           ends_with_ci(name, ".gif") || ends_with_ci(name, ".bmp") || ends_with_ci(name, ".tiff");
}

static int hex_nibble(int ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

static int is_unreserved(unsigned char ch) {
    return isalnum(ch) || ch == '-' || ch == '.' || ch == '_' || ch == '~';
}

static char *url_encode(const char *text) {
    static const char *hex = "0123456789ABCDEF";
    Buf buf;
    const unsigned char *p;

    buf_init(&buf);
    for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
        if (is_unreserved(*p)) {
            if (buf_putc(&buf, (char)*p) != 0) {
                buf_free(&buf);
                return NULL;
            }
        } else {
            char tmp[3];
            tmp[0] = '%';
            tmp[1] = hex[(*p >> 4) & 0xF];
            tmp[2] = hex[*p & 0xF];
            if (buf_append(&buf, tmp, 3) != 0) {
                buf_free(&buf);
                return NULL;
            }
        }
    }
    return buf_steal(&buf);
}

static char *url_decode(const char *text, size_t len) {
    Buf buf;
    size_t i;

    buf_init(&buf);
    for (i = 0; i < len; i++) {
        if (text[i] == '%' && i + 2 < len) {
            int hi = hex_nibble((unsigned char)text[i + 1]);
            int lo = hex_nibble((unsigned char)text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                char ch = (char)((hi << 4) | lo);
                if (buf_putc(&buf, ch) != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                i += 2;
                continue;
            }
        }
        if (buf_putc(&buf, text[i]) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    return buf_steal(&buf);
}

static int utf8_decode(const unsigned char *text, size_t len, unsigned *cp, size_t *adv) {
    unsigned char c;
    unsigned value;
    size_t need;
    size_t i;

    if (len == 0) {
        return -1;
    }
    c = text[0];
    if (c < 0x80) {
        *cp = c;
        *adv = 1;
        return 0;
    }
    if ((c & 0xE0) == 0xC0) {
        value = c & 0x1F;
        need = 2;
    } else if ((c & 0xF0) == 0xE0) {
        value = c & 0x0F;
        need = 3;
    } else if ((c & 0xF8) == 0xF0) {
        value = c & 0x07;
        need = 4;
    } else {
        return -1;
    }
    if (len < need) {
        return -1;
    }
    for (i = 1; i < need; i++) {
        if ((text[i] & 0xC0) != 0x80) {
            return -1;
        }
        value = (value << 6) | (text[i] & 0x3F);
    }
    *cp = value;
    *adv = need;
    return 0;
}

static int utf8_encode(unsigned cp, char out[4]) {
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static const char *latin1_map(unsigned cp) {
    static const char *const table[96] = {
        " ", "!", "C/", "PS", "$?", "Y=", "|", "SS", "\"", "(c)", "a", "<<", "!", "", "(r)", "-",
        "deg", "+-", "2", "3", "'", "u", "P", "*", ",", "1", "o", ">>", " 1/4", " 1/2", " 3/4", "?",
        "A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
        "D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss",
        "a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        "d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y"
    };
    if (cp < 0xA0 || cp > 0xFF) {
        return NULL;
    }
    return table[cp - 0xA0];
}

static const char *punct_map(unsigned cp) {
    switch (cp) {
    case 0x2010:
    case 0x2011:
    case 0x2012:
    case 0x2013:
        return "-";
    case 0x2014:
    case 0x2015:
        return "--";
    case 0x2018:
    case 0x2019:
    case 0x201B:
    case 0x2032:
        return "'";
    case 0x201A:
        return ",";
    case 0x201C:
    case 0x201D:
        return "\"";
    case 0x201E:
        return ",,";
    case 0x2022:
        return "*";
    case 0x2026:
        return "...";
    case 0x2033:
        return "''";
    case 0x20AC:
        return "EUR";
    default:
        return NULL;
    }
}

static int append_translit(Buf *buf, const char *text, size_t len) {
    size_t i = 0;
    while (i < len) {
        unsigned cp = 0;
        size_t adv = 0;
        const char *mapped;
        if (utf8_decode((const unsigned char *)text + i, len - i, &cp, &adv) != 0) {
            if (buf_putc(buf, text[i]) != 0) {
                return -1;
            }
            i++;
            continue;
        }
        mapped = latin1_map(cp);
        if (!mapped) {
            mapped = punct_map(cp);
        }
        if (mapped) {
            if (buf_puts(buf, mapped) != 0) {
                return -1;
            }
        } else if (cp < 0x80) {
            if (buf_putc(buf, (char)cp) != 0) {
                return -1;
            }
        } else {
            char encoded[4];
            int n = utf8_encode(cp, encoded);
            if (n < 0 || buf_append(buf, encoded, (size_t)n) != 0) {
                return -1;
            }
        }
        i += adv;
    }
    return 0;
}

static int is_trim_space(unsigned cp) {
    return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == '\f' || cp == '\v' || cp == 0xA0 ||
           (cp >= 0x2000 && cp <= 0x200A) || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
           cp == 0x3000;
}

static char *trim_copy(const char *text, size_t len) {
    size_t i = 0;
    size_t end = len;
    size_t start = 0;
    char *out;

    while (i < end) {
        unsigned cp = 0;
        size_t adv = 0;
        if (utf8_decode((const unsigned char *)text + i, end - i, &cp, &adv) != 0 || !is_trim_space(cp)) {
            break;
        }
        i += adv;
    }
    start = i;
    i = end;
    while (i > start) {
        size_t prev = i - 1;
        while (prev > start && ((unsigned char)text[prev] & 0xC0) == 0x80) {
            prev--;
        }
        {
            unsigned cp = 0;
            size_t adv = 0;
            if (utf8_decode((const unsigned char *)text + prev, i - prev, &cp, &adv) != 0 || !is_trim_space(cp) ||
                prev + adv != i) {
                break;
            }
            i = prev;
        }
    }
    out = (char *)malloc(i - start + 1);
    if (!out) {
        return NULL;
    }
    memcpy(out, text + start, i - start);
    out[i - start] = '\0';
    return out;
}

static int append_codepoint(Buf *buf, unsigned cp) {
    char encoded[4];
    int n = utf8_encode(cp, encoded);
    if (n < 0) {
        return -1;
    }
    return buf_append(buf, encoded, (size_t)n);
}

static int decode_entities(Buf *buf, const char *text, size_t len) {
    static const struct {
        const char *name;
        unsigned cp;
        const char *literal;
    } named[] = {
        {"amp", '&', NULL},
        {"apos", '\'', NULL},
        {"deg", 0xB0, NULL},
        {"gt", '>', NULL},
        {"hellip", 0x2026, NULL},
        {"ldquo", 0x201C, NULL},
        {"lsquo", 0x2018, NULL},
        {"lt", '<', NULL},
        {"mdash", 0x2014, NULL},
        {"nbsp", 0xA0, NULL},
        {"ndash", 0x2013, NULL},
        {"quot", '"', NULL},
        {"rdquo", 0x201D, NULL},
        {"rsquo", 0x2019, NULL},
        {"shy", 0xAD, NULL}
    };
    size_t i = 0;

    while (i < len) {
        size_t semi;
        size_t name_len;
        size_t k;
        int matched = 0;
        if (text[i] != '&') {
            if (buf_putc(buf, text[i]) != 0) {
                return -1;
            }
            i++;
            continue;
        }
        semi = i + 1;
        while (semi < len && semi < i + 24 && text[semi] != ';' && text[semi] != '&' && text[semi] != ' ' &&
               text[semi] != '<') {
            semi++;
        }
        if (semi >= len || text[semi] != ';') {
            if (buf_putc(buf, '&') != 0) {
                return -1;
            }
            i++;
            continue;
        }
        name_len = semi - (i + 1);
        if (name_len > 1 && text[i + 1] == '#') {
            unsigned cp = 0;
            int ok = 1;
            if (text[i + 2] == 'x' || text[i + 2] == 'X') {
                size_t h;
                if (i + 3 >= semi) {
                    ok = 0;
                }
                for (h = i + 3; ok && h < semi; h++) {
                    int digit = hex_nibble((unsigned char)text[h]);
                    if (digit < 0 || cp > 0x10FFFF) {
                        ok = 0;
                        break;
                    }
                    cp = (cp << 4) | (unsigned)digit;
                }
            } else {
                size_t h;
                if (i + 2 >= semi) {
                    ok = 0;
                }
                for (h = i + 2; ok && h < semi; h++) {
                    if (!isdigit((unsigned char)text[h]) || cp > 0x10FFFF) {
                        ok = 0;
                        break;
                    }
                    cp = cp * 10u + (unsigned)(text[h] - '0');
                }
            }
            if (ok && cp > 0 && cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF)) {
                if (append_codepoint(buf, cp) != 0) {
                    return -1;
                }
                i = semi + 1;
                continue;
            }
        }
        for (k = 0; k < sizeof(named) / sizeof(named[0]); k++) {
            if (strlen(named[k].name) == name_len && strncmp(text + i + 1, named[k].name, name_len) == 0) {
                if (named[k].literal) {
                    if (buf_puts(buf, named[k].literal) != 0) {
                        return -1;
                    }
                } else if (append_codepoint(buf, named[k].cp) != 0) {
                    return -1;
                }
                matched = 1;
                break;
            }
        }
        if (!matched) {
            if (buf_putc(buf, '&') != 0) {
                return -1;
            }
            i++;
            continue;
        }
        i = semi + 1;
    }
    return 0;
}

static char *strip_tags(const char *html, size_t len) {
    Buf buf;
    size_t i = 0;
    int in_tag = 0;
    char quote = 0;

    buf_init(&buf);
    while (i < len) {
        char ch = html[i];
        if (!in_tag) {
            if (ch == '<') {
                in_tag = 1;
                quote = 0;
            } else if (buf_putc(&buf, ch) != 0) {
                buf_free(&buf);
                return NULL;
            }
        } else if (quote) {
            if (ch == quote) {
                quote = 0;
            }
        } else if (ch == '"' || ch == '\'') {
            quote = ch;
        } else if (ch == '>') {
            in_tag = 0;
        }
        i++;
    }
    return buf_steal(&buf);
}

char *gallery_caption_from_html(const char *html, size_t len) {
    char *stripped;
    Buf decoded;
    char *trimmed;
    Buf ascii;
    char *result;

    if (!html) {
        html = "";
        len = 0;
    }
    stripped = strip_tags(html, len);
    if (!stripped) {
        return NULL;
    }
    buf_init(&decoded);
    if (decode_entities(&decoded, stripped, strlen(stripped)) != 0) {
        free(stripped);
        buf_free(&decoded);
        return NULL;
    }
    free(stripped);
    trimmed = trim_copy(decoded.data ? decoded.data : "", decoded.len);
    buf_free(&decoded);
    if (!trimmed) {
        return NULL;
    }
    buf_init(&ascii);
    if (append_translit(&ascii, trimmed, strlen(trimmed)) != 0) {
        free(trimmed);
        buf_free(&ascii);
        return NULL;
    }
    free(trimmed);
    result = buf_steal(&ascii);
    return result;
}

static char *object_filename(const JsonValue *object) {
    JsonValue *name = json_object_get(object, "name");
    size_t name_len = 0;
    const char *name_text = json_string_get(name, &name_len);
    JsonValue *image;
    JsonValue *path;
    size_t path_len = 0;
    const char *path_text;
    const char *slash;
    char *decoded;

    if (name_text && name_len) {
        return strndup(name_text, name_len);
    }
    image = json_object_get(object, "image");
    path = json_object_get(image, "path");
    path_text = json_string_get(path, &path_len);
    if (!path_text) {
        return strdup("");
    }
    slash = last_slash(path_text, path_len);
    if (slash) {
        size_t off = (size_t)(slash - path_text) + 1;
        decoded = url_decode(path_text + off, path_len - off);
    } else {
        decoded = url_decode(path_text, path_len);
    }
    return decoded ? decoded : strdup("");
}

char *gallery_image_data_from_doc(const JsonDoc *doc, size_t *out_len) {
    JsonValue *objects;
    size_t i;
    size_t count;
    Buf buf;

    buf_init(&buf);
    if (out_len) {
        *out_len = 0;
    }
    if (!doc || !doc->root) {
        return buf_steal(&buf);
    }
    objects = json_object_get(doc->root, "objects");
    count = json_array_len(objects);
    for (i = 0; i < count; i++) {
        JsonValue *object = json_array_at(objects, i);
        JsonValue *image = json_object_get(object, "image");
        JsonValue *path;
        JsonValue *caption_html;
        size_t path_len = 0;
        const char *path_text;
        const char *segment;
        size_t segment_len;
        char *name;
        char *caption = NULL;
        const char *slash;

        if (!image) {
            continue;
        }
        path = json_object_get(image, "path");
        path_text = json_string_get(path, &path_len);
        if (!path_text) {
            continue;
        }
        slash = last_slash(path_text, path_len);
        if (slash) {
            segment = slash + 1;
            segment_len = path_len - (size_t)(segment - path_text);
        } else {
            segment = path_text;
            segment_len = path_len;
        }
        name = url_decode(segment, segment_len);
        if (!name) {
            buf_free(&buf);
            return NULL;
        }
        caption_html = json_object_get(object, "imageCaption");
        if (caption_html && json_type(caption_html) == JSON_STRING) {
            size_t html_len = 0;
            const char *html = json_string_get(caption_html, &html_len);
            caption = gallery_caption_from_html(html, html_len);
            if (!caption) {
                free(name);
                buf_free(&buf);
                return NULL;
            }
        }
        if (buf_puts(&buf, name) != 0) {
            free(name);
            free(caption);
            buf_free(&buf);
            return NULL;
        }
        if (caption && caption[0]) {
            if (buf_putc(&buf, '=') != 0 || buf_puts(&buf, caption) != 0) {
                free(name);
                free(caption);
                buf_free(&buf);
                return NULL;
            }
        }
        if (buf_puts(&buf, "\n\n") != 0) {
            free(name);
            free(caption);
            buf_free(&buf);
            return NULL;
        }
        free(name);
        free(caption);
    }
    if (out_len) {
        *out_len = buf.len;
    }
    return buf.data ? buf.data : buf_steal(&buf);
}

static int read_u16be(FILE *file, unsigned *out) {
    int hi = fgetc(file);
    int lo = fgetc(file);
    if (hi < 0 || lo < 0) {
        return -1;
    }
    *out = ((unsigned)hi << 8) | (unsigned)lo;
    return 0;
}

static int read_png_size(FILE *file, int *width, int *height) {
    unsigned char header[16];
    if (fread(header, 1, 16, file) != 16 || memcmp(header + 4, "IHDR", 4) != 0) {
        return -1;
    }
    *width = (header[8] << 24) | (header[9] << 16) | (header[10] << 8) | header[11];
    *height = (header[12] << 24) | (header[13] << 16) | (header[14] << 8) | header[15];
    return (*width > 0 && *height > 0) ? 0 : -1;
}

static int read_jpeg_size(FILE *file, int *width, int *height) {
    if (fseek(file, 2, SEEK_SET) != 0) {
        return -1;
    }
    for (;;) {
        int marker;
        unsigned length;
        int sof;
        do {
            marker = fgetc(file);
            if (marker < 0) {
                return -1;
            }
        } while (marker == 0xFF);
        if (marker == 0xD9 || marker == 0xDA) {
            return -1;
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            continue;
        }
        if (read_u16be(file, &length) != 0 || length < 2) {
            return -1;
        }
        sof = marker == 0xC0 || marker == 0xC1 || marker == 0xC2 || marker == 0xC3 || marker == 0xC5 ||
              marker == 0xC6 || marker == 0xC7 || marker == 0xC9 || marker == 0xCA || marker == 0xCB ||
              marker == 0xCD || marker == 0xCE || marker == 0xCF;
        if (sof) {
            int precision = fgetc(file);
            unsigned hh = 0;
            unsigned ww = 0;
            if (precision < 0 || read_u16be(file, &hh) != 0 || read_u16be(file, &ww) != 0) {
                return -1;
            }
            *width = (int)ww;
            *height = (int)hh;
            return (*width > 0 && *height > 0) ? 0 : -1;
        }
        if (fseek(file, (long)(length - 2), SEEK_CUR) != 0) {
            return -1;
        }
    }
}

int gallery_image_size(const char *path, int *width, int *height) {
    FILE *file;
    unsigned char sig[8];
    static const unsigned char png[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    int rc;

    if (width) {
        *width = 0;
    }
    if (height) {
        *height = 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    if (fread(sig, 1, 8, file) != 8) {
        fclose(file);
        return -1;
    }
    if (memcmp(sig, png, 8) == 0) {
        rc = read_png_size(file, width, height);
        fclose(file);
        return rc;
    }
    if (sig[0] == 0xFF && sig[1] == 0xD8) {
        rc = read_jpeg_size(file, width, height);
        fclose(file);
        return rc;
    }
    fclose(file);
    return -1;
}

static int caption_rejected(const char *caption, char *err, size_t err_sz) {
    const unsigned char *p;
    if (!caption) {
        return 0;
    }
    if (strchr(caption, '=')) {
        set_err(err, err_sz, CAPTION_EQUALS_ERROR);
        return 1;
    }
    for (p = (const unsigned char *)caption; *p; p++) {
        if (*p < 0x20 || *p == 0x7F) {
            set_err(err, err_sz, CAPTION_CONTROL_ERROR);
            return 1;
        }
    }
    return 0;
}

static void format_mtime(time_t seconds, long nsec, char *out, size_t out_sz) {
    struct tm tm;
    long ms = nsec / 1000000L;
    if (ms < 0) {
        ms = 0;
    }
    if (ms > 999) {
        ms = 999;
    }
    gmtime_r(&seconds, &tm);
    snprintf(out, out_sz, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms);
}

static int set_string_field(JsonValue *object, const char *after_key, const char *key, const char *text) {
    return json_object_put_after(object, after_key, key, json_string_cstr(text ? text : ""));
}

static void sync_caption_fields(JsonValue *object, const char *caption) {
    JsonValue *html_value = json_object_get(object, "imageCaption");
    char *current = NULL;
    char *wrapped;
    size_t html_len = 0;
    const char *html;

    html = json_string_get(html_value, &html_len);
    if (html) {
        current = gallery_caption_from_html(html, html_len);
    }
    if ((current ? current : "")[0] == '\0' && (!caption || caption[0] == '\0')) {
        free(current);
        return;
    }
    if (current && caption && strcmp(current, caption) == 0) {
        free(current);
        return;
    }
    free(current);
    if (!caption || caption[0] == '\0') {
        json_object_remove(object, "imageCaption");
        json_object_remove(object, "comment");
        return;
    }
    wrapped = (char *)malloc(strlen(caption) + 32);
    if (!wrapped) {
        return;
    }
    sprintf(wrapped, "<div class=\"comment\">%s</div>", caption);
    set_string_field(object, "image", "imageCaption", wrapped);
    set_string_field(object, "name", "comment", caption);
    free(wrapped);
}

static JsonValue *new_slide_object(const char *name, int width, int height, int thumb_w, int thumb_h,
                                   long long file_size, time_t mtime, long nsec, int index, int total) {
    JsonValue *object = json_object();
    JsonValue *image = json_object();
    JsonValue *thumb = json_object();
    JsonValue *dates = json_object();
    JsonValue *camera = json_object();
    char *encoded;
    char *image_path;
    char *thumb_path;
    char file_date[96];
    char resolution[64];
    char thumb_caption[80];

    if (!object || !image || !thumb || !dates || !camera) {
        json_free(object);
        json_free(image);
        json_free(thumb);
        json_free(dates);
        json_free(camera);
        return NULL;
    }
    encoded = url_encode(name);
    if (!encoded) {
        json_free(object);
        json_free(image);
        json_free(thumb);
        json_free(dates);
        json_free(camera);
        return NULL;
    }
    image_path = (char *)malloc(strlen(encoded) + 8);
    thumb_path = (char *)malloc(strlen(encoded) + 8);
    if (!image_path || !thumb_path) {
        free(encoded);
        free(image_path);
        free(thumb_path);
        json_free(object);
        json_free(image);
        json_free(thumb);
        json_free(dates);
        json_free(camera);
        return NULL;
    }
    sprintf(image_path, "slides/%s", encoded);
    sprintf(thumb_path, "thumbs/%s", encoded);
    format_mtime(mtime, nsec, file_date, sizeof(file_date));
    snprintf(resolution, sizeof(resolution), "%d x %d", width, height);
    snprintf(thumb_caption, sizeof(thumb_caption), "<div class=\"nr\">%d / %d</div>", index, total);
    json_object_put(object, "path", json_string_cstr(encoded));
    json_object_put(image, "path", json_string_cstr(image_path));
    json_object_put(image, "width", json_number_int(width));
    json_object_put(image, "height", json_number_int(height));
    json_object_put(object, "image", image);
    json_object_put(thumb, "path", json_string_cstr(thumb_path));
    json_object_put(thumb, "width", json_number_int(thumb_w));
    json_object_put(thumb, "height", json_number_int(thumb_h));
    json_object_put(object, "thumb", thumb);
    json_object_put(object, "fileSize", json_number_int(file_size));
    json_object_put(object, "thumbCaption", json_string_cstr(thumb_caption));
    json_object_put(object, "name", json_string_cstr(name));
    json_object_put(dates, "dateTaken", json_number_int((long long)mtime));
    json_object_put(object, "dates", dates);
    json_object_put(object, "fileDate", json_string_cstr(file_date));
    json_object_put(object, "category", json_string_cstr("image"));
    json_object_put(camera, "resolution", json_string_cstr(resolution));
    json_object_put(object, "camera", camera);
    free(encoded);
    free(image_path);
    free(thumb_path);
    return object;
}

static void copy_json_field(JsonValue *dst, JsonValue *src, const char *key) {
    JsonValue *sv = json_object_get(src, key);
    JsonValue *dv = json_object_get(dst, key);
    if (!sv) {
        return;
    }
    if (json_type(sv) == JSON_STRING) {
        size_t n = 0;
        const char *text = json_string_get(sv, &n);
        if (dv && json_type(dv) == JSON_STRING) {
            size_t m = 0;
            const char *old = json_string_get(dv, &m);
            if (old && m == n && memcmp(old, text, n) == 0) {
                return;
            }
            json_string_set(dv, text, n);
        } else {
            json_object_put(dst, key, json_string(text, n));
        }
    } else if (json_type(sv) == JSON_NUMBER) {
        const char *lex = json_number_lexeme(sv);
        if (dv && json_type(dv) == JSON_NUMBER) {
            const char *old = json_number_lexeme(dv);
            if (old && lex && strcmp(old, lex) == 0) {
                return;
            }
            json_number_set_lexeme(dv, lex);
        } else if (lex) {
            json_object_put(dst, key, json_number(lex));
        }
    }
}

static void update_counter_group(JsonValue *root, const char *key, long long total) {
    JsonValue *group = json_object_get(root, key);
    const char *fields[] = {"total", "images", "files"};
    size_t i;
    if (!group || json_type(group) != JSON_OBJECT) {
        group = json_object();
        json_object_put(root, key, group);
    }
    for (i = 0; i < 3; i++) {
        JsonValue *value = json_object_get(group, fields[i]);
        long long current = 0;
        if (value && json_type(value) == JSON_NUMBER && json_as_int(value, &current) == 0 && current == total) {
            continue;
        }
        if (value && json_type(value) == JSON_NUMBER) {
            json_number_set_int(value, total);
        } else {
            json_object_put(group, fields[i], json_number_int(total));
        }
    }
}

static void update_thumb_captions(JsonValue *objects, size_t total) {
    size_t i;
    for (i = 0; i < total; i++) {
        JsonValue *object = json_array_at(objects, i);
        JsonValue *existing;
        char caption[80];
        size_t old_len = 0;
        const char *old;
        snprintf(caption, sizeof(caption), "<div class=\"nr\">%zu / %zu</div>", i + 1, total);
        existing = json_object_get(object, "thumbCaption");
        old = json_string_get(existing, &old_len);
        if (old && strlen(caption) == old_len && memcmp(old, caption, old_len) == 0) {
            continue;
        }
        json_object_put_after(object, "fileSize", "thumbCaption", json_string_cstr(caption));
    }
}

int gallery_apply_image_list(JsonDoc *doc, const JsonValue *images, const char *folder_fs, char *err, size_t err_sz) {
    JsonValue *objects;
    size_t count;
    size_t old_count;
    size_t i;
    int *used = NULL;
    int *src_index = NULL;
    char **names = NULL;
    char **captions = NULL;
    int *is_new = NULL;
    int *widths = NULL;
    int *heights = NULL;
    int *thumb_w = NULL;
    int *thumb_h = NULL;
    long long *sizes = NULL;
    time_t *mtimes = NULL;
    long *nsecs = NULL;
    JsonValue **taken = NULL;
    JsonValue *fresh = NULL;
    int structure_changed = 0;
    int rc = -1;

    if (!doc || !doc->root || json_type(doc->root) != JSON_OBJECT) {
        set_err(err, err_sz, "deep-data.json is not a JSON object");
        return -1;
    }
    if (!images || json_type(images) != JSON_ARRAY) {
        set_err(err, err_sz, "images must be an array");
        return 1;
    }
    objects = json_object_get(doc->root, "objects");
    if (!objects || json_type(objects) != JSON_ARRAY) {
        set_err(err, err_sz, "deep-data.json has no objects array");
        return -1;
    }
    count = json_array_len(images);
    old_count = json_array_len(objects);
    names = (char **)calloc(count ? count : 1, sizeof(*names));
    captions = (char **)calloc(count ? count : 1, sizeof(*captions));
    is_new = (int *)calloc(count ? count : 1, sizeof(*is_new));
    src_index = (int *)malloc((count ? count : 1) * sizeof(*src_index));
    widths = (int *)calloc(count ? count : 1, sizeof(*widths));
    heights = (int *)calloc(count ? count : 1, sizeof(*heights));
    thumb_w = (int *)calloc(count ? count : 1, sizeof(*thumb_w));
    thumb_h = (int *)calloc(count ? count : 1, sizeof(*thumb_h));
    sizes = (long long *)calloc(count ? count : 1, sizeof(*sizes));
    mtimes = (time_t *)calloc(count ? count : 1, sizeof(*mtimes));
    nsecs = (long *)calloc(count ? count : 1, sizeof(*nsecs));
    used = (int *)calloc(old_count ? old_count : 1, sizeof(*used));
    if (!names || !captions || !is_new || !src_index || !widths || !heights || !thumb_w || !thumb_h || !sizes ||
        !mtimes || !nsecs || !used) {
        set_err(err, err_sz, "out of memory");
        goto cleanup;
    }
    for (i = 0; i < count; i++) {
        src_index[i] = -1;
    }
    for (i = 0; i < count; i++) {
        JsonValue *item = json_array_at(images, i);
        JsonValue *name_value = json_object_get(item, "name");
        JsonValue *caption_value = json_object_get(item, "caption");
        size_t name_len = 0;
        const char *name_text = json_string_get(name_value, &name_len);
        char *slide;
        char *thumb;
        struct stat st;
        size_t j;
        if (!item || json_type(item) != JSON_OBJECT || !name_text) {
            set_err(err, err_sz, "Each image needs a name");
            rc = 1;
            goto cleanup;
        }
        names[i] = strndup(name_text, name_len);
        if (!names[i] || !valid_image_name(names[i])) {
            set_err(err, err_sz, "Image filename is not allowed");
            rc = 1;
            goto cleanup;
        }
        for (j = 0; j < i; j++) {
            if (strcmp(names[j], names[i]) == 0) {
                snprintf(err ? err : (char *)"", err ? err_sz : 0, "Duplicate image filename: %s", names[i]);
                rc = 1;
                goto cleanup;
            }
        }
        if (caption_value && json_type(caption_value) != JSON_STRING && json_type(caption_value) != JSON_NULL) {
            set_err(err, err_sz, "Image caption must be a string");
            rc = 1;
            goto cleanup;
        }
        if (caption_value && json_type(caption_value) == JSON_STRING) {
            size_t cap_len = 0;
            const char *cap = json_string_get(caption_value, &cap_len);
            captions[i] = cap ? strndup(cap, cap_len) : strdup("");
        } else {
            captions[i] = strdup("");
        }
        if (!captions[i]) {
            set_err(err, err_sz, "out of memory");
            goto cleanup;
        }
        if (caption_rejected(captions[i], err, err_sz)) {
            rc = 1;
            goto cleanup;
        }
        slide = join_path3(folder_fs, "slides", names[i]);
        thumb = join_path3(folder_fs, "thumbs", names[i]);
        if (!slide || !thumb || !file_exists(slide) || !file_exists(thumb)) {
            snprintf(err ? err : (char *)"", err ? err_sz : 0, "Image is missing from slides/ or thumbs/: %s", names[i]);
            free(slide);
            free(thumb);
            rc = 1;
            goto cleanup;
        }
        for (j = 0; j < old_count; j++) {
            char *existing;
            if (used[j]) {
                continue;
            }
            existing = object_filename(json_array_at(objects, j));
            if (existing && strcmp(existing, names[i]) == 0) {
                src_index[i] = (int)j;
                used[j] = 1;
                free(existing);
                break;
            }
            free(existing);
        }
        if (src_index[i] < 0) {
            is_new[i] = 1;
            if (gallery_image_size(slide, &widths[i], &heights[i]) != 0 ||
                gallery_image_size(thumb, &thumb_w[i], &thumb_h[i]) != 0 || stat(slide, &st) != 0) {
                snprintf(err ? err : (char *)"", err ? err_sz : 0, "Could not read image dimensions for %s", names[i]);
                free(slide);
                free(thumb);
                rc = 1;
                goto cleanup;
            }
            sizes[i] = (long long)st.st_size;
            mtimes[i] = st.st_mtime;
#if defined(_WIN32)
            nsecs[i] = 0;
#elif defined(__APPLE__)
            nsecs[i] = st.st_mtimespec.tv_nsec;
#else
            nsecs[i] = st.st_mtim.tv_nsec;
#endif
        }
        free(slide);
        free(thumb);
    }
    structure_changed = count != old_count;
    if (!structure_changed) {
        for (i = 0; i < count; i++) {
            if (is_new[i] || src_index[i] != (int)i) {
                structure_changed = 1;
                break;
            }
        }
    }
    taken = (JsonValue **)calloc(old_count ? old_count : 1, sizeof(*taken));
    if (!taken) {
        set_err(err, err_sz, "out of memory");
        goto cleanup;
    }
    for (i = old_count; i > 0; i--) {
        size_t index = i - 1;
        if (used[index]) {
            taken[index] = json_array_detach(objects, index);
        }
    }
    json_array_clear(objects);
    fresh = json_array();
    if (!fresh) {
        set_err(err, err_sz, "out of memory");
        goto cleanup_taken;
    }
    for (i = 0; i < count; i++) {
        JsonValue *object;
        if (is_new[i]) {
            object = new_slide_object(names[i], widths[i], heights[i], thumb_w[i], thumb_h[i], sizes[i], mtimes[i],
                                      nsecs[i], (int)i + 1, (int)count);
        } else {
            object = taken[src_index[i]];
            taken[src_index[i]] = NULL;
        }
        if (!object) {
            set_err(err, err_sz, "Could not build gallery object");
            json_free(fresh);
            fresh = NULL;
            goto cleanup_taken;
        }
        sync_caption_fields(object, captions[i]);
        if (json_array_add(fresh, object) != 0) {
            set_err(err, err_sz, "out of memory");
            json_free(fresh);
            fresh = NULL;
            goto cleanup_taken;
        }
    }
    json_array_transfer(objects, fresh);
    json_free(fresh);
    fresh = NULL;
    if (structure_changed) {
        JsonValue *first;
        JsonValue *top_thumb;
        update_thumb_captions(objects, count);
        update_counter_group(doc->root, "counters", (long long)count);
        update_counter_group(doc->root, "deepCounters", (long long)count);
        first = json_array_at(objects, 0);
        if (first) {
            JsonValue *src_thumb = json_object_get(first, "thumb");
            top_thumb = json_object_get(doc->root, "thumb");
            if (src_thumb) {
                if (!top_thumb || json_type(top_thumb) != JSON_OBJECT) {
                    top_thumb = json_object();
                    json_object_put(doc->root, "thumb", top_thumb);
                }
                copy_json_field(top_thumb, src_thumb, "path");
                copy_json_field(top_thumb, src_thumb, "width");
                copy_json_field(top_thumb, src_thumb, "height");
            }
        }
    }
    rc = 0;
    goto cleanup;

cleanup_taken:
    if (taken) {
        for (i = 0; i < old_count; i++) {
            json_free(taken[i]);
        }
    }
cleanup:
    if (names) {
        for (i = 0; i < count; i++) {
            free(names[i]);
            free(captions[i]);
        }
    }
    free(names);
    free(captions);
    free(is_new);
    free(src_index);
    free(widths);
    free(heights);
    free(thumb_w);
    free(thumb_h);
    free(sizes);
    free(mtimes);
    free(nsecs);
    free(used);
    free(taken);
    return rc;
}
typedef struct PhpField {
    int present;
    int is_heredoc;
    char quote;
    size_t value_start;
    size_t value_end;
    size_t body_start;
    size_t body_end;
    char *indent;
    char *decoded;
} PhpField;

typedef struct PhpHeader {
    PhpField vehicle_type;
    PhpField make_type;
    PhpField do_not_render;
    PhpField vehicle_name;
    PhpField inventory_number;
    PhpField price;
    PhpField description;
} PhpHeader;

typedef struct Edit {
    size_t start;
    size_t end;
    char *text;
} Edit;

typedef struct Edits {
    Edit *items;
    size_t count;
    size_t cap;
} Edits;

static void set_errf(char *err, size_t err_sz, const char *fmt, ...) {
    va_list ap;
    if (!err || err_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(err, err_sz, fmt, ap);
    va_end(ap);
}

static void php_field_clear(PhpField *field) {
    if (!field) {
        return;
    }
    free(field->indent);
    free(field->decoded);
    memset(field, 0, sizeof(*field));
}

static void php_header_clear(PhpHeader *header) {
    if (!header) {
        return;
    }
    php_field_clear(&header->vehicle_type);
    php_field_clear(&header->make_type);
    php_field_clear(&header->do_not_render);
    php_field_clear(&header->vehicle_name);
    php_field_clear(&header->inventory_number);
    php_field_clear(&header->price);
    php_field_clear(&header->description);
}

static void skip_ws_span(const char *text, size_t len, size_t *index) {
    while (*index < len && isspace((unsigned char)text[*index])) {
        (*index)++;
    }
}

static int read_ident(const char *text, size_t len, size_t *index, char *out, size_t out_sz) {
    size_t start;
    if (*index >= len || !(isalpha((unsigned char)text[*index]) || text[*index] == '_')) {
        return -1;
    }
    start = *index;
    (*index)++;
    while (*index < len && (isalnum((unsigned char)text[*index]) || text[*index] == '_')) {
        (*index)++;
    }
    if (*index - start >= out_sz) {
        return -1;
    }
    memcpy(out, text + start, *index - start);
    out[*index - start] = '\0';
    return 0;
}

static int skip_php_single(const char *text, size_t len, size_t *index) {
    if (*index >= len || text[*index] != '\'') {
        return -1;
    }
    (*index)++;
    while (*index < len) {
        if (text[*index] == '\\' && *index + 1 < len) {
            *index += 2;
            continue;
        }
        if (text[*index] == '\'') {
            (*index)++;
            return 0;
        }
        (*index)++;
    }
    return -1;
}

static int skip_php_double(const char *text, size_t len, size_t *index) {
    if (*index >= len || text[*index] != '"') {
        return -1;
    }
    (*index)++;
    while (*index < len) {
        if (text[*index] == '\\' && *index + 1 < len) {
            *index += 2;
            continue;
        }
        if (text[*index] == '"') {
            (*index)++;
            return 0;
        }
        (*index)++;
    }
    return -1;
}

static char *php_decode_single_span(const char *text, size_t start, size_t end) {
    Buf buf;
    size_t i = start + 1;
    buf_init(&buf);
    if (end == 0 || text[start] != '\'' || text[end - 1] != '\'') {
        buf_free(&buf);
        return NULL;
    }
    while (i + 1 < end) {
        if (text[i] == '\\' && (text[i + 1] == '\\' || text[i + 1] == '\'')) {
            if (buf_putc(&buf, text[i + 1]) != 0) {
                buf_free(&buf);
                return NULL;
            }
            i += 2;
            continue;
        }
        if (buf_putc(&buf, text[i]) != 0) {
            buf_free(&buf);
            return NULL;
        }
        i++;
    }
    return buf_steal(&buf);
}

static char *php_decode_double_span(const char *text, size_t start, size_t end) {
    Buf buf;
    size_t i = start + 1;
    buf_init(&buf);
    if (end == 0 || text[start] != '"' || text[end - 1] != '"') {
        buf_free(&buf);
        return NULL;
    }
    while (i + 1 < end) {
        if (text[i] == '\\' && i + 1 + 1 <= end) {
            char esc = text[i + 1];
            char out = esc;
            if (esc == 'n') {
                out = '\n';
            } else if (esc == 'r') {
                out = '\r';
            } else if (esc == 't') {
                out = '\t';
            }
            if (buf_putc(&buf, out) != 0) {
                buf_free(&buf);
                return NULL;
            }
            i += 2;
            continue;
        }
        if (buf_putc(&buf, text[i]) != 0) {
            buf_free(&buf);
            return NULL;
        }
        i++;
    }
    return buf_steal(&buf);
}

static char *php_quote(const char *text, char quote) {
    Buf buf;
    const unsigned char *p;
    if (quote != '\'' && quote != '"') {
        quote = '\'';
    }
    buf_init(&buf);
    if (buf_putc(&buf, quote) != 0) {
        buf_free(&buf);
        return NULL;
    }
    for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
        if (*p == (unsigned char)quote || *p == '\\' || (quote == '"' && *p == '$')) {
            if (buf_putc(&buf, '\\') != 0) {
                buf_free(&buf);
                return NULL;
            }
        } else if (quote == '"' && *p == '\n') {
            if (buf_puts(&buf, "\\n") != 0) {
                buf_free(&buf);
                return NULL;
            }
            continue;
        } else if (quote == '"' && *p == '\r') {
            if (buf_puts(&buf, "\\r") != 0) {
                buf_free(&buf);
                return NULL;
            }
            continue;
        }
        if (buf_putc(&buf, (char)*p) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    if (buf_putc(&buf, quote) != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static int is_php_number(const char *text) {
    const char *p = text;
    if (!p || !p[0]) {
        return 0;
    }
    if (*p == '-') {
        p++;
    }
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    if (*p == '0' && isdigit((unsigned char)p[1])) {
        return 0;
    }
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        while (isdigit((unsigned char)*p)) {
            p++;
        }
    }
    return *p == '\0';
}

static char *heredoc_decode(const char *body, size_t len, const char *indent) {
    Buf buf;
    size_t indent_len = indent ? strlen(indent) : 0;
    size_t i = 0;
    int first = 1;

    if (len && body[len - 1] == '\n') {
        len--;
    }
    if (len && body[len - 1] == '\r') {
        len--;
    }
    buf_init(&buf);
    if (len == 0) {
        return buf_steal(&buf);
    }
    while (1) {
        size_t line = i;
        size_t line_len;
        const char *text;
        while (i < len && body[i] != '\n') {
            i++;
        }
        line_len = i - line;
        if (line_len && body[line + line_len - 1] == '\r') {
            line_len--;
        }
        text = body + line;
        if (indent_len && line_len >= indent_len && memcmp(text, indent, indent_len) == 0) {
            text += indent_len;
            line_len -= indent_len;
        }
        if (!first && buf_putc(&buf, '\n') != 0) {
            buf_free(&buf);
            return NULL;
        }
        first = 0;
        if (line_len && buf_append(&buf, text, line_len) != 0) {
            buf_free(&buf);
            return NULL;
        }
        if (i >= len) {
            break;
        }
        i++;
        if (i == len) {
            if (buf_putc(&buf, '\n') != 0) {
                buf_free(&buf);
                return NULL;
            }
            break;
        }
    }
    return buf_steal(&buf);
}

static char *heredoc_encode(const char *value, const char *indent) {
    Buf buf;
    const char *cursor = value ? value : "";
    if (!indent) {
        indent = "";
    }
    buf_init(&buf);
    while (1) {
        if (buf_puts(&buf, indent) != 0) {
            buf_free(&buf);
            return NULL;
        }
        while (*cursor && *cursor != '\n') {
            if (buf_putc(&buf, *cursor) != 0) {
                buf_free(&buf);
                return NULL;
            }
            cursor++;
        }
        if (*cursor == '\n') {
            cursor++;
            if (buf_putc(&buf, '\n') != 0) {
                buf_free(&buf);
                return NULL;
            }
            if (*cursor == '\0') {
                if (buf_puts(&buf, indent) != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            }
            continue;
        }
        break;
    }
    if (buf_putc(&buf, '\n') != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static int parse_heredoc_at(const char *text, size_t len, size_t *index, PhpField *field) {
    char ident[64];
    char quote = 0;
    size_t i = *index;
    if (i + 3 > len || memcmp(text + i, "<<<", 3) != 0) {
        return -1;
    }
    i += 3;
    if (i < len && (text[i] == '\'' || text[i] == '"')) {
        quote = text[i++];
    }
    if (read_ident(text, len, &i, ident, sizeof(ident)) != 0) {
        return -1;
    }
    if (quote) {
        if (i >= len || text[i] != quote) {
            return -1;
        }
        i++;
    }
    if (i < len && text[i] == '\r') {
        i++;
    }
    if (i >= len || text[i] != '\n') {
        return -1;
    }
    i++;
    field->body_start = i;
    while (i < len) {
        size_t line = i;
        size_t line_len;
        size_t k;
        size_t id_len = strlen(ident);
        while (i < len && text[i] != '\n') {
            i++;
        }
        line_len = i - line;
        if (line_len && text[line + line_len - 1] == '\r') {
            line_len--;
        }
        k = 0;
        while (k < line_len && (text[line + k] == ' ' || text[line + k] == '\t')) {
            k++;
        }
        if (k + id_len < line_len && memcmp(text + line + k, ident, id_len) == 0 && text[line + k + id_len] == ';') {
            size_t j = k + id_len + 1;
            while (j < line_len && (text[line + j] == ' ' || text[line + j] == '\t')) {
                j++;
            }
            if (j == line_len) {
                field->is_heredoc = 1;
                field->body_end = line;
                field->indent = strndup(text + line, k);
                field->decoded = heredoc_decode(text + field->body_start, field->body_end - field->body_start, field->indent);
                if (i < len && text[i] == '\n') {
                    i++;
                }
                *index = i;
                return field->indent && field->decoded ? 0 : -1;
            }
        }
        if (i < len && text[i] == '\n') {
            i++;
        }
    }
    return -1;
}

static int parse_php_value(const char *text, size_t len, size_t *index, PhpField *field) {
    size_t i = *index;
    skip_ws_span(text, len, &i);
    field->value_start = i;
    if (i + 3 <= len && memcmp(text + i, "<<<", 3) == 0) {
        *index = i;
        return parse_heredoc_at(text, len, index, field);
    }
    if (i < len && text[i] == '\'') {
        size_t end = i;
        if (skip_php_single(text, len, &end) != 0) {
            return -1;
        }
        field->quote = '\'';
        field->value_end = end;
        field->decoded = php_decode_single_span(text, i, end);
        *index = end;
        return field->decoded ? 0 : -1;
    }
    if (i < len && text[i] == '"') {
        size_t end = i;
        if (skip_php_double(text, len, &end) != 0) {
            return -1;
        }
        field->quote = '"';
        field->value_end = end;
        field->decoded = php_decode_double_span(text, i, end);
        *index = end;
        return field->decoded ? 0 : -1;
    }
    if (i + 4 <= len && strncmp(text + i, "true", 4) == 0 && (i + 4 == len || !isalnum((unsigned char)text[i + 4]))) {
        field->decoded = strdup("true");
        field->value_end = i + 4;
        *index = field->value_end;
        return field->decoded ? 0 : -1;
    }
    if (i + 5 <= len && strncmp(text + i, "false", 5) == 0 && (i + 5 == len || !isalnum((unsigned char)text[i + 5]))) {
        field->decoded = strdup("false");
        field->value_end = i + 5;
        *index = field->value_end;
        return field->decoded ? 0 : -1;
    }
    if (i + 4 <= len && strncasecmp(text + i, "TRUE", 4) == 0 && (i + 4 == len || !isalnum((unsigned char)text[i + 4]))) {
        field->decoded = strdup("true");
        field->value_end = i + 4;
        *index = field->value_end;
        return 0;
    }
    if (i + 5 <= len && strncasecmp(text + i, "FALSE", 5) == 0 && (i + 5 == len || !isalnum((unsigned char)text[i + 5]))) {
        field->decoded = strdup("false");
        field->value_end = i + 5;
        *index = field->value_end;
        return 0;
    }
    if (i < len && (text[i] == '-' || isdigit((unsigned char)text[i]))) {
        size_t start = i;
        if (text[i] == '-') {
            i++;
        }
        if (i >= len || !isdigit((unsigned char)text[i])) {
            return -1;
        }
        while (i < len && isdigit((unsigned char)text[i])) {
            i++;
        }
        if (i < len && text[i] == '.' && i + 1 < len && isdigit((unsigned char)text[i + 1])) {
            i++;
            while (i < len && isdigit((unsigned char)text[i])) {
                i++;
            }
        }
        field->decoded = strndup(text + start, i - start);
        field->value_end = i;
        *index = i;
        return field->decoded ? 0 : -1;
    }
    return -1;
}

static PhpField *php_slot(PhpHeader *header, const char *name) {
    if (strcmp(name, "vehicleType") == 0) {
        return &header->vehicle_type;
    }
    if (strcmp(name, "makeType") == 0) {
        return &header->make_type;
    }
    if (strcmp(name, "doNotRender") == 0) {
        return &header->do_not_render;
    }
    if (strcmp(name, "vehicleName") == 0) {
        return &header->vehicle_name;
    }
    if (strcmp(name, "inventoryNumber") == 0) {
        return &header->inventory_number;
    }
    if (strcmp(name, "price") == 0) {
        return &header->price;
    }
    if (strcmp(name, "description") == 0) {
        return &header->description;
    }
    return NULL;
}

static int try_php_assignment(const char *text, size_t len, size_t *index, PhpHeader *header) {
    size_t i = *index;
    char name[64];
    PhpField field;
    PhpField *slot;
    if (i >= len || text[i] != '$') {
        return -1;
    }
    i++;
    if (read_ident(text, len, &i, name, sizeof(name)) != 0) {
        return -1;
    }
    skip_ws_span(text, len, &i);
    if (i >= len || text[i] != '=' || (i + 1 < len && text[i + 1] == '=')) {
        return -1;
    }
    i++;
    memset(&field, 0, sizeof(field));
    if (parse_php_value(text, len, &i, &field) != 0) {
        php_field_clear(&field);
        return -1;
    }
    slot = php_slot(header, name);
    if (slot && !slot->present) {
        *slot = field;
        slot->present = 1;
    } else {
        php_field_clear(&field);
    }
    *index = i;
    return 0;
}

static int scan_php_header(const char *text, size_t len, PhpHeader *header) {
    size_t i = 0;
    memset(header, 0, sizeof(*header));
    while (i < len) {
        if (text[i] == '/' && i + 1 < len && text[i + 1] == '/') {
            while (i < len && text[i] != '\n') {
                i++;
            }
            continue;
        }
        if (text[i] == '/' && i + 1 < len && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(text[i] == '*' && text[i + 1] == '/')) {
                i++;
            }
            if (i + 1 < len) {
                i += 2;
            }
            continue;
        }
        if (text[i] == '\'') {
            if (skip_php_single(text, len, &i) != 0) {
                return -1;
            }
            continue;
        }
        if (text[i] == '"') {
            if (skip_php_double(text, len, &i) != 0) {
                return -1;
            }
            continue;
        }
        if (text[i] == '<' && i + 3 <= len && memcmp(text + i, "<<<", 3) == 0) {
            PhpField ignored;
            memset(&ignored, 0, sizeof(ignored));
            if (parse_heredoc_at(text, len, &i, &ignored) == 0) {
                php_field_clear(&ignored);
                continue;
            }
        }
        if (text[i] == '$') {
            size_t next = i;
            if (try_php_assignment(text, len, &next, header) == 0) {
                i = next;
                continue;
            }
        }
        i++;
    }
    return header->vehicle_name.present ? 0 : -1;
}

static char *dup_field(const PhpField *field) {
    return strdup(field && field->decoded ? field->decoded : "");
}

static void header_from_php(const PhpHeader *php, GallerySalesHeader *out) {
    memset(out, 0, sizeof(*out));
    out->vehicle_type = dup_field(&php->vehicle_type);
    out->make_type = dup_field(&php->make_type);
    out->vehicle_name = dup_field(&php->vehicle_name);
    out->inventory_number = dup_field(&php->inventory_number);
    out->price = dup_field(&php->price);
    out->description = dup_field(&php->description);
    out->do_not_render = php->do_not_render.decoded && strcasecmp(php->do_not_render.decoded, "true") == 0;
}

static int sales_header_from_text(const char *text, GallerySalesHeader *out, char *err, size_t err_sz) {
    PhpHeader header;
    if (!out) {
        set_err(err, err_sz, "missing header");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    if (!text) {
        set_err(err, err_sz, "Could not read index.php");
        return -1;
    }
    if (scan_php_header(text, strlen(text), &header) != 0) {
        php_header_clear(&header);
        set_err(err, err_sz, "Could not parse sales page header");
        return -1;
    }
    header_from_php(&header, out);
    php_header_clear(&header);
    if (!out->vehicle_name || !out->vehicle_type || !out->make_type || !out->inventory_number || !out->price ||
        !out->description) {
        gallery_sales_header_free(out);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

int gallery_read_sales_header(const char *index_php_path, GallerySalesHeader *out, char *err, size_t err_sz) {
    size_t len = 0;
    char *text;
    int rc;
    if (!out) {
        set_err(err, err_sz, "missing header");
        return -1;
    }
    text = read_file(index_php_path, &len);
    if (!text) {
        memset(out, 0, sizeof(*out));
        set_err(err, err_sz, "Could not read index.php");
        return -1;
    }
    rc = sales_header_from_text(text, out, err, err_sz);
    free(text);
    return rc;
}

void gallery_sales_header_free(GallerySalesHeader *header) {
    if (!header) {
        return;
    }
    free(header->vehicle_type);
    free(header->make_type);
    free(header->vehicle_name);
    free(header->inventory_number);
    free(header->price);
    free(header->description);
    memset(header, 0, sizeof(*header));
}

static int edits_add(Edits *edits, size_t start, size_t end, char *text) {
    Edit *next;
    if (!text) {
        return -1;
    }
    if (edits->count == edits->cap) {
        size_t cap = edits->cap ? edits->cap * 2 : 8;
        next = (Edit *)realloc(edits->items, cap * sizeof(*next));
        if (!next) {
            free(text);
            return -1;
        }
        edits->items = next;
        edits->cap = cap;
    }
    edits->items[edits->count].start = start;
    edits->items[edits->count].end = end;
    edits->items[edits->count].text = text;
    edits->count++;
    return 0;
}

static int edit_cmp(const void *a, const void *b) {
    const Edit *ea = (const Edit *)a;
    const Edit *eb = (const Edit *)b;
    if (ea->start < eb->start) {
        return -1;
    }
    if (ea->start > eb->start) {
        return 1;
    }
    return 0;
}

static char *apply_edits(const char *text, size_t len, Edits *edits, size_t *out_len) {
    Buf buf;
    size_t pos = 0;
    size_t i;
    qsort(edits->items, edits->count, sizeof(Edit), edit_cmp);
    buf_init(&buf);
    for (i = 0; i < edits->count; i++) {
        if (edits->items[i].start < pos || edits->items[i].end < edits->items[i].start || edits->items[i].end > len) {
            buf_free(&buf);
            return NULL;
        }
        if (buf_append(&buf, text + pos, edits->items[i].start - pos) != 0 ||
            buf_puts(&buf, edits->items[i].text) != 0) {
            buf_free(&buf);
            return NULL;
        }
        pos = edits->items[i].end;
    }
    if (buf_append(&buf, text + pos, len - pos) != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (out_len) {
        *out_len = buf.len;
    }
    return buf.data ? buf.data : strdup("");
}

static void edits_free(Edits *edits) {
    size_t i;
    for (i = 0; i < edits->count; i++) {
        free(edits->items[i].text);
    }
    free(edits->items);
    edits->items = NULL;
    edits->count = 0;
    edits->cap = 0;
}

static int heredoc_is_unsafe(const char *text) {
    const char *cursor = text ? text : "";
    while (*cursor) {
        const char *line = cursor;
        size_t len = 0;
        while (line[len] && line[len] != '\n') {
            len++;
        }
        while (len && (line[0] == ' ' || line[0] == '\t')) {
            line++;
            len--;
        }
        if ((len == 4 && memcmp(line, "DESC", 4) == 0) || (len == 5 && memcmp(line, "DESC;", 5) == 0)) {
            return 1;
        }
        if (line[len] == '\0') {
            break;
        }
        cursor = line + len + 1;
    }
    return 0;
}

static int maybe_edit(Edits *edits, const char *original, size_t start, size_t end, char *replacement) {
    size_t n;
    if (!replacement) {
        return -1;
    }
    n = strlen(replacement);
    if (end >= start && end - start == n && memcmp(original + start, replacement, n) == 0) {
        free(replacement);
        return 0;
    }
    return edits_add(edits, start, end, replacement);
}

static int queue_text_field(Edits *edits, const char *original, const PhpField *field, const char *desired, int numeric_ok,
                            int force_single) {
    char *replacement;
    const char *current = field->decoded ? field->decoded : "";
    if (!field->present) {
        return -1;
    }
    if (strcmp(current, desired ? desired : "") == 0) {
        return 0;
    }
    if (field->is_heredoc) {
        if (heredoc_is_unsafe(desired)) {
            return -2;
        }
        replacement = heredoc_encode(desired, field->indent);
        return maybe_edit(edits, original, field->body_start, field->body_end, replacement);
    }
    if (numeric_ok && is_php_number(desired ? desired : "")) {
        replacement = strdup(desired);
    } else if (force_single) {
        replacement = php_quote(desired, '\'');
    } else {
        replacement = php_quote(desired, field->quote ? field->quote : '"');
    }
    return maybe_edit(edits, original, field->value_start, field->value_end, replacement);
}

char *gallery_apply_sales_header(const char *php_text, size_t len, const GallerySalesHeader *header, size_t *out_len,
                                 char *err, size_t err_sz) {
    PhpHeader parsed;
    Edits edits;
    char *rendered;
    int rc;
    const char *bool_text;

    memset(&edits, 0, sizeof(edits));
    if (!php_text || !header) {
        set_err(err, err_sz, "missing sales header");
        return NULL;
    }
    if (scan_php_header(php_text, len, &parsed) != 0) {
        php_header_clear(&parsed);
        set_err(err, err_sz, "Could not parse sales page header");
        return NULL;
    }
    rc = queue_text_field(&edits, php_text, &parsed.vehicle_type, header->vehicle_type, 0, 1);
    if (rc == 0) {
        rc = queue_text_field(&edits, php_text, &parsed.make_type, header->make_type, 0, 1);
    }
    if (rc == 0) {
        rc = queue_text_field(&edits, php_text, &parsed.vehicle_name, header->vehicle_name, 0, 1);
    }
    if (rc == 0) {
        rc = queue_text_field(&edits, php_text, &parsed.inventory_number, header->inventory_number, 1, 1);
    }
    if (rc == 0) {
        rc = queue_text_field(&edits, php_text, &parsed.price, header->price, 1, 0);
    }
    if (rc == 0) {
        bool_text = header->do_not_render ? "true" : "false";
        if (parsed.do_not_render.present && strcmp(parsed.do_not_render.decoded ? parsed.do_not_render.decoded : "", bool_text) != 0) {
            rc = maybe_edit(&edits, php_text, parsed.do_not_render.value_start, parsed.do_not_render.value_end, strdup(bool_text));
        }
    }
    if (rc == 0) {
        rc = queue_text_field(&edits, php_text, &parsed.description, header->description, 0, 0);
    }
    if (rc == -2) {
        php_header_clear(&parsed);
        edits_free(&edits);
        set_err(err, err_sz, "Description cannot contain a DESC; line because that would close the PHP heredoc.");
        return NULL;
    }
    if (rc != 0) {
        php_header_clear(&parsed);
        edits_free(&edits);
        set_err(err, err_sz, "Could not update one of the sales page variables");
        return NULL;
    }
    if (edits.count == 0) {
        php_header_clear(&parsed);
        if (out_len) {
            *out_len = len;
        }
        return strndup(php_text, len);
    }
    rendered = apply_edits(php_text, len, &edits, out_len);
    php_header_clear(&parsed);
    edits_free(&edits);
    if (!rendered) {
        set_err(err, err_sz, "Could not rewrite index.php");
    }
    return rendered;
}
#ifdef _WIN32
static int append_cmd_arg(char *dst, size_t cap, size_t *len, const char *arg) {
    size_t o = *len;
    size_t i = 0;
    int quote = arg[0] == '\0';
    if (o) {
        if (o + 1 >= cap) return -1;
        dst[o++] = ' ';
    }
    for (i = 0; arg[i] && !quote; i++) {
        if (arg[i] == ' ' || arg[i] == '\t' || arg[i] == '"') quote = 1;
    }
    if (quote) {
        if (o + 1 >= cap) return -1;
        dst[o++] = '"';
    }
    i = 0;
    while (arg[i]) {
        size_t back = 0;
        size_t k;
        while (arg[i] == '\\') {
            back++;
            i++;
        }
        if (arg[i] == '\0') {
            if (quote) back *= 2;
            if (o + back >= cap) return -1;
            for (k = 0; k < back; k++) dst[o++] = '\\';
            break;
        }
        if (arg[i] == '"') {
            back = back * 2 + 1;
            if (o + back >= cap) return -1;
            for (k = 0; k < back; k++) dst[o++] = '\\';
            dst[o++] = '"';
            i++;
            continue;
        }
        if (o + back + 1 >= cap) return -1;
        for (k = 0; k < back; k++) dst[o++] = '\\';
        dst[o++] = arg[i];
        i++;
    }
    if (quote) {
        if (o + 1 >= cap) return -1;
        dst[o++] = '"';
    }
    if (o >= cap) return -1;
    dst[o] = '\0';
    *len = o;
    return 0;
}

static int run_capture(char *const argv[], char **out, int *exit_code) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL;
    HANDLE wr = NULL;
    HANDLE nul = NULL;
    Buf buf;
    char *cmd;
    size_t cap = 32768;
    size_t len = 0;
    int i;
    DWORD code = 1;

    if (out) *out = NULL;
    if (!argv || !argv[0]) return -1;
    cmd = (char *)malloc(cap);
    if (!cmd) return -1;
    cmd[0] = '\0';
    for (i = 0; argv[i]; i++) {
        if (append_cmd_arg(cmd, cap, &len, argv[i]) != 0) {
            free(cmd);
            return -1;
        }
    }
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        free(cmd);
        return -1;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = (nul && nul != INVALID_HANDLE_VALUE) ? nul : NULL;
    si.hStdOutput = wr;
    si.hStdError = wr;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        if (nul && nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        free(cmd);
        return -1;
    }
    free(cmd);
    CloseHandle(wr);
    if (nul && nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    buf_init(&buf);
    for (;;) {
        char tmp[1024];
        DWORD got = 0;
        if (!ReadFile(rd, tmp, sizeof tmp, &got, NULL) || got == 0) break;
        if (buf.len < 1024u * 1024u) {
            size_t take = (size_t)got;
            if (buf.len + take > 1024u * 1024u) take = 1024u * 1024u - buf.len;
            if (take && buf_append(&buf, tmp, take) != 0) break;
        }
    }
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    if (!GetExitCodeProcess(pi.hProcess, &code)) code = 1;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code) *exit_code = (int)code;
    if (out) *out = buf_steal(&buf);
    else buf_free(&buf);
    return 0;
}
#else
static int run_capture(char *const argv[], char **out, int *exit_code) {
    int fds[2];
    pid_t pid;
    Buf buf;
    int status = 0;

    if (out) {
        *out = NULL;
    }
    if (pipe(fds) != 0) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        if (fds[1] > 2) {
            close(fds[1]);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    buf_init(&buf);
    for (;;) {
        char tmp[1024];
        ssize_t got = read(fds[0], tmp, sizeof(tmp));
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        if (got == 0) {
            break;
        }
        if (buf.len < 1024 * 1024) {
            size_t take = (size_t)got;
            if (buf.len + take > 1024 * 1024) {
                take = 1024 * 1024 - buf.len;
            }
            if (buf_append(&buf, tmp, take) != 0) {
                break;
            }
        }
    }
    close(fds[0]);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            status = -1;
            break;
        }
    }
    if (exit_code) {
        if (status >= 0 && WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        } else {
            *exit_code = -1;
        }
    }
    if (out) {
        *out = buf_steal(&buf);
    } else {
        buf_free(&buf);
    }
    return 0;
}
#endif

static char *git_unquote(const char *text, size_t len) {
    Buf buf;
    size_t i = 0;
    size_t end = len;
    if (len >= 2 && text[0] == '"') {
        i = 1;
        if (text[len - 1] == '"') {
            end = len - 1;
        }
    }
    buf_init(&buf);
    while (i < end) {
        if (text[i] == '\\' && i + 1 < end) {
            unsigned char esc = (unsigned char)text[i + 1];
            if (esc >= '0' && esc <= '7') {
                int value = esc - '0';
                size_t k;
                i += 2;
                for (k = 0; k < 2 && i < end && text[i] >= '0' && text[i] <= '7'; k++, i++) {
                    value = (value << 3) | (text[i] - '0');
                }
                if (buf_putc(&buf, (char)value) != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                continue;
            }
            if (esc == 'n') {
                esc = '\n';
            } else if (esc == 't') {
                esc = '\t';
            }
            if (buf_putc(&buf, (char)esc) != 0) {
                buf_free(&buf);
                return NULL;
            }
            i += 2;
            continue;
        }
        if (buf_putc(&buf, text[i]) != 0) {
            buf_free(&buf);
            return NULL;
        }
        i++;
    }
    return buf_steal(&buf);
}

static int path_touches(const char *path, const char *rel) {
    size_t n;
    if (!path || !rel) {
        return 0;
    }
    n = strlen(rel);
    if (strcmp(path, rel) == 0) {
        return 1;
    }
    return strncmp(path, rel, n) == 0 && path[n] == '/';
}

static int porcelain_touches(const char *text, const char *rel) {
    const char *line = text ? text : "";
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len && line[len - 1] == '\r') {
            len--;
        }
        if (len >= 4 && line[2] == ' ') {
            const char *path = line + 3;
            size_t path_len = len - 3;
            const char *arrow = NULL;
            size_t i;
            for (i = 0; i + 4 <= path_len; i++) {
                if (memcmp(path + i, " -> ", 4) == 0) {
                    arrow = path + i;
                    break;
                }
            }
            if (arrow) {
                char *left = git_unquote(path, (size_t)(arrow - path));
                char *right = git_unquote(arrow + 4, path_len - (size_t)(arrow + 4 - path));
                int hit = path_touches(left, rel) || path_touches(right, rel);
                free(left);
                free(right);
                if (hit) {
                    return 1;
                }
            } else {
                char *one = git_unquote(path, path_len);
                int hit = path_touches(one, rel);
                free(one);
                if (hit) {
                    return 1;
                }
            }
        }
        if (!nl) {
            break;
        }
        line = nl + 1;
    }
    return 0;
}

static char *git_status_text(const char *repo, const char *pathspec) {
    char *argv[7];
    char *out = NULL;
    int code = 1;
    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo;
    argv[3] = "status";
    argv[4] = "--porcelain";
    argv[5] = "--";
    argv[6] = (char *)pathspec;
    /* execvp needs a NULL terminator. Rebuild with the terminator. */
    {
        char *full[8];
        full[0] = argv[0];
        full[1] = argv[1];
        full[2] = argv[2];
        full[3] = argv[3];
        full[4] = argv[4];
        full[5] = argv[5];
        full[6] = argv[6];
        full[7] = NULL;
        if (run_capture(full, &out, &code) != 0 || code != 0) {
            free(out);
            return strdup("");
        }
    }
    return out ? out : strdup("");
}

static char *git_head_text(const char *repo, const char *public_path) {
    const char *rel = public_path;
    char *git_dir;
    struct stat st;
    char *spec;
    size_t len;
    char *out = NULL;
    int code = 1;
    if (!repo || !repo[0] || !rel) {
        return NULL;
    }
    while (*rel == '/') {
        rel++;
    }
    if (!*rel) {
        return NULL;
    }
    git_dir = join_path(repo, ".git");
    if (!git_dir || stat(git_dir, &st) != 0) {
        free(git_dir);
        return NULL;
    }
    free(git_dir);
    len = strlen(rel) + 6;
    spec = (char *)malloc(len);
    if (!spec) {
        return NULL;
    }
    snprintf(spec, len, "HEAD:%s", rel);
    {
        char *full[6];
        full[0] = "git";
        full[1] = "-C";
        full[2] = (char *)repo;
        full[3] = "show";
        full[4] = spec;
        full[5] = NULL;
        if (run_capture(full, &out, &code) != 0 || code != 0) {
            free(out);
            free(spec);
            return NULL;
        }
    }
    free(spec);
    return out;
}

static int list_names(const char *dir, int want_dirs, int images_only, StrList *out) {
    DIR *handle = opendir(dir);
    struct dirent *ent;
    if (!handle) {
        return -1;
    }
    while ((ent = readdir(handle)) != NULL) {
        char *path;
        struct stat st;
        if (ent->d_name[0] == '.') {
            continue;
        }
        path = join_path(dir, ent->d_name);
        if (!path || stat(path, &st) != 0) {
            free(path);
            continue;
        }
        if (want_dirs) {
            if (S_ISDIR(st.st_mode)) {
                sl_add(out, ent->d_name);
            }
        } else if (S_ISREG(st.st_mode) && (!images_only || is_image_name(ent->d_name))) {
            sl_add(out, ent->d_name);
        }
        free(path);
    }
    closedir(handle);
    sl_sort(out);
    return 0;
}

static int is_page_file(const char *name) {
    return ends_with_ci(name, ".html") || ends_with_ci(name, ".htm") || ends_with_ci(name, ".shtml") ||
           ends_with_ci(name, ".php");
}

static int index_rank(const char *name) {
    if (strcmp(name, "index.php") == 0) {
        return 0;
    }
    if (strcmp(name, "index.html") == 0) {
        return 1;
    }
    if (strcmp(name, "index.shtml") == 0) {
        return 2;
    }
    if (strcmp(name, "index.htm") == 0) {
        return 3;
    }
    return 100;
}

static char *pick_primary(const char *folder, const char *slug) {
    StrList pages;
    size_t i;
    char *values_name = NULL;
    int values_rank = 1000;
    char *index_name = NULL;
    int best_index = 1000;
    char *named = NULL;
    char *plain = NULL;
    sl_init(&pages);
    if (list_names(folder, 0, 0, &pages) != 0) {
        sl_free(&pages);
        return NULL;
    }
    for (i = 0; i < pages.count; i++) {
        char *path;
        char *text;
        size_t len = 0;
        int rank;
        if (!is_page_file(pages.items[i])) {
            continue;
        }
        if (!plain && !strstr(pages.items[i], ".mailchimp.")) {
            plain = pages.items[i];
        }
        rank = index_rank(pages.items[i]);
        if (rank < best_index) {
            best_index = rank;
            index_name = pages.items[i];
        }
        path = join_path(folder, pages.items[i]);
        text = path ? read_file(path, &len) : NULL;
        free(path);
        if (text && strstr(text, "page-values")) {
            int score = rank;
            if (score == 100) {
                score = 50;
            }
            if (!values_name || score < values_rank) {
                values_name = pages.items[i];
                values_rank = score;
            }
        }
        if (!named) {
            char probe[1024];
            snprintf(probe, sizeof(probe), "%s.html", slug);
            if (strcmp(pages.items[i], probe) == 0) {
                named = pages.items[i];
            }
            snprintf(probe, sizeof(probe), "%s.shtml", slug);
            if (strcmp(pages.items[i], probe) == 0) {
                named = pages.items[i];
            }
            snprintf(probe, sizeof(probe), "%s.htm", slug);
            if (strcmp(pages.items[i], probe) == 0) {
                named = pages.items[i];
            }
            snprintf(probe, sizeof(probe), "%s.php", slug);
            if (strcmp(pages.items[i], probe) == 0) {
                named = pages.items[i];
            }
        }
        free(text);
    }
    {
        const char *chosen = values_name ? values_name : (best_index < 100 ? index_name : (named ? named : plain));
        char *result = chosen ? strdup(chosen) : NULL;
        sl_free(&pages);
        return result;
    }
}

static char *url_join_public(const char *section, const char *slug, const char *rest) {
    Buf buf;
    char *encoded_slug = url_encode(slug ? slug : "");
    const char *cursor;
    buf_init(&buf);
    if (!encoded_slug || buf_putc(&buf, '/') != 0 || buf_puts(&buf, section ? section : "") != 0 || buf_putc(&buf, '/') != 0 ||
        buf_puts(&buf, encoded_slug) != 0) {
        free(encoded_slug);
        buf_free(&buf);
        return NULL;
    }
    free(encoded_slug);
    cursor = rest;
    while (cursor && *cursor) {
        const char *slash = strchr(cursor, '/');
        size_t n = slash ? (size_t)(slash - cursor) : strlen(cursor);
        char *segment = strndup(cursor, n);
        char *encoded;
        if (!segment) {
            buf_free(&buf);
            return NULL;
        }
        encoded = url_encode(segment);
        free(segment);
        if (!encoded || buf_putc(&buf, '/') != 0 || buf_puts(&buf, encoded) != 0) {
            free(encoded);
            buf_free(&buf);
            return NULL;
        }
        free(encoded);
        if (!slash) {
            break;
        }
        cursor = slash + 1;
    }
    return buf_steal(&buf);
}

static int find_html_comment(const char *text, size_t len, const char *word, size_t *start, size_t *end, size_t *body_start,
                             size_t *body_end) {
    size_t i;
    size_t word_len = strlen(word);
    for (i = 0; i + 4 < len; i++) {
        size_t j;
        if (memcmp(text + i, "<!--", 4) != 0) {
            continue;
        }
        j = i + 4;
        while (j < len && isspace((unsigned char)text[j])) {
            j++;
        }
        if (j + word_len <= len && memcmp(text + j, word, word_len) == 0) {
            unsigned char next = (j + word_len < len) ? (unsigned char)text[j + word_len] : 0;
            size_t k;
            if (isalnum(next) || next == '_' || next == '-') {
                continue;
            }
            for (k = j + word_len; k + 3 <= len; k++) {
                if (text[k] == '-' && text[k + 1] == '-' && text[k + 2] == '>') {
                    *start = i;
                    *end = k + 3;
                    *body_start = j + word_len;
                    *body_end = k;
                    return 0;
                }
            }
        }
    }
    return -1;
}

static char *comment_indent(const char *text, size_t start) {
    size_t line = start;
    size_t i;
    while (line > 0 && text[line - 1] != '\n') {
        line--;
    }
    i = line;
    while (i < start && (text[i] == ' ' || text[i] == '\t')) {
        i++;
    }
    if (i != start) {
        return strdup("");
    }
    return strndup(text + line, start - line);
}

static void assign_kv(char **slot, const char *value) {
    free(*slot);
    *slot = strdup(value ? value : "");
}

static char *trim_dup(const char *text, size_t len) {
    while (len && isspace((unsigned char)text[0])) {
        text++;
        len--;
    }
    while (len && isspace((unsigned char)text[len - 1])) {
        len--;
    }
    return strndup(text, len);
}

static void parse_comment_fields(const char *body, size_t len, char **title, char **description, char **image, char **type,
                                 char **date, char **vehicle_type, char **make_type, char **vehicle_name, char **override_ids,
                                 int related) {
    size_t i = 0;
    while (i < len) {
        size_t line = i;
        size_t line_len;
        char *raw;
        char *colon;
        char *key;
        char *value;
        while (i < len && body[i] != '\n') {
            i++;
        }
        line_len = i - line;
        if (line_len && body[line + line_len - 1] == '\r') {
            line_len--;
        }
        raw = trim_dup(body + line, line_len);
        if (raw && raw[0] && (colon = strchr(raw, ':')) != NULL) {
            *colon = '\0';
            key = raw;
            value = trim_dup(colon + 1, strlen(colon + 1));
            if (!related) {
                if (strcasecmp(key, "title") == 0) {
                    assign_kv(title, value);
                } else if (strcasecmp(key, "description") == 0) {
                    assign_kv(description, value);
                } else if (strcasecmp(key, "image") == 0) {
                    assign_kv(image, value);
                } else if (strcasecmp(key, "type") == 0) {
                    assign_kv(type, value);
                } else if (strcasecmp(key, "date") == 0) {
                    assign_kv(date, value);
                }
            } else if (strcasecmp(key, "vehicleType") == 0) {
                assign_kv(vehicle_type, value);
            } else if (strcasecmp(key, "makeType") == 0) {
                assign_kv(make_type, value);
            } else if (strcasecmp(key, "vehicleName") == 0) {
                assign_kv(vehicle_name, value);
            } else if (strcasecmp(key, "Override IDs") == 0 || strcasecmp(key, "override") == 0 ||
                       strcasecmp(key, "overrideIds") == 0) {
                assign_kv(override_ids, value);
            }
            free(value);
        }
        free(raw);
        if (i < len && body[i] == '\n') {
            i++;
        }
    }
}

static int field_has_break(const char *text) {
    const unsigned char *p;
    if (!text) {
        return 0;
    }
    for (p = (const unsigned char *)text; *p; p++) {
        if (*p < 0x20 || *p == 0x7F) {
            return 1;
        }
    }
    return strstr(text, "-->") != NULL;
}

static char *build_page_comment(const char *indent, const char *title, const char *description, const char *image,
                                const char *type, const char *date) {
    Buf buf;
    buf_init(&buf);
    if (buf_puts(&buf, "<!-- page-values\n") != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (buf_printf(&buf, "%stitle: %s\n", indent, title ? title : "") != 0 ||
        buf_printf(&buf, "%sdescription: %s\n", indent, description ? description : "") != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (image && image[0] && buf_printf(&buf, "%simage: %s\n", indent, image) != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (type && type[0] && buf_printf(&buf, "%stype: %s\n", indent, type) != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (date && date[0] && buf_printf(&buf, "%sdate: %s\n", indent, date) != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (buf_printf(&buf, "%s-->", indent) != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static char *build_related_comment(const char *indent, const char *vehicle_type, const char *make_type, const char *vehicle_name,
                                   const char *override_ids) {
    Buf buf;
    const char *pairs[][2] = {{"vehicleType", vehicle_type}, {"makeType", make_type}, {"vehicleName", vehicle_name},
                              {"Override IDs", override_ids}};
    size_t i;
    buf_init(&buf);
    if (buf_puts(&buf, "<!-- related-sales\n") != 0) {
        buf_free(&buf);
        return NULL;
    }
    for (i = 0; i < 4; i++) {
        const char *value = pairs[i][1] ? pairs[i][1] : "";
        if (value[0]) {
            if (buf_printf(&buf, "%s%s: %s\n", indent, pairs[i][0], value) != 0) {
                buf_free(&buf);
                return NULL;
            }
        } else if (buf_printf(&buf, "%s%s:\n", indent, pairs[i][0]) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    if (buf_printf(&buf, "%s-->", indent) != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static char *replace_span(const char *text, size_t len, size_t start, size_t end, const char *insert) {
    Buf buf;
    buf_init(&buf);
    if (start > len || end > len || end < start) {
        buf_free(&buf);
        return NULL;
    }
    if (buf_append(&buf, text, start) != 0 || buf_puts(&buf, insert ? insert : "") != 0 ||
        buf_append(&buf, text + end, len - end) != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static int valid_page_type(const char *type) {
    return !type || !type[0] || strcmp(type, "Article") == 0 || strcmp(type, "NewsArticle") == 0 ||
           strcmp(type, "CollectionPage") == 0;
}

static int valid_page_date(const char *date) {
    size_t i;
    if (!date || !date[0]) {
        return 1;
    }
    if (strlen(date) != 10 || date[4] != '-' || date[7] != '-') {
        return 0;
    }
    for (i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (!isdigit((unsigned char)date[i])) {
            return 0;
        }
    }
    return 1;
}

static char *strip_shy(const char *text) {
    Buf buf;
    const char *p = text ? text : "";
    buf_init(&buf);
    while (*p) {
        if ((unsigned char)p[0] == 0xC2 && (unsigned char)p[1] == 0xAD) {
            p += 2;
            continue;
        }
        if (strncasecmp(p, "&shy;", 5) == 0) {
            p += 5;
            continue;
        }
        if (strncasecmp(p, "&#173;", 6) == 0 || strncasecmp(p, "&#xad;", 6) == 0) {
            p += 6;
            continue;
        }
        if (buf_putc(&buf, *p) != 0) {
            buf_free(&buf);
            return NULL;
        }
        p++;
    }
    return buf_steal(&buf);
}

static char *find_ci(const char *haystack, const char *needle) {
    size_t needle_len = strlen(needle);
    if (needle_len == 0) {
        return (char *)haystack;
    }
    for (; haystack && *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
    }
    return NULL;
}

static char *remove_ul_blocks(const char *html) {
    Buf buf;
    const char *p = html ? html : "";
    buf_init(&buf);
    while (*p) {
        if ((p[0] == '<') && strncasecmp(p, "<ul", 3) == 0 &&
            (p[3] == '>' || p[3] == ' ' || p[3] == '\t' || p[3] == '\n' || p[3] == '\r')) {
            const char *close = find_ci(p, "</ul");
            if (!close) {
                if (buf_putc(&buf, *p) != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                p++;
                continue;
            }
            close += 4;
            while (*close && *close != '>') {
                close++;
            }
            if (*close == '>') {
                close++;
            }
            p = close;
            if (buf_puts(&buf, "\n\n") != 0) {
                buf_free(&buf);
                return NULL;
            }
            continue;
        }
        if (buf_putc(&buf, *p) != 0) {
            buf_free(&buf);
            return NULL;
        }
        p++;
    }
    return buf_steal(&buf);
}

static char *collapse_ws(const char *text) {
    Buf buf;
    const char *p = text ? text : "";
    int pending = 0;
    int started = 0;
    buf_init(&buf);
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    for (; *p; p++) {
        if (isspace((unsigned char)*p)) {
            pending = 1;
            continue;
        }
        if (pending && started && buf_putc(&buf, ' ') != 0) {
            buf_free(&buf);
            return NULL;
        }
        pending = 0;
        started = 1;
        if (buf_putc(&buf, *p) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    return buf_steal(&buf);
}

static char *plain_html(const char *html) {
    char *without_ul = remove_ul_blocks(html);
    char *stripped;
    Buf decoded;
    char *collapsed;
    char *shy;
    if (!without_ul) {
        return strdup("");
    }
    stripped = strip_tags(without_ul, strlen(without_ul));
    free(without_ul);
    if (!stripped) {
        return strdup("");
    }
    buf_init(&decoded);
    if (decode_entities(&decoded, stripped, strlen(stripped)) != 0) {
        free(stripped);
        buf_free(&decoded);
        return strdup("");
    }
    free(stripped);
    collapsed = collapse_ws(decoded.data ? decoded.data : "");
    buf_free(&decoded);
    shy = strip_shy(collapsed ? collapsed : "");
    free(collapsed);
    return shy ? shy : strdup("");
}

static char *truncate_meta(const char *text) {
    size_t len = text ? strlen(text) : 0;
    size_t bytes = 0;
    size_t chars = 0;
    size_t last_space = (size_t)-1;
    Buf buf;
    if (!text) {
        return strdup("");
    }
    if (len <= 155) {
        return strdup(text);
    }
    while (bytes < len && chars < 155) {
        unsigned cp = 0;
        size_t adv = 1;
        if ((unsigned char)text[bytes] < 0x80) {
            cp = (unsigned char)text[bytes];
        } else if (utf8_decode((const unsigned char *)text + bytes, len - bytes, &cp, &adv) != 0) {
            adv = 1;
        }
        if (cp == ' ') {
            last_space = bytes;
        }
        bytes += adv;
        chars++;
    }
    if (last_space != (size_t)-1 && last_space > 80 && last_space < bytes) {
        bytes = last_space;
    }
    while (bytes > 0 && strchr(" \t.,;:-", text[bytes - 1])) {
        bytes--;
    }
    buf_init(&buf);
    if (buf_append(&buf, text, bytes) != 0 || buf_puts(&buf, "...") != 0) {
        buf_free(&buf);
        return strdup("");
    }
    return buf_steal(&buf);
}

static int contains_for_sale(const char *text) {
    const char *p = text ? text : "";
    for (; *p; p++) {
        if (strncasecmp(p, "for", 3) == 0 && (p == text || !isalnum((unsigned char)p[-1]))) {
            const char *q = p + 3;
            while (*q == ' ' || *q == '\t') {
                q++;
            }
            if (strncasecmp(q, "sale", 4) == 0 && !isalnum((unsigned char)q[4])) {
                return 1;
            }
        }
    }
    return 0;
}

static char *sales_share_title(const char *vehicle, int sold) {
    char *name = strip_shy(vehicle ? vehicle : "");
    char *trimmed;
    Buf buf;
    if (!name) {
        return strdup(BRAND_NAME);
    }
    trimmed = collapse_ws(name);
    free(name);
    buf_init(&buf);
    if (trimmed && trimmed[0]) {
        if (buf_puts(&buf, trimmed) != 0) {
            free(trimmed);
            buf_free(&buf);
            return strdup(BRAND_NAME);
        }
        if (!sold && !contains_for_sale(trimmed) && buf_puts(&buf, " for Sale") != 0) {
            free(trimmed);
            buf_free(&buf);
            return strdup(BRAND_NAME);
        }
        if (buf_puts(&buf, " | ") != 0) {
            free(trimmed);
            buf_free(&buf);
            return strdup(BRAND_NAME);
        }
    }
    free(trimmed);
    if (buf_puts(&buf, BRAND_NAME) != 0) {
        buf_free(&buf);
        return strdup(BRAND_NAME);
    }
    return buf_steal(&buf);
}

static char *article_share_title(const char *title) {
    const char *suffix = " | " BRAND_NAME;
    Buf buf;
    if (!title || !title[0]) {
        return strdup(BRAND_NAME);
    }
    if (strcmp(title, BRAND_NAME) == 0 || (strlen(title) >= strlen(suffix) && strcmp(title + strlen(title) - strlen(suffix), suffix) == 0)) {
        return strdup(title);
    }
    buf_init(&buf);
    if (buf_puts(&buf, title) != 0 || buf_puts(&buf, suffix) != 0) {
        buf_free(&buf);
        return strdup(title);
    }
    return buf_steal(&buf);
}

static int json_boolish(const JsonValue *value, int *out) {
    const char *text;
    if (!value || !out) {
        return -1;
    }
    if (json_type(value) == JSON_BOOL) {
        *out = json_bool_value(value);
        return 0;
    }
    if (json_type(value) == JSON_NUMBER) {
        long long number = 0;
        if (json_as_int(value, &number) != 0) {
            return -1;
        }
        *out = number != 0;
        return 0;
    }
    if (json_type(value) == JSON_STRING) {
        text = json_string_get(value, NULL);
        if (!text) {
            return -1;
        }
        if (strcasecmp(text, "true") == 0 || strcmp(text, "1") == 0) {
            *out = 1;
            return 0;
        }
        if (strcasecmp(text, "false") == 0 || strcmp(text, "0") == 0 || text[0] == '\0') {
            *out = 0;
            return 0;
        }
    }
    return -1;
}

static char *json_text_dup(const JsonValue *value) {
    char *text = json_as_text(value);
    if (!text) {
        return strdup("");
    }
    return text;
}

static int caption_is_real(const char *caption, const char *vehicle, const char *image_name) {
    char *base;
    const char *text = caption ? caption : "";
    if (!text[0]) {
        return 0;
    }
    if (vehicle && vehicle[0] && strcasecmp(text, vehicle) == 0) {
        return 0;
    }
    if (strncasecmp(text, "photo", 5) == 0) {
        const char *p = text + 5;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (isdigit((unsigned char)*p)) {
            while (isdigit((unsigned char)*p)) {
                p++;
            }
            if (*p == '\0') {
                return 0;
            }
        }
    }
    if (vehicle && vehicle[0] && strncasecmp(text, vehicle, strlen(vehicle)) == 0) {
        const char *p = text + strlen(vehicle);
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (*p == '-' || *p == ':') {
            p++;
        } else if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 &&
                   ((unsigned char)p[2] == 0x93 || (unsigned char)p[2] == 0x94)) {
            p += 3;
        } else {
            p = NULL;
        }
        if (p) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (strncasecmp(p, "photo", 5) == 0) {
                const char *q = p + 5;
                while (*q == ' ' || *q == '\t') {
                    q++;
                }
                if (isdigit((unsigned char)*q)) {
                    while (isdigit((unsigned char)*q)) {
                        q++;
                    }
                    if (*q == '\0') {
                        return 0;
                    }
                }
            }
        }
    }
    if (image_name && image_name[0] && strcasecmp(text, image_name) == 0) {
        return 0;
    }
    base = NULL;
    if (image_name && image_name[0]) {
        const char *dot = strrchr(image_name, '.');
        base = dot && dot != image_name ? strndup(image_name, (size_t)(dot - image_name)) : strdup(image_name);
        if (base && strcasecmp(text, base) == 0) {
            free(base);
            return 0;
        }
    }
    free(base);
    if (ends_with_ci(text, ".jpg") || ends_with_ci(text, ".jpeg") || ends_with_ci(text, ".png") || ends_with_ci(text, ".gif") ||
        ends_with_ci(text, ".webp") || ends_with_ci(text, ".bmp") || ends_with_ci(text, ".tif") || ends_with_ci(text, ".tiff")) {
        return 0;
    }
    return 1;
}

static void jput_str(JsonValue *object, const char *key, const char *value) {
    json_object_put(object, key, json_string_cstr(value ? value : ""));
}

static void jput_bool(JsonValue *object, const char *key, int value) {
    json_object_put(object, key, json_bool(value));
}

static void jput_int(JsonValue *object, const char *key, long long value) {
    json_object_put(object, key, json_number_int(value));
}

static void jwarn(JsonValue *warnings, const char *text) {
    if (warnings) {
        json_array_add(warnings, json_string_cstr(text));
    }
}

static long long json_ll(const JsonValue *value) {
    long long number = 0;
    if (json_as_int(value, &number) != 0) {
        return 0;
    }
    return number;
}

typedef struct GImage {
    char *name;
    char *caption;
    int width;
    int height;
    int thumb_width;
    int thumb_height;
    long long file_size;
    int has_slide;
    int has_thumb;
    int is_bullet;
} GImage;

static void free_gimages(GImage *images, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        free(images[i].name);
        free(images[i].caption);
    }
    free(images);
}

static int load_deep_images(const char *folder, JsonDoc **doc_out, GImage **images_out, size_t *count_out, size_t *captions_out,
                            char *err, size_t err_sz) {
    char *path = join_path(folder, "deep-data.json");
    size_t len = 0;
    char *text;
    JsonDoc *doc;
    JsonValue *objects;
    size_t count;
    size_t i;
    size_t captions = 0;
    GImage *images;
    if (doc_out) {
        *doc_out = NULL;
    }
    *images_out = NULL;
    *count_out = 0;
    if (captions_out) {
        *captions_out = 0;
    }
    if (!path) {
        return -1;
    }
    text = read_file(path, &len);
    free(path);
    if (!text) {
        return 1;
    }
    doc = json_parse(text, len, err, err_sz);
    free(text);
    if (!doc) {
        return -1;
    }
    objects = json_object_get(doc->root, "objects");
    count = json_array_len(objects);
    images = (GImage *)calloc(count ? count : 1, sizeof(*images));
    if (!images) {
        json_doc_free(doc);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    for (i = 0; i < count; i++) {
        JsonValue *object = json_array_at(objects, i);
        JsonValue *image = json_object_get(object, "image");
        JsonValue *thumb = json_object_get(object, "thumb");
        JsonValue *caption_html = json_object_get(object, "imageCaption");
        char *slide;
        char *thumb_path;
        images[i].name = object_filename(object);
        if (caption_html && json_type(caption_html) == JSON_STRING) {
            size_t html_len = 0;
            const char *html = json_string_get(caption_html, &html_len);
            images[i].caption = gallery_caption_from_html(html, html_len);
        } else {
            images[i].caption = strdup("");
        }
        if (!images[i].name) {
            images[i].name = strdup("");
        }
        if (!images[i].caption) {
            images[i].caption = strdup("");
        }
        if (images[i].caption[0]) {
            captions++;
        }
        images[i].width = (int)json_ll(json_object_get(image, "width"));
        images[i].height = (int)json_ll(json_object_get(image, "height"));
        images[i].thumb_width = (int)json_ll(json_object_get(thumb, "width"));
        images[i].thumb_height = (int)json_ll(json_object_get(thumb, "height"));
        images[i].file_size = json_ll(json_object_get(object, "fileSize"));
        slide = join_path3(folder, "slides", images[i].name);
        thumb_path = join_path3(folder, "thumbs", images[i].name);
        images[i].has_slide = file_exists(slide);
        images[i].has_thumb = file_exists(thumb_path);
        free(slide);
        free(thumb_path);
    }
    if (doc_out) {
        *doc_out = doc;
    } else {
        json_doc_free(doc);
    }
    *images_out = images;
    *count_out = count;
    if (captions_out) {
        *captions_out = captions;
    }
    return 0;
}

static void mark_bullets(GImage *images, size_t count, const char *vehicle) {
    char *seen[4] = {NULL, NULL, NULL, NULL};
    int nseen = 0;
    size_t i;
    for (i = 1; i < count && nseen < 4; i++) {
        char *key;
        int s;
        int duplicate = 0;
        char *lower;
        if (!caption_is_real(images[i].caption, vehicle, images[i].name)) {
            continue;
        }
        lower = strdup(images[i].caption ? images[i].caption : "");
        if (!lower) {
            break;
        }
        for (key = lower; *key; key++) {
            if (*key >= 'A' && *key <= 'Z') {
                *key = (char)(*key - 'A' + 'a');
            }
        }
        for (s = 0; s < nseen; s++) {
            if (strcmp(seen[s], lower) == 0) {
                duplicate = 1;
            }
        }
        if (duplicate) {
            free(lower);
            continue;
        }
        images[i].is_bullet = 1;
        seen[nseen++] = lower;
    }
    for (i = 0; i < 4; i++) {
        free(seen[i]);
    }
}

static char *slide_name_n(const char *raw, size_t len, int lower) {
    char *decoded;
    char *result;
    const char *body;
    const char *base;
    size_t i;
    size_t n;
    if (!raw) {
        return NULL;
    }
    while (len && isspace((unsigned char)raw[0])) {
        raw++;
        len--;
    }
    while (len && isspace((unsigned char)raw[len - 1])) {
        len--;
    }
    for (i = 0; i < len; i++) {
        if (raw[i] == '?' || raw[i] == '#') {
            len = i;
            break;
        }
    }
    if (!len) {
        return NULL;
    }
    decoded = url_decode(raw, len);
    if (!decoded) {
        return NULL;
    }
    for (i = 0; decoded[i]; i++) {
        if (decoded[i] == '+') {
            decoded[i] = ' ';
        }
    }
    body = decoded;
    for (i = 0; decoded[i]; i++) {
        if ((i == 0 || (!isalnum((unsigned char)decoded[i - 1]) && decoded[i - 1] != '_' && decoded[i - 1] != '-')) &&
            (strncasecmp(decoded + i, "slides/", 7) == 0 || strncasecmp(decoded + i, "thumbs/", 7) == 0)) {
            body = decoded + i + 7;
            break;
        }
    }
    base = body;
    for (i = 0; body[i]; i++) {
        if (body[i] == '/' || body[i] == '\\') {
            base = body + i + 1;
        }
    }
    while (*base == ' ' || *base == '\t') {
        base++;
    }
    n = strlen(base);
    while (n && isspace((unsigned char)base[n - 1])) {
        n--;
    }
    result = strndup(base, n);
    free(decoded);
    if (!result) {
        return NULL;
    }
    if (lower) {
        for (i = 0; result[i]; i++) {
            if (result[i] >= 'A' && result[i] <= 'Z') {
                result[i] = (char)(result[i] - 'A' + 'a');
            }
        }
    }
    if (!result[0] || !is_image_name(result)) {
        free(result);
        return NULL;
    }
    return result;
}

static char *slide_key_cstr(const char *text) {
    return text ? slide_name_n(text, strlen(text), 1) : NULL;
}

static int name_is_documented(const char *filename, const StrList *documented) {
    char *key = slide_key_cstr(filename);
    int found = key && documented && sl_has(documented, key);
    free(key);
    return found;
}

static char *html_decode_basic(const char *text, size_t len) {
    Buf buf;
    size_t i;
    if (!text) {
        return NULL;
    }
    buf_init(&buf);
    for (i = 0; i < len; i++) {
        char ch = text[i];
        size_t skip = 0;
        if (text[i] == '&') {
            if (i + 5 <= len && strncasecmp(text + i, "&amp;", 5) == 0) {
                ch = '&';
                skip = 4;
            } else if (i + 6 <= len && strncasecmp(text + i, "&quot;", 6) == 0) {
                ch = '"';
                skip = 5;
            } else if (i + 6 <= len && strncasecmp(text + i, "&apos;", 6) == 0) {
                ch = '\'';
                skip = 5;
            } else if (i + 4 <= len && strncasecmp(text + i, "&lt;", 4) == 0) {
                ch = '<';
                skip = 3;
            } else if (i + 4 <= len && strncasecmp(text + i, "&gt;", 4) == 0) {
                ch = '>';
                skip = 3;
            } else if (i + 5 <= len && strncasecmp(text + i, "&#39;", 5) == 0) {
                ch = '\'';
                skip = 4;
            } else if (i + 5 <= len && strncasecmp(text + i, "&#34;", 5) == 0) {
                ch = '"';
                skip = 4;
            }
        }
        if (buf_putc(&buf, ch) != 0) {
            buf_free(&buf);
            return NULL;
        }
        i += skip;
    }
    return buf_steal(&buf);
}

static char *html_plain(const char *text, size_t len) {
    Buf buf;
    size_t i;
    char *raw;
    char *decoded;
    char *collapsed;
    buf_init(&buf);
    for (i = 0; i < len; i++) {
        if (text[i] == '<') {
            int br = i + 3 <= len && strncasecmp(text + i, "<br", 3) == 0;
            while (i < len && text[i] != '>') {
                i++;
            }
            if (br && buf_putc(&buf, ' ') != 0) {
                buf_free(&buf);
                return NULL;
            }
            continue;
        }
        if (buf_putc(&buf, text[i]) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    raw = buf_steal(&buf);
    decoded = raw ? html_decode_basic(raw, strlen(raw)) : NULL;
    collapsed = decoded ? collapse_ws(decoded) : NULL;
    free(raw);
    free(decoded);
    return collapsed ? collapsed : strdup("");
}

static int attr_at(const char *tag, const char *p, const char *end, const char *name) {
    size_t n = strlen(name);
    unsigned char prev;
    unsigned char next;
    if ((size_t)(end - p) < n || strncasecmp(p, name, n) != 0) {
        return 0;
    }
    if (p > tag) {
        prev = (unsigned char)p[-1];
        if (isalnum(prev) || prev == '-' || prev == '_' || prev == ':') {
            return 0;
        }
    }
    if (p + n < end) {
        next = (unsigned char)p[n];
        if (isalnum(next) || next == '-' || next == '_' || next == ':') {
            return 0;
        }
    }
    return 1;
}

static const char *skip_attr_space(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static char *html_attr(const char *tag, size_t tag_len, const char *name) {
    const char *p = tag;
    const char *end = tag + tag_len;
    if (!tag || !name) {
        return NULL;
    }
    while (p < end) {
        if (attr_at(tag, p, end, name)) {
            const char *q = skip_attr_space(p + strlen(name), end);
            if (q < end && *q == '=') {
                q = skip_attr_space(q + 1, end);
                if (q < end && (*q == '"' || *q == '\'')) {
                    char quote = *q++;
                    const char *start = q;
                    while (q < end && *q != quote) {
                        q++;
                    }
                    return html_decode_basic(start, (size_t)(q - start));
                }
            }
        }
        p++;
    }
    return NULL;
}

static int class_has(const char *tag, size_t tag_len, const char *token) {
    char *cls = html_attr(tag, tag_len, "class");
    char *p;
    size_t n;
    int found = 0;
    if (!cls || !token) {
        free(cls);
        return 0;
    }
    n = strlen(token);
    p = cls;
    while (*p) {
        size_t len = 0;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (!*p) {
            break;
        }
        while (p[len] && !isspace((unsigned char)p[len])) {
            len++;
        }
        if (len == n && strncasecmp(p, token, n) == 0) {
            found = 1;
            break;
        }
        p += len;
    }
    free(cls);
    return found;
}

static const char *html_tag_close(const char *tag) {
    const char *p;
    char quote = 0;
    if (!tag || *tag != '<') {
        return NULL;
    }
    for (p = tag + 1; *p; p++) {
        if (quote) {
            if (*p == quote) {
                quote = 0;
            }
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
            continue;
        }
        if (*p == '>') {
            return p;
        }
    }
    return NULL;
}

static int tag_open(const char *p, const char *name) {
    size_t n;
    unsigned char next;
    if (!p || *p != '<' || !name) {
        return 0;
    }
    n = strlen(name);
    if (strncasecmp(p + 1, name, n) != 0) {
        return 0;
    }
    next = (unsigned char)p[1 + n];
    return next == '>' || next == '/' || isspace(next);
}

static char *first_attr_in(const char *html, size_t len, const char *tag_name, const char *attr) {
    const char *p = html;
    const char *end = html ? html + len : NULL;
    if (!html || !tag_name || !attr) {
        return NULL;
    }
    while (p < end && *p) {
        if (tag_open(p, tag_name)) {
            const char *gt = html_tag_close(p);
            if (!gt || gt >= end) {
                return NULL;
            }
            return html_attr(p, (size_t)(gt - p + 1), attr);
        }
        p++;
    }
    return NULL;
}

static char *html_comment_text(const char *start, size_t limit) {
    const char *end;
    const char *p;
    if (!start) {
        return NULL;
    }
    end = start + limit;
    for (p = start; p < end && *p; p++) {
        if (!tag_open(p, "div")) {
            continue;
        }
        {
            const char *gt = html_tag_close(p);
            const char *inner;
            const char *close;
            if (!gt || gt >= end || !class_has(p, (size_t)(gt - p + 1), "comment")) {
                continue;
            }
            inner = gt + 1;
            close = find_ci(inner, "</div");
            if (!close || close > end) {
                return html_plain(inner, (size_t)(end - inner));
            }
            return html_plain(inner, (size_t)(close - inner));
        }
    }
    return NULL;
}

static int ref_boundary(const char *text, const char *hit) {
    unsigned char prev;
    if (!hit || hit <= text) {
        return 1;
    }
    prev = (unsigned char)hit[-1];
    return !isalnum(prev) && prev != '_' && prev != '-';
}

static void html_collect_refs(const char *text, StrList *keys) {
    const char *p = text ? text : "";
    if (!keys) {
        return;
    }
    while (*p) {
        int slides = strncasecmp(p, "slides/", 7) == 0;
        int thumbs = strncasecmp(p, "thumbs/", 7) == 0;
        if ((slides || thumbs) && ref_boundary(text, p)) {
            const char *start = p + 7;
            const char *end = start;
            char quote = (p > text && (p[-1] == '"' || p[-1] == '\'')) ? p[-1] : 0;
            char *raw;
            char *key;
            if (quote) {
                while (*end && *end != quote && *end != '?' && *end != '#') {
                    end++;
                }
            } else {
                while (*end && *end != '"' && *end != '\'' && *end != '<' && *end != '>' && *end != '?' && *end != '#' &&
                       *end != ')' && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r' && *end != '&') {
                    end++;
                }
            }
            raw = strndup(start, (size_t)(end - start));
            key = raw ? slide_key_cstr(raw) : NULL;
            if (key) {
                sl_add_unique(keys, key);
            }
            free(key);
            free(raw);
            p = end;
            if (!*p) {
                break;
            }
            continue;
        }
        p++;
    }
}

static char *matching_disk_name(const StrList *disk, const char *key) {
    size_t i;
    if (!disk || !key) {
        return NULL;
    }
    for (i = 0; i < disk->count; i++) {
        char *dk = slide_key_cstr(disk->items[i]);
        int match = dk && strcmp(dk, key) == 0;
        free(dk);
        if (match) {
            return strdup(disk->items[i]);
        }
    }
    return NULL;
}

static int remember_image(GImage **images, size_t *count, size_t *cap, StrList *seen, const StrList *disk, const char *raw,
                          const char *caption, size_t *captions) {
    char *key = slide_key_cstr(raw);
    char *display;
    GImage *grown;
    if (!key) {
        return 0;
    }
    if (sl_has(seen, key)) {
        free(key);
        return 0;
    }
    if (sl_add(seen, key) != 0) {
        free(key);
        return -1;
    }
    display = matching_disk_name(disk, key);
    if (!display) {
        display = slide_name_n(raw, strlen(raw), 0);
    }
    free(key);
    if (!display) {
        return -1;
    }
    if (*count == *cap) {
        size_t next = *cap ? *cap * 2 : 8;
        grown = (GImage *)realloc(*images, next * sizeof(*grown));
        if (!grown) {
            free(display);
            return -1;
        }
        *images = grown;
        *cap = next;
    }
    memset(&(*images)[*count], 0, sizeof(**images));
    (*images)[*count].name = display;
    (*images)[*count].caption = strdup(caption ? caption : "");
    if (!(*images)[*count].caption) {
        free(display);
        (*images)[*count].name = NULL;
        return -1;
    }
    if ((*images)[*count].caption[0] && captions) {
        (*captions)++;
    }
    (*count)++;
    return 0;
}

static void gimage_fill_files(const char *folder, GImage *images, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) {
        char *slide = images[i].name ? join_path3(folder, "slides", images[i].name) : NULL;
        char *thumb = images[i].name ? join_path3(folder, "thumbs", images[i].name) : NULL;
        struct stat st;
        images[i].width = 0;
        images[i].height = 0;
        images[i].thumb_width = 0;
        images[i].thumb_height = 0;
        images[i].has_slide = slide && file_exists(slide);
        images[i].has_thumb = thumb && file_exists(thumb);
        if (slide) {
            gallery_image_size(slide, &images[i].width, &images[i].height);
        }
        if (thumb) {
            gallery_image_size(thumb, &images[i].thumb_width, &images[i].thumb_height);
        }
        images[i].file_size = (slide && stat(slide, &st) == 0) ? (long long)st.st_size : 0;
        free(slide);
        free(thumb);
    }
}

static void html_collect_gallery(const char *html, const StrList *disk, GImage **images, size_t *count, size_t *captions) {
    const char *p = html ? html : "";
    size_t cap = *count;
    StrList seen;
    sl_init(&seen);
    while (*p) {
        if (p[0] == '<' && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const char *end = strstr(p + 4, "-->");
            p = end ? end + 3 : p + 4;
            continue;
        }
        if (tag_open(p, "style") || tag_open(p, "script")) {
            const char *ender = tag_open(p, "style") ? "</style" : "</script";
            const char *end = find_ci(p + 1, ender);
            p = end ? end + strlen(ender) : p + 1;
            continue;
        }
        if (tag_open(p, "div")) {
            const char *gt = html_tag_close(p);
            if (!gt) {
                break;
            }
            if (class_has(p, (size_t)(gt - p + 1), "thumbnail-wrapper")) {
                const char *close = find_ci(gt + 1, "</div");
                size_t region = close ? (size_t)(close - p) : (size_t)(gt - p + 1);
                char *href = first_attr_in(p, region, "a", "href");
                char *src = first_attr_in(p, region, "img", "src");
                char *caption = first_attr_in(p, region, "a", "data-title");
                const char *raw = (href && href[0]) ? href : src;
                char *plain = NULL;
                if (!caption || !caption[0]) {
                    free(caption);
                    caption = first_attr_in(p, region, "img", "alt");
                }
                if (caption) {
                    plain = collapse_ws(caption);
                }
                if (raw && raw[0]) {
                    remember_image(images, count, &cap, &seen, disk, raw, plain ? plain : "", captions);
                }
                free(plain);
                free(href);
                free(src);
                free(caption);
                p = close ? close + 5 : gt + 1;
                continue;
            }
            p = gt + 1;
            continue;
        }
        if (tag_open(p, "a")) {
            const char *gt = html_tag_close(p);
            if (!gt) {
                break;
            }
            if (class_has(p, (size_t)(gt - p + 1), "thumb")) {
                char *href = html_attr(p, (size_t)(gt - p + 1), "href");
                const char *window = gt + 1;
                const char *limit = window;
                size_t looked = 0;
                char *caption = NULL;
                while (*limit && looked < 4000) {
                    if (tag_open(limit, "div")) {
                        const char *gt2 = html_tag_close(limit);
                        if (gt2 && class_has(limit, (size_t)(gt2 - limit + 1), "card")) {
                            break;
                        }
                    }
                    limit++;
                    looked++;
                }
                if (href && (find_ci(href, "slides/") || find_ci(href, "thumbs/"))) {
                    caption = html_comment_text(window, (size_t)(limit - window));
                    if (!caption || !caption[0]) {
                        char *alt = first_attr_in(window, (size_t)(limit - window), "img", "alt");
                        char *plain = alt ? collapse_ws(alt) : NULL;
                        free(caption);
                        free(alt);
                        caption = plain;
                    }
                    remember_image(images, count, &cap, &seen, disk, href, caption ? caption : "", captions);
                }
                free(href);
                free(caption);
            }
            p = gt + 1;
            continue;
        }
        p++;
    }
    sl_free(&seen);
}

static void read_folder_pages(const char *folder, const char *slug, StrList *refs, GImage **images, size_t *count,
                              size_t *captions) {
    char *primary = pick_primary(folder, slug && slug[0] ? slug : "");
    StrList names;
    StrList disk;
    char *slides;
    size_t i;
    int pass;
    int have = 0;
    sl_init(&names);
    sl_init(&disk);
    slides = folder ? join_path(folder, "slides") : NULL;
    if (slides) {
        list_names(slides, 0, 1, &disk);
    }
    free(slides);
    if (!folder || list_names(folder, 0, 0, &names) != 0) {
        sl_free(&names);
        sl_free(&disk);
        free(primary);
        return;
    }
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < names.count; i++) {
            int is_primary = primary && strcmp(names.items[i], primary) == 0;
            char *path;
            char *text;
            size_t before;
            if (!is_page_file(names.items[i]) || strstr(names.items[i], ".mailchimp.")) {
                continue;
            }
            if ((pass == 0 && !is_primary) || (pass == 1 && is_primary)) {
                continue;
            }
            path = join_path(folder, names.items[i]);
            text = path ? read_file(path, NULL) : NULL;
            free(path);
            if (!text) {
                continue;
            }
            html_collect_refs(text, refs);
            if (!have) {
                before = *count;
                html_collect_gallery(text, &disk, images, count, captions);
                if (*count > before) {
                    have = 1;
                }
            }
            free(text);
        }
    }
    gimage_fill_files(folder, *images, *count);
    sl_free(&names);
    sl_free(&disk);
    free(primary);
}

static int html_gallery_section(const char *section) {
    return section && (strcmp(section, "articles") == 0 || strcmp(section, "awards") == 0 || strcmp(section, "projects") == 0);
}

static void document_images(StrList *documented, const GImage *images, size_t count) {
    size_t i;
    if (!documented) {
        return;
    }
    for (i = 0; i < count; i++) {
        char *key = slide_key_cstr(images[i].name);
        if (key) {
            sl_add_unique(documented, key);
            free(key);
        }
    }
}

/* HTML galleries live in the page. A slide used there is not unlisted. */
static int load_documented_images(const char *folder, const char *section, const char *slug, GImage **images_out,
                                  size_t *count_out, size_t *captions_out, StrList *documented, int *from_html,
                                  size_t *page_ref_count, char *err, size_t err_sz) {
    int deep_rc;
    GImage *html_images = NULL;
    size_t html_count = 0;
    size_t html_captions = 0;
    StrList page_refs;
    size_t i;
    sl_init(&page_refs);
    if (images_out) {
        *images_out = NULL;
    }
    if (count_out) {
        *count_out = 0;
    }
    if (captions_out) {
        *captions_out = 0;
    }
    if (from_html) {
        *from_html = 0;
    }
    if (page_ref_count) {
        *page_ref_count = 0;
    }
    deep_rc = load_deep_images(folder, NULL, images_out, count_out, captions_out, err, err_sz);
    if (deep_rc < 0) {
        free_gimages(images_out ? *images_out : NULL, count_out ? *count_out : 0);
        if (images_out) {
            *images_out = NULL;
        }
        if (count_out) {
            *count_out = 0;
        }
        if (captions_out) {
            *captions_out = 0;
        }
    }
    if (html_gallery_section(section)) {
        read_folder_pages(folder, slug, &page_refs, &html_images, &html_count, &html_captions);
        if (html_count > 0) {
            free_gimages(images_out ? *images_out : NULL, count_out ? *count_out : 0);
            if (images_out) {
                *images_out = html_images;
            } else {
                free_gimages(html_images, html_count);
            }
            if (count_out) {
                *count_out = html_count;
            }
            if (captions_out) {
                *captions_out = html_captions;
            }
            html_images = NULL;
            html_count = 0;
            if (from_html) {
                *from_html = 1;
            }
        }
    }
    if (page_ref_count) {
        *page_ref_count = page_refs.count;
    }
    if (documented) {
        for (i = 0; i < page_refs.count; i++) {
            sl_add_unique(documented, page_refs.items[i]);
        }
        document_images(documented, images_out ? *images_out : NULL, count_out ? *count_out : 0);
    }
    free_gimages(html_images, html_count);
    sl_free(&page_refs);
    return deep_rc;
}

static void add_listed_images(JsonValue *array, const char *section, const char *slug, const GImage *images, size_t count) {
    size_t i;
    if (!array) {
        return;
    }
    for (i = 0; i < count; i++) {
        JsonValue *item = json_object();
        char *slide_rel = join_path("slides", images[i].name ? images[i].name : "");
        char *thumb_rel = join_path("thumbs", images[i].name ? images[i].name : "");
        char *slide_url = url_join_public(section, slug, slide_rel);
        char *thumb_url = url_join_public(section, slug, thumb_rel);
        if (item) {
            jput_str(item, "name", images[i].name);
            jput_str(item, "caption", images[i].caption);
            jput_str(item, "slideUrl", slide_url);
            jput_str(item, "thumbUrl", thumb_url);
            jput_int(item, "width", images[i].width);
            jput_int(item, "height", images[i].height);
            jput_int(item, "thumbWidth", images[i].thumb_width);
            jput_int(item, "thumbHeight", images[i].thumb_height);
            jput_int(item, "fileSize", images[i].file_size);
            jput_bool(item, "hasSlide", images[i].has_slide);
            jput_bool(item, "hasThumb", images[i].has_thumb);
            jput_bool(item, "isBullet", images[i].is_bullet);
            json_array_add(array, item);
        }
        free(slide_rel);
        free(thumb_rel);
        free(slide_url);
        free(thumb_url);
    }
}

static void add_unlisted_slides(JsonValue *array, const char *section, const char *slug, const char *folder,
                                const StrList *documented) {
    StrList files;
    char *slides;
    char *thumbs;
    size_t i;
    if (!array) {
        return;
    }
    slides = folder ? join_path(folder, "slides") : NULL;
    thumbs = folder ? join_path(folder, "thumbs") : NULL;
    sl_init(&files);
    if (slides && list_names(slides, 0, 1, &files) == 0) {
        for (i = 0; i < files.count; i++) {
            JsonValue *item;
            char *slide_path;
            char *thumb_path;
            char *slide_rel;
            char *thumb_rel;
            char *slide_url;
            char *thumb_url;
            struct stat st;
            int width = 0;
            int height = 0;
            if (name_is_documented(files.items[i], documented)) {
                continue;
            }
            item = json_object();
            if (!item) {
                continue;
            }
            slide_path = join_path(slides, files.items[i]);
            thumb_path = thumbs ? join_path(thumbs, files.items[i]) : NULL;
            slide_rel = join_path("slides", files.items[i]);
            thumb_rel = join_path("thumbs", files.items[i]);
            slide_url = url_join_public(section, slug, slide_rel);
            thumb_url = url_join_public(section, slug, thumb_rel);
            if (slide_path) {
                gallery_image_size(slide_path, &width, &height);
            }
            jput_str(item, "name", files.items[i]);
            jput_str(item, "slideUrl", slide_url);
            jput_str(item, "thumbUrl", thumb_url);
            jput_bool(item, "hasThumb", thumb_path && file_exists(thumb_path));
            jput_int(item, "width", width);
            jput_int(item, "height", height);
            jput_int(item, "fileSize", slide_path && stat(slide_path, &st) == 0 ? (long long)st.st_size : 0);
            json_array_add(array, item);
            free(slide_path);
            free(thumb_path);
            free(slide_rel);
            free(thumb_rel);
            free(slide_url);
            free(thumb_url);
        }
    }
    sl_free(&files);
    free(slides);
    free(thumbs);
}

static char *folder_rel(const char *section, const char *slug) {
    return join_path(section, slug);
}

static int ensure_folder(const char *repo, const char *section, const char *slug, char *folder_out, size_t folder_sz, char *err,
                         size_t err_sz) {
    int n;
    if (!gallery_section_ok(section)) {
        set_err(err, err_sz, "Unknown section");
        return 1;
    }
    if (!gallery_valid_slug(slug)) {
        set_err(err, err_sz, "Folder name is not allowed");
        return 1;
    }
    n = snprintf(folder_out, folder_sz, "%s/%s/%s", repo, section, slug);
    if (n < 0 || (size_t)n >= folder_sz) {
        set_err(err, err_sz, "Folder path is too long");
        return -1;
    }
    if (!dir_exists(folder_out) || !path_within(repo, folder_out)) {
        set_err(err, err_sz, "Folder not found");
        return 2;
    }
    return 0;
}

static JsonValue *empty_page_values(void) {
    JsonValue *object = json_object();
    jput_str(object, "title", "");
    jput_str(object, "description", "");
    jput_str(object, "image", "");
    jput_str(object, "type", "");
    jput_str(object, "date", "");
    return object;
}

static JsonValue *empty_related(void) {
    JsonValue *object = json_object();
    jput_str(object, "vehicleType", "");
    jput_str(object, "makeType", "");
    jput_str(object, "vehicleName", "");
    jput_str(object, "overrideIds", "");
    return object;
}

static void read_page_meta(const char *page_path, char **title, char **description, char **image, char **type, char **date,
                           char **vehicle_type, char **make_type, char **vehicle_name, char **override_ids, int *jalbum) {
    size_t len = 0;
    char *text = read_file(page_path, &len);
    size_t start = 0;
    size_t end = 0;
    size_t body_start = 0;
    size_t body_end = 0;
    *title = strdup("");
    *description = strdup("");
    *image = strdup("");
    *type = strdup("");
    *date = strdup("");
    *vehicle_type = strdup("");
    *make_type = strdup("");
    *vehicle_name = strdup("");
    *override_ids = strdup("");
    *jalbum = 0;
    if (!text) {
        return;
    }
    *jalbum = strstr(text, "Generated by jAlbum") != NULL || strstr(text, "jalbum.net") != NULL;
    if (find_html_comment(text, len, "page-values", &start, &end, &body_start, &body_end) == 0) {
        parse_comment_fields(text + body_start, body_end - body_start, title, description, image, type, date, NULL, NULL, NULL,
                             NULL, 0);
    }
    if (find_html_comment(text, len, "related-sales", &start, &end, &body_start, &body_end) == 0) {
        parse_comment_fields(text + body_start, body_end - body_start, NULL, NULL, NULL, NULL, NULL, vehicle_type, make_type,
                             vehicle_name, override_ids, 1);
    }
    free(text);
}

static void studio_attach_page_fields(JsonValue *root, const char *repo, const char *public_path, int deep);

static JsonDoc *finish_doc(JsonValue *root) {
    JsonDoc *doc = json_doc_new(root);
    if (!doc) {
        return NULL;
    }
    return doc;
}

static char *first_image_data_slide(const char *folder) {
    char *path;
    FILE *file;
    char line[4096];
    if (!folder) {
        return NULL;
    }
    path = join_path(folder, "image_data.txt");
    if (!path) {
        return NULL;
    }
    file = fopen(path, "rb");
    free(path);
    if (!file) {
        return NULL;
    }
    while (fgets(line, (int)sizeof line, file)) {
        char *start = line;
        char *stop;
        char *eq;
        while (*start == ' ' || *start == '\t') {
            start++;
        }
        if (*start == '\0' || *start == '\n' || *start == '\r') {
            continue;
        }
        eq = strchr(start, '=');
        if (eq) {
            *eq = '\0';
        }
        stop = start + strlen(start);
        while (stop > start && (stop[-1] == '\n' || stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t')) {
            stop--;
            *stop = '\0';
        }
        if (*start && !strchr(start, '/') && !strchr(start, '\\')) {
            char *name = strdup(start);
            fclose(file);
            return name;
        }
    }
    fclose(file);
    return NULL;
}

/* folderthumb.jpg when it exists, otherwise the first slide named in image_data.txt. */
static char *hero_thumb_url(const char *section, const char *slug, const char *folder, int has_folder_thumb,
                            const GImage *images, size_t image_count) {
    char *name = NULL;
    char *rel = NULL;
    char *url = NULL;
    size_t i;
    if (!section || !slug) {
        return strdup("");
    }
    if (has_folder_thumb) {
        url = url_join_public(section, slug, "folderthumb.jpg");
        return url ? url : strdup("");
    }
    name = first_image_data_slide(folder);
    if (!name) {
        for (i = 0; i < image_count; i++) {
            if (images && images[i].name && images[i].name[0] && images[i].has_slide) {
                name = strdup(images[i].name);
                break;
            }
        }
    }
    if (!name || !name[0]) {
        free(name);
        return strdup("");
    }
    rel = join_path("slides", name);
    free(name);
    if (!rel) {
        return strdup("");
    }
    url = url_join_public(section, slug, rel);
    free(rel);
    return url ? url : strdup("");
}

JsonDoc *gallery_folders_document(const char *repo_root, const char *section, char *err, size_t err_sz) {
    char section_dir[4096];
    StrList folders;
    char *porcelain;
    JsonValue *root;
    JsonValue *list;
    size_t i;
    if (!repo_root || !gallery_section_ok(section)) {
        set_err(err, err_sz, "Unknown section");
        return NULL;
    }
    snprintf(section_dir, sizeof(section_dir), "%s/%s", repo_root, section);
    sl_init(&folders);
    if (!dir_exists(section_dir) || list_names(section_dir, 1, 0, &folders) != 0) {
        sl_free(&folders);
        set_err(err, err_sz, "Could not read section directory");
        return NULL;
    }
    porcelain = git_status_text(repo_root, section);
    root = json_object();
    list = json_array();
    if (!root || !list) {
        json_free(root);
        json_free(list);
        free(porcelain);
        sl_free(&folders);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    jput_bool(root, "ok", 1);
    jput_str(root, "section", section);
    json_object_put(root, "folders", list);
    for (i = 0; i < folders.count; i++) {
        const char *slug = folders.items[i];
        char *folder = join_path(section_dir, slug);
        char *rel = folder_rel(section, slug);
        char *slides = folder ? join_path(folder, "slides") : NULL;
        char *thumbs = folder ? join_path(folder, "thumbs") : NULL;
        char *share = folder ? join_path(folder, "shareimage.jpg") : NULL;
        char *thumb = folder ? join_path(folder, "folderthumb.jpg") : NULL;
        JsonValue *item = json_object();
        JsonValue *warnings = json_array();
        GImage *images = NULL;
        size_t image_count = 0;
        size_t caption_count = 0;
        StrList slide_files;
        size_t slides_on_disk = 0;
        size_t thumbs_on_disk = 0;
        size_t unlisted = 0;
        size_t s;
        int load_rc;
        int from_html = 0;
        size_t page_refs = 0;
        char load_err[128];
        StrList documented;
        sl_init(&slide_files);
        sl_init(&documented);
        if (slides && list_names(slides, 0, 1, &slide_files) == 0) {
            slides_on_disk = slide_files.count;
        }
        {
            StrList thumb_files;
            sl_init(&thumb_files);
            if (thumbs && list_names(thumbs, 0, 1, &thumb_files) == 0) {
                thumbs_on_disk = thumb_files.count;
            }
            sl_free(&thumb_files);
        }
        load_err[0] = '\0';
        load_rc = folder ? load_documented_images(folder, section, slug, &images, &image_count, &caption_count, &documented,
                                                  &from_html, &page_refs, load_err, sizeof(load_err))
                         : -1;
        if (load_rc < 0 && load_err[0]) {
            jwarn(warnings, "deep-data.json could not be parsed");
        } else if (load_rc > 0 && !from_html && page_refs == 0 && slides_on_disk) {
            jwarn(warnings, "Missing deep-data.json");
        }
        for (s = 0; s < slide_files.count; s++) {
            if (!name_is_documented(slide_files.items[s], &documented)) {
                unlisted++;
            }
        }
        if (strcmp(section, "sales") == 0) {
            char *index_path = folder ? join_path(folder, "index.php") : NULL;
            GallerySalesHeader header;
            char *web;
            memset(&header, 0, sizeof(header));
            if (!index_path || gallery_read_sales_header(index_path, &header, NULL, 0) != 0) {
                jwarn(warnings, "Could not parse index.php");
                header.vehicle_name = strdup(slug);
                header.vehicle_type = strdup("");
                header.make_type = strdup("");
                header.inventory_number = strdup("");
                header.price = strdup("");
                header.description = strdup("");
            }
            web = url_join_public(section, slug, "index.php");
            jput_str(item, "slug", slug);
            jput_str(item, "path", web ? web : "");
            jput_str(item, "vehicleName", header.vehicle_name);
            jput_str(item, "vehicleType", header.vehicle_type);
            jput_str(item, "makeType", header.make_type);
            jput_str(item, "inventoryNumber", header.inventory_number);
            jput_str(item, "price", header.price);
            jput_bool(item, "sold", header.price && strcasecmp(header.price, "SOLD") == 0);
            jput_bool(item, "doNotRender", header.do_not_render);
            free(web);
            gallery_sales_header_free(&header);
            free(index_path);
        } else {
            char *primary = folder ? pick_primary(folder, slug) : NULL;
            char *page_path = primary ? join_path(folder, primary) : NULL;
            char *title = NULL;
            char *description = NULL;
            char *image = NULL;
            char *type = NULL;
            char *date = NULL;
            char *vehicle_type = NULL;
            char *make_type = NULL;
            char *vehicle_name = NULL;
            char *override_ids = NULL;
            int jalbum = 0;
            char *web = NULL;
            if (page_path) {
                read_page_meta(page_path, &title, &description, &image, &type, &date, &vehicle_type, &make_type, &vehicle_name,
                               &override_ids, &jalbum);
                web = url_join_public(section, slug, primary);
            } else {
                jwarn(warnings, "No HTML page found");
                title = strdup(slug);
                description = strdup("");
                image = strdup("");
                type = strdup("");
                date = strdup("");
                vehicle_type = strdup("");
                make_type = strdup("");
                vehicle_name = strdup("");
                override_ids = strdup("");
            }
            jput_str(item, "slug", slug);
            jput_str(item, "path", web ? web : "");
            jput_str(item, "title", title && title[0] ? title : slug);
            jput_str(item, "vehicleName", title && title[0] ? title : slug);
            jput_str(item, "vehicleType", vehicle_type);
            jput_str(item, "makeType", make_type);
            jput_str(item, "inventoryNumber", "");
            jput_str(item, "price", "");
            jput_bool(item, "sold", 0);
            jput_bool(item, "doNotRender", 0);
            jput_bool(item, "jalbum", jalbum);
            jput_str(item, "primaryFile", primary ? primary : "");
            free(title);
            free(description);
            free(image);
            free(type);
            free(date);
            free(vehicle_type);
            free(make_type);
            free(vehicle_name);
            free(override_ids);
            free(primary);
            free(page_path);
            free(web);
        }
        if ((strcmp(section, "sales") == 0 || slides_on_disk || thumbs_on_disk) && (!share || !file_exists(share))) {
            jwarn(warnings, "Missing shareimage.jpg");
        }
        if (strcmp(section, "sales") == 0 && (!thumb || !file_exists(thumb))) {
            jwarn(warnings, "Missing folderthumb.jpg");
        }
        if (slides && thumbs && dir_exists(slides) && dir_exists(thumbs) && slides_on_disk != thumbs_on_disk) {
            char message[160];
            snprintf(message, sizeof(message), "slides/ has %zu images but thumbs/ has %zu", slides_on_disk, thumbs_on_disk);
            jwarn(warnings, message);
        }
        if (unlisted > 0) {
            char message[120];
            snprintf(message, sizeof(message), "%zu image(s) in slides/ are not in the gallery", unlisted);
            jwarn(warnings, message);
        }
        jput_int(item, "photoCount", (long long)((from_html || load_rc == 0) ? image_count : 0));
        jput_int(item, "slidesOnDisk", (long long)slides_on_disk);
        jput_int(item, "unlistedCount", (long long)unlisted);
        jput_int(item, "captionCount", (long long)caption_count);
        jput_bool(item, "hasShareImage", share && file_exists(share));
        {
            int has_folder_thumb = thumb && file_exists(thumb);
            char *hero = hero_thumb_url(section, slug, folder, has_folder_thumb, images, image_count);
            jput_bool(item, "hasFolderThumb", has_folder_thumb);
            jput_str(item, "heroThumbUrl", hero);
            free(hero);
        }
        jput_bool(item, "dirty", rel && porcelain_touches(porcelain, rel));
        json_object_put(item, "warnings", warnings);
        json_array_add(list, item);
        free_gimages(images, image_count);
        sl_free(&slide_files);
        sl_free(&documented);
        free(folder);
        free(rel);
        free(slides);
        free(thumbs);
        free(share);
        free(thumb);
    }
    free(porcelain);
    sl_free(&folders);
    return finish_doc(root);
}

JsonDoc *gallery_state_document(const char *repo_root, const char *section, const char *slug, char *err, size_t err_sz) {
    char folder[4096];
    int folder_rc = ensure_folder(repo_root, section, slug, folder, sizeof(folder), err, err_sz);
    JsonValue *root;
    JsonValue *details;
    JsonValue *page_values;
    JsonValue *related;
    JsonValue *share;
    JsonValue *images_json;
    JsonValue *unlisted_json;
    JsonValue *warnings;
    GallerySalesHeader header;
    GImage *images = NULL;
    size_t image_count = 0;
    size_t caption_count = 0;
    int load_rc;
    int from_html = 0;
    size_t page_refs = 0;
    StrList documented;
    char *primary = NULL;
    char *page_path = NULL;
    char *title = NULL;
    char *page_description = NULL;
    char *page_image = NULL;
    char *page_type = NULL;
    char *page_date = NULL;
    char *rel_type = NULL;
    char *rel_make = NULL;
    char *rel_name = NULL;
    char *rel_override = NULL;
    int jalbum = 0;
    char *preview = NULL;
    char *share_file;
    char *thumb_file;
    char *slides;
    char *thumbs;
    char *porcelain;
    char *rel;
    StrList slide_files;
    size_t slides_on_disk = 0;
    size_t thumbs_on_disk = 0;
    char *share_title = NULL;
    char *share_description = NULL;
    char *effective = NULL;
    int sold = 0;

    if (folder_rc != 0) {
        return NULL;
    }
    memset(&header, 0, sizeof(header));
    root = json_object();
    details = json_object();
    warnings = json_array();
    images_json = json_array();
    unlisted_json = json_array();
    if (!root || !details || !warnings || !images_json || !unlisted_json) {
        json_free(root);
        json_free(details);
        json_free(warnings);
        json_free(images_json);
        json_free(unlisted_json);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (strcmp(section, "sales") == 0) {
        char *index_path = join_path(folder, "index.php");
        if (!index_path || gallery_read_sales_header(index_path, &header, err, err_sz) != 0) {
            jwarn(warnings, "Could not parse index.php");
            header.vehicle_name = strdup(slug);
            header.vehicle_type = strdup("");
            header.make_type = strdup("");
            header.inventory_number = strdup("");
            header.price = strdup("");
            header.description = strdup("");
        }
        free(index_path);
        preview = url_join_public(section, slug, "index.php");
        page_values = empty_page_values();
        related = empty_related();
    } else {
        primary = pick_primary(folder, slug);
        if (primary) {
            page_path = join_path(folder, primary);
            read_page_meta(page_path, &title, &page_description, &page_image, &page_type, &page_date, &rel_type, &rel_make,
                           &rel_name, &rel_override, &jalbum);
            preview = url_join_public(section, slug, primary);
        } else {
            jwarn(warnings, "No HTML page found");
            title = strdup("");
            page_description = strdup("");
            page_image = strdup("");
            page_type = strdup("");
            page_date = strdup("");
            rel_type = strdup("");
            rel_make = strdup("");
            rel_name = strdup("");
            rel_override = strdup("");
        }
        header.vehicle_name = strdup(title && title[0] ? title : slug);
        header.vehicle_type = strdup(rel_type ? rel_type : "");
        header.make_type = strdup(rel_make ? rel_make : "");
        header.inventory_number = strdup("");
        header.price = strdup("");
        header.description = strdup(page_description ? page_description : "");
        page_values = json_object();
        related = json_object();
        jput_str(page_values, "title", title);
        jput_str(page_values, "description", page_description);
        jput_str(page_values, "image", page_image);
        jput_str(page_values, "type", page_type && page_type[0] ? page_type : "Article");
        jput_str(page_values, "date", page_date);
        jput_str(related, "vehicleType", rel_type);
        jput_str(related, "makeType", rel_make);
        jput_str(related, "vehicleName", rel_name);
        jput_str(related, "overrideIds", rel_override);
    }
    sl_init(&documented);
    load_rc = load_documented_images(folder, section, slug, &images, &image_count, &caption_count, &documented, &from_html,
                                     &page_refs, err, err_sz);
    if (load_rc < 0) {
        jwarn(warnings, "deep-data.json could not be parsed");
    }
    sold = header.price && strcasecmp(header.price, "SOLD") == 0;
    mark_bullets(images, image_count, header.vehicle_name);
    if (strcmp(section, "sales") == 0) {
        char *plain = plain_html(header.description);
        share_title = sales_share_title(header.vehicle_name, sold);
        share_description = truncate_meta(plain);
        free(plain);
    } else {
        share_title = article_share_title(title && title[0] ? title : header.vehicle_name);
        share_description = truncate_meta(page_description ? page_description : "");
    }
    add_listed_images(images_json, section, slug, images, image_count);
    sl_init(&slide_files);
    slides = join_path(folder, "slides");
    thumbs = join_path(folder, "thumbs");
    if (slides && list_names(slides, 0, 1, &slide_files) == 0) {
        slides_on_disk = slide_files.count;
    }
    {
        StrList thumb_files;
        sl_init(&thumb_files);
        if (thumbs && list_names(thumbs, 0, 1, &thumb_files) == 0) {
            thumbs_on_disk = thumb_files.count;
        }
        sl_free(&thumb_files);
    }
    add_unlisted_slides(unlisted_json, section, slug, folder, &documented);
    if (load_rc > 0 && !from_html && page_refs == 0 && slides_on_disk) {
        jwarn(warnings, "Missing deep-data.json");
    }
    share_file = join_path(folder, "shareimage.jpg");
    thumb_file = join_path(folder, "folderthumb.jpg");
    if ((strcmp(section, "sales") == 0 || slides_on_disk || thumbs_on_disk) && !file_exists(share_file)) {
        jwarn(warnings, "Missing shareimage.jpg");
    }
    if (strcmp(section, "sales") == 0 && !file_exists(thumb_file)) {
        jwarn(warnings, "Missing folderthumb.jpg");
    }
    if (slides && thumbs && dir_exists(slides) && dir_exists(thumbs) && slides_on_disk != thumbs_on_disk) {
        char message[160];
        snprintf(message, sizeof(message), "slides/ has %zu images but thumbs/ has %zu", slides_on_disk, thumbs_on_disk);
        jwarn(warnings, message);
    }
    if (json_array_len(unlisted_json) > 0) {
        char message[120];
        snprintf(message, sizeof(message), "%zu image(s) in slides/ are not in the gallery", json_array_len(unlisted_json));
        jwarn(warnings, message);
    }
    if (file_exists(share_file)) {
        effective = url_join_public(section, slug, "shareimage.jpg");
    } else if (image_count && images[0].has_slide) {
        char *slide_rel = join_path("slides", images[0].name);
        effective = url_join_public(section, slug, slide_rel);
        free(slide_rel);
    } else if (file_exists(thumb_file)) {
        effective = url_join_public(section, slug, "folderthumb.jpg");
    } else {
        effective = strdup("");
    }
    share = json_object();
    jput_bool(share, "hasShareImage", file_exists(share_file));
    {
        char *share_url = url_join_public(section, slug, "shareimage.jpg");
        char *thumb_url = url_join_public(section, slug, "folderthumb.jpg");
        jput_str(share, "shareImageUrl", share_url);
        jput_bool(share, "hasFolderThumb", file_exists(thumb_file));
        jput_str(share, "folderThumbUrl", thumb_url);
        free(share_url);
        free(thumb_url);
    }
    jput_str(share, "effectiveShareImage", effective);
    jput_str(share, "title", share_title);
    jput_str(share, "description", share_description);
    rel = folder_rel(section, slug);
    porcelain = git_status_text(repo_root, rel ? rel : section);
    jput_bool(root, "ok", 1);
    jput_str(root, "section", section);
    jput_str(root, "slug", slug);
    jput_str(root, "previewUrl", preview);
    jput_str(details, "vehicleName", header.vehicle_name);
    jput_str(details, "vehicleType", header.vehicle_type);
    jput_str(details, "makeType", header.make_type);
    jput_str(details, "inventoryNumber", header.inventory_number);
    jput_str(details, "price", header.price);
    jput_bool(details, "doNotRender", header.do_not_render);
    jput_str(details, "description", header.description);
    json_object_put(root, "details", details);
    json_object_put(root, "pageValues", page_values);
    json_object_put(root, "relatedSales", related);
    json_object_put(root, "share", share);
    json_object_put(root, "images", images_json);
    json_object_put(root, "unlistedSlides", unlisted_json);
    jput_bool(root, "dirty", porcelain_touches(porcelain, rel));
    jput_bool(root, "jalbum", jalbum);
    json_object_put(root, "warnings", warnings);
    gallery_sales_header_free(&header);
    free_gimages(images, image_count);
    free(primary);
    free(page_path);
    free(title);
    free(page_description);
    free(page_image);
    free(page_type);
    free(page_date);
    free(rel_type);
    free(rel_make);
    free(rel_name);
    free(rel_override);
    if (preview && preview[0]) {
        studio_attach_page_fields(root, repo_root, preview, 1);
    }
    free(preview);
    free(share_title);
    free(share_description);
    free(effective);
    free(share_file);
    free(thumb_file);
    free(slides);
    free(thumbs);
    free(porcelain);
    free(rel);
    sl_free(&slide_files);
    sl_free(&documented);
    return finish_doc(root);
}

int gallery_save_images(const char *repo_root, const char *section, const char *slug, const JsonValue *images, char *err,
                        size_t err_sz) {
    char folder[4096];
    int folder_rc = ensure_folder(repo_root, section, slug, folder, sizeof(folder), err, err_sz);
    char *deep_path;
    char *image_path;
    size_t len = 0;
    char *text;
    JsonDoc *doc;
    char *rendered;
    size_t rendered_len = 0;
    char *image_text;
    size_t image_len = 0;
    int apply_rc;
    int wrote = 0;
    if (folder_rc != 0) {
        return folder_rc;
    }
    deep_path = join_path(folder, "deep-data.json");
    image_path = join_path(folder, "image_data.txt");
    text = deep_path ? read_file(deep_path, &len) : NULL;
    if (!text) {
        free(deep_path);
        free(image_path);
        set_err(err, err_sz, "deep-data.json is missing");
        return 2;
    }
    doc = json_parse(text, len, err, err_sz);
    free(text);
    if (!doc) {
        free(deep_path);
        free(image_path);
        return -1;
    }
    apply_rc = gallery_apply_image_list(doc, images, folder, err, err_sz);
    if (apply_rc != 0) {
        json_doc_free(doc);
        free(deep_path);
        free(image_path);
        return apply_rc;
    }
    rendered = json_stringify(doc, &rendered_len);
    image_text = gallery_image_data_from_doc(doc, &image_len);
    json_doc_free(doc);
    if (!rendered || !image_text) {
        free(rendered);
        free(image_text);
        free(deep_path);
        free(image_path);
        set_err(err, err_sz, "Could not serialize gallery files");
        return -1;
    }
    if (write_if_changed(deep_path, rendered, rendered_len, 0, &wrote) != 0 ||
        write_if_changed(image_path, image_text, image_len, 1, &wrote) != 0) {
        free(rendered);
        free(image_text);
        free(deep_path);
        free(image_path);
        set_err(err, err_sz, "Could not write gallery files");
        return -1;
    }
    free(rendered);
    free(image_text);
    free(deep_path);
    free(image_path);
    return 0;
}

static int update_page_file(const char *repo, const char *page_path, const JsonValue *page_values, const JsonValue *related,
                            char *err, size_t err_sz) {
    size_t len = 0;
    char *text = read_file(page_path, &len);
    char *title = NULL;
    char *description = NULL;
    char *image = NULL;
    char *type = NULL;
    char *date = NULL;
    char *vehicle_type = NULL;
    char *make_type = NULL;
    char *vehicle_name = NULL;
    char *override_ids = NULL;
    int jalbum = 0;
    char *updated;
    char *script;
    char *output = NULL;
    int code = 1;
    int changed = 0;
    if (!text) {
        set_err(err, err_sz, "Could not read the page");
        return -1;
    }
    read_page_meta(page_path, &title, &description, &image, &type, &date, &vehicle_type, &make_type, &vehicle_name, &override_ids,
                   &jalbum);
    (void)jalbum;
    updated = text;
    text = NULL;
    if (page_values && json_type(page_values) == JSON_OBJECT) {
        char *new_title = json_object_has(page_values, "title") ? json_text_dup(json_object_get(page_values, "title")) : strdup(title);
        char *new_description =
            json_object_has(page_values, "description") ? json_text_dup(json_object_get(page_values, "description")) : strdup(description);
        char *new_image = json_object_has(page_values, "image") ? json_text_dup(json_object_get(page_values, "image")) : strdup(image);
        char *new_type = json_object_has(page_values, "type") ? json_text_dup(json_object_get(page_values, "type")) : strdup(type);
        char *new_date = json_object_has(page_values, "date") ? json_text_dup(json_object_get(page_values, "date")) : strdup(date);
        if (!new_title || !new_title[0] || !new_description || !new_description[0]) {
            set_err(err, err_sz, "Page title and description are required");
            free(updated);
            free(new_title);
            free(new_description);
            free(new_image);
            free(new_type);
            free(new_date);
            free(title);
            free(description);
            free(image);
            free(type);
            free(date);
            free(vehicle_type);
            free(make_type);
            free(vehicle_name);
            free(override_ids);
            return 1;
        }
        if (field_has_break(new_title) || field_has_break(new_description) || field_has_break(new_image) ||
            field_has_break(new_type) || field_has_break(new_date) || !valid_page_type(new_type) || !valid_page_date(new_date)) {
            set_err(err, err_sz, "Page values contain an invalid character, type, or date");
            free(updated);
            free(new_title);
            free(new_description);
            free(new_image);
            free(new_type);
            free(new_date);
            free(title);
            free(description);
            free(image);
            free(type);
            free(date);
            free(vehicle_type);
            free(make_type);
            free(vehicle_name);
            free(override_ids);
            return 1;
        }
        if (strcmp(new_title, title) != 0 || strcmp(new_description, description) != 0 || strcmp(new_image ? new_image : "", image) != 0 ||
            strcmp(new_type ? new_type : "", type) != 0 || strcmp(new_date ? new_date : "", date) != 0) {
            size_t start = 0;
            size_t end = 0;
            size_t body_start = 0;
            size_t body_end = 0;
            char *indent = NULL;
            char *comment;
            char *next;
            if (find_html_comment(updated, strlen(updated), "page-values", &start, &end, &body_start, &body_end) == 0) {
                indent = comment_indent(updated, start);
            } else {
                indent = strdup("");
                start = 0;
                end = 0;
            }
            comment = build_page_comment(indent ? indent : "", new_title, new_description, new_image, new_type, new_date);
            if (!comment) {
                free(indent);
                free(updated);
                set_err(err, err_sz, "Could not build page-values comment");
                free(new_title);
                free(new_description);
                free(new_image);
                free(new_type);
                free(new_date);
                free(title);
                free(description);
                free(image);
                free(type);
                free(date);
                free(vehicle_type);
                free(make_type);
                free(vehicle_name);
                free(override_ids);
                return -1;
            }
            if (end > start) {
                next = replace_span(updated, strlen(updated), start, end, comment);
            } else {
                Buf buf;
                buf_init(&buf);
                buf_puts(&buf, comment);
                buf_puts(&buf, "\n\n");
                buf_puts(&buf, updated);
                next = buf_steal(&buf);
            }
            free(comment);
            free(indent);
            if (!next) {
                free(updated);
                set_err(err, err_sz, "Could not update page-values");
                free(new_title);
                free(new_description);
                free(new_image);
                free(new_type);
                free(new_date);
                free(title);
                free(description);
                free(image);
                free(type);
                free(date);
                free(vehicle_type);
                free(make_type);
                free(vehicle_name);
                free(override_ids);
                return -1;
            }
            free(updated);
            updated = next;
            changed = 1;
        }
        free(new_title);
        free(new_description);
        free(new_image);
        free(new_type);
        free(new_date);
    }
    if (related && json_type(related) == JSON_OBJECT) {
        char *new_type = json_object_has(related, "vehicleType") ? json_text_dup(json_object_get(related, "vehicleType")) : strdup(vehicle_type);
        char *new_make = json_object_has(related, "makeType") ? json_text_dup(json_object_get(related, "makeType")) : strdup(make_type);
        char *new_name = json_object_has(related, "vehicleName") ? json_text_dup(json_object_get(related, "vehicleName")) : strdup(vehicle_name);
        char *new_override = NULL;
        if (json_object_has(related, "overrideIds")) {
            new_override = json_text_dup(json_object_get(related, "overrideIds"));
        } else if (json_object_has(related, "Override IDs")) {
            new_override = json_text_dup(json_object_get(related, "Override IDs"));
        } else {
            new_override = strdup(override_ids ? override_ids : "");
        }
        if (field_has_break(new_type) || field_has_break(new_make) || field_has_break(new_name) || field_has_break(new_override)) {
            set_err(err, err_sz, "Related sales fields cannot contain line breaks");
            free(updated);
            free(new_type);
            free(new_make);
            free(new_name);
            free(new_override);
            free(title);
            free(description);
            free(image);
            free(type);
            free(date);
            free(vehicle_type);
            free(make_type);
            free(vehicle_name);
            free(override_ids);
            return 1;
        }
        if (strcmp(new_type ? new_type : "", vehicle_type ? vehicle_type : "") != 0 ||
            strcmp(new_make ? new_make : "", make_type ? make_type : "") != 0 ||
            strcmp(new_name ? new_name : "", vehicle_name ? vehicle_name : "") != 0 ||
            strcmp(new_override ? new_override : "", override_ids ? override_ids : "") != 0) {
            size_t start = 0;
            size_t end = 0;
            size_t body_start = 0;
            size_t body_end = 0;
            char *indent = NULL;
            char *comment;
            char *next;
            if (find_html_comment(updated, strlen(updated), "related-sales", &start, &end, &body_start, &body_end) == 0) {
                indent = comment_indent(updated, start);
            } else {
                indent = strdup("");
                start = 0;
                end = 0;
            }
            comment = build_related_comment(indent ? indent : "", new_type, new_make, new_name, new_override);
            if (!comment) {
                free(indent);
                free(updated);
                free(new_type);
                free(new_make);
                free(new_name);
                free(new_override);
                free(title);
                free(description);
                free(image);
                free(type);
                free(date);
                free(vehicle_type);
                free(make_type);
                free(vehicle_name);
                free(override_ids);
                set_err(err, err_sz, "Could not build related-sales comment");
                return -1;
            }
            if (end > start) {
                next = replace_span(updated, strlen(updated), start, end, comment);
            } else {
                Buf buf;
                buf_init(&buf);
                buf_puts(&buf, comment);
                buf_puts(&buf, "\n\n");
                buf_puts(&buf, updated);
                next = buf_steal(&buf);
            }
            free(comment);
            free(indent);
            free(updated);
            updated = next;
            changed = 1;
        }
        free(new_type);
        free(new_make);
        free(new_name);
        free(new_override);
    }
    if (changed) {
        if (write_if_changed(page_path, updated, strlen(updated), 0, NULL) != 0) {
            free(updated);
            set_err(err, err_sz, "Could not write the page");
            free(title);
            free(description);
            free(image);
            free(type);
            free(date);
            free(vehicle_type);
            free(make_type);
            free(vehicle_name);
            free(override_ids);
            return -1;
        }
        script = join_path3(repo, "scripts/page_values", "render_page_values.py");
        if (script) {
            char *argv[4];
            argv[0] = "python3";
            argv[1] = script;
            argv[2] = (char *)page_path;
            argv[3] = NULL;
            if (run_capture(argv, &output, &code) != 0 || code != 0) {
                set_errf(err, err_sz, "Page values were saved, but render_page_values.py failed: %s",
                         output && output[0] ? output : "python3 exited");
                free(script);
                free(output);
                free(updated);
                free(title);
                free(description);
                free(image);
                free(type);
                free(date);
                free(vehicle_type);
                free(make_type);
                free(vehicle_name);
                free(override_ids);
                return -1;
            }
            free(script);
            free(output);
        }
    }
    free(updated);
    free(title);
    free(description);
    free(image);
    free(type);
    free(date);
    free(vehicle_type);
    free(make_type);
    free(vehicle_name);
    free(override_ids);
    return 0;
}

int gallery_save_details(const char *repo_root, const char *section, const char *slug, const JsonValue *details,
                         const JsonValue *page_values, const JsonValue *related, char *err, size_t err_sz) {
    char folder[4096];
    int folder_rc = ensure_folder(repo_root, section, slug, folder, sizeof(folder), err, err_sz);
    if (folder_rc != 0) {
        return folder_rc;
    }
    if (strcmp(section, "sales") == 0) {
        char *index_path = join_path(folder, "index.php");
        GallerySalesHeader header;
        size_t len = 0;
        char *text;
        char *rendered;
        size_t rendered_len = 0;
        int bool_value = 0;
        memset(&header, 0, sizeof(header));
        if (!index_path || gallery_read_sales_header(index_path, &header, err, err_sz) != 0) {
            free(index_path);
            return -1;
        }
        if (details && json_type(details) == JSON_OBJECT) {
            if (json_object_has(details, "vehicleName")) {
                free(header.vehicle_name);
                header.vehicle_name = json_text_dup(json_object_get(details, "vehicleName"));
            }
            if (json_object_has(details, "vehicleType")) {
                free(header.vehicle_type);
                header.vehicle_type = json_text_dup(json_object_get(details, "vehicleType"));
            }
            if (json_object_has(details, "makeType")) {
                free(header.make_type);
                header.make_type = json_text_dup(json_object_get(details, "makeType"));
            }
            if (json_object_has(details, "inventoryNumber")) {
                free(header.inventory_number);
                header.inventory_number = json_text_dup(json_object_get(details, "inventoryNumber"));
            }
            if (json_object_has(details, "price")) {
                free(header.price);
                header.price = json_text_dup(json_object_get(details, "price"));
            }
            if (json_object_has(details, "description")) {
                free(header.description);
                header.description = json_text_dup(json_object_get(details, "description"));
            }
            if (json_object_has(details, "doNotRender") && json_boolish(json_object_get(details, "doNotRender"), &bool_value) == 0) {
                header.do_not_render = bool_value;
            }
        }
        text = read_file(index_path, &len);
        rendered = text ? gallery_apply_sales_header(text, len, &header, &rendered_len, err, err_sz) : NULL;
        free(text);
        gallery_sales_header_free(&header);
        if (!rendered) {
            free(index_path);
            return err && err[0] ? (strstr(err, "DESC") ? 1 : -1) : -1;
        }
        if (write_if_changed(index_path, rendered, rendered_len, 0, NULL) != 0) {
            free(rendered);
            free(index_path);
            set_err(err, err_sz, "Could not write index.php");
            return -1;
        }
        free(rendered);
        free(index_path);
        return 0;
    }
    {
        char *primary = pick_primary(folder, slug);
        char *page_path;
        int rc;
        if (!primary) {
            set_err(err, err_sz, "No HTML page found");
            return 2;
        }
        page_path = join_path(folder, primary);
        free(primary);
        rc = update_page_file(repo_root, page_path, page_values, related, err, err_sz);
        free(page_path);
        return rc;
    }
}

int gallery_revert_folder(const char *repo_root, const char *section, const char *slug, char *err, size_t err_sz) {
    char folder[4096];
    int folder_rc = ensure_folder(repo_root, section, slug, folder, sizeof(folder), err, err_sz);
    char *rel;
    char *output = NULL;
    int code = 1;
    char *argv[7];
    if (folder_rc != 0) {
        return folder_rc;
    }
    rel = folder_rel(section, slug);
    if (!rel) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "checkout";
    argv[4] = "--";
    argv[5] = rel;
    argv[6] = NULL;
    if (run_capture(argv, &output, &code) != 0 || code != 0) {
        set_errf(err, err_sz, "git checkout failed: %s", output && output[0] ? output : "git exited");
        free(output);
        free(rel);
        return -1;
    }
    free(output);
    free(rel);
    return 0;
}
/* Studio file tree, raw page editor, common error check, widgets, and templates. */

static const char *ed_ga_id(void) {
    return config_get()->google_measurement_id;
}

static const char *ed_extreme_login(void) {
    return config_get()->extreme_login;
}

static const char *ed_extreme_host(void) {
    const char *host = config_get()->extreme_tracker_host;
    return (host && host[0]) ? host : "extreme-dm.com";
}

#define ED_PRICE_COMMA \
    "The price is an unquoted number with a comma, so this page cannot render. A comma in $price, such as 59,500, is a PHP syntax error and the vehicle page stops before it is shown."
#define ED_UNKNOWN_NAME \
    "This sales folder's index.php has no vehicle name the sales inventory can read. The listing shows Unknown."
#define ED_MISSING_INDEX "This sales folder has no index.php. The sales inventory lists it as Unknown."
#define ED_UNREADABLE_INDEX \
    "This sales folder's index.php could not be read. The sales inventory lists it as Unknown."
#define ED_SHARE_IMAGE \
    "This sales folder has no shareimage.jpg, so its share card and search snippet have no image."
#define ED_NOT_SET_UP "Page values are not set up. Add a page-values block at the top of this article."
#define ED_MISSING_SEO "The SEO meta tags are missing. This article has no generated-seo block."
#define ED_OUT_OF_DATE \
    "Page values are missing from the generated share tags. The generated block is absent or out of date."
#define ED_TYPE_LIST "'car', 'motorcycle', 'sidecar', or 'truck'"
#define ED_HEADER_INCLUDE "<!--#include virtual=\"/includes/header.html\"-->"
#define ED_SIMILAR_INCLUDE "<!--#include virtual=\"/php/similar_vehicles.php?embed=1\" -->"

static const char *const ED_SECTIONS[] = {"sales", "articles", "awards", "projects", "video",
                                         "whatsnew", "parts", "contact", "newsletter"};

static const char *ed_section_label(const char *section) {
    if (!section) {
        return "";
    }
    if (strcmp(section, "sales") == 0) {
        return "Sales";
    }
    if (strcmp(section, "articles") == 0) {
        return "Articles";
    }
    if (strcmp(section, "awards") == 0) {
        return "Awards";
    }
    if (strcmp(section, "projects") == 0) {
        return "Projects";
    }
    if (strcmp(section, "video") == 0) {
        return "Video";
    }
    if (strcmp(section, "whatsnew") == 0) {
        return "What's New";
    }
    if (strcmp(section, "parts") == 0) {
        return "Parts";
    }
    if (strcmp(section, "contact") == 0) {
        return "Contact";
    }
    if (strcmp(section, "newsletter") == 0) {
        return "Newsletter";
    }
    return section;
}

static int ed_content_section(const char *section) {
    size_t i;
    for (i = 0; i < sizeof(ED_SECTIONS) / sizeof(ED_SECTIONS[0]); i++) {
        if (section && strcmp(section, ED_SECTIONS[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_editable_section(const char *section) {
    if (!section || !section[0]) {
        return 0;
    }
    if (ed_content_section(section)) {
        return 1;
    }
    return strcmp(section, "includes") == 0 || strcmp(section, "style") == 0 ||
           strcmp(section, "php") == 0 || strcmp(section, "scripts") == 0 ||
           strcmp(section, "js") == 0 || strcmp(section, "css") == 0;
}

static int ed_watched_section(const char *section) {
    return section && (strcmp(section, "articles") == 0 || strcmp(section, "awards") == 0 ||
                       strcmp(section, "projects") == 0);
}

static int ed_skip_dir_name(const char *name) {
    return name && (strcasecmp(name, "slides") == 0 || strcasecmp(name, "thumbs") == 0 ||
                    strcasecmp(name, "res") == 0 || strcasecmp(name, "album") == 0 ||
                    strcasecmp(name, "temphidden") == 0);
}

static int ed_starts_ci(const char *text, const char *prefix) {
    size_t n;
    if (!text || !prefix) {
        return 0;
    }
    n = strlen(prefix);
    return strncasecmp(text, prefix, n) == 0;
}

static int ed_is_junk_name(const char *name) {
    if (!name || !name[0] || name[0] == '.') {
        return 1;
    }
    if (strcasecmp(name, "Thumbs.db") == 0 || strcasecmp(name, "ehthumbs.db") == 0 ||
        strcasecmp(name, "desktop.ini") == 0 || strcasecmp(name, "lifeboat.zip") == 0 ||
        strcasecmp(name, "album.rss") == 0 || strcasecmp(name, "manifest.jmf") == 0) {
        return 1;
    }
    if (ed_starts_ci(name, ".fuse_hidden") || ed_starts_ci(name, "folderimg") || ed_starts_ci(name, "folderimage")) {
        return 1;
    }
    return 0;
}

static int ed_listed_name(const char *name) {
    const char *dot;
    if (ed_is_junk_name(name)) {
        return 0;
    }
    dot = strrchr(name, '.');
    return dot && dot != name && dot[1] != '\0';
}

static int ed_is_text_name(const char *name) {
    return name && (ends_with_ci(name, ".txt") || ends_with_ci(name, ".json") || ends_with_ci(name, ".css") ||
                    ends_with_ci(name, ".js") || ends_with_ci(name, ".xml") || ends_with_ci(name, ".csv"));
}

static int ed_is_image_ext(const char *name) {
    return name && (ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg") || ends_with_ci(name, ".png") ||
                    ends_with_ci(name, ".gif") || ends_with_ci(name, ".webp") || ends_with_ci(name, ".svg") ||
                    ends_with_ci(name, ".bmp") || ends_with_ci(name, ".tif") || ends_with_ci(name, ".tiff"));
}

static int ed_is_video_ext(const char *name) {
    return name && (ends_with_ci(name, ".mp4") || ends_with_ci(name, ".m4v") || ends_with_ci(name, ".webm") ||
                    ends_with_ci(name, ".mov"));
}

static int ed_is_audio_ext(const char *name) {
    return name && (ends_with_ci(name, ".mp3") || ends_with_ci(name, ".wav"));
}

/* Lower rank is a better stand-in when a folder has no HTML or PHP page. */
static int ed_asset_rank(const char *name) {
    if (!ed_listed_name(name) || is_page_file(name)) {
        return 1000;
    }
    if (ends_with_ci(name, ".pdf")) {
        return 0;
    }
    if (ed_is_image_ext(name) || ed_is_video_ext(name)) {
        return 1;
    }
    if (ed_is_audio_ext(name)) {
        return 2;
    }
    if (ends_with_ci(name, ".swf") || ends_with_ci(name, ".rm")) {
        return 3;
    }
    if (ends_with_ci(name, ".doc") || ends_with_ci(name, ".docx") || ends_with_ci(name, ".rtf")) {
        return 4;
    }
    return 5;
}

static void ed_media_labels(const char *name, const char **api_type, const char **kind) {
    const char *type = "document";
    const char *media = "document";
    if (name && ends_with_ci(name, ".pdf")) {
        type = "pdf";
        media = "pdf";
    } else if (ed_is_image_ext(name)) {
        type = "image";
        media = "image";
    } else if (ed_is_video_ext(name)) {
        type = "video";
        media = "video";
    } else if (ed_is_audio_ext(name)) {
        type = "audio";
        media = "audio";
    }
    if (api_type) {
        *api_type = type;
    }
    if (kind) {
        *kind = media;
    }
}

static const char *ed_badge_type(const char *name) {
    if (!name) {
        return "file";
    }
    if (ends_with_ci(name, ".php")) {
        return "php";
    }
    if (ends_with_ci(name, ".shtml")) {
        return "shtml";
    }
    if (ends_with_ci(name, ".html") || ends_with_ci(name, ".htm")) {
        return "html";
    }
    if (ends_with_ci(name, ".txt")) {
        return "txt";
    }
    if (ends_with_ci(name, ".json")) {
        return "json";
    }
    if (ends_with_ci(name, ".css")) {
        return "css";
    }
    if (ends_with_ci(name, ".js")) {
        return "js";
    }
    if (ends_with_ci(name, ".xml")) {
        return "xml";
    }
    if (ends_with_ci(name, ".csv")) {
        return "csv";
    }
    if (ends_with_ci(name, ".pdf")) {
        return "pdf";
    }
    if (ends_with_ci(name, ".jpg") || ends_with_ci(name, ".jpeg")) {
        return "jpg";
    }
    if (ends_with_ci(name, ".png")) {
        return "png";
    }
    if (ends_with_ci(name, ".gif")) {
        return "gif";
    }
    if (ends_with_ci(name, ".webp")) {
        return "webp";
    }
    if (ends_with_ci(name, ".svg")) {
        return "svg";
    }
    if (ends_with_ci(name, ".bmp")) {
        return "bmp";
    }
    if (ends_with_ci(name, ".tif") || ends_with_ci(name, ".tiff")) {
        return "tiff";
    }
    if (ed_is_video_ext(name)) {
        return "video";
    }
    if (ed_is_audio_ext(name)) {
        return "audio";
    }
    if (ends_with_ci(name, ".swf") || ends_with_ci(name, ".rm")) {
        return "media";
    }
    if (ends_with_ci(name, ".doc") || ends_with_ci(name, ".docx")) {
        return "doc";
    }
    if (ends_with_ci(name, ".rtf")) {
        return "rtf";
    }
    return "file";
}

static const char *ed_file_type(const char *name) {
    return ed_badge_type(name);
}

static int ed_html_page(const char *name) {
    return name && (ends_with_ci(name, ".html") || ends_with_ci(name, ".htm") || ends_with_ci(name, ".shtml"));
}

static char *ed_clip(const char *text, size_t limit) {
    size_t n;
    char *out;
    if (!text) {
        return strdup("");
    }
    n = strlen(text);
    if (n <= limit) {
        return strdup(text);
    }
    if (limit < 3) {
        return strndup(text, limit);
    }
    out = strndup(text, limit);
    if (!out) {
        return NULL;
    }
    memcpy(out + limit - 3, "...", 3);
    return out;
}

static char *ed_html_escape(const char *text) {
    Buf buf;
    const unsigned char *p;
    buf_init(&buf);
    for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
        const char *rep = NULL;
        if (*p == '&') {
            rep = "&amp;";
        } else if (*p == '<') {
            rep = "&lt;";
        } else if (*p == '>') {
            rep = "&gt;";
        } else if (*p == '"') {
            rep = "&quot;";
        } else if (*p == '\'') {
            rep = "&#x27;";
        }
        if (rep) {
            if (buf_puts(&buf, rep) != 0) {
                buf_free(&buf);
                return NULL;
            }
        } else if (buf_putc(&buf, (char)*p) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    return buf_steal(&buf);
}

static int ed_buf_html(Buf *buf, const char *text) {
    char *esc = ed_html_escape(text);
    int rc;
    if (!esc) {
        return -1;
    }
    rc = buf_puts(buf, esc);
    free(esc);
    return rc;
}

static char *ed_one_line(const char *text) {
    Buf buf;
    const unsigned char *p;
    int pending = 0;
    int started = 0;
    buf_init(&buf);
    for (p = (const unsigned char *)(text ? text : ""); *p; p++) {
        if (*p == '\r') {
            continue;
        }
        if (isspace(*p)) {
            pending = started;
            continue;
        }
        if (pending && buf_putc(&buf, ' ') != 0) {
            buf_free(&buf);
            return NULL;
        }
        pending = 0;
        started = 1;
        if (buf_putc(&buf, (char)*p) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    return buf_steal(&buf);
}

static int ed_span_has(const char *start, const char *end, const char *needle) {
    size_t n;
    size_t len;
    size_t i;
    if (!start || !end || end < start || !needle) {
        return 0;
    }
    n = strlen(needle);
    len = (size_t)(end - start);
    if (n == 0 || n > len) {
        return n == 0;
    }
    for (i = 0; i + n <= len; i++) {
        if (memcmp(start + i, needle, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *ed_find_ci_span(const char *text, const char *needle) {
    return text && needle ? find_ci(text, needle) : NULL;
}

static char *ed_replace(const char *text, size_t start, size_t end, const char *insert) {
    return replace_span(text, text ? strlen(text) : 0, start, end, insert);
}

static int ed_mkdir_one(const char *path) {
    if (!path || !path[0]) {
        return -1;
    }
    if (dir_exists(path)) {
        return 0;
    }
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST) {
        return 0;
    }
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
        return 0;
    }
#endif
    return dir_exists(path) ? 0 : -1;
}

static int ed_fs_inside(const char *repo, const char *fs) {
    char *copy;
    char *slash;
    int ok;
    if (!repo || !fs) {
        return 0;
    }
    if (file_exists(fs) || dir_exists(fs)) {
        return path_within(repo, fs);
    }
    copy = strdup(fs);
    if (!copy) {
        return 0;
    }
    slash = strrchr(copy, '/');
    if (!slash || slash == copy) {
        free(copy);
        return 0;
    }
    *slash = '\0';
    ok = dir_exists(copy) && path_within(repo, copy);
    free(copy);
    return ok;
}

typedef struct EdFile {
    char *fs;
    char *folder;
    char *public_path;
    char *section;
    char *slug;
    char *filename;
    char *text;
    int php;
    int missing;
    int media;
    int text_asset;
    long long file_size;
    int width;
    int height;
    const char *api_type;
    const char *media_kind;
} EdFile;

static void ed_file_clear(EdFile *file) {
    if (!file) {
        return;
    }
    free(file->fs);
    free(file->folder);
    free(file->public_path);
    free(file->section);
    free(file->slug);
    free(file->filename);
    free(file->text);
    memset(file, 0, sizeof(*file));
}

static int ed_segment_ok(const char *seg, size_t n) {
    size_t i;
    if (n == 0 || (n == 1 && seg[0] == '.') || (n == 2 && seg[0] == '.' && seg[1] == '.')) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)seg[i];
        if (ch < 0x20 || ch == 0x7F || ch == '/' || ch == '\\' || ch == '\0') {
            return 0;
        }
    }
    if (n >= 2 && seg[0] == '.' && seg[1] == '.') {
        return 0;
    }
    return 1;
}

static int ed_resolve(const char *repo, const char *public_path, EdFile *out, int require_file, char *err,
                      size_t err_sz) {
    char *decoded;
    const char *cursor;
    Buf rel;
    char *section = NULL;
    Buf slug;
    char *filename = NULL;
    int saw_section = 0;
    size_t len;

    memset(out, 0, sizeof(*out));
    if (!repo || !repo[0] || !public_path || public_path[0] != '/') {
        set_err(err, err_sz, "Path must start with / and stay inside a content section");
        return 1;
    }
    if (strchr(public_path, '\\') || strstr(public_path, "..")) {
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    decoded = url_decode(public_path, strlen(public_path));
    if (!decoded) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    if (strchr(decoded, '\\') || strstr(decoded, "..")) {
        free(decoded);
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    buf_init(&rel);
    buf_init(&slug);
    cursor = decoded;
    while (*cursor == '/') {
        cursor++;
    }
    while (*cursor) {
        const char *slash = strchr(cursor, '/');
        size_t n = slash ? (size_t)(slash - cursor) : strlen(cursor);
        if (!ed_segment_ok(cursor, n)) {
            buf_free(&rel);
            buf_free(&slug);
            free(section);
            free(filename);
            free(decoded);
            set_err(err, err_sz, "Path is not allowed");
            return 1;
        }
        if (!saw_section) {
            section = strndup(cursor, n);
            saw_section = 1;
        } else if (!slash) {
            free(filename);
            filename = strndup(cursor, n);
        } else {
            if (slug.len && buf_putc(&slug, '/') != 0) {
                buf_free(&rel);
                buf_free(&slug);
                free(section);
                free(filename);
                free(decoded);
                set_err(err, err_sz, "out of memory");
                return -1;
            }
            if (buf_append(&slug, cursor, n) != 0) {
                buf_free(&rel);
                buf_free(&slug);
                free(section);
                free(filename);
                free(decoded);
                set_err(err, err_sz, "out of memory");
                return -1;
            }
        }
        if (buf_putc(&rel, '/') != 0 || buf_append(&rel, cursor, n) != 0) {
            buf_free(&rel);
            buf_free(&slug);
            free(section);
            free(filename);
            free(decoded);
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        if (!slash) {
            break;
        }
        cursor = slash + 1;
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }
    }
    free(decoded);
    if (!filename && section && (is_page_file(section) || ed_is_text_name(section))) {
        filename = section;
        section = strdup("");
    }
    if (!section || (!ed_editable_section(section) && section[0] != '\0') || !filename || !ed_listed_name(filename)) {
        buf_free(&rel);
        buf_free(&slug);
        free(section);
        free(filename);
        set_err(err, err_sz, "Open an .html, .htm, .shtml, .php, text, or media file inside a content section");
        return 1;
    }
    len = strlen(repo) + rel.len + 1;
    out->fs = (char *)malloc(len);
    if (!out->fs) {
        buf_free(&rel);
        buf_free(&slug);
        free(section);
        free(filename);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    memcpy(out->fs, repo, strlen(repo));
    memcpy(out->fs + strlen(repo), rel.data ? rel.data : "", rel.len);
    out->fs[strlen(repo) + rel.len] = '\0';
    out->public_path = rel.data ? strdup(rel.data) : NULL;
    buf_free(&rel);
    out->section = section;
    out->slug = slug.data ? strdup(slug.data) : strdup("");
    buf_free(&slug);
    out->filename = filename;
    out->php = ends_with_ci(filename, ".php");
    out->text_asset = ed_is_text_name(filename);
    out->media = !is_page_file(filename) && !out->text_asset;
    if (out->media) {
        ed_media_labels(filename, &out->api_type, &out->media_kind);
    }
    if (!out->public_path || !out->slug) {
        ed_file_clear(out);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    {
        char *slash = strrchr(out->fs, '/');
        out->folder = slash ? strndup(out->fs, (size_t)(slash - out->fs)) : strdup(out->fs);
    }
    if (!out->folder || !ed_fs_inside(repo, out->fs)) {
        ed_file_clear(out);
        set_err(err, err_sz, "Path is outside the repository");
        return 1;
    }
    if (!file_exists(out->fs)) {
        out->missing = 1;
        if (require_file) {
            set_err(err, err_sz, out->media || out->text_asset ? "File not found" : "Page not found");
            return 2;
        }
        return 0;
    }
    if (out->media) {
        struct stat st;
        out->text = NULL;
        out->width = 0;
        out->height = 0;
        if (stat(out->fs, &st) == 0 && S_ISREG(st.st_mode)) {
            out->file_size = (long long)st.st_size;
        }
        if (out->media_kind && strcmp(out->media_kind, "image") == 0) {
            gallery_image_size(out->fs, &out->width, &out->height);
        }
        return 0;
    }
    out->text = read_file(out->fs, NULL);
    if (!out->text) {
        out->missing = 1;
        if (require_file) {
            ed_file_clear(out);
            set_err(err, err_sz, out->text_asset ? "File not found" : "Page not found");
            return 2;
        }
    }
    return 0;
}

static char *ed_public_join(const char *section, const char *slug, const char *filename) {
    Buf buf;
    buf_init(&buf);
    if (buf_putc(&buf, '/') != 0 || buf_puts(&buf, section ? section : "") != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (slug && slug[0]) {
        if (buf_putc(&buf, '/') != 0 || buf_puts(&buf, slug) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    if (filename && filename[0]) {
        if (buf_putc(&buf, '/') != 0 || buf_puts(&buf, filename) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    return buf_steal(&buf);
}

static const char *ed_var_at(const char *text, const char *name) {
    size_t n;
    const char *p;
    if (!text || !name) {
        return NULL;
    }
    n = strlen(name);
    p = text;
    while ((p = strchr(p, '$')) != NULL) {
        unsigned char next;
        if (strncmp(p + 1, name, n) == 0) {
            next = (unsigned char)p[1 + n];
            if (!isalnum(next) && next != '_') {
                return p;
            }
        }
        p++;
    }
    return NULL;
}

static int ed_has_assign(const char *text, const char *name) {
    const char *p = ed_var_at(text, name);
    const char *q;
    if (!p) {
        return 0;
    }
    q = p + 1 + strlen(name);
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    return *q == '=' && q[1] != '=';
}

static int ed_line_price_comma(const char *line, size_t len) {
    size_t i = 0;
    size_t stop;
    size_t q;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (i + 6 > len || memcmp(line + i, "$price", 6) != 0) {
        return 0;
    }
    i += 6;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    if (i >= len || line[i] != '=') {
        return 0;
    }
    i++;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) {
        i++;
    }
    stop = i;
    while (stop < len && line[stop] != '\n' && line[stop] != '\r' && line[stop] != ';' && line[stop] != '\'' &&
           line[stop] != '"') {
        stop++;
    }
    if (stop < len && (line[stop] == '\'' || line[stop] == '"')) {
        return 0;
    }
    for (q = i; q < stop; q++) {
        size_t r;
        if (!isdigit((unsigned char)line[q])) {
            continue;
        }
        r = q + 1;
        while (r < stop && isspace((unsigned char)line[r])) {
            r++;
        }
        if (r < stop && line[r] == ',') {
            r++;
            while (r < stop && isspace((unsigned char)line[r])) {
                r++;
            }
            if (r < stop && isdigit((unsigned char)line[r])) {
                return 1;
            }
        }
    }
    return 0;
}

static int ed_price_comma(const char *text) {
    const char *p = text ? text : "";
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (ed_line_price_comma(p, n)) {
            return 1;
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

static int ed_hidden(const char *text) {
    const char *p = ed_var_at(text, "doNotRender");
    const char *q;
    if (!p) {
        return 0;
    }
    q = p + strlen("$doNotRender");
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    if (*q != '=') {
        return 0;
    }
    q++;
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    return strncasecmp(q, "true", 4) == 0 && !isalnum((unsigned char)q[4]);
}

static char *ed_clean_label(const char *value, size_t limit) {
    Buf buf;
    const unsigned char *p;
    int pending = 0;
    int started = 0;
    char *collapsed;
    char *shy;
    buf_init(&buf);
    for (p = (const unsigned char *)(value ? value : ""); *p; p++) {
        if (*p < 0x20 || *p == 0x7F) {
            pending = started;
            continue;
        }
        if (isspace(*p)) {
            pending = started;
            continue;
        }
        if (pending && buf_putc(&buf, ' ') != 0) {
            buf_free(&buf);
            return strdup("");
        }
        pending = 0;
        started = 1;
        if (buf_putc(&buf, (char)*p) != 0) {
            buf_free(&buf);
            return strdup("");
        }
    }
    collapsed = buf_steal(&buf);
    shy = strip_shy(collapsed ? collapsed : "");
    free(collapsed);
    if (!shy) {
        return strdup("");
    }
    if (limit && strlen(shy) > limit) {
        char *clipped = ed_clip(shy, limit);
        free(shy);
        return clipped ? clipped : strdup("");
    }
    return shy;
}

static int ed_valid_vehicle_type(const char *value) {
    return value && (strcmp(value, "car") == 0 || strcmp(value, "motorcycle") == 0 || strcmp(value, "sidecar") == 0 ||
                     strcmp(value, "truck") == 0);
}

static char *ed_vehicle_type_message(const char *text) {
    const char *p = text ? text : "";
    const char *line_value = NULL;
    size_t line_len = 0;
    int have_quoted_line = 0;
    char *shown;
    char message[640];
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        size_t i = 0;
        while (i < n && (p[i] == ' ' || p[i] == '\t')) {
            i++;
        }
        if (i + 12 <= n && memcmp(p + i, "$vehicleType", 12) == 0) {
            size_t q = i + 12;
            while (q < n && (p[q] == ' ' || p[q] == '\t')) {
                q++;
            }
            if (q < n && p[q] == '=') {
                q++;
                while (q < n && (p[q] == ' ' || p[q] == '\t')) {
                    q++;
                }
                if (q < n && (p[q] == '\'' || p[q] == '"')) {
                    char quote = p[q++];
                    size_t start = q;
                    while (q < n && p[q] != quote) {
                        q++;
                    }
                    if (q < n && p[q] == quote) {
                        size_t semi = q + 1;
                        while (semi < n && (p[semi] == ' ' || p[semi] == '\t')) {
                            semi++;
                        }
                        if (semi < n && p[semi] == ';') {
                            have_quoted_line = 1;
                            line_value = p + start;
                            line_len = q - start;
                        }
                    }
                }
            }
            break;
        }
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    if (have_quoted_line) {
        char *raw = strndup(line_value, line_len);
        char *clean;
        if (!raw) {
            return NULL;
        }
        clean = ed_clean_label(raw, 80);
        free(raw);
        if (!clean || !clean[0]) {
            free(clean);
            snprintf(message, sizeof(message),
                     "$vehicleType is empty. Use %s so the sales page type filter can match this vehicle.", ED_TYPE_LIST);
            return strdup(message);
        }
        if (!ed_valid_vehicle_type(clean)) {
            snprintf(message, sizeof(message),
                     "$vehicleType is '%s', which is not a valid vehicle type. Use %s so the sales page type filter can match this vehicle.",
                     clean, ED_TYPE_LIST);
            free(clean);
            return strdup(message);
        }
        free(clean);
    } else {
        const char *var = ed_var_at(text, "vehicleType");
        if (var) {
            const char *q = var + strlen("$vehicleType");
            const char *end;
            char *raw;
            while (*q && isspace((unsigned char)*q)) {
                q++;
            }
            if (*q == '=') {
                q++;
                while (*q && isspace((unsigned char)*q)) {
                    q++;
                }
                end = q;
                while (*end && *end != ';' && *end != '\n' && *end != '\r') {
                    end++;
                }
                raw = strndup(q, (size_t)(end - q));
                if (raw) {
                    char *trim = trim_dup(raw, strlen(raw));
                    char *clean;
                    size_t tn;
                    free(raw);
                    tn = trim ? strlen(trim) : 0;
                    if (trim && tn >= 2 && ((trim[0] == '\'' && trim[tn - 1] == '\'') || (trim[0] == '"' && trim[tn - 1] == '"'))) {
                        memmove(trim, trim + 1, tn - 2);
                        trim[tn - 2] = '\0';
                    }
                    clean = ed_clean_label(trim ? trim : "", 80);
                    free(trim);
                    if (clean && clean[0]) {
                        snprintf(message, sizeof(message),
                                 "$vehicleType is '%s', which the sales page cannot read as a valid vehicle type. Use a quoted %s.",
                                 clean, ED_TYPE_LIST);
                        free(clean);
                        return strdup(message);
                    }
                    free(clean);
                }
            }
        }
        snprintf(message, sizeof(message),
                 "$vehicleType is missing. Use %s so the sales page type filter can match this vehicle.", ED_TYPE_LIST);
        return strdup(message);
    }
    {
        const char *var = ed_var_at(text, "vehicleType");
        char *sales = NULL;
        if (var) {
            const char *q = var + strlen("$vehicleType");
            while (*q && isspace((unsigned char)*q)) {
                q++;
            }
            if (*q == '=') {
                q++;
                while (*q && isspace((unsigned char)*q)) {
                    q++;
                }
                if (*q == '\'' || *q == '"') {
                    char quote = *q++;
                    const char *start = q;
                    while (*q && *q != quote) {
                        q++;
                    }
                    sales = strndup(start, (size_t)(q - start));
                }
            }
        }
        shown = ed_clean_label(sales ? sales : "", 80);
        free(sales);
        if (!ed_valid_vehicle_type(shown)) {
            if (!shown || !shown[0]) {
                free(shown);
                snprintf(message, sizeof(message),
                         "$vehicleType is missing. Use %s so the sales page type filter can match this vehicle.", ED_TYPE_LIST);
                return strdup(message);
            }
            snprintf(message, sizeof(message),
                     "$vehicleType is '%s', which is not a valid vehicle type. Use %s so the sales page type filter can match this vehicle.",
                     shown, ED_TYPE_LIST);
            free(shown);
            return strdup(message);
        }
        free(shown);
    }
    return NULL;
}

static char *ed_id_key(const char *raw) {
    char *clean;
    char *trim;
    size_t i;
    const char *p;
    if (!raw) {
        return strdup("");
    }
    clean = (char *)malloc(strlen(raw) + 1);
    if (!clean) {
        return NULL;
    }
    p = raw;
    i = 0;
    for (; *p; p++) {
        unsigned char ch = (unsigned char)*p;
        if (ch < 0x20 || ch == 0x7F) {
            continue;
        }
        clean[i++] = (char)ch;
    }
    clean[i] = '\0';
    trim = trim_dup(clean, strlen(clean));
    free(clean);
    if (!trim) {
        return NULL;
    }
    if (!trim[0]) {
        return trim;
    }
    for (p = trim; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            return trim;
        }
    }
    p = trim;
    while (*p == '0') {
        p++;
    }
    if (*p == '\0') {
        free(trim);
        return strdup("0");
    }
    {
        char *key = strdup(p);
        free(trim);
        return key;
    }
}

static int ed_quoted_name_present(const char *text) {
    const char *var = ed_var_at(text, "vehicleName");
    const char *q;
    char quote;
    if (!var) {
        return 0;
    }
    q = var + strlen("$vehicleName");
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    if (*q != '=') {
        return 0;
    }
    q++;
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    if (*q != '\'' && *q != '"') {
        return 0;
    }
    quote = *q++;
    if (*q == '\0' || *q == quote) {
        return 0;
    }
    while (*q && *q != quote) {
        q++;
    }
    if (*q != quote) {
        return 0;
    }
    q++;
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    return *q == ';';
}

typedef struct EdInvRow {
    char *segment;
    char *key;
    char *raw;
    char *name;
    char *path;
    int hidden;
} EdInvRow;

typedef struct EdDupRow {
    char *path;
    char *title;
    char *description;
} EdDupRow;

typedef struct EdAudit {
    EdInvRow *inv;
    size_t inv_n;
    size_t inv_cap;
    EdDupRow *dups;
    size_t dup_n;
    size_t dup_cap;
    StrList ign_exact;
    StrList ign_prefix;
    int ignore_ok;
    int pages;
    int inventory;
} EdAudit;

static void ed_audit_init(EdAudit *audit) {
    memset(audit, 0, sizeof(*audit));
    sl_init(&audit->ign_exact);
    sl_init(&audit->ign_prefix);
}

static void ed_audit_free(EdAudit *audit) {
    size_t i;
    if (!audit) {
        return;
    }
    for (i = 0; i < audit->inv_n; i++) {
        free(audit->inv[i].segment);
        free(audit->inv[i].key);
        free(audit->inv[i].raw);
        free(audit->inv[i].name);
        free(audit->inv[i].path);
    }
    free(audit->inv);
    for (i = 0; i < audit->dup_n; i++) {
        free(audit->dups[i].path);
        free(audit->dups[i].title);
        free(audit->dups[i].description);
    }
    free(audit->dups);
    sl_free(&audit->ign_exact);
    sl_free(&audit->ign_prefix);
    memset(audit, 0, sizeof(*audit));
}

static int ed_inv_add(EdAudit *audit, const char *segment, const char *key, const char *raw, const char *name,
                      const char *path, int hidden) {
    EdInvRow *row;
    if (audit->inv_n == audit->inv_cap) {
        size_t cap = audit->inv_cap ? audit->inv_cap * 2 : 32;
        EdInvRow *next = (EdInvRow *)realloc(audit->inv, cap * sizeof(*next));
        if (!next) {
            return -1;
        }
        audit->inv = next;
        audit->inv_cap = cap;
    }
    row = &audit->inv[audit->inv_n++];
    row->segment = strdup(segment ? segment : "");
    row->key = strdup(key ? key : "");
    row->raw = strdup(raw ? raw : "");
    row->name = strdup(name ? name : "");
    row->path = strdup(path ? path : "");
    row->hidden = hidden;
    return (row->segment && row->key && row->raw && row->name && row->path) ? 0 : -1;
}

static int ed_dup_add(EdAudit *audit, const char *path, const char *title, const char *description) {
    EdDupRow *row;
    if (audit->dup_n == audit->dup_cap) {
        size_t cap = audit->dup_cap ? audit->dup_cap * 2 : 32;
        EdDupRow *next = (EdDupRow *)realloc(audit->dups, cap * sizeof(*next));
        if (!next) {
            return -1;
        }
        audit->dups = next;
        audit->dup_cap = cap;
    }
    row = &audit->dups[audit->dup_n++];
    row->path = strdup(path ? path : "");
    row->title = strdup(title ? title : "");
    row->description = strdup(description ? description : "");
    return (row->path && row->title && row->description) ? 0 : -1;
}

static void ed_load_ignore(EdAudit *audit, const char *repo) {
    char *path = join_path3(repo, "scripts/page_values", "ignore.txt");
    char *text;
    const char *p;
    audit->ignore_ok = 0;
    if (!path) {
        return;
    }
    text = read_file(path, NULL);
    free(path);
    if (!text) {
        return;
    }
    audit->ignore_ok = 1;
    p = text;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char *line = strndup(p, n);
        char *hash;
        char *trim;
        if (!line) {
            break;
        }
        if (n && line[n - 1] == '\r') {
            line[n - 1] = '\0';
        }
        hash = strchr(line, '#');
        if (hash) {
            *hash = '\0';
        }
        trim = trim_dup(line, strlen(line));
        free(line);
        if (trim && trim[0] && !strstr(trim, "..")) {
            char *cursor = trim;
            size_t tn;
            while (*cursor == '/' || *cursor == '\\') {
                cursor++;
            }
            for (tn = 0; cursor[tn]; tn++) {
                if (cursor[tn] == '\\') {
                    cursor[tn] = '/';
                }
            }
            if (cursor[0]) {
                tn = strlen(cursor);
                if (cursor[tn - 1] == '/') {
                    sl_add(&audit->ign_prefix, cursor);
                } else {
                    sl_add(&audit->ign_exact, cursor);
                }
            }
        }
        free(trim);
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    free(text);
}

static int ed_ignored(const EdAudit *audit, const char *relative) {
    size_t i;
    if (!audit || !relative) {
        return 0;
    }
    for (i = 0; i < audit->ign_exact.count; i++) {
        if (strcmp(audit->ign_exact.items[i], relative) == 0) {
            return 1;
        }
    }
    for (i = 0; i < audit->ign_prefix.count; i++) {
        if (strncmp(relative, audit->ign_prefix.items[i], strlen(audit->ign_prefix.items[i])) == 0) {
            return 1;
        }
    }
    return 0;
}

static int ed_rel_watched(const char *relative) {
    const char *p;
    if (!relative || !ed_html_page(relative)) {
        return 0;
    }
    if (!(strncmp(relative, "articles/", 9) == 0 || strncmp(relative, "awards/", 7) == 0 ||
          strncmp(relative, "projects/", 9) == 0 || strcmp(relative, "articles") == 0 ||
          strcmp(relative, "awards") == 0 || strcmp(relative, "projects") == 0)) {
        return 0;
    }
    p = relative;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t n = slash ? (size_t)(slash - p) : strlen(p);
        char *part = strndup(p, n);
        int skip;
        if (!part) {
            return 0;
        }
        skip = ed_skip_dir_name(part);
        free(part);
        if (skip) {
            return 0;
        }
        if (!slash) {
            break;
        }
        p = slash + 1;
    }
    return 1;
}

static int ed_is_jalbum_text(const char *text) {
    return text && (strstr(text, "Generated by jAlbum") != NULL || strstr(text, "jalbum.net") != NULL);
}

static int ed_is_mailchimp(const char *text) {
    return text && (strstr(text, "mcnPreviewText") != NULL || strstr(text, "mailchi.mp") != NULL);
}

static int ed_handcrafted_sibling(const char *folder, const char *self_name) {
    StrList names;
    size_t i;
    int found = 0;
    sl_init(&names);
    if (!folder || list_names(folder, 0, 0, &names) != 0) {
        sl_free(&names);
        return 0;
    }
    for (i = 0; i < names.count && !found; i++) {
        char *path;
        char *text;
        if (!is_page_file(names.items[i]) || (self_name && strcmp(names.items[i], self_name) == 0)) {
            continue;
        }
        if (!ed_html_page(names.items[i])) {
            continue;
        }
        path = join_path(folder, names.items[i]);
        text = path ? read_file(path, NULL) : NULL;
        free(path);
        if (text && !ed_is_jalbum_text(text) && !ed_is_mailchimp(text)) {
            found = 1;
        }
        free(text);
    }
    sl_free(&names);
    return found;
}

static char *ed_php_assign_raw(const char *text, const char *name) {
    const char *var = ed_var_at(text, name);
    const char *q;
    const char *end;
    char quote;
    if (!var) {
        return strdup("");
    }
    q = var + 1 + strlen(name);
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    if (*q != '=') {
        return strdup("");
    }
    q++;
    while (*q && isspace((unsigned char)*q)) {
        q++;
    }
    if (*q == '\'' || *q == '"') {
        quote = *q++;
        end = q;
        while (*end && *end != quote) {
            end++;
        }
        return strndup(q, (size_t)(end - q));
    }
    end = q;
    while (*end && *end != ';' && *end != '\n' && *end != '\r') {
        end++;
    }
    return trim_dup(q, (size_t)(end - q));
}

static void ed_scan_sales(EdAudit *audit, const char *repo) {
    char *sales = join_path(repo, "sales");
    StrList dirs;
    size_t i;
    sl_init(&dirs);
    if (!sales || list_names(sales, 1, 0, &dirs) != 0) {
        free(sales);
        sl_free(&dirs);
        return;
    }
    for (i = 0; i < dirs.count; i++) {
        char *folder = join_path(sales, dirs.items[i]);
        char *index = folder ? join_path(folder, "index.php") : NULL;
        char *text = index ? read_file(index, NULL) : NULL;
        char *raw;
        char *key;
        char *name;
        char *path;
        int hidden;
        if (!text) {
            free(folder);
            free(index);
            continue;
        }
        hidden = ed_hidden(text);
        raw = ed_php_assign_raw(text, "inventoryNumber");
        key = ed_id_key(raw);
        name = ed_php_assign_raw(text, "vehicleName");
        path = ed_public_join("sales", dirs.items[i], "index.php");
        if (key && key[0] && path) {
            char *plain = ed_clean_label(name ? name : "", 0);
            char *shown_raw = ed_clean_label(raw ? raw : "", 80);
            ed_inv_add(audit, dirs.items[i], key, shown_raw ? shown_raw : "", plain ? plain : "", path, hidden);
            free(plain);
            free(shown_raw);
        }
        free(raw);
        free(key);
        free(name);
        free(path);
        free(text);
        free(folder);
        free(index);
    }
    free(sales);
    sl_free(&dirs);
}

typedef struct EdValues {
    int present;
    int count;
    char *title;
    char *description;
    char *image;
    char *type;
    char *date;
    char *error;
} EdValues;

static void ed_values_clear(EdValues *values) {
    if (!values) {
        return;
    }
    free(values->title);
    free(values->description);
    free(values->image);
    free(values->type);
    free(values->date);
    free(values->error);
    memset(values, 0, sizeof(*values));
}

static int ed_known_page_field(const char *key) {
    return key && (strcmp(key, "title") == 0 || strcmp(key, "description") == 0 || strcmp(key, "image") == 0 ||
                   strcmp(key, "type") == 0 || strcmp(key, "date") == 0);
}

static void ed_parse_values(const char *text, EdValues *out) {
    size_t len = text ? strlen(text) : 0;
    size_t offset = 0;
    size_t start = 0;
    size_t end = 0;
    size_t body_start = 0;
    size_t body_end = 0;
    int seen_title = 0;
    int seen_description = 0;
    int seen_image = 0;
    int seen_type = 0;
    int seen_date = 0;
    const char *body;
    size_t body_len;
    size_t i;
    memset(out, 0, sizeof(*out));
    out->title = strdup("");
    out->description = strdup("");
    out->image = strdup("");
    out->type = strdup("");
    out->date = strdup("");
    if (!text || !out->title) {
        return;
    }
    while (offset < len && find_html_comment(text + offset, len - offset, "page-values", &start, &end, &body_start, &body_end) == 0) {
        out->count++;
        if (out->count == 1) {
            body = text + offset + body_start;
            body_len = body_end - body_start;
            i = 0;
            while (i < body_len) {
                size_t line = i;
                size_t line_len;
                char *raw;
                char *colon;
                while (i < body_len && body[i] != '\n') {
                    i++;
                }
                line_len = i - line;
                if (line_len && body[line + line_len - 1] == '\r') {
                    line_len--;
                }
                raw = trim_dup(body + line, line_len);
                if (raw && raw[0]) {
                    colon = strchr(raw, ':');
                    if (!colon) {
                        free(out->error);
                        out->error = ed_clip("page-values line is not a field", 200);
                    } else if (!out->error) {
                        char *value_raw;
                        char *value;
                        *colon = '\0';
                        value_raw = trim_dup(colon + 1, strlen(colon + 1));
                        value = strip_shy(value_raw ? value_raw : "");
                        free(value_raw);
                        {
                            char *key = raw;
                            size_t k;
                            for (k = 0; key[k]; k++) {
                                if (key[k] >= 'A' && key[k] <= 'Z') {
                                    key[k] = (char)(key[k] - 'A' + 'a');
                                }
                            }
                            if (!ed_known_page_field(key)) {
                                char tmp[160];
                                snprintf(tmp, sizeof(tmp), "unknown page-values field: %s", key);
                                out->error = strdup(tmp);
                            } else if ((strcmp(key, "title") == 0 && seen_title) ||
                                       (strcmp(key, "description") == 0 && seen_description) ||
                                       (strcmp(key, "image") == 0 && seen_image) || (strcmp(key, "type") == 0 && seen_type) ||
                                       (strcmp(key, "date") == 0 && seen_date)) {
                                char tmp[160];
                                snprintf(tmp, sizeof(tmp), "duplicate page-values field: %s", key);
                                out->error = strdup(tmp);
                            } else if (!value || !value[0]) {
                                char tmp[160];
                                snprintf(tmp, sizeof(tmp), "page-values field is empty: %s", key);
                                out->error = strdup(tmp);
                            } else if (strcmp(key, "title") == 0) {
                                free(out->title);
                                out->title = strdup(value);
                                seen_title = 1;
                            } else if (strcmp(key, "description") == 0) {
                                free(out->description);
                                out->description = strdup(value);
                                seen_description = 1;
                            } else if (strcmp(key, "image") == 0) {
                                free(out->image);
                                out->image = strdup(value);
                                seen_image = 1;
                            } else if (strcmp(key, "type") == 0) {
                                free(out->type);
                                out->type = strdup(value);
                                seen_type = 1;
                            } else if (strcmp(key, "date") == 0) {
                                free(out->date);
                                out->date = strdup(value);
                                seen_date = 1;
                            }
                        }
                        free(value);
                    }
                }
                free(raw);
                if (i < body_len && body[i] == '\n') {
                    i++;
                }
            }
        }
        offset += end;
    }
    if (out->count > 0) {
        out->present = 1;
    }
    if (out->count > 1 && !out->error) {
        char tmp[80];
        snprintf(tmp, sizeof(tmp), "expected one page-values comment, found %d", out->count);
        out->error = strdup(tmp);
    }
    if (out->present && !out->error) {
        if (!seen_title || !seen_description) {
            Buf miss;
            buf_init(&miss);
            buf_puts(&miss, "page-values is missing ");
            if (!seen_title) {
                buf_puts(&miss, "title");
            }
            if (!seen_title && !seen_description) {
                buf_puts(&miss, ", ");
            }
            if (!seen_description) {
                buf_puts(&miss, "description");
            }
            out->error = buf_steal(&miss);
        } else if (out->type[0] && !valid_page_type(out->type)) {
            out->error = strdup("type must be Article, NewsArticle, CollectionPage");
        } else if (seen_date && !valid_page_date(out->date)) {
            out->error = strdup("date must be YYYY-MM-DD");
        }
        if (!out->type[0]) {
            free(out->type);
            out->type = strdup("Article");
        }
    }
}

typedef struct EdRelated {
    int present;
    int has_include;
    char *vehicle_type;
    char *make_type;
    char *vehicle_name;
    char *override_ids;
} EdRelated;

static void ed_related_clear(EdRelated *related) {
    if (!related) {
        return;
    }
    free(related->vehicle_type);
    free(related->make_type);
    free(related->vehicle_name);
    free(related->override_ids);
    memset(related, 0, sizeof(*related));
}

static void ed_parse_related(const char *text, EdRelated *out) {
    size_t len = text ? strlen(text) : 0;
    size_t start = 0;
    size_t end = 0;
    size_t body_start = 0;
    size_t body_end = 0;
    memset(out, 0, sizeof(*out));
    out->vehicle_type = strdup("");
    out->make_type = strdup("");
    out->vehicle_name = strdup("");
    out->override_ids = strdup("");
    if (text && find_html_comment(text, len, "related-sales", &start, &end, &body_start, &body_end) == 0) {
        out->present = 1;
        parse_comment_fields(text + body_start, body_end - body_start, NULL, NULL, NULL, NULL, NULL, &out->vehicle_type,
                             &out->make_type, &out->vehicle_name, &out->override_ids, 1);
    }
    if (text && strstr(text, "similar_vehicles.php")) {
        out->has_include = 1;
    }
}

static int ed_count_images(const char *dir) {
    StrList names;
    int count = 0;
    sl_init(&names);
    if (dir && dir_exists(dir) && list_names(dir, 0, 1, &names) == 0) {
        count = (int)names.count;
    }
    sl_free(&names);
    return count;
}

static char *ed_image_fs(const char *repo, const char *folder, const char *image) {
    if (!image || !image[0]) {
        return NULL;
    }
    if (strncmp(image, "https://", 8) == 0 || strncmp(image, "http://", 7) == 0) {
        return NULL;
    }
    if (image[0] == '/') {
        size_t n = strlen(repo) + strlen(image) + 1;
        char *path = (char *)malloc(n);
        if (!path) {
            return NULL;
        }
        snprintf(path, n, "%s%s", repo, image);
        return path;
    }
    return folder ? join_path(folder, image) : NULL;
}

static char *ed_image_public(const char *section, const char *slug, const char *image) {
    if (!image || !image[0]) {
        return strdup("");
    }
    if (strncmp(image, "https://", 8) == 0 || strncmp(image, "http://", 7) == 0 || image[0] == '/') {
        return strdup(image);
    }
    return ed_public_join(section, slug, image);
}

static void ed_nearby_same(const char *folder, const char *image, const char *wanted_fs, StrList *out) {
    const char *base;
    StrList dirs;
    size_t i;
    if (!folder || !image || !out) {
        return;
    }
    base = strrchr(image, '/');
    base = base ? base + 1 : image;
    if (!base[0] || strcmp(base, ".") == 0 || strcmp(base, "..") == 0) {
        return;
    }
    sl_init(&dirs);
    sl_add(&dirs, ".");
    {
        StrList subs;
        sl_init(&subs);
        if (list_names(folder, 1, 0, &subs) == 0) {
            for (i = 0; i < subs.count && out->count < 4; i++) {
                sl_add(&dirs, subs.items[i]);
            }
        }
        sl_free(&subs);
    }
    for (i = 0; i < dirs.count && out->count < 4; i++) {
        char *dir = strcmp(dirs.items[i], ".") == 0 ? strdup(folder) : join_path(folder, dirs.items[i]);
        char *candidate;
        if (!dir) {
            continue;
        }
        candidate = join_path(dir, base);
        if (candidate && file_exists(candidate) && (!wanted_fs || strcmp(candidate, wanted_fs) != 0)) {
            sl_add(out, dirs.items[i]);
        }
        free(candidate);
        free(dir);
    }
    sl_free(&dirs);
}

static char *ed_image_missing_message(const char *relative, const char *image, const char *looked, StrList *folders) {
    Buf buf;
    size_t i;
    buf_init(&buf);
    if (image && strchr(image, '/')) {
        buf_printf(&buf, "%s names image \"%s\", looked up at %s, and that file is not there.", relative ? relative : "",
                   image, looked ? looked : "");
    } else {
        buf_printf(&buf, "%s names image \"%s\" beside the page, and %s is not there.", relative ? relative : "",
                   image ? image : "", looked ? looked : "");
    }
    if (folders && folders->count) {
        buf_puts(&buf, " The same filename is in ");
        for (i = 0; i < folders->count && i < 3; i++) {
            const char *folder = folders->items[i];
            if (i) {
                buf_puts(&buf, i + 1 == folders->count || i == 2 ? (folders->count == 2 || i == 1 ? " and " : ", and ")
                                                               : ", ");
            }
            if (!folder[0] || strcmp(folder, ".") == 0) {
                buf_puts(&buf, "the page folder");
            } else {
                buf_puts(&buf, folder);
                buf_puts(&buf, "/");
            }
        }
        if (folders->count > 3) {
            char extra[32];
            snprintf(extra, sizeof(extra), ", and %zu more", folders->count - 3);
            buf_puts(&buf, extra);
        }
        buf_puts(&buf, ".");
    }
    {
        char *text = buf_steal(&buf);
        char *clipped = ed_clip(text ? text : "", 480);
        free(text);
        return clipped;
    }
}

static int ed_generated_current(const char *text, const char *title, const char *description) {
    const char *open_tag;
    const char *close_tag;
    char *share;
    char *esc_title;
    char *esc_desc;
    int ok = 0;
    if (!text || !title) {
        return 0;
    }
    open_tag = strstr(text, "<!-- generated-seo -->");
    close_tag = strstr(text, "<!-- /generated-seo -->");
    if (!open_tag || !close_tag || close_tag < open_tag) {
        return 0;
    }
    share = article_share_title(title);
    esc_title = ed_html_escape(share ? share : "");
    esc_desc = ed_html_escape(description ? description : "");
    ok = esc_title && ed_span_has(open_tag, close_tag, esc_title) &&
         (!description || !description[0] || (esc_desc && ed_span_has(open_tag, close_tag, esc_desc)));
    free(share);
    free(esc_title);
    free(esc_desc);
    return ok;
}

static int ed_python_stale(const char *repo, const char *fs_path) {
    char *script = join_path3(repo, "scripts/page_values", "render_page_values.py");
    char *argv[5];
    char *output = NULL;
    int code = 1;
    int stale = 0;
    if (!script) {
        return -1;
    }
    argv[0] = "python3";
    argv[1] = script;
    argv[2] = "--check";
    argv[3] = (char *)fs_path;
    argv[4] = NULL;
    if (run_capture(argv, &output, &code) != 0) {
        free(script);
        free(output);
        return -1;
    }
    stale = code != 0;
    free(script);
    free(output);
    return stale;
}

static int ed_run_render(const char *repo, const char *fs_path, char *err, size_t err_sz) {
    char *script = join_path3(repo, "scripts/page_values", "render_page_values.py");
    char *argv[4];
    char *output = NULL;
    int code = 1;
    if (!script) {
        set_err(err, err_sz, "render_page_values.py is missing");
        return -1;
    }
    argv[0] = "python3";
    argv[1] = script;
    argv[2] = (char *)fs_path;
    argv[3] = NULL;
    if (run_capture(argv, &output, &code) != 0 || code != 0) {
        set_errf(err, err_sz, "render_page_values.py failed: %s", output && output[0] ? output : "python3 exited");
        free(script);
        free(output);
        return -1;
    }
    free(script);
    free(output);
    return 0;
}

static char *ed_preview_body_path(const char *repo) {
    const char *home = settings_get_studio_home();
    const char *base = (home && home[0]) ? home : repo;
    char path[PATH_MAX];
    if (!base || !base[0]) return NULL;
    if (settings_ensure_dirs(base) != 0) return NULL;
    if (settings_install_path(base, "state/preview-body.html", path, sizeof path) != 0) return NULL;
    return strdup(path);
}

/* Render page-values SEO against a copy in studio/state. The open file stays untouched. */
static char *ed_render_memory(const char *repo, const char *page_fs, const char *content, char *err, size_t err_sz) {
    char *script;
    char *body;
    char *argv[6];
    char *output = NULL;
    int code = 1;
    if (!repo || !page_fs || !content) {
        set_err(err, err_sz, "content is required");
        return NULL;
    }
    script = join_path3(repo, "scripts/page_values", "render_page_values.py");
    body = ed_preview_body_path(repo);
    if (!body) {
        free(script);
        set_err(err, err_sz, "Could not prepare state");
        return NULL;
    }
    if (!script || !body) {
        free(script);
        free(body);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (atomic_write(body, content, strlen(content)) != 0) {
        free(script);
        free(body);
        set_err(err, err_sz, "Could not stage page values");
        return NULL;
    }
    argv[0] = "python3";
    argv[1] = script;
    argv[2] = "--body-file";
    argv[3] = body;
    argv[4] = (char *)page_fs;
    argv[5] = NULL;
    if (run_capture(argv, &output, &code) != 0 || code != 0 || !output) {
        set_errf(err, err_sz, "render_page_values.py failed: %s", output && output[0] ? output : "python3 exited");
        free(script);
        free(body);
        free(output);
        return NULL;
    }
    free(script);
    free(body);
    return output;
}

static int ed_run_newsletter_clean(const char *repo, char *err, size_t err_sz) {
    char *script = join_path3(repo, "scripts/newsletter", "clean_newsletter_archive.py");
    char *argv[4];
    char *output = NULL;
    int code = 1;
    if (!script) {
        set_err(err, err_sz, "clean_newsletter_archive.py is missing");
        return -1;
    }
    argv[0] = "python3";
    argv[1] = script;
    argv[2] = "--no-stage";
    argv[3] = NULL;
    if (run_capture(argv, &output, &code) != 0 || code != 0) {
        set_errf(err, err_sz, "clean_newsletter_archive.py failed: %s", output && output[0] ? output : "python3 exited");
        free(script);
        free(output);
        return -1;
    }
    free(script);
    free(output);
    return 0;
}

static void ed_walk_pages(EdAudit *audit, const char *repo, const char *section) {
    char *root = join_path(repo, section);
    StrList stack_fs;
    StrList stack_rel;
    sl_init(&stack_fs);
    sl_init(&stack_rel);
    if (root) {
        sl_add(&stack_fs, root);
        sl_add(&stack_rel, section);
    }
    free(root);
    while (stack_fs.count) {
        char *dir = stack_fs.items[stack_fs.count - 1];
        char *rel = stack_rel.items[stack_rel.count - 1];
        StrList files;
        StrList subs;
        size_t i;
        stack_fs.count--;
        stack_rel.count--;
        sl_init(&files);
        sl_init(&subs);
        if (list_names(dir, 0, 0, &files) == 0) {
            for (i = 0; i < files.count; i++) {
                char *relative;
                char *path;
                char *text;
                EdValues values;
                char *title_key;
                char *desc_key;
                char *public_path;
                if (!ed_html_page(files.items[i])) {
                    continue;
                }
                relative = ed_public_join(rel, NULL, files.items[i]);
                if (!relative) {
                    continue;
                }
                memmove(relative, relative + 1, strlen(relative));
                if (!ed_rel_watched(relative) || ed_ignored(audit, relative)) {
                    free(relative);
                    continue;
                }
                path = join_path(dir, files.items[i]);
                text = path ? read_file(path, NULL) : NULL;
                if (!text || ed_is_mailchimp(text) || (ed_is_jalbum_text(text) && ed_handcrafted_sibling(dir, files.items[i]))) {
                    free(text);
                    free(path);
                    free(relative);
                    continue;
                }
                ed_parse_values(text, &values);
                if (values.present && !values.error && values.title && values.title[0] && values.description &&
                    values.description[0]) {
                    title_key = collapse_ws(values.title);
                    desc_key = collapse_ws(values.description);
                    public_path = ed_public_join(rel, NULL, files.items[i]);
                    if (title_key && desc_key && public_path) {
                        ed_dup_add(audit, public_path, title_key, desc_key);
                    }
                    free(title_key);
                    free(desc_key);
                    free(public_path);
                }
                ed_values_clear(&values);
                free(text);
                free(path);
                free(relative);
            }
        }
        if (list_names(dir, 1, 0, &subs) == 0) {
            for (i = 0; i < subs.count; i++) {
                char *child;
                char *child_rel;
                if (ed_skip_dir_name(subs.items[i])) {
                    continue;
                }
                child = join_path(dir, subs.items[i]);
                child_rel = ed_public_join(rel, NULL, subs.items[i]);
                if (child && child_rel) {
                    memmove(child_rel, child_rel + 1, strlen(child_rel));
                    sl_add(&stack_fs, child);
                    sl_add(&stack_rel, child_rel);
                }
                free(child);
                free(child_rel);
            }
        }
        sl_free(&files);
        sl_free(&subs);
        free(dir);
        free(rel);
    }
    sl_free(&stack_fs);
    sl_free(&stack_rel);
}

static void ed_audit_build(EdAudit *audit, const char *repo, int want_pages, int want_inv) {
    ed_load_ignore(audit, repo);
    if (want_inv) {
        ed_scan_sales(audit, repo);
        audit->inventory = 1;
    }
    if (want_pages) {
        ed_walk_pages(audit, repo, "articles");
        ed_walk_pages(audit, repo, "awards");
        ed_walk_pages(audit, repo, "projects");
        audit->pages = 1;
    }
}

static char *ed_join_phrases(char **items, size_t count) {
    Buf buf;
    size_t i;
    buf_init(&buf);
    if (count == 0) {
        return strdup("");
    }
    if (count == 1) {
        return strdup(items[0] ? items[0] : "");
    }
    for (i = 0; i + 1 < count; i++) {
        if (i && buf_puts(&buf, ", ") != 0) {
            buf_free(&buf);
            return strdup("");
        }
        if (buf_puts(&buf, items[i] ? items[i] : "") != 0) {
            buf_free(&buf);
            return strdup("");
        }
    }
    if (count == 2) {
        if (buf_puts(&buf, " and ") != 0 || buf_puts(&buf, items[count - 1]) != 0) {
            buf_free(&buf);
            return strdup("");
        }
    } else if (buf_puts(&buf, ", and ") != 0 || buf_puts(&buf, items[count - 1]) != 0) {
        buf_free(&buf);
        return strdup("");
    }
    return buf_steal(&buf);
}

static char *ed_duplicate_inventory_message(const EdAudit *audit, const char *segment) {
    const EdInvRow *self = NULL;
    size_t i;
    size_t n = 0;
    char **labels = NULL;
    char *id;
    char *joined;
    char *message;
    Buf buf;
    if (!audit || !segment) {
        return NULL;
    }
    for (i = 0; i < audit->inv_n; i++) {
        if (!audit->inv[i].hidden && strcmp(audit->inv[i].segment, segment) == 0) {
            self = &audit->inv[i];
            break;
        }
    }
    if (!self || !self->key[0]) {
        return NULL;
    }
    for (i = 0; i < audit->inv_n; i++) {
        if (audit->inv[i].hidden || strcmp(audit->inv[i].key, self->key) != 0 ||
            strcmp(audit->inv[i].segment, self->segment) == 0) {
            continue;
        }
        n++;
    }
    if (!n) {
        return NULL;
    }
    labels = (char **)calloc(n, sizeof(*labels));
    if (!labels) {
        return NULL;
    }
    n = 0;
    for (i = 0; i < audit->inv_n; i++) {
        char *label;
        const char *name;
        if (audit->inv[i].hidden || strcmp(audit->inv[i].key, self->key) != 0 ||
            strcmp(audit->inv[i].segment, self->segment) == 0) {
            continue;
        }
        name = audit->inv[i].name[0] ? audit->inv[i].name : "Unknown";
        label = (char *)malloc(strlen(name) + strlen(audit->inv[i].path) + 8);
        if (!label) {
            break;
        }
        snprintf(label, strlen(name) + strlen(audit->inv[i].path) + 8, "%s (%s)", name, audit->inv[i].path);
        labels[n++] = label;
    }
    id = ed_clean_label(self->key, 80);
    joined = ed_join_phrases(labels, n);
    buf_init(&buf);
    buf_printf(&buf, "Inventory number %s is shared with %s.", id ? id : "", joined ? joined : "");
    buf_puts(&buf, " Each posted sales vehicle needs a unique inventory number.");
    message = buf_steal(&buf);
    {
        char *clipped = ed_clip(message ? message : "", 500);
        free(message);
        message = clipped;
    }
    for (i = 0; i < n; i++) {
        free(labels[i]);
    }
    free(labels);
    free(id);
    free(joined);
    return message;
}

static int ed_inventory_known(const EdAudit *audit, const char *raw) {
    char *key = ed_id_key(raw);
    size_t i;
    int found = 0;
    if (!key || !key[0] || !audit) {
        free(key);
        return 0;
    }
    for (i = 0; i < audit->inv_n; i++) {
        if (strcmp(audit->inv[i].key, key) == 0) {
            found = 1;
            break;
        }
    }
    free(key);
    return found;
}

static void ed_add_issue(JsonValue *errors, const char *id, const char *code, const char *severity, const char *title,
                         const char *message, int fixable) {
    JsonValue *item = json_object();
    char *clipped;
    if (!errors || !item) {
        json_free(item);
        return;
    }
    clipped = ed_clip(message ? message : "", 500);
    jput_str(item, "id", id);
    if (code && code[0]) {
        jput_str(item, "code", code);
    }
    jput_str(item, "severity", severity ? severity : "error");
    jput_str(item, "title", title ? title : "");
    jput_str(item, "message", clipped ? clipped : "");
    jput_bool(item, "fixable", fixable);
    json_array_add(errors, item);
    free(clipped);
}

static int ed_sales_vehicle(const EdFile *file) {
    return file && strcmp(file->section, "sales") == 0 && file->slug && file->slug[0] && file->filename &&
           strcmp(file->filename, "index.php") == 0 && !strchr(file->slug, '/');
}

static void ed_extreme_info(const char *text, int *present, char *pid, size_t pid_sz, int *noscript) {
    const char *hit;
    const char *pid_at;
    size_t i = 0;
    if (pid && pid_sz) {
        pid[0] = '\0';
    }
    if (present) {
        *present = 0;
    }
    if (noscript) {
        *noscript = 0;
    }
    if (!text) {
        return;
    }
    hit = strstr(text, "extreme-dm.com");
    if (!hit && config_get()->extreme_tracker_host[0]) {
        hit = strstr(text, config_get()->extreme_tracker_host);
    }
    if (!hit) {
        return;
    }
    if (ed_extreme_login()[0]) {
        char login_pat[96];
        snprintf(login_pat, sizeof(login_pat), "login=%s", ed_extreme_login());
        if (!strstr(text, login_pat)) {
            return;
        }
    } else if (!strstr(text, "login=")) {
        return;
    }
    if (present) {
        *present = 1;
    }
    pid_at = strstr(hit, "pid=");
    if (pid_at && pid && pid_sz) {
        pid_at += 4;
        while (pid_at[i] && pid_at[i] != '&' && pid_at[i] != '\'' && pid_at[i] != '"' && pid_at[i] != '<' &&
               pid_at[i] != ' ' && i + 1 < pid_sz) {
            pid[i] = pid_at[i];
            i++;
        }
        pid[i] = '\0';
    }
    if (noscript) {
        *noscript = strstr(text, "<noscript") && strstr(text, "extreme-dm.com") ? 1 : 0;
    }
}

static int ed_header_include(const char *text) {
    return text && strstr(text, "/includes/header.html") != NULL;
}

static int ed_traffic_header_call(const char *text) {
    return text && strstr(text, "traffic_public_header") != NULL;
}

static int ed_google_present(const char *text) {
    return text && (strstr(text, "googletagmanager.com/gtag/js") || strstr(text, "<!-- Google tag (gtag.js) -->"));
}

static void ed_copy_google_id(const char *text, char *out, size_t out_sz) {
    const char *hit;
    const char *p;
    size_t i = 0;
    if (!out || out_sz == 0) {
        return;
    }
    snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), ed_ga_id());
    if (!text) {
        return;
    }
    hit = strstr(text, "googletagmanager.com/gtag/js?id=");
    if (hit) {
        p = hit + strlen("googletagmanager.com/gtag/js?id=");
    } else {
        hit = strstr(text, "gtag('config', '");
        if (!hit) {
            hit = strstr(text, "gtag(\"config\", \"");
        }
        if (!hit) {
            return;
        }
        p = strchr(hit, '\'');
        if (!p) {
            p = strchr(hit, '"');
        }
        if (!p) {
            return;
        }
        p++;
    }
    while (p[i] && p[i] != '&' && p[i] != '\'' && p[i] != '"' && p[i] != ' ' && p[i] != '<' && i + 1 < out_sz) {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    if (!out[0]) {
        snprintf(out, out_sz, "%.*s", (int)(out_sz - 1), ed_ga_id());
    }
}

static void ed_gallery_issues(JsonValue *errors, const char *folder, const char *section) {
    char *slides = folder ? join_path(folder, "slides") : NULL;
    char *thumbs = folder ? join_path(folder, "thumbs") : NULL;
    char *deep = folder ? join_path(folder, "deep-data.json") : NULL;
    const char *base = folder ? strrchr(folder, '/') : NULL;
    int slides_on = ed_count_images(slides);
    int thumbs_on = ed_count_images(thumbs);
    int slides_dir = slides && dir_exists(slides);
    int thumbs_dir = thumbs && dir_exists(thumbs);
    int deep_file = deep && file_exists(deep);
    GImage *images = NULL;
    size_t count = 0;
    StrList documented;
    StrList slide_files;
    int from_html = 0;
    size_t page_refs = 0;
    size_t unlisted = 0;
    size_t s;
    sl_init(&documented);
    sl_init(&slide_files);
    if (slides_dir && thumbs_dir && slides_on != thumbs_on) {
        char message[180];
        snprintf(message, sizeof(message), "slides/ has %d images but thumbs/ has %d.", slides_on, thumbs_on);
        ed_add_issue(errors, "gallery-mismatch", "unequal", "error", "Slides and thumbs do not match", message, 0);
    }
    load_documented_images(folder, section, base ? base + 1 : "", &images, &count, NULL, &documented, &from_html, &page_refs,
                           NULL, 0);
    if (slides && list_names(slides, 0, 1, &slide_files) == 0) {
        for (s = 0; s < slide_files.count; s++) {
            if (!name_is_documented(slide_files.items[s], &documented)) {
                unlisted++;
            }
        }
    }
    if ((slides_dir || thumbs_dir) && !deep_file && !from_html && page_refs == 0 && (slides_on || thumbs_on)) {
        ed_add_issue(errors, "gallery-mismatch", "deep-data", "error", "Gallery data is missing",
                     "deep-data.json is missing, so this gallery's slides are not documented.", 0);
    } else if (unlisted) {
        char message[180];
        if (from_html || page_refs) {
            snprintf(message, sizeof(message), "%zu image(s) in slides/ are not in the gallery.", unlisted);
        } else {
            snprintf(message, sizeof(message), "%zu image(s) in slides/ are not listed in deep-data.json.", unlisted);
        }
        ed_add_issue(errors, "gallery-mismatch", "unlisted", "error", "Unlisted gallery slides", message, 0);
    }
    free_gimages(images, count);
    sl_free(&slide_files);
    sl_free(&documented);
    free(slides);
    free(thumbs);
    free(deep);
}

static int ed_has_meta_name(const char *head, const char *key) {
    const char *p = head ? head : "";
    size_t key_len = strlen(key);
    while ((p = strstr(p, key)) != NULL) {
        if ((p == head || !isalnum((unsigned char)p[-1])) && (p[key_len] == '"' || p[key_len] == '\'')) {
            return 1;
        }
        p += key_len;
    }
    return 0;
}

static char *ed_seo_gap(const char *text) {
    const char *head_open = text ? ed_find_ci_span(text, "<head") : NULL;
    const char *head_close = head_open ? ed_find_ci_span(head_open, "</head>") : NULL;
    const char *head = head_open ? head_open : (text ? text : "");
    const char *end = head_close ? head_close : head + strlen(head);
    int missing_title;
    int missing_desc;
    int missing_canonical;
    int missing_json;
    char *message;
    Buf buf;
    const char *parts[12];
    size_t n = 0;
    char og[160];
    char tw[160];
    missing_title = !ed_span_has(head, end, "<title");
    missing_desc = !ed_has_meta_name(head, "description");
    missing_canonical = !ed_span_has(head, end, "canonical");
    missing_json = !ed_span_has(head, end, "application/ld+json");
    if (missing_title) {
        parts[n++] = "a title";
    }
    if (missing_desc) {
        parts[n++] = "a meta description";
    }
    if (missing_canonical) {
        parts[n++] = "a canonical link";
    }
    {
        const char *og_names[] = {"title", "description", "URL", "image", "type"};
        const char *og_keys[] = {"og:title", "og:description", "og:url", "og:image", "og:type"};
        size_t i;
        size_t og_n = 0;
        Buf list;
        buf_init(&list);
        for (i = 0; i < 5; i++) {
            if (!ed_has_meta_name(head, og_keys[i])) {
                if (og_n && buf_puts(&list, og_n == 1 ? "" : "") != 0) {
                    buf_free(&list);
                    break;
                }
                if (og_n == 1 && buf_puts(&list, " and ") != 0) {
                    buf_free(&list);
                    break;
                }
                if (og_n > 1 && buf_puts(&list, ", ") != 0) {
                    buf_free(&list);
                    break;
                }
                if (buf_puts(&list, og_names[i]) != 0) {
                    buf_free(&list);
                    break;
                }
                og_n++;
            }
        }
        if (og_n && list.data) {
            snprintf(og, sizeof(og), "Open Graph %s", list.data);
            parts[n++] = og;
        }
        buf_free(&list);
    }
    {
        const char *tw_names[] = {"card", "title", "description", "image"};
        const char *tw_keys[] = {"twitter:card", "twitter:title", "twitter:description", "twitter:image"};
        size_t i;
        size_t tw_n = 0;
        Buf list;
        buf_init(&list);
        for (i = 0; i < 4; i++) {
            if (!ed_has_meta_name(head, tw_keys[i])) {
                if (tw_n == 1 && buf_puts(&list, " and ") != 0) {
                    buf_free(&list);
                    break;
                }
                if (tw_n > 1 && buf_puts(&list, ", ") != 0) {
                    buf_free(&list);
                    break;
                }
                if (buf_puts(&list, tw_names[i]) != 0) {
                    buf_free(&list);
                    break;
                }
                tw_n++;
            }
        }
        if (tw_n && list.data) {
            snprintf(tw, sizeof(tw), "Twitter %s", list.data);
            parts[n++] = tw;
        }
        buf_free(&list);
    }
    if (missing_json) {
        parts[n++] = "structured data";
    }
    if (!n) {
        return NULL;
    }
    buf_init(&buf);
    buf_puts(&buf, "SEO blocks are incomplete. Missing ");
    {
        size_t i;
        for (i = 0; i < n; i++) {
            if (i && i + 1 == n) {
                buf_puts(&buf, ", and ");
            } else if (i) {
                buf_puts(&buf, ", ");
            }
            buf_puts(&buf, parts[i]);
        }
    }
    buf_puts(&buf, ".");
    message = buf_steal(&buf);
    {
        char *clipped = ed_clip(message ? message : "", 480);
        free(message);
        return clipped;
    }
}

static JsonValue *ed_issues(const char *repo, const EdFile *file, const EdAudit *audit, int deep) {
    JsonValue *errors = json_array();
    const char *text = file && file->text ? file->text : "";
    int vehicle = ed_sales_vehicle(file);
    int hidden = vehicle && ed_hidden(text);
    if (!errors || !file) {
        return errors;
    }
    if (strcmp(file->section, "sales") == 0 && file->slug && file->slug[0] && !strchr(file->slug, '/') &&
        (!file->filename || strcmp(file->filename, "index.php") == 0)) {
        if (file->missing || !file_exists(file->fs)) {
            ed_add_issue(errors, "sales-missing-index", "sales-missing-index", "error", "Sales page is missing",
                         ED_MISSING_INDEX, 0);
        } else if (!file->text) {
            ed_add_issue(errors, "sales-missing-index", "sales-missing-index", "error", "Sales page is unreadable",
                         ED_UNREADABLE_INDEX, 0);
        } else if (vehicle || strcmp(file->filename, "index.php") == 0) {
            if (ed_price_comma(text)) {
                ed_add_issue(errors, "sales-index-parse", "sales-index-parse", "error", "Price cannot be parsed",
                             ED_PRICE_COMMA, 0);
            }
            if (!hidden) {
                if (!ed_quoted_name_present(text)) {
                    ed_add_issue(errors, "sales-index-parse", "sales-index-parse", "error", "Vehicle name is missing",
                                 ED_UNKNOWN_NAME, 0);
                }
                if (!ed_price_comma(text) && !ed_has_assign(text, "price")) {
                    ed_add_issue(errors, "sales-index-parse", "sales-index-parse", "error", "Price is missing",
                                 "$price is missing or unparseable, so this vehicle page cannot render.", 0);
                }
                if (!ed_has_assign(text, "description")) {
                    ed_add_issue(errors, "sales-index-parse", "sales-index-parse", "error", "Description is missing",
                                 "$description is missing or unparseable, so this vehicle page cannot render.", 0);
                }
                {
                    char *type_message = ed_vehicle_type_message(text);
                    if (type_message) {
                        ed_add_issue(errors, "sales-vehicle-type", "sales-vehicle-type", "error", "Vehicle type is not valid",
                                     type_message, 0);
                        free(type_message);
                    }
                }
                {
                    char *share = join_path(file->folder, "shareimage.jpg");
                    if (!share || !file_exists(share)) {
                        ed_add_issue(errors, "sales-missing-shareimage", "sales-missing-shareimage", "error",
                                     "Share image is missing", ED_SHARE_IMAGE, 0);
                    }
                    free(share);
                }
                {
                    char *dup = ed_duplicate_inventory_message(audit, file->slug);
                    if (dup) {
                        ed_add_issue(errors, "sales-duplicate-inventory", "sales-duplicate-inventory", "error",
                                     "Inventory number is duplicated", dup, 0);
                        free(dup);
                    }
                }
            }
            ed_gallery_issues(errors, file->folder, file->section);
        }
    }
    if (html_gallery_section(file->section) && file->folder && file->slug && file->slug[0]) {
        ed_gallery_issues(errors, file->folder, file->section);
    }
    if (ed_watched_section(file->section) && ed_html_page(file->filename) && file->text) {
        char *relative = file->public_path && file->public_path[0] == '/' ? strdup(file->public_path + 1)
                                                                          : strdup(file->public_path ? file->public_path : "");
        int skipped = ed_is_mailchimp(text) || (ed_is_jalbum_text(text) && ed_handcrafted_sibling(file->folder, file->filename));
        int ignored = relative && ed_ignored(audit, relative);
        if (!audit->ignore_ok && strcmp(file->public_path, "/articles") == 0) {
            ed_add_issue(errors, "page-values", "not-set-up", "error", "Page values check is not set up",
                         "Page values check is not set up. scripts/page_values/ignore.txt is missing.", 0);
        }
        if (!skipped && !ignored && ed_rel_watched(relative)) {
            EdValues values;
            ed_parse_values(text, &values);
            if (ed_is_jalbum_text(text) && !values.present) {
                char *gap = ed_seo_gap(text);
                if (gap) {
                    ed_add_issue(errors, "page-values", "seo", "error", "SEO tags are incomplete", gap, 1);
                    free(gap);
                }
            } else if (!values.present) {
                ed_add_issue(errors, "page-values", "not-set-up", "error", "Page values are not set up", ED_NOT_SET_UP, 1);
                if (!strstr(text, "<!-- generated-seo -->")) {
                    ed_add_issue(errors, "page-values", "missing", "error", "Generated SEO is missing", ED_MISSING_SEO, 1);
                }
            } else if (values.error) {
                char wrapped[700];
                snprintf(wrapped, sizeof(wrapped), "Page values are missing: %s", values.error);
                ed_add_issue(errors, "page-values", "missing", "error", "Page values are incomplete", wrapped, 1);
            } else if (values.image && values.image[0] && strncmp(values.image, "http://", 7) != 0 &&
                       strncmp(values.image, "https://", 8) != 0) {
                char *fs = ed_image_fs(repo, file->folder, values.image);
                char *looked = NULL;
                if (!fs || !file_exists(fs)) {
                    StrList nearby;
                    char *message;
                    const char *show = fs ? fs : "";
                    sl_init(&nearby);
                    if (repo && fs && strncmp(fs, repo, strlen(repo)) == 0) {
                        show = fs + strlen(repo);
                        if (*show == '/') {
                            show++;
                        }
                    }
                    looked = strdup(show);
                    ed_nearby_same(file->folder, values.image, fs, &nearby);
                    message = ed_image_missing_message(relative, values.image, looked, &nearby);
                    ed_add_issue(errors, "page-values", "image-missing", "error", "Page image is missing",
                                 message ? message : "The page-values image is not on disk.", 1);
                    free(message);
                    sl_free(&nearby);
                } else if (!ed_generated_current(text, values.title, values.description) ||
                           (deep && ed_python_stale(repo, file->fs) == 1)) {
                    ed_add_issue(errors, "page-values", "missing", "error", "Generated SEO is out of date", ED_OUT_OF_DATE,
                                 1);
                }
                free(fs);
                free(looked);
            } else if (!ed_generated_current(text, values.title, values.description) ||
                       (deep && ed_python_stale(repo, file->fs) == 1)) {
                ed_add_issue(errors, "page-values", "missing", "error", "Generated SEO is out of date", ED_OUT_OF_DATE, 1);
            }
            if (values.present && !values.error && values.title && values.description && audit) {
                char *title_key = collapse_ws(values.title);
                char *desc_key = collapse_ws(values.description);
                size_t i;
                size_t others = 0;
                Buf list;
                buf_init(&list);
                for (i = 0; i < audit->dup_n; i++) {
                    if (title_key && desc_key && strcmp(audit->dups[i].title, title_key) == 0 &&
                        strcmp(audit->dups[i].description, desc_key) == 0 &&
                        strcmp(audit->dups[i].path, file->public_path) != 0) {
                        if (others < 3) {
                            if (others && buf_puts(&list, ", ") != 0) {
                                break;
                            }
                            buf_puts(&list, audit->dups[i].path);
                        }
                        others++;
                    }
                }
                if (others > 3) {
                    char extra[40];
                    snprintf(extra, sizeof(extra), ", and %zu more", others - 3);
                    buf_puts(&list, extra);
                }
                if (others && list.data) {
                    char *message = (char *)malloc(strlen(list.data) + 80);
                    if (message) {
                        snprintf(message, strlen(list.data) + 80, "Page values are a perfect duplicate of %s.", list.data);
                        ed_add_issue(errors, "page-values", "duplicate", "error", "Page values are duplicated", message, 0);
                        free(message);
                    }
                }
                buf_free(&list);
                free(title_key);
                free(desc_key);
            }
            ed_values_clear(&values);
            if (ed_watched_section(file->section)) {
                EdRelated related;
                ed_parse_related(text, &related);
                if (related.present && !related.has_include) {
                    ed_add_issue(errors, "related-sales", "missing-include", "error", "Suggestion include is missing",
                                 "This page has a related-sales comment but does not include <!--#include virtual=\"/php/similar_vehicles.php?embed=1\" -->.",
                                 1);
                } else if (!related.present && related.has_include) {
                    ed_add_issue(errors, "related-sales", "missing-comment", "error", "Related sales comment is missing",
                                 "This page includes similar_vehicles.php but has no related-sales comment for the suggestion strip to read.",
                                 1);
                }
                if (related.present && related.override_ids && related.override_ids[0] && audit && audit->inventory) {
                    const char *cursor = related.override_ids;
                    while (*cursor) {
                        const char *comma = strchr(cursor, ',');
                        size_t n = comma ? (size_t)(comma - cursor) : strlen(cursor);
                        char *token = trim_dup(cursor, n);
                        if (token && token[0] && !ed_inventory_known(audit, token)) {
                            char message[240];
                            char *shown = ed_clean_label(token, 80);
                            snprintf(message, sizeof(message), "Override ID %s does not match any vehicle in sales/.",
                                     shown && shown[0] ? shown : token);
                            ed_add_issue(errors, "related-sales", "unknown-override", "error", "Override ID is unknown",
                                         message, 1);
                            free(shown);
                        }
                        free(token);
                        if (!comma) {
                            break;
                        }
                        cursor = comma + 1;
                    }
                }
                ed_related_clear(&related);
            }
        }
        free(relative);
    }
    if (file->text && !vehicle) {
        int google = ed_google_present(text);
        int extreme = 0;
        int noscript = 0;
        char pid[64];
        int header = ed_header_include(text) || ed_traffic_header_call(text);
        ed_extreme_info(text, &extreme, pid, sizeof(pid), &noscript);
        if (!google) {
            char msg[320];
            if (ed_ga_id()[0]) {
                snprintf(msg, sizeof(msg), "Google Analytics is missing. Add the gtag.js tag for %s.", ed_ga_id());
            } else {
                snprintf(msg, sizeof(msg), "Google Analytics is missing. Add the gtag.js tag.");
            }
            ed_add_issue(errors, "tracking-missing", "google", "error", "Google Analytics is missing", msg, 1);
        }
        if (!extreme) {
            char msg[320];
            if (ed_extreme_login()[0]) {
                snprintf(msg, sizeof(msg), "The eXTReMe Pro Tracker is missing (%s, login %s).", ed_extreme_host(),
                         ed_extreme_login());
            } else {
                snprintf(msg, sizeof(msg), "The eXTReMe Pro Tracker is missing (%s).", ed_extreme_host());
            }
            ed_add_issue(errors, "tracking-missing", "extreme", "error", "eXTReMe tracker is missing", msg, 1);
        }
        if (!header) {
            ed_add_issue(errors, "tracking-missing", "admin", "error", "Admin tracking is missing",
                         "Admin Dashboard tracking is missing. Add <!--#include virtual=\"/includes/header.html\"--> so the page includes logview.php and track.js.",
                         1);
        }
    }
    return errors;
}
static char *ed_data_str(const JsonValue *data, const char *key, const char *fallback) {
    if (data && json_type(data) == JSON_OBJECT && json_object_has(data, key)) {
        char *text = json_text_dup(json_object_get(data, key));
        return text ? text : strdup("");
    }
    return strdup(fallback ? fallback : "");
}

static int ed_data_enabled(const JsonValue *data, int fallback) {
    int value = fallback;
    if (!data || json_type(data) != JSON_OBJECT) {
        return fallback;
    }
    if (json_object_has(data, "enabled") && json_boolish(json_object_get(data, "enabled"), &value) == 0) {
        return value;
    }
    if (json_object_has(data, "present") && json_boolish(json_object_get(data, "present"), &value) == 0) {
        return value;
    }
    return fallback;
}

static int ed_data_bool(const JsonValue *data, const char *key, int fallback) {
    int value = fallback;
    if (!data || json_type(data) != JSON_OBJECT || !key || !json_object_has(data, key)) {
        return fallback;
    }
    if (json_boolish(json_object_get(data, key), &value) != 0) {
        return fallback;
    }
    return value;
}

static int ed_field_ok(const char *text, char *err, size_t err_sz, const char *label) {
    if (field_has_break(text)) {
        set_errf(err, err_sz, "%s cannot contain line breaks or '-->'", label);
        return 0;
    }
    return 1;
}

static JsonValue *ed_widgets(const char *repo, const EdFile *file) {
    JsonValue *widgets = json_object();
    JsonValue *page_values = json_object();
    JsonValue *related_json = json_object();
    JsonValue *google = json_object();
    JsonValue *extreme = json_object();
    JsonValue *admin = json_object();
    JsonValue *share = json_object();
    const char *text = file && file->text ? file->text : "";
    int vehicle = ed_sales_vehicle(file);
    EdValues values;
    EdRelated related;
    GallerySalesHeader header;
    char *share_file;
    char *thumb_file;
    char *effective = NULL;
    char *share_title = NULL;
    char *share_description = NULL;
    int google_on = 0;
    int extreme_on = 0;
    int noscript = 0;
    char pid[64];
    int header_on = 0;

    memset(&values, 0, sizeof(values));
    memset(&related, 0, sizeof(related));
    memset(&header, 0, sizeof(header));
    if (!widgets || !page_values || !related_json || !google || !extreme || !admin || !share || !file) {
        json_free(widgets);
        json_free(page_values);
        json_free(related_json);
        json_free(google);
        json_free(extreme);
        json_free(admin);
        json_free(share);
        return NULL;
    }
    if (vehicle && file->text && sales_header_from_text(file->text, &header, NULL, 0) != 0) {
        header.vehicle_name = strdup("");
        header.vehicle_type = strdup("");
        header.make_type = strdup("");
        header.inventory_number = strdup("");
        header.price = strdup("");
        header.description = strdup("");
    }
    if (!vehicle) {
        ed_parse_values(text, &values);
        ed_parse_related(text, &related);
    } else {
        values.present = header.vehicle_name && header.vehicle_name[0];
        values.title = strdup(header.vehicle_name ? header.vehicle_name : "");
        values.description = strdup(header.description ? header.description : "");
        values.image = strdup("");
        values.type = strdup(header.vehicle_type ? header.vehicle_type : "");
        values.date = strdup("");
        related.vehicle_type = strdup(header.vehicle_type ? header.vehicle_type : "");
        related.make_type = strdup(header.make_type ? header.make_type : "");
        related.vehicle_name = strdup(header.vehicle_name ? header.vehicle_name : "");
        related.override_ids = strdup("");
        related.present = 0;
        related.has_include = text && strstr(text, "similar_vehicles.php") ? 1 : 0;
    }
    share_file = file->folder ? join_path(file->folder, "shareimage.jpg") : NULL;
    thumb_file = file->folder ? join_path(file->folder, "folderthumb.jpg") : NULL;
    if (!vehicle && values.image && values.image[0]) {
        char *fs = ed_image_fs(repo, file->folder, values.image);
        if ((fs && file_exists(fs)) || strncmp(values.image, "http://", 7) == 0 || strncmp(values.image, "https://", 8) == 0) {
            effective = ed_image_public(file->section, file->slug, values.image);
        }
        free(fs);
    }
    if (!effective && share_file && file_exists(share_file)) {
        effective = ed_public_join(file->section, file->slug, "shareimage.jpg");
    }
    if (!effective && file->folder) {
        char *slides = join_path(file->folder, "slides");
        StrList names;
        sl_init(&names);
        if (slides && list_names(slides, 0, 1, &names) == 0 && names.count) {
            char *rel = join_path("slides", names.items[0]);
            effective = ed_public_join(file->section, file->slug, rel);
            free(rel);
        }
        sl_free(&names);
        free(slides);
    }
    if (!effective && thumb_file && file_exists(thumb_file)) {
        effective = ed_public_join(file->section, file->slug, "folderthumb.jpg");
    }
    if (!effective) {
        effective = strdup("");
    }
    if (vehicle) {
        char *plain = plain_html(header.description ? header.description : "");
        int sold = header.price && strcasecmp(header.price, "SOLD") == 0;
        share_title = sales_share_title(header.vehicle_name, sold);
        share_description = truncate_meta(plain ? plain : "");
        free(plain);
        if (share_file && file_exists(share_file)) {
            free(values.image);
            values.image = strdup("shareimage.jpg");
        }
    } else {
        share_title = article_share_title(values.title && values.title[0] ? values.title : file->filename);
        share_description = truncate_meta(values.description ? values.description : "");
    }
    google_on = ed_google_present(text);
    ed_extreme_info(text, &extreme_on, pid, sizeof(pid), &noscript);
    header_on = ed_header_include(text) || ed_traffic_header_call(text);
    jput_bool(page_values, "present", values.present);
    jput_str(page_values, "title", values.title);
    jput_str(page_values, "description", values.description);
    jput_str(page_values, "image", values.image);
    jput_str(page_values, "type", values.type && values.type[0] ? values.type : (vehicle ? "" : "Article"));
    jput_str(page_values, "date", values.date);
    jput_bool(related_json, "present", related.present);
    jput_bool(related_json, "hasInclude", related.has_include);
    jput_str(related_json, "vehicleType", related.vehicle_type);
    jput_str(related_json, "makeType", related.make_type);
    jput_str(related_json, "vehicleName", related.vehicle_name);
    jput_str(related_json, "overrideIds", related.override_ids);
    jput_bool(google, "present", vehicle || google_on);
    jput_bool(google, "inheritedFromInventoryPhp", vehicle);
    {
        char measurement[40];
        ed_copy_google_id(text, measurement, sizeof(measurement));
        jput_str(google, "measurementId", measurement);
    }
    jput_bool(extreme, "present", vehicle || extreme_on);
    jput_bool(extreme, "inheritedFromInventoryPhp", vehicle);
    jput_str(extreme, "login", ed_extreme_login());
    if (vehicle) {
        jput_str(extreme, "pid", header.inventory_number && header.inventory_number[0] ? header.inventory_number : "sales");
        jput_bool(extreme, "hasNoscript", 1);
    } else {
        jput_str(extreme, "pid", pid);
        jput_bool(extreme, "hasNoscript", noscript);
    }
    jput_bool(admin, "present", vehicle || header_on);
    jput_bool(admin, "inheritedFromInventoryPhp", vehicle);
    jput_bool(admin, "hasHeaderInclude", vehicle || ed_header_include(text) || ed_traffic_header_call(text));
    jput_bool(admin, "recordsViaLogview", vehicle || header_on || (text && strstr(text, "logview.php")));
    jput_bool(admin, "hasTrackJs", vehicle || header_on || (text && strstr(text, "/includes/track.js")));
    jput_bool(share, "hasShareImage", share_file && file_exists(share_file));
    {
        char *share_url = ed_public_join(file->section, file->slug, "shareimage.jpg");
        char *thumb_url = ed_public_join(file->section, file->slug, "folderthumb.jpg");
        jput_str(share, "shareImageUrl", share_url);
        jput_bool(share, "hasFolderThumb", thumb_file && file_exists(thumb_file));
        jput_str(share, "folderThumbUrl", thumb_url);
        free(share_url);
        free(thumb_url);
    }
    jput_str(share, "effectiveShareImage", effective);
    jput_str(share, "title", share_title);
    jput_str(share, "description", share_description);
    json_object_put(widgets, "pageValues", page_values);
    json_object_put(widgets, "relatedSales", related_json);
    json_object_put(widgets, "googleTracking", google);
    json_object_put(widgets, "extremeTracking", extreme);
    json_object_put(widgets, "adminTracking", admin);
    json_object_put(widgets, "share", share);
    ed_values_clear(&values);
    ed_related_clear(&related);
    gallery_sales_header_free(&header);
    free(share_file);
    free(thumb_file);
    free(effective);
    free(share_title);
    free(share_description);
    return widgets;
}

static int ed_fill_asset(JsonValue *root, const EdFile *file) {
    JsonValue *errors = json_array();
    JsonValue *widgets = json_object();
    char *slides = file && file->folder ? join_path(file->folder, "slides") : NULL;
    char *thumbs = file && file->folder ? join_path(file->folder, "thumbs") : NULL;
    int gallery = slides && thumbs && dir_exists(slides) && dir_exists(thumbs);
    const char *file_type;
    if (!root || !file || !errors || !widgets) {
        json_free(errors);
        json_free(widgets);
        free(slides);
        free(thumbs);
        return -1;
    }
    if (file->media) {
        file_type = file->api_type && file->api_type[0] ? file->api_type : "document";
    } else {
        file_type = ed_badge_type(file->filename);
    }
    jput_str(root, "path", file->public_path);
    jput_str(root, "previewUrl", file->public_path);
    jput_str(root, "section", file->section);
    jput_str(root, "slug", file->slug ? file->slug : "");
    jput_str(root, "fileType", file_type);
    jput_bool(root, "isMedia", file->media ? 1 : 0);
    jput_str(root, "mediaKind", file->media ? (file->media_kind ? file->media_kind : "document") : "");
    jput_int(root, "fileSize", file->file_size);
    jput_int(root, "width", file->media ? file->width : 0);
    jput_int(root, "height", file->media ? file->height : 0);
    jput_str(root, "content", file->media ? "" : (file->text ? file->text : ""));
    jput_bool(root, "hasGallery", gallery);
    jput_bool(root, "isJalbum", 0);
    jput_bool(root, "jalbum", 0);
    json_object_put(root, "widgets", widgets);
    json_object_put(root, "errors", errors);
    free(slides);
    free(thumbs);
    return 0;
}

static int ed_fill_loaded(JsonValue *root, const char *repo, const EdFile *file, int deep, int with_content,
                          char *err, size_t err_sz) {
    EdAudit audit;
    JsonValue *widgets;
    JsonValue *errors;
    char *slides;
    char *thumbs;
    if (!root || !file) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    if (file->media || file->text_asset || !ed_content_section(file->section)) {
        (void)deep;
        if (ed_fill_asset(root, file) != 0) {
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        if (with_content && !file->media) {
            char *head_text = git_head_text(repo, file->public_path);
            if (head_text) {
                jput_str(root, "gitContent", head_text);
                free(head_text);
            }
        }
        return 0;
    }
    ed_audit_init(&audit);
    ed_audit_build(&audit, repo, ed_watched_section(file->section), 1);
    widgets = ed_widgets(repo, file);
    errors = ed_issues(repo, file, &audit, deep);
    if (!widgets || !errors) {
        json_free(widgets);
        json_free(errors);
        ed_audit_free(&audit);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    jput_str(root, "path", file->public_path);
    jput_str(root, "section", file->section);
    jput_str(root, "slug", file->slug);
    jput_str(root, "fileType", ed_file_type(file->filename));
    jput_bool(root, "isJalbum", ed_is_jalbum_text(file->text));
    jput_bool(root, "jalbum", ed_is_jalbum_text(file->text));
    slides = join_path(file->folder, "slides");
    thumbs = join_path(file->folder, "thumbs");
    jput_bool(root, "hasGallery", slides && thumbs && dir_exists(slides) && dir_exists(thumbs));
    if (with_content) {
        char *head_text = git_head_text(repo, file->public_path);
        jput_str(root, "content", file->text ? file->text : "");
        if (head_text) {
            jput_str(root, "gitContent", head_text);
            free(head_text);
        }
    }
    json_object_put(root, "widgets", widgets);
    json_object_put(root, "errors", errors);
    free(slides);
    free(thumbs);
    ed_audit_free(&audit);
    return 0;
}

static int ed_fill_page(JsonValue *root, const char *repo, const char *public_path, int deep, int with_content,
                        char *err, size_t err_sz) {
    EdFile file;
    int rc;
    memset(&file, 0, sizeof(file));
    rc = ed_resolve(repo, public_path, &file, 1, err, err_sz);
    if (rc != 0) {
        ed_file_clear(&file);
        return rc;
    }
    rc = ed_fill_loaded(root, repo, &file, deep, with_content, err, err_sz);
    ed_file_clear(&file);
    return rc;
}

static void studio_attach_page_fields(JsonValue *root, const char *repo, const char *public_path, int deep) {
    char err[256];
    if (!root || !repo || !public_path || !public_path[0]) {
        return;
    }
    ed_fill_page(root, repo, public_path, deep, 1, err, sizeof(err));
}

static int ed_folder_has_page(const char *folder) {
    StrList names;
    size_t i;
    char *slides;
    int found = 0;
    sl_init(&names);
    if (list_names(folder, 0, 0, &names) == 0) {
        for (i = 0; i < names.count; i++) {
            if (ed_listed_name(names.items[i])) {
                found = 1;
                break;
            }
        }
    }
    sl_free(&names);
    slides = join_path(folder, "slides");
    if (slides && dir_exists(slides)) {
        found = 1;
    }
    free(slides);
    return found;
}

static void ed_collect_slugs(const char *dir, const char *prefix, int direct, StrList *out) {
    StrList subs;
    size_t i;
    sl_init(&subs);
    if (list_names(dir, 1, 0, &subs) != 0) {
        sl_free(&subs);
        return;
    }
    for (i = 0; i < subs.count; i++) {
        char *child;
        char *slug;
        if (ed_skip_dir_name(subs.items[i])) {
            continue;
        }
        child = join_path(dir, subs.items[i]);
        slug = (prefix && prefix[0]) ? join_path(prefix, subs.items[i]) : strdup(subs.items[i]);
        if (child && slug && (direct || ed_folder_has_page(child))) {
            sl_add(out, slug);
        }
        if (child && slug) {
            ed_collect_slugs(child, slug, 0, out);
        }
        free(child);
        free(slug);
    }
    sl_free(&subs);
}

static const char *ed_slug_base(const char *slug) {
    const char *slash = slug ? strrchr(slug, '/') : NULL;
    return slash ? slash + 1 : (slug ? slug : "");
}

static int ed_file_is_primary(const char *section, const char *name, const char *primary) {
    if (!name || !name[0]) {
        return 0;
    }
    if (primary && strcmp(name, primary) == 0) {
        return 1;
    }
    if ((!primary || !primary[0]) && section && strcmp(section, "sales") == 0 && strcmp(name, "index.php") == 0) {
        return 1;
    }
    return 0;
}

static long long ed_page_issue_count(const char *repo, const char *section, const char *slug, const char *folder,
                                     const char *filename, const EdAudit *audit) {
    EdFile page;
    JsonValue *issues;
    long long count = 0;
    if (!filename || !is_page_file(filename)) {
        return 0;
    }
    memset(&page, 0, sizeof(page));
    page.section = (char *)(section ? section : "");
    page.slug = (char *)(slug ? slug : "");
    page.filename = (char *)(filename ? filename : "");
    page.folder = (char *)(folder ? folder : "");
    if (folder && filename && filename[0]) {
        page.fs = join_path(folder, filename);
    }
    page.public_path = ed_public_join(section, slug, filename);
    if (!page.public_path) {
        page.public_path = strdup("");
    }
    page.php = filename && ends_with_ci(filename, ".php");
    if (page.fs) {
        page.text = read_file(page.fs, NULL);
    }
    page.missing = page.text ? 0 : 1;
    issues = ed_issues(repo, &page, audit, 0);
    if (issues) {
        count = (long long)json_array_len(issues);
        json_free(issues);
    }
    free(page.fs);
    free(page.public_path);
    free(page.text);
    return count;
}

static void ed_stamp_file_status(JsonValue *entry, const char *repo, const char *section, const char *slug,
                                 const char *folder, const char *primary, const char *porcelain, const EdAudit *audit,
                                 long long primary_issues, int use_primary_issues) {
    const char *name;
    char *rel = NULL;
    long long count;
    int primary_file;
    if (!entry) {
        return;
    }
    name = json_string_get(json_object_get(entry, "name"), NULL);
    primary_file = use_primary_issues && ed_file_is_primary(section, name, primary);
    if (primary_file) {
        count = primary_issues;
    } else {
        count = ed_page_issue_count(repo, section, slug, folder, name, audit);
    }
    if (section && name && name[0]) {
        rel = (slug && slug[0]) ? join_path3(section, slug, name) : join_path(section, name);
    }
    jput_int(entry, "issueCount", count);
    jput_bool(entry, "dirty", rel && porcelain_touches(porcelain, rel));
    free(rel);
}

static JsonValue *ed_file_entry(const char *section, const char *slug, const char *name) {
    JsonValue *item = json_object();
    char *path = ed_public_join(section, slug, name);
    if (!item) {
        free(path);
        return NULL;
    }
    jput_str(item, "name", name);
    jput_str(item, "path", path);
    jput_str(item, "type", ed_file_type(name));
    free(path);
    return item;
}

static JsonValue *ed_tree_folder(const char *repo, const char *section, const char *section_dir, const char *slug,
                                 const char *porcelain, const EdAudit *audit) {
    JsonValue *item = json_object();
    JsonValue *files = json_array();
    char *folder = join_path(section_dir, slug);
    char *primary = NULL;
    char *public_primary = NULL;
    char *rel;
    StrList names;
    size_t i;
    GImage *images = NULL;
    size_t image_count = 0;
    size_t unlisted = 0;
    int slides_on = 0;
    int load_rc = 1;
    char *name = NULL;
    EdFile page;
    JsonValue *issues;
    if (!item || !files || !folder) {
        json_free(item);
        json_free(files);
        free(folder);
        return NULL;
    }
    if (strcmp(section, "sales") == 0) {
        char *index = join_path(folder, "index.php");
        if (index && file_exists(index)) {
            primary = strdup("index.php");
        }
        free(index);
    }
    if (!primary) {
        primary = pick_primary(folder, ed_slug_base(slug));
    }
    sl_init(&names);
    if (list_names(folder, 0, 0, &names) == 0) {
        int best_rank = 1000;
        const char *best_asset = NULL;
        for (i = 0; i < names.count; i++) {
            JsonValue *entry;
            int rank;
            if (!ed_listed_name(names.items[i])) {
                continue;
            }
            entry = ed_file_entry(section, slug, names.items[i]);
            if (entry) {
                json_array_add(files, entry);
            }
            if (primary) {
                continue;
            }
            rank = ed_asset_rank(names.items[i]);
            if (rank < best_rank) {
                best_rank = rank;
                best_asset = names.items[i];
            }
        }
        if (!primary && best_asset) {
            primary = strdup(best_asset);
        }
    }
    {
        char *slides = join_path(folder, "slides");
        StrList slide_files;
        sl_init(&slide_files);
        if (slides && list_names(slides, 0, 1, &slide_files) == 0) {
            slides_on = (int)slide_files.count;
        }
        {
            StrList documented;
            int from_html = 0;
            sl_init(&documented);
            load_rc = load_documented_images(folder, section, ed_slug_base(slug), &images, &image_count, NULL, &documented,
                                             &from_html, NULL, NULL, 0);
            for (i = 0; i < slide_files.count; i++) {
                if (!name_is_documented(slide_files.items[i], &documented)) {
                    unlisted++;
                }
            }
            if (from_html) {
                load_rc = 0;
            }
            sl_free(&documented);
        }
        sl_free(&slide_files);
        free(slides);
    }
    if (strcmp(section, "sales") == 0) {
        GallerySalesHeader header;
        char *index_path = primary ? join_path(folder, primary) : NULL;
        memset(&header, 0, sizeof(header));
        if (index_path && gallery_read_sales_header(index_path, &header, NULL, 0) == 0) {
            name = strdup(header.vehicle_name && header.vehicle_name[0] ? header.vehicle_name : slug);
            gallery_sales_header_free(&header);
        } else {
            name = strdup(ed_slug_base(slug));
        }
        free(index_path);
    } else if (primary && is_page_file(primary)) {
        char *page_path = join_path(folder, primary);
        char *title = NULL;
        char *description = NULL;
        char *image = NULL;
        char *type = NULL;
        char *date = NULL;
        char *a = NULL;
        char *b = NULL;
        char *c = NULL;
        char *d = NULL;
        int jalbum = 0;
        if (page_path) {
            read_page_meta(page_path, &title, &description, &image, &type, &date, &a, &b, &c, &d, &jalbum);
        }
        name = strdup(title && title[0] ? title : ed_slug_base(slug));
        free(page_path);
        free(title);
        free(description);
        free(image);
        free(type);
        free(date);
        free(a);
        free(b);
        free(c);
        free(d);
    } else {
        name = strdup(ed_slug_base(slug));
    }
    memset(&page, 0, sizeof(page));
    page.section = (char *)section;
    page.slug = (char *)slug;
    page.folder = folder;
    if (primary && is_page_file(primary)) {
        page.filename = primary;
        page.fs = join_path(folder, primary);
        page.public_path = ed_public_join(section, slug, primary);
        page.php = ends_with_ci(primary, ".php");
        page.text = page.fs ? read_file(page.fs, NULL) : NULL;
        page.missing = page.text ? 0 : 1;
        issues = ed_issues(repo, &page, audit, 0);
        public_primary = page.public_path ? strdup(page.public_path) : strdup("");
    } else if (strcmp(section, "sales") == 0 && slug && slug[0] && !strchr(slug, '/')) {
        page.filename = (char *)"index.php";
        page.fs = join_path(folder, "index.php");
        page.public_path = ed_public_join(section, slug, "index.php");
        page.php = 1;
        page.missing = 1;
        issues = ed_issues(repo, &page, audit, 0);
        public_primary = (primary && primary[0]) ? ed_public_join(section, slug, primary) : strdup("");
    } else {
        issues = json_array();
        public_primary = (primary && primary[0]) ? ed_public_join(section, slug, primary) : strdup("");
    }
    if (!public_primary) {
        public_primary = strdup("");
    }
    rel = join_path(section, slug);
    jput_str(item, "slug", slug);
    jput_str(item, "name", name && name[0] ? name : slug);
    jput_str(item, "primaryFile", public_primary);
    json_object_put(item, "files", files);
    jput_int(item, "photoCount", load_rc == 0 ? (long long)image_count : (long long)slides_on);
    jput_int(item, "unlistedCount", (long long)unlisted);
    jput_bool(item, "dirty", rel && porcelain_touches(porcelain, rel));
    {
        long long count = issues ? (long long)json_array_len(issues) : 0;
        size_t nfiles = json_array_len(files);
        for (i = 0; i < nfiles; i++) {
            ed_stamp_file_status(json_array_at(files, i), repo, section, slug, folder, primary, porcelain, audit, count, 1);
        }
        jput_int(item, "issueCount", count);
    }
    json_free(issues);
    free_gimages(images, image_count);
    free(page.fs);
    free(page.public_path);
    free(page.text);
    free(primary);
    free(public_primary);
    free(rel);
    free(name);
    free(folder);
    sl_free(&names);
    return item;
}

static JsonValue *ed_tree_section(const char *repo, const char *section, const EdAudit *audit) {
    JsonValue *item = json_object();
    JsonValue *folders = json_array();
    JsonValue *root_files = json_array();
    char *dir = join_path(repo, section);
    char *porcelain = NULL;
    StrList slugs;
    StrList files;
    size_t i;
    if (!item || !folders || !root_files) {
        json_free(item);
        json_free(folders);
        json_free(root_files);
        free(dir);
        return NULL;
    }
    jput_str(item, "id", section);
    jput_str(item, "label", ed_section_label(section));
    json_object_put(item, "folders", folders);
    json_object_put(item, "rootFiles", root_files);
    if (!dir || !dir_exists(dir)) {
        free(dir);
        return item;
    }
    porcelain = git_status_text(repo, section);
    sl_init(&slugs);
    sl_init(&files);
    ed_collect_slugs(dir, "", 1, &slugs);
    sl_sort(&slugs);
    if (list_names(dir, 0, 0, &files) == 0) {
        for (i = 0; i < files.count; i++) {
            JsonValue *entry;
            if (!ed_listed_name(files.items[i])) {
                continue;
            }
            entry = ed_file_entry(section, "", files.items[i]);
            if (entry) {
                json_array_add(root_files, entry);
            }
        }
    }
    {
        size_t nfiles = json_array_len(root_files);
        for (i = 0; i < nfiles; i++) {
            ed_stamp_file_status(json_array_at(root_files, i), repo, section, "", dir, NULL, porcelain, audit, 0, 0);
        }
    }
    for (i = 0; i < slugs.count; i++) {
        JsonValue *folder = ed_tree_folder(repo, section, dir, slugs.items[i], porcelain, audit);
        if (folder) {
            json_array_add(folders, folder);
        }
    }
    free(porcelain);
    sl_free(&slugs);
    sl_free(&files);
    free(dir);
    return item;
}

JsonDoc *gallery_tree_document(const char *repo_root, char *err, size_t err_sz) {
    JsonValue *root = json_object();
    JsonValue *sections = json_array();
    EdAudit audit;
    size_t i;
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        json_free(root);
        json_free(sections);
        return NULL;
    }
    if (!root || !sections) {
        json_free(root);
        json_free(sections);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    ed_audit_init(&audit);
    ed_audit_build(&audit, repo_root, 1, 1);
    jput_bool(root, "ok", 1);
    json_object_put(root, "sections", sections);
    {
        const char *ordered[sizeof(ED_SECTIONS) / sizeof(ED_SECTIONS[0])];
        size_t count = sizeof(ordered) / sizeof(ordered[0]);
        for (i = 0; i < count; i++) {
            ordered[i] = ED_SECTIONS[i];
        }
        qsort(ordered, count, sizeof(ordered[0]), cmp_cstr);
        for (i = 0; i < count; i++) {
            JsonValue *section = ed_tree_section(repo_root, ordered[i], &audit);
            if (section) {
                json_array_add(sections, section);
            }
        }
    }
    ed_audit_free(&audit);
    return finish_doc(root);
}

void gallery_attach_page_slides(const char *repo_root, JsonDoc *doc) {
    const char *section;
    const char *slug;
    char folder[4096];
    char err[256];
    GImage *images = NULL;
    size_t count = 0;
    size_t captions = 0;
    StrList documented;
    int from_html = 0;
    JsonValue *images_json;
    JsonValue *unlisted_json;
    if (!doc || !doc->root || !repo_root) {
        return;
    }
    section = json_string_get(json_object_get(doc->root, "section"), NULL);
    slug = json_string_get(json_object_get(doc->root, "slug"), NULL);
    if (!gallery_section_ok(section) || !slug || !slug[0] || !gallery_valid_slug(slug)) {
        return;
    }
    err[0] = '\0';
    if (ensure_folder(repo_root, section, slug, folder, sizeof(folder), err, sizeof(err)) != 0) {
        return;
    }
    sl_init(&documented);
    load_documented_images(folder, section, slug, &images, &count, &captions, &documented, &from_html, NULL, err, sizeof(err));
    if (strcmp(section, "sales") == 0) {
        GallerySalesHeader header;
        char *index_path = join_path(folder, "index.php");
        memset(&header, 0, sizeof(header));
        if (!index_path || gallery_read_sales_header(index_path, &header, NULL, 0) != 0) {
            mark_bullets(images, count, "");
        } else {
            mark_bullets(images, count, header.vehicle_name);
        }
        gallery_sales_header_free(&header);
        free(index_path);
    } else {
        mark_bullets(images, count, "");
    }
    images_json = json_array();
    unlisted_json = json_array();
    add_listed_images(images_json, section, slug, images, count);
    add_unlisted_slides(unlisted_json, section, slug, folder, &documented);
    if (images_json) {
        json_object_put(doc->root, "images", images_json);
    }
    if (unlisted_json) {
        json_object_put(doc->root, "unlistedSlides", unlisted_json);
    }
    free_gimages(images, count);
    sl_free(&documented);
    (void)captions;
    (void)from_html;
}

int gallery_page_document(const char *repo_root, const char *public_path, JsonDoc **out, char *err, size_t err_sz) {
    JsonValue *root = json_object();
    int rc;
    if (!out) {
        set_err(err, err_sz, "out of memory");
        json_free(root);
        return -1;
    }
    *out = NULL;
    if (!root) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    jput_bool(root, "ok", 1);
    rc = ed_fill_page(root, repo_root, public_path, 1, 1, err, err_sz);
    if (rc != 0) {
        json_free(root);
        return rc;
    }
    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

static void ed_note_render_error(JsonDoc *doc, const char *message) {
    JsonValue *errors;
    if (!doc || !doc->root || !message || !message[0]) {
        return;
    }
    errors = json_object_get(doc->root, "errors");
    if (!errors || json_type(errors) != JSON_ARRAY) {
        errors = json_array();
        json_object_put(doc->root, "errors", errors);
    }
    ed_add_issue(errors, "page-values", "render-failed", "error", "Page values script failed", message, 1);
}

int gallery_page_save(const char *repo_root, const char *public_path, const char *content, int run_page_values,
                      JsonDoc **out, char *err, size_t err_sz) {
    EdFile file;
    int rc;
    char render_err[512];
    int render_failed = 0;
    memset(&file, 0, sizeof(file));
    render_err[0] = '\0';
    if (!out) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    *out = NULL;
    if (!content) {
        set_err(err, err_sz, "content is required");
        return 1;
    }
    rc = ed_resolve(repo_root, public_path, &file, 1, err, err_sz);
    if (rc != 0) {
        ed_file_clear(&file);
        return rc;
    }
    if (file.media) {
        ed_file_clear(&file);
        set_err(err, err_sz, "This file is a document or media asset and is not edited as text");
        return 1;
    }
    if (atomic_write(file.fs, content, strlen(content)) != 0) {
        ed_file_clear(&file);
        set_err(err, err_sz, "Could not write the page");
        return -1;
    }
    if (run_page_values && ed_watched_section(file.section) && strstr(content, "<!-- page-values")) {
        if (ed_run_render(repo_root, file.fs, render_err, sizeof(render_err)) != 0) {
            render_failed = 1;
        }
    }
    ed_file_clear(&file);
    rc = gallery_page_document(repo_root, public_path, out, err, err_sz);
    if (rc == 0 && render_failed && out && *out) {
        ed_note_render_error(*out, render_err);
    }
    return rc;
}

static char *ed_cut(const char *text, size_t start, size_t end) {
    return ed_replace(text, start, end, "");
}

static char *ed_insert_at(const char *text, size_t pos, const char *insert) {
    return ed_replace(text, pos, pos, insert);
}

static size_t ed_ci_pos(const char *text, const char *needle) {
    const char *found = text ? ed_find_ci_span(text, needle) : NULL;
    return found ? (size_t)(found - text) : (size_t)-1;
}

static char *ed_remove_google(const char *text) {
    const char *mark = text ? strstr(text, "<!-- Google tag (gtag.js) -->") : NULL;
    const char *script;
    const char *end;
    size_t start;
    if (!text) {
        return strdup("");
    }
    if (mark) {
        script = strstr(mark, "</script>");
        if (script) {
            const char *second = strstr(script + 9, "</script>");
            if (second && (size_t)(second - mark) < 2000) {
                script = second;
            }
        }
        if (script && (size_t)(script - mark) < 2500) {
            end = script + 9;
            if (*end == '\r') {
                end++;
            }
            if (*end == '\n') {
                end++;
            }
            return ed_cut(text, (size_t)(mark - text), (size_t)(end - text));
        }
    }
    script = strstr(text, "googletagmanager.com/gtag/js?id=");
    if (!script) {
        return strdup(text);
    }
    start = (size_t)(script - text);
    while (start > 0 && text[start] != '<') {
        start--;
    }
    end = strstr(script, "</script>");
    if (!end) {
        return strdup(text);
    }
    end += 9;
    {
        const char *next = strstr(end, "<script");
        const char *next_end = next ? strstr(next, "</script>") : NULL;
        if (next && next_end && (size_t)(next_end - end) < 800 && ed_span_has(next, next_end, "gtag")) {
            end = next_end + 9;
        }
    }
    if (*end == '\r') {
        end++;
    }
    if (*end == '\n') {
        end++;
    }
    return ed_cut(text, start, (size_t)(end - text));
}

static char *ed_remove_extreme(const char *text) {
    const char *start = strstr(text, "<!-- Start eXTReMe Pro Tracker Code -->");
    const char *end = start ? strstr(start, "<!-- End eXTReMe Pro Tracker Code -->") : NULL;
    const char *hit;
    size_t from;
    if (start && end) {
        end += strlen("<!-- End eXTReMe Pro Tracker Code -->");
        if (*end == '\r') {
            end++;
        }
        if (*end == '\n') {
            end++;
        }
        return ed_cut(text, (size_t)(start - text), (size_t)(end - text));
    }
    hit = strstr(text, "extreme-dm.com");
    if (!hit) {
        return strdup(text);
    }
    from = (size_t)(hit - text);
    while (from > 0 && !(text[from] == '<' && (from == 0 || text[from - 1] != '!'))) {
        if (from >= 7 && strncmp(text + from - 7, "<script", 7) == 0) {
            from -= 7;
            break;
        }
        from--;
    }
    end = strstr(hit, "</noscript>");
    if (end) {
        end += 11;
    } else {
        end = strstr(hit, "</script>");
        if (!end) {
            return strdup(text);
        }
        end += 9;
    }
    if (*end == '\r') {
        end++;
    }
    if (*end == '\n') {
        end++;
    }
    return ed_cut(text, from, (size_t)(end - text));
}

static char *ed_extreme_block_full(const char *pid, const char *login, int noscript) {
    const char *use_pid = pid && pid[0] ? pid : "articles";
    const char *use_login = login && login[0] ? login : ed_extreme_login();
    const char *host = ed_extreme_host();
    Buf buf;
    buf_init(&buf);
    buf_puts(&buf, "<!-- Start eXTReMe Pro Tracker Code -->\n<script>\nEXd = document;\n");
    buf_puts(&buf, "location.protocol == \"https:\" ? EXp = \"https\" : EXp = \"http\";\n");
    buf_puts(&buf, "EXd.write('<img src=\"' + EXp + '://");
    buf_puts(&buf, host);
    buf_puts(&buf, "/n2.g?login=");
    buf_puts(&buf, use_login);
    buf_puts(&buf, "&d=' + screen.width + 'x' + screen.height + '&auto=y&pid=");
    buf_puts(&buf, use_pid);
    buf_puts(&buf, "&jv=' + navigator.javaEnabled() + '&c=' + screen.colorDepth + '&l=' + escape(EXd.referrer) + '\" width=\"0\" height=\"0\" alt=\"\">');\n");
    buf_puts(&buf, "</script>\n");
    if (noscript) {
        buf_puts(&buf, "<noscript><img height=\"1\" width=\"1\" alt=\"\" src=\"https://");
        buf_puts(&buf, host);
        buf_puts(&buf, "/n2.g?login=");
        buf_puts(&buf, use_login);
        buf_puts(&buf, "&amp;auto=y&amp;pid=");
        buf_puts(&buf, use_pid);
        buf_puts(&buf, "\"></noscript>\n");
    }
    buf_puts(&buf, "<!-- End eXTReMe Pro Tracker Code -->\n");
    return buf_steal(&buf);
}

static char *ed_extreme_block(const char *pid) {
    return ed_extreme_block_full(pid, ed_extreme_login(), 1);
}

static char *ed_remove_header_include(const char *text) {
    const char *hit = text ? strstr(text, "/includes/header.html") : NULL;
    const char *start;
    const char *end;
    if (!hit) {
        return strdup(text ? text : "");
    }
    start = hit;
    while (start > text && !(start[0] == '<' && start[1] == '!' && start[2] == '-' && start[3] == '-')) {
        start--;
    }
    end = strstr(hit, "-->");
    if (!end || start < text || memcmp(start, "<!--", 4) != 0) {
        return strdup(text);
    }
    end += 3;
    if (*end == '\r') {
        end++;
    }
    if (*end == '\n') {
        end++;
    }
    return ed_cut(text, (size_t)(start - text), (size_t)(end - text));
}

static char *ed_ensure_header(const char *text) {
    size_t body;
    const char *found;
    char *insert;
    char *next;
    if (!text) {
        return NULL;
    }
    if (ed_header_include(text)) {
        return strdup(text);
    }
    found = ed_find_ci_span(text, "<body");
    if (!found) {
        insert = (char *)malloc(strlen(ED_HEADER_INCLUDE) + strlen(text) + 2);
        if (!insert) {
            return NULL;
        }
        snprintf(insert, strlen(ED_HEADER_INCLUDE) + strlen(text) + 2, "%s\n%s", ED_HEADER_INCLUDE, text);
        return insert;
    }
    found = strchr(found, '>');
    if (!found) {
        return strdup(text);
    }
    body = (size_t)(found - text) + 1;
    insert = (char *)malloc(strlen(ED_HEADER_INCLUDE) + 2);
    if (!insert) {
        return NULL;
    }
    snprintf(insert, strlen(ED_HEADER_INCLUDE) + 2, "\n%s", ED_HEADER_INCLUDE);
    next = ed_insert_at(text, body, insert);
    free(insert);
    return next;
}

static size_t ed_after_tag_open(const char *text, const char *tag) {
    const char *found = text ? ed_find_ci_span(text, tag) : NULL;
    const char *gt = found ? strchr(found, '>') : NULL;
    if (!gt) {
        return 0;
    }
    return (size_t)(gt - text) + 1;
}

static size_t ed_after_charset_or_head(const char *text) {
    const char *meta = text ? ed_find_ci_span(text, "<meta charset") : NULL;
    const char *gt = meta ? strchr(meta, '>') : NULL;
    if (gt) {
        return (size_t)(gt - text) + 1;
    }
    return ed_after_tag_open(text, "<head");
}

static size_t ed_after_head_open(const char *text) {
    size_t pos = ed_after_tag_open(text, "<head");
    return pos;
}

static int ed_newsletter_file(const EdFile *file, const char *text) {
    if (text && strstr(text, "newsletter-archive")) {
        return 1;
    }
    if (file && file->public_path && strstr(file->public_path, "/newsletter")) {
        return 1;
    }
    if (file && file->section && strcmp(file->section, "newsletter") == 0) {
        return 1;
    }
    return 0;
}

static char *ed_ensure_generated_seo(char *text) {
    size_t start = 0;
    size_t end = 0;
    size_t body_start = 0;
    size_t body_end = 0;
    char *next;
    if (!text || strstr(text, "<!-- generated-seo -->")) {
        return text;
    }
    if (find_html_comment(text, strlen(text), "page-values", &start, &end, &body_start, &body_end) != 0) {
        return text;
    }
    next = ed_insert_at(text, end, "\n\n  <!-- generated-seo -->\n  <!-- /generated-seo -->\n");
    free(text);
    return next;
}

static char *ed_strip_generated_seo(char *text) {
    const char *open;
    const char *close;
    char *next;
    if (!text) {
        return NULL;
    }
    open = strstr(text, "<!-- generated-seo -->");
    close = open ? strstr(open, "<!-- /generated-seo -->") : NULL;
    if (!open || !close) {
        return text;
    }
    close += strlen("<!-- /generated-seo -->");
    if (*close == '\r') {
        close++;
    }
    if (*close == '\n') {
        close++;
    }
    next = ed_cut(text, (size_t)(open - text), (size_t)(close - text));
    free(text);
    return next;
}

static int ed_ssi_bounds(const char *text, const char *hit, size_t *start, size_t *end) {
    const char *open;
    const char *close;
    if (!text || !hit || hit < text || !start || !end) {
        return 0;
    }
    open = hit;
    while (open > text && (size_t)(hit - open) < 160 && memcmp(open, "<!--", 4) != 0) {
        open--;
    }
    close = strstr(hit, "-->");
    if (memcmp(open, "<!--", 4) != 0 || !close || (size_t)(hit - open) > 160 || (size_t)(close - hit) > 400) {
        return 0;
    }
    *start = (size_t)(open - text);
    *end = (size_t)(close - text) + 3;
    return 1;
}

static char *ed_cut_include_token(const char *text, const char *token) {
    const char *hit = text && token ? strstr(text, token) : NULL;
    size_t start = 0;
    size_t end = 0;
    const char *close;
    if (!text) {
        return strdup("");
    }
    while (hit) {
        if (ed_ssi_bounds(text, hit, &start, &end)) {
            close = text + end;
            if (*close == '\r') {
                end++;
                close++;
            }
            if (*close == '\n') {
                end++;
            }
            return ed_cut(text, start, end);
        }
        hit = strstr(hit + 1, token);
    }
    return strdup(text);
}

static char *ed_apply_page_values(const char *text, const JsonValue *data, char *err, size_t err_sz) {
    char *title = ed_data_str(data, "title", "");
    char *description = ed_data_str(data, "description", "");
    char *image = ed_data_str(data, "image", "");
    char *type = ed_data_str(data, "type", "");
    char *date = ed_data_str(data, "date", "");
    char *comment;
    char *next = NULL;
    size_t len = text ? strlen(text) : 0;
    size_t start = 0;
    size_t end = 0;
    size_t body_start = 0;
    size_t body_end = 0;
    int enabled = ed_data_enabled(data, 1);
    if (!enabled) {
        if (text && find_html_comment(text, len, "page-values", &start, &end, &body_start, &body_end) == 0) {
            next = ed_cut(text, start, end);
        } else {
            next = strdup(text ? text : "");
        }
        if (next) {
            next = ed_strip_generated_seo(next);
        }
        free(title);
        free(description);
        free(image);
        free(type);
        free(date);
        return next;
    }
    if (ed_data_bool(data, "adding", 0) &&
        !(text && find_html_comment(text, len, "page-values", &start, &end, &body_start, &body_end) == 0)) {
        size_t pos;
        char *block;
        if (!type || !type[0]) {
            free(type);
            type = strdup("Article");
        }
        if (!type || !ed_field_ok(title, err, err_sz, "Title") || !ed_field_ok(description, err, err_sz, "Description") ||
            !ed_field_ok(image, err, err_sz, "Image") || !ed_field_ok(type, err, err_sz, "Type") ||
            !ed_field_ok(date, err, err_sz, "Date") || !valid_page_type(type) || !valid_page_date(date)) {
            if (err && !err[0]) {
                set_err(err, err_sz, "Page values contain an invalid character, type, or date");
            }
            free(title);
            free(description);
            free(image);
            free(type);
            free(date);
            return NULL;
        }
        comment = build_page_comment("  ", title ? title : "", description ? description : "", image, type, date);
        pos = ed_after_charset_or_head(text);
        block = comment ? (char *)malloc(strlen(comment) + 8) : NULL;
        if (block) {
            snprintf(block, strlen(comment) + 8, "\n  %s\n", comment);
            next = ed_insert_at(text ? text : "", pos, block);
            free(block);
        }
        free(comment);
        free(title);
        free(description);
        free(image);
        free(type);
        free(date);
        if (next) {
            next = ed_ensure_generated_seo(next);
        }
        if (!next) {
            set_err(err, err_sz, "Could not update page-values");
        }
        return next;
    }
    if (!title || !title[0] || !description || !description[0]) {
        set_err(err, err_sz, "Page title and description are required");
        free(title);
        free(description);
        free(image);
        free(type);
        free(date);
        return NULL;
    }
    if (!ed_field_ok(title, err, err_sz, "Title") || !ed_field_ok(description, err, err_sz, "Description") ||
        !ed_field_ok(image, err, err_sz, "Image") || !ed_field_ok(type, err, err_sz, "Type") ||
        !ed_field_ok(date, err, err_sz, "Date") || !valid_page_type(type) || !valid_page_date(date)) {
        if (err && !err[0]) {
            set_err(err, err_sz, "Page values contain an invalid character, type, or date");
        }
        free(title);
        free(description);
        free(image);
        free(type);
        free(date);
        return NULL;
    }
    if (text && find_html_comment(text, len, "page-values", &start, &end, &body_start, &body_end) == 0) {
        char *indent = comment_indent(text, start);
        comment = build_page_comment(indent ? indent : "", title, description, image, type, date);
        free(indent);
        next = comment ? ed_replace(text, start, end, comment) : NULL;
    } else {
        size_t pos = ed_after_charset_or_head(text);
        comment = build_page_comment("  ", title, description, image, type, date);
        if (comment) {
            char *block = (char *)malloc(strlen(comment) + 8);
            if (block) {
                snprintf(block, strlen(comment) + 8, "\n  %s\n", comment);
                next = ed_insert_at(text ? text : "", pos, block);
                free(block);
            }
        }
    }
    free(comment);
    free(title);
    free(description);
    free(image);
    free(type);
    free(date);
    if (next) {
        next = ed_ensure_generated_seo(next);
    }
    if (!next) {
        set_err(err, err_sz, "Could not update page-values");
    }
    return next;
}

static char *ed_apply_related(const EdFile *file, const char *text, const JsonValue *data, char *err, size_t err_sz) {
    EdRelated current;
    char *vehicle_type;
    char *make_type;
    char *vehicle_name;
    char *override_ids;
    char *comment;
    char *next;
    size_t len = text ? strlen(text) : 0;
    size_t start = 0;
    size_t end = 0;
    size_t body_start = 0;
    size_t body_end = 0;
    int enabled = ed_data_enabled(data, 1);
    memset(&current, 0, sizeof(current));
    ed_parse_related(text, &current);
    if (!enabled) {
        next = text ? strdup(text) : strdup("");
        if (next && find_html_comment(next, strlen(next), "related-sales", &start, &end, &body_start, &body_end) == 0) {
            char *cut = ed_cut(next, start, end);
            free(next);
            next = cut;
        }
        if (next && strstr(next, "similar_vehicles.php")) {
            char *cut = ed_cut_include_token(next, "similar_vehicles.php");
            free(next);
            next = cut;
        }
        ed_related_clear(&current);
        return next;
    }
    vehicle_type = ed_data_str(data, "vehicleType", current.vehicle_type);
    make_type = ed_data_str(data, "makeType", current.make_type);
    vehicle_name = ed_data_str(data, "vehicleName", current.vehicle_name);
    if (data && json_object_has(data, "overrideIds")) {
        override_ids = ed_data_str(data, "overrideIds", "");
    } else if (data && json_object_has(data, "Override IDs")) {
        override_ids = ed_data_str(data, "Override IDs", "");
    } else {
        override_ids = strdup(current.override_ids ? current.override_ids : "");
    }
    if (!ed_field_ok(vehicle_type, err, err_sz, "Vehicle type") || !ed_field_ok(make_type, err, err_sz, "Make") ||
        !ed_field_ok(vehicle_name, err, err_sz, "Vehicle name") || !ed_field_ok(override_ids, err, err_sz, "Override IDs")) {
        free(vehicle_type);
        free(make_type);
        free(vehicle_name);
        free(override_ids);
        ed_related_clear(&current);
        return NULL;
    }
    comment = build_related_comment("  ", vehicle_type, make_type, vehicle_name, override_ids);
    if (!comment) {
        set_err(err, err_sz, "Could not build related-sales comment");
        free(vehicle_type);
        free(make_type);
        free(vehicle_name);
        free(override_ids);
        ed_related_clear(&current);
        return NULL;
    }
    if (text && find_html_comment(text, len, "related-sales", &start, &end, &body_start, &body_end) == 0) {
        char *indent = comment_indent(text, start);
        char *rebuilt = build_related_comment(indent ? indent : "", vehicle_type, make_type, vehicle_name, override_ids);
        free(indent);
        free(comment);
        comment = rebuilt;
        next = comment ? ed_replace(text, start, end, comment) : NULL;
    } else if (text && find_html_comment(text, len, "page-values", &start, &end, &body_start, &body_end) == 0) {
        char *block = (char *)malloc(strlen(comment) + 8);
        if (!block) {
            next = NULL;
        } else {
            snprintf(block, strlen(comment) + 8, "\n\n  %s", comment);
            next = ed_insert_at(text, end, block);
            free(block);
        }
    } else if (ed_newsletter_file(file, text)) {
        char *plain = build_related_comment("", vehicle_type, make_type, vehicle_name, override_ids);
        size_t pos = ed_ci_pos(text ? text : "", "<!doctype");
        char *block;
        free(comment);
        comment = plain;
        if (pos == (size_t)-1) {
            pos = 0;
        }
        block = comment ? (char *)malloc(strlen(comment) + 4) : NULL;
        if (!block) {
            next = NULL;
        } else {
            snprintf(block, strlen(comment) + 4, "%s\n\n", comment);
            next = ed_insert_at(text ? text : "", pos, block);
            free(block);
        }
    } else {
        size_t pos = ed_after_charset_or_head(text);
        char *block = comment ? (char *)malloc(strlen(comment) + 8) : NULL;
        if (!block) {
            next = NULL;
        } else {
            snprintf(block, strlen(comment) + 8, "\n  %s\n", comment);
            next = ed_insert_at(text ? text : "", pos, block);
            free(block);
        }
    }
    free(comment);
    if (next) {
        int want_include = ed_data_bool(data, "hasInclude", 1);
        const char *hit = strstr(next, "similar_vehicles.php");
        size_t include_start = 0;
        size_t include_end = 0;
        int found = 0;
        while (hit && !found) {
            if (ed_ssi_bounds(next, hit, &include_start, &include_end)) {
                found = 1;
            } else {
                hit = strstr(hit + 1, "similar_vehicles.php");
            }
        }
        if (!want_include) {
            if (found) {
                char *cut = ed_cut(next, include_start, include_end);
                free(next);
                next = cut;
            }
        } else if (found) {
            char *replaced = ed_replace(next, include_start, include_end, ED_SIMILAR_INCLUDE);
            free(next);
            next = replaced;
        } else {
            size_t pos = ed_ci_pos(next, "<script src=\"/JS/lightbox");
            char *block;
            if (pos == (size_t)-1) {
                pos = ed_ci_pos(next, "<!-- Start eXTReMe");
            }
            if (pos == (size_t)-1) {
                pos = ed_ci_pos(next, "</body>");
            }
            if (pos == (size_t)-1) {
                pos = strlen(next);
            }
            block = (char *)malloc(strlen(ED_SIMILAR_INCLUDE) + 2);
            if (block) {
                snprintf(block, strlen(ED_SIMILAR_INCLUDE) + 2, "%s\n", ED_SIMILAR_INCLUDE);
                {
                    char *with = ed_insert_at(next, pos, block);
                    free(next);
                    next = with;
                }
                free(block);
            }
        }
    }
    free(vehicle_type);
    free(make_type);
    free(vehicle_name);
    free(override_ids);
    ed_related_clear(&current);
    if (!next) {
        set_err(err, err_sz, "Could not update related-sales");
    }
    return next;
}

static int ed_valid_pid(const char *pid) {
    size_t i;
    size_t n;
    if (!pid || !pid[0]) {
        return 0;
    }
    n = strlen(pid);
    if (n > 32) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)pid[i];
        if (!isalnum(ch) && ch != '_' && ch != '-') {
            return 0;
        }
    }
    return 1;
}

static const char *ed_action(const JsonValue *data) {
    JsonValue *value;
    const char *text;
    if (!data || !json_object_has(data, "action")) {
        return "";
    }
    value = json_object_get(data, "action");
    if (!value || json_type(value) != JSON_STRING) {
        return "";
    }
    text = json_string_get(value, NULL);
    return text ? text : "";
}

static int ed_data_index(const JsonValue *data, const char *key, int *out) {
    long long number = 0;
    if (!data || !out || !json_object_has(data, key)) {
        return -1;
    }
    if (json_as_int(json_object_get(data, key), &number) != 0 || number < 0 || number > 100000) {
        return -1;
    }
    *out = (int)number;
    return 0;
}

static int ed_data_cursor(const JsonValue *data, size_t len, size_t *out) {
    long long number = 0;
    if (!data || !out || !json_object_has(data, "cursor")) {
        return -1;
    }
    if (json_as_int(json_object_get(data, "cursor"), &number) != 0 || number <= 0) {
        return -1;
    }
    if ((size_t)number > len) {
        number = (long long)len;
    }
    *out = (size_t)number;
    return 0;
}

static char *ed_replace_literal(const char *text, const char *from, const char *to) {
    const char *hit;
    char *next;
    char *again;
    if (!text || !from || !from[0]) {
        return strdup(text ? text : "");
    }
    hit = strstr(text, from);
    if (!hit) {
        return strdup(text);
    }
    next = ed_replace(text, (size_t)(hit - text), (size_t)(hit - text) + strlen(from), to ? to : "");
    if (!next || !strstr(next, from)) {
        return next;
    }
    again = ed_replace_literal(next, from, to);
    free(next);
    return again;
}

static int ed_valid_measurement(const char *id) {
    size_t i;
    size_t n;
    if (!id || strncmp(id, "G-", 2) != 0) {
        return 0;
    }
    n = strlen(id);
    if (n < 4 || n > 32) {
        return 0;
    }
    for (i = 2; i < n; i++) {
        unsigned char ch = (unsigned char)id[i];
        if (!isalnum(ch) && ch != '_' && ch != '-') {
            return 0;
        }
    }
    return 1;
}

static char *ed_google_block(const char *id) {
    const char *use = id && id[0] ? id : ed_ga_id();
    Buf buf;
    buf_init(&buf);
    if (buf_printf(&buf,
                   "  <!-- Google tag (gtag.js) -->\n"
                   "  <script async src=\"https://www.googletagmanager.com/gtag/js?id=%s\"></script>\n"
                   "  <script>\n"
                   "    window.dataLayer = window.dataLayer || [];\n"
                   "    function gtag() { dataLayer.push(arguments); }\n"
                   "    gtag('js', new Date());\n"
                   "\n"
                   "    gtag('config', '%s');\n"
                   "  </script>\n",
                   use, use) != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static const char *ed_default_pid(const EdFile *file) {
    if (!file || !file->section) {
        return "articles";
    }
    if (strcmp(file->section, "awards") == 0) {
        return "awards";
    }
    if (strcmp(file->section, "projects") == 0) {
        return "projects";
    }
    if (strcmp(file->section, "newsletter") == 0) {
        return "newsletr";
    }
    if (strcmp(file->section, "parts") == 0) {
        return "parts";
    }
    if (strcmp(file->section, "contact") == 0) {
        return "contacts";
    }
    if (strcmp(file->section, "video") == 0) {
        return "video";
    }
    if (strcmp(file->section, "whatsnew") == 0) {
        return "whtsnew";
    }
    if (strcmp(file->section, "sales") == 0) {
        return "salesinv";
    }
    return "articles";
}

static int ed_ssi_span(const char *text, const char *token, int nth, size_t *start, size_t *end) {
    const char *p = text;
    int seen = 0;
    if (!text || !token || !start || !end) {
        return -1;
    }
    while ((p = strstr(p, token)) != NULL) {
        size_t from = 0;
        size_t to = 0;
        if (ed_ssi_bounds(text, p, &from, &to)) {
            if (seen == nth) {
                *start = from;
                *end = to;
                return 0;
            }
            seen++;
            p = text + to;
            continue;
        }
        p += strlen(token);
    }
    return -1;
}

static int ed_page_target_ok(const char *page) {
    size_t i;
    size_t n;
    if (!page) {
        return 0;
    }
    n = strlen(page);
    if (n > 500) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)page[i];
        if (ch < 0x20 || ch == '"' || ch == '\\' || ch == '<' || ch == '>') {
            return 0;
        }
    }
    if (page[0] && page[0] != '/') {
        return 0;
    }
    return strstr(page, "-->") == NULL;
}

static char *ed_index_include(const char *page) {
    char *out;
    size_t n = strlen(page ? page : "") + 80;
    out = (char *)malloc(n);
    if (!out) {
        return NULL;
    }
    snprintf(out, n, "<!--#include virtual=\"/php/index_sales_links.php?page=%s\" -->", page ? page : "");
    return out;
}

static size_t ed_list_card_insert(const char *text) {
    const char *card = text ? strstr(text, "D_35p_1f list") : NULL;
    const char *region_end;
    const char *p;
    const char *best = NULL;
    if (!card) {
        return (size_t)-1;
    }
    region_end = strstr(card + 12, "D_35p_1f");
    if (!region_end) {
        region_end = strstr(card, "</body>");
    }
    if (!region_end) {
        region_end = text + strlen(text);
    }
    p = card;
    while (p < region_end) {
        const char *close = strstr(p, "</div>");
        const char *q;
        int closes = 1;
        if (!close || close >= region_end) {
            break;
        }
        q = close + 6;
        while (closes < 3 && q < region_end) {
            while (q < region_end && isspace((unsigned char)*q)) {
                q++;
            }
            if ((size_t)(region_end - q) >= 6 && strncmp(q, "</div>", 6) == 0) {
                closes++;
                q += 6;
            } else {
                break;
            }
        }
        if (closes == 3) {
            best = close;
        }
        p = close + 6;
    }
    return best ? (size_t)(best - text) : (size_t)-1;
}

static char *ed_apply_index(const char *text, const JsonValue *data, char *err, size_t err_sz) {
    const char *action = ed_action(data);
    const char *source = text ? text : "";
    char *page = ed_data_str(data, "page", "");
    char *include = NULL;
    char *next = NULL;
    int index = 0;
    size_t start = 0;
    size_t end = 0;
    if (!page) {
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (!ed_page_target_ok(page)) {
        free(page);
        set_err(err, err_sz, "Sales link page must be empty or a site path without quotes");
        return NULL;
    }
    if (strcmp(action, "remove") == 0 || strcmp(action, "update") == 0) {
        if (ed_data_index(data, "index", &index) != 0 ||
            ed_ssi_span(source, "index_sales_links.php?page=", index, &start, &end) != 0) {
            free(page);
            set_err(err, err_sz, "That sales link include is no longer in the page");
            return NULL;
        }
    }
    if (strcmp(action, "remove") == 0) {
        next = ed_cut(source, start, end);
        free(page);
        return next;
    }
    include = ed_index_include(page);
    free(page);
    if (!include) {
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (strcmp(action, "update") == 0) {
        next = ed_replace(source, start, end, include);
    } else {
        size_t pos = (size_t)-1;
        size_t cursor = 0;
        char *block = (char *)malloc(strlen(include) + 16);
        if (ed_data_cursor(data, strlen(source), &cursor) == 0) {
            pos = cursor;
        } else {
            pos = ed_list_card_insert(source);
            if (pos == (size_t)-1) {
                pos = ed_ci_pos(source, "</body>");
            }
            if (pos == (size_t)-1) {
                pos = strlen(source);
            }
        }
        if (block) {
            snprintf(block, strlen(include) + 16, "\n        %s\n", include);
            next = ed_insert_at(source, pos, block);
            free(block);
        }
    }
    free(include);
    if (!next) {
        set_err(err, err_sz, "Could not update the sales link include");
    }
    return next;
}

static int ed_nn_id_ok(const char *id) {
    size_t i;
    size_t n;
    if (!id || !id[0]) {
        return 0;
    }
    n = strlen(id);
    if (n > 12) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!isdigit((unsigned char)id[i])) {
            return 0;
        }
    }
    return 1;
}

static int ed_nn_span(const char *text, int nth, size_t *start, size_t *end, char *id, size_t id_sz) {
    const char *p = text ? text : "";
    int seen = 0;
    if (!start || !end || !id || id_sz < 2) {
        return -1;
    }
    while ((p = strstr(p, "<!--")) != NULL) {
        const char *body = p + 4;
        const char *num;
        const char *close;
        size_t n = 0;
        while (*body == ' ' || *body == '\t') {
            body++;
        }
        if (strncmp(body, "NN", 2) == 0 && (body[2] == ' ' || body[2] == '\t')) {
            num = body + 2;
            while (*num == ' ' || *num == '\t') {
                num++;
            }
            while (isdigit((unsigned char)num[n])) {
                n++;
            }
            close = strstr(num + n, "-->");
            if (n > 0 && n < id_sz && close && (size_t)(close - (num + n)) < 8) {
                if (seen == nth) {
                    *start = (size_t)(p - (text ? text : ""));
                    *end = (size_t)(close - (text ? text : "")) + 3;
                    memcpy(id, num, n);
                    id[n] = '\0';
                    return 0;
                }
                seen++;
            }
        }
        p += 4;
    }
    return -1;
}

static char *ed_drop_nn_anchor(char *text, const char *id) {
    char attr[40];
    const char *hit;
    const char *open;
    const char *close;
    char *next;
    if (!text || !id || !id[0]) {
        return text;
    }
    snprintf(attr, sizeof(attr), "id=\"NN%s\"", id);
    hit = strstr(text, attr);
    if (!hit) {
        snprintf(attr, sizeof(attr), "id='NN%s'", id);
        hit = strstr(text, attr);
    }
    if (!hit) {
        return text;
    }
    open = hit;
    while (open > text && (size_t)(hit - open) < 80 && memcmp(open, "<span", 5) != 0) {
        open--;
    }
    close = strstr(hit, "</span>");
    if (memcmp(open, "<span", 5) != 0 || !close || (size_t)(close - hit) > 180) {
        return text;
    }
    close += 7;
    if (*close == '\r') {
        close++;
    }
    if (*close == '\n') {
        close++;
    }
    next = ed_cut(text, (size_t)(open - text), (size_t)(close - text));
    free(text);
    return next;
}

static char *ed_apply_nn(const char *text, const JsonValue *data, char *err, size_t err_sz) {
    const char *action = ed_action(data);
    const char *source = text ? text : "";
    char *id = ed_data_str(data, "id", "");
    char old_id[16];
    int index = 0;
    size_t start = 0;
    size_t end = 0;
    char *next = NULL;
    old_id[0] = '\0';
    if (!id) {
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (strcmp(action, "remove") == 0 || strcmp(action, "update") == 0) {
        if (ed_data_index(data, "index", &index) != 0 || ed_nn_span(source, index, &start, &end, old_id, sizeof(old_id)) != 0) {
            free(id);
            set_err(err, err_sz, "That NN marker is no longer in the page");
            return NULL;
        }
    }
    if (strcmp(action, "remove") == 0) {
        next = ed_cut(source, start, end);
        next = ed_drop_nn_anchor(next, old_id);
        free(id);
        return next;
    }
    if (!ed_nn_id_ok(id)) {
        free(id);
        set_err(err, err_sz, "NN id must be digits");
        return NULL;
    }
    if (strcmp(action, "update") == 0) {
        char canon[48];
        char old_attr[40];
        char new_attr[40];
        snprintf(canon, sizeof(canon), "<!-- NN %s -->", id);
        next = ed_replace(source, start, end, canon);
        if (next && strcmp(old_id, id) != 0) {
            snprintf(old_attr, sizeof(old_attr), "id=\"NN%s\"", old_id);
            snprintf(new_attr, sizeof(new_attr), "id=\"NN%s\"", id);
            if (strstr(next, old_attr)) {
                char *swapped = ed_replace_literal(next, old_attr, new_attr);
                free(next);
                next = swapped;
            } else {
                const char *hit = strstr(next, canon);
                if (hit) {
                    char span[96];
                    char *with;
                    snprintf(span, sizeof(span), "\n<span id=\"NN%s\" class=\"bookmark-anchor\"></span>", id);
                    with = ed_insert_at(next, (size_t)(hit - next) + strlen(canon), span);
                    free(next);
                    next = with;
                }
            }
        }
    } else {
        char block[160];
        size_t pos = (size_t)-1;
        size_t cursor = 0;
        snprintf(block, sizeof(block), "<!-- NN %s -->\n<span id=\"NN%s\" class=\"bookmark-anchor\"></span>\n", id, id);
        if (ed_data_cursor(data, strlen(source), &cursor) == 0) {
            pos = cursor;
        } else if (ed_nn_span(source, 0, &start, &end, old_id, sizeof(old_id)) == 0) {
            pos = start;
        } else {
            const char *begin = strstr(source, "Begin vehicle listings");
            if (begin) {
                const char *nl = strchr(begin, '\n');
                pos = nl ? (size_t)(nl - source) + 1 : (size_t)(begin - source);
            } else {
                pos = ed_ci_pos(source, "</body>");
                if (pos == (size_t)-1) {
                    pos = strlen(source);
                }
            }
        }
        next = ed_insert_at(source, pos, block);
    }
    free(id);
    if (!next) {
        set_err(err, err_sz, "Could not update the NN marker");
    }
    return next;
}

static int ed_search_span(const char *text, size_t *start, size_t *end) {
    const char *hit = text ? strstr(text, "SEARCH_IMAGE:") : NULL;
    if (!hit) {
        return -1;
    }
    return ed_ssi_bounds(text, hit, start, end) ? 0 : -1;
}

static char *ed_apply_search(const EdFile *file, const char *text, const JsonValue *data, char *err, size_t err_sz) {
    const char *source = text ? text : "";
    char *path = ed_data_str(data, "path", "");
    char *comment = NULL;
    char *next = NULL;
    size_t start = 0;
    size_t end = 0;
    int enabled = ed_data_enabled(data, 1);
    if (!path) {
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    if (!enabled) {
        if (ed_search_span(source, &start, &end) == 0) {
            next = ed_cut(source, start, end);
        } else {
            next = strdup(source);
        }
        free(path);
        return next;
    }
    if (!ed_field_ok(path, err, err_sz, "Search image")) {
        free(path);
        return NULL;
    }
    comment = (char *)malloc(strlen(path) + 32);
    if (!comment) {
        free(path);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    snprintf(comment, strlen(path) + 32, "<!-- SEARCH_IMAGE: %s -->", path);
    if (ed_search_span(source, &start, &end) == 0) {
        next = ed_replace(source, start, end, comment);
    } else if (ed_newsletter_file(file, source)) {
        size_t pos = ed_ci_pos(source, "<!doctype");
        char *block = (char *)malloc(strlen(comment) + 2);
        if (pos == (size_t)-1) {
            pos = 0;
        }
        if (block) {
            snprintf(block, strlen(comment) + 2, "%s\n", comment);
            next = ed_insert_at(source, pos, block);
            free(block);
        }
    } else {
        size_t pos = ed_after_head_open(source);
        char *block = (char *)malloc(strlen(comment) + 4);
        if (block) {
            snprintf(block, strlen(comment) + 4, "\n  %s", comment);
            next = ed_insert_at(source, pos, block);
            free(block);
        }
    }
    free(comment);
    free(path);
    if (!next) {
        set_err(err, err_sz, "Could not update the search image");
    }
    return next;
}

static char *ed_apply_archive(const char *text, const JsonValue *data, char *err, size_t err_sz) {
    const char *source = text ? text : "";
    int enabled = ed_data_enabled(data, 1);
    if (!enabled) {
        return ed_cut_include_token(source, "newsletter-archive");
    }
    if (strstr(source, "newsletter-archive")) {
        return strdup(source);
    }
    {
        char *next = ed_insert_at(source, ed_after_charset_or_head(source), "\n  <!-- newsletter-archive -->\n");
        if (!next) {
            set_err(err, err_sz, "Could not add the newsletter archive marker");
        }
        return next;
    }
}

static size_t ed_subscribe_pos(const char *text) {
    const char *hit = text ? strstr(text, "similar_vehicles.php") : NULL;
    size_t start = 0;
    size_t end = 0;
    size_t pos;
    while (hit) {
        if (ed_ssi_bounds(text, hit, &start, &end)) {
            return start;
        }
        hit = strstr(hit + 1, "similar_vehicles.php");
    }
    pos = text ? ed_ci_pos(text, "<!-- Start eXTReMe") : (size_t)-1;
    if (pos == (size_t)-1 && text) {
        pos = ed_ci_pos(text, "</body>");
    }
    if (pos == (size_t)-1) {
        return text ? strlen(text) : 0;
    }
    return pos;
}

static char *ed_apply_subscribe(const char *text, const JsonValue *data, char *err, size_t err_sz) {
    const char *source = text ? text : "";
    const char *include = "<!--#include virtual=\"/includes/subscribe.html\" -->\n";
    int enabled = ed_data_enabled(data, 1);
    if (!enabled) {
        if (!strstr(source, "/includes/subscribe.html")) {
            return strdup(source);
        }
        return ed_cut_include_token(source, "/includes/subscribe.html");
    }
    if (strstr(source, "/includes/subscribe.html")) {
        return strdup(source);
    }
    {
        char *next = ed_insert_at(source, ed_subscribe_pos(source), include);
        if (!next) {
            set_err(err, err_sz, "Could not add the subscribe box");
        }
        return next;
    }
}

static int ed_sales_type_ok(const char *type) {
    size_t i;
    size_t n;
    if (!type || !type[0] || strlen(type) > 32) {
        return 0;
    }
    n = strlen(type);
    for (i = 0; i < n; i++) {
        unsigned char ch = (unsigned char)type[i];
        if (!isalnum(ch) && ch != '_' && ch != '-') {
            return 0;
        }
    }
    return 1;
}

static void ed_take_field(char **slot, const JsonValue *data, const char *key) {
    char *text;
    if (!slot || !data || !json_object_has(data, key)) {
        return;
    }
    text = json_text_dup(json_object_get(data, key));
    if (!text) {
        return;
    }
    free(*slot);
    *slot = text;
}

static char *ed_apply_sales_listing(const char *text, const JsonValue *data, char *err, size_t err_sz) {
    PhpHeader php;
    GallerySalesHeader header;
    size_t len = text ? strlen(text) : 0;
    size_t out_len = 0;
    char *rendered;
    int hidden = 0;
    memset(&php, 0, sizeof(php));
    memset(&header, 0, sizeof(header));
    if (!text || scan_php_header(text, len, &php) != 0) {
        php_header_clear(&php);
        set_err(err, err_sz, "Could not parse sales page header");
        return NULL;
    }
    header_from_php(&php, &header);
    php_header_clear(&php);
    ed_take_field(&header.vehicle_name, data, "vehicleName");
    ed_take_field(&header.vehicle_type, data, "vehicleType");
    ed_take_field(&header.make_type, data, "makeType");
    ed_take_field(&header.inventory_number, data, "inventoryNumber");
    ed_take_field(&header.price, data, "price");
    if (data && json_object_has(data, "doNotRender") && json_boolish(json_object_get(data, "doNotRender"), &hidden) == 0) {
        header.do_not_render = hidden;
    }
    if (!header.vehicle_name || !header.vehicle_type || !header.make_type || !header.inventory_number || !header.price ||
        !header.description || !ed_field_ok(header.vehicle_name, err, err_sz, "Vehicle name") ||
        !ed_field_ok(header.vehicle_type, err, err_sz, "Vehicle type") ||
        !ed_field_ok(header.make_type, err, err_sz, "Make") ||
        !ed_field_ok(header.inventory_number, err, err_sz, "Inventory number") ||
        !ed_field_ok(header.price, err, err_sz, "Price") || !ed_sales_type_ok(header.vehicle_type)) {
        if (err && !err[0]) {
            set_err(err, err_sz, "Vehicle type must be car, motorcycle, truck, or sidecar");
        }
        gallery_sales_header_free(&header);
        return NULL;
    }
    rendered = gallery_apply_sales_header(text, len, &header, &out_len, err, err_sz);
    gallery_sales_header_free(&header);
    return rendered;
}

static char *ed_mutate(const EdFile *file, const char *text, const char *widget, const JsonValue *data, int *run_render,
                       char *err, size_t err_sz) {
    int enabled;
    *run_render = 0;
    if (!widget || !widget[0]) {
        set_err(err, err_sz, "widget is required");
        return NULL;
    }
    if (ed_sales_vehicle(file) &&
        (strcmp(widget, "pageValues") == 0 || strcmp(widget, "relatedSales") == 0 ||
         strcmp(widget, "googleTracking") == 0 || strcmp(widget, "extremeTracking") == 0 ||
         strcmp(widget, "adminTracking") == 0)) {
        int adding = ed_data_bool(data, "adding", 0);
        int explicit = 0;
        if (strcmp(widget, "pageValues") == 0) {
            explicit = text && strstr(text, "<!-- page-values") != NULL;
        } else if (strcmp(widget, "relatedSales") == 0) {
            explicit = text && (strstr(text, "<!-- related-sales") != NULL || strstr(text, "similar_vehicles.php") != NULL);
        } else if (strcmp(widget, "googleTracking") == 0) {
            explicit = ed_google_present(text);
        } else if (strcmp(widget, "extremeTracking") == 0) {
            explicit = text && strstr(text, "extreme-dm.com") != NULL;
        } else if (strcmp(widget, "adminTracking") == 0) {
            explicit = text && (ed_header_include(text) || strstr(text, "trackerforfeatures.html") || strstr(text, "mainmenu.html"));
        }
        if (!adding && !explicit) {
            if (strcmp(widget, "pageValues") == 0 || strcmp(widget, "relatedSales") == 0) {
                set_err(err, err_sz, "Page values and related sales apply to articles, awards, and projects pages");
            } else {
                set_err(err, err_sz,
                        "Sales vehicle pages inherit Google Analytics, eXTReMe, and admin tracking from php/inventory.php");
            }
            return NULL;
        }
    }
    if (strcmp(widget, "pageValues") == 0) {
        char *title = ed_data_str(data, "title", "");
        char *description = ed_data_str(data, "description", "");
        int ready = ed_data_enabled(data, 1) && title && title[0] && description && description[0];
        free(title);
        free(description);
        *run_render = ed_watched_section(file->section) && ready;
        return ed_apply_page_values(text, data, err, err_sz);
    }
    if (strcmp(widget, "relatedSales") == 0) {
        *run_render = ed_watched_section(file->section);
        return ed_apply_related(file, text, data, err, err_sz);
    }
    if (strcmp(widget, "googleTracking") == 0) {
        char *id = ed_data_str(data, "measurementId", ed_ga_id());
        char *stripped;
        char *block;
        char *next;
        size_t pos;
        enabled = ed_data_enabled(data, 1);
        if (!id || !id[0]) {
            free(id);
            id = strdup(ed_ga_id());
        }
        if (!enabled) {
            free(id);
            return ed_remove_google(text ? text : "");
        }
        if (!id || !ed_valid_measurement(id)) {
            free(id);
            set_err(err, err_sz, "Measurement ID must start with G- followed by letters or numbers");
            return NULL;
        }
        stripped = ed_remove_google(text ? text : "");
        block = ed_google_block(id);
        free(id);
        if (!stripped || !block) {
            free(stripped);
            free(block);
            set_err(err, err_sz, "Could not update the Google tag");
            return NULL;
        }
        pos = ed_ci_pos(stripped, "</head>");
        if (pos == (size_t)-1) {
            pos = strlen(stripped);
        }
        next = ed_insert_at(stripped, pos, block);
        free(stripped);
        free(block);
        return next;
    }
    if (strcmp(widget, "extremeTracking") == 0) {
        char *pid = ed_data_str(data, "pid", "");
        char *login = ed_data_str(data, "login", ed_extreme_login());
        char *block;
        char *stripped;
        char *next;
        int noscript = ed_data_bool(data, "hasNoscript", 1);
        enabled = ed_data_enabled(data, 1);
        if (!pid || !pid[0]) {
            free(pid);
            pid = strdup(ed_default_pid(file));
        }
        if (!login || !login[0]) {
            free(login);
            login = strdup(ed_extreme_login());
        }
        if (!enabled) {
            free(pid);
            free(login);
            return ed_remove_extreme(text ? text : "");
        }
        if (!pid || !login || !ed_valid_pid(pid) || !ed_valid_pid(login)) {
            free(pid);
            free(login);
            set_err(err, err_sz, "Tracker login and pid must be letters, numbers, dash, or underscore");
            return NULL;
        }
        stripped = ed_remove_extreme(text ? text : "");
        block = ed_extreme_block_full(pid, login, noscript);
        free(pid);
        free(login);
        if (!stripped || !block) {
            free(stripped);
            free(block);
            set_err(err, err_sz, "Could not update the eXTReMe tracker");
            return NULL;
        }
        {
            size_t pos = ed_ci_pos(stripped, "</body>");
            if (pos == (size_t)-1) {
                pos = strlen(stripped);
            }
            next = ed_insert_at(stripped, pos, block);
        }
        free(stripped);
        free(block);
        return next;
    }
    if (strcmp(widget, "adminTracking") == 0) {
        char *swapped;
        char *swapped_menu;
        char *ensured;
        int upgrade = ed_data_bool(data, "upgrade", 0);
        enabled = ed_data_enabled(data, 1);
        if (!enabled && !upgrade) {
            return ed_remove_header_include(text ? text : "");
        }
        swapped = ed_replace_literal(text ? text : "", "/includes/trackerforfeatures.html", "/includes/header.html");
        if (!swapped) {
            return NULL;
        }
        swapped_menu = ed_replace_literal(swapped, "/includes/mainmenu.html", "/includes/header.html");
        free(swapped);
        if (!swapped_menu) {
            return NULL;
        }
        ensured = ed_ensure_header(swapped_menu);
        free(swapped_menu);
        return ensured;
    }
    if (strcmp(widget, "searchImage") == 0) {
        return ed_apply_search(file, text, data, err, err_sz);
    }
    if (strcmp(widget, "newsletterArchive") == 0) {
        return ed_apply_archive(text, data, err, err_sz);
    }
    if (strcmp(widget, "newsletterSubscribe") == 0) {
        return ed_apply_subscribe(text, data, err, err_sz);
    }
    if (strcmp(widget, "indexSalesLinks") == 0) {
        return ed_apply_index(text, data, err, err_sz);
    }
    if (strcmp(widget, "projectInventory") == 0) {
        return ed_apply_nn(text, data, err, err_sz);
    }
    if (strcmp(widget, "salesListing") == 0) {
        if (!ed_sales_vehicle(file)) {
            set_err(err, err_sz, "Vehicle listing fields apply to a sales vehicle index.php");
            return NULL;
        }
        return ed_apply_sales_listing(text, data, err, err_sz);
    }
    set_err(err, err_sz, "Unknown widget");
    return NULL;
}

int gallery_widget_apply(const char *repo_root, const char *public_path, const char *content, int has_content,
                         const char *widget, const JsonValue *data, int dry_run, JsonDoc **out, char *err,
                         size_t err_sz) {
    EdFile file;
    char *source = NULL;
    char *updated = NULL;
    int run_render = 0;
    int rc;
    char render_err[512];
    int render_failed = 0;
    memset(&file, 0, sizeof(file));
    render_err[0] = '\0';
    if (!out) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    *out = NULL;
    rc = ed_resolve(repo_root, public_path, &file, 1, err, err_sz);
    if (rc != 0) {
        ed_file_clear(&file);
        return rc;
    }
    if (file.media) {
        ed_file_clear(&file);
        set_err(err, err_sz, "This file is a document or media asset and is not edited as text");
        return 1;
    }
    if (has_content) {
        if (!content) {
            ed_file_clear(&file);
            set_err(err, err_sz, "content is required");
            return 1;
        }
        source = strdup(content);
    } else {
        source = file.text ? strdup(file.text) : strdup("");
    }
    if (!source) {
        ed_file_clear(&file);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    if (strcmp(widget, "newsletterArchive") == 0 && ed_data_bool(data, "clean", 0)) {
        free(source);
        if (!file.public_path || !strstr(file.public_path, "/newsletter")) {
            ed_file_clear(&file);
            set_err(err, err_sz, "Newsletter cleaning applies to articles/newsletter pages");
            return 1;
        }
        if (ed_run_newsletter_clean(repo_root, err, err_sz) != 0) {
            ed_file_clear(&file);
            return err && err[0] ? 1 : -1;
        }
        ed_file_clear(&file);
        return gallery_page_document(repo_root, public_path, out, err, err_sz);
    }
    updated = ed_mutate(&file, source, widget, data, &run_render, err, err_sz);
    free(source);
    if (!updated) {
        ed_file_clear(&file);
        return err && err[0] ? 1 : -1;
    }
    if (dry_run) {
        JsonValue *root;
        if (run_render && strstr(updated, "<!-- page-values")) {
            char *rendered = ed_render_memory(repo_root, file.fs, updated, render_err, sizeof(render_err));
            if (!rendered) {
                render_failed = 1;
            } else {
                free(updated);
                updated = rendered;
            }
        }
        free(file.text);
        file.text = updated;
        root = json_object();
        if (!root) {
            ed_file_clear(&file);
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        jput_bool(root, "ok", 1);
        rc = ed_fill_loaded(root, repo_root, &file, 1, 1, err, err_sz);
        if (rc != 0) {
            json_free(root);
            ed_file_clear(&file);
            return rc;
        }
        *out = json_doc_new(root);
        ed_file_clear(&file);
        if (!*out) {
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        if (render_failed) ed_note_render_error(*out, render_err);
        return 0;
    }
    if (atomic_write(file.fs, updated, strlen(updated)) != 0) {
        free(updated);
        ed_file_clear(&file);
        set_err(err, err_sz, "Could not write the page");
        return -1;
    }
    if (run_render && strstr(updated, "<!-- page-values")) {
        if (ed_run_render(repo_root, file.fs, render_err, sizeof(render_err)) != 0) {
            render_failed = 1;
        }
    }
    free(updated);
    ed_file_clear(&file);
    rc = gallery_page_document(repo_root, public_path, out, err, err_sz);
    if (rc == 0 && render_failed && out && *out) {
        ed_note_render_error(*out, render_err);
    }
    return rc;
}

static void ed_add_template(JsonValue *list, const char *id, const char *label, const char *section, const char *filename,
                            const char *description) {
    JsonValue *item = json_object();
    JsonValue *creates = json_array();
    if (!item || !creates) {
        json_free(item);
        json_free(creates);
        return;
    }
    jput_str(item, "id", id);
    jput_str(item, "label", label);
    jput_str(item, "section", section);
    jput_str(item, "defaultFilename", filename);
    jput_str(item, "description", description);
    if (strcmp(id, "sales-vehicle") == 0) {
        json_array_add(creates, json_string_cstr("slides/"));
        json_array_add(creates, json_string_cstr("thumbs/"));
        json_array_add(creates, json_string_cstr("deep-data.json"));
        json_array_add(creates, json_string_cstr("image_data.txt"));
    } else if (strcmp(id, "blank") != 0) {
        json_array_add(creates, json_string_cstr("slides/"));
        json_array_add(creates, json_string_cstr("thumbs/"));
    }
    json_object_put(item, "creates", creates);
    json_array_add(list, item);
}

JsonDoc *gallery_templates_document(char *err, size_t err_sz) {
    JsonValue *root = json_object();
    JsonValue *list = json_array();
    (void)err;
    (void)err_sz;
    if (!root || !list) {
        json_free(root);
        json_free(list);
        return NULL;
    }
    jput_bool(root, "ok", 1);
    json_object_put(root, "templates", list);
    ed_add_template(list, "blank", "None (Blank Page)", "", "index.html",
                    "A blank HTML or PHP page in the section you choose. Gallery sections also get empty slides/ and thumbs/ folders.");
    ed_add_template(list, "article-2026", "2026 Lightbox Article / Event Coverage", "articles", "<slug>.html",
                    "A lightbox article with page-values, related sales, generated SEO, the 2026 article CSS, Google Analytics, the admin header include, Lightbox2, and the eXTReMe articles tracker. Creates slides/ and thumbs/.");
    ed_add_template(list, "award-2026", "Award Showcase with Lightbox Gallery", "awards", "index.html",
                    "An award page with the same lightbox gallery, page-values, generated SEO, and the eXTReMe awards tracker. Creates slides/ and thumbs/.");
    ed_add_template(list, "project-2026", "Restoration Project Gallery", "projects", "index.html",
                    "A restoration project page with the lightbox gallery, page-values, generated SEO, and the eXTReMe projects tracker. Creates slides/ and thumbs/.");
    ed_add_template(list, "sales-vehicle", "Inventory Vehicle Listing", "sales", "index.php",
                    "A sales vehicle index.php plus an empty deep-data.json, image_data.txt, slides/, and thumbs/.");
    return finish_doc(root);
}

static char *ed_article_css(const char *repo) {
    char *path = NULL;
    char *text;
    const char *open_tag;
    const char *close_tag;
    char *css;
    if (repo) {
        path = join_path3(repo, "articles/greenwichconcours2026", "greenwichconcours2026.html");
    }
    text = path ? read_file(path, NULL) : NULL;
    free(path);
    if (!text) {
        return strdup("\n    .articleContainer { box-sizing: border-box; width: 100%; padding: 0 10px; }\n    .thumbs-wrapper { display: flex; flex-direction: column; align-items: center; width: 100%; }\n");
    }
    open_tag = strstr(text, "<style>");
    close_tag = open_tag ? strstr(open_tag, "</style>") : NULL;
    if (!open_tag || !close_tag) {
        free(text);
        return strdup("");
    }
    css = strndup(open_tag + 7, (size_t)(close_tag - (open_tag + 7)));
    free(text);
    return css ? css : strdup("");
}

static int ed_buf_paragraphs(Buf *buf, const char *text) {
    const char *p = text ? text : "";
    if (!p[0]) {
        return buf_puts(buf, "<p></p>");
    }
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        char *line = trim_dup(p, n);
        if (line && line[0]) {
            if (buf_puts(buf, "<p>") != 0 || ed_buf_html(buf, line) != 0 || buf_puts(buf, "</p>\n") != 0) {
                free(line);
                return -1;
            }
        }
        free(line);
        if (!nl) {
            break;
        }
        p = nl + 1;
    }
    return 0;
}

static char *ed_html_template(const char *repo, const char *title, const char *subtitle, const char *author,
                              const char *description, const char *date, const char *vehicle_type, const char *make_type,
                              const char *vehicle_name, const char *override_ids, const char *pid, const char *heading) {
    char *css = ed_article_css(repo);
    char *block;
    Buf buf;
    buf_init(&buf);
    buf_puts(&buf, "<!doctype html>\n<html lang=\"en-US\">\n\n<head>\n<meta charset=\"utf-8\" />\n");
    buf_puts(&buf, "  <!-- page-values\n");
    buf_puts(&buf, "  title: ");
    buf_puts(&buf, title ? title : "");
    buf_puts(&buf, "\n  description: ");
    buf_puts(&buf, description ? description : "");
    buf_puts(&buf, "\n  type: Article\n  date: ");
    buf_puts(&buf, date ? date : "");
    buf_puts(&buf, "\n  -->\n\n  <!-- related-sales\n  vehicleType: ");
    buf_puts(&buf, vehicle_type ? vehicle_type : "");
    buf_puts(&buf, "\n  makeType: ");
    buf_puts(&buf, make_type ? make_type : "");
    buf_puts(&buf, "\n  vehicleName: ");
    buf_puts(&buf, vehicle_name ? vehicle_name : "");
    buf_puts(&buf, "\n  Override IDs: ");
    buf_puts(&buf, override_ids ? override_ids : "");
    buf_puts(&buf, "\n  -->\n\n  <!-- generated-seo -->\n  <!-- /generated-seo -->\n\n");
    buf_puts(&buf, "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n");
    buf_puts(&buf, "  <meta http-equiv=\"Content-Language\" content=\"en-us\">\n");
    buf_puts(&buf, "  <link rel=\"stylesheet\" type=\"text/css\" href=\"/style/classicCarsGeneric.css\">\n");
    buf_puts(&buf, "  <link rel=\"stylesheet\" type=\"text/css\" href=\"/JS/lightbox2-2.11.3/dist/css/lightbox.css\">\n\n  <style>");
    buf_puts(&buf, css ? css : "");
    buf_puts(&buf, "</style>\n\n");
    {
        char *gtag = ed_google_block(ed_ga_id());
        if (gtag) {
            buf_puts(&buf, gtag);
            free(gtag);
        }
    }
    buf_puts(&buf, "</head>\n\n<body class=\"textJustify\">\n");
    buf_puts(&buf, ED_HEADER_INCLUDE);
    buf_puts(&buf, "\n\n  <div class=\"articleContainer\">\n    <h1 class=\"centerText\">");
    ed_buf_html(&buf, title ? title : "");
    buf_puts(&buf, "</h1>\n");
    if (subtitle && subtitle[0]) {
        buf_puts(&buf, "    <h3 class=\"centerText articleSubtitle\">");
        ed_buf_html(&buf, subtitle);
        buf_puts(&buf, "</h3>\n");
    }
    buf_puts(&buf, "    <h5 class=\"centerText articleByline\">By ");
    ed_buf_html(&buf, author && author[0] ? author : "Nicole LeBoeuf");
    buf_puts(&buf, "</h5>\n\n");
    ed_buf_paragraphs(&buf, description ? description : "");
    buf_puts(&buf, "\n    <hr>\n\n    <section class=\"articleGallerySection\" aria-label=\"");
    ed_buf_html(&buf, heading ? heading : "Photo Gallery");
    buf_puts(&buf, "\">\n      <h2 class=\"centerText\">");
    ed_buf_html(&buf, heading ? heading : "Photo Gallery");
    buf_puts(&buf, "</h2>\n");
    buf_puts(&buf, "      <p class=\"centerText bodyCaption articleGalleryNote\">Click any photo to view full size.</p>\n");
    buf_puts(&buf, "      <div class=\"thumbs-wrapper\">\n        <div class=\"thumbs\"></div>\n      </div>\n    </section>\n  </div>\n\n");
    buf_puts(&buf, ED_SIMILAR_INCLUDE);
    buf_puts(&buf, "\n<script src=\"/JS/lightbox2-2.11.3/dist/js/lightbox-plus-jquery.js\"></script>\n");
    block = ed_extreme_block(pid);
    buf_puts(&buf, block ? block : "");
    buf_puts(&buf, "</body>\n\n</html>\n");
    free(css);
    free(block);
    return buf_steal(&buf);
}

static char *ed_php_sq(const char *text) {
    Buf buf;
    const char *p;
    buf_init(&buf);
    if (buf_putc(&buf, '\'') != 0) {
        buf_free(&buf);
        return NULL;
    }
    for (p = text ? text : ""; *p; p++) {
        if (*p == '\\' || *p == '\'') {
            if (buf_putc(&buf, '\\') != 0) {
                buf_free(&buf);
                return NULL;
            }
        }
        if (buf_putc(&buf, *p) != 0) {
            buf_free(&buf);
            return NULL;
        }
    }
    if (buf_putc(&buf, '\'') != 0) {
        buf_free(&buf);
        return NULL;
    }
    return buf_steal(&buf);
}

static char *ed_sales_template(const char *vehicle_type, const char *make_type, const char *vehicle_name,
                               const char *inventory, const char *price, const char *description) {
    char *type_q = ed_php_sq(vehicle_type && vehicle_type[0] ? vehicle_type : "car");
    char *make_q = ed_php_sq(make_type ? make_type : "");
    char *name_q = ed_php_sq(vehicle_name ? vehicle_name : "");
    char *inv_q = NULL;
    char *price_q = NULL;
    const char *marker = "DESC";
    Buf buf;
    const char *p;
    if (description) {
        p = description;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            char *line = trim_dup(p, n);
            if (line && strcmp(line, "DESC") == 0) {
                marker = "STUDIO_DESC";
            }
            free(line);
            if (!nl) {
                break;
            }
            p = nl + 1;
        }
    }
    if (inventory && is_php_number(inventory)) {
        inv_q = strdup(inventory);
    } else if (inventory && inventory[0]) {
        inv_q = ed_php_sq(inventory);
    } else {
        inv_q = strdup("''");
    }
    if (price && is_php_number(price)) {
        price_q = strdup(price);
    } else if (price && price[0]) {
        price_q = ed_php_sq(price);
    } else {
        price_q = strdup("0");
    }
    buf_init(&buf);
    buf_puts(&buf, "<?php\n\n// Page Variables\n\n\n");
    buf_printf(&buf, "$vehicleType = %s;\n", type_q ? type_q : "''");
    buf_printf(&buf, "$makeType = %s;\n", make_q ? make_q : "''");
    buf_puts(&buf, "$doNotRender = false;\n");
    buf_printf(&buf, "$vehicleName = %s;\n", name_q ? name_q : "''");
    buf_printf(&buf, "$inventoryNumber = %s;\n", inv_q ? inv_q : "''");
    buf_printf(&buf, "$price = %s;\n", price_q ? price_q : "0");
    buf_printf(&buf, "$description = <<<%s\n", marker);
    buf_puts(&buf, description ? description : "");
    if (description && description[0] && description[strlen(description) - 1] != '\n') {
        buf_putc(&buf, '\n');
    }
    buf_printf(&buf, "%s;\n\n", marker);
    buf_puts(&buf, "// Inventory PHP (Processes the logic with variables provided by this file)\n\n");
    buf_puts(&buf, "function read_image_data($gallery_folder)\n{\n");
    buf_puts(&buf, "    $image_data_file = $gallery_folder . '/image_data.txt';\n    $images = [];\n\n");
    buf_puts(&buf, "    if (file_exists($image_data_file)) {\n");
    buf_puts(&buf, "        $lines = file($image_data_file, FILE_IGNORE_NEW_LINES | FILE_SKIP_EMPTY_LINES);\n");
    buf_puts(&buf, "        for ($i = 0; $i < count($lines); $i++) {\n");
    buf_puts(&buf, "            $image_name = $lines[$i];\n");
    buf_puts(&buf, "            $caption = isset($lines[$i + 1]) && substr($lines[$i + 1], 0, 1) == '=' ? substr($lines[++$i], 1) : '';\n");
    buf_puts(&buf, "            $images[] = ['image_name' => $image_name, 'caption' => $caption];\n        }\n    }\n\n    return $images;\n}\n\n");
    buf_puts(&buf, "$image_data = read_image_data(__DIR__);\n\n");
    buf_puts(&buf, "$rootPath = $_SERVER['DOCUMENT_ROOT'];\nrequire_once($rootPath . '/php/inventory.php');\n?>\n");
    free(type_q);
    free(make_q);
    free(name_q);
    free(inv_q);
    free(price_q);
    return buf_steal(&buf);
}

static void ed_today(char *out, size_t out_sz) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (!local) {
        snprintf(out, out_sz, "2026-01-01");
        return;
    }
    strftime(out, out_sz, "%Y-%m-%d", local);
}

static char *ed_blank_document(int php) {
    if (php) {
        return strdup("<?php\n");
    }
    return strdup("<!doctype html>\n"
                  "<html lang=\"en-US\">\n"
                  "<head>\n"
                  "<meta charset=\"utf-8\" />\n"
                  "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\" />\n"
                  "<title></title>\n"
                  "</head>\n"
                  "<body>\n"
                  "\n"
                  "</body>\n"
                  "</html>\n");
}

int gallery_page_create(const char *repo_root, const JsonValue *body, JsonDoc **out, char *err, size_t err_sz) {
    char *template_id = ed_data_str(body, "templateId", "");
    char *section = ed_data_str(body, "section", "");
    char *slug = ed_data_str(body, "slug", "");
    char *filename = ed_data_str(body, "filename", "");
    char *title = ed_data_str(body, "title", "");
    char *subtitle = ed_data_str(body, "subtitle", "");
    char *author = ed_data_str(body, "author", "");
    char *description = ed_data_str(body, "description", "");
    char *vehicle_type = ed_data_str(body, "vehicleType", "");
    char *make_type = ed_data_str(body, "makeType", "");
    char *vehicle_name = ed_data_str(body, "vehicleName", "");
    char *override_ids = ed_data_str(body, "overrideIds", "");
    char *inventory = ed_data_str(body, "inventoryNumber", "");
    char *price = ed_data_str(body, "price", "");
    char *one_title = NULL;
    char *one_description = NULL;
    char *one_subtitle = NULL;
    char *folder = NULL;
    char *target = NULL;
    char *public_path = NULL;
    char *page = NULL;
    char date[16];
    const char *expect = NULL;
    const char *pid = "articles";
    const char *heading = "Photo Gallery";
    int html = 1;
    int blank = 0;
    int gallery_dirs = 1;
    JsonValue *root;
    int rc = 0;

    if (out) {
        *out = NULL;
    }
    blank = !template_id || !template_id[0] || strcmp(template_id, "blank") == 0 || strcmp(template_id, "none") == 0;
    if (blank) {
        gallery_dirs = gallery_section_ok(section);
        if (!ed_content_section(section)) {
            set_err(err, err_sz, section && section[0] ? "Unknown section" : "Section is required");
            rc = 1;
            goto done;
        }
    } else if (strcmp(template_id, "article-2026") == 0) {
        expect = "articles";
        pid = "articles";
        heading = "Photo Gallery";
    } else if (strcmp(template_id, "award-2026") == 0) {
        expect = "awards";
        pid = "awards";
        heading = "Award Photo Gallery";
    } else if (strcmp(template_id, "project-2026") == 0) {
        expect = "projects";
        pid = "projects";
        heading = "Restoration Photo Gallery";
    } else if (strcmp(template_id, "sales-vehicle") == 0) {
        expect = "sales";
        html = 0;
    } else {
        set_err(err, err_sz, "Unknown template");
        rc = 1;
        goto done;
    }
    if (!blank) {
        if (!section[0]) {
            free(section);
            section = strdup(expect);
        }
        if (!section || strcmp(section, expect) != 0) {
            set_err(err, err_sz, "Template does not match that section");
            rc = 1;
            goto done;
        }
    }
    if (!gallery_valid_slug(slug)) {
        set_err(err, err_sz, "Folder name is not allowed");
        rc = 1;
        goto done;
    }
    if (!filename || !filename[0]) {
        free(filename);
        if (blank && strcmp(section, "sales") == 0) {
            filename = strdup("index.php");
        } else if (blank && strcmp(section, "articles") != 0) {
            filename = strdup("index.html");
        } else if (!blank && !html) {
            filename = strdup("index.php");
        } else if (blank || strcmp(template_id, "article-2026") == 0) {
            filename = (char *)malloc(strlen(slug) + 6);
            if (filename) {
                snprintf(filename, strlen(slug) + 6, "%s.html", slug);
            }
        } else {
            filename = strdup("index.html");
        }
    }
    if (blank) {
        html = !(filename && ends_with_ci(filename, ".php"));
    }
    if (!filename || !is_page_file(filename) || strchr(filename, '/') || strchr(filename, '\\') || strstr(filename, "..") ||
        filename[0] == '.' || (html && !ed_html_page(filename)) || (!html && !ends_with_ci(filename, ".php"))) {
        set_err(err, err_sz, blank ? "Filename is not allowed" : "Filename is not allowed for this template");
        rc = 1;
        goto done;
    }
    if (!blank) {
        if (!ed_field_ok(title, err, err_sz, "Title") || !ed_field_ok(vehicle_name, err, err_sz, "Vehicle name") ||
            !ed_field_ok(make_type, err, err_sz, "Make") || !ed_field_ok(vehicle_type, err, err_sz, "Vehicle type") ||
            !ed_field_ok(override_ids, err, err_sz, "Override IDs") || !ed_field_ok(inventory, err, err_sz, "Inventory number") ||
            !ed_field_ok(price, err, err_sz, "Price") || !ed_field_ok(author, err, err_sz, "Author") ||
            !ed_field_ok(subtitle, err, err_sz, "Subtitle")) {
            rc = 1;
            goto done;
        }
        one_title = ed_one_line(title && title[0] ? title : (vehicle_name && vehicle_name[0] ? vehicle_name : slug));
        one_description = ed_one_line(description && description[0] ? description : (one_title ? one_title : slug));
        one_subtitle = ed_one_line(subtitle);
        if (!one_title || !one_description || field_has_break(one_title) || field_has_break(one_description)) {
            set_err(err, err_sz, "Title and description must be a single line");
            rc = 1;
            goto done;
        }
        if (!vehicle_type[0]) {
            free(vehicle_type);
            vehicle_type = strdup("car");
        }
        if (!html && !ed_valid_vehicle_type(vehicle_type)) {
            set_err(err, err_sz, "vehicleType must be car, motorcycle, sidecar, or truck");
            rc = 1;
            goto done;
        }
        if (!vehicle_name[0]) {
            free(vehicle_name);
            vehicle_name = strdup(one_title);
        }
    }
    folder = join_path3(repo_root, section, slug);
    target = folder ? join_path(folder, filename) : NULL;
    public_path = ed_public_join(section, slug, filename);
    if (!folder || !target || !public_path) {
        set_err(err, err_sz, "out of memory");
        rc = -1;
        goto done;
    }
    if (file_exists(target)) {
        set_err(err, err_sz, "A page already exists at that path.");
        rc = 3;
        goto done;
    }
    {
        char *section_dir = join_path(repo_root, section);
        if (!section_dir || !dir_exists(section_dir) || !path_within(repo_root, section_dir)) {
            free(section_dir);
            set_err(err, err_sz, "Content section was not found");
            rc = 2;
            goto done;
        }
        free(section_dir);
    }
    if (ed_mkdir_one(folder) != 0) {
        set_err(err, err_sz, "Could not create the page folder");
        rc = -1;
        goto done;
    }
    if (gallery_dirs) {
        char *slides = join_path(folder, "slides");
        char *thumbs = join_path(folder, "thumbs");
        if (ed_mkdir_one(slides) != 0 || ed_mkdir_one(thumbs) != 0 || !path_within(repo_root, folder)) {
            free(slides);
            free(thumbs);
            set_err(err, err_sz, "Could not create slides or thumbs");
            rc = -1;
            goto done;
        }
        free(slides);
        free(thumbs);
    }
    ed_today(date, sizeof(date));
    if (blank) {
        page = ed_blank_document(!html);
    } else if (html) {
        page = ed_html_template(repo_root, one_title, one_subtitle, author && author[0] ? author : "Nicole LeBoeuf",
                                one_description, date, vehicle_type, make_type, vehicle_name, override_ids, pid, heading);
    } else {
        page = ed_sales_template(vehicle_type, make_type, vehicle_name, inventory, price,
                                 description && description[0] ? description : one_description);
        {
            char *deep = join_path(folder, "deep-data.json");
            char *image_data = join_path(folder, "image_data.txt");
            const char *empty_deep =
                "{\"transitionSpeed\":600,\"deepCounters\":{\"total\":0,\"images\":0,\"files\":0},\"counters\":{\"total\":0,\"images\":0,\"files\":0},\"objects\":[]}\n";
            if (!deep || !image_data || atomic_write(deep, empty_deep, strlen(empty_deep)) != 0 ||
                atomic_write(image_data, "", 0) != 0) {
                free(deep);
                free(image_data);
                set_err(err, err_sz, "Could not create gallery files");
                rc = -1;
                goto done;
            }
            free(deep);
            free(image_data);
        }
    }
    if (!page || atomic_write(target, page, strlen(page)) != 0) {
        set_err(err, err_sz, "Could not write the new page");
        rc = -1;
        goto done;
    }
    if (html && !blank) {
        char render_err[512];
        render_err[0] = '\0';
        if (ed_run_render(repo_root, target, render_err, sizeof(render_err)) != 0) {
            root = json_object();
            if (!root) {
                set_err(err, err_sz, "out of memory");
                rc = -1;
                goto done;
            }
            jput_bool(root, "ok", 1);
            jput_str(root, "path", public_path);
            jput_str(root, "section", section);
            jput_str(root, "slug", slug);
            jput_str(root, "renderError", render_err);
            if (out) {
                *out = json_doc_new(root);
            } else {
                json_free(root);
            }
            rc = 0;
            goto done;
        }
    }
    root = json_object();
    if (!root) {
        set_err(err, err_sz, "out of memory");
        rc = -1;
        goto done;
    }
    jput_bool(root, "ok", 1);
    jput_str(root, "path", public_path);
    jput_str(root, "section", section);
    jput_str(root, "slug", slug);
    if (out) {
        *out = json_doc_new(root);
        if (!*out) {
            json_free(root);
            set_err(err, err_sz, "out of memory");
            rc = -1;
        }
    } else {
        json_free(root);
    }
done:
    free(template_id);
    free(section);
    free(slug);
    free(filename);
    free(title);
    free(subtitle);
    free(author);
    free(description);
    free(vehicle_type);
    free(make_type);
    free(vehicle_name);
    free(override_ids);
    free(inventory);
    free(price);
    free(one_title);
    free(one_description);
    free(one_subtitle);
    free(folder);
    free(target);
    free(public_path);
    free(page);
    return rc;
}
