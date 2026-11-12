#include "wasm_runtime.h"

static int wmm_is_kw(const char *w, u32 len) {
    static const char *const kw[] = {
        "function", "return", "if", "else", "elseif", "foreach", "for", "while",
        "require_once", "include", "include_once", "require", "echo", "isset",
        "var", "let", "const", "class", "async", "await", "switch", "case",
        "break", "continue", "try", "catch", "throw", "new", "true", "false", "null"
    };
    u32 i;
    for (i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        if (w_strlen(kw[i]) == len && w_strncasecmp(w, kw[i], len) == 0) return 1;
    }
    return 0;
}

static int wmm_is_block_tag(const char *s, u32 len, u32 *out_tlen) {
    static const char *const tags[] = {
        "section", "article", "header", "footer", "main", "nav", "aside",
        "figure", "div", "form", "table", "ul", "ol", "style", "script"
    };
    u32 i;
    for (i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
        u32 tlen = w_strlen(tags[i]);
        if (len >= tlen && w_strncasecmp(s, tags[i], tlen) == 0 &&
            (len == tlen || !w_is_ident_char(s[tlen]))) {
            if (out_tlen) *out_tlen = tlen;
            return 1;
        }
    }
    return 0;
}

static int wmm_meta_start(const char *tr, u32 tr_len, char *name_out) {
    static const char *const names[] = {"page-values", "related-sales", "generated-seo"};
    u32 p = 4u, k;
    if (tr_len < 4u || w_memcmp(tr, "<!--", 4u) != 0) return 0;
    while (p < tr_len && w_is_space(tr[p])) p++;
    for (k = 0; k < 3u; k++) {
        u32 nlen = w_strlen(names[k]);
        if (p + nlen <= tr_len && w_strncasecmp(tr + p, names[k], nlen) == 0 &&
            (p + nlen == tr_len || !w_is_ident_char(tr[p + nlen]))) {
            w_memcpy(name_out, names[k], nlen + 1u);
            return 1;
        }
    }
    return 0;
}

static void wmm_emit_seg(WBuf *out, int *first, u32 col, u32 len, const char *tok) {
    if (*first) *first = 0;
    else wbuf_putc(out, ',');
    wbuf_puts(out, "{\"col\":");
    wbuf_put_int(out, (i32)col);
    wbuf_puts(out, ",\"len\":");
    wbuf_put_int(out, (i32)(len > 0 ? len : 1u));
    wbuf_puts(out, ",\"token\":\"");
    wbuf_puts(out, tok);
    wbuf_puts(out, "\"}");
}

static int wmm_extract_attr(const char *attrs, u32 alen, const char *attr_name,
                            int first_word_only, char prefix, char *out, u32 out_sz) {
    u32 nlen = w_strlen(attr_name), i;
    for (i = 0; i + nlen + 3u <= alen; i++) {
        u32 p, vs, ve, w = 0;
        char q;
        if (i > 0 && w_is_ident_char(attrs[i - 1u])) continue;
        if (w_strncasecmp(attrs + i, attr_name, nlen) != 0) continue;
        p = i + nlen;
        while (p < alen && w_is_space(attrs[p])) p++;
        if (p >= alen || attrs[p] != '=') continue;
        p++;
        while (p < alen && w_is_space(attrs[p])) p++;
        if (p >= alen || (attrs[p] != '"' && attrs[p] != '\'')) continue;
        q = attrs[p++];
        vs = p;
        while (p < alen && attrs[p] != q) p++;
        if (p == vs) continue;
        ve = p;
        while (vs < ve && w_is_space(attrs[vs])) vs++;
        while (ve > vs && w_is_space(attrs[ve - 1u])) ve--;
        if (vs >= ve) { out[0] = prefix; out[1] = '\0'; return 1; }
        out[w++] = prefix;
        while (vs < ve && w + 1u < out_sz) {
            if (first_word_only && w_is_space(attrs[vs])) break;
            out[w++] = attrs[vs++];
        }
        out[w] = '\0';
        return 1;
    }
    return 0;
}

__attribute__((export_name("wasm_scan_minimap_lines")))
u32 wasm_scan_minimap_lines(const char *src, u32 src_len,
                            const char *lang_ptr, u32 lang_len) {
    char lang[16];
    char block_stack[64][64];
    int bsp = 0, in_comment = 0, in_meta = 0, in_php, in_style, in_script;
    u32 k, line_start = 0, line_idx = 0;
    WBuf out;

    for (k = 0; k < lang_len && k + 1u < sizeof(lang); k++) lang[k] = w_fold(lang_ptr[k]);
    lang[k] = '\0';
    in_php = (w_strcmp(lang, "php") == 0);
    in_style = (w_strcmp(lang, "css") == 0);
    in_script = (w_strcmp(lang, "js") == 0);

    wbuf_init(&out, src_len + 512u);
    wbuf_putc(&out, '[');

    while (line_start <= src_len) {
        u32 line_end = line_start;
        char expanded[256];
        u32 exp_len = 0, bi, indent = 0, trim_s, trim_e, limit, col;
        int is_block_header = 0, has_meta_start = 0, first_seg = 1;
        char active_label[64], meta_name[32];

        while (line_end < src_len && src[line_end] != '\n') line_end++;
        for (bi = line_start; bi < line_end && exp_len + 4u < sizeof(expanded); ) {
            u8 c = (u8)src[bi];
            if (c == '\t') {
                w_memcpy(expanded + exp_len, "    ", 4u);
                exp_len += 4u;
                bi++;
            } else if (c < 0x80u) {
                expanded[exp_len++] = (char)c;
                bi++;
            } else if (c < 0xE0u) {
                expanded[exp_len++] = (char)0x80;
                bi += 2u;
            } else if (c < 0xF0u) {
                expanded[exp_len++] = (char)0x80;
                bi += 3u;
            } else {
                expanded[exp_len++] = (char)0x80;
                expanded[exp_len++] = (char)0x80;
                bi += 4u;
            }
        }
        expanded[exp_len] = '\0';

        while (indent < exp_len && expanded[indent] == ' ') indent++;
        trim_s = indent;
        while (trim_s < exp_len && w_is_space(expanded[trim_s])) trim_s++;
        trim_e = exp_len;
        while (trim_e > trim_s && w_is_space(expanded[trim_e - 1u])) trim_e--;

        if (bsp > 0) w_memcpy(active_label, block_stack[bsp - 1], w_strlen(block_stack[bsp - 1]) + 1u);
        else active_label[0] = '\0';

        if (!in_comment && trim_e > trim_s) {
            const char *tr = expanded + trim_s;
            u32 tr_len = trim_e - trim_s;
            if (wmm_meta_start(tr, tr_len, meta_name)) {
                in_meta = 1;
                has_meta_start = 1;
                is_block_header = 1;
                w_memcpy(active_label, "<!-- ", 5u);
                w_memcpy(active_label + 5u, meta_name, w_strlen(meta_name));
                w_memcpy(active_label + 5u + w_strlen(meta_name), " -->", 5u);
                if (bsp < 64) w_memcpy(block_stack[bsp++], active_label, w_strlen(active_label) + 1u);
            } else if (w_strcmp(lang, "html") == 0 || w_strcmp(lang, "php") == 0) {
                u32 tlen = 0;
                if (tr[0] == '<' && wmm_is_block_tag(tr + 1u, tr_len - 1u, &tlen)) {
                    char tname[24], suffix[40], close_pat[32];
                    const char *attrs = tr + 1u + tlen;
                    u32 alen = tr_len - 1u - tlen, ti;
                    const char *gt = w_memchr(attrs, '>', alen);
                    if (gt) alen = (u32)(gt - attrs);
                    for (ti = 0; ti < tlen && ti + 1u < sizeof(tname); ti++) tname[ti] = w_fold(tr[1u + ti]);
                    tname[ti] = '\0';
                    suffix[0] = '\0';
                    if (!wmm_extract_attr(attrs, alen, "class", 1, '.', suffix, sizeof(suffix))) {
                        wmm_extract_attr(attrs, alen, "id", 0, '#', suffix, sizeof(suffix));
                    }
                    active_label[0] = '<';
                    w_memcpy(active_label + 1u, tname, tlen);
                    w_memcpy(active_label + 1u + tlen, suffix, w_strlen(suffix));
                    active_label[1u + tlen + w_strlen(suffix)] = '>';
                    active_label[2u + tlen + w_strlen(suffix)] = '\0';
                    is_block_header = 1;
                    close_pat[0] = '<'; close_pat[1] = '/';
                    w_memcpy(close_pat + 2u, tname, tlen + 1u);
                    if (!(tr_len >= 2u && tr[tr_len - 2u] == '/' && tr[tr_len - 1u] == '>') &&
                        w_index_of_ci(tr, tr_len, close_pat, 0, tr_len) < 0 && bsp < 64) {
                        w_memcpy(block_stack[bsp++], active_label, w_strlen(active_label) + 1u);
                    }
                    if (w_strcmp(tname, "style") == 0 && w_index_of_ci(tr, tr_len, "</style", 0, tr_len) < 0) in_style = 1;
                    if (w_strcmp(tname, "script") == 0 && w_index_of_ci(tr, tr_len, "</script", 0, tr_len) < 0) in_script = 1;
                } else if (tr_len >= 3u && tr[0] == '<' && tr[1] == '/' && wmm_is_block_tag(tr + 2u, tr_len - 2u, &tlen)) {
                    if (bsp > 0) bsp--;
                    if (w_strncasecmp(tr, "</style", 7u) == 0) in_style = 0;
                    if (w_strncasecmp(tr, "</script", 8u) == 0) in_script = 0;
                }
            } else if ((w_strcmp(lang, "js") == 0 || w_strcmp(lang, "css") == 0) && tr[tr_len - 1u] == '{') {
                u32 he = tr_len - 1u;
                while (he > 0 && w_is_space(tr[he - 1u])) he--;
                is_block_header = 1;
                if (he > 0) {
                    if (he > 32u) he = 32u;
                    w_memcpy(active_label, tr, he);
                    active_label[he] = '\0';
                    if (bsp < 64) w_memcpy(block_stack[bsp++], active_label, he + 1u);
                }
            } else if ((w_strcmp(lang, "js") == 0 || w_strcmp(lang, "css") == 0) && tr[0] == '}') {
                if (bsp > 0) bsp--;
            }
        }

        if (line_idx++) wbuf_putc(&out, ',');
        wbuf_puts(&out, "{\"indent\":");
        wbuf_put_int(&out, (i32)(indent < 92u ? indent : 92u));
        wbuf_puts(&out, ",\"blockDepth\":");
        wbuf_put_int(&out, (i32)bsp);
        wbuf_puts(&out, ",\"isBlockHeader\":");
        wbuf_puts(&out, is_block_header ? "true" : "false");
        wbuf_puts(&out, ",\"isMetaBlock\":");
        wbuf_puts(&out, (in_meta || has_meta_start) ? "true" : "false");
        wbuf_puts(&out, ",\"blockLabel\":");
        wbuf_append_json_esc(&out, active_label, w_strlen(active_label));
        wbuf_puts(&out, ",\"segments\":[");

        limit = exp_len < 92u ? exp_len : 92u;
        col = indent < limit ? indent : limit;
        while (col < limit) {
            const char *rest = expanded + col;
            u32 rlen = limit - col;
            if (expanded[col] == ' ') { col++; continue; }
            if (in_comment || in_meta) {
                i32 cl = w_index_of(rest, rlen, "-->", 0, rlen);
                if (cl >= 0) {
                    u32 sp = (u32)cl + 3u;
                    wmm_emit_seg(&out, &first_seg, col, sp, in_meta ? "metaKey" : "comment");
                    col += sp;
                    in_comment = 0;
                    if (in_meta && bsp > 0) bsp--;
                    in_meta = 0;
                } else {
                    const char *colon = w_memchr(rest, ':', rlen);
                    if (in_meta && colon) {
                        u32 clen = (u32)(colon - rest) + 1u;
                        wmm_emit_seg(&out, &first_seg, col, clen, "metaKey");
                        if (clen < rlen) wmm_emit_seg(&out, &first_seg, col + clen, rlen - clen, "string");
                    } else {
                        wmm_emit_seg(&out, &first_seg, col, rlen, in_meta ? "metaKey" : "comment");
                    }
                    break;
                }
                continue;
            }
            if (rlen >= 5u && w_memcmp(rest, "<!--#", 5u) == 0) {
                i32 cl = w_index_of(rest, rlen, "-->", 0, rlen);
                u32 sp = (cl >= 0) ? (u32)cl + 3u : rlen;
                wmm_emit_seg(&out, &first_seg, col, sp, "ssi");
                col += sp;
                continue;
            }
            if (rlen >= 4u && w_memcmp(rest, "<!--", 4u) == 0) {
                i32 cl = w_index_of(rest, rlen, "-->", 0, rlen);
                if (cl >= 0) {
                    u32 sp = (u32)cl + 3u;
                    wmm_emit_seg(&out, &first_seg, col, sp, "comment");
                    col += sp;
                } else {
                    in_comment = 1;
                    wmm_emit_seg(&out, &first_seg, col, rlen, "comment");
                    break;
                }
                continue;
            }
            if (rlen >= 2u && rest[0] == '<' && rest[1] == '?') {
                u32 sp = (rlen >= 5u && w_strncasecmp(rest, "<?php", 5u) == 0) ? 5u : 2u;
                in_php = 1;
                wmm_emit_seg(&out, &first_seg, col, sp, "ssi");
                col += sp;
                continue;
            }
            if (in_php && rlen >= 2u && rest[0] == '?' && rest[1] == '>') {
                in_php = (w_strcmp(lang, "php") == 0 && !w_memchr(expanded, '<', exp_len));
                wmm_emit_seg(&out, &first_seg, col, 2u, "ssi");
                col += 2u;
                continue;
            }
            if ((in_php || in_script || in_style) && rlen >= 2u &&
                ((rest[0] == '/' && rest[1] == '/') || (rest[0] == '/' && rest[1] == '*'))) {
                wmm_emit_seg(&out, &first_seg, col, rlen, "comment");
                break;
            }
            if (expanded[col] == '"' || expanded[col] == '\'') {
                char q = expanded[col];
                u32 e = col + 1u;
                while (e < limit && expanded[e] != q) {
                    if (expanded[e] == '\\') e++;
                    e++;
                }
                if (e < limit) e++;
                wmm_emit_seg(&out, &first_seg, col, e - col, "string");
                col = e;
                continue;
            }
            if (expanded[col] == '<' && (w_strcmp(lang, "html") == 0 || w_strcmp(lang, "php") == 0)) {
                u32 e = col + 1u;
                if (e < limit && (expanded[e] == '/' || expanded[e] == '!')) e++;
                while (e < limit && w_is_name_char(expanded[e])) e++;
                wmm_emit_seg(&out, &first_seg, col, e - col, "tag");
                col = e;
                continue;
            }
            if (expanded[col] == '$' && in_php) {
                u32 e = col + 1u;
                while (e < limit && w_is_ident_char(expanded[e])) e++;
                wmm_emit_seg(&out, &first_seg, col, e - col, "variable");
                col = e;
                continue;
            }
            if (w_is_ident_start(expanded[col]) || expanded[col] == '@' || expanded[col] == '-') {
                u32 e = col + 1u;
                const char *tok = "text";
                while (e < limit && w_is_name_char(expanded[e])) e++;
                if (e < limit && (expanded[e] == '=' || (in_style && expanded[e] == ':'))) tok = "attr";
                else if (expanded[col] == '@' || wmm_is_kw(expanded + col, e - col)) tok = "keyword";
                else if (in_style) tok = "tag";
                wmm_emit_seg(&out, &first_seg, col, e - col, tok);
                col = e;
                continue;
            }
            if (w_is_digit(expanded[col]) || expanded[col] == '#') {
                u32 e = col + 1u;
                while (e < limit && (w_is_hex(expanded[e]) || w_strchr(".%pxremvhw-", expanded[e]))) e++;
                wmm_emit_seg(&out, &first_seg, col, e - col, "number");
                col = e;
                continue;
            }
            {
                u32 e = col + 1u;
                while (e < limit && !w_strchr(" \t\"'<$@#", expanded[e]) && !w_is_ident_char(expanded[e])) e++;
                wmm_emit_seg(&out, &first_seg, col, e - col, "punct");
                col = e;
            }
        }
        wbuf_puts(&out, "]}");
        if (line_end >= src_len) break;
        line_start = line_end + 1u;
    }
    wbuf_putc(&out, ']');
    wasm_set_result(out.data, out.len);
    return out.len;
}


