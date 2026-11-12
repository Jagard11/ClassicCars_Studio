#include "wasm_runtime.h"

u32 wasm_detect_language(const char *path, u32 path_len);

static u32 wcm_php_open_len(const char *text, u32 len, u32 i) {
    if (i + 2u >= len || text[i] != '<' || text[i + 1u] != '?') return 0;
    if (text[i + 2u] == '=') return 3u;
    if (i + 5u <= len && w_strncasecmp(text + i, "<?php", 5u) == 0) {
        if (i + 5u == len || !w_is_ident_char(text[i + 5u])) return 5u;
    }
    return 0;
}

static u32 wcm_skip_quoted(const char *text, u32 len, u32 start, char quote, int multiline) {
    u32 i = start + 1u;
    while (i < len) {
        if (text[i] == '\\') {
            i = (i + 2u <= len) ? i + 2u : len;
            continue;
        }
        if (text[i] == quote) return i + 1u;
        if (!multiline && text[i] == '\n') return i;
        i++;
    }
    return len;
}

static int wcm_matches_end_tag(const char *text, u32 len, u32 i, const char *tag) {
    u32 tlen = w_strlen(tag);
    if (i + 2u + tlen > len || text[i] != '<' || text[i + 1u] != '/') return 0;
    if (w_strncasecmp(text + i + 2u, tag, tlen) != 0) return 0;
    if (i + 2u + tlen < len && w_is_name_char(text[i + 2u + tlen])) return 0;
    return 1;
}

static int wcm_match_heredoc(const char *text, u32 len, u32 start,
                             u32 *body_s, u32 *body_e, u32 *after_close) {
    u32 p = start + 3u, id_s, id_e, nl, line_pos;
    char quote = '\0';
    if (p >= len) return 0;
    while (p < len && (text[p] == ' ' || text[p] == '\t')) p++;
    if (p < len && (text[p] == '\'' || text[p] == '"')) quote = text[p++];
    if (p >= len || !w_is_ident_start(text[p])) return 0;
    id_s = p++;
    while (p < len && w_is_ident_char(text[p])) p++;
    id_e = p;
    if (quote) {
        if (p >= len || text[p] != quote) return 0;
        p++;
    }
    nl = p;
    if (nl < len && text[nl] == '\r') nl++;
    if (nl >= len || text[nl] != '\n') return 0;
    *body_s = nl + 1u;
    line_pos = *body_s;
    while (line_pos < len) {
        u32 j = line_pos, id_len = id_e - id_s;
        while (j < len && (text[j] == ' ' || text[j] == '\t')) j++;
        if (j + id_len <= len && w_memcmp(text + j, text + id_s, id_len) == 0 &&
            (j + id_len == len || !w_is_ident_char(text[j + id_len]))) {
            u32 k = j + id_len, tail;
            if (k < len && text[k] == ';') k++;
            tail = k;
            while (tail < len && (text[tail] == ' ' || text[tail] == '\t')) tail++;
            if (tail >= len || text[tail] == '\n' || text[tail] == '\r') {
                *body_e = line_pos;
                *after_close = k;
                return 1;
            }
        }
        while (line_pos < len && text[line_pos] != '\n') line_pos++;
        if (line_pos < len) line_pos++;
    }
    *body_e = len;
    *after_close = len;
    return 1;
}

static int wcm_read_embedded_open(const char *text, u32 len, u32 start,
                                  char *name_out, u32 *gt_out, int *self_close, int *is_json) {
    u32 tlen = 0, i;
    char quote = '\0';
    if (start + 6u <= len && w_strncasecmp(text + start, "<style", 6u) == 0 &&
        (start + 6u == len || !w_is_name_char(text[start + 6u]))) {
        w_memcpy(name_out, "style", 6u);
        tlen = 6u;
    } else if (start + 7u <= len && w_strncasecmp(text + start, "<script", 7u) == 0 &&
               (start + 7u == len || !w_is_name_char(text[start + 7u]))) {
        w_memcpy(name_out, "script", 7u);
        tlen = 7u;
    } else {
        return 0;
    }
    *self_close = 0;
    *is_json = 0;
    i = start + tlen;
    while (i < len) {
        char ch = text[i];
        if (quote) {
            if (ch == quote) quote = '\0';
            i++;
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; i++; continue; }
        if (ch == '>') {
            u32 k = i;
            while (k > start && w_is_space(text[k - 1u])) k--;
            if (k > start && text[k - 1u] == '/') *self_close = 1;
            if (w_index_of_ci(text, len, "application/ld+json", start, i) >= 0 ||
                w_index_of_ci(text, len, "application/json", start, i) >= 0) {
                *is_json = 1;
            }
            *gt_out = i;
            return 1;
        }
        i++;
    }
    *gt_out = len;
    return 1;
}

static const char *wcm_scan_context_at(const char *text, u32 len, u32 target, int starts_in_php) {
    int in_php = starts_in_php;
    const char *enclosing = "";
    u32 i = 0;
    while (i < target && i < len) {
        if (in_php) {
            char c = text[i];
            if (c == '\'' || c == '"') {
                i = wcm_skip_quoted(text, len, i, c, 1);
                if (i > target) return "php";
                continue;
            }
            if ((c == '/' && i + 1u < len && text[i + 1u] == '/') ||
                (c == '#' && (i + 1u >= len || text[i + 1u] != '['))) {
                i32 nl = w_index_of(text, len, "\n", i + 1u, len);
                u32 llim = (nl < 0) ? len : (u32)nl;
                i32 cl = w_index_of(text, len, "?>", i + 1u, llim);
                if (cl >= 0) {
                    if (target <= (u32)cl + 1u) return "php";
                    in_php = 0;
                    i = (u32)cl + 2u;
                    continue;
                }
                if (nl < 0 || (u32)nl >= target) return "php";
                i = (u32)nl + 1u;
                continue;
            }
            if (c == '/' && i + 1u < len && text[i + 1u] == '*') {
                i32 cl = w_index_of(text, len, "*/", i + 2u, len);
                if (cl < 0 || (u32)cl + 2u > target) return "php";
                i = (u32)cl + 2u;
                continue;
            }
            if (c == '<' && i + 2u < len && text[i + 1u] == '<' && text[i + 2u] == '<') {
                u32 bs = 0, be = 0, ac = 0;
                if (wcm_match_heredoc(text, len, i, &bs, &be, &ac)) {
                    if (target < bs) return "php";
                    if (target < be) return wcm_scan_context_at(text + bs, be - bs, target - bs, 0);
                    i = ac;
                    continue;
                }
            }
            if (c == '?' && i + 1u < len && text[i + 1u] == '>') {
                if (target <= i + 1u) return "php";
                in_php = 0;
                i += 2u;
                continue;
            }
            i++;
            continue;
        }
        {
            u32 plen = wcm_php_open_len(text, len, i);
            if (plen > 0) {
                if (target < i + plen) return "php";
                in_php = 1;
                i += plen;
                continue;
            }
        }
        if (w_strcmp(enclosing, "css") == 0) {
            if (text[i] == '/' && i + 1u < len && text[i + 1u] == '*') {
                i32 cl = w_index_of(text, len, "*/", i + 2u, len);
                if (cl < 0 || (u32)cl + 2u > target) return "css";
                i = (u32)cl + 2u;
                continue;
            }
            if (text[i] == '"' || text[i] == '\'') {
                i = wcm_skip_quoted(text, len, i, text[i], 0);
                if (i > target) return "css";
                continue;
            }
            if (wcm_matches_end_tag(text, len, i, "style")) {
                i32 gt = w_index_of(text, len, ">", i, len);
                if (gt < 0 || target <= (u32)gt) return "html";
                enclosing = "";
                i = (u32)gt + 1u;
                continue;
            }
            i++;
            continue;
        }
        if (w_strcmp(enclosing, "js") == 0 || w_strcmp(enclosing, "json") == 0) {
            if (text[i] == '/' && i + 1u < len && text[i + 1u] == '/') {
                i32 nl = w_index_of(text, len, "\n", i + 2u, len);
                if (nl < 0 || (u32)nl >= target) return enclosing;
                i = (u32)nl + 1u;
                continue;
            }
            if (text[i] == '/' && i + 1u < len && text[i + 1u] == '*') {
                i32 cl = w_index_of(text, len, "*/", i + 2u, len);
                if (cl < 0 || (u32)cl + 2u > target) return enclosing;
                i = (u32)cl + 2u;
                continue;
            }
            if (text[i] == '"' || text[i] == '\'' || text[i] == '`') {
                i = wcm_skip_quoted(text, len, i, text[i], text[i] == '`');
                if (i > target) return enclosing;
                continue;
            }
            if (wcm_matches_end_tag(text, len, i, "script")) {
                i32 gt = w_index_of(text, len, ">", i, len);
                if (gt < 0 || target <= (u32)gt) return "html";
                enclosing = "";
                i = (u32)gt + 1u;
                continue;
            }
            i++;
            continue;
        }
        if (i + 4u <= len && w_memcmp(text + i, "<!--", 4u) == 0) {
            i32 cl = w_index_of(text, len, "-->", i + 4u, len);
            if (cl < 0 || target <= (u32)cl + 3u) return "html";
            i = (u32)cl + 3u;
            continue;
        }
        if (text[i] == '<') {
            char tname[16];
            u32 gt = 0;
            int sc = 0, ij = 0;
            if (wcm_read_embedded_open(text, len, i, tname, &gt, &sc, &ij)) {
                if (target <= gt) return "html";
                if (!sc) enclosing = (w_strcmp(tname, "style") == 0) ? "css" : (ij ? "json" : "js");
                i = gt + 1u;
                continue;
            }
        }
        i++;
    }
    if (in_php) return "php";
    if (!in_php && wcm_php_open_len(text, len, target) > 0) return "php";
    if (w_strcmp(enclosing, "css") == 0) {
        if (wcm_matches_end_tag(text, len, target, "style")) return "html";
        return "css";
    }
    if (w_strcmp(enclosing, "js") == 0 || w_strcmp(enclosing, "json") == 0) {
        if (wcm_matches_end_tag(text, len, target, "script")) return "html";
        return enclosing;
    }
    return "html";
}

static const char *wcm_detect_context(const char *text, u32 len, u32 byte_offset,
                                      const char *path, u32 path_len) {
    u32 target = byte_offset > len ? len : byte_offset;
    u32 ll = wasm_detect_language(path, path_len);
    const char *base = (const char *)(unsigned long)wasm_result_ptr();
    int starts_php = 0;
    (void)ll;
    if (w_strcmp(base, "css") == 0) return "css";
    if (w_strcmp(base, "js") == 0) return "js";
    if (w_strcmp(base, "json") == 0) return "json";
    if (w_strcmp(base, "php") == 0) {
        u32 p = 0;
        while (p < len && w_is_space(text[p])) p++;
        if (p >= len || text[p] != '<') starts_php = 1;
    }
    return wcm_scan_context_at(text, len, target, starts_php);
}

__attribute__((export_name("wasm_detect_comment_context")))
u32 wasm_detect_comment_context(const char *src, u32 src_len, u32 u16_off,
                                const char *path, u32 path_len) {
    u32 u8_off = wasm_u16_to_u8(src, src_len, u16_off);
    const char *ctx = wcm_detect_context(src, src_len, u8_off, path, path_len);
    u32 clen = w_strlen(ctx);
    wasm_set_result(ctx, clen);
    return clen;
}

static int wcm_is_line_ctx(const char *ctx) {
    return w_strcmp(ctx, "php") == 0 || w_strcmp(ctx, "js") == 0 || w_strcmp(ctx, "json") == 0;
}

static u32 wcm_emit_edit(const char *src, u32 u8_from, u32 u8_to,
                         const char *ins, u32 ins_len,
                         u32 u8_sel_start, u32 u8_sel_end, const char *ctx) {
    u32 u16_from = wasm_u8_to_u16(src, u8_from);
    u32 u16_to = wasm_u8_to_u16(src, u8_to);
    u32 u16_ins_len = wasm_u8_to_u16(ins, ins_len);
    u32 u16_ss, u16_se;
    WBuf out;

    /* Map byte selection offsets in the new text to UTF-16 offsets */
    if (u8_sel_start <= u8_from) {
        u16_ss = wasm_u8_to_u16(src, u8_sel_start);
    } else if (u8_sel_start <= u8_from + ins_len) {
        u16_ss = u16_from + wasm_u8_to_u16(ins, u8_sel_start - u8_from);
    } else {
        u16_ss = u16_from + u16_ins_len + wasm_u8_to_u16(src + u8_to, u8_sel_start - (u8_from + ins_len));
    }

    if (u8_sel_end <= u8_from) {
        u16_se = wasm_u8_to_u16(src, u8_sel_end);
    } else if (u8_sel_end <= u8_from + ins_len) {
        u16_se = u16_from + wasm_u8_to_u16(ins, u8_sel_end - u8_from);
    } else {
        u16_se = u16_from + u16_ins_len + wasm_u8_to_u16(src + u8_to, u8_sel_end - (u8_from + ins_len));
    }

    wbuf_init(&out, ins_len + 128u);
    wbuf_puts(&out, "{\"from\":"); wbuf_put_int(&out, (i32)u16_from);
    wbuf_puts(&out, ",\"to\":"); wbuf_put_int(&out, (i32)u16_to);
    wbuf_puts(&out, ",\"insert\":"); wbuf_append_json_esc(&out, ins, ins_len);
    wbuf_puts(&out, ",\"selectionStart\":"); wbuf_put_int(&out, (i32)u16_ss);
    wbuf_puts(&out, ",\"selectionEnd\":"); wbuf_put_int(&out, (i32)u16_se);
    wbuf_puts(&out, ",\"context\":\""); wbuf_puts(&out, ctx);
    wbuf_puts(&out, "\"}");
    wasm_set_result(out.data, out.len);
    return out.len;
}

static int wcm_try_uncomment_single(const char *tr, u32 tr_len, const char *ctx,
                                    const char **out_ptr, u32 *out_len, u32 *prefix_rm) {
    if (tr_len >= 7u && w_memcmp(tr, "<!--", 4u) == 0 && w_memcmp(tr + tr_len - 3u, "-->", 3u) == 0 &&
        w_index_of(tr, tr_len, "-->", 4u, tr_len) == (i32)(tr_len - 3u)) {
        u32 pre = (tr_len >= 8u && tr[4] == ' ') ? 5u : 4u;
        u32 suf = (tr_len >= pre + 4u && tr[tr_len - 4u] == ' ') ? 4u : 3u;
        *out_ptr = tr + pre;
        *out_len = tr_len - pre - suf;
        *prefix_rm = pre;
        return 1;
    }
    if (tr_len >= 4u && w_memcmp(tr, "/*", 2u) == 0 && w_memcmp(tr + tr_len - 2u, "*/", 2u) == 0 &&
        w_index_of(tr, tr_len, "*/", 2u, tr_len) == (i32)(tr_len - 2u)) {
        u32 pre = (tr_len >= 5u && tr[2] == ' ') ? 3u : 2u;
        u32 suf = (tr_len >= pre + 3u && tr[tr_len - 3u] == ' ') ? 3u : 2u;
        *out_ptr = tr + pre;
        *out_len = tr_len - pre - suf;
        *prefix_rm = pre;
        return 1;
    }
    if (wcm_is_line_ctx(ctx)) {
        if (tr_len >= 2u && w_memcmp(tr, "//", 2u) == 0) {
            u32 pre = (tr_len >= 3u && tr[2] == ' ') ? 3u : 2u;
            *out_ptr = tr + pre;
            *out_len = tr_len - pre;
            *prefix_rm = pre;
            return 1;
        }
        if (w_strcmp(ctx, "php") == 0 && tr_len >= 1u && tr[0] == '#' && (tr_len == 1u || tr[1] != '[')) {
            u32 pre = (tr_len >= 2u && tr[1] == ' ') ? 2u : 1u;
            *out_ptr = tr + pre;
            *out_len = tr_len - pre;
            *prefix_rm = pre;
            return 1;
        }
    }
    return 0;
}

static void wcm_strip_outer_delims(const char *raw, u32 raw_len, u32 open_len, u32 close_len,
                                   const char **out_p, u32 *out_l) {
    const char *b = raw + open_len;
    u32 bl = raw_len - open_len - close_len;
    u32 ls = 0;
    while (ls < bl && (b[ls] == ' ' || b[ls] == '\t')) ls++;
    if (ls < bl && b[ls] == '\r') ls++;
    if (ls < bl && b[ls] == '\n') {
        u32 te = bl;
        while (te > ls && (b[te - 1u] == ' ' || b[te - 1u] == '\t')) te--;
        if (te > ls && b[te - 1u] == '\n') {
            te--;
            if (te > ls && b[te - 1u] == '\r') te--;
            *out_p = b + ls + 1u;
            *out_l = te - (ls + 1u);
            return;
        }
    }
    if (bl > 0 && b[0] == ' ') { b++; bl--; }
    if (bl > 0 && b[bl - 1u] == ' ') bl--;
    *out_p = b;
    *out_l = bl;
}

static int wcm_unwrap_block_text(const char *in, u32 in_len, const char **out_p, u32 *out_l) {
    if (in_len >= 7u && w_memcmp(in, "<!--", 4u) == 0 && w_memcmp(in + in_len - 3u, "-->", 3u) == 0 &&
        w_index_of(in, in_len, "-->", 4u, in_len) == (i32)(in_len - 3u)) {
        wcm_strip_outer_delims(in, in_len, 4u, 3u, out_p, out_l);
        return 1;
    }
    if (in_len >= 4u && w_memcmp(in, "/*", 2u) == 0 && w_memcmp(in + in_len - 2u, "*/", 2u) == 0 &&
        w_index_of(in, in_len, "*/", 2u, in_len) == (i32)(in_len - 2u)) {
        wcm_strip_outer_delims(in, in_len, 2u, 2u, out_p, out_l);
        return 1;
    }
    return 0;
}

static int wcm_match_single_line_php(const char *line, u32 line_len,
                                     u32 *open_len_out, u32 *body_len_out) {
    u32 p = 0, plen, ce = line_len, cs;
    while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;
    plen = wcm_php_open_len(line, line_len, p);
    if (!plen || p + plen >= line_len || (line[p + plen] != ' ' && line[p + plen] != '\t')) return 0;
    p += plen;
    while (p < line_len && (line[p] == ' ' || line[p] == '\t')) p++;
    while (ce > p && (line[ce - 1u] == ' ' || line[ce - 1u] == '\t')) ce--;
    if (ce < p + 2u || line[ce - 2u] != '?' || line[ce - 1u] != '>') return 0;
    cs = ce - 2u;
    while (cs > p && (line[cs - 1u] == ' ' || line[cs - 1u] == '\t')) cs--;
    if (cs <= p) return 0;
    *open_len_out = p;
    *body_len_out = cs - p;
    return 1;
}

static int wcm_is_ssi_or_meta_comment(const char *s, u32 len) {
    u32 p = 4u;
    if (len < 4u || w_memcmp(s, "<!--", 4u) != 0) return 0;
    while (p < len && w_is_space(s[p])) p++;
    if (p < len && s[p] == '#') return 1;
    if (p < len && s[p] == '/') { p++; while (p < len && w_is_space(s[p])) p++; }
    if ((p + 11u <= len && w_strncasecmp(s + p, "page-values", 11u) == 0) ||
        (p + 13u <= len && w_strncasecmp(s + p, "related-sales", 13u) == 0) ||
        (p + 13u <= len && w_strncasecmp(s + p, "generated-seo", 13u) == 0)) {
        return 1;
    }
    return 0;
}

static u32 wcm_try_enclosing_at_caret(const char *text, u32 len, u32 line_s, u32 content_e,
                                      u32 caret, const char *ctx) {
    const char *opens[2] = {"/*", "<!--"};
    const char *closes[2] = {"*/", "-->"};
    u32 start_k = (w_strcmp(ctx, "html") == 0) ? 1u : 0u;
    u32 k;
    for (k = start_k; k < 2u; k++) {
        const char *op = opens[k], *cl = closes[k];
        u32 olen = w_strlen(op), clen = w_strlen(cl);
        i32 open_at = -1;
        u32 pos = (caret < len) ? caret : len;
        for (;;) {
            if (pos + olen <= len && w_memcmp(text + pos, op, olen) == 0) {
                open_at = (i32)pos;
                break;
            }
            if (pos == 0) break;
            pos--;
        }
        if (open_at < 0) continue;
        {
            i32 close_at = w_index_of(text, len, cl, (u32)open_at + olen, len);
            u32 close_end;
            const char *unw_p;
            u32 unw_l, pre_rm, next_c;
            if (close_at < 0 || (u32)close_at + clen <= line_s) continue;
            close_end = (u32)close_at + clen;
            if (caret < (u32)open_at || caret > close_end) continue;
            if ((u32)open_at >= line_s && close_end <= content_e) continue;
            if (olen == 4u && wcm_is_ssi_or_meta_comment(text + open_at, close_end - (u32)open_at)) continue;
            wcm_strip_outer_delims(text + open_at, close_end - (u32)open_at, olen, clen, &unw_p, &unw_l);
            pre_rm = (u32)(unw_p - (text + open_at));
            next_c = (caret > pre_rm) ? (caret - pre_rm) : (u32)open_at;
            if (next_c < (u32)open_at) next_c = (u32)open_at;
            if (next_c > (u32)open_at + unw_l) next_c = (u32)open_at + unw_l;
            return wcm_emit_edit(text, (u32)open_at, close_end, unw_p, unw_l, next_c, next_c, ctx);
        }
    }
    return 0;
}

static u32 wcm_try_inline_at_caret(const char *text, u32 len, u32 first_nw, u32 last_nw,
                                   u32 caret, const char *ctx) {
    const char *opens[2] = {"/*", "<!--"};
    const char *closes[2] = {"*/", "-->"};
    u32 start_k = (w_strcmp(ctx, "html") == 0) ? 1u : 0u;
    u32 k;
    for (k = start_k; k < 2u; k++) {
        const char *op = opens[k], *cl = closes[k];
        u32 olen = w_strlen(op), clen = w_strlen(cl), sf = first_nw;
        while (sf < last_nw) {
            i32 cs = w_index_of(text, len, op, sf, last_nw);
            i32 ce_at;
            u32 ce;
            if (cs < 0 || (u32)cs >= last_nw) break;
            ce_at = w_index_of(text, len, cl, (u32)cs + olen, last_nw);
            if (ce_at < 0 || (u32)ce_at + clen > last_nw) break;
            ce = (u32)ce_at + clen;
            if (caret >= (u32)cs && caret <= ce) {
                u32 raw_l = ce - (u32)cs;
                u32 pre = (raw_l > olen && text[(u32)cs + olen] == ' ') ? olen + 1u : olen;
                u32 suf = (raw_l >= pre + clen + 1u && text[ce - clen - 1u] == ' ') ? clen + 1u : clen;
                u32 in_l = raw_l - pre - suf;
                u32 nc = (caret > pre) ? caret - pre : (u32)cs;
                if (nc < (u32)cs) nc = (u32)cs;
                if (nc > (u32)cs + in_l) nc = (u32)cs + in_l;
                return wcm_emit_edit(text, (u32)cs, ce, text + (u32)cs + pre, in_l, nc, nc, ctx);
            }
            sf = ce;
        }
    }
    return 0;
}

static u32 wcm_toggle_at_cursor(const char *text, u32 len, u32 caret,
                                const char *path, u32 path_len) {
    u32 line_s = caret, line_e = caret, content_e, first_nw, last_nw, tr_len;
    const char *ctx;
    const char *un_p = 0;
    u32 un_l = 0, pre_rm = 0;

    while (line_s > 0 && text[line_s - 1u] != '\n') line_s--;
    while (line_e < len && text[line_e] != '\n') line_e++;
    content_e = (line_e > line_s && text[line_e - 1u] == '\r') ? line_e - 1u : line_e;
    first_nw = line_s;
    while (first_nw < content_e && (text[first_nw] == ' ' || text[first_nw] == '\t')) first_nw++;
    last_nw = content_e;
    while (last_nw > first_nw && (text[last_nw - 1u] == ' ' || text[last_nw - 1u] == '\t')) last_nw--;
    tr_len = last_nw - first_nw;

    if (tr_len == 0) {
        ctx = wcm_detect_context(text, len, caret, path, path_len);
    } else if ((tr_len >= 6u && w_strncasecmp(text + first_nw, "<style", 6u) == 0 && !w_is_name_char(text[first_nw + 6u])) ||
               (tr_len >= 7u && w_strncasecmp(text + first_nw, "</style", 7u) == 0) ||
               (tr_len >= 7u && w_strncasecmp(text + first_nw, "<script", 7u) == 0 && !w_is_name_char(text[first_nw + 7u])) ||
               (tr_len >= 8u && w_strncasecmp(text + first_nw, "</script", 8u) == 0) ||
               (tr_len >= 7u && w_memcmp(text + first_nw, "<!--", 4u) == 0 && w_memcmp(text + last_nw - 3u, "-->", 3u) == 0)) {
        ctx = "html";
    } else {
        u32 po = 0, pb = 0;
        if (wcm_match_single_line_php(text + first_nw, tr_len, &po, &pb)) ctx = "php";
        else ctx = wcm_detect_context(text, len, first_nw, path, path_len);
    }

    if (tr_len == 0) {
        u32 ind = first_nw - line_s, off;
        WBuf ins;
        wbuf_init(&ins, ind + 16u);
        wbuf_append(&ins, text + line_s, ind);
        if (w_strcmp(ctx, "html") == 0) { wbuf_puts(&ins, "<!--  -->"); off = 5u; }
        else if (w_strcmp(ctx, "css") == 0) { wbuf_puts(&ins, "/*  */"); off = 3u; }
        else { wbuf_puts(&ins, "// "); off = 3u; }
        return wcm_emit_edit(text, line_s, content_e, ins.data, ins.len, line_s + ind + off, line_s + ind + off, ctx);
    }

    if (wcm_try_uncomment_single(text + first_nw, tr_len, ctx, &un_p, &un_l, &pre_rm)) {
        u32 k = 0;
        while (k < un_l && w_is_space(un_p[k])) k++;
        if (k == un_l) {
            u32 pos = first_nw;
            return wcm_emit_edit(text, line_s, content_e, text + line_s, first_nw - line_s, pos, pos, ctx);
        }
        {
            u32 nc = (caret > pre_rm) ? caret - pre_rm : first_nw;
            if (nc < first_nw) nc = first_nw;
            if (nc > first_nw + un_l) nc = first_nw + un_l;
            return wcm_emit_edit(text, first_nw, last_nw, un_p, un_l, nc, nc, ctx);
        }
    }

    {
        u32 enc = wcm_try_enclosing_at_caret(text, len, line_s, content_e, caret, ctx);
        if (enc) return enc;
        enc = wcm_try_inline_at_caret(text, len, first_nw, last_nw, caret, ctx);
        if (enc) return enc;
    }

    if (wcm_is_line_ctx(ctx)) {
        u32 po = 0, pb = 0;
        if (wcm_match_single_line_php(text + line_s, content_e - line_s, &po, &pb)) {
            u32 bs = line_s + po;
            const char *ip = 0;
            u32 il = 0, pr = 0;
            if (wcm_try_uncomment_single(text + bs, pb, "php", &ip, &il, &pr)) {
                u32 nc = (caret > pr) ? caret - pr : bs;
                if (nc < bs) nc = bs;
                if (nc > bs + il) nc = bs + il;
                return wcm_emit_edit(text, bs, bs + pb, ip, il, nc, nc, "php");
            } else {
                WBuf wb;
                u32 nc = caret + 3u;
                wbuf_init(&wb, pb + 8u);
                wbuf_puts(&wb, "/* ");
                wbuf_append(&wb, text + bs, pb);
                wbuf_puts(&wb, " */");
                if (nc < bs) nc = bs;
                if (nc > bs + wb.len) nc = bs + wb.len;
                return wcm_emit_edit(text, bs, bs + pb, wb.data, wb.len, nc, nc, "php");
            }
        }
        {
            u32 nc = (caret < first_nw) ? first_nw + 3u : caret + 3u;
            return wcm_emit_edit(text, first_nw, first_nw, "// ", 3u, nc, nc, ctx);
        }
    }

    {
        const char *op = (w_strcmp(ctx, "html") == 0) ? "<!--" : "/*";
        const char *cl = (w_strcmp(ctx, "html") == 0) ? "-->" : "*/";
        u32 shift = w_strlen(op) + 1u;
        u32 bc = caret < first_nw ? first_nw : (caret > last_nw ? last_nw : caret);
        WBuf wb;
        wbuf_init(&wb, tr_len + 16u);
        wbuf_puts(&wb, op); wbuf_putc(&wb, ' ');
        wbuf_append(&wb, text + first_nw, tr_len);
        wbuf_putc(&wb, ' '); wbuf_puts(&wb, cl);
        return wcm_emit_edit(text, first_nw, last_nw, wb.data, wb.len, bc + shift, bc + shift, ctx);
    }
}

static u32 wcm_try_unwrap_surrounding(const char *text, u32 len, u32 start, u32 end,
                                      u32 first_nw, u32 last_nw) {
    u32 bef_l = start - first_nw, aft_l = last_nw - end, sel_l = end - start;
    const char *bef = text + first_nw, *aft = text + end, *sel = text + start;
    if ((bef_l == 4u || (bef_l == 5u && bef[4] == ' ')) && w_memcmp(bef, "<!--", 4u) == 0 &&
        ((aft_l == 3u && w_memcmp(aft, "-->", 3u) == 0) || (aft_l == 4u && w_memcmp(aft, " -->", 4u) == 0)) &&
        w_index_of(sel, sel_l, "-->", 0, sel_l) < 0) {
        return wcm_emit_edit(text, first_nw, last_nw, sel, sel_l, first_nw, first_nw + sel_l, "html");
    }
    if ((bef_l == 2u || (bef_l == 3u && bef[2] == ' ')) && w_memcmp(bef, "/*", 2u) == 0 &&
        ((aft_l == 2u && w_memcmp(aft, "*/", 2u) == 0) || (aft_l == 3u && w_memcmp(aft, " */", 3u) == 0)) &&
        w_index_of(sel, sel_l, "*/", 0, sel_l) < 0) {
        return wcm_emit_edit(text, first_nw, last_nw, sel, sel_l, first_nw, first_nw + sel_l, "css");
    }
    if ((bef_l == 2u || (bef_l == 3u && bef[2] == ' ')) && w_memcmp(bef, "//", 2u) == 0 && aft_l == 0) {
        return wcm_emit_edit(text, first_nw, last_nw, sel, sel_l, first_nw, first_nw + sel_l, "js");
    }
    (void)len;
    return 0;
}

static u32 wcm_toggle_partial_line(const char *text, u32 len, u32 start, u32 end,
                                   const char *path, u32 path_len) {
    u32 in_from = start, in_to = end, in_l;
    const char *ctx, *un_p = 0;
    u32 un_l = 0;
    while (in_from < end && (text[in_from] == ' ' || text[in_from] == '\t')) in_from++;
    while (in_to > in_from && (text[in_to - 1u] == ' ' || text[in_to - 1u] == '\t')) in_to--;
    if (in_from >= in_to) return wcm_toggle_at_cursor(text, len, start, path, path_len);
    in_l = in_to - in_from;
    ctx = wcm_detect_context(text, len, in_from, path, path_len);
    if (wcm_unwrap_block_text(text + in_from, in_l, &un_p, &un_l)) {
        return wcm_emit_edit(text, in_from, in_to, un_p, un_l, in_from, in_from + un_l, ctx);
    }
    if (wcm_is_line_ctx(ctx) && in_l >= 2u && w_memcmp(text + in_from, "//", 2u) == 0) {
        u32 pre = (in_l >= 3u && text[in_from + 2u] == ' ') ? 3u : 2u;
        return wcm_emit_edit(text, in_from, in_to, text + in_from + pre, in_l - pre,
                             in_from, in_from + (in_l - pre), ctx);
    }
    {
        const char *op = (w_strcmp(ctx, "html") == 0) ? "<!--" : "/*";
        const char *cl = (w_strcmp(ctx, "html") == 0) ? "-->" : "*/";
        WBuf wb;
        wbuf_init(&wb, in_l + 16u);
        wbuf_puts(&wb, op); wbuf_putc(&wb, ' ');
        wbuf_append(&wb, text + in_from, in_l);
        wbuf_putc(&wb, ' '); wbuf_puts(&wb, cl);
        return wcm_emit_edit(text, in_from, in_to, wb.data, wb.len, in_from, in_from + wb.len, ctx);
    }
}

static u32 wcm_build_block_res(const char *text, u32 b_start, u32 b_end,
                               const char *rep, u32 rep_len, u32 orig_s, u32 orig_e,
                               int had_nl, const char *ctx) {
    int single_line = (w_memchr(rep, '\n', rep_len) == 0);
    if (single_line && orig_s > b_start && !had_nl) {
        u32 ind = 0, tr = rep_len;
        while (ind < rep_len && (rep[ind] == ' ' || rep[ind] == '\t')) ind++;
        while (tr > ind && (rep[tr - 1u] == ' ' || rep[tr - 1u] == '\t')) tr--;
        return wcm_emit_edit(text, b_start, b_end, rep, rep_len, b_start + ind, b_start + tr, ctx);
    }
    {
        u32 ss = (orig_s <= b_start) ? b_start : (orig_s < b_start + rep_len ? orig_s : b_start + rep_len);
        u32 extra = (had_nl && orig_e > b_end) ? (orig_e - b_end) : 0;
        return wcm_emit_edit(text, b_start, b_end, rep, rep_len, ss, b_start + rep_len + extra, ctx);
    }
}

static u32 wcm_unwrap_sel_if_block(const char *text, u32 r_start, u32 r_end,
                                   u32 orig_e, int had_nl, const char *ctx) {
    u32 in_from = r_start, in_to = r_end;
    const char *un_p = 0;
    u32 un_l = 0, extra;
    while (in_from < r_end && w_is_space(text[in_from])) in_from++;
    while (in_to > in_from && w_is_space(text[in_to - 1u])) in_to--;
    if (in_from >= in_to) return 0;
    if (!wcm_unwrap_block_text(text + in_from, in_to - in_from, &un_p, &un_l)) return 0;
    extra = (had_nl && orig_e > r_end) ? (orig_e - r_end) : 0;
    return wcm_emit_edit(text, in_from, in_to, un_p, un_l, in_from, in_from + un_l + extra, ctx);
}

static u32 wcm_toggle_multi_slash(const char *text, u32 b_start, u32 b_end,
                                  u32 orig_s, u32 orig_e, int had_nl, const char *ctx) {
    u32 non_blank = 0, all_commented = 1, min_indent = 0xFFFFFFFFu, ls = b_start;
    int is_php = (w_strcmp(ctx, "php") == 0);
    WBuf wb;

    while (ls <= b_end) {
        u32 le = ls, ce, ind = 0, tr;
        while (le < b_end && text[le] != '\n') le++;
        ce = (le > ls && text[le - 1u] == '\r') ? le - 1u : le;
        while ( ls + ind < ce && (text[ls + ind] == ' ' || text[ls + ind] == '\t')) ind++;
        tr = ce;
        while (tr > ls + ind && w_is_space(text[tr - 1u])) tr--;
        if (tr > ls + ind) {
            const char *p = text + ls + ind;
            u32 pl = tr - (ls + ind);
            non_blank++;
            if (ind < min_indent) min_indent = ind;
            if (!((pl >= 2u && p[0] == '/' && p[1] == '/') ||
                  (is_php && pl >= 1u && p[0] == '#' && (pl == 1u || p[1] != '[')))) {
                all_commented = 0;
            }
        }
        if (le >= b_end) break;
        ls = le + 1u;
    }

    wbuf_init(&wb, (b_end - b_start) + 64u);
    ls = b_start;
    if (non_blank == 0) {
        while (ls <= b_end) {
            u32 le = ls, has_cr, ce;
            while (le < b_end && text[le] != '\n') le++;
            has_cr = (le > ls && text[le - 1u] == '\r') ? 1u : 0u;
            ce = le - has_cr;
            wbuf_append(&wb, text + ls, ce - ls);
            wbuf_puts(&wb, "// ");
            if (has_cr) wbuf_putc(&wb, '\r');
            if (le >= b_end) break;
            wbuf_putc(&wb, '\n');
            ls = le + 1u;
        }
        return wcm_build_block_res(text, b_start, b_end, wb.data, wb.len, orig_s, orig_e, had_nl, ctx);
    }

    if (all_commented) {
        while (ls <= b_end) {
            u32 le = ls, ind = 0;
            while (le < b_end && text[le] != '\n') le++;
            while (ls + ind < le && (text[ls + ind] == ' ' || text[ls + ind] == '\t')) ind++;
            wbuf_append(&wb, text + ls, ind);
            {
                const char *p = text + ls + ind;
                u32 rem = le - (ls + ind), skip = 0;
                if (rem >= 2u && p[0] == '/' && p[1] == '/') skip = (rem >= 3u && p[2] == ' ') ? 3u : 2u;
                else if (is_php && rem >= 1u && p[0] == '#') skip = (rem >= 2u && p[1] == ' ') ? 2u : 1u;
                wbuf_append(&wb, p + skip, rem - skip);
            }
            if (le >= b_end) break;
            wbuf_putc(&wb, '\n');
            ls = le + 1u;
        }
        return wcm_build_block_res(text, b_start, b_end, wb.data, wb.len, orig_s, orig_e, had_nl, ctx);
    }

    if (min_indent == 0xFFFFFFFFu) min_indent = 0;
    while (ls <= b_end) {
        u32 le = ls, ce, tr;
        while (le < b_end && text[le] != '\n') le++;
        ce = (le > ls && text[le - 1u] == '\r') ? le - 1u : le;
        tr = ls;
        while (tr < ce && w_is_space(text[tr])) tr++;
        if (tr == ce) {
            wbuf_append(&wb, text + ls, le - ls);
        } else {
            wbuf_append(&wb, text + ls, min_indent);
            wbuf_puts(&wb, "// ");
            wbuf_append(&wb, text + ls + min_indent, le - (ls + min_indent));
        }
        if (le >= b_end) break;
        wbuf_putc(&wb, '\n');
        ls = le + 1u;
    }
    return wcm_build_block_res(text, b_start, b_end, wb.data, wb.len, orig_s, orig_e, had_nl, ctx);
}

static u32 wcm_toggle_multi_block(const char *text, u32 len, u32 b_start, u32 b_end,
                                  u32 orig_s, u32 orig_e, u32 eff_e, int had_nl,
                                  const char *ctx) {
    const char *op = (w_strcmp(ctx, "html") == 0) ? "<!--" : "/*";
    const char *cl = (w_strcmp(ctx, "html") == 0) ? "-->" : "*/";
    u32 non_blank = 0, all_wrapped = 1, ls = b_start, in_from, in_to, in_l;
    const char *un_p = 0;
    u32 un_l = 0;

    while (ls <= b_end) {
        u32 le = ls, ce, fnw, lnw;
        while (le < b_end && text[le] != '\n') le++;
        ce = (le > ls && text[le - 1u] == '\r') ? le - 1u : le;
        fnw = ls;
        while (fnw < ce && (text[fnw] == ' ' || text[fnw] == '\t')) fnw++;
        lnw = ce;
        while (lnw > fnw && (text[lnw - 1u] == ' ' || text[lnw - 1u] == '\t')) lnw--;
        if (lnw > fnw) {
            non_blank++;
            if (!wcm_unwrap_block_text(text + fnw, lnw - fnw, &un_p, &un_l)) all_wrapped = 0;
        }
        if (le >= b_end) break;
        ls = le + 1u;
    }

    if (non_blank > 0 && all_wrapped) {
        WBuf wb;
        wbuf_init(&wb, (b_end - b_start) + 16u);
        ls = b_start;
        while (ls <= b_end) {
            u32 le = ls, has_cr, ce, fnw, lnw;
            while (le < b_end && text[le] != '\n') le++;
            has_cr = (le > ls && text[le - 1u] == '\r') ? 1u : 0u;
            ce = le - has_cr;
            fnw = ls;
            while (fnw < ce && (text[fnw] == ' ' || text[fnw] == '\t')) fnw++;
            lnw = ce;
            while (lnw > fnw && (text[lnw - 1u] == ' ' || text[lnw - 1u] == '\t')) lnw--;
            if (lnw == fnw || !wcm_unwrap_block_text(text + fnw, lnw - fnw, &un_p, &un_l)) {
                wbuf_append(&wb, text + ls, le - ls);
            } else {
                wbuf_append(&wb, text + ls, fnw - ls);
                wbuf_append(&wb, un_p, un_l);
                wbuf_append(&wb, text + lnw, ce - lnw);
                if (has_cr) wbuf_putc(&wb, '\r');
            }
            if (le >= b_end) break;
            wbuf_putc(&wb, '\n');
            ls = le + 1u;
        }
        return wcm_build_block_res(text, b_start, b_end, wb.data, wb.len, orig_s, orig_e, had_nl, ctx);
    }

    in_from = b_start;
    in_to = b_end;
    while (in_from < b_end && w_is_space(text[in_from])) in_from++;
    while (in_to > in_from && w_is_space(text[in_to - 1u])) in_to--;
    if (in_from >= in_to) return wcm_toggle_at_cursor(text, len, orig_s, "", 0);
    in_l = in_to - in_from;

    if (w_index_of(text, len, cl, in_from, in_to) >= 0) {
        WBuf wb;
        wbuf_init(&wb, (b_end - b_start) + 64u);
        ls = b_start;
        while (ls <= b_end) {
            u32 le = ls, has_cr, ce, fnw, lnw;
            while (le < b_end && text[le] != '\n') le++;
            has_cr = (le > ls && text[le - 1u] == '\r') ? 1u : 0u;
            ce = le - has_cr;
            fnw = ls;
            while (fnw < ce && (text[fnw] == ' ' || text[fnw] == '\t')) fnw++;
            lnw = ce;
            while (lnw > fnw && (text[lnw - 1u] == ' ' || text[lnw - 1u] == '\t')) lnw--;
            if (lnw == fnw || wcm_unwrap_block_text(text + fnw, lnw - fnw, &un_p, &un_l)) {
                wbuf_append(&wb, text + ls, le - ls);
            } else {
                wbuf_append(&wb, text + ls, fnw - ls);
                wbuf_puts(&wb, op); wbuf_putc(&wb, ' ');
                wbuf_append(&wb, text + fnw, lnw - fnw);
                wbuf_putc(&wb, ' '); wbuf_puts(&wb, cl);
                wbuf_append(&wb, text + lnw, ce - lnw);
                if (has_cr) wbuf_putc(&wb, '\r');
            }
            if (le >= b_end) break;
            wbuf_putc(&wb, '\n');
            ls = le + 1u;
        }
        return wcm_build_block_res(text, b_start, b_end, wb.data, wb.len, orig_s, orig_e, had_nl, ctx);
    }

    {
        WBuf wb;
        int full_line = (orig_s <= b_start && (had_nl || eff_e >= in_to));
        u32 ss = full_line ? orig_s : in_from;
        u32 extra = (had_nl && orig_e > in_to) ? (orig_e - in_to) : 0;
        wbuf_init(&wb, in_l + 16u);
        wbuf_puts(&wb, op); wbuf_putc(&wb, ' ');
        wbuf_append(&wb, text + in_from, in_l);
        wbuf_putc(&wb, ' '); wbuf_puts(&wb, cl);
        return wcm_emit_edit(text, in_from, in_to, wb.data, wb.len, ss, in_from + wb.len + extra, ctx);
    }
}

__attribute__((export_name("wasm_toggle_comment")))
u32 wasm_toggle_comment(const char *src, u32 src_len,
                        u32 u16_sel_start, u32 u16_sel_end,
                        const char *path, u32 path_len) {
    u32 a = wasm_u16_to_u8(src, src_len, u16_sel_start);
    u32 b = wasm_u16_to_u8(src, src_len, u16_sel_end);
    u32 start = a < b ? a : b;
    u32 end = a < b ? b : a;
    u32 eff_e, fl_s, fl_e, fl_ce, b_start, b_end, probe;
    int had_nl = 0;
    const char *ctx;

    if (start == end) {
        return wcm_toggle_at_cursor(src, src_len, start, path, path_len);
    }
    eff_e = end;
    if (end > start && src[end - 1u] == '\n') {
        had_nl = 1;
        eff_e = end - 1u;
        if (eff_e > start && src[eff_e - 1u] == '\r') eff_e--;
    }
    if (eff_e <= start) {
        return wcm_toggle_at_cursor(src, src_len, start, path, path_len);
    }

    fl_s = start;
    while (fl_s > 0 && src[fl_s - 1u] != '\n') fl_s--;
    fl_e = start;
    while (fl_e < src_len && src[fl_e] != '\n') fl_e++;
    fl_ce = (fl_e > fl_s && src[fl_e - 1u] == '\r') ? fl_e - 1u : fl_e;

    if (eff_e <= fl_ce && !had_nl) {
        u32 fnw = fl_s, lnw = fl_ce, sur;
        while (fnw < fl_ce && (src[fnw] == ' ' || src[fnw] == '\t')) fnw++;
        while (lnw > fnw && (src[lnw - 1u] == ' ' || src[lnw - 1u] == '\t')) lnw--;
        sur = wcm_try_unwrap_surrounding(src, src_len, start, eff_e, fnw, lnw);
        if (sur) return sur;
        if (start > fnw || eff_e < lnw) {
            return wcm_toggle_partial_line(src, src_len, start, eff_e, path, path_len);
        }
    }

    b_start = fl_s;
    b_end = eff_e;
    while (b_end < src_len && src[b_end] != '\n') b_end++;
    if (b_end > b_start && src[b_end - 1u] == '\r') b_end--;

    probe = start;
    while (probe < eff_e && w_is_space(src[probe])) probe++;
    if (probe >= eff_e) probe = start;
    ctx = wcm_detect_context(src, src_len, probe, path, path_len);
    if ((eff_e - probe >= 6u && w_strncasecmp(src + probe, "<style", 6u) == 0 && !w_is_name_char(src[probe + 6u])) ||
        (eff_e - probe >= 7u && w_strncasecmp(src + probe, "</style", 7u) == 0) ||
        (eff_e - probe >= 7u && w_strncasecmp(src + probe, "<script", 7u) == 0 && !w_is_name_char(src[probe + 7u])) ||
        (eff_e - probe >= 8u && w_strncasecmp(src + probe, "</script", 8u) == 0)) {
        ctx = "html";
    }

    {
        u32 u1 = wcm_unwrap_sel_if_block(src, start, eff_e, end, had_nl, ctx);
        if (u1) return u1;
        u1 = wcm_unwrap_sel_if_block(src, b_start, b_end, end, had_nl, ctx);
        if (u1) return u1;
    }
    if (wcm_is_line_ctx(ctx)) {
        return wcm_toggle_multi_slash(src, b_start, b_end, start, end, had_nl, ctx);
    }
    return wcm_toggle_multi_block(src, src_len, b_start, b_end, start, end, eff_e, had_nl, ctx);
}




