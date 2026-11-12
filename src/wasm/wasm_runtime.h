#ifndef STUDIO_WASM_RUNTIME_H
#define STUDIO_WASM_RUNTIME_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef int                i32;

typedef struct WBuf {
    char *data;
    u32 len;
    u32 cap;
} WBuf;

void wasm_reset_heap(void);
u32 wasm_alloc(u32 bytes);
u32 wasm_result_ptr(void);
u32 wasm_result_len(void);
void wasm_set_result(const char *ptr, u32 len);

void wbuf_init(WBuf *b, u32 initial_cap);
void wbuf_putc(WBuf *b, char c);
void wbuf_append(WBuf *b, const char *s, u32 n);
void wbuf_puts(WBuf *b, const char *s);
void wbuf_put_int(WBuf *b, i32 val);
void wbuf_append_html_esc(WBuf *b, const char *s, u32 n);
void wbuf_append_json_esc(WBuf *b, const char *s, u32 n);

void *w_memcpy(void *dst, const void *src, u32 n);
void *w_memset(void *dst, int val, u32 n);
int w_memcmp(const void *a, const void *b, u32 n);
u32 w_strlen(const char *s);
int w_strcmp(const char *a, const char *b);
int w_strncmp(const char *a, const char *b, u32 n);
int w_strcasecmp(const char *a, const char *b);
int w_strncasecmp(const char *a, const char *b, u32 n);
const char *w_strchr(const char *s, char c);
const char *w_memchr(const char *s, char c, u32 n);
i32 w_index_of(const char *text, u32 text_len, const char *needle, u32 from, u32 end);
i32 w_index_of_ci(const char *text, u32 text_len, const char *needle, u32 from, u32 end);

int w_is_space(char ch);
int w_is_digit(char ch);
int w_is_hex(char ch);
int w_is_ident_start(char ch);
int w_is_ident_char(char ch);
int w_is_name_char(char ch);
int w_is_attr_start(char ch);
int w_is_attr_char(char ch);
int w_is_css_ident_start(char ch);
int w_is_css_ident_char(char ch);
char w_fold(char ch);

u32 wasm_u16_to_u8(const char *s, u32 byte_len, u32 u16_off);
u32 wasm_u8_to_u16(const char *s, u32 u8_off);

#endif
