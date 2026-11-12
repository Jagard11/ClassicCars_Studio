#include "wasm_runtime.h"

__attribute__((export_name("wasm_sanitize_macro_slug")))
u32 wasm_sanitize_macro_slug(const char *src, u32 src_len,
                             const char *fb, u32 fb_len) {
    char clean[128], out[64];
    u32 s = 0, e = src_len, clen = 0, olen = 0, i;
    int last_dash = 1;
    while (s < e && w_is_space(src[s])) s++;
    while (e > s && w_is_space(src[e - 1u])) e--;
    for (i = s; i < e && clen + 1u < sizeof(clean); i++) {
        char c = src[i];
        if (w_is_ident_char(c) || c == '-') {
            clean[clen++] = c;
            last_dash = 0;
        } else if (!last_dash) {
            clean[clen++] = '-';
            last_dash = 1;
        }
    }
    while (clen > 0 && clean[clen - 1u] == '-') clen--;
    clean[clen] = '\0';

    if (clen > 0 && ((clean[0] >= 'A' && clean[0] <= 'Z') || (clean[0] >= 'a' && clean[0] <= 'z'))) {
        olen = clen > 52u ? 52u : clen;
        w_memcpy(out, clean, olen);
    } else {
        const char *f = (fb && fb_len > 0) ? fb : "macro";
        u32 fl = (fb && fb_len > 0) ? fb_len : 5u;
        const char *tail = clen > 0 ? clean : "item";
        u32 tl = clen > 0 ? clen : 4u;
        for (i = 0; i < fl && olen < 52u; i++) out[olen++] = f[i];
        if (olen < 52u) out[olen++] = '-';
        for (i = 0; i < tl && olen < 52u; i++) out[olen++] = tail[i];
    }
    out[olen] = '\0';
    {
        WBuf b;
        wbuf_init(&b, 64u);
        wbuf_append(&b, out, olen);
        wasm_set_result(b.data, b.len);
        return b.len;
    }
}

static int w_parse_price_digits(const char *raw, u32 len, char *digits_out, u32 *dlen_out,
                                u32 *int_len_out, u32 *frac_len_out) {
    u32 i, dlen = 0, dot_seen = 0, int_l = 0, frac_l = 0, non_zero = 0;
    for (i = 0; i < len && dlen < 48u; i++) {
        char c = raw[i];
        if (c == '$' || c == ',' || w_is_space(c)) continue;
        if (w_is_digit(c)) {
            if (c != '0') non_zero = 1;
            digits_out[dlen++] = c;
            if (dot_seen) frac_l++;
            else int_l++;
        } else if (c == '.' && !dot_seen && int_l > 0) {
            dot_seen = 1;
            digits_out[dlen++] = '.';
        } else {
            return 0;
        }
    }
    digits_out[dlen] = '\0';
    if (int_l == 0 || (dot_seen && frac_l == 0) || !non_zero) return 0;
    *dlen_out = dlen;
    *int_len_out = int_l;
    *frac_len_out = frac_l;
    return 1;
}

__attribute__((export_name("wasm_normalize_price")))
u32 wasm_normalize_price(const char *src, u32 src_len) {
    u32 s = 0, e = src_len;
    char digits[64];
    u32 dlen = 0, int_l = 0, frac_l = 0;
    WBuf b;
    while (s < e && w_is_space(src[s])) s++;
    while (e > s && w_is_space(src[e - 1u])) e--;
    wbuf_init(&b, (e - s) + 32u);
    if (e - s == 4u && w_strncasecmp(src + s, "sold", 4u) == 0) {
        wbuf_puts(&b, "SOLD");
    } else if (e - s == 16u && w_strncasecmp(src + s, "price on request", 16u) == 0) {
        wbuf_puts(&b, "Price on Request");
    } else if (w_parse_price_digits(src + s, e - s, digits, &dlen, &int_l, &frac_l)) {
        u32 is = 0, all_zero_frac = 1, k;
        while (is + 1u < int_l && digits[is] == '0') is++;
        for (k = 0; k < frac_l; k++) {
            if (digits[int_l + 1u + k] != '0') all_zero_frac = 0;
        }
        if (frac_l == 0 || all_zero_frac) {
            wbuf_append(&b, digits + is, int_l - is);
        } else {
            wbuf_append(&b, digits, dlen);
        }
    } else {
        wbuf_append(&b, src + s, e - s);
    }
    wasm_set_result(b.data, b.len);
    return b.len;
}

__attribute__((export_name("wasm_format_price_label")))
u32 wasm_format_price_label(const char *src, u32 src_len, int sold_flag) {
    u32 s = 0, e = src_len;
    char digits[64], fmt[96];
    u32 dlen = 0, int_l = 0, frac_l = 0;
    WBuf b;
    while (s < e && w_is_space(src[s])) s++;
    while (e > s && w_is_space(src[e - 1u])) e--;
    wbuf_init(&b, 160u);
    if (sold_flag || (e - s == 4u && w_strncasecmp(src + s, "sold", 4u) == 0)) {
        wbuf_puts(&b, "{\"key\":\"sold\",\"label\":\"SOLD\",\"tone\":\"danger\",\"short\":\"SOLD\"}");
    } else if (w_parse_price_digits(src + s, e - s, digits, &dlen, &int_l, &frac_l)) {
        u32 is = 0, rem_int, fw = 0, i, all_zero_frac = 1;
        while (is + 1u < int_l && digits[is] == '0') is++;
        rem_int = int_l - is;
        for (i = 0; i < frac_l; i++) {
            if (digits[int_l + 1u + i] != '0') all_zero_frac = 0;
        }
        fmt[fw++] = '$';
        for (i = 0; i < rem_int && fw + 4u < sizeof(fmt); i++) {
            if (i > 0 && ((rem_int - i) % 3u) == 0) fmt[fw++] = ',';
            fmt[fw++] = digits[is + i];
        }
        if (frac_l > 0 && !all_zero_frac && fw + 4u < sizeof(fmt)) {
            fmt[fw++] = '.';
            fmt[fw++] = digits[int_l + 1u];
            fmt[fw++] = (frac_l >= 2u) ? digits[int_l + 2u] : '0';
        }
        fmt[fw] = '\0';
        wbuf_puts(&b, "{\"key\":\"sale\",\"label\":\"For Sale \\u00b7 ");
        wbuf_append(&b, fmt, fw);
        wbuf_puts(&b, "\",\"tone\":\"ok\",\"short\":\"");
        wbuf_append(&b, fmt, fw);
        wbuf_puts(&b, "\"}");
    } else {
        wbuf_puts(&b, "{\"key\":\"por\",\"label\":\"Price on Request\",\"tone\":\"warn\",\"short\":\"On request\"}");
    }
    wasm_set_result(b.data, b.len);
    return b.len;
}

static i32 w_find_open_tag_end(const char *s, u32 len, const char *tag) {
    u32 tlen = w_strlen(tag), i;
    for (i = 0; i + 1u + tlen < len; i++) {
        if (s[i] == '<' && w_strncasecmp(s + i + 1u, tag, tlen) == 0) {
            char next = s[i + 1u + tlen];
            if (next == '>' || w_is_space(next)) {
                u32 k = i + 1u + tlen;
                while (k < len && s[k] != '>') k++;
                if (k < len) return (i32)(k + 1u);
            }
        }
    }
    return -1;
}

static i32 w_find_close_tag_start(const char *s, u32 len, const char *tag) {
    u32 tlen = w_strlen(tag), i;
    for (i = 0; i + 2u + tlen < len; i++) {
        if (s[i] == '<' && s[i + 1u] == '/' && w_strncasecmp(s + i + 2u, tag, tlen) == 0) {
            u32 k = i + 2u + tlen;
            while (k < len && w_is_space(s[k])) k++;
            if (k < len && s[k] == '>') return (i32)i;
        }
    }
    return -1;
}

__attribute__((export_name("wasm_insert_at_target")))
u32 wasm_insert_at_target(const char *src, u32 src_len,
                          const char *snip, u32 snip_len,
                          const char *target, u32 target_len,
                          i32 u16_cursor_offset) {
    u32 clean_len = snip_len;
    WBuf content, out;
    u32 u8_start = 0;

    while (clean_len > 0 && w_is_space(snip[clean_len - 1u])) clean_len--;
    wbuf_init(&content, src_len + clean_len + 16u);

    if (target_len == 6u && w_memcmp(target, "cursor", 6u) == 0 && u16_cursor_offset >= 0) {
        u32 cur = wasm_u16_to_u8(src, src_len, (u32)u16_cursor_offset);
        wbuf_append(&content, src, cur);
        if (cur > 0 && src[cur - 1u] != '\n') wbuf_putc(&content, '\n');
        u8_start = content.len;
        wbuf_append(&content, snip, clean_len);
        if (cur < src_len && src[cur] != '\n') wbuf_putc(&content, '\n');
        wbuf_append(&content, src + cur, src_len - cur);
    } else if (target_len == 8u && w_memcmp(target, "head-end", 8u) == 0) {
        i32 idx = w_find_close_tag_start(src, src_len, "head");
        if (idx >= 0) {
            wbuf_append(&content, src, (u32)idx);
            if (idx > 0 && src[idx - 1] != '\n') wbuf_putc(&content, '\n');
            u8_start = content.len;
            wbuf_append(&content, snip, clean_len);
            wbuf_putc(&content, '\n');
            wbuf_append(&content, src + idx, src_len - (u32)idx);
        } else goto top_insert;
    } else if (target_len == 10u && w_memcmp(target, "head-start", 10u) == 0) {
        i32 idx = w_find_open_tag_end(src, src_len, "head");
        if (idx >= 0) {
            wbuf_append(&content, src, (u32)idx);
            wbuf_putc(&content, '\n');
            u8_start = content.len;
            wbuf_append(&content, snip, clean_len);
            wbuf_append(&content, src + idx, src_len - (u32)idx);
        } else goto top_insert;
    } else if (target_len == 10u && w_memcmp(target, "body-start", 10u) == 0) {
        i32 idx = w_find_open_tag_end(src, src_len, "body");
        if (idx >= 0) {
            wbuf_append(&content, src, (u32)idx);
            wbuf_putc(&content, '\n');
            u8_start = content.len;
            wbuf_append(&content, snip, clean_len);
            wbuf_append(&content, src + idx, src_len - (u32)idx);
        } else goto top_insert;
    } else if (target_len == 8u && w_memcmp(target, "body-end", 8u) == 0) {
        i32 idx = w_find_close_tag_start(src, src_len, "body");
        if (idx >= 0) {
            wbuf_append(&content, src, (u32)idx);
            if (idx > 0 && src[idx - 1] != '\n') wbuf_putc(&content, '\n');
            u8_start = content.len;
            wbuf_append(&content, snip, clean_len);
            wbuf_putc(&content, '\n');
            wbuf_append(&content, src + idx, src_len - (u32)idx);
        } else goto top_insert;
    } else if (target_len == 6u && w_memcmp(target, "bottom", 6u) == 0) {
        wbuf_append(&content, src, src_len);
        if (src_len > 0 && src[src_len - 1u] != '\n') wbuf_putc(&content, '\n');
        u8_start = content.len;
        wbuf_append(&content, snip, clean_len);
        wbuf_putc(&content, '\n');
    } else {
    top_insert:
        u8_start = 0;
        wbuf_append(&content, snip, clean_len);
        wbuf_putc(&content, '\n');
        if (src_len > 0) wbuf_append(&content, src, src_len);
    }

    {
        u32 u16_s = wasm_u8_to_u16(content.data, u8_start);
        u32 u16_l = wasm_u8_to_u16(snip, clean_len);
        wbuf_init(&out, content.len + 128u);
        wbuf_puts(&out, "{\"content\":");
        wbuf_append_json_esc(&out, content.data, content.len);
        wbuf_puts(&out, ",\"range\":{\"startOffset\":");
        wbuf_put_int(&out, (i32)u16_s);
        wbuf_puts(&out, ",\"endOffset\":");
        wbuf_put_int(&out, (i32)(u16_s + u16_l));
        wbuf_puts(&out, "}}");
        wasm_set_result(out.data, out.len);
        return out.len;
    }
}

