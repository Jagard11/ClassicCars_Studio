#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "project_search.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <io.h>
#include <windows.h>
#ifndef _MSC_VER
#include <dirent.h>
#endif
#else
#include <dirent.h>
#include <strings.h>
#include <unistd.h>
#endif

#include "compat.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#define MAX_SEARCH_FILE_BYTES (4u * 1024u * 1024u)
#define DEFAULT_MAX_RESULTS   2000
#define MAX_CAPTURE_GROUPS    10
#define REGEX_STEP_LIMIT      100000

typedef struct SBuf {
    char *data;
    size_t len;
    size_t cap;
} SBuf;

static void sbuf_init(SBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void sbuf_free(SBuf *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int sbuf_reserve(SBuf *b, size_t add) {
    size_t need;
    size_t next;
    char * grown;
    if (!b) return -1;
    if (add > SIZE_MAX - b->len - 1) return -1;
    need = b->len + add + 1;
    if (need <= b->cap) return 0;
    next = b->cap ? b->cap : 128;
    while (next < need) {
        if (next > SIZE_MAX / 2) {
            next = need;
            break;
        }
        next *= 2;
    }
    grown = (char *)realloc(b->data, next);
    if (!grown) return -1;
    b->data = grown;
    b->cap = next;
    return 0;
}

static int sbuf_append(SBuf *b, const char *s, size_t n) {
    if (sbuf_reserve(b, n) != 0) return -1;
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int sbuf_putc(SBuf *b, char c) {
    return sbuf_append(b, &c, 1);
}

static int sbuf_puts(SBuf *b, const char *s) {
    return s ? sbuf_append(b, s, strlen(s)) : 0;
}

static char *sbuf_steal(SBuf *b) {
    char *out;
    if (!b) return NULL;
    if (!b->data) return strdup("");
    out = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return out;
}

static void set_err(char *err, size_t err_sz, const char *fmt, ...) {
    va_list ap;
    if (!err || err_sz == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, err_sz, fmt, ap);
    va_end(ap);
}

static int ends_with_ci(const char *s, const char *suffix) {
    size_t ns;
    size_t nf;
    if (!s || !suffix) return 0;
    ns = strlen(s);
    nf = strlen(suffix);
    if (nf > ns) return 0;
    return strcasecmp(s + ns - nf, suffix) == 0;
}

static int is_searchable_ext(const char *name) {
    if (!name || !name[0]) return 0;
    return ends_with_ci(name, ".html") || ends_with_ci(name, ".htm") || ends_with_ci(name, ".shtml") ||
           ends_with_ci(name, ".php") || ends_with_ci(name, ".css") || ends_with_ci(name, ".js") ||
           ends_with_ci(name, ".json") || ends_with_ci(name, ".txt") || ends_with_ci(name, ".xml") ||
           ends_with_ci(name, ".csv");
}

static int is_skipped_dir(const char *name, int at_root) {
    if (!name || !name[0] || name[0] == '.') return 1;
    if (strcasecmp(name, "slides") == 0 || strcasecmp(name, "thumbs") == 0 ||
        strcasecmp(name, "res") == 0 || strcasecmp(name, "album") == 0 ||
        strcasecmp(name, "temphidden") == 0 || strcasecmp(name, "node_modules") == 0) {
        return 1;
    }
    if (at_root) {
        if (strcasecmp(name, "studio") == 0 || strcasecmp(name, "build") == 0 ||
            strcasecmp(name, "data") == 0 || strcasecmp(name, "admin") == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_skipped_file(const char *name) {
    if (!name || !name[0] || name[0] == '.') return 1;
    if (strcasecmp(name, "Thumbs.db") == 0 || strcasecmp(name, "ehthumbs.db") == 0 ||
        strcasecmp(name, "desktop.ini") == 0 || strcasecmp(name, "lifeboat.zip") == 0 ||
        strcasecmp(name, "album.rss") == 0 || strcasecmp(name, "manifest.jmf") == 0) {
        return 1;
    }
    if (strncasecmp(name, ".fuse_hidden", 12) == 0) return 1;
    return !is_searchable_ext(name);
}

static int natcasecmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            const char *as = a;
            const char *bs = b;
            const char *ae;
            const char *be;
            size_t al;
            size_t bl;
            int cmp;
            while (*as == '0') as++;
            while (*bs == '0') bs++;
            ae = as;
            be = bs;
            while (isdigit((unsigned char)*ae)) ae++;
            while (isdigit((unsigned char)*be)) be++;
            al = (size_t)(ae - as);
            bl = (size_t)(be - bs);
            if (al != bl) return al < bl ? -1 : 1;
            cmp = al ? strncmp(as, bs, al) : 0;
            if (cmp) return cmp;
            a = ae;
            b = be;
            continue;
        }
        {
            unsigned char ca = (unsigned char)tolower((unsigned char)*a);
            unsigned char cb = (unsigned char)tolower((unsigned char)*b);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

static int cmp_str_ptr(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return natcasecmp(sa, sb);
}

typedef struct FileList {
    char **items;
    size_t count;
    size_t cap;
} FileList;

static void fl_init(FileList *fl) {
    fl->items = NULL;
    fl->count = 0;
    fl->cap = 0;
}

static void fl_free(FileList *fl) {
    size_t i;
    if (!fl) return;
    for (i = 0; i < fl->count; i++) free(fl->items[i]);
    free(fl->items);
    fl->items = NULL;
    fl->count = 0;
    fl->cap = 0;
}

static int fl_add(FileList *fl, const char *rel) {
    char *copy;
    if (fl->count == fl->cap) {
        size_t next = fl->cap ? fl->cap * 2 : 64;
        char **grown = (char **)realloc(fl->items, next * sizeof(*grown));
        if (!grown) return -1;
        fl->items = grown;
        fl->cap = next;
    }
    copy = strdup(rel ? rel : "");
    if (!copy) return -1;
    fl->items[fl->count++] = copy;
    return 0;
}

/* Glob matching for include/exclude file filters.
 * '**' matches across '/', '*' matches within a path segment, '?' matches 1 char. */
static int glob_match_inner(const char *pat, const char *str) {
    while (*pat) {
        if (pat[0] == '*' && pat[1] == '*') {
            pat += 2;
            if (*pat == '/') {
                if (glob_match_inner(pat + 1, str)) return 1;
            }
            while (*pat == '*') pat++;
            if (!*pat) return 1;
            for (; *str; str++) {
                if (glob_match_inner(pat, str)) return 1;
            }
            return 0;
        }
        if (*pat == '*') {
            pat++;
            for (; *str && *str != '/'; str++) {
                if (glob_match_inner(pat, str)) return 1;
            }
            return glob_match_inner(pat, str);
        }
        if (*pat == '?') {
            if (!*str || *str == '/') return 0;
            pat++;
            str++;
            continue;
        }
        if (tolower((unsigned char)*pat) != tolower((unsigned char)*str)) {
            return 0;
        }
        pat++;
        str++;
    }
    return *str == '\0';
}

static int single_file_glob_matches(const char *raw_pat, const char *rel_path) {
    char pat[512];
    size_t len;
    const char *p = raw_pat;
    const char *rel = rel_path;
    const char *base;
    if (!p || !rel) return 0;
    while (*p == ' ' || *p == '\t') p++;
    while (*p == '/') p++;
    len = strlen(p);
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) len--;
    if (len == 0 || len >= sizeof(pat)) return 0;
    memcpy(pat, p, len);
    pat[len] = '\0';

    while (*rel == '/') rel++;
    base = strrchr(rel, '/');
    base = base ? base + 1 : rel;

    /* If pattern ends with '/', treat as directory prefix */
    if (pat[len - 1] == '/') {
        pat[len - 1] = '\0';
        len--;
        if (strncasecmp(rel, pat, len) == 0 && (rel[len] == '/' || rel[len] == '\0')) {
            return 1;
        }
        return 0;
    }
    /* If pattern has no '/' and no '*', e.g. "sales" or "index.php" */
    if (!strchr(pat, '/')) {
        if (glob_match_inner(pat, base)) return 1;
        if (glob_match_inner(pat, rel)) return 1;
        /* Also check if it's a directory name prefix like "sales" */
        if (!strchr(pat, '*') && !strchr(pat, '?') && !strchr(pat, '.')) {
            if (strncasecmp(rel, pat, len) == 0 && rel[len] == '/') return 1;
        }
        return 0;
    }
    if (glob_match_inner(pat, rel)) return 1;
    /* Also allow "sales/" + "*" to match nested "sales/foo/index.php" */
    if (len >= 2 && pat[len - 2] == '/' && pat[len - 1] == '*') {
        size_t prefix_len = len - 1;
        if (strncasecmp(rel, pat, prefix_len) == 0) return 1;
    }
    return 0;
}

static int pattern_list_matches(const char *list, const char *rel_path, int empty_default) {
    const char *p = list;
    int saw_any = 0;
    if (!p) return empty_default;
    while (*p) {
        char token[512];
        size_t n = 0;
        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') p++;
        if (!*p) break;
        while (*p && *p != ',' && *p != ';') {
            if (n + 1 < sizeof(token)) token[n++] = *p;
            p++;
        }
        token[n] = '\0';
        if (n > 0) {
            saw_any = 1;
            if (single_file_glob_matches(token, rel_path)) return 1;
        }
    }
    return saw_any ? 0 : empty_default;
}

static void walk_searchable_files(const char *repo_root, const char *rel_dir, int depth,
                                  const char *include_globs, const char *exclude_globs,
                                  FileList *out) {
    char abs_dir[PATH_MAX];
    DIR *dir;
    struct dirent *ent;
    if (depth > 8 || out->count >= 10000) return;
    if (rel_dir && rel_dir[0]) {
        if (snprintf(abs_dir, sizeof(abs_dir), "%s/%s", repo_root, rel_dir) >= (int)sizeof(abs_dir)) return;
    } else {
        if (snprintf(abs_dir, sizeof(abs_dir), "%s", repo_root) >= (int)sizeof(abs_dir)) return;
    }
    dir = opendir(abs_dir);
    if (!dir) return;
    while ((ent = readdir(dir)) != NULL) {
        char child_rel[PATH_MAX];
        char child_abs[PATH_MAX];
        struct stat st;
        const char *name = ent->d_name;
        if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (rel_dir && rel_dir[0]) {
            if (snprintf(child_rel, sizeof(child_rel), "%s/%s", rel_dir, name) >= (int)sizeof(child_rel)) continue;
        } else {
            if (snprintf(child_rel, sizeof(child_rel), "%s", name) >= (int)sizeof(child_rel)) continue;
        }
        if (snprintf(child_abs, sizeof(child_abs), "%s/%s", repo_root, child_rel) >= (int)sizeof(child_abs)) continue;
        if (stat(child_abs, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (is_skipped_dir(name, depth == 0)) continue;
            walk_searchable_files(repo_root, child_rel, depth + 1, include_globs, exclude_globs, out);
        } else if (S_ISREG(st.st_mode)) {
            if (is_skipped_file(name)) continue;
            if ((size_t)st.st_size > MAX_SEARCH_FILE_BYTES) continue;
            if (!pattern_list_matches(include_globs, child_rel, 1)) continue;
            if (pattern_list_matches(exclude_globs, child_rel, 0)) continue;
            fl_add(out, child_rel);
        }
    }
    closedir(dir);
}

static char *read_text_file(const char *fs_path, size_t *out_len) {
    FILE *fp;
    long sz;
    char *buf;
    size_t got;
    if (out_len) *out_len = 0;
    fp = fopen(fs_path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    sz = ftell(fp);
    if (sz < 0 || (unsigned long)sz > MAX_SEARCH_FILE_BYTES) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)sz + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    got = sz > 0 ? fread(buf, 1, (size_t)sz, fp) : 0;
    fclose(fp);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    /* Skip binary files containing NUL bytes */
    if (got > 0 && memchr(buf, '\0', got) != NULL) {
        free(buf);
        return NULL;
    }
    buf[got] = '\0';
    if (out_len) *out_len = got;
    return buf;
}

static int atomic_write_text(const char *path, const char *data, size_t len) {
    char tmp[PATH_MAX];
    FILE *fp;
    if (!path || !path[0]) return -1;
    if (snprintf(tmp, sizeof(tmp), "%s.studio-tmp", path) >= (int)sizeof(tmp)) return -1;
    fp = fopen(tmp, "wb");
    if (!fp) return -1;
    if (len > 0 && fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    if (fflush(fp) != 0) {
        fclose(fp);
        remove(tmp);
        return -1;
    }
    fclose(fp);
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
#endif
    return 0;
}

/* =========================================================================
 * Custom Portable Regex Engine (supports literal, wildcard, and regex modes)
 * ========================================================================= */

typedef enum RxNodeKind {
    RX_EMPTY = 0,
    RX_SEQ,
    RX_ALT,
    RX_QUANT,
    RX_GROUP,
    RX_CHAR,
    RX_DOT,
    RX_CLASS,
    RX_ANCHOR_BOL,
    RX_ANCHOR_EOL,
    RX_BOUNDARY,
    RX_NOT_BOUNDARY
} RxNodeKind;

typedef struct RxNode RxNode;

struct RxNode {
    RxNodeKind kind;
    char ch;
    unsigned char bitmap[32];
    int negated;
    int group_idx;
    int min_rep;
    int max_rep; /* -1 for unbounded */
    int greedy;
    RxNode *left;
    RxNode *right;
};

typedef struct RxCompiler {
    const char *src;
    size_t pos;
    size_t len;
    int case_sensitive;
    int next_group;
    char err[256];
} RxCompiler;

typedef struct RxCaptures {
    const char *start[MAX_CAPTURE_GROUPS];
    const char *end[MAX_CAPTURE_GROUPS];
} RxCaptures;

static void rx_free(RxNode *node) {
    if (!node) return;
    rx_free(node->left);
    rx_free(node->right);
    free(node);
}

static RxNode *rx_alloc(RxNodeKind kind) {
    RxNode *n = (RxNode *)calloc(1, sizeof(*n));
    if (!n) return NULL;
    n->kind = kind;
    return n;
}

static void bitmap_set(unsigned char bm[32], unsigned char c, int case_sensitive) {
    bm[c >> 3] |= (unsigned char)(1u << (c & 7));
    if (!case_sensitive) {
        unsigned char lo = (unsigned char)tolower(c);
        unsigned char hi = (unsigned char)toupper(c);
        bm[lo >> 3] |= (unsigned char)(1u << (lo & 7));
        bm[hi >> 3] |= (unsigned char)(1u << (hi & 7));
    }
}

static int bitmap_test(const unsigned char bm[32], unsigned char c) {
    return (bm[c >> 3] & (unsigned char)(1u << (c & 7))) != 0;
}

static int is_word_byte(unsigned char c) {
    return isalnum(c) || c == '_';
}

static void bitmap_add_shorthand(unsigned char bm[32], char code, int case_sensitive) {
    int c;
    for (c = 0; c < 256; c++) {
        int hit = 0;
        if (code == 'd' || code == 'D') {
            hit = isdigit(c) != 0;
            if (code == 'D') hit = !hit;
        } else if (code == 'w' || code == 'W') {
            hit = is_word_byte((unsigned char)c);
            if (code == 'W') hit = !hit;
        } else if (code == 's' || code == 'S') {
            hit = isspace(c) != 0;
            if (code == 'S') hit = !hit;
        }
        if (hit) bitmap_set(bm, (unsigned char)c, case_sensitive);
    }
}

static RxNode *parse_alternation(RxCompiler *c);

static int parse_escape_char(RxCompiler *c, unsigned char *out_ch, char *out_shorthand) {
    unsigned char ch;
    *out_shorthand = 0;
    if (c->pos >= c->len) {
        snprintf(c->err, sizeof(c->err), "Trailing backslash in pattern");
        return -1;
    }
    ch = (unsigned char)c->src[c->pos++];
    switch (ch) {
        case 'n': *out_ch = '\n'; return 0;
        case 'r': *out_ch = '\r'; return 0;
        case 't': *out_ch = '\t'; return 0;
        case 'd': case 'D': case 'w': case 'W': case 's': case 'S':
            *out_shorthand = (char)ch;
            return 0;
        default:
            *out_ch = ch;
            return 0;
    }
}

static RxNode *parse_char_class(RxCompiler *c) {
    RxNode *node = rx_alloc(RX_CLASS);
    int first = 1;
    int have_prev = 0;
    unsigned char prev_ch = 0;
    if (!node) return NULL;
    if (c->pos < c->len && c->src[c->pos] == '^') {
        node->negated = 1;
        c->pos++;
    }
    while (c->pos < c->len) {
        unsigned char ch = (unsigned char)c->src[c->pos];
        char shorthand = 0;
        if (ch == ']' && !first) {
            c->pos++;
            return node;
        }
        first = 0;
        c->pos++;
        if (ch == '\\') {
            if (parse_escape_char(c, &ch, &shorthand) != 0) {
                rx_free(node);
                return NULL;
            }
            if (shorthand) {
                bitmap_add_shorthand(node->bitmap, shorthand, c->case_sensitive);
                have_prev = 0;
                continue;
            }
        }
        if (have_prev && ch == '-' && c->pos < c->len && c->src[c->pos] != ']') {
            unsigned char end_ch = (unsigned char)c->src[c->pos++];
            char end_short = 0;
            int k;
            if (end_ch == '\\') {
                if (parse_escape_char(c, &end_ch, &end_short) != 0) {
                    rx_free(node);
                    return NULL;
                }
                if (end_short) {
                    bitmap_set(node->bitmap, '-', c->case_sensitive);
                    bitmap_add_shorthand(node->bitmap, end_short, c->case_sensitive);
                    have_prev = 0;
                    continue;
                }
            }
            if (prev_ch > end_ch) {
                unsigned char tmp = prev_ch;
                prev_ch = end_ch;
                end_ch = tmp;
            }
            for (k = (int)prev_ch; k <= (int)end_ch; k++) {
                bitmap_set(node->bitmap, (unsigned char)k, c->case_sensitive);
            }
            have_prev = 0;
            continue;
        }
        bitmap_set(node->bitmap, ch, c->case_sensitive);
        prev_ch = ch;
        have_prev = 1;
    }
    snprintf(c->err, sizeof(c->err), "Unclosed character class '['");
    rx_free(node);
    return NULL;
}

static RxNode *parse_atom(RxCompiler *c) {
    unsigned char ch;
    if (c->pos >= c->len) return rx_alloc(RX_EMPTY);
    ch = (unsigned char)c->src[c->pos];
    if (ch == '(') {
        int cap_idx = 0;
        RxNode *inner;
        RxNode *grp;
        c->pos++;
        if (c->pos + 1 < c->len && c->src[c->pos] == '?' && c->src[c->pos + 1] == ':') {
            c->pos += 2;
        } else {
            if (c->next_group < MAX_CAPTURE_GROUPS) {
                cap_idx = c->next_group++;
            }
        }
        inner = parse_alternation(c);
        if (!inner || c->err[0]) {
            rx_free(inner);
            return NULL;
        }
        if (c->pos >= c->len || c->src[c->pos] != ')') {
            snprintf(c->err, sizeof(c->err), "Unclosed group '('");
            rx_free(inner);
            return NULL;
        }
        c->pos++;
        if (cap_idx > 0) {
            grp = rx_alloc(RX_GROUP);
            if (!grp) {
                rx_free(inner);
                return NULL;
            }
            grp->group_idx = cap_idx;
            grp->left = inner;
            return grp;
        }
        return inner;
    }
    if (ch == '[') {
        c->pos++;
        return parse_char_class(c);
    }
    if (ch == '.') {
        c->pos++;
        return rx_alloc(RX_DOT);
    }
    if (ch == '^') {
        c->pos++;
        return rx_alloc(RX_ANCHOR_BOL);
    }
    if (ch == '$') {
        c->pos++;
        return rx_alloc(RX_ANCHOR_EOL);
    }
    if (ch == '\\') {
        unsigned char esc = 0;
        char shorthand = 0;
        RxNode *node;
        c->pos++;
        if (c->pos < c->len && (c->src[c->pos] == 'b' || c->src[c->pos] == 'B')) {
            char bch = c->src[c->pos++];
            return rx_alloc(bch == 'b' ? RX_BOUNDARY : RX_NOT_BOUNDARY);
        }
        if (parse_escape_char(c, &esc, &shorthand) != 0) return NULL;
        if (shorthand) {
            node = rx_alloc(RX_CLASS);
            if (!node) return NULL;
            bitmap_add_shorthand(node->bitmap, shorthand, c->case_sensitive);
            return node;
        }
        node = rx_alloc(RX_CHAR);
        if (!node) return NULL;
        node->ch = (char)esc;
        return node;
    }
    if (ch == '*' || ch == '+' || ch == '?' || ch == '{') {
        snprintf(c->err, sizeof(c->err), "Unexpected quantifier '%c'", (char)ch);
        return NULL;
    }
    c->pos++;
    {
        RxNode *node = rx_alloc(RX_CHAR);
        if (!node) return NULL;
        node->ch = (char)ch;
        return node;
    }
}

static int parse_braced_quant(RxCompiler *c, int *min_out, int *max_out) {
    size_t save = c->pos;
    int min_v = 0;
    int max_v = 0;
    int digits = 0;
    if (c->pos >= c->len || c->src[c->pos] != '{') return 0;
    c->pos++;
    while (c->pos < c->len && isdigit((unsigned char)c->src[c->pos])) {
        min_v = min_v * 10 + (c->src[c->pos] - '0');
        if (min_v > 1000) min_v = 1000;
        c->pos++;
        digits++;
    }
    if (!digits || c->pos >= c->len) {
        c->pos = save;
        return 0;
    }
    if (c->src[c->pos] == '}') {
        c->pos++;
        *min_out = min_v;
        *max_out = min_v;
        return 1;
    }
    if (c->src[c->pos] != ',') {
        c->pos = save;
        return 0;
    }
    c->pos++;
    if (c->pos < c->len && c->src[c->pos] == '}') {
        c->pos++;
        *min_out = min_v;
        *max_out = -1;
        return 1;
    }
    digits = 0;
    while (c->pos < c->len && isdigit((unsigned char)c->src[c->pos])) {
        max_v = max_v * 10 + (c->src[c->pos] - '0');
        if (max_v > 1000) max_v = 1000;
        c->pos++;
        digits++;
    }
    if (!digits || c->pos >= c->len || c->src[c->pos] != '}' || max_v < min_v) {
        c->pos = save;
        return 0;
    }
    c->pos++;
    *min_out = min_v;
    *max_out = max_v;
    return 1;
}

static RxNode *parse_quantified(RxCompiler *c) {
    RxNode *atom = parse_atom(c);
    if (!atom || c->err[0]) return atom;
    if (c->pos < c->len) {
        char q = c->src[c->pos];
        int min_rep = -1;
        int max_rep = -1;
        if (q == '*') {
            c->pos++;
            min_rep = 0;
            max_rep = -1;
        } else if (q == '+') {
            c->pos++;
            min_rep = 1;
            max_rep = -1;
        } else if (q == '?') {
            c->pos++;
            min_rep = 0;
            max_rep = 1;
        } else if (q == '{') {
            if (!parse_braced_quant(c, &min_rep, &max_rep)) {
                return atom;
            }
        }
        if (min_rep >= 0) {
            RxNode *quant = rx_alloc(RX_QUANT);
            if (!quant) {
                rx_free(atom);
                return NULL;
            }
            quant->left = atom;
            quant->min_rep = min_rep;
            quant->max_rep = max_rep;
            quant->greedy = 1;
            if (c->pos < c->len && c->src[c->pos] == '?') {
                c->pos++;
                quant->greedy = 0;
            }
            return quant;
        }
    }
    return atom;
}

static RxNode *parse_sequence(RxCompiler *c) {
    RxNode *head = NULL;
    while (c->pos < c->len && c->src[c->pos] != '|' && c->src[c->pos] != ')') {
        RxNode *part = parse_quantified(c);
        if (!part || c->err[0]) {
            rx_free(head);
            rx_free(part);
            return NULL;
        }
        if (!head) {
            head = part;
        } else {
            RxNode *seq = rx_alloc(RX_SEQ);
            if (!seq) {
                rx_free(head);
                rx_free(part);
                return NULL;
            }
            seq->left = head;
            seq->right = part;
            head = seq;
        }
    }
    return head ? head : rx_alloc(RX_EMPTY);
}

static RxNode *parse_alternation(RxCompiler *c) {
    RxNode *left = parse_sequence(c);
    if (!left || c->err[0]) return left;
    while (c->pos < c->len && c->src[c->pos] == '|') {
        RxNode *right;
        RxNode *alt;
        c->pos++;
        right = parse_sequence(c);
        if (!right || c->err[0]) {
            rx_free(left);
            rx_free(right);
            return NULL;
        }
        alt = rx_alloc(RX_ALT);
        if (!alt) {
            rx_free(left);
            rx_free(right);
            return NULL;
        }
        alt->left = left;
        alt->right = right;
        left = alt;
    }
    return left;
}

static char *pattern_to_regex_source(const char *query, const char *mode, char *err, size_t err_sz) {
    SBuf b;
    const char *p;
    if (!query || !query[0]) {
        set_err(err, err_sz, "Search query is required");
        return NULL;
    }
    if (mode && strcmp(mode, "regex") == 0) {
        return strdup(query);
    }
    sbuf_init(&b);
    if (mode && strcmp(mode, "wildcard") == 0) {
        for (p = query; *p; p++) {
            if (*p == '*') {
                /* Each * becomes a capturing group so $1, $2 work in Wildcard mode! */
                if (sbuf_puts(&b, "(.*?)") != 0) {
                    sbuf_free(&b);
                    return NULL;
                }
            } else if (*p == '?') {
                if (sbuf_puts(&b, "(.)") != 0) {
                    sbuf_free(&b);
                    return NULL;
                }
            } else {
                if (strchr("\\.^$|()[]{}+*?", *p)) {
                    if (sbuf_putc(&b, '\\') != 0) {
                        sbuf_free(&b);
                        return NULL;
                    }
                }
                if (sbuf_putc(&b, *p) != 0) {
                    sbuf_free(&b);
                    return NULL;
                }
            }
        }
        /* If the wildcard pattern ends with (*?), make the trailing wildcard greedy so
         * patterns like "foo*" match to the end of the line as expected. */
        if (b.len >= 5 && memcmp(b.data + b.len - 5, "(.*?)", 5) == 0) {
            memcpy(b.data + b.len - 5, "(.*)", 4);
            b.len -= 1;
            b.data[b.len] = '\0';
        }
        return sbuf_steal(&b);
    }
    /* Literal mode */
    for (p = query; *p; p++) {
        if (strchr("\\.^$|()[]{}+*?", *p)) {
            if (sbuf_putc(&b, '\\') != 0) {
                sbuf_free(&b);
                return NULL;
            }
        }
        if (sbuf_putc(&b, *p) != 0) {
            sbuf_free(&b);
            return NULL;
        }
    }
    return sbuf_steal(&b);
}

static RxNode *rx_compile(const char *pattern, int case_sensitive, char *err, size_t err_sz) {
    RxCompiler c;
    RxNode *root;
    memset(&c, 0, sizeof(c));
    c.src = pattern ? pattern : "";
    c.len = strlen(c.src);
    c.case_sensitive = case_sensitive;
    c.next_group = 1;
    root = parse_alternation(&c);
    if (!root || c.err[0]) {
        set_err(err, err_sz, "%s", c.err[0] ? c.err : "Invalid regular expression");
        rx_free(root);
        return NULL;
    }
    if (c.pos < c.len) {
        set_err(err, err_sz, "Unexpected '%c' at position %zu", c.src[c.pos], c.pos + 1);
        rx_free(root);
        return NULL;
    }
    return root;
}

typedef struct RxExec {
    const char *bol;
    const char *eol;
    int case_sensitive;
    int steps;
} RxExec;

typedef struct RxCont {
    const RxNode *node;
    const struct RxCont *next;
    int close_group; /* >= 0 when closing capture group; < 0 for quantifier step */
    const char *group_start;
} RxCont;

static int rx_eval(RxExec *ex, const RxNode *node, const RxCont *cont, const char *sp, RxCaptures *caps);

static int rx_match_quant(RxExec *ex, const RxNode *qnode, const RxCont *cont, const char *sp,
                          RxCaptures *caps, int count);

static int rx_run_cont(RxExec *ex, const RxCont *cont, const char *sp, RxCaptures *caps) {
    if (!cont) return 1;
    if (cont->close_group < 0) {
        int next_count = -cont->close_group;
        if (sp == cont->group_start && next_count > cont->node->min_rep) {
            return rx_run_cont(ex, cont->next, sp, caps);
        }
        return rx_match_quant(ex, cont->node, cont->next, sp, caps, next_count);
    }
    if (!cont->node && cont->close_group >= 0 && cont->close_group < MAX_CAPTURE_GROUPS) {
        const char *old_s = caps->start[cont->close_group];
        const char *old_e = caps->end[cont->close_group];
        caps->start[cont->close_group] = cont->group_start;
        caps->end[cont->close_group] = sp;
        if (rx_run_cont(ex, cont->next, sp, caps)) return 1;
        caps->start[cont->close_group] = old_s;
        caps->end[cont->close_group] = old_e;
        return 0;
    }
    return rx_eval(ex, cont->node, cont->next, sp, caps);
}

static int rx_match_quant(RxExec *ex, const RxNode *qnode, const RxCont *cont, const char *sp,
                          RxCaptures *caps, int count) {
    if (++ex->steps > REGEX_STEP_LIMIT) return 0;
    if (qnode->max_rep >= 0 && count == qnode->max_rep) {
        return rx_run_cont(ex, cont, sp, caps);
    }
    if (count < qnode->min_rep) {
        /* Must match at least once more */
        RxCont step_cont;
        step_cont.node = qnode;
        step_cont.next = cont;
        step_cont.close_group = -(count + 1);
        step_cont.group_start = sp;
        return rx_eval(ex, qnode->left, &step_cont, sp, caps);
    }
    if (qnode->greedy) {
        RxCaptures saved = *caps;
        RxCont step_cont;
        step_cont.node = qnode;
        step_cont.next = cont;
        step_cont.close_group = -(count + 1);
        step_cont.group_start = sp;
        if (rx_eval(ex, qnode->left, &step_cont, sp, caps)) return 1;
        *caps = saved;
        return rx_run_cont(ex, cont, sp, caps);
    } else {
        RxCaptures saved = *caps;
        RxCont step_cont;
        if (rx_run_cont(ex, cont, sp, caps)) return 1;
        *caps = saved;
        step_cont.node = qnode;
        step_cont.next = cont;
        step_cont.close_group = -(count + 1);
        step_cont.group_start = sp;
        return rx_eval(ex, qnode->left, &step_cont, sp, caps);
    }
}

static int rx_eval(RxExec *ex, const RxNode *node, const RxCont *cont, const char *sp, RxCaptures *caps) {
    if (++ex->steps > REGEX_STEP_LIMIT) return 0;
    if (!node) return rx_run_cont(ex, cont, sp, caps);

    switch (node->kind) {
        case RX_EMPTY:
            return rx_run_cont(ex, cont, sp, caps);
        case RX_SEQ: {
            RxCont seq_cont;
            seq_cont.node = node->right;
            seq_cont.next = cont;
            seq_cont.close_group = 0;
            seq_cont.group_start = NULL;
            return rx_eval(ex, node->left, &seq_cont, sp, caps);
        }
        case RX_ALT: {
            RxCaptures saved = *caps;
            if (rx_eval(ex, node->left, cont, sp, caps)) return 1;
            *caps = saved;
            return rx_eval(ex, node->right, cont, sp, caps);
        }
        case RX_GROUP: {
            RxCont grp_cont;
            grp_cont.node = NULL;
            grp_cont.next = cont;
            grp_cont.close_group = node->group_idx;
            grp_cont.group_start = sp;
            return rx_eval(ex, node->left, &grp_cont, sp, caps);
        }
        case RX_QUANT:
            return rx_match_quant(ex, node, cont, sp, caps, 0);
        case RX_CHAR: {
            unsigned char a;
            unsigned char b;
            if (sp >= ex->eol) return 0;
            a = (unsigned char)*sp;
            b = (unsigned char)node->ch;
            if (!ex->case_sensitive) {
                a = (unsigned char)tolower(a);
                b = (unsigned char)tolower(b);
            }
            if (a != b) return 0;
            return rx_run_cont(ex, cont, sp + 1, caps);
        }
        case RX_DOT:
            if (sp >= ex->eol || *sp == '\r' || *sp == '\n') return 0;
            return rx_run_cont(ex, cont, sp + 1, caps);
        case RX_CLASS: {
            int hit;
            if (sp >= ex->eol) return 0;
            hit = bitmap_test(node->bitmap, (unsigned char)*sp);
            if (node->negated) hit = !hit;
            if (!hit) return 0;
            return rx_run_cont(ex, cont, sp + 1, caps);
        }
        case RX_ANCHOR_BOL:
            if (sp != ex->bol) return 0;
            return rx_run_cont(ex, cont, sp, caps);
        case RX_ANCHOR_EOL:
            if (sp != ex->eol) return 0;
            return rx_run_cont(ex, cont, sp, caps);
        case RX_BOUNDARY:
        case RX_NOT_BOUNDARY: {
            int left_w = (sp > ex->bol) ? is_word_byte((unsigned char)sp[-1]) : 0;
            int right_w = (sp < ex->eol) ? is_word_byte((unsigned char)sp[0]) : 0;
            int is_b = (left_w != right_w);
            if (node->kind == RX_BOUNDARY ? !is_b : is_b) return 0;
            return rx_run_cont(ex, cont, sp, caps);
        }
    }
    return 0;
}

static int rx_find_on_line(const RxNode *rx, const char *bol, const char *eol, const char *search_from,
                           int case_sensitive, int whole_word, RxCaptures *out_caps) {
    const char *p;
    for (p = search_from; p <= eol; p++) {
        RxExec ex;
        RxCaptures caps;
        RxCont finish;
        ex.bol = bol;
        ex.eol = eol;
        ex.case_sensitive = case_sensitive;
        ex.steps = 0;
        memset(&caps, 0, sizeof(caps));
        finish.node = NULL;
        finish.next = NULL;
        finish.close_group = 0;
        finish.group_start = p;
        if (rx_eval(&ex, rx, &finish, p, &caps) && caps.start[0] && caps.end[0]) {
            /* Do not allow empty matches unless the pattern explicitly matched at least 1 char,
             * except when searching anchored patterns — still require non-empty for find/replace safety. */
            if (caps.end[0] <= caps.start[0]) {
                continue;
            }
            if (whole_word) {
                int start_w = is_word_byte((unsigned char)caps.start[0][0]);
                int prev_w = (caps.start[0] > bol) ? is_word_byte((unsigned char)caps.start[0][-1]) : 0;
                int end_w = is_word_byte((unsigned char)caps.end[0][-1]);
                int next_w = (caps.end[0] < eol) ? is_word_byte((unsigned char)caps.end[0][0]) : 0;
                if (start_w && prev_w) continue;
                if (end_w && next_w) continue;
            }
            *out_caps = caps;
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 * Scope Masking (all, html-attr, comments, php)
 * ========================================================================= */

#define SCOPE_BIT_ATTR    1u
#define SCOPE_BIT_COMMENT 2u
#define SCOPE_BIT_PHP     4u

static unsigned char *build_scope_mask(const char *text, size_t len) {
    unsigned char *mask = (unsigned char *)calloc(len + 1, 1);
    size_t i = 0;
    if (!mask) return NULL;
    while (i < len) {
        /* PHP block <? ... ?> */
        if (i + 2 <= len && text[i] == '<' && text[i + 1] == '?') {
            size_t start = i;
            i += 2;
            while (i < len) {
                if (i + 2 <= len && text[i] == '?' && text[i + 1] == '>') {
                    i += 2;
                    break;
                }
                i++;
            }
            memset(mask + start, SCOPE_BIT_PHP, i - start);
            continue;
        }
        /* HTML Comment <!-- ... --> */
        if (i + 4 <= len && memcmp(text + i, "<!--", 4) == 0) {
            size_t start = i;
            i += 4;
            while (i < len) {
                if (i + 3 <= len && memcmp(text + i, "-->", 3) == 0) {
                    i += 3;
                    break;
                }
                i++;
            }
            memset(mask + start, SCOPE_BIT_COMMENT, i - start);
            continue;
        }
        /* HTML opening tag <tag attr="val"> */
        if (text[i] == '<' && i + 1 < len && (isalpha((unsigned char)text[i + 1]) || text[i + 1] == '/')) {
            i++;
            while (i < len && (isalnum((unsigned char)text[i]) || text[i] == '-' || text[i] == ':' || text[i] == '/')) {
                i++;
            }
            {
                size_t attr_start = i;
                char quote = 0;
                while (i < len) {
                    if (quote) {
                        if (text[i] == quote) quote = 0;
                        i++;
                        continue;
                    }
                    if (text[i] == '"' || text[i] == '\'') {
                        quote = text[i++];
                        continue;
                    }
                    if (i + 2 <= len && text[i] == '<' && text[i + 1] == '?') {
                        /* Embedded PHP inside tag attributes */
                        size_t php_s = i;
                        i += 2;
                        while (i < len) {
                            if (i + 2 <= len && text[i] == '?' && text[i + 1] == '>') {
                                i += 2;
                                break;
                            }
                            i++;
                        }
                        memset(mask + php_s, SCOPE_BIT_PHP | SCOPE_BIT_ATTR, i - php_s);
                        attr_start = i;
                        continue;
                    }
                    if (text[i] == '>') {
                        if (i > attr_start) {
                            size_t k;
                            for (k = attr_start; k < i; k++) mask[k] |= SCOPE_BIT_ATTR;
                        }
                        i++;
                        break;
                    }
                    i++;
                }
            }
            continue;
        }
        i++;
    }
    return mask;
}

static unsigned char parse_scope_bit(const char *scope) {
    if (!scope || !scope[0] || strcmp(scope, "all") == 0) return 0;
    if (strcmp(scope, "html-attr") == 0 || strcmp(scope, "attributes") == 0) return SCOPE_BIT_ATTR;
    if (strcmp(scope, "comments") == 0 || strcmp(scope, "comment") == 0) return SCOPE_BIT_COMMENT;
    if (strcmp(scope, "php") == 0) return SCOPE_BIT_PHP;
    return 0;
}

static int match_in_scope(const unsigned char *mask, unsigned char req_bit, size_t start, size_t end) {
    size_t i;
    if (!req_bit || !mask) return 1;
    if (end <= start) return (mask[start] & req_bit) != 0;
    for (i = start; i < end; i++) {
        if ((mask[i] & req_bit) != 0) return 1;
    }
    return 0;
}

static char *expand_replacement(const char *repl, const char *mode, const RxCaptures *caps) {
    SBuf b;
    const char *p;
    int literal_mode = (!mode || strcmp(mode, "literal") == 0);
    sbuf_init(&b);
    if (!repl) return strdup("");
    if (literal_mode) {
        return strdup(repl);
    }
    for (p = repl; *p; p++) {
        if (*p == '$') {
            if (p[1] == '$') {
                sbuf_putc(&b, '$');
                p++;
                continue;
            }
            if (p[1] == '&' || p[1] == '0') {
                if (caps->start[0] && caps->end[0] && caps->end[0] >= caps->start[0]) {
                    sbuf_append(&b, caps->start[0], (size_t)(caps->end[0] - caps->start[0]));
                }
                p++;
                continue;
            }
            if (p[1] >= '1' && p[1] <= '9') {
                int idx = p[1] - '0';
                if (caps->start[idx] && caps->end[idx] && caps->end[idx] >= caps->start[idx]) {
                    sbuf_append(&b, caps->start[idx], (size_t)(caps->end[idx] - caps->start[idx]));
                }
                p++;
                continue;
            }
        }
        if (*p == '\\' && p[1]) {
            if (p[1] == 'n') {
                sbuf_putc(&b, '\n');
                p++;
                continue;
            }
            if (p[1] == 't') {
                sbuf_putc(&b, '\t');
                p++;
                continue;
            }
            if (p[1] == 'r') {
                sbuf_putc(&b, '\r');
                p++;
                continue;
            }
            if (p[1] == '\\') {
                sbuf_putc(&b, '\\');
                p++;
                continue;
            }
        }
        sbuf_putc(&b, *p);
    }
    return sbuf_steal(&b);
}

static char *param_str(const JsonValue *obj, const char *key, const char *fallback) {
    JsonValue *v;
    char *t;
    if (!obj || json_type(obj) != JSON_OBJECT) return strdup(fallback ? fallback : "");
    v = json_object_get(obj, key);
    if (!v || json_type(v) == JSON_NULL) return strdup(fallback ? fallback : "");
    t = json_as_text(v);
    return t ? t : strdup(fallback ? fallback : "");
}

static int param_bool(const JsonValue *obj, const char *key, int fallback) {
    JsonValue *v;
    long long n = 0;
    const char *t;
    if (!obj || json_type(obj) != JSON_OBJECT) return fallback;
    v = json_object_get(obj, key);
    if (!v) return fallback;
    if (json_type(v) == JSON_BOOL) return json_bool_value(v) ? 1 : 0;
    if (json_type(v) == JSON_NUMBER && json_as_int(v, &n) == 0) return n != 0;
    if (json_type(v) == JSON_STRING) {
        t = json_string_get(v, NULL);
        if (!t || !t[0]) return fallback;
        return strcmp(t, "1") == 0 || strcasecmp(t, "true") == 0 || strcasecmp(t, "yes") == 0;
    }
    return fallback;
}

static long long param_int(const JsonValue *obj, const char *key, long long fallback) {
    JsonValue *v;
    long long n = 0;
    const char *t;
    if (!obj || json_type(obj) != JSON_OBJECT) return fallback;
    v = json_object_get(obj, key);
    if (!v) return fallback;
    if (json_type(v) == JSON_NUMBER && json_as_int(v, &n) == 0) return n;
    if (json_type(v) == JSON_STRING) {
        t = json_string_get(v, NULL);
        if (t && t[0]) return strtoll(t, NULL, 10);
    }
    return fallback;
}

static int jput_s(JsonValue *obj, const char *k, const char *v) {
    JsonValue *s = json_string_cstr(v ? v : "");
    if (!s) return -1;
    return json_object_put(obj, k, s);
}

static int jput_n(JsonValue *obj, const char *k, const char *v, size_t n) {
    JsonValue *s = json_string(v ? v : "", n);
    if (!s) return -1;
    return json_object_put(obj, k, s);
}

static int jput_i(JsonValue *obj, const char *k, long long v) {
    JsonValue *num = json_number_int(v);
    if (!num) return -1;
    return json_object_put(obj, k, num);
}

static int jput_b(JsonValue *obj, const char *k, int v) {
    JsonValue *b = json_bool(v ? 1 : 0);
    if (!b) return -1;
    return json_object_put(obj, k, b);
}

static void split_rel_metadata(const char *rel, char *section, size_t sec_sz,
                               char *slug, size_t slug_sz, char *name, size_t name_sz) {
    const char *first_slash;
    const char *last_slash;
    section[0] = '\0';
    slug[0] = '\0';
    name[0] = '\0';
    if (!rel) return;
    while (*rel == '/') rel++;
    first_slash = strchr(rel, '/');
    last_slash = strrchr(rel, '/');
    if (!first_slash) {
        snprintf(name, name_sz, "%s", rel);
        return;
    }
    {
        size_t slen = (size_t)(first_slash - rel);
        if (slen >= sec_sz) slen = sec_sz - 1;
        memcpy(section, rel, slen);
        section[slen] = '\0';
    }
    snprintf(name, name_sz, "%s", last_slash + 1);
    if (last_slash > first_slash) {
        size_t klen = (size_t)(last_slash - (first_slash + 1));
        if (klen >= slug_sz) klen = slug_sz - 1;
        memcpy(slug, first_slash + 1, klen);
        slug[klen] = '\0';
    }
}

int project_search_query(const char *repo_root, const JsonValue *params, JsonDoc **out,
                         char *err, size_t err_sz) {
    char *query = NULL;
    char *replacement = NULL;
    char *mode = NULL;
    char *scope = NULL;
    char *include_globs = NULL;
    char *exclude_globs = NULL;
    char *rx_src = NULL;
    int case_sensitive;
    int whole_word;
    int has_replacement = 0;
    long long max_results;
    unsigned char scope_bit;
    RxNode *rx = NULL;
    FileList files;
    JsonValue *root = NULL;
    JsonValue *files_arr = NULL;
    long long total_matches = 0;
    long long total_files = 0;
    int truncated = 0;
    size_t i;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }

    query = param_str(params, "query", "");
    has_replacement = params && json_type(params) == JSON_OBJECT && json_object_has(params, "replacement");
    replacement = param_str(params, "replacement", "");
    mode = param_str(params, "mode", "literal");
    scope = param_str(params, "scope", "all");
    include_globs = param_str(params, "include", "");
    exclude_globs = param_str(params, "exclude", "");
    case_sensitive = param_bool(params, "caseSensitive", 0);
    whole_word = param_bool(params, "wholeWord", 0);
    max_results = param_int(params, "maxResults", DEFAULT_MAX_RESULTS);
    if (max_results < 1) max_results = 1;
    if (max_results > 5000) max_results = 5000;

    if (!query || !query[0]) {
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        set_err(err, err_sz, "Search query is required");
        return 1;
    }

    rx_src = pattern_to_regex_source(query, mode, err, err_sz);
    if (!rx_src) {
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        return 1;
    }

    rx = rx_compile(rx_src, case_sensitive, err, err_sz);
    free(rx_src);
    if (!rx) {
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        return 1;
    }

    scope_bit = parse_scope_bit(scope);
    fl_init(&files);
    walk_searchable_files(repo_root, "", 0, include_globs, exclude_globs, &files);
    if (files.count > 1) {
        qsort(files.items, files.count, sizeof(files.items[0]), cmp_str_ptr);
    }

    root = json_object();
    files_arr = json_array();
    if (!root || !files_arr) {
        json_free(root);
        json_free(files_arr);
        fl_free(&files);
        rx_free(rx);
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    for (i = 0; i < files.count; i++) {
        char fs_path[PATH_MAX];
        char pub_path[PATH_MAX];
        char section[128];
        char slug[512];
        char fname[256];
        size_t text_len = 0;
        char *text;
        unsigned char *mask = NULL;
        const char *bol;
        int line_num = 1;
        int index_in_file = 0;
        JsonValue *file_obj = NULL;
        JsonValue *matches_arr = NULL;

        if (snprintf(fs_path, sizeof(fs_path), "%s/%s", repo_root, files.items[i]) >= (int)sizeof(fs_path)) {
            continue;
        }
        text = read_text_file(fs_path, &text_len);
        if (!text) continue;
        if (scope_bit) {
            mask = build_scope_mask(text, text_len);
            if (!mask) {
                free(text);
                continue;
            }
        }

        bol = text;
        while (bol <= text + text_len) {
            const char *nl = memchr(bol, '\n', (size_t)((text + text_len) - bol));
            const char *eol = nl ? nl : (text + text_len);
            const char *line_end = (eol > bol && eol[-1] == '\r') ? (eol - 1) : eol;
            const char *cursor = bol;
            while (cursor <= line_end) {
                RxCaptures caps;
                size_t abs_s;
                size_t abs_e;
                if (!rx_find_on_line(rx, bol, line_end, cursor, case_sensitive, whole_word, &caps)) {
                    break;
                }
                abs_s = (size_t)(caps.start[0] - text);
                abs_e = (size_t)(caps.end[0] - text);
                if (match_in_scope(mask, scope_bit, abs_s, abs_e)) {
                    if (total_matches >= max_results) {
                        truncated = 1;
                        break;
                    }
                    if (!file_obj) {
                        file_obj = json_object();
                        matches_arr = json_array();
                        if (!file_obj || !matches_arr) {
                            json_free(file_obj);
                            json_free(matches_arr);
                            file_obj = NULL;
                            matches_arr = NULL;
                            break;
                        }
                        snprintf(pub_path, sizeof(pub_path), "/%s", files.items[i]);
                        split_rel_metadata(files.items[i], section, sizeof(section),
                                           slug, sizeof(slug), fname, sizeof(fname));
                        jput_s(file_obj, "path", pub_path);
                        jput_s(file_obj, "section", section);
                        jput_s(file_obj, "slug", slug);
                        jput_s(file_obj, "name", fname);
                    }
                    {
                        JsonValue *m = json_object();
                        size_t line_len = (size_t)(line_end - bol);
                        size_t col_s = (size_t)(caps.start[0] - bol);
                        size_t col_e = (size_t)(caps.end[0] - bol);
                        size_t snip_s = 0;
                        size_t snip_e = line_len;
                        if (line_len > 240) {
                            snip_s = col_s > 80 ? col_s - 80 : 0;
                            snip_e = col_e + 120 < line_len ? col_e + 120 : line_len;
                        }
                        if (m) {
                            jput_i(m, "indexInFile", index_in_file);
                            jput_i(m, "line", line_num);
                            jput_i(m, "column", (long long)col_s + 1);
                            jput_i(m, "startOffset", (long long)abs_s);
                            jput_i(m, "endOffset", (long long)abs_e);
                            jput_n(m, "matchText", caps.start[0], (size_t)(caps.end[0] - caps.start[0]));
                            jput_n(m, "lineText", bol + snip_s, snip_e - snip_s);
                            jput_i(m, "lineStartCol", (long long)(col_s - snip_s));
                            jput_i(m, "lineEndCol", (long long)(col_e - snip_s));
                            if (has_replacement) {
                                char *expanded = expand_replacement(replacement, mode, &caps);
                                if (expanded) {
                                    jput_s(m, "replacePreview", expanded);
                                    free(expanded);
                                }
                            }
                            json_array_add(matches_arr, m);
                        }
                    }
                    total_matches++;
                }
                index_in_file++;
                cursor = caps.end[0] > caps.start[0] ? caps.end[0] : (caps.start[0] + 1);
            }
            if (truncated || !nl) break;
            bol = nl + 1;
            line_num++;
        }

        if (file_obj && matches_arr) {
            jput_i(file_obj, "matchCount", (long long)json_array_len(matches_arr));
            json_object_put(file_obj, "matches", matches_arr);
            json_array_add(files_arr, file_obj);
            total_files++;
        }
        free(mask);
        free(text);
        if (truncated) break;
    }

    jput_b(root, "ok", 1);
    jput_s(root, "query", query);
    jput_s(root, "mode", mode);
    jput_s(root, "scope", scope);
    jput_i(root, "totalMatches", total_matches);
    jput_i(root, "totalFiles", total_files);
    jput_b(root, "truncated", truncated);
    json_object_put(root, "files", files_arr);

    fl_free(&files);
    rx_free(rx);
    free(query);
    free(replacement);
    free(mode);
    free(scope);
    free(include_globs);
    free(exclude_globs);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

static int safe_rel_path(const char *path, char *out, size_t out_sz) {
    const char *p = path;
    size_t n;
    if (!p || !out || out_sz == 0) return -1;
    while (*p == '/') p++;
    if (!*p || strstr(p, "..") != NULL || strchr(p, '\\') != NULL) return -1;
    n = strlen(p);
    if (n >= out_sz) return -1;
    memcpy(out, p, n + 1);
    return 0;
}

int project_search_replace(const char *repo_root, const JsonValue *params, JsonDoc **out,
                           char *err, size_t err_sz) {
    char *query = NULL;
    char *replacement = NULL;
    char *mode = NULL;
    char *scope = NULL;
    char *include_globs = NULL;
    char *exclude_globs = NULL;
    char *target_path = NULL;
    char *rx_src = NULL;
    int case_sensitive;
    int whole_word;
    long long target_match_index;
    long long target_start_offset;
    unsigned char scope_bit;
    RxNode *rx = NULL;
    FileList files;
    JsonValue *root = NULL;
    JsonValue *modified_arr = NULL;
    long long replaced_matches = 0;
    long long replaced_files = 0;
    size_t i;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }
    if (!params || json_type(params) != JSON_OBJECT || !json_object_has(params, "replacement")) {
        set_err(err, err_sz, "replacement is required");
        return 1;
    }

    query = param_str(params, "query", "");
    replacement = param_str(params, "replacement", "");
    mode = param_str(params, "mode", "literal");
    scope = param_str(params, "scope", "all");
    include_globs = param_str(params, "include", "");
    exclude_globs = param_str(params, "exclude", "");
    target_path = param_str(params, "path", "");
    case_sensitive = param_bool(params, "caseSensitive", 0);
    whole_word = param_bool(params, "wholeWord", 0);
    target_match_index = param_int(params, "matchIndex", -1);
    target_start_offset = param_int(params, "startOffset", -1);

    if (!query || !query[0]) {
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        free(target_path);
        set_err(err, err_sz, "Search query is required");
        return 1;
    }

    rx_src = pattern_to_regex_source(query, mode, err, err_sz);
    if (!rx_src) {
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        free(target_path);
        return 1;
    }

    rx = rx_compile(rx_src, case_sensitive, err, err_sz);
    free(rx_src);
    if (!rx) {
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        free(target_path);
        return 1;
    }

    scope_bit = parse_scope_bit(scope);
    fl_init(&files);
    if (target_path && target_path[0]) {
        char rel[PATH_MAX];
        if (safe_rel_path(target_path, rel, sizeof(rel)) != 0 || is_skipped_file(strrchr(rel, '/') ? strrchr(rel, '/') + 1 : rel)) {
            fl_free(&files);
            rx_free(rx);
            free(query);
            free(replacement);
            free(mode);
            free(scope);
            free(include_globs);
            free(exclude_globs);
            free(target_path);
            set_err(err, err_sz, "Target file path is not allowed");
            return 1;
        }
        fl_add(&files, rel);
    } else {
        walk_searchable_files(repo_root, "", 0, include_globs, exclude_globs, &files);
        if (files.count > 1) {
            qsort(files.items, files.count, sizeof(files.items[0]), cmp_str_ptr);
        }
    }

    root = json_object();
    modified_arr = json_array();
    if (!root || !modified_arr) {
        json_free(root);
        json_free(modified_arr);
        fl_free(&files);
        rx_free(rx);
        free(query);
        free(replacement);
        free(mode);
        free(scope);
        free(include_globs);
        free(exclude_globs);
        free(target_path);
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    for (i = 0; i < files.count; i++) {
        char fs_path[PATH_MAX];
        char pub_path[PATH_MAX];
        size_t text_len = 0;
        char *text;
        unsigned char *mask = NULL;
        SBuf updated;
        const char *bol;
        size_t last_copied = 0;
        int index_in_file = 0;
        int file_replaced = 0;

        if (snprintf(fs_path, sizeof(fs_path), "%s/%s", repo_root, files.items[i]) >= (int)sizeof(fs_path)) {
            continue;
        }
        text = read_text_file(fs_path, &text_len);
        if (!text) continue;
        if (scope_bit) {
            mask = build_scope_mask(text, text_len);
            if (!mask) {
                free(text);
                continue;
            }
        }
        sbuf_init(&updated);

        bol = text;
        while (bol <= text + text_len) {
            const char *nl = memchr(bol, '\n', (size_t)((text + text_len) - bol));
            const char *eol = nl ? nl : (text + text_len);
            const char *line_end = (eol > bol && eol[-1] == '\r') ? (eol - 1) : eol;
            const char *cursor = bol;
            while (cursor <= line_end) {
                RxCaptures caps;
                size_t abs_s;
                size_t abs_e;
                if (!rx_find_on_line(rx, bol, line_end, cursor, case_sensitive, whole_word, &caps)) {
                    break;
                }
                abs_s = (size_t)(caps.start[0] - text);
                abs_e = (size_t)(caps.end[0] - text);
                if (match_in_scope(mask, scope_bit, abs_s, abs_e)) {
                    int should_replace = 1;
                    if (target_start_offset >= 0) {
                        should_replace = ((long long)abs_s == target_start_offset);
                    } else if (target_match_index >= 0) {
                        should_replace = (index_in_file == target_match_index);
                    }
                    if (should_replace) {
                        char *expanded = expand_replacement(replacement, mode, &caps);
                        if (expanded) {
                            sbuf_append(&updated, text + last_copied, abs_s - last_copied);
                            sbuf_puts(&updated, expanded);
                            free(expanded);
                            last_copied = abs_e;
                            file_replaced++;
                            replaced_matches++;
                        }
                    }
                }
                index_in_file++;
                cursor = caps.end[0] > caps.start[0] ? caps.end[0] : (caps.start[0] + 1);
            }
            if (!nl) break;
            bol = nl + 1;
        }

        if (file_replaced > 0) {
            sbuf_append(&updated, text + last_copied, text_len - last_copied);
            if (atomic_write_text(fs_path, updated.data ? updated.data : "", updated.len) == 0) {
                snprintf(pub_path, sizeof(pub_path), "/%s", files.items[i]);
                json_array_add(modified_arr, json_string_cstr(pub_path));
                replaced_files++;
            }
        }
        sbuf_free(&updated);
        free(mask);
        free(text);
    }

    jput_b(root, "ok", 1);
    jput_i(root, "replacedMatches", replaced_matches);
    jput_i(root, "replacedFiles", replaced_files);
    json_object_put(root, "modifiedPaths", modified_arr);

    fl_free(&files);
    rx_free(rx);
    free(query);
    free(replacement);
    free(mode);
    free(scope);
    free(include_globs);
    free(exclude_globs);
    free(target_path);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

