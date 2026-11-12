#include "wasm_runtime.h"

typedef struct WHlCtx {
    const char *text;
    u32 len;
    u32 i;
    WBuf out;
} WHlCtx;

static int whl_ends_ci(const char *s, u32 slen, const char *ext) {
    u32 elen = w_strlen(ext);
    return slen >= elen && w_strncasecmp(s + slen - elen, ext, elen) == 0;
}

__attribute__((export_name("wasm_detect_language")))
u32 wasm_detect_language(const char *path, u32 path_len) {
    const char *res = "text";
    if (path && path_len > 0) {
        if (whl_ends_ci(path, path_len, ".php")) res = "php";
        else if (whl_ends_ci(path, path_len, ".html") || whl_ends_ci(path, path_len, ".htm") ||
                 whl_ends_ci(path, path_len, ".shtml")) res = "html";
        else if (whl_ends_ci(path, path_len, ".css")) res = "css";
        else if (whl_ends_ci(path, path_len, ".js") || whl_ends_ci(path, path_len, ".mjs")) res = "js";
        else if (whl_ends_ci(path, path_len, ".json")) res = "json";
    }
    wasm_set_result(res, w_strlen(res));
    return w_strlen(res);
}

static void whl_emit(WHlCtx *ctx, const char *cls, u32 start, u32 end) {
    if (end <= start || start >= ctx->len) return;
    if (end > ctx->len) end = ctx->len;
    if (!cls || !cls[0]) {
        wbuf_append_html_esc(&ctx->out, ctx->text + start, end - start);
    } else {
        wbuf_puts(&ctx->out, "<span class=\"");
        wbuf_puts(&ctx->out, cls);
        wbuf_puts(&ctx->out, "\">");
        wbuf_append_html_esc(&ctx->out, ctx->text + start, end - start);
        wbuf_puts(&ctx->out, "</span>");
    }
}

static void whl_emit_space(WHlCtx *ctx, u32 end) {
    u32 s = ctx->i;
    while (ctx->i < end && w_is_space(ctx->text[ctx->i])) ctx->i++;
    whl_emit(ctx, "", s, ctx->i);
}

static void whl_emit_gap(WHlCtx *ctx, u32 end) {
    if (ctx->i < end && w_is_space(ctx->text[ctx->i])) whl_emit_space(ctx, end);
}

static i32 whl_entity_end(const char *text, u32 i, u32 end) {
    u32 j, s;
    if (i + 2u >= end || text[i] != '&') return -1;
    j = i + 1u;
    if (text[j] == '#') {
        j++;
        if (j < end && (text[j] == 'x' || text[j] == 'X')) {
            j++;
            s = j;
            while (j < end && w_is_hex(text[j])) j++;
            if (j == s || j >= end || text[j] != ';') return -1;
            return (i32)(j + 1u);
        }
        s = j;
        while (j < end && w_is_digit(text[j])) j++;
        if (j == s || j >= end || text[j] != ';') return -1;
        return (i32)(j + 1u);
    }
    s = j;
    if (j >= end || !w_is_ident_start(text[j])) return -1;
    j++;
    while (j < end && w_is_ident_char(text[j])) j++;
    if (j == s || j >= end || text[j] != ';') return -1;
    return (i32)(j + 1u);
}

static u32 whl_find_end_tag(const char *text, u32 from, u32 end, const char *name) {
    u32 nlen = w_strlen(name);
    u32 i;
    for (i = from; i + 2u + nlen <= end; i++) {
        u32 j, k;
        if (text[i] != '<' || text[i + 1u] != '/') continue;
        j = i + 2u;
        for (k = 0; k < nlen && j < end; k++, j++) {
            if (w_fold(text[j]) != w_fold(name[k])) break;
        }
        if (k != nlen) continue;
        if (j < end && w_is_name_char(text[j])) continue;
        return i;
    }
    return end;
}

static u32 whl_php_open_len(const char *text, u32 i, u32 end) {
    if (i + 2u >= end || text[i] != '<' || text[i + 1u] != '?') return 0;
    if (text[i + 2u] == '=') return 3u;
    if (i + 5u <= end && w_strncasecmp(text + i, "<?php", 5u) == 0) {
        if (i + 5u == end || !w_is_ident_char(text[i + 5u])) return 5u;
    }
    return 0;
}

static int whl_is_php_word(const char *w, u32 len) {
    static const char *const words[] = {
        "function", "return", "if", "else", "elseif", "foreach", "for", "while",
        "require_once", "include", "include_once", "require", "echo", "isset",
        "true", "false", "null", "array", "as", "new"
    };
    u32 i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (w_strlen(words[i]) == len && w_strncasecmp(w, words[i], len) == 0) return 1;
    }
    return 0;
}

static int whl_is_js_word(const char *w, u32 len) {
    static const char *const words[] = {
        "var", "let", "const", "function", "window", "document", "return",
        "if", "else", "for", "while", "new", "this", "true", "false", "null",
        "undefined", "typeof", "in", "of", "switch", "case", "break", "continue",
        "try", "catch", "throw", "class", "async", "await", "do"
    };
    u32 i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (w_strlen(words[i]) == len && w_memcmp(w, words[i], len) == 0) return 1;
    }
    return 0;
}

static int whl_is_grouping_at(const char *w, u32 len) {
    static const char *const words[] = {
        "media", "supports", "container", "layer", "keyframes", "document", "scope"
    };
    u32 i;
    for (i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
        if (w_strlen(words[i]) == len && w_strncasecmp(w, words[i], len) == 0) return 1;
    }
    return 0;
}

static int whl_scan_multi_op(WHlCtx *ctx, u32 end) {
    static const char *const ops[] = {
        "===", "!==", ">>=", "<<=", "=>", "->", "::", "&&", "||", "++", "--",
        "+=", "-=", "*=", "/=", ".=", "==", "!=", "<=", ">=", "??", "?.", "<<", ">>"
    };
    u32 k;
    for (k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
        u32 olen = w_strlen(ops[k]);
        if (ctx->i + olen <= end && w_memcmp(ctx->text + ctx->i, ops[k], olen) == 0) {
            whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + olen);
            ctx->i += olen;
            return 1;
        }
    }
    return 0;
}

static void whl_scan_line_comment(WHlCtx *ctx, u32 end) {
    u32 start = ctx->i;
    while (ctx->i < end && ctx->text[ctx->i] != '\n') ctx->i++;
    if (ctx->i < end && ctx->text[ctx->i] == '\n') ctx->i++;
    whl_emit(ctx, "tok-comment", start, ctx->i);
}

static void whl_scan_block_comment(WHlCtx *ctx, u32 end) {
    i32 at = w_index_of(ctx->text, ctx->len, "*/", ctx->i + 2u, end);
    u32 stop = (at < 0) ? end : (u32)at + 2u;
    whl_emit(ctx, "tok-comment", ctx->i, stop);
    ctx->i = stop;
}

static void whl_scan_simple_string(WHlCtx *ctx, u32 end, char quote) {
    u32 start = ctx->i++;
    while (ctx->i < end) {
        if (ctx->text[ctx->i] == '\\') {
            ctx->i = (ctx->i + 2u <= end) ? ctx->i + 2u : end;
            continue;
        }
        if (ctx->text[ctx->i] == quote) {
            ctx->i++;
            break;
        }
        if (quote != '`' && ctx->text[ctx->i] == '\n') break;
        ctx->i++;
    }
    whl_emit(ctx, "tok-string", start, ctx->i);
}

static int whl_scan_plain_number(WHlCtx *ctx, u32 end, int allow_sign) {
    u32 i = ctx->i, before;
    if (allow_sign && i < end && (ctx->text[i] == '-' || ctx->text[i] == '+')) {
        if (i + 1u >= end || !w_is_digit(ctx->text[i + 1u])) return 0;
        i++;
    }
    if (i + 1u < end && ctx->text[i] == '0' && (ctx->text[i + 1u] == 'x' || ctx->text[i + 1u] == 'X')) {
        u32 hex = i + 2u;
        i += 2u;
        while (i < end && w_is_hex(ctx->text[i])) i++;
        if (i == hex) return 0;
        whl_emit(ctx, "tok-number", ctx->i, i);
        ctx->i = i;
        return 1;
    }
    before = i;
    if (i >= end || !w_is_digit(ctx->text[i])) return 0;
    while (i < end && w_is_digit(ctx->text[i])) i++;
    if (i + 1u < end && ctx->text[i] == '.' && w_is_digit(ctx->text[i + 1u])) {
        i += 2u;
        while (i < end && w_is_digit(ctx->text[i])) i++;
    }
    if (i == before) return 0;
    whl_emit(ctx, "tok-number", ctx->i, i);
    ctx->i = i;
    return 1;
}

static int whl_scan_variable(WHlCtx *ctx, u32 end) {
    u32 i = ctx->i + 1u;
    if (ctx->i >= end || ctx->text[ctx->i] != '$') return 0;
    if (i < end && ctx->text[i] == '$') i++;
    if (i >= end || !w_is_ident_start(ctx->text[i])) return 0;
    i++;
    while (i < end && w_is_ident_char(ctx->text[i])) i++;
    whl_emit(ctx, "tok-variable", ctx->i, i);
    ctx->i = i;
    return 1;
}

static void whl_scan_php_single(WHlCtx *ctx, u32 end) {
    u32 start = ctx->i++;
    while (ctx->i < end) {
        if (ctx->text[ctx->i] == '\\') {
            ctx->i = (ctx->i + 2u <= end) ? ctx->i + 2u : end;
            continue;
        }
        if (ctx->text[ctx->i] == '\'') {
            ctx->i++;
            break;
        }
        ctx->i++;
    }
    whl_emit(ctx, "tok-string", start, ctx->i);
}

static void whl_scan_php_double(WHlCtx *ctx, u32 end) {
    u32 seg = ctx->i++;
    while (ctx->i < end) {
        char c = ctx->text[ctx->i];
        if (c == '\\') {
            ctx->i = (ctx->i + 2u <= end) ? ctx->i + 2u : end;
            continue;
        }
        if (c == '"') {
            ctx->i++;
            break;
        }
        if (c == '$' && ctx->i + 1u < end &&
            (w_is_ident_start(ctx->text[ctx->i + 1u]) || ctx->text[ctx->i + 1u] == '{')) {
            whl_emit(ctx, "tok-string", seg, ctx->i);
            if (ctx->text[ctx->i + 1u] == '{') {
                u32 brace = ctx->i;
                ctx->i += 2u;
                while (ctx->i < end && ctx->text[ctx->i] != '}') ctx->i++;
                if (ctx->i < end && ctx->text[ctx->i] == '}') ctx->i++;
                whl_emit(ctx, "tok-variable", brace, ctx->i);
            } else {
                whl_scan_variable(ctx, end);
            }
            seg = ctx->i;
            continue;
        }
        ctx->i++;
    }
    whl_emit(ctx, "tok-string", seg, ctx->i);
}

static void whl_scan_markup(WHlCtx *ctx, u32 end);

static int whl_scan_heredoc(WHlCtx *ctx, u32 end) {
    u32 start = ctx->i, p = start + 3u, id_s, id_e, nl;
    char quote = '\0';
    if (p >= end) return 0;
    while (p < end && (ctx->text[p] == ' ' || ctx->text[p] == '\t')) p++;
    if (p < end && (ctx->text[p] == '\'' || ctx->text[p] == '"')) quote = ctx->text[p++];
    if (p >= end || !w_is_ident_start(ctx->text[p])) return 0;
    id_s = p++;
    while (p < end && w_is_ident_char(ctx->text[p])) p++;
    id_e = p;
    if (quote) {
        if (p >= end || ctx->text[p] != quote) return 0;
        p++;
    }
    nl = p;
    if (nl < end && ctx->text[nl] == '\r') nl++;
    if (nl >= end || ctx->text[nl] != '\n') return 0;
    whl_emit(ctx, "tok-keyword", start, p);
    whl_emit(ctx, "", p, nl + 1u);
    ctx->i = nl + 1u;
    {
        u32 id_len = id_e - id_s, line_pos = ctx->i;
        while (line_pos < end) {
            u32 j = line_pos;
            while (j < end && (ctx->text[j] == ' ' || ctx->text[j] == '\t')) j++;
            if (j + id_len <= end && w_memcmp(ctx->text + j, ctx->text + id_s, id_len) == 0 &&
                (j + id_len == end || !w_is_ident_char(ctx->text[j + id_len]))) {
                u32 k = j + id_len, tail;
                if (k < end && ctx->text[k] == ';') k++;
                tail = k;
                while (tail < end && (ctx->text[tail] == ' ' || ctx->text[tail] == '\t')) tail++;
                if (tail >= end || ctx->text[tail] == '\n' || ctx->text[tail] == '\r') {
                    whl_scan_markup(ctx, j);
                    whl_emit(ctx, "tok-keyword", j, k);
                    ctx->i = k;
                    return 1;
                }
            }
            while (line_pos < end && ctx->text[line_pos] != '\n') line_pos++;
            if (line_pos < end) line_pos++;
        }
    }
    whl_scan_markup(ctx, end);
    return 1;
}

static void whl_scan_php(WHlCtx *ctx, u32 end) {
    while (ctx->i < end) {
        u32 i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '/') { whl_scan_line_comment(ctx, end); continue; }
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '*') { whl_scan_block_comment(ctx, end); continue; }
        if (c == '#' && (i + 1u >= end || ctx->text[i + 1u] != '[')) { whl_scan_line_comment(ctx, end); continue; }
        if (c == '\'') { whl_scan_php_single(ctx, end); continue; }
        if (c == '"') { whl_scan_php_double(ctx, end); continue; }
        if (c == '<' && i + 2u < end && ctx->text[i + 1u] == '<' && ctx->text[i + 2u] == '<' &&
            whl_scan_heredoc(ctx, end)) continue;
        if (c == '?' && i + 1u < end && ctx->text[i + 1u] == '>') {
            whl_emit(ctx, "tok-php-tag", i, i + 2u);
            ctx->i = i + 2u;
            return;
        }
        if (c == '$' && whl_scan_variable(ctx, end)) continue;
        if (w_is_ident_start(c)) {
            u32 we = i + 1u;
            while (we < end && w_is_ident_char(ctx->text[we])) we++;
            whl_emit(ctx, whl_is_php_word(ctx->text + i, we - i) ? "tok-keyword" : "", i, we);
            ctx->i = we;
            continue;
        }
        if (w_is_digit(c) || (c == '.' && i + 1u < end && w_is_digit(ctx->text[i + 1u]))) {
            if (whl_scan_plain_number(ctx, end, 0)) continue;
        }
        if (w_is_space(c)) { whl_emit_space(ctx, end); continue; }
        if (whl_scan_multi_op(ctx, end)) continue;
        whl_emit(ctx, "tok-punctuation", i, i + 1u);
        ctx->i = i + 1u;
    }
}

static void whl_scan_js(WHlCtx *ctx, u32 end) {
    while (ctx->i < end) {
        u32 i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '/') { whl_scan_line_comment(ctx, end); continue; }
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '*') { whl_scan_block_comment(ctx, end); continue; }
        if (c == '"' || c == '\'' || c == '`') { whl_scan_simple_string(ctx, end, c); continue; }
        if (w_is_space(c)) { whl_emit_space(ctx, end); continue; }
        if (w_is_ident_start(c)) {
            u32 we = i + 1u;
            while (we < end && w_is_ident_char(ctx->text[we])) we++;
            whl_emit(ctx, whl_is_js_word(ctx->text + i, we - i) ? "tok-keyword" : "", i, we);
            ctx->i = we;
            continue;
        }
        if (w_is_digit(c) || (c == '.' && i + 1u < end && w_is_digit(ctx->text[i + 1u]))) {
            if (whl_scan_plain_number(ctx, end, 0)) continue;
        }
        if (whl_scan_multi_op(ctx, end)) continue;
        whl_emit(ctx, "tok-punctuation", i, i + 1u);
        ctx->i = i + 1u;
    }
}

static void whl_scan_json(WHlCtx *ctx, u32 end) {
    while (ctx->i < end) {
        u32 i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '/') { whl_scan_line_comment(ctx, end); continue; }
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '*') { whl_scan_block_comment(ctx, end); continue; }
        if (w_is_space(c)) { whl_emit_space(ctx, end); continue; }
        if (c == '"') {
            u32 k = i + 1u, after;
            while (k < end) {
                if (ctx->text[k] == '\\') { k = (k + 2u <= end) ? k + 2u : end; continue; }
                if (ctx->text[k] == '"') { k++; break; }
                k++;
            }
            after = k;
            while (after < end && w_is_space(ctx->text[after])) after++;
            whl_emit(ctx, (after < end && ctx->text[after] == ':') ? "tok-attr" : "tok-string", i, k);
            ctx->i = k;
            continue;
        }
        if (c == '-' || w_is_digit(c)) {
            if (whl_scan_plain_number(ctx, end, 1)) continue;
        }
        if (w_is_ident_start(c)) {
            u32 we = i + 1u;
            int is_kw = 0;
            while (we < end && w_is_ident_char(ctx->text[we])) we++;
            if ((we - i == 4u && (w_memcmp(ctx->text + i, "true", 4u) == 0 || w_memcmp(ctx->text + i, "null", 4u) == 0)) ||
                (we - i == 5u && w_memcmp(ctx->text + i, "false", 5u) == 0)) {
                is_kw = 1;
            }
            whl_emit(ctx, is_kw ? "tok-keyword" : "", i, we);
            ctx->i = we;
            continue;
        }
        whl_emit(ctx, "tok-punctuation", i, i + 1u);
        ctx->i = i + 1u;
    }
}

static int whl_scan_css_hex(WHlCtx *ctx, u32 end) {
    u32 i = ctx->i + 1u, digits = i, len;
    if (ctx->i >= end || ctx->text[ctx->i] != '#') return 0;
    while (i < end && w_is_hex(ctx->text[i])) i++;
    len = i - digits;
    if (len != 3u && len != 4u && len != 6u && len != 8u) return 0;
    if (i < end && w_is_css_ident_char(ctx->text[i])) return 0;
    whl_emit(ctx, "tok-number", ctx->i, i);
    ctx->i = i;
    return 1;
}

static int whl_scan_css_number(WHlCtx *ctx, u32 end) {
    u32 i = ctx->i, before;
    if (i < end && (ctx->text[i] == '-' || ctx->text[i] == '+')) {
        char next = (i + 1u < end) ? ctx->text[i + 1u] : '\0';
        char next2 = (i + 2u < end) ? ctx->text[i + 2u] : '\0';
        if (!(w_is_digit(next) || (next == '.' && w_is_digit(next2)))) return 0;
        i++;
    }
    before = i;
    if (i + 1u < end && ctx->text[i] == '.' && w_is_digit(ctx->text[i + 1u])) {
        i += 2u;
        while (i < end && w_is_digit(ctx->text[i])) i++;
    } else if (i < end && w_is_digit(ctx->text[i])) {
        while (i < end && w_is_digit(ctx->text[i])) i++;
        if (i + 1u < end && ctx->text[i] == '.' && w_is_digit(ctx->text[i + 1u])) {
            i += 2u;
            while (i < end && w_is_digit(ctx->text[i])) i++;
        }
    }
    if (i == before) return 0;
    if (i < end && ctx->text[i] == '%') i++;
    else if (i < end && w_is_ident_start(ctx->text[i])) {
        while (i < end && w_is_ident_char(ctx->text[i])) i++;
    }
    whl_emit(ctx, "tok-number", ctx->i, i);
    ctx->i = i;
    return 1;
}

static int whl_scan_css_ident(WHlCtx *ctx, u32 end) {
    u32 start = ctx->i, i = start;
    if (i + 1u < end && ctx->text[i] == '-' && ctx->text[i + 1u] == '-') i += 2u;
    else if (i < end && ctx->text[i] == '-') i++;
    if (i >= end || !w_is_css_ident_start(ctx->text[i])) return 0;
    while (i < end && w_is_css_ident_char(ctx->text[i])) i++;
    whl_emit(ctx, (i - start == 9u && w_strncasecmp(ctx->text + start, "important", 9u) == 0) ? "tok-keyword" : "tok-val",
             start, i);
    ctx->i = i;
    return 1;
}

static void whl_scan_css_expr(WHlCtx *ctx, u32 end) {
    int depth = 0;
    while (ctx->i < end) {
        u32 i = ctx->i;
        char c = ctx->text[i];
        if (c == '/' && i + 1u < end && ctx->text[i + 1u] == '*') { whl_scan_block_comment(ctx, end); continue; }
        if (c == '"' || c == '\'') { whl_scan_simple_string(ctx, end, c); continue; }
        if (w_is_space(c)) { whl_emit_space(ctx, end); continue; }
        if ((c == '{' || c == ';' || c == '}') && depth == 0) return;
        if (c == '(') { depth++; whl_emit(ctx, "tok-punctuation", i, i + 1u); ctx->i = i + 1u; continue; }
        if (c == ')') { if (depth > 0) depth--; whl_emit(ctx, "tok-punctuation", i, i + 1u); ctx->i = i + 1u; continue; }
        if (c == '#' && whl_scan_css_hex(ctx, end)) continue;
        if (whl_scan_css_number(ctx, end)) continue;
        if ((w_is_css_ident_start(c) || c == '-') && whl_scan_css_ident(ctx, end)) continue;
        whl_emit(ctx, "tok-punctuation", i, i + 1u);
        ctx->i = i + 1u;
    }
}

static void whl_scan_css(WHlCtx *ctx, u32 end) {
    int stack[64];
    int sp = 0, pending_grouping = 0;
    stack[0] = 0;
    while (ctx->i < end) {
        char c = ctx->text[ctx->i];
        int mode;
        if (c == '/' && ctx->i + 1u < end && ctx->text[ctx->i + 1u] == '*') { whl_scan_block_comment(ctx, end); continue; }
        if (w_is_space(c)) { whl_emit_space(ctx, end); continue; }
        if (ctx->i >= end) break;
        mode = stack[sp];
        c = ctx->text[ctx->i];
        if (c == '{') {
            whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
            ctx->i++;
            if (sp + 1 < 64) stack[++sp] = (mode == 0 && pending_grouping) ? 0 : 1;
            pending_grouping = 0;
            continue;
        }
        if (c == '}') {
            whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
            ctx->i++;
            if (sp > 0) sp--;
            pending_grouping = 0;
            continue;
        }
        if (c == ';') {
            whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
            ctx->i++;
            pending_grouping = 0;
            continue;
        }
        if (mode == 0 && c == '@') {
            u32 start = ctx->i, i = start + 1u;
            while (i < end && w_is_css_ident_char(ctx->text[i])) i++;
            pending_grouping = whl_is_grouping_at(ctx->text + start + 1u, i - (start + 1u));
            whl_emit(ctx, "tok-keyword", start, i);
            ctx->i = i;
            whl_scan_css_expr(ctx, end);
            continue;
        }
        if (mode == 0) {
            u32 start = ctx->i, i = start;
            while (i < end) {
                char sc = ctx->text[i];
                if (sc == '{' || sc == '}' || sc == '@' || sc == ';') break;
                if (sc == '/' && i + 1u < end && ctx->text[i + 1u] == '*') break;
                i++;
            }
            if (i == start) i = start + 1u;
            whl_emit(ctx, "tok-selector", start, i);
            ctx->i = i;
            continue;
        }
        {
            u32 start = ctx->i, i = start;
            if (i + 1u < end && ctx->text[i] == '-' && ctx->text[i + 1u] == '-') i += 2u;
            else if (i < end && ctx->text[i] == '-') i++;
            if (i < end && w_is_css_ident_start(ctx->text[i])) {
                while (i < end && w_is_css_ident_char(ctx->text[i])) i++;
                whl_emit(ctx, "tok-prop", start, i);
                ctx->i = i;
                whl_emit_gap(ctx, end);
                if (ctx->i < end && ctx->text[ctx->i] == ':') {
                    whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
                    ctx->i++;
                    whl_scan_css_expr(ctx, end);
                }
            } else {
                whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
                ctx->i++;
            }
        }
    }
}

static void whl_scan_html_quoted(WHlCtx *ctx, u32 end, char quote, char *val_out, u32 val_sz) {
    u32 open = ctx->i, seg, vlen = 0;
    if (val_out && val_sz) val_out[0] = '\0';
    if (open >= end || ctx->text[open] != quote) return;
    ctx->i = open + 1u;
    seg = open;
    while (ctx->i < end) {
        if (ctx->text[ctx->i] == quote) {
            ctx->i++;
            whl_emit(ctx, "tok-string", seg, ctx->i);
            return;
        }
        if (ctx->text[ctx->i] == '&') {
            i32 ent = whl_entity_end(ctx->text, ctx->i, end);
            if (ent > (i32)ctx->i) {
                whl_emit(ctx, "tok-string", seg, ctx->i);
                whl_emit(ctx, "tok-entity", ctx->i, (u32)ent);
                ctx->i = (u32)ent;
                seg = ctx->i;
                continue;
            }
        }
        if (val_out && vlen + 1u < val_sz) {
            val_out[vlen++] = ctx->text[ctx->i];
            val_out[vlen] = '\0';
        }
        ctx->i++;
    }
    whl_emit(ctx, "tok-string", seg, ctx->i);
}

static void whl_scan_unquoted(WHlCtx *ctx, u32 end, char *val_out, u32 val_sz) {
    u32 start = ctx->i, vlen = 0;
    if (val_out && val_sz) val_out[0] = '\0';
    while (ctx->i < end) {
        char c = ctx->text[ctx->i];
        if (w_is_space(c) || c == '>' || c == '"' || c == '\'') break;
        if (c == '/' && ctx->i + 1u < end && ctx->text[ctx->i + 1u] == '>') break;
        if (val_out && vlen + 1u < val_sz) {
            val_out[vlen++] = c;
            val_out[vlen] = '\0';
        }
        ctx->i++;
    }
    whl_emit(ctx, "tok-string", start, ctx->i);
}

static void whl_scan_ssi(WHlCtx *ctx, u32 end) {
    u32 start = ctx->i;
    i32 close = w_index_of(ctx->text, ctx->len, "-->", start + 4u, end);
    u32 limit = (close < 0) ? end : (u32)close;
    u32 p = start + 4u;
    while (p < limit && w_is_space(ctx->text[p])) p++;
    if (p >= limit || ctx->text[p] != '#') {
        u32 stop = (close < 0) ? end : (u32)close + 3u;
        whl_emit(ctx, "tok-comment", start, stop);
        ctx->i = stop;
        return;
    }
    p++;
    while (p < limit && w_is_space(ctx->text[p])) p++;
    if (p >= limit || !((ctx->text[p] >= 'A' && ctx->text[p] <= 'Z') || (ctx->text[p] >= 'a' && ctx->text[p] <= 'z'))) {
        u32 stop = (close < 0) ? end : (u32)close + 3u;
        whl_emit(ctx, "tok-comment", start, stop);
        ctx->i = stop;
        return;
    }
    while (p < limit && ((ctx->text[p] >= 'A' && ctx->text[p] <= 'Z') || (ctx->text[p] >= 'a' && ctx->text[p] <= 'z'))) p++;
    whl_emit(ctx, "tok-ssi", start, p);
    ctx->i = p;
    while (ctx->i < limit) {
        u32 i = ctx->i;
        char c = ctx->text[i];
        if (w_is_space(c)) { whl_emit_space(ctx, limit); continue; }
        if (c == '"' || c == '\'') { whl_scan_html_quoted(ctx, limit, c, 0, 0); continue; }
        if (w_is_attr_start(c)) {
            ctx->i = i + 1u;
            while (ctx->i < limit && w_is_attr_char(ctx->text[ctx->i])) ctx->i++;
            whl_emit(ctx, "tok-attr", i, ctx->i);
            whl_emit_gap(ctx, limit);
            if (ctx->i < limit && ctx->text[ctx->i] == '=') {
                whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
                ctx->i++;
                whl_emit_gap(ctx, limit);
                if (ctx->i < limit && (ctx->text[ctx->i] == '"' || ctx->text[ctx->i] == '\'')) {
                    whl_scan_html_quoted(ctx, limit, ctx->text[ctx->i], 0, 0);
                } else if (ctx->i < limit && ctx->text[ctx->i] != '>') {
                    whl_scan_unquoted(ctx, limit, 0, 0);
                }
            }
            continue;
        }
        whl_emit(ctx, "tok-ssi", i, i + 1u);
        ctx->i = i + 1u;
    }
    if (close >= 0) {
        whl_emit(ctx, "tok-ssi", (u32)close, (u32)close + 3u);
        ctx->i = (u32)close + 3u;
    }
}

static u32 whl_meta_head_len(const char *text, u32 start, u32 end) {
    static const char *const names[] = {"page-values", "related-sales", "generated-seo"};
    u32 p = start + 4u, k;
    if (start + 4u > end || w_memcmp(text + start, "<!--", 4u) != 0) return 0;
    while (p < end && w_is_space(text[p])) p++;
    if (p < end && text[p] == '/') {
        p++;
        while (p < end && w_is_space(text[p])) p++;
    }
    for (k = 0; k < 3u; k++) {
        u32 nlen = w_strlen(names[k]);
        if (p + nlen <= end && w_strncasecmp(text + p, names[k], nlen) == 0) {
            char next = (p + nlen < end) ? text[p + nlen] : '\0';
            if (!w_is_ident_char(next) && next != '-') return (p + nlen) - start;
        }
    }
    return 0;
}

static void whl_emit_classified(WHlCtx *ctx, u32 start, u32 end, const char *cls) {
    u32 i = start, seg = start;
    while (i < end) {
        if (ctx->text[i] == '&') {
            i32 ent = whl_entity_end(ctx->text, i, end);
            if (ent > (i32)i) {
                whl_emit(ctx, cls, seg, i);
                whl_emit(ctx, "tok-entity", i, (u32)ent);
                i = (u32)ent;
                seg = i;
                continue;
            }
        }
        i++;
    }
    whl_emit(ctx, cls, seg, end);
}

static void whl_scan_meta(WHlCtx *ctx, u32 end, u32 head_len) {
    u32 start = ctx->i;
    i32 close = w_index_of(ctx->text, ctx->len, "-->", start + 4u, end);
    u32 limit = (close < 0) ? end : (u32)close;
    u32 i = (start + head_len < limit) ? start + head_len : limit;
    whl_emit(ctx, "tok-meta-comment", start, i);
    while (i < limit) {
        u32 le = i, body_end, p, indent, key_end;
        int nl = 0;
        while (le < limit && ctx->text[le] != '\n') le++;
        if (le < limit && ctx->text[le] == '\n') { le++; nl = 1; }
        body_end = le - (u32)nl;
        p = i;
        while (p < body_end && (ctx->text[p] == ' ' || ctx->text[p] == '\t')) p++;
        indent = p - i;
        key_end = p;
        if (p + 12u <= body_end && w_strncasecmp(ctx->text + p, "Override IDs", 12u) == 0) {
            key_end = p + 12u;
        } else if (p < body_end && w_is_ident_start(ctx->text[p])) {
            key_end = p + 1u;
            while (key_end < body_end && (w_is_ident_char(ctx->text[key_end]) || ctx->text[key_end] == '-')) key_end++;
        }
        if (key_end > p) {
            u32 cpos = key_end;
            while (cpos < body_end && (ctx->text[cpos] == ' ' || ctx->text[cpos] == '\t')) cpos++;
            if (cpos < body_end && ctx->text[cpos] == ':') {
                u32 klen = (cpos + 1u) - p;
                if (indent) whl_emit(ctx, "tok-meta-comment", i, i + indent);
                whl_emit(ctx, "tok-meta-key", i + indent, i + indent + klen);
                whl_emit_classified(ctx, i + indent + klen, body_end, "tok-meta-val");
                if (nl) whl_emit(ctx, "", le - 1u, le);
                i = le;
                continue;
            }
        }
        whl_emit(ctx, "tok-meta-comment", i, le);
        i = le;
    }
    ctx->i = limit;
    if (close >= 0) {
        whl_emit(ctx, "tok-meta-comment", (u32)close, (u32)close + 3u);
        ctx->i = (u32)close + 3u;
    }
}

static void whl_scan_markup(WHlCtx *ctx, u32 end) {
    while (ctx->i < end) {
        u32 i = ctx->i;
        u32 php_len = whl_php_open_len(ctx->text, i, end);
        if (php_len) {
            whl_emit(ctx, "tok-php-tag", i, i + php_len);
            ctx->i = i + php_len;
            whl_scan_php(ctx, end);
            continue;
        }
        if (i + 4u <= end && w_memcmp(ctx->text + i, "<!--", 4u) == 0) {
            u32 p = i + 4u, mlen;
            while (p < end && p < i + 12u && w_is_space(ctx->text[p])) p++;
            if (p < end && p < i + 12u && ctx->text[p] == '#') {
                whl_scan_ssi(ctx, end);
            } else if ((mlen = whl_meta_head_len(ctx->text, i, (i + 64u < end) ? i + 64u : end)) > 0) {
                whl_scan_meta(ctx, end, mlen);
            } else {
                i32 close = w_index_of(ctx->text, ctx->len, "-->", i + 4u, end);
                u32 stop = (close < 0) ? end : (u32)close + 3u;
                whl_emit(ctx, "tok-comment", i, stop);
                ctx->i = stop;
            }
            continue;
        }
        if (i + 9u <= end && w_strncasecmp(ctx->text + i, "<!doctype", 9u) == 0 &&
            (i + 9u == end || (!w_is_ident_char(ctx->text[i + 9u]) && ctx->text[i + 9u] != '-'))) {
            u32 stop = i + 9u;
            while (stop < end && ctx->text[stop] != '>') stop++;
            if (stop < end) stop++;
            whl_emit(ctx, "tok-doctype", i, stop);
            ctx->i = stop;
            continue;
        }
        if (ctx->text[i] == '<' && i + 1u < end &&
            (ctx->text[i + 1u] == '/' || ctx->text[i + 1u] == '!' || w_is_ident_start(ctx->text[i + 1u]))) {
            u32 ti = i + 1u;
            int closing = 0, self_closing = 0;
            if (ti < end && ctx->text[ti] == '/') { closing = 1; ti++; }
            if (ti < end && w_is_ident_start(ctx->text[ti])) {
                u32 ns = ti++, tlen;
                char tag_name[32], type_val[64];
                type_val[0] = '\0';
                while (ti < end && w_is_name_char(ctx->text[ti])) ti++;
                tlen = ti - ns;
                if (tlen >= sizeof(tag_name)) tlen = sizeof(tag_name) - 1u;
                w_memcpy(tag_name, ctx->text + ns, tlen);
                tag_name[tlen] = '\0';
                whl_emit(ctx, "tok-punctuation", i, ns);
                whl_emit(ctx, "tok-tag", ns, ti);
                ctx->i = ti;
                while (ctx->i < end) {
                    char c = ctx->text[ctx->i];
                    if (c == '>') { whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u); ctx->i++; break; }
                    if (c == '/' && ctx->i + 1u < end && ctx->text[ctx->i + 1u] == '>') {
                        whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 2u);
                        ctx->i += 2u;
                        self_closing = 1;
                        break;
                    }
                    if (w_is_space(c)) { whl_emit_space(ctx, end); continue; }
                    if (!w_is_attr_start(c)) { whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u); ctx->i++; continue; }
                    {
                        u32 as = ctx->i++;
                        int is_type = 0;
                        char val_buf[64];
                        val_buf[0] = '\0';
                        while (ctx->i < end && w_is_attr_char(ctx->text[ctx->i])) ctx->i++;
                        if (ctx->i - as == 4u && w_strncasecmp(ctx->text + as, "type", 4u) == 0) is_type = 1;
                        whl_emit(ctx, "tok-attr", as, ctx->i);
                        whl_emit_gap(ctx, end);
                        if (ctx->i >= end || ctx->text[ctx->i] != '=') continue;
                        whl_emit(ctx, "tok-punctuation", ctx->i, ctx->i + 1u);
                        ctx->i++;
                        whl_emit_gap(ctx, end);
                        if (ctx->i < end && (ctx->text[ctx->i] == '"' || ctx->text[ctx->i] == '\'')) {
                            whl_scan_html_quoted(ctx, end, ctx->text[ctx->i], val_buf, sizeof(val_buf));
                        } else if (ctx->i < end && ctx->text[ctx->i] != '>' && ctx->text[ctx->i] != '/') {
                            whl_scan_unquoted(ctx, end, val_buf, sizeof(val_buf));
                        }
                        if (is_type) w_memcpy(type_val, val_buf, w_strlen(val_buf) + 1u);
                    }
                }
                if (!closing && !self_closing) {
                    if (w_strcasecmp(tag_name, "style") == 0) {
                        u32 stop = whl_find_end_tag(ctx->text, ctx->i, end, "style");
                        whl_scan_css(ctx, stop);
                    } else if (w_strcasecmp(tag_name, "script") == 0) {
                        u32 stop = whl_find_end_tag(ctx->text, ctx->i, end, "script");
                        const char *tv = type_val;
                        while (*tv && w_is_space(*tv)) tv++;
                        if (w_strncasecmp(tv, "application/ld+json", 19u) == 0 ||
                            w_strncasecmp(tv, "application/json", 16u) == 0) {
                            whl_scan_json(ctx, stop);
                        } else {
                            whl_scan_js(ctx, stop);
                        }
                    }
                }
                continue;
            }
            whl_emit(ctx, "", i, i + 1u);
            ctx->i = i + 1u;
            continue;
        }
        if (ctx->text[i] == '&') {
            i32 ent = whl_entity_end(ctx->text, i, end);
            if (ent > (i32)i) {
                whl_emit(ctx, "tok-entity", i, (u32)ent);
                ctx->i = (u32)ent;
                continue;
            }
        }
        {
            u32 p = i;
            while (p < end && ctx->text[p] != '<' && ctx->text[p] != '&') p++;
            if (p == i) p = i + 1u;
            whl_emit(ctx, "", i, p);
            ctx->i = p;
        }
    }
}

__attribute__((export_name("wasm_highlight_document")))
u32 wasm_highlight_document(const char *src, u32 src_len,
                            const char *lang, u32 lang_len,
                            int add_trailing_space) {
    WHlCtx ctx;
    char lbuf[16];
    u32 k;
    ctx.text = src ? src : "";
    ctx.len = src_len;
    ctx.i = 0;
    wbuf_init(&ctx.out, src_len * 2u + 256u);
    for (k = 0; k < lang_len && k + 1u < sizeof(lbuf); k++) lbuf[k] = w_fold(lang[k]);
    lbuf[k] = '\0';

    if (src_len == 0 || !lbuf[0] || w_strcmp(lbuf, "text") == 0 || w_strcmp(lbuf, "plain") == 0) {
        wbuf_append_html_esc(&ctx.out, ctx.text, src_len);
    } else if (w_strcmp(lbuf, "css") == 0) {
        whl_scan_css(&ctx, src_len);
    } else if (w_strcmp(lbuf, "js") == 0 || w_strcmp(lbuf, "javascript") == 0) {
        whl_scan_js(&ctx, src_len);
    } else if (w_strcmp(lbuf, "json") == 0) {
        whl_scan_json(&ctx, src_len);
    } else if (w_strcmp(lbuf, "php") == 0) {
        u32 p = 0;
        while (p < src_len && w_is_space(ctx.text[p])) p++;
        if (p >= src_len || ctx.text[p] != '<') {
            whl_scan_php(&ctx, src_len);
            if (ctx.i < src_len) whl_scan_markup(&ctx, src_len);
        } else {
            whl_scan_markup(&ctx, src_len);
        }
    } else {
        whl_scan_markup(&ctx, src_len);
    }
    if (ctx.i < src_len) whl_emit(&ctx, "", ctx.i, src_len);
    if (add_trailing_space && src_len > 0 && ctx.text[src_len - 1u] == '\n') {
        wbuf_putc(&ctx.out, ' ');
    }
    wasm_set_result(ctx.out.data, ctx.out.len);
    return ctx.out.len;
}




