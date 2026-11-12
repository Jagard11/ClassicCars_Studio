#include "wasm_runtime.h"

#define W_DIFF_NONE     0
#define W_DIFF_ADDED    1
#define W_DIFF_MODIFIED 2

typedef struct WLineSlice {
    const char *ptr;
    u32 len;
    u32 hash;
} WLineSlice;

typedef struct WAnchorPair {
    i32 b;
    i32 c;
} WAnchorPair;

static u32 g_diff_line_count = 0;
static u32 g_diff_added = 0;
static u32 g_diff_modified = 0;
static u32 g_diff_deleted = 0;
static u32 g_diff_kinds_ptr = 0;
static u32 g_diff_del_after_ptr = 0;

__attribute__((export_name("wasm_diff_line_count")))
u32 wasm_diff_line_count(void) { return g_diff_line_count; }

__attribute__((export_name("wasm_diff_added_count")))
u32 wasm_diff_added_count(void) { return g_diff_added; }

__attribute__((export_name("wasm_diff_modified_count")))
u32 wasm_diff_modified_count(void) { return g_diff_modified; }

__attribute__((export_name("wasm_diff_deleted_count")))
u32 wasm_diff_deleted_count(void) { return g_diff_deleted; }

__attribute__((export_name("wasm_diff_line_kinds_ptr")))
u32 wasm_diff_line_kinds_ptr(void) { return g_diff_kinds_ptr; }

__attribute__((export_name("wasm_diff_deleted_after_ptr")))
u32 wasm_diff_deleted_after_ptr(void) { return g_diff_del_after_ptr; }

static char *w_norm_newlines(const char *s, u32 len, u32 *out_len) {
    char *out = (char *)(unsigned long)wasm_alloc(len + 1u);
    u32 i, w = 0;
    for (i = 0; i < len; i++) {
        if (s[i] == '\r') {
            if (i + 1u < len && s[i + 1u] == '\n') continue;
            out[w++] = '\n';
        } else {
            out[w++] = s[i];
        }
    }
    out[w] = '\0';
    *out_len = w;
    return out;
}

static WLineSlice *w_split_lines(const char *norm, u32 len, u32 *count_out) {
    u32 count = 1u, i, idx = 0, start = 0;
    WLineSlice *arr;
    for (i = 0; i < len; i++) {
        if (norm[i] == '\n') count++;
    }
    arr = (WLineSlice *)(unsigned long)wasm_alloc(count * sizeof(WLineSlice));
    for (i = 0; i <= len; i++) {
        if (i == len || norm[i] == '\n') {
            u32 l = i - start, k, h = 2166136261u;
            for (k = 0; k < l; k++) {
                h ^= (u8)norm[start + k];
                h *= 16777619u;
            }
            arr[idx].ptr = norm + start;
            arr[idx].len = l;
            arr[idx].hash = h;
            idx++;
            start = i + 1u;
        }
    }
    *count_out = count;
    return arr;
}

static int w_lines_eq(const WLineSlice *a, const WLineSlice *b) {
    return a->len == b->len && a->hash == b->hash &&
           (a->len == 0 || w_memcmp(a->ptr, b->ptr, a->len) == 0);
}

__attribute__((export_name("wasm_compute_line_diff")))
u32 wasm_compute_line_diff(const char *base_ptr, u32 base_len, int has_base,
                           const char *cur_ptr, u32 cur_len) {
    u32 c_norm_len = 0, b_norm_len = 0, c_count = 0, b_count = 0, i;
    char *c_norm = w_norm_newlines(cur_ptr ? cur_ptr : "", cur_len, &c_norm_len);
    WLineSlice *c_lines = w_split_lines(c_norm, c_norm_len, &c_count);
    u8 *line_kinds = (u8 *)(unsigned long)wasm_alloc(c_count ? c_count : 1u);
    u8 *del_after = (u8 *)(unsigned long)wasm_alloc(c_count ? c_count : 1u);
    WBuf del_json, hunk_json, out_json;
    u32 del_items = 0, hunk_items = 0;

    w_memset(line_kinds, 0, c_count ? c_count : 1u);
    w_memset(del_after, 0, c_count ? c_count : 1u);
    g_diff_line_count = c_count;
    g_diff_added = 0;
    g_diff_modified = 0;
    g_diff_deleted = 0;
    g_diff_kinds_ptr = (u32)(unsigned long)line_kinds;
    g_diff_del_after_ptr = (u32)(unsigned long)del_after;

    wbuf_init(&del_json, 256u);
    wbuf_init(&hunk_json, 512u);
    wbuf_putc(&del_json, '[');
    wbuf_putc(&hunk_json, '[');

    if (has_base) {
        char *b_norm = w_norm_newlines(base_ptr ? base_ptr : "", base_len, &b_norm_len);
        if (b_norm_len != c_norm_len || w_memcmp(b_norm, c_norm, c_norm_len) != 0) {
            WLineSlice *b_lines = w_split_lines(b_norm, b_norm_len, &b_count);
            u32 prefix = 0, min_len = b_count < c_count ? b_count : c_count;
            i32 b_end, c_end, m, n;
            WAnchorPair *anchors;
            u32 anchor_count = 0;

            while (prefix < min_len && w_lines_eq(&b_lines[prefix], &c_lines[prefix])) prefix++;
            b_end = (i32)b_count - 1;
            c_end = (i32)c_count - 1;
            while (b_end >= (i32)prefix && c_end >= (i32)prefix &&
                   w_lines_eq(&b_lines[b_end], &c_lines[c_end])) {
                b_end--;
                c_end--;
            }
            m = (b_end >= (i32)prefix) ? (b_end - (i32)prefix + 1) : 0;
            n = (c_end >= (i32)prefix) ? (c_end - (i32)prefix + 1) : 0;
            anchors = (WAnchorPair *)(unsigned long)wasm_alloc((u32)(m + n + 2) * sizeof(WAnchorPair));

            if (m > 0 && n > 0) {
                if ((u32)m * (u32)n <= 360000u) {
                    u32 stride = (u32)n + 1u;
                    u16 *dp = (u16 *)(unsigned long)wasm_alloc(((u32)m + 1u) * stride * sizeof(u16));
                    i32 r, c;
                    w_memset(dp, 0, ((u32)m + 1u) * stride * sizeof(u16));
                    for (r = m - 1; r >= 0; r--) {
                        for (c = n - 1; c >= 0; c--) {
                            if (w_lines_eq(&b_lines[(i32)prefix + r], &c_lines[(i32)prefix + c])) {
                                dp[(u32)r * stride + (u32)c] = (u16)(dp[(u32)(r + 1) * stride + (u32)(c + 1)] + 1u);
                            } else {
                                u16 dn = dp[(u32)(r + 1) * stride + (u32)c];
                                u16 rt = dp[(u32)r * stride + (u32)(c + 1)];
                                dp[(u32)r * stride + (u32)c] = dn >= rt ? dn : rt;
                            }
                        }
                    }
                    r = 0; c = 0;
                    while (r < m && c < n) {
                        if (w_lines_eq(&b_lines[(i32)prefix + r], &c_lines[(i32)prefix + c])) {
                            anchors[anchor_count].b = (i32)prefix + r;
                            anchors[anchor_count].c = (i32)prefix + c;
                            anchor_count++;
                            r++; c++;
                        } else if (dp[(u32)(r + 1) * stride + (u32)c] >= dp[(u32)r * stride + (u32)(c + 1)]) {
                            r++;
                        } else {
                            c++;
                        }
                    }
                } else {
                    i32 r = 0, c = 0;
                    while (r < m && c < n) {
                        if (w_lines_eq(&b_lines[(i32)prefix + r], &c_lines[(i32)prefix + c])) {
                            anchors[anchor_count].b = (i32)prefix + r;
                            anchors[anchor_count].c = (i32)prefix + c;
                            anchor_count++;
                            r++; c++;
                            continue;
                        }
                        {
                            int matched = 0;
                            i32 step, rem = (m - r) > (n - c) ? (m - r) : (n - c);
                            i32 lim = rem < 12 ? rem : 12;
                            for (step = 1; step <= lim; step++) {
                                if (c + step < n && w_lines_eq(&b_lines[(i32)prefix + r], &c_lines[(i32)prefix + c + step])) {
                                    c += step; matched = 1; break;
                                }
                                if (r + step < m && w_lines_eq(&b_lines[(i32)prefix + r + step], &c_lines[(i32)prefix + c])) {
                                    r += step; matched = 1; break;
                                }
                            }
                            if (!matched) { r++; c++; }
                        }
                    }
                }
            }

            {
                i32 prev_b = (i32)prefix - 1, prev_c = (i32)prefix - 1;
                anchors[anchor_count].b = b_end + 1;
                anchors[anchor_count].c = c_end + 1;
                anchor_count++;
                for (i = 0; i < anchor_count; i++) {
                    i32 next_b = anchors[i].b, next_c = anchors[i].c;
                    i32 del_span = next_b - prev_b - 1, ins_span = next_c - prev_c - 1;
                    if (del_span > 0 && ins_span == 0) {
                        i32 tidx = prev_c >= 0 ? prev_c : 0;
                        if (tidx >= (i32)c_count) tidx = (i32)c_count - 1;
                        if (tidx < 0) tidx = 0;
                        del_after[tidx] = 1;
                        g_diff_deleted += (u32)del_span;
                        if (del_items++) wbuf_putc(&del_json, ',');
                        wbuf_puts(&del_json, "{\"line\":"); wbuf_put_int(&del_json, tidx + 1);
                        wbuf_puts(&del_json, ",\"count\":"); wbuf_put_int(&del_json, del_span);
                        wbuf_putc(&del_json, '}');
                        if (hunk_items++) wbuf_putc(&hunk_json, ',');
                        wbuf_puts(&hunk_json, "{\"startLine\":"); wbuf_put_int(&hunk_json, tidx + 1);
                        wbuf_puts(&hunk_json, ",\"endLine\":"); wbuf_put_int(&hunk_json, tidx + 1);
                        wbuf_puts(&hunk_json, ",\"kind\":\"del\",\"count\":"); wbuf_put_int(&hunk_json, del_span);
                        wbuf_putc(&hunk_json, '}');
                    } else if (del_span == 0 && ins_span > 0) {
                        i32 sidx = prev_c + 1, eidx = next_c - 1, k;
                        for (k = sidx; k <= eidx; k++) { line_kinds[k] = W_DIFF_ADDED; g_diff_added++; }
                        if (hunk_items++) wbuf_putc(&hunk_json, ',');
                        wbuf_puts(&hunk_json, "{\"startLine\":"); wbuf_put_int(&hunk_json, sidx + 1);
                        wbuf_puts(&hunk_json, ",\"endLine\":"); wbuf_put_int(&hunk_json, eidx + 1);
                        wbuf_puts(&hunk_json, ",\"kind\":\"add\",\"count\":"); wbuf_put_int(&hunk_json, ins_span);
                        wbuf_putc(&hunk_json, '}');
                    } else if (del_span > 0 && ins_span > 0) {
                        i32 sidx = prev_c + 1, eidx = next_c - 1, k;
                        i32 mod_span = del_span < ins_span ? del_span : ins_span;
                        for (k = 0; k < ins_span; k++) {
                            if (k < mod_span) { line_kinds[sidx + k] = W_DIFF_MODIFIED; g_diff_modified++; }
                            else { line_kinds[sidx + k] = W_DIFF_ADDED; g_diff_added++; }
                        }
                        if (hunk_items++) wbuf_putc(&hunk_json, ',');
                        wbuf_puts(&hunk_json, "{\"startLine\":"); wbuf_put_int(&hunk_json, sidx + 1);
                        wbuf_puts(&hunk_json, ",\"endLine\":"); wbuf_put_int(&hunk_json, eidx + 1);
                        wbuf_puts(&hunk_json, ",\"kind\":\"mod\",\"count\":"); wbuf_put_int(&hunk_json, ins_span);
                        wbuf_putc(&hunk_json, '}');
                        if (del_span > ins_span) {
                            i32 extra = del_span - ins_span;
                            i32 tidx = eidx >= (i32)c_count ? (i32)c_count - 1 : (eidx < 0 ? 0 : eidx);
                            del_after[tidx] = 1;
                            g_diff_deleted += (u32)extra;
                            if (del_items++) wbuf_putc(&del_json, ',');
                            wbuf_puts(&del_json, "{\"line\":"); wbuf_put_int(&del_json, tidx + 1);
                            wbuf_puts(&del_json, ",\"count\":"); wbuf_put_int(&del_json, extra);
                            wbuf_putc(&del_json, '}');
                        }
                    }
                    prev_b = next_b;
                    prev_c = next_c;
                }
            }
        }
    }

    wbuf_putc(&del_json, ']');
    wbuf_putc(&hunk_json, ']');
    wbuf_init(&out_json, del_json.len + hunk_json.len + 64u);
    wbuf_puts(&out_json, "{\"deletions\":");
    wbuf_append(&out_json, del_json.data, del_json.len);
    wbuf_puts(&out_json, ",\"hunks\":");
    wbuf_append(&out_json, hunk_json.data, hunk_json.len);
    wbuf_putc(&out_json, '}');
    wasm_set_result(out_json.data, out_json.len);
    return out_json.len;
}

