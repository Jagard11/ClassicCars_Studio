#include "wasm_runtime.h"

extern u8 __heap_base;
static u32 g_heap_ptr = 0;
static u32 g_res_ptr = 0;
static u32 g_res_len = 0;

static void ensure_memory_capacity(u32 needed_end) {
    u32 cur_pages = __builtin_wasm_memory_size(0);
    u32 cur_bytes = cur_pages * 65536u;
    if (needed_end > cur_bytes) {
        u32 extra_bytes = needed_end - cur_bytes;
        u32 add_pages = (extra_bytes + 65535u) / 65536u;
        if (add_pages < 8u) add_pages = 8u;
        __builtin_wasm_memory_grow(0, add_pages);
    }
}

__attribute__((export_name("wasm_reset_heap")))
void wasm_reset_heap(void) {
    g_heap_ptr = (u32)(unsigned long)&__heap_base;
    if (g_heap_ptr < 4096u) g_heap_ptr = 4096u;
    g_heap_ptr = (g_heap_ptr + 7u) & ~7u;
}

__attribute__((export_name("wasm_alloc")))
u32 wasm_alloc(u32 bytes) {
    u32 aligned, out;
    if (g_heap_ptr == 0) wasm_reset_heap();
    aligned = (bytes + 7u) & ~7u;
    if (aligned == 0) aligned = 8u;
    ensure_memory_capacity(g_heap_ptr + aligned + 64u);
    out = g_heap_ptr;
    g_heap_ptr += aligned;
    return out;
}

__attribute__((export_name("wasm_result_ptr")))
u32 wasm_result_ptr(void) {
    return g_res_ptr;
}

__attribute__((export_name("wasm_result_len")))
u32 wasm_result_len(void) {
    return g_res_len;
}

void wasm_set_result(const char *ptr, u32 len) {
    g_res_ptr = (u32)(unsigned long)ptr;
    g_res_len = len;
}

void *w_memcpy(void *dst, const void *src, u32 n) {
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    u32 i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void *w_memset(void *dst, int val, u32 n) {
    u8 *d = (u8 *)dst;
    u8 v = (u8)val;
    u32 i;
    for (i = 0; i < n; i++) d[i] = v;
    return dst;
}

int w_memcmp(const void *a, const void *b, u32 n) {
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    u32 i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}

u32 w_strlen(const char *s) {
    u32 n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int w_strcmp(const char *a, const char *b) {
    u32 i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        i++;
    }
    return (int)(u8)a[i] - (int)(u8)b[i];
}

int w_strncmp(const char *a, const char *b, u32 n) {
    u32 i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(u8)a[i] - (int)(u8)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

char w_fold(char ch) {
    return (ch >= 'A' && ch <= 'Z') ? (char)(ch + 32) : ch;
}

int w_strcasecmp(const char *a, const char *b) {
    u32 i = 0;
    while (a[i] && b[i]) {
        char ca = w_fold(a[i]);
        char cb = w_fold(b[i]);
        if (ca != cb) return (int)(u8)ca - (int)(u8)cb;
        i++;
    }
    return (int)(u8)w_fold(a[i]) - (int)(u8)w_fold(b[i]);
}

int w_strncasecmp(const char *a, const char *b, u32 n) {
    u32 i;
    for (i = 0; i < n; i++) {
        char ca = w_fold(a[i]);
        char cb = w_fold(b[i]);
        if (ca != cb) return (int)(u8)ca - (int)(u8)cb;
        if (!a[i]) return 0;
    }
    return 0;
}

const char *w_strchr(const char *s, char c) {
    if (!s) return 0;
    while (*s) {
        if (*s == c) return s;
        s++;
    }
    return (c == '\0') ? s : 0;
}

const char *w_memchr(const char *s, char c, u32 n) {
    u32 i;
    for (i = 0; i < n; i++) {
        if (s[i] == c) return s + i;
    }
    return 0;
}

i32 w_index_of(const char *text, u32 text_len, const char *needle, u32 from, u32 end) {
    u32 nlen = w_strlen(needle);
    u32 k;
    if (end > text_len) end = text_len;
    if (nlen == 0) return (i32)from;
    if (from + nlen > end) return -1;
    for (k = from; k + nlen <= end; k++) {
        if (w_memcmp(text + k, needle, nlen) == 0) return (i32)k;
    }
    return -1;
}

i32 w_index_of_ci(const char *text, u32 text_len, const char *needle, u32 from, u32 end) {
    u32 nlen = w_strlen(needle);
    u32 k;
    if (end > text_len) end = text_len;
    if (nlen == 0) return (i32)from;
    if (from + nlen > end) return -1;
    for (k = from; k + nlen <= end; k++) {
        if (w_strncasecmp(text + k, needle, nlen) == 0) return (i32)k;
    }
    return -1;
}

int w_is_space(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\t' || ch == '\r' || ch == '\f';
}

int w_is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

int w_is_hex(char ch) {
    return w_is_digit(ch) || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

int w_is_ident_start(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

int w_is_ident_char(char ch) {
    return w_is_ident_start(ch) || w_is_digit(ch);
}

int w_is_name_char(char ch) {
    return w_is_ident_char(ch) || ch == '-' || ch == ':';
}

int w_is_attr_start(char ch) {
    return w_is_ident_start(ch) || ch == ':' || ch == '_';
}

int w_is_attr_char(char ch) {
    return w_is_name_char(ch);
}

int w_is_css_ident_start(char ch) {
    return w_is_ident_start(ch);
}

int w_is_css_ident_char(char ch) {
    return w_is_ident_char(ch) || ch == '-';
}

void wbuf_init(WBuf *b, u32 initial_cap) {
    u32 cap = initial_cap < 256u ? 256u : ((initial_cap + 7u) & ~7u);
    b->data = (char *)(unsigned long)wasm_alloc(cap);
    b->len = 0;
    b->cap = cap;
    b->data[0] = '\0';
}

static void wbuf_reserve(WBuf *b, u32 extra) {
    u32 need = b->len + extra + 1u;
    u32 new_cap, add_aligned;
    char *next;
    if (need <= b->cap) return;
    new_cap = b->cap ? b->cap * 2u : 512u;
    while (new_cap < need) new_cap *= 2u;
    new_cap = (new_cap + 7u) & ~7u;
    if ((u32)(unsigned long)b->data + b->cap == g_heap_ptr) {
        add_aligned = new_cap - b->cap;
        ensure_memory_capacity(g_heap_ptr + add_aligned + 64u);
        g_heap_ptr += add_aligned;
        b->cap = new_cap;
        return;
    }
    next = (char *)(unsigned long)wasm_alloc(new_cap);
    if (b->len > 0) w_memcpy(next, b->data, b->len + 1u);
    b->data = next;
    b->cap = new_cap;
}

void wbuf_append(WBuf *b, const char *s, u32 n) {
    if (n == 0) return;
    wbuf_reserve(b, n);
    w_memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void wbuf_puts(WBuf *b, const char *s) {
    if (s && s[0]) wbuf_append(b, s, w_strlen(s));
}

void wbuf_putc(WBuf *b, char c) {
    wbuf_reserve(b, 1u);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void wbuf_put_int(WBuf *b, i32 val) {
    char tmp[16];
    u32 n = 0, v;
    if (val < 0) {
        wbuf_putc(b, '-');
        v = (u32)(-val);
    } else {
        v = (u32)val;
    }
    if (v == 0) {
        wbuf_putc(b, '0');
        return;
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) wbuf_putc(b, tmp[--n]);
}

void wbuf_append_html_esc(WBuf *b, const char *s, u32 n) {
    u32 i;
    wbuf_reserve(b, n * 2u + 16u);
    for (i = 0; i < n; i++) {
        char ch = s[i];
        if (ch == '&') wbuf_append(b, "&amp;", 5u);
        else if (ch == '<') wbuf_append(b, "&lt;", 4u);
        else if (ch == '>') wbuf_append(b, "&gt;", 4u);
        else wbuf_putc(b, ch);
    }
}

void wbuf_append_json_esc(WBuf *b, const char *s, u32 n) {
    static const char hex[] = "0123456789abcdef";
    u32 i;
    wbuf_putc(b, '"');
    for (i = 0; i < n; i++) {
        u8 ch = (u8)s[i];
        if (ch == '"') wbuf_append(b, "\\\"", 2u);
        else if (ch == '\\') wbuf_append(b, "\\\\", 2u);
        else if (ch == '\n') wbuf_append(b, "\\n", 2u);
        else if (ch == '\r') wbuf_append(b, "\\r", 2u);
        else if (ch == '\t') wbuf_append(b, "\\t", 2u);
        else if (ch < 0x20u) {
            char esc[6] = {'\\', 'u', '0', '0', hex[(ch >> 4) & 0xF], hex[ch & 0xF]};
            wbuf_append(b, esc, 6u);
        } else {
            wbuf_putc(b, (char)ch);
        }
    }
    wbuf_putc(b, '"');
}

u32 wasm_u16_to_u8(const char *s, u32 byte_len, u32 u16_off) {
    u32 i = 0, u16_pos = 0;
    const u8 *p = (const u8 *)s;
    while (i < byte_len && u16_pos < u16_off) {
        u8 c = p[i];
        if (c < 0x80u) { i += 1u; u16_pos += 1u; }
        else if (c < 0xE0u) { i += 2u; u16_pos += 1u; }
        else if (c < 0xF0u) { i += 3u; u16_pos += 1u; }
        else { i += 4u; u16_pos += 2u; }
        if (i > byte_len) i = byte_len;
    }
    return i;
}

u32 wasm_u8_to_u16(const char *s, u32 u8_off) {
    u32 i = 0, u16_pos = 0;
    const u8 *p = (const u8 *)s;
    while (i < u8_off) {
        u8 c = p[i];
        if (c < 0x80u) { i += 1u; u16_pos += 1u; }
        else if (c < 0xE0u) { i += 2u; u16_pos += 1u; }
        else if (c < 0xF0u) { i += 3u; u16_pos += 1u; }
        else { i += 4u; u16_pos += 2u; }
    }
    return u16_pos;
}

