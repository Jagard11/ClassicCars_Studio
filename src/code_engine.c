#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "code_engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>
#endif

#include "compat.h"

typedef struct CeBuf {
    char *data;
    size_t len;
    size_t cap;
} CeBuf;

static void cebuf_init(CeBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static const char *ce_strcasestr(const char *haystack, const char *needle) {
    size_t nlen;
    if (!haystack || !needle) return NULL;
    if (!needle[0]) return haystack;
    nlen = strlen(needle);
    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, nlen) == 0) return haystack;
    }
    return NULL;
}

static int cebuf_reserve(CeBuf *b, size_t extra) {
    size_t need = b->len + extra + 1;
    size_t cap;
    char *next;
    if (need <= b->cap) return 0;
    cap = b->cap ? b->cap : 256;
    while (cap < need) cap *= 2;
    next = (char *)realloc(b->data, cap);
    if (!next) return -1;
    b->data = next;
    b->cap = cap;
    return 0;
}

static void cebuf_append(CeBuf *b, const char *s, size_t n) {
    if (!n || cebuf_reserve(b, n) != 0) return;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

static void cebuf_puts(CeBuf *b, const char *s) {
    if (s && s[0]) cebuf_append(b, s, strlen(s));
}

static void cebuf_putc(CeBuf *b, char c) {
    cebuf_append(b, &c, 1);
}

static char *cebuf_steal(CeBuf *b) {
    char *out = b->data ? b->data : strdup("");
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return out;
}

static void cebuf_append_esc(CeBuf *b, const char *s, size_t n) {
    size_t i;
    for (i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == '&') cebuf_append(b, "&amp;", 5);
        else if (ch == '<') cebuf_append(b, "&lt;", 4);
        else if (ch == '>') cebuf_append(b, "&gt;", 4);
        else cebuf_putc(b, ch);
    }
}

static int ce_is_space(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\f';
}

static int ce_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

static int ce_is_hex(char ch) {
    return ce_is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static int ce_is_ident_start(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static int ce_is_ident_char(char ch) {
    return ce_is_ident_start(ch) || ce_is_digit(ch);
}

static int ce_is_name_char(char ch) {
    return ce_is_ident_char(ch) || ch == '-' || ch == ':';
}

static int ce_is_attr_start(char ch) {
    return ce_is_ident_start(ch) || ch == ':' || ch == '_';
}

static int ce_is_attr_char(char ch) {
    return ce_is_name_char(ch);
}

static int ce_is_css_ident_start(char ch) {
    return ce_is_ident_start(ch);
}

static int ce_is_css_ident_char(char ch) {
    return ce_is_ident_char(ch) || ch == '-';
}

static char ce_fold(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
}

static int ce_ends_with_ci(const char *s, const char *ext) {
    size_t ns, ne;
    if (!s || !ext) return 0;
    ns = strlen(s);
    ne = strlen(ext);
    return ns >= ne && strcasecmp(s + ns - ne, ext) == 0;
}

const char *code_engine_language_from_path(const char *path) {
    if (!path || !path[0]) return "text";
    if (ce_ends_with_ci(path, ".php")) return "php";
    if (ce_ends_with_ci(path, ".html") || ce_ends_with_ci(path, ".htm") || ce_ends_with_ci(path, ".shtml")) {
        return "html";
    }
    if (ce_ends_with_ci(path, ".css")) return "css";
    if (ce_ends_with_ci(path, ".js") || ce_ends_with_ci(path, ".mjs")) return "js";
    if (ce_ends_with_ci(path, ".json")) return "json";
    return "text";
}

typedef struct CeCtx {
    const char *text;
    size_t len;
    size_t i;
    CeBuf out;
} CeCtx;

static void ce_emit(CeCtx *ctx, const char *cls, size_t start, size_t end) {
    if (end <= start || start >= ctx->len) return;
    if (end > ctx->len) end = ctx->len;
    if (!cls || !cls[0]) {
        cebuf_append_esc(&ctx->out, ctx->text + start, end - start);
    } else {
        cebuf_puts(&ctx->out, "<span class=\"");
        cebuf_puts(&ctx->out, cls);
        cebuf_puts(&ctx->out, "\">");
        cebuf_append_esc(&ctx->out, ctx->text + start, end - start);
        cebuf_puts(&ctx->out, "</span>");
    }
}

static void ce_emit_space(CeCtx *ctx, size_t end) {
    size_t s = ctx->i;
    while (ctx->i < end && ce_is_space(ctx->text[ctx->i])) ctx->i++;
    ce_emit(ctx, "", s, ctx->i);
}

static void ce_emit_gap(CeCtx *ctx, size_t end) {
    if (ctx->i < end && ce_is_space(ctx->text[ctx->i])) ce_emit_space(ctx, end);
}

static ssize_t ce_index_before(const char *text, size_t text_len, const char *needle, size_t from, size_t end) {
    size_t nlen = strlen(needle);
    size_t k;
    if (end > text_len) end = text_len;
    if (from + nlen > end) return -1;
    for (k = from; k + nlen <= end; k++) {
        if (memcmp(text + k, needle, nlen) == 0) return (ssize_t)k;
    }
    return -1;
}

static ssize_t ce_entity_end(const char *text, size_t i, size_t end) {
    size_t j, start_pos;
    if (i + 2 >= end || text[i] != '&') return -1;
    j = i + 1;
    if (text[j] == '#') {
        j++;
        if (j < end && (text[j] == 'x' || text[j] == 'X')) {
            j++;
            start_pos = j;
            while (j < end && ce_is_hex(text[j])) j++;
            if (j == start_pos || j >= end || text[j] != ';') return -1;
            return (ssize_t)(j + 1);
        }
        start_pos = j;
        while (j < end && ce_is_digit(text[j])) j++;
        if (j == start_pos || j >= end || text[j] != ';') return -1;
        return (ssize_t)(j + 1);
    }
    start_pos = j;
    if (j >= end || !ce_is_ident_start(text[j])) return -1;
    j++;
    while (j < end && ce_is_ident_char(text[j])) j++;
    if (j == start_pos || j >= end || text[j] != ';') return -1;
    return (ssize_t)(j + 1);
}

static size_t ce_find_end_tag(const char *text, size_t from, size_t end, const char *name) {
    size_t nlen = strlen(name);
    size_t i;
    for (i = from; i + 2 + nlen <= end; i++) {
        size_t j, k;
        if (text[i] != '<' || text[i + 1] != '/') continue;
        j = i + 2;
        for (k = 0; k < nlen && j < end; k++, j++) {
            if (ce_fold(text[j]) != ce_fold(name[k])) break;
        }
        if (k != nlen) continue;
        if (j < end && ce_is_name_char(text[j])) continue;
        return i;
    }
    return end;
}

static size_t ce_php_open_len(const char *text, size_t i, size_t end) {
    if (i + 2 >= end || text[i] != '<' || text[i + 1] != '?') return 0;
    if (text[i + 2] == '=') return 3;
    if (i + 5 <= end && strncasecmp(text + i, "<?php", 5) == 0) {
        if (i + 5 == end || !ce_is_ident_char(text[i + 5])) return 5;
    }
    return 0;
}

static int ce_is_php_word(const char *w, size_t len) {
    static const char *const words[] = {
        "function", "return", "if", "else", "elseif", "foreach", "for", "while",
        "require_once", "include", "include_once", "require", "echo", "isset",
        "true", "false", "null", "array", "as", "new"
    };
    size_t i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strlen(words[i]) == len && strncasecmp(w, words[i], len) == 0) return 1;
    }
    return 0;
}

static int ce_is_js_word(const char *w, size_t len) {
    static const char *const words[] = {
        "var", "let", "const", "function", "window", "document", "return",
        "if", "else", "for", "while", "new", "this", "true", "false", "null",
        "undefined", "typeof", "in", "of", "switch", "case", "break", "continue",
        "try", "catch", "throw", "class", "async", "await", "do"
    };
    size_t i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strlen(words[i]) == len && memcmp(w, words[i], len) == 0) return 1;
    }
    return 0;
}

static int ce_is_grouping_at(const char *w, size_t len) {
    static const char *const words[] = {
        "media", "supports", "container", "layer", "keyframes", "document", "scope"
    };
    size_t i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (strlen(words[i]) == len && strncasecmp(w, words[i], len) == 0) return 1;
    }
    return 0;
}

static int ce_scan_multi_op(CeCtx *ctx, size_t end) {
    static const char *const ops[] = {
        "===", "!==", ">>=", "<<=", "=>", "->", "::", "&&", "||", "++", "--",
        "+=", "-=", "*=", "/=", ".=", "==", "!=", "<=", ">=", "??", "?.", "<<", ">>"
    };
    size_t k;
    for (k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
        size_t olen = strlen(ops[k]);
        if (ctx->i + olen <= end && memcmp(ctx->text + ctx->i, ops[k], olen) == 0) {
            ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + olen);
            ctx->i += olen;
            return 1;
        }
    }
    return 0;
}

static void ce_scan_line_comment(CeCtx *ctx, size_t end) {
    size_t start = ctx->i;
    while (ctx->i < end && ctx->text[ctx->i] != '\n') ctx->i++;
    if (ctx->i < end && ctx->text[ctx->i] == '\n') ctx->i++;
    ce_emit(ctx, "tok-comment", start, ctx->i);
}

static void ce_scan_block_comment(CeCtx *ctx, size_t end) {
    ssize_t at = ce_index_before(ctx->text, ctx->len, "*/", ctx->i + 2, end);
    size_t stop = (at < 0) ? end : (size_t)at + 2;
    ce_emit(ctx, "tok-comment", ctx->i, stop);
    ctx->i = stop;
}

static void ce_scan_simple_string(CeCtx *ctx, size_t end, char quote) {
    size_t start = ctx->i++;
    while (ctx->i < end) {
        if (ctx->text[ctx->i] == '\\') {
            ctx->i = (ctx->i + 2 <= end) ? ctx->i + 2 : end;
            continue;
        }
        if (ctx->text[ctx->i] == quote) {
            ctx->i++;
            break;
        }
        if (quote != '`' && ctx->text[ctx->i] == '\n') break;
        ctx->i++;
    }
    ce_emit(ctx, "tok-string", start, ctx->i);
}

static int ce_scan_plain_number(CeCtx *ctx, size_t end, int allow_sign) {
    size_t i = ctx->i;
    size_t before;
    if (allow_sign && i < end && (ctx->text[i] == '-' || ctx->text[i] == '+')) {
        if (i + 1 >= end || !ce_is_digit(ctx->text[i + 1])) return 0;
        i++;
    }
    if (i + 1 < end && ctx->text[i] == '0' && (ctx->text[i + 1] == 'x' || ctx->text[i + 1] == 'X')) {
        size_t hex = i + 2;
        i += 2;
        while (i < end && ce_is_hex(ctx->text[i])) i++;
        if (i == hex) return 0;
        ce_emit(ctx, "tok-number", ctx->i, i);
        ctx->i = i;
        return 1;
    }
    before = i;
    if (i >= end || !ce_is_digit(ctx->text[i])) return 0;
    while (i < end && ce_is_digit(ctx->text[i])) i++;
    if (i + 1 < end && ctx->text[i] == '.' && ce_is_digit(ctx->text[i + 1])) {
        i += 2;
        while (i < end && ce_is_digit(ctx->text[i])) i++;
    }
    if (i == before) return 0;
    ce_emit(ctx, "tok-number", ctx->i, i);
    ctx->i = i;
    return 1;
}

static int ce_scan_variable(CeCtx *ctx, size_t end) {
    size_t i = ctx->i + 1;
    if (ctx->i >= end || ctx->text[ctx->i] != '$') return 0;
    if (i < end && ctx->text[i] == '$') i++;
    if (i >= end || !ce_is_ident_start(ctx->text[i])) return 0;
    i++;
    while (i < end && ce_is_ident_char(ctx->text[i])) i++;
    ce_emit(ctx, "tok-variable", ctx->i, i);
    ctx->i = i;
    return 1;
}

static void ce_scan_php_single(CeCtx *ctx, size_t end) {
    size_t start = ctx->i++;
    while (ctx->i < end) {
        if (ctx->text[ctx->i] == '\\') {
            ctx->i = (ctx->i + 2 <= end) ? ctx->i + 2 : end;
            continue;
        }
        if (ctx->text[ctx->i] == '\'') {
            ctx->i++;
            break;
        }
        ctx->i++;
    }
    ce_emit(ctx, "tok-string", start, ctx->i);
}

static void ce_scan_php_double(CeCtx *ctx, size_t end) {
    size_t seg = ctx->i++;
    while (ctx->i < end) {
        char c = ctx->text[ctx->i];
        if (c == '\\') {
            ctx->i = (ctx->i + 2 <= end) ? ctx->i + 2 : end;
            continue;
        }
        if (c == '"') {
            ctx->i++;
            break;
        }
        if (c == '$' && ctx->i + 1 < end &&
            (ce_is_ident_start(ctx->text[ctx->i + 1]) || ctx->text[ctx->i + 1] == '{')) {
            ce_emit(ctx, "tok-string", seg, ctx->i);
            if (ctx->text[ctx->i + 1] == '{') {
                size_t brace = ctx->i;
                ctx->i += 2;
                while (ctx->i < end && ctx->text[ctx->i] != '}') ctx->i++;
                if (ctx->i < end && ctx->text[ctx->i] == '}') ctx->i++;
                ce_emit(ctx, "tok-variable", brace, ctx->i);
            } else {
                ce_scan_variable(ctx, end);
            }
            seg = ctx->i;
            continue;
        }
        ctx->i++;
    }
    ce_emit(ctx, "tok-string", seg, ctx->i);
}

static void ce_scan_markup(CeCtx *ctx, size_t end);

static int ce_scan_heredoc(CeCtx *ctx, size_t end) {
    size_t start = ctx->i;
    size_t p = start + 3;
    char quote = '\0';
    size_t id_s, id_e, nl;
    if (p >= end) return 0;
    while (p < end && (ctx->text[p] == ' ' || ctx->text[p] == '\t')) p++;
    if (p < end && (ctx->text[p] == '\'' || ctx->text[p] == '"')) quote = ctx->text[p++];
    if (p >= end || !ce_is_ident_start(ctx->text[p])) return 0;
    id_s = p++;
    while (p < end && ce_is_ident_char(ctx->text[p])) p++;
    id_e = p;
    if (quote) {
        if (p >= end || ctx->text[p] != quote) return 0;
        p++;
    }
    nl = p;
    if (nl < end && ctx->text[nl] == '\r') nl++;
    if (nl >= end || ctx->text[nl] != '\n') return 0;
    ce_emit(ctx, "tok-keyword", start, p);
    ce_emit(ctx, "", p, nl + 1);
    ctx->i = nl + 1;
    {
        size_t id_len = id_e - id_s;
        size_t line_pos = ctx->i;
        while (line_pos < end) {
            size_t j = line_pos;
            while (j < end && (ctx->text[j] == ' ' || ctx->text[j] == '\t')) j++;
            if (j + id_len <= end && memcmp(ctx->text + j, ctx->text + id_s, id_len) == 0 &&
                (j + id_len == end || !ce_is_ident_char(ctx->text[j + id_len]))) {
                size_t k = j + id_len;
                size_t tail;
                if (k < end && ctx->text[k] == ';') k++;
                tail = k;
                while (tail < end && (ctx->text[tail] == ' ' || ctx->text[tail] == '\t')) tail++;
                if (tail >= end || ctx->text[tail] == '\n' || ctx->text[tail] == '\r') {
                    ce_scan_markup(ctx, j);
                    ce_emit(ctx, "tok-keyword", j, k);
                    ctx->i = k;
                    return 1;
                }
            }
            while (line_pos < end && ctx->text[line_pos] != '\n') line_pos++;
            if (line_pos < end) line_pos++;
        }
    }
    ce_scan_markup(ctx, end);
    return 1;
}

static void ce_scan_php(CeCtx *ctx, size_t end) {
    while (ctx->i < end) {
        size_t i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '/') {
            ce_scan_line_comment(ctx, end);
            continue;
        }
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '*') {
            ce_scan_block_comment(ctx, end);
            continue;
        }
        if (c == '#' && (i + 1 >= end || ctx->text[i + 1] != '[')) {
            ce_scan_line_comment(ctx, end);
            continue;
        }
        if (c == '\'') {
            ce_scan_php_single(ctx, end);
            continue;
        }
        if (c == '"') {
            ce_scan_php_double(ctx, end);
            continue;
        }
        if (c == '<' && i + 2 < end && ctx->text[i + 1] == '<' && ctx->text[i + 2] == '<' &&
            ce_scan_heredoc(ctx, end)) {
            continue;
        }
        if (c == '?' && i + 1 < end && ctx->text[i + 1] == '>') {
            ce_emit(ctx, "tok-php-tag", i, i + 2);
            ctx->i = i + 2;
            return;
        }
        if (c == '$' && ce_scan_variable(ctx, end)) continue;
        if (ce_is_ident_start(c)) {
            size_t we = i + 1;
            while (we < end && ce_is_ident_char(ctx->text[we])) we++;
            ce_emit(ctx, ce_is_php_word(ctx->text + i, we - i) ? "tok-keyword" : "", i, we);
            ctx->i = we;
            continue;
        }
        if (ce_is_digit(c) || (c == '.' && i + 1 < end && ce_is_digit(ctx->text[i + 1]))) {
            if (ce_scan_plain_number(ctx, end, 0)) continue;
        }
        if (ce_is_space(c)) {
            ce_emit_space(ctx, end);
            continue;
        }
        if (ce_scan_multi_op(ctx, end)) continue;
        ce_emit(ctx, "tok-punctuation", i, i + 1);
        ctx->i = i + 1;
    }
}

static void ce_scan_js(CeCtx *ctx, size_t end) {
    while (ctx->i < end) {
        size_t i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '/') {
            ce_scan_line_comment(ctx, end);
            continue;
        }
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '*') {
            ce_scan_block_comment(ctx, end);
            continue;
        }
        if (c == '"' || c == '\'' || c == '`') {
            ce_scan_simple_string(ctx, end, c);
            continue;
        }
        if (ce_is_space(c)) {
            ce_emit_space(ctx, end);
            continue;
        }
        if (ce_is_ident_start(c)) {
            size_t we = i + 1;
            while (we < end && ce_is_ident_char(ctx->text[we])) we++;
            ce_emit(ctx, ce_is_js_word(ctx->text + i, we - i) ? "tok-keyword" : "", i, we);
            ctx->i = we;
            continue;
        }
        if (ce_is_digit(c) || (c == '.' && i + 1 < end && ce_is_digit(ctx->text[i + 1]))) {
            if (ce_scan_plain_number(ctx, end, 0)) continue;
        }
        if (ce_scan_multi_op(ctx, end)) continue;
        ce_emit(ctx, "tok-punctuation", i, i + 1);
        ctx->i = i + 1;
    }
}

static void ce_scan_json(CeCtx *ctx, size_t end) {
    while (ctx->i < end) {
        size_t i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '/') {
            ce_scan_line_comment(ctx, end);
            continue;
        }
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '*') {
            ce_scan_block_comment(ctx, end);
            continue;
        }
        if (ce_is_space(c)) {
            ce_emit_space(ctx, end);
            continue;
        }
        if (c == '"') {
            size_t k = i + 1;
            size_t after;
            while (k < end) {
                if (ctx->text[k] == '\\') {
                    k = (k + 2 <= end) ? k + 2 : end;
                    continue;
                }
                if (ctx->text[k] == '"') {
                    k++;
                    break;
                }
                k++;
            }
            after = k;
            while (after < end && ce_is_space(ctx->text[after])) after++;
            ce_emit(ctx, (after < end && ctx->text[after] == ':') ? "tok-attr" : "tok-string", i, k);
            ctx->i = k;
            continue;
        }
        if (c == '-' || ce_is_digit(c)) {
            if (ce_scan_plain_number(ctx, end, 1)) continue;
        }
        if (ce_is_ident_start(c)) {
            size_t we = i + 1;
            int is_kw = 0;
            while (we < end && ce_is_ident_char(ctx->text[we])) we++;
            if ((we - i == 4 && (memcmp(ctx->text + i, "true", 4) == 0 || memcmp(ctx->text + i, "null", 4) == 0)) ||
                (we - i == 5 && memcmp(ctx->text + i, "false", 5) == 0)) {
                is_kw = 1;
            }
            ce_emit(ctx, is_kw ? "tok-keyword" : "", i, we);
            ctx->i = we;
            continue;
        }
        ce_emit(ctx, "tok-punctuation", i, i + 1);
        ctx->i = i + 1;
    }
}

static int ce_scan_css_hex(CeCtx *ctx, size_t end) {
    size_t i = ctx->i + 1;
    size_t digits = i, len;
    if (ctx->i >= end || ctx->text[ctx->i] != '#') return 0;
    while (i < end && ce_is_hex(ctx->text[i])) i++;
    len = i - digits;
    if (len != 3 && len != 4 && len != 6 && len != 8) return 0;
    if (i < end && ce_is_css_ident_char(ctx->text[i])) return 0;
    ce_emit(ctx, "tok-number", ctx->i, i);
    ctx->i = i;
    return 1;
}

static int ce_scan_css_number(CeCtx *ctx, size_t end) {
    size_t i = ctx->i, before;
    if (i < end && (ctx->text[i] == '-' || ctx->text[i] == '+')) {
        char next = (i + 1 < end) ? ctx->text[i + 1] : '\0';
        char next2 = (i + 2 < end) ? ctx->text[i + 2] : '\0';
        if (!(ce_is_digit(next) || (next == '.' && ce_is_digit(next2)))) return 0;
        i++;
    }
    before = i;
    if (i + 1 < end && ctx->text[i] == '.' && ce_is_digit(ctx->text[i + 1])) {
        i += 2;
        while (i < end && ce_is_digit(ctx->text[i])) i++;
    } else if (i < end && ce_is_digit(ctx->text[i])) {
        while (i < end && ce_is_digit(ctx->text[i])) i++;
        if (i + 1 < end && ctx->text[i] == '.' && ce_is_digit(ctx->text[i + 1])) {
            i += 2;
            while (i < end && ce_is_digit(ctx->text[i])) i++;
        }
    }
    if (i == before) return 0;
    if (i < end && ctx->text[i] == '%') i++;
    else if (i < end && ce_is_ident_start(ctx->text[i])) {
        while (i < end && ce_is_ident_char(ctx->text[i])) i++;
    }
    ce_emit(ctx, "tok-number", ctx->i, i);
    ctx->i = i;
    return 1;
}

static int ce_scan_css_ident(CeCtx *ctx, size_t end) {
    size_t start = ctx->i, i = start;
    if (i + 1 < end && ctx->text[i] == '-' && ctx->text[i + 1] == '-') i += 2;
    else if (i < end && ctx->text[i] == '-') i++;
    if (i >= end || !ce_is_css_ident_start(ctx->text[i])) return 0;
    while (i < end && ce_is_css_ident_char(ctx->text[i])) i++;
    if (i - start == 9 && strncasecmp(ctx->text + start, "important", 9) == 0) {
        ce_emit(ctx, "tok-keyword", start, i);
    } else {
        ce_emit(ctx, "tok-val", start, i);
    }
    ctx->i = i;
    return 1;
}

static void ce_scan_css_expr(CeCtx *ctx, size_t end) {
    int depth = 0;
    while (ctx->i < end) {
        size_t i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1 < end && ctx->text[i + 1] == '*') {
            ce_scan_block_comment(ctx, end);
            continue;
        }
        if (c == '"' || c == '\'') {
            ce_scan_simple_string(ctx, end, c);
            continue;
        }
        if (ce_is_space(c)) {
            ce_emit_space(ctx, end);
            continue;
        }
        if ((c == '{' || c == ';' || c == '}') && depth == 0) return;
        if (c == '(') {
            depth++;
            ce_emit(ctx, "tok-punctuation", i, i + 1);
            ctx->i = i + 1;
            continue;
        }
        if (c == ')') {
            if (depth > 0) depth--;
            ce_emit(ctx, "tok-punctuation", i, i + 1);
            ctx->i = i + 1;
            continue;
        }
        if (c == '#' && ce_scan_css_hex(ctx, end)) continue;
        if (ce_scan_css_number(ctx, end)) continue;
        if ((ce_is_css_ident_start(c) || c == '-') && ce_scan_css_ident(ctx, end)) continue;
        ce_emit(ctx, "tok-punctuation", i, i + 1);
        ctx->i = i + 1;
    }
}

static void ce_scan_css(CeCtx *ctx, size_t end) {
    int stack[64];
    int sp = 0;
    int pending_grouping = 0;
    stack[0] = 0; /* 0 = rules, 1 = decls */
    while (ctx->i < end) {
        char c = ctx->text[ctx->i];
        int mode;
        if (c == '/' && ctx->i + 1 < end && ctx->text[ctx->i + 1] == '*') {
            ce_scan_block_comment(ctx, end);
            continue;
        }
        if (ce_is_space(c)) {
            ce_emit_space(ctx, end);
            continue;
        }
        if (ctx->i >= end) break;
        mode = stack[sp];
        c = ctx->text[ctx->i];
        if (c == '{') {
            ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
            ctx->i++;
            if (sp + 1 < 64) {
                stack[++sp] = (mode == 0 && pending_grouping) ? 0 : 1;
            }
            pending_grouping = 0;
            continue;
        }
        if (c == '}') {
            ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
            ctx->i++;
            if (sp > 0) sp--;
            pending_grouping = 0;
            continue;
        }
        if (c == ';') {
            ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
            ctx->i++;
            pending_grouping = 0;
            continue;
        }
        if (mode == 0 && c == '@') {
            size_t start = ctx->i;
            size_t i = start + 1;
            while (i < end && ce_is_css_ident_char(ctx->text[i])) i++;
            pending_grouping = ce_is_grouping_at(ctx->text + start + 1, i - (start + 1));
            ce_emit(ctx, "tok-keyword", start, i);
            ctx->i = i;
            ce_scan_css_expr(ctx, end);
            continue;
        }
        if (mode == 0) {
            size_t start = ctx->i, i = start;
            while (i < end) {
                char sc = ctx->text[i];
                if (sc == '{' || sc == '}' || sc == '@' || sc == ';') break;
                if (sc == '/' && i + 1 < end && ctx->text[i + 1] == '*') break;
                i++;
            }
            if (i == start) i = start + 1;
            ce_emit(ctx, "tok-selector", start, i);
            ctx->i = i;
            continue;
        }
        {
            size_t start = ctx->i, i = start;
            if (i + 1 < end && ctx->text[i] == '-' && ctx->text[i + 1] == '-') i += 2;
            else if (i < end && ctx->text[i] == '-') i++;
            if (i < end && ce_is_css_ident_start(ctx->text[i])) {
                while (i < end && ce_is_css_ident_char(ctx->text[i])) i++;
                ce_emit(ctx, "tok-prop", start, i);
                ctx->i = i;
                ce_emit_gap(ctx, end);
                if (ctx->i < end && ctx->text[ctx->i] == ':') {
                    ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
                    ctx->i++;
                    ce_scan_css_expr(ctx, end);
                }
            } else {
                ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
                ctx->i++;
            }
        }
    }
}

static void ce_scan_html_quoted(CeCtx *ctx, size_t end, char quote, char *val_out, size_t val_sz) {
    size_t open = ctx->i, seg, vlen = 0;
    if (val_out && val_sz) val_out[0] = '\0';
    if (open >= end || ctx->text[open] != quote) return;
    ctx->i = open + 1;
    seg = open;
    while (ctx->i < end) {
        if (ctx->text[ctx->i] == quote) {
            ctx->i++;
            ce_emit(ctx, "tok-string", seg, ctx->i);
            return;
        }
        if (ctx->text[ctx->i] == '&') {
            ssize_t ent = ce_entity_end(ctx->text, ctx->i, end);
            if (ent > (ssize_t)ctx->i) {
                ce_emit(ctx, "tok-string", seg, ctx->i);
                ce_emit(ctx, "tok-entity", ctx->i, (size_t)ent);
                ctx->i = (size_t)ent;
                seg = ctx->i;
                continue;
            }
        }
        if (val_out && vlen + 1 < val_sz) {
            val_out[vlen++] = ctx->text[ctx->i];
            val_out[vlen] = '\0';
        }
        ctx->i++;
    }
    ce_emit(ctx, "tok-string", seg, ctx->i);
}

static void ce_scan_unquoted(CeCtx *ctx, size_t end, char *val_out, size_t val_sz) {
    size_t start = ctx->i, vlen = 0;
    if (val_out && val_sz) val_out[0] = '\0';
    while (ctx->i < end) {
        char c = ctx->text[ctx->i];
        if (ce_is_space(c) || c == '>' || c == '"' || c == '\'') break;
        if (c == '/' && ctx->i + 1 < end && ctx->text[ctx->i + 1] == '>') break;
        if (val_out && vlen + 1 < val_sz) {
            val_out[vlen++] = c;
            val_out[vlen] = '\0';
        }
        ctx->i++;
    }
    ce_emit(ctx, "tok-string", start, ctx->i);
}

static void ce_scan_ssi(CeCtx *ctx, size_t end) {
    size_t start = ctx->i;
    ssize_t close = ce_index_before(ctx->text, ctx->len, "-->", start + 4, end);
    size_t limit = (close < 0) ? end : (size_t)close;
    size_t p = start + 4;
    while (p < limit && ce_is_space(ctx->text[p])) p++;
    if (p >= limit || ctx->text[p] != '#') {
        size_t stop = (close < 0) ? end : (size_t)close + 3;
        ce_emit(ctx, "tok-comment", start, stop);
        ctx->i = stop;
        return;
    }
    p++;
    while (p < limit && ce_is_space(ctx->text[p])) p++;
    if (p >= limit || !isalpha((unsigned char)ctx->text[p])) {
        size_t stop = (close < 0) ? end : (size_t)close + 3;
        ce_emit(ctx, "tok-comment", start, stop);
        ctx->i = stop;
        return;
    }
    while (p < limit && isalpha((unsigned char)ctx->text[p])) p++;
    ce_emit(ctx, "tok-ssi", start, p);
    ctx->i = p;
    while (ctx->i < limit) {
        size_t i = ctx->i;
        char c = ctx->text[i];
        if (ce_is_space(c)) {
            ce_emit_space(ctx, limit);
            continue;
        }
        if (c == '"' || c == '\'') {
            ce_scan_html_quoted(ctx, limit, c, NULL, 0);
            continue;
        }
        if (ce_is_attr_start(c)) {
            ctx->i = i + 1;
            while (ctx->i < limit && ce_is_attr_char(ctx->text[ctx->i])) ctx->i++;
            ce_emit(ctx, "tok-attr", i, ctx->i);
            ce_emit_gap(ctx, limit);
            if (ctx->i < limit && ctx->text[ctx->i] == '=') {
                ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
                ctx->i++;
                ce_emit_gap(ctx, limit);
                if (ctx->i < limit && (ctx->text[ctx->i] == '"' || ctx->text[ctx->i] == '\'')) {
                    ce_scan_html_quoted(ctx, limit, ctx->text[ctx->i], NULL, 0);
                } else if (ctx->i < limit && ctx->text[ctx->i] != '>') {
                    ce_scan_unquoted(ctx, limit, NULL, 0);
                }
            }
            continue;
        }
        ce_emit(ctx, "tok-ssi", i, i + 1);
        ctx->i = i + 1;
    }
    if (close >= 0) {
        ce_emit(ctx, "tok-ssi", (size_t)close, (size_t)close + 3);
        ctx->i = (size_t)close + 3;
    }
}

static size_t ce_meta_head_len(const char *text, size_t start, size_t end) {
    static const char *const names[] = {"page-values", "related-sales", "generated-seo"};
    size_t p = start + 4, k;
    if (start + 4 > end || memcmp(text + start, "<!--", 4) != 0) return 0;
    while (p < end && ce_is_space(text[p])) p++;
    if (p < end && text[p] == '/') {
        p++;
        while (p < end && ce_is_space(text[p])) p++;
    }
    for (k = 0; k < 3; k++) {
        size_t nlen = strlen(names[k]);
        if (p + nlen <= end && strncasecmp(text + p, names[k], nlen) == 0) {
            char next = (p + nlen < end) ? text[p + nlen] : '\0';
            if (!ce_is_ident_char(next) && next != '-') return (p + nlen) - start;
        }
    }
    return 0;
}

static void ce_emit_classified(CeCtx *ctx, size_t start, size_t end, const char *cls) {
    size_t i = start, seg = start;
    while (i < end) {
        if (ctx->text[i] == '&') {
            ssize_t ent = ce_entity_end(ctx->text, i, end);
            if (ent > (ssize_t)i) {
                ce_emit(ctx, cls, seg, i);
                ce_emit(ctx, "tok-entity", i, (size_t)ent);
                i = (size_t)ent;
                seg = i;
                continue;
            }
        }
        i++;
    }
    ce_emit(ctx, cls, seg, end);
}

static void ce_scan_meta(CeCtx *ctx, size_t end, size_t head_len) {
    size_t start = ctx->i;
    ssize_t close = ce_index_before(ctx->text, ctx->len, "-->", start + 4, end);
    size_t limit = (close < 0) ? end : (size_t)close;
    size_t i = (start + head_len < limit) ? start + head_len : limit;
    ce_emit(ctx, "tok-meta-comment", start, i);
    while (i < limit) {
        size_t le = i;
        int nl = 0;
        size_t body_end, p, indent, key_end;
        while (le < limit && ctx->text[le] != '\n') le++;
        if (le < limit && ctx->text[le] == '\n') {
            le++;
            nl = 1;
        }
        body_end = le - (size_t)nl;
        p = i;
        while (p < body_end && (ctx->text[p] == ' ' || ctx->text[p] == '\t')) p++;
        indent = p - i;
        key_end = p;
        if (p + 12 <= body_end && strncasecmp(ctx->text + p, "Override IDs", 12) == 0) {
            key_end = p + 12;
        } else if (p < body_end && ce_is_ident_start(ctx->text[p])) {
            key_end = p + 1;
            while (key_end < body_end && (ce_is_ident_char(ctx->text[key_end]) || ctx->text[key_end] == '-')) key_end++;
        }
        if (key_end > p) {
            size_t cpos = key_end;
            while (cpos < body_end && (ctx->text[cpos] == ' ' || ctx->text[cpos] == '\t')) cpos++;
            if (cpos < body_end && ctx->text[cpos] == ':') {
                size_t klen = (cpos + 1) - p;
                if (indent) ce_emit(ctx, "tok-meta-comment", i, i + indent);
                ce_emit(ctx, "tok-meta-key", i + indent, i + indent + klen);
                ce_emit_classified(ctx, i + indent + klen, body_end, "tok-meta-val");
                if (nl) ce_emit(ctx, "", le - 1, le);
                i = le;
                continue;
            }
        }
        ce_emit(ctx, "tok-meta-comment", i, le);
        i = le;
    }
    ctx->i = limit;
    if (close >= 0) {
        ce_emit(ctx, "tok-meta-comment", (size_t)close, (size_t)close + 3);
        ctx->i = (size_t)close + 3;
    }
}

static void ce_scan_markup(CeCtx *ctx, size_t end) {
    while (ctx->i < end) {
        size_t i = ctx->i;
        size_t php_len = ce_php_open_len(ctx->text, i, end);
        if (php_len) {
            ce_emit(ctx, "tok-php-tag", i, i + php_len);
            ctx->i = i + php_len;
            ce_scan_php(ctx, end);
            continue;
        }
        if (i + 4 <= end && memcmp(ctx->text + i, "<!--", 4) == 0) {
            size_t p = i + 4;
            size_t mlen;
            while (p < end && p < i + 12 && ce_is_space(ctx->text[p])) p++;
            if (p < end && p < i + 12 && ctx->text[p] == '#') {
                ce_scan_ssi(ctx, end);
            } else if ((mlen = ce_meta_head_len(ctx->text, i, (i + 64 < end) ? i + 64 : end)) > 0) {
                ce_scan_meta(ctx, end, mlen);
            } else {
                ssize_t close = ce_index_before(ctx->text, ctx->len, "-->", i + 4, end);
                size_t stop = (close < 0) ? end : (size_t)close + 3;
                ce_emit(ctx, "tok-comment", i, stop);
                ctx->i = stop;
            }
            continue;
        }
        if (i + 9 <= end && strncasecmp(ctx->text + i, "<!doctype", 9) == 0 &&
            (i + 9 == end || (!ce_is_ident_char(ctx->text[i + 9]) && ctx->text[i + 9] != '-'))) {
            size_t stop = i + 9;
            while (stop < end && ctx->text[stop] != '>') stop++;
            if (stop < end) stop++;
            ce_emit(ctx, "tok-doctype", i, stop);
            ctx->i = stop;
            continue;
        }
        if (ctx->text[i] == '<' && i + 1 < end &&
            (ctx->text[i + 1] == '/' || ctx->text[i + 1] == '!' || ce_is_ident_start(ctx->text[i + 1]))) {
            size_t ti = i + 1;
            int closing = 0, self_closing = 0;
            if (ti < end && ctx->text[ti] == '/') {
                closing = 1;
                ti++;
            }
            if (ti < end && ce_is_ident_start(ctx->text[ti])) {
                size_t ns = ti++;
                char tag_name[32], type_val[64];
                size_t tlen;
                type_val[0] = '\0';
                while (ti < end && ce_is_name_char(ctx->text[ti])) ti++;
                tlen = ti - ns;
                if (tlen >= sizeof(tag_name)) tlen = sizeof(tag_name) - 1;
                memcpy(tag_name, ctx->text + ns, tlen);
                tag_name[tlen] = '\0';
                ce_emit(ctx, "tok-punctuation", i, ns);
                ce_emit(ctx, "tok-tag", ns, ti);
                ctx->i = ti;
                while (ctx->i < end) {
                    char c = ctx->text[ctx->i];
                    if (c == '>') {
                        ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
                        ctx->i++;
                        break;
                    }
                    if (c == '/' && ctx->i + 1 < end && ctx->text[ctx->i + 1] == '>') {
                        ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 2);
                        ctx->i += 2;
                        self_closing = 1;
                        break;
                    }
                    if (ce_is_space(c)) {
                        ce_emit_space(ctx, end);
                        continue;
                    }
                    if (!ce_is_attr_start(c)) {
                        ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
                        ctx->i++;
                        continue;
                    }
                    {
                        size_t as = ctx->i++;
                        int is_type = 0;
                        char val_buf[64];
                        val_buf[0] = '\0';
                        while (ctx->i < end && ce_is_attr_char(ctx->text[ctx->i])) ctx->i++;
                        if (ctx->i - as == 4 && strncasecmp(ctx->text + as, "type", 4) == 0) is_type = 1;
                        ce_emit(ctx, "tok-attr", as, ctx->i);
                        ce_emit_gap(ctx, end);
                        if (ctx->i >= end || ctx->text[ctx->i] != '=') continue;
                        ce_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1);
                        ctx->i++;
                        ce_emit_gap(ctx, end);
                        if (ctx->i < end && (ctx->text[ctx->i] == '"' || ctx->text[ctx->i] == '\'')) {
                            ce_scan_html_quoted(ctx, end, ctx->text[ctx->i], val_buf, sizeof(val_buf));
                        } else if (ctx->i < end && ctx->text[ctx->i] != '>' && ctx->text[ctx->i] != '/') {
                            ce_scan_unquoted(ctx, end, val_buf, sizeof(val_buf));
                        }
                        if (is_type) snprintf(type_val, sizeof(type_val), "%s", val_buf);
                    }
                }
                if (!closing && !self_closing) {
                    if (strcasecmp(tag_name, "style") == 0) {
                        size_t stop = ce_find_end_tag(ctx->text, ctx->i, end, "style");
                        ce_scan_css(ctx, stop);
                    } else if (strcasecmp(tag_name, "script") == 0) {
                        size_t stop = ce_find_end_tag(ctx->text, ctx->i, end, "script");
                        const char *tv = type_val;
                        while (*tv && ce_is_space(*tv)) tv++;
                        if (strncasecmp(tv, "application/ld+json", 19) == 0 ||
                            strncasecmp(tv, "application/json", 16) == 0) {
                            ce_scan_json(ctx, stop);
                        } else {
                            ce_scan_js(ctx, stop);
                        }
                    }
                }
                continue;
            }
            ce_emit(ctx, "", i, i + 1);
            ctx->i = i + 1;
            continue;
        }
        if (ctx->text[i] == '&') {
            ssize_t ent = ce_entity_end(ctx->text, i, end);
            if (ent > (ssize_t)i) {
                ce_emit(ctx, "tok-entity", i, (size_t)ent);
                ctx->i = (size_t)ent;
                continue;
            }
        }
        {
            size_t p = i;
            while (p < end && ctx->text[p] != '<' && ctx->text[p] != '&') p++;
            if (p == i) p = i + 1;
            ce_emit(ctx, "", i, p);
            ctx->i = p;
        }
    }
}

char *code_engine_highlight_syntax(const char *source, const char *language) {
    CeCtx ctx;
    const char *lang = (language && language[0]) ? language : "text";
    if (!source) source = "";
    ctx.text = source;
    ctx.len = strlen(source);
    ctx.i = 0;
    cebuf_init(&ctx.out);
    if (ctx.len == 0 || strcasecmp(lang, "text") == 0 || strcasecmp(lang, "plain") == 0) {
        cebuf_append_esc(&ctx.out, source, ctx.len);
        return cebuf_steal(&ctx.out);
    }
    if (strcasecmp(lang, "css") == 0) {
        ce_scan_css(&ctx, ctx.len);
    } else if (strcasecmp(lang, "js") == 0 || strcasecmp(lang, "javascript") == 0) {
        ce_scan_js(&ctx, ctx.len);
    } else if (strcasecmp(lang, "json") == 0) {
        ce_scan_json(&ctx, ctx.len);
    } else if (strcasecmp(lang, "php") == 0) {
        size_t p = 0;
        while (p < ctx.len && ce_is_space(source[p])) p++;
        if (p >= ctx.len || source[p] != '<') {
            ce_scan_php(&ctx, ctx.len);
            if (ctx.i < ctx.len) ce_scan_markup(&ctx, ctx.len);
        } else {
            ce_scan_markup(&ctx, ctx.len);
        }
    } else {
        ce_scan_markup(&ctx, ctx.len);
    }
    if (ctx.i < ctx.len) ce_emit(&ctx, "", ctx.i, ctx.len);
    return cebuf_steal(&ctx.out);
}

char *code_engine_highlight_document(const char *source, const char *language) {
    char *html = code_engine_highlight_syntax(source, language);
    size_t slen = source ? strlen(source) : 0;
    if (html && slen > 0 && source[slen - 1] == '\n') {
        size_t hlen = strlen(html);
        char *next = (char *)realloc(html, hlen + 2);
        if (next) {
            next[hlen] = ' ';
            next[hlen + 1] = '\0';
            html = next;
        }
    }
    return html;
}

typedef struct CeLineSlice {
    const char *ptr;
    size_t len;
    unsigned int hash;
} CeLineSlice;

static char *ce_normalize_newlines(const char *s, size_t *out_len) {
    size_t i, n = s ? strlen(s) : 0, w = 0;
    char *out = (char *)malloc(n + 1);
    if (!out) return NULL;
    for (i = 0; i < n; i++) {
        if (s[i] == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') continue;
            out[w++] = '\n';
        } else {
            out[w++] = s[i];
        }
    }
    out[w] = '\0';
    if (out_len) *out_len = w;
    return out;
}

static CeLineSlice *ce_split_lines(const char *norm, size_t len, size_t *count_out) {
    size_t count = 1, i, idx = 0, start = 0;
    CeLineSlice *arr;
    for (i = 0; i < len; i++) {
        if (norm[i] == '\n') count++;
    }
    arr = (CeLineSlice *)malloc(count * sizeof(CeLineSlice));
    if (!arr) {
        *count_out = 0;
        return NULL;
    }
    for (i = 0; i <= len; i++) {
        if (i == len || norm[i] == '\n') {
            size_t l = i - start, k;
            unsigned int h = 2166136261u;
            for (k = 0; k < l; k++) {
                h ^= (unsigned char)norm[start + k];
                h *= 16777619u;
            }
            arr[idx].ptr = norm + start;
            arr[idx].len = l;
            arr[idx].hash = h;
            idx++;
            start = i + 1;
        }
    }
    *count_out = count;
    return arr;
}

static int ce_lines_eq(const CeLineSlice *a, const CeLineSlice *b) {
    return a->len == b->len && a->hash == b->hash &&
           (a->len == 0 || memcmp(a->ptr, b->ptr, a->len) == 0);
}

typedef struct CeAnchorPair {
    ssize_t b;
    ssize_t c;
} CeAnchorPair;

JsonValue *code_engine_compute_line_diff(const char *base_text, const char *current_text) {
    size_t curr_len = 0, base_len = 0, curr_count = 0, base_count = 0, i;
    char *curr_norm = ce_normalize_newlines(current_text ? current_text : "", &curr_len);
    CeLineSlice *curr_lines = ce_split_lines(curr_norm ? curr_norm : "", curr_len, &curr_count);
    unsigned char *line_kinds = (unsigned char *)calloc(curr_count ? curr_count : 1, 1);
    unsigned char *deleted_after = (unsigned char *)calloc(curr_count ? curr_count : 1, 1);
    JsonValue *root = json_object();
    JsonValue *deletions = json_array();
    JsonValue *hunks = json_array();
    long long added_count = 0, modified_count = 0, deleted_count = 0;

    if (base_text && curr_norm && curr_lines && strcmp(base_text, current_text ? current_text : "") != 0) {
        char *base_norm = ce_normalize_newlines(base_text, &base_len);
        if (base_norm && strcmp(base_norm, curr_norm) != 0) {
            CeLineSlice *base_lines = ce_split_lines(base_norm, base_len, &base_count);
            if (base_lines) {
                size_t prefix = 0;
                size_t min_len = base_count < curr_count ? base_count : curr_count;
                ssize_t base_end, curr_end, m, n;
                CeAnchorPair *anchors = NULL;
                size_t anchor_count = 0;

                while (prefix < min_len && ce_lines_eq(&base_lines[prefix], &curr_lines[prefix])) prefix++;
                base_end = (ssize_t)base_count - 1;
                curr_end = (ssize_t)curr_count - 1;
                while (base_end >= (ssize_t)prefix && curr_end >= (ssize_t)prefix &&
                       ce_lines_eq(&base_lines[base_end], &curr_lines[curr_end])) {
                    base_end--;
                    curr_end--;
                }
                m = (base_end >= (ssize_t)prefix) ? (base_end - (ssize_t)prefix + 1) : 0;
                n = (curr_end >= (ssize_t)prefix) ? (curr_end - (ssize_t)prefix + 1) : 0;
                anchors = (CeAnchorPair *)malloc((size_t)(m + n + 2) * sizeof(CeAnchorPair));
                if (anchors && m > 0 && n > 0) {
                    if ((size_t)m * (size_t)n <= 360000u) {
                        size_t stride = (size_t)n + 1;
                        unsigned short *dp = (unsigned short *)calloc(((size_t)m + 1) * stride, sizeof(unsigned short));
                        if (dp) {
                            ssize_t r, c;
                            for (r = m - 1; r >= 0; r--) {
                                for (c = n - 1; c >= 0; c--) {
                                    if (ce_lines_eq(&base_lines[(ssize_t)prefix + r], &curr_lines[(ssize_t)prefix + c])) {
                                        dp[(size_t)r * stride + (size_t)c] = (unsigned short)(dp[(size_t)(r + 1) * stride + (size_t)(c + 1)] + 1);
                                    } else {
                                        unsigned short dn = dp[(size_t)(r + 1) * stride + (size_t)c];
                                        unsigned short rt = dp[(size_t)r * stride + (size_t)(c + 1)];
                                        dp[(size_t)r * stride + (size_t)c] = dn >= rt ? dn : rt;
                                    }
                                }
                            }
                            r = 0; c = 0;
                            while (r < m && c < n) {
                                if (ce_lines_eq(&base_lines[(ssize_t)prefix + r], &curr_lines[(ssize_t)prefix + c])) {
                                    anchors[anchor_count].b = (ssize_t)prefix + r;
                                    anchors[anchor_count].c = (ssize_t)prefix + c;
                                    anchor_count++;
                                    r++; c++;
                                } else if (dp[(size_t)(r + 1) * stride + (size_t)c] >= dp[(size_t)r * stride + (size_t)(c + 1)]) {
                                    r++;
                                } else {
                                    c++;
                                }
                            }
                            free(dp);
                        }
                    } else {
                        ssize_t r = 0, c = 0;
                        while (r < m && c < n) {
                            if (ce_lines_eq(&base_lines[(ssize_t)prefix + r], &curr_lines[(ssize_t)prefix + c])) {
                                anchors[anchor_count].b = (ssize_t)prefix + r;
                                anchors[anchor_count].c = (ssize_t)prefix + c;
                                anchor_count++;
                                r++; c++;
                                continue;
                            }
                            {
                                int matched = 0;
                                ssize_t step, rem = (m - r) > (n - c) ? (m - r) : (n - c);
                                ssize_t lim = rem < 12 ? rem : 12;
                                for (step = 1; step <= lim; step++) {
                                    if (c + step < n && ce_lines_eq(&base_lines[(ssize_t)prefix + r], &curr_lines[(ssize_t)prefix + c + step])) {
                                        c += step; matched = 1; break;
                                    }
                                    if (r + step < m && ce_lines_eq(&base_lines[(ssize_t)prefix + r + step], &curr_lines[(ssize_t)prefix + c])) {
                                        r += step; matched = 1; break;
                                    }
                                }
                                if (!matched) { r++; c++; }
                            }
                        }
                    }
                }
                if (anchors) {
                    ssize_t prev_b = (ssize_t)prefix - 1, prev_c = (ssize_t)prefix - 1;
                    anchors[anchor_count].b = base_end + 1;
                    anchors[anchor_count].c = curr_end + 1;
                    anchor_count++;
                    for (i = 0; i < anchor_count; i++) {
                        ssize_t next_b = anchors[i].b, next_c = anchors[i].c;
                        ssize_t del_span = next_b - prev_b - 1, ins_span = next_c - prev_c - 1;
                        if (del_span > 0 && ins_span == 0) {
                            ssize_t tidx = prev_c >= 0 ? prev_c : 0;
                            JsonValue *dobj = json_object(), *hobj = json_object();
                            if (tidx >= (ssize_t)curr_count) tidx = (ssize_t)curr_count - 1;
                            if (tidx < 0) tidx = 0;
                            deleted_after[tidx] = 1;
                            deleted_count += del_span;
                            json_object_put(dobj, "line", json_number_int(tidx + 1));
                            json_object_put(dobj, "count", json_number_int(del_span));
                            json_array_add(deletions, dobj);
                            json_object_put(hobj, "startLine", json_number_int(tidx + 1));
                            json_object_put(hobj, "endLine", json_number_int(tidx + 1));
                            json_object_put(hobj, "kind", json_string_cstr("del"));
                            json_object_put(hobj, "count", json_number_int(del_span));
                            json_array_add(hunks, hobj);
                        } else if (del_span == 0 && ins_span > 0) {
                            ssize_t sidx = prev_c + 1, eidx = next_c - 1, k;
                            JsonValue *hobj = json_object();
                            for (k = sidx; k <= eidx; k++) { line_kinds[k] = CODE_DIFF_ADDED; added_count++; }
                            json_object_put(hobj, "startLine", json_number_int(sidx + 1));
                            json_object_put(hobj, "endLine", json_number_int(eidx + 1));
                            json_object_put(hobj, "kind", json_string_cstr("add"));
                            json_object_put(hobj, "count", json_number_int(ins_span));
                            json_array_add(hunks, hobj);
                        } else if (del_span > 0 && ins_span > 0) {
                            ssize_t sidx = prev_c + 1, eidx = next_c - 1, k;
                            ssize_t mod_span = del_span < ins_span ? del_span : ins_span;
                            JsonValue *hobj = json_object();
                            for (k = 0; k < ins_span; k++) {
                                if (k < mod_span) { line_kinds[sidx + k] = CODE_DIFF_MODIFIED; modified_count++; }
                                else { line_kinds[sidx + k] = CODE_DIFF_ADDED; added_count++; }
                            }
                            json_object_put(hobj, "startLine", json_number_int(sidx + 1));
                            json_object_put(hobj, "endLine", json_number_int(eidx + 1));
                            json_object_put(hobj, "kind", json_string_cstr("mod"));
                            json_object_put(hobj, "count", json_number_int(ins_span));
                            json_array_add(hunks, hobj);
                            if (del_span > ins_span) {
                                ssize_t extra = del_span - ins_span;
                                ssize_t tidx = eidx >= (ssize_t)curr_count ? (ssize_t)curr_count - 1 : (eidx < 0 ? 0 : eidx);
                                JsonValue *dobj = json_object();
                                deleted_after[tidx] = 1;
                                deleted_count += extra;
                                json_object_put(dobj, "line", json_number_int(tidx + 1));
                                json_object_put(dobj, "count", json_number_int(extra));
                                json_array_add(deletions, dobj);
                            }
                        }
                        prev_b = next_b;
                        prev_c = next_c;
                    }
                    free(anchors);
                }
                free(base_lines);
            }
        }
        free(base_norm);
    }

    {
        JsonValue *lk_arr = json_array(), *da_arr = json_array();
        for (i = 0; i < curr_count; i++) {
            json_array_add(lk_arr, json_number_int(line_kinds[i]));
            json_array_add(da_arr, json_number_int(deleted_after[i]));
        }
        json_object_put(root, "hasChanges", json_bool(added_count > 0 || modified_count > 0 || deleted_count > 0));
        json_object_put(root, "addedCount", json_number_int(added_count));
        json_object_put(root, "modifiedCount", json_number_int(modified_count));
        json_object_put(root, "deletedCount", json_number_int(deleted_count));
        json_object_put(root, "lineKinds", lk_arr);
        json_object_put(root, "deletedAfter", da_arr);
        json_object_put(root, "deletions", deletions);
        json_object_put(root, "hunks", hunks);
    }
    free(line_kinds);
    free(deleted_after);
    free(curr_lines);
    free(curr_norm);
    return root;
}

static int ce_is_minimap_kw(const char *w, size_t len) {
    static const char *const kw[] = {
        "function", "return", "if", "else", "elseif", "foreach", "for", "while",
        "require_once", "include", "include_once", "require", "echo", "isset",
        "var", "let", "const", "class", "async", "await", "switch", "case",
        "break", "continue", "try", "catch", "throw", "new", "true", "false", "null"
    };
    size_t i;
    for (i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        if (strlen(kw[i]) == len && strncasecmp(w, kw[i], len) == 0) return 1;
    }
    return 0;
}

static int ce_is_block_tag(const char *s, size_t len, size_t *out_tag_len) {
    static const char *const tags[] = {
        "section", "article", "header", "footer", "main", "nav", "aside",
        "figure", "div", "form", "table", "ul", "ol", "style", "script"
    };
    size_t i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        size_t tlen = strlen(tags[i]);
        if (len >= tlen && strncasecmp(s, tags[i], tlen) == 0 &&
            (len == tlen || !ce_is_name_char(s[tlen]))) {
            if (out_tag_len) *out_tag_len = tlen;
            return 1;
        }
    }
    return 0;
}

static void ce_push_seg(JsonValue *segs, size_t col, size_t len, const char *token) {
    JsonValue *obj = json_object();
    if (!obj) return;
    json_object_put(obj, "col", json_number_int((long long)col));
    json_object_put(obj, "len", json_number_int((long long)(len > 0 ? len : 1)));
    json_object_put(obj, "token", json_string_cstr(token));
    json_array_add(segs, obj);
}

JsonValue *code_engine_scan_minimap_lines(const char *source, const char *language) {
    const char *lang = (language && language[0]) ? language : "text";
    JsonValue *out = json_array();
    int in_comment = 0, in_meta = 0;
    int in_php = (strcasecmp(lang, "php") == 0);
    int in_style = (strcasecmp(lang, "css") == 0);
    int in_script = (strcasecmp(lang, "js") == 0);
    char block_stack[64][64];
    int bsp = 0;
    const char *p = source ? source : "";

    for (;;) {
        const char *nl = strchr(p, '\n');
        size_t raw_len = nl ? (size_t)(nl - p) : strlen(p);
        char expanded[256];
        size_t exp_len = 0, i, indent = 0, trim_s, trim_e, limit, col;
        int is_block_header = 0, is_meta_start = 0;
        char active_label[64];
        JsonValue *line_obj, *segs;

        for (i = 0; i < raw_len && exp_len + 4 < sizeof(expanded); i++) {
            if (p[i] == '\t') {
                memcpy(expanded + exp_len, "    ", 4);
                exp_len += 4;
            } else if (p[i] != '\r') {
                expanded[exp_len++] = p[i];
            }
        }
        expanded[exp_len] = '\0';
        while (indent < exp_len && expanded[indent] == ' ') indent++;
        trim_s = indent;
        trim_e = exp_len;
        while (trim_e > trim_s && ce_is_space(expanded[trim_e - 1])) trim_e--;
        snprintf(active_label, sizeof(active_label), "%s", bsp > 0 ? block_stack[bsp - 1] : "");

        if (!in_comment && trim_e > trim_s) {
            const char *tr = expanded + trim_s;
            size_t tr_len = trim_e - trim_s;
            size_t mlen = ce_meta_head_len(tr, 0, tr_len);
            if (mlen > 0 && tr[4] != '/') {
                in_meta = 1;
                is_meta_start = 1;
                is_block_header = 1;
                if (ce_strcasestr(tr, "page-values")) snprintf(active_label, sizeof(active_label), "<!-- page-values -->");
                else if (ce_strcasestr(tr, "related-sales")) snprintf(active_label, sizeof(active_label), "<!-- related-sales -->");
                else snprintf(active_label, sizeof(active_label), "<!-- generated-seo -->");
                if (bsp < 64) snprintf(block_stack[bsp++], 64, "%s", active_label);
            } else if (strcasecmp(lang, "html") == 0 || strcasecmp(lang, "php") == 0) {
                size_t tlen = 0;
                if (tr[0] == '<' && ce_is_block_tag(tr + 1, tr_len - 1, &tlen)) {
                    char tname[24], suffix[40], close_pat[32];
                    const char *attrs = tr + 1 + tlen;
                    size_t attrs_len = tr_len - 1 - tlen;
                    const char *gt = memchr(attrs, '>', attrs_len);
                    size_t k;
                    if (gt) attrs_len = (size_t)(gt - attrs);
                    for (k = 0; k < tlen && k + 1 < sizeof(tname); k++) tname[k] = ce_fold(tr[1 + k]);
                    tname[k] = '\0';
                    suffix[0] = '\0';
                    {
                        const char *cm = ce_strcasestr(attrs, "class=");
                        const char *im = ce_strcasestr(attrs, "id=");
                        if (cm && (size_t)(cm - attrs) + 7 < attrs_len && (cm[6] == '"' || cm[6] == '\'')) {
                            char q = cm[6];
                            const char * vs = cm + 7;
                            size_t vi = 0;
                            while ((size_t)(vs - attrs) < attrs_len && (*vs == ' ' || *vs == '\t')) vs++;
                            suffix[vi++] = '.';
                            while ((size_t)(vs - attrs) < attrs_len && *vs != q && !ce_is_space(*vs) && vi + 1 < sizeof(suffix)) {
                                suffix[vi++] = *vs++;
                            }
                            suffix[vi] = '\0';
                            if (vi == 1) suffix[0] = '\0';
                        } else if (im && (size_t)(im - attrs) + 4 < attrs_len && (im[3] == '"' || im[3] == '\'')) {
                            char q = im[3];
                            const char *vs = im + 4;
                            size_t vi = 0;
                            while ((size_t)(vs - attrs) < attrs_len && (*vs == ' ' || *vs == '\t')) vs++;
                            suffix[vi++] = '#';
                            while ((size_t)(vs - attrs) < attrs_len && *vs != q && !ce_is_space(*vs) && vi + 1 < sizeof(suffix)) {
                                suffix[vi++] = *vs++;
                            }
                            suffix[vi] = '\0';
                            if (vi == 1) suffix[0] = '\0';
                        }
                    }
                    snprintf(active_label, sizeof(active_label), "<%s%s>", tname, suffix);
                    is_block_header = 1;
                    snprintf(close_pat, sizeof(close_pat), "</%s", tname);
                    if (!(tr_len >= 2 && tr[tr_len - 2] == '/' && tr[tr_len - 1] == '>') &&
                        !ce_strcasestr(tr, close_pat) && bsp < 64) {
                        snprintf(block_stack[bsp++], 64, "%s", active_label);
                    }
                    if (strcmp(tname, "style") == 0 && !ce_strcasestr(tr, "</style")) in_style = 1;
                    if (strcmp(tname, "script") == 0 && !ce_strcasestr(tr, "</script")) in_script = 1;
                } else if (tr_len >= 3 && tr[0] == '<' && tr[1] == '/' && ce_is_block_tag(tr + 2, tr_len - 2, &tlen)) {
                    if (bsp > 0) bsp--;
                    if (strncasecmp(tr, "</style", 7) == 0) in_style = 0;
                    if (strncasecmp(tr, "</script", 8) == 0) in_script = 0;
                }
            } else if ((strcasecmp(lang, "js") == 0 || strcasecmp(lang, "css") == 0) && tr[tr_len - 1] == '{') {
                is_block_header = 1;
                snprintf(active_label, sizeof(active_label), "%.*s", (int)(tr_len - 1 > 32 ? 32 : tr_len - 1), tr);
                if (bsp < 64) snprintf(block_stack[bsp++], 64, "%s", active_label);
            } else if ((strcasecmp(lang, "js") == 0 || strcasecmp(lang, "css") == 0) && tr[0] == '}') {
                if (bsp > 0) bsp--;
            }
        }

        segs = json_array();
        limit = exp_len < 92 ? exp_len : 92;
        col = indent < limit ? indent : limit;
        while (col < limit) {
            const char *rest = expanded + col;
            size_t rlen = limit - col;
            if (expanded[col] == ' ') { col++; continue; }
            if (in_comment || in_meta) {
                const char *cl = strstr(rest, "-->");
                if (cl && (size_t)(cl - rest) < rlen) {
                    size_t sp = (size_t)(cl - rest) + 3;
                    ce_push_seg(segs, col, sp, in_meta ? "metaKey" : "comment");
                    col += sp;
                    in_comment = 0;
                    if (in_meta && bsp > 0) bsp--;
                    in_meta = 0;
                } else {
                    const char *colon = memchr(rest, ':', rlen);
                    if (in_meta && colon) {
                        size_t clen = (size_t)(colon - rest) + 1;
                        ce_push_seg(segs, col, clen, "metaKey");
                        if (clen < rlen) ce_push_seg(segs, col + clen, rlen - clen, "string");
                    } else {
                        ce_push_seg(segs, col, rlen, in_meta ? "metaKey" : "comment");
                    }
                    break;
                }
                continue;
            }
            if (rlen >= 5 && memcmp(rest, "<!--#", 5) == 0) {
                const char *cl = strstr(rest, "-->");
                size_t sp = (cl && (size_t)(cl - rest) + 3 <= rlen) ? (size_t)(cl - rest) + 3 : rlen;
                ce_push_seg(segs, col, sp, "ssi");
                col += sp;
                continue;
            }
            if (rlen >= 4 && memcmp(rest, "<!--", 4) == 0) {
                const char *cl = strstr(rest, "-->");
                if (cl && (size_t)(cl - rest) + 3 <= rlen) {
                    size_t sp = (size_t)(cl - rest) + 3;
                    ce_push_seg(segs, col, sp, "comment");
                    col += sp;
                } else {
                    in_comment = 1;
                    ce_push_seg(segs, col, rlen, "comment");
                    break;
                }
                continue;
            }
            if (rlen >= 2 && rest[0] == '<' && rest[1] == '?') {
                size_t sp = (rlen >= 5 && strncasecmp(rest, "<?php", 5) == 0) ? 5 : 2;
                in_php = 1;
                ce_push_seg(segs, col, sp, "ssi");
                col += sp;
                continue;
            }
            if (in_php && rlen >= 2 && rest[0] == '?' && rest[1] == '>') {
                in_php = (strcasecmp(lang, "php") == 0 && !strchr(expanded, '<'));
                ce_push_seg(segs, col, 2, "ssi");
                col += 2;
                continue;
            }
            if ((in_php || in_script || in_style) && rlen >= 2 &&
                ((rest[0] == '/' && rest[1] == '/') || (rest[0] == '/' && rest[1] == '*'))) {
                ce_push_seg(segs, col, rlen, "comment");
                break;
            }
            if (expanded[col] == '"' || expanded[col] == '\'') {
                char q = expanded[col];
                size_t e = col + 1;
                while (e < limit && expanded[e] != q) {
                    if (expanded[e] == '\\') e++;
                    e++;
                }
                if (e < limit) e++;
                ce_push_seg(segs, col, e - col, "string");
                col = e;
                continue;
            }
            if (expanded[col] == '<' && (strcasecmp(lang, "html") == 0 || strcasecmp(lang, "php") == 0)) {
                size_t e = col + 1;
                if (e < limit && (expanded[e] == '/' || expanded[e] == '!')) e++;
                while (e < limit && ce_is_name_char(expanded[e])) e++;
                ce_push_seg(segs, col, e - col, "tag");
                col = e;
                continue;
            }
            if (expanded[col] == '$' && in_php) {
                size_t e = col + 1;
                while (e < limit && ce_is_ident_char(expanded[e])) e++;
                ce_push_seg(segs, col, e - col, "variable");
                col = e;
                continue;
            }
            if (ce_is_ident_start(expanded[col]) || expanded[col] == '@' || expanded[col] == '-') {
                size_t e = col + 1;
                const char *tok = "text";
                while (e < limit && ce_is_name_char(expanded[e])) e++;
                if (e < limit && (expanded[e] == '=' || (in_style && expanded[e] == ':'))) tok = "attr";
                else if (expanded[col] == '@' || ce_is_minimap_kw(expanded + col, e - col)) tok = "keyword";
                else if (in_style) tok = "tag";
                ce_push_seg(segs, col, e - col, tok);
                col = e;
                continue;
            }
            if (ce_is_digit(expanded[col]) || expanded[col] == '#') {
                size_t e = col + 1;
                while (e < limit && (ce_is_hex(expanded[e]) || strchr(".%pxremvhw-", expanded[e]))) e++;
                ce_push_seg(segs, col, e - col, "number");
                col = e;
                continue;
            }
            {
                size_t e = col + 1;
                while (e < limit && !strchr(" \t\"'<$@#", expanded[e]) && !ce_is_ident_char(expanded[e])) e++;
                ce_push_seg(segs, col, e - col, "punct");
                col = e;
            }
        }

        line_obj = json_object();
        json_object_put(line_obj, "indent", json_number_int((long long)(indent < 92 ? indent : 92)));
        json_object_put(line_obj, "blockDepth", json_number_int((long long)bsp));
        json_object_put(line_obj, "isBlockHeader", json_bool(is_block_header));
        json_object_put(line_obj, "isMetaBlock", json_bool(in_meta || is_meta_start));
        json_object_put(line_obj, "blockLabel", json_string_cstr(active_label));
        json_object_put(line_obj, "segments", segs);
        json_array_add(out, line_obj);
        if (!nl) break;
        p = nl + 1;
    }
    return out;
}

const char *code_engine_detect_comment_context(const char *source, size_t char_offset,
                                               const char *file_path) {
    const char *base_lang = code_engine_language_from_path(file_path);
    size_t len = source ? strlen(source) : 0;
    size_t target = char_offset > len ? len : char_offset;
    int in_php = 0;
    const char *enclosing = "";
    size_t i = 0;
    if (strcmp(base_lang, "css") == 0) return "css";
    if (strcmp(base_lang, "js") == 0) return "js";
    if (strcmp(base_lang, "json") == 0) return "json";
    if (strcmp(base_lang, "php") == 0) {
        size_t p = 0;
        while (p < len && ce_is_space(source[p])) p++;
        if (p >= len || source[p] != '<') in_php = 1;
    }
    while (i < target) {
        if (in_php) {
            if (i + 2 <= len && source[i] == '?' && source[i + 1] == '>') {
                if (target <= i + 1) return "php";
                in_php = 0;
                i += 2;
                continue;
            }
            i++;
            continue;
        }
        {
            size_t plen = ce_php_open_len(source, i, len);
            if (plen > 0) {
                if (target < i + plen) return "php";
                in_php = 1;
                i += plen;
                continue;
            }
        }
        if (strcmp(enclosing, "css") == 0 && i + 7 <= len && strncasecmp(source + i, "</style", 7) == 0) {
            enclosing = "";
        } else if (strcmp(enclosing, "js") == 0 && i + 8 <= len && strncasecmp(source + i, "</script", 8) == 0) {
            enclosing = "";
        } else if (!enclosing[0] && i + 6 <= len && strncasecmp(source + i, "<style", 6) == 0 &&
                   !ce_is_name_char(source[i + 6])) {
            const char *gt = strchr(source + i, '>');
            if (gt && target > (size_t)(gt - source)) enclosing = "css";
        } else if (!enclosing[0] && i + 7 <= len && strncasecmp(source + i, "<script", 7) == 0 &&
                   !ce_is_name_char(source[i + 7])) {
            const char *gt = strchr(source + i, '>');
            if (gt && target > (size_t)(gt - source)) enclosing = "js";
        }
        i++;
    }
    if (in_php) return "php";
    if (enclosing[0]) return enclosing;
    return "html";
}

static JsonValue *ce_comment_result(size_t from, size_t to, const char *insert,
                                    size_t sel_start, size_t sel_end, const char *ctx) {
    JsonValue *obj = json_object();
    json_object_put(obj, "from", json_number_int((long long)from));
    json_object_put(obj, "to", json_number_int((long long)to));
    json_object_put(obj, "insert", json_string_cstr(insert ? insert : ""));
    json_object_put(obj, "selectionStart", json_number_int((long long)sel_start));
    json_object_put(obj, "selectionEnd", json_number_int((long long)sel_end));
    json_object_put(obj, "context", json_string_cstr(ctx ? ctx : "html"));
    return obj;
}

JsonValue *code_engine_compute_comment_toggle(const char *source, size_t selection_start,
                                              size_t selection_end, const char *file_path) {
    const char *text = source ? source : "";
    size_t len = strlen(text);
    size_t start = selection_start < selection_end ? selection_start : selection_end;
    size_t end = selection_start < selection_end ? selection_end : selection_start;
    size_t line_s, line_e, first_nw, last_nw, tr_len;
    const char *ctx;
    int is_line_ctx;

    if (start > len) start = len;
    if (end > len) end = len;
    line_s = start;
    while (line_s > 0 && text[line_s - 1] != '\n') line_s--;
    line_e = end;
    while (line_e < len && text[line_e] != '\n') line_e++;
    if (line_e > line_s && text[line_e - 1] == '\r') line_e--;

    first_nw = line_s;
    while (first_nw < line_e && (text[first_nw] == ' ' || text[first_nw] == '\t')) first_nw++;
    last_nw = line_e;
    while (last_nw > first_nw && (text[last_nw - 1] == ' ' || text[last_nw - 1] == '\t')) last_nw--;
    tr_len = last_nw - first_nw;

    ctx = code_engine_detect_comment_context(text, tr_len ? first_nw : start, file_path);
    if (tr_len >= 7 && strncasecmp(text + first_nw, "<style", 6) == 0) ctx = "html";
    if (tr_len >= 8 && strncasecmp(text + first_nw, "<script", 7) == 0) ctx = "html";
    if (tr_len >= 7 && memcmp(text + first_nw, "<!--", 4) == 0 && memcmp(text + last_nw - 3, "-->", 3) == 0) ctx = "html";
    is_line_ctx = (strcmp(ctx, "php") == 0 || strcmp(ctx, "js") == 0 || strcmp(ctx, "json") == 0);

    if (tr_len == 0) {
        size_t ind = first_nw - line_s;
        CeBuf ins;
        char *s;
        size_t cur_off;
        cebuf_init(&ins);
        cebuf_append(&ins, text + line_s, ind);
        if (strcmp(ctx, "html") == 0) { cebuf_puts(&ins, "<!--  -->"); cur_off = 5; }
        else if (strcmp(ctx, "css") == 0) { cebuf_puts(&ins, "/*  */"); cur_off = 3; }
        else { cebuf_puts(&ins, "// "); cur_off = 3; }
        s = cebuf_steal(&ins);
        {
            JsonValue *r = ce_comment_result(line_s, line_e, s, line_s + ind + cur_off, line_s + ind + cur_off, ctx);
            free(s);
            return r;
        }
    }

    if (tr_len >= 7 && memcmp(text + first_nw, "<!--", 4) == 0 && memcmp(text + last_nw - 3, "-->", 3) == 0) {
        size_t pre = (tr_len >= 8 && text[first_nw + 4] == ' ') ? 5 : 4;
        size_t suf = (tr_len >= pre + 4 && text[last_nw - 4] == ' ') ? 4 : 3;
        size_t ilen = tr_len - pre - suf;
        char *inner = (char *)malloc(ilen + 1);
        size_t nc = start > pre ? start - pre : first_nw;
        JsonValue *r;
        memcpy(inner, text + first_nw + pre, ilen);
        inner[ilen] = '\0';
        if (nc < first_nw) nc = first_nw;
        if (nc > first_nw + ilen) nc = first_nw + ilen;
        r = ce_comment_result(first_nw, last_nw, inner, start == end ? nc : first_nw,
                              start == end ? nc : first_nw + ilen, ctx);
        free(inner);
        return r;
    }
    if (tr_len >= 4 && memcmp(text + first_nw, "/*", 2) == 0 && memcmp(text + last_nw - 2, "*/", 2) == 0) {
        size_t pre = (tr_len >= 5 && text[first_nw + 2] == ' ') ? 3 : 2;
        size_t suf = (tr_len >= pre + 3 && text[last_nw - 3] == ' ') ? 3 : 2;
        size_t ilen = tr_len - pre - suf;
        char *inner = (char *)malloc(ilen + 1);
        size_t nc = start > pre ? start - pre : first_nw;
        JsonValue *r;
        memcpy(inner, text + first_nw + pre, ilen);
        inner[ilen] = '\0';
        if (nc < first_nw) nc = first_nw;
        if (nc > first_nw + ilen) nc = first_nw + ilen;
        r = ce_comment_result(first_nw, last_nw, inner, start == end ? nc : first_nw,
                              start == end ? nc : first_nw + ilen, ctx);
        free(inner);
        return r;
    }
    if (is_line_ctx && tr_len >= 2 && memcmp(text + first_nw, "//", 2) == 0) {
        size_t pre = (tr_len >= 3 && text[first_nw + 2] == ' ') ? 3 : 2;
        size_t ilen = tr_len - pre;
        char *inner = (char *)malloc(ilen + 1);
        size_t nc = start > pre ? start - pre : first_nw;
        JsonValue *r;
        memcpy(inner, text + first_nw + pre, ilen);
        inner[ilen] = '\0';
        if (nc < first_nw) nc = first_nw;
        if (nc > first_nw + ilen) nc = first_nw + ilen;
        r = ce_comment_result(first_nw, last_nw, inner, start == end ? nc : first_nw,
                              start == end ? nc : first_nw + ilen, ctx);
        free(inner);
        return r;
    }
    if (is_line_ctx && start == end) {
        size_t nc = start < first_nw ? first_nw + 3 : start + 3;
        return ce_comment_result(first_nw, first_nw, "// ", nc, nc, ctx);
    }
    {
        const char *open_d = (strcmp(ctx, "html") == 0) ? "<!--" : "/*";
        const char *close_d = (strcmp(ctx, "html") == 0) ? "-->" : "*/";
        CeBuf w;
        char *s;
        size_t shift = strlen(open_d) + 1;
        size_t base_c = start < first_nw ? first_nw : (start > last_nw ? last_nw : start);
        JsonValue *r;
        cebuf_init(&w);
        cebuf_puts(&w, open_d);
        cebuf_putc(&w, ' ');
        cebuf_append(&w, text + first_nw, tr_len);
        cebuf_putc(&w, ' ');
        cebuf_puts(&w, close_d);
        s = cebuf_steal(&w);
        r = ce_comment_result(first_nw, last_nw, s,
                              start == end ? base_c + shift : first_nw,
                              start == end ? base_c + shift : first_nw + strlen(s), ctx);
        free(s);
        return r;
    }
}

static const char *ce_jstr(const JsonValue *obj, const char *key, const char *def) {
    JsonValue *v;
    const char *s;
    if (!obj || json_type(obj) != JSON_OBJECT) return def;
    v = json_object_get(obj, key);
    if (!v || json_type(v) != JSON_STRING) return def;
    s = json_string_get(v, NULL);
    return s ? s : def;
}

static int ce_jbool(const JsonValue *obj, const char *key, int def) {
    JsonValue *v;
    if (!obj || json_type(obj) != JSON_OBJECT) return def;
    v = json_object_get(obj, key);
    if (!v || json_type(v) != JSON_BOOL) return def;
    return json_bool_value(v) ? 1 : 0;
}

static int ce_locate_macro_block(const JsonValue *macro, const char *source,
                                 size_t *out_start, size_t *out_end) {
    const char *sm = ce_jstr(macro, "startMarker", "");
    const char *em = ce_jstr(macro, "endMarker", "");
    const char *found_s;
    if (!source || !source[0] || !sm[0]) return 0;
    found_s = strstr(source, sm);
    if (!found_s) return 0;
    *out_start = (size_t)(found_s - source);
    if (em[0]) {
        const char *found_e = strstr(found_s + strlen(sm), em);
        if (!found_e) return 0;
        *out_end = (size_t)(found_e - source) + strlen(em);
        return 1;
    }
    {
        const char *nl = strchr(found_s, '\n');
        *out_end = nl ? (size_t)(nl - source) : strlen(source);
        return 1;
    }
}

static char *ce_render_macro_template(const JsonValue *macro, const JsonValue *values) {
    const char *tpl = ce_jstr(macro, "template", "");
    JsonValue *fields = json_object_get(macro, "fields");
    CeBuf out;
    const char *p = tpl;
    cebuf_init(&out);
    while (*p) {
        if (p[0] == '{' && p[1] == '{') {
            const char *close = strstr(p + 2, "}}");
            if (close) {
                const char *ks = p + 2, *ke = close;
                char key[64];
                size_t klen, i;
                const char *val = NULL;
                while (ks < ke && ce_is_space(*ks)) ks++;
                while (ke > ks && ce_is_space(*(ke - 1))) ke--;
                klen = (size_t)(ke - ks);
                if (klen < sizeof(key)) {
                    memcpy(key, ks, klen);
                    key[klen] = '\0';
                    val = ce_jstr(values, key, NULL);
                    if (!val && fields && json_type(fields) == JSON_ARRAY) {
                        for (i = 0; i < json_array_len(fields); i++) {
                            JsonValue *f = json_array_at(fields, i);
                            if (strcmp(ce_jstr(f, "key", ""), key) == 0) {
                                val = ce_jstr(f, "defaultValue", "");
                                break;
                            }
                        }
                    }
                    cebuf_puts(&out, val ? val : "");
                    p = close + 2;
                    continue;
                }
            }
        }
        cebuf_putc(&out, *p++);
    }
    return cebuf_steal(&out);
}

JsonValue *code_engine_evaluate_macros(const JsonValue *macros_array, const char *doc_path,
                                       const char *doc_section, int is_media,
                                       const char *source) {
    JsonValue *root = json_object();
    JsonValue *evals = json_array();
    JsonValue *issues = json_array();
    size_t i, n = (macros_array && json_type(macros_array) == JSON_ARRAY) ? json_array_len(macros_array) : 0;
    (void)doc_section;
    for (i = 0; i < n && !is_media && doc_path && doc_path[0]; i++) {
        JsonValue *m = json_array_at(macros_array, i);
        int enabled = ce_jbool(m, "enabled", 1);
        size_t b_start = 0, b_end = 0;
        int present;
        JsonValue *ev, *vals, *fields;
        char *rendered;
        if (!enabled) continue;
        present = ce_locate_macro_block(m, source ? source : "", &b_start, &b_end);
        vals = json_object();
        fields = json_object_get(m, "fields");
        if (fields && json_type(fields) == JSON_ARRAY) {
            size_t fk;
            for (fk = 0; fk < json_array_len(fields); fk++) {
                JsonValue *f = json_array_at(fields, fk);
                json_object_put(vals, ce_jstr(f, "key", "field"),
                                json_string_cstr(ce_jstr(f, "defaultValue", "")));
            }
        }
        rendered = ce_render_macro_template(m, vals);
        ev = json_object();
        json_object_put(ev, "macro", json_clone(m));
        json_object_put(ev, "applies", json_bool(1));
        json_object_put(ev, "present", json_bool(present));
        if (present) {
            JsonValue *rng = json_object();
            json_object_put(rng, "startOffset", json_number_int((long long)b_start));
            json_object_put(rng, "endOffset", json_number_int((long long)b_end));
            json_object_put(ev, "range", rng);
        } else {
            json_object_put(ev, "range", json_null());
        }
        json_object_put(ev, "values", vals);
        json_object_put(ev, "rendered", json_string_cstr(rendered));
        json_object_put(ev, "statusLabel", json_string_cstr(present ? "Active" : "Not in page"));
        json_object_put(ev, "statusTone", json_string_cstr(present ? "ok" : ""));
        json_object_put(ev, "issues", json_array());
        json_array_add(evals, ev);
        free(rendered);
    }
    json_object_put(root, "evaluations", evals);
    json_object_put(root, "issues", issues);
    return root;
}

JsonValue *code_engine_apply_macro(const JsonValue *macro_def, const char *source,
                                   const JsonValue *input_values, const char *mode,
                                   int cursor_offset) {
    const char *text = source ? source : "";
    size_t len = strlen(text);
    const char *title = ce_jstr(macro_def, "title", "Custom Macro");
    const char *m_mode = (mode && mode[0]) ? mode : "apply";
    size_t b_start = 0, b_end = 0;
    int existing = ce_locate_macro_block(macro_def, text, &b_start, &b_end);
    JsonValue *root = json_object();
    char label[256];

    if (strcmp(m_mode, "remove") == 0) {
        CeBuf nb;
        char *s;
        snprintf(label, sizeof(label), "Removed %s", title);
        if (!existing) {
            json_object_put(root, "content", json_string_cstr(text));
            json_object_put(root, "range", json_null());
            json_object_put(root, "label", json_string_cstr(label));
            return root;
        }
        if (b_end < len && text[b_end] == '\n') b_end++;
        cebuf_init(&nb);
        cebuf_append(&nb, text, b_start);
        cebuf_puts(&nb, text + b_end);
        s = cebuf_steal(&nb);
        json_object_put(root, "content", json_string_cstr(s));
        json_object_put(root, "range", json_null());
        json_object_put(root, "label", json_string_cstr(label));
        free(s);
        return root;
    }
    {
        char *rendered = ce_render_macro_template(macro_def, input_values);
        size_t rlen = strlen(rendered);
        CeBuf nb;
        char *s;
        JsonValue *rng = json_object();
        size_t s_off = 0;
        while (rlen > 0 && ce_is_space(rendered[rlen - 1])) rendered[--rlen] = '\0';
        cebuf_init(&nb);
        if (existing && strcmp(m_mode, "insert-cursor") != 0) {
            cebuf_append(&nb, text, b_start);
            cebuf_append(&nb, rendered, rlen);
            cebuf_puts(&nb, text + b_end);
            s_off = b_start;
            snprintf(label, sizeof(label), "Updated %s", title);
        } else {
            size_t ins_at = (cursor_offset >= 0 && (size_t)cursor_offset <= len) ? (size_t)cursor_offset : len;
            cebuf_append(&nb, text, ins_at);
            if (ins_at > 0 && text[ins_at - 1] != '\n') { cebuf_putc(&nb, '\n'); ins_at++; }
            s_off = nb.len;
            cebuf_append(&nb, rendered, rlen);
            cebuf_putc(&nb, '\n');
            if ((size_t)cursor_offset <= len && cursor_offset >= 0) cebuf_puts(&nb, text + cursor_offset);
            snprintf(label, sizeof(label), "Inserted %s", title);
        }
        s = cebuf_steal(&nb);
        json_object_put(rng, "startOffset", json_number_int((long long)s_off));
        json_object_put(rng, "endOffset", json_number_int((long long)(s_off + rlen)));
        json_object_put(root, "content", json_string_cstr(s));
        json_object_put(root, "range", rng);
        json_object_put(root, "label", json_string_cstr(label));
        free(s);
        free(rendered);
        return root;
    }
}










