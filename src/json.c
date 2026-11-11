#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "json.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"

typedef struct JsonMember {
    char *key;
    size_t key_len;
    JsonValue *value;
} JsonMember;

struct JsonValue {
    JsonType type;
    union {
        int boolean;
        char *number;
        struct {
            char *data;
            size_t len;
        } string;
        struct {
            JsonValue **items;
            size_t count;
            size_t cap;
        } array;
        struct {
            JsonMember *members;
            size_t count;
            size_t cap;
        } object;
    } u;
};

typedef struct Buf {
    char *data;
    size_t len;
    size_t cap;
} Buf;

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
    cap = buf->cap ? buf->cap : 64;
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

static int buf_append(Buf *buf, const char *text, size_t len) {
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

static int buf_putc(Buf *buf, char ch) {
    return buf_append(buf, &ch, 1);
}

static int buf_puts(Buf *buf, const char *text) {
    return buf_append(buf, text, strlen(text));
}

static void set_err(char *err, size_t err_sz, const char *message) {
    if (err && err_sz) {
        snprintf(err, err_sz, "%s", message);
    }
}

static int contains_seq(const char *text, size_t len, const char *needle) {
    size_t needle_len = strlen(needle);
    size_t i;

    if (needle_len == 0 || len < needle_len) {
        return 0;
    }
    for (i = 0; i + needle_len <= len; i++) {
        if (memcmp(text + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int hex_value(int ch) {
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
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        return 4;
    }
    return -1;
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
    if ((need == 2 && value < 0x80) || (need == 3 && value < 0x800) || (need == 4 && value < 0x10000) || value > 0x10FFFF) {
        return -1;
    }
    if (value >= 0xD800 && value <= 0xDFFF) {
        return -1;
    }
    *cp = value;
    *adv = need;
    return 0;
}

JsonType json_type(const JsonValue *value) {
    return value ? value->type : JSON_NULL;
}

JsonValue *json_null(void) {
    JsonValue *value = (JsonValue *)calloc(1, sizeof(*value));
    if (value) {
        value->type = JSON_NULL;
    }
    return value;
}

JsonValue *json_bool(int flag) {
    JsonValue *value = (JsonValue *)calloc(1, sizeof(*value));
    if (!value) {
        return NULL;
    }
    value->type = JSON_BOOL;
    value->u.boolean = flag ? 1 : 0;
    return value;
}

JsonValue *json_number(const char *lexeme) {
    JsonValue *value;

    if (!lexeme) {
        return NULL;
    }
    value = (JsonValue *)calloc(1, sizeof(*value));
    if (!value) {
        return NULL;
    }
    value->type = JSON_NUMBER;
    value->u.number = strdup(lexeme);
    if (!value->u.number) {
        free(value);
        return NULL;
    }
    return value;
}

JsonValue *json_number_int(long long number) {
    char lexeme[64];
    snprintf(lexeme, sizeof(lexeme), "%lld", number);
    return json_number(lexeme);
}

JsonValue *json_string(const char *text, size_t len) {
    JsonValue *value = (JsonValue *)calloc(1, sizeof(*value));
    if (!value) {
        return NULL;
    }
    value->type = JSON_STRING;
    value->u.string.data = (char *)malloc(len + 1);
    if (!value->u.string.data) {
        free(value);
        return NULL;
    }
    if (len && text) {
        memcpy(value->u.string.data, text, len);
    } else if (len) {
        memset(value->u.string.data, 0, len);
    }
    value->u.string.data[len] = '\0';
    value->u.string.len = len;
    return value;
}

JsonValue *json_string_cstr(const char *text) {
    if (!text) {
        text = "";
    }
    return json_string(text, strlen(text));
}

JsonValue *json_array(void) {
    JsonValue *value = (JsonValue *)calloc(1, sizeof(*value));
    if (value) {
        value->type = JSON_ARRAY;
    }
    return value;
}

JsonValue *json_object(void) {
    JsonValue *value = (JsonValue *)calloc(1, sizeof(*value));
    if (value) {
        value->type = JSON_OBJECT;
    }
    return value;
}

void json_free(JsonValue *value) {
    size_t i;

    if (!value) {
        return;
    }
    switch (value->type) {
    case JSON_NUMBER:
        free(value->u.number);
        break;
    case JSON_STRING:
        free(value->u.string.data);
        break;
    case JSON_ARRAY:
        for (i = 0; i < value->u.array.count; i++) {
            json_free(value->u.array.items[i]);
        }
        free(value->u.array.items);
        break;
    case JSON_OBJECT:
        for (i = 0; i < value->u.object.count; i++) {
            free(value->u.object.members[i].key);
            json_free(value->u.object.members[i].value);
        }
        free(value->u.object.members);
        break;
    case JSON_NULL:
    case JSON_BOOL:
        break;
    }
    free(value);
}

JsonValue *json_clone(const JsonValue *value) {
    size_t i;
    size_t n;
    JsonValue *copy;
    JsonValue *item;
    const char *key;

    if (!value) {
        return NULL;
    }
    switch (json_type(value)) {
    case JSON_NULL:
        return json_null();
    case JSON_BOOL:
        return json_bool(json_bool_value(value));
    case JSON_NUMBER:
        return json_number(json_number_lexeme(value));
    case JSON_STRING: {
        size_t len = 0;
        const char *text = json_string_get(value, &len);
        return json_string(text ? text : "", text ? len : 0);
    }
    case JSON_ARRAY:
        copy = json_array();
        if (!copy) {
            return NULL;
        }
        n = json_array_len(value);
        for (i = 0; i < n; i++) {
            item = json_clone(json_array_at(value, i));
            if (!item) {
                json_free(copy);
                return NULL;
            }
            if (json_array_add(copy, item) != 0) {
                json_free(copy);
                return NULL;
            }
        }
        return copy;
    case JSON_OBJECT:
        copy = json_object();
        if (!copy) {
            return NULL;
        }
        n = json_object_len(value);
        for (i = 0; i < n; i++) {
            key = json_object_key(value, i);
            item = json_clone(json_object_val(value, i));
            if (!key) {
                json_free(item);
                json_free(copy);
                return NULL;
            }
            if (!item || json_object_put(copy, key, item) != 0) {
                json_free(copy);
                return NULL;
            }
        }
        return copy;
    }
    return NULL;
}

JsonDoc *json_doc_new(JsonValue *root) {
    JsonDoc *doc = (JsonDoc *)calloc(1, sizeof(*doc));
    if (!doc) {
        json_free(root);
        return NULL;
    }
    doc->root = root;
    return doc;
}

void json_doc_free(JsonDoc *doc) {
    if (!doc) {
        return;
    }
    json_free(doc->root);
    free(doc);
}

int json_bool_value(const JsonValue *value) {
    return value && value->type == JSON_BOOL && value->u.boolean;
}

const char *json_number_lexeme(const JsonValue *value) {
    if (!value || value->type != JSON_NUMBER) {
        return NULL;
    }
    return value->u.number;
}

int json_number_set_lexeme(JsonValue *value, const char *lexeme) {
    char *copy;

    if (!value || value->type != JSON_NUMBER || !lexeme) {
        return -1;
    }
    copy = strdup(lexeme);
    if (!copy) {
        return -1;
    }
    free(value->u.number);
    value->u.number = copy;
    return 0;
}

int json_number_set_int(JsonValue *value, long long number) {
    char lexeme[64];
    snprintf(lexeme, sizeof(lexeme), "%lld", number);
    return json_number_set_lexeme(value, lexeme);
}

const char *json_string_get(const JsonValue *value, size_t *len) {
    if (!value || value->type != JSON_STRING) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }
    if (len) {
        *len = value->u.string.len;
    }
    return value->u.string.data ? value->u.string.data : "";
}

int json_string_set(JsonValue *value, const char *text, size_t len) {
    char *copy;

    if (!value || value->type != JSON_STRING) {
        return -1;
    }
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return -1;
    }
    if (len && text) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    free(value->u.string.data);
    value->u.string.data = copy;
    value->u.string.len = len;
    return 0;
}

int json_as_int(const JsonValue *value, long long *out) {
    const char *lexeme;
    char *end;
    long long number;

    if (!value || !out) {
        return -1;
    }
    if (value->type == JSON_BOOL) {
        *out = value->u.boolean ? 1 : 0;
        return 0;
    }
    if (value->type != JSON_NUMBER && value->type != JSON_STRING) {
        return -1;
    }
    lexeme = value->type == JSON_NUMBER ? value->u.number : value->u.string.data;
    if (!lexeme || !lexeme[0]) {
        return -1;
    }
    number = strtoll(lexeme, &end, 10);
    if (end == lexeme || *end != '\0') {
        return -1;
    }
    *out = number;
    return 0;
}

char *json_as_text(const JsonValue *value) {
    if (!value) {
        return NULL;
    }
    if (value->type == JSON_STRING) {
        return strndup(value->u.string.data ? value->u.string.data : "", value->u.string.len);
    }
    if (value->type == JSON_NUMBER) {
        return strdup(value->u.number ? value->u.number : "");
    }
    if (value->type == JSON_BOOL) {
        return strdup(value->u.boolean ? "true" : "false");
    }
    if (value->type == JSON_NULL) {
        return strdup("");
    }
    return NULL;
}

size_t json_array_len(const JsonValue *array) {
    if (!array || array->type != JSON_ARRAY) {
        return 0;
    }
    return array->u.array.count;
}

JsonValue *json_array_at(const JsonValue *array, size_t index) {
    if (!array || array->type != JSON_ARRAY || index >= array->u.array.count) {
        return NULL;
    }
    return array->u.array.items[index];
}

int json_array_add(JsonValue *array, JsonValue *item) {
    JsonValue **next;

    if (!item) {
        return -1;
    }
    if (!array || array->type != JSON_ARRAY) {
        json_free(item);
        return -1;
    }
    if (array->u.array.count == array->u.array.cap) {
        size_t cap = array->u.array.cap ? array->u.array.cap * 2 : 8;
        next = (JsonValue **)realloc(array->u.array.items, cap * sizeof(*next));
        if (!next) {
            json_free(item);
            return -1;
        }
        array->u.array.items = next;
        array->u.array.cap = cap;
    }
    array->u.array.items[array->u.array.count++] = item;
    return 0;
}

JsonValue *json_array_detach(JsonValue *array, size_t index) {
    JsonValue *item;

    if (!array || array->type != JSON_ARRAY || index >= array->u.array.count) {
        return NULL;
    }
    item = array->u.array.items[index];
    memmove(&array->u.array.items[index], &array->u.array.items[index + 1],
            (array->u.array.count - index - 1) * sizeof(item));
    array->u.array.count--;
    return item;
}

void json_array_clear(JsonValue *array) {
    size_t i;

    if (!array || array->type != JSON_ARRAY) {
        return;
    }
    for (i = 0; i < array->u.array.count; i++) {
        json_free(array->u.array.items[i]);
        array->u.array.items[i] = NULL;
    }
    array->u.array.count = 0;
}

void json_array_transfer(JsonValue *dst, JsonValue *src) {
    if (!dst || !src || dst->type != JSON_ARRAY || src->type != JSON_ARRAY || dst == src) {
        return;
    }
    json_array_clear(dst);
    free(dst->u.array.items);
    dst->u.array.items = src->u.array.items;
    dst->u.array.count = src->u.array.count;
    dst->u.array.cap = src->u.array.cap;
    src->u.array.items = NULL;
    src->u.array.count = 0;
    src->u.array.cap = 0;
}

static int key_eq(const JsonMember *member, const char *key) {
    size_t len = strlen(key);
    return member->key_len == len && memcmp(member->key, key, len) == 0;
}

size_t json_object_len(const JsonValue *object) {
    if (!object || object->type != JSON_OBJECT) {
        return 0;
    }
    return object->u.object.count;
}

const char *json_object_key(const JsonValue *object, size_t index) {
    if (!object || object->type != JSON_OBJECT || index >= object->u.object.count) {
        return NULL;
    }
    return object->u.object.members[index].key;
}

JsonValue *json_object_val(const JsonValue *object, size_t index) {
    if (!object || object->type != JSON_OBJECT || index >= object->u.object.count) {
        return NULL;
    }
    return object->u.object.members[index].value;
}

JsonValue *json_object_get(const JsonValue *object, const char *key) {
    size_t i;
    JsonValue *found = NULL;

    if (!object || object->type != JSON_OBJECT || !key) {
        return NULL;
    }
    for (i = 0; i < object->u.object.count; i++) {
        if (key_eq(&object->u.object.members[i], key)) {
            found = object->u.object.members[i].value;
        }
    }
    return found;
}

int json_object_has(const JsonValue *object, const char *key) {
    size_t i;

    if (!object || object->type != JSON_OBJECT || !key) {
        return 0;
    }
    for (i = 0; i < object->u.object.count; i++) {
        if (key_eq(&object->u.object.members[i], key)) {
            return 1;
        }
    }
    return 0;
}

static int object_grow(JsonValue *object) {
    size_t cap;
    JsonMember *next;

    if (object->u.object.count < object->u.object.cap) {
        return 0;
    }
    cap = object->u.object.cap ? object->u.object.cap * 2 : 8;
    next = (JsonMember *)realloc(object->u.object.members, cap * sizeof(*next));
    if (!next) {
        return -1;
    }
    object->u.object.members = next;
    object->u.object.cap = cap;
    return 0;
}

static int object_insert(JsonValue *object, size_t index, const char *key, JsonValue *value) {
    char *key_copy;
    size_t key_len;

    if (object_grow(object) != 0) {
        json_free(value);
        return -1;
    }
    key_len = strlen(key);
    key_copy = (char *)malloc(key_len + 1);
    if (!key_copy) {
        json_free(value);
        return -1;
    }
    memcpy(key_copy, key, key_len + 1);
    if (index > object->u.object.count) {
        index = object->u.object.count;
    }
    memmove(&object->u.object.members[index + 1], &object->u.object.members[index],
            (object->u.object.count - index) * sizeof(JsonMember));
    object->u.object.members[index].key = key_copy;
    object->u.object.members[index].key_len = key_len;
    object->u.object.members[index].value = value;
    object->u.object.count++;
    return 0;
}

int json_object_put_after(JsonValue *object, const char *after_key, const char *key, JsonValue *value) {
    size_t i;
    size_t insert_at;

    if (!value) {
        return -1;
    }
    if (!object || object->type != JSON_OBJECT || !key) {
        json_free(value);
        return -1;
    }
    for (i = object->u.object.count; i > 0; i--) {
        if (key_eq(&object->u.object.members[i - 1], key)) {
            json_free(object->u.object.members[i - 1].value);
            object->u.object.members[i - 1].value = value;
            return 0;
        }
    }
    insert_at = object->u.object.count;
    if (after_key) {
        for (i = 0; i < object->u.object.count; i++) {
            if (key_eq(&object->u.object.members[i], after_key)) {
                insert_at = i + 1;
                break;
            }
        }
    }
    return object_insert(object, insert_at, key, value);
}

int json_object_put(JsonValue *object, const char *key, JsonValue *value) {
    return json_object_put_after(object, NULL, key, value);
}

void json_object_remove(JsonValue *object, const char *key) {
    size_t read_at;
    size_t write_at = 0;

    if (!object || object->type != JSON_OBJECT || !key) {
        return;
    }
    for (read_at = 0; read_at < object->u.object.count; read_at++) {
        if (key_eq(&object->u.object.members[read_at], key)) {
            free(object->u.object.members[read_at].key);
            json_free(object->u.object.members[read_at].value);
            continue;
        }
        if (write_at != read_at) {
            object->u.object.members[write_at] = object->u.object.members[read_at];
        }
        write_at++;
    }
    object->u.object.count = write_at;
}

typedef struct Parser {
    const char *text;
    size_t len;
    size_t index;
    int depth;
    char err[192];
} Parser;

static void parser_fail(Parser *parser, const char *message) {
    if (!parser->err[0]) {
        snprintf(parser->err, sizeof(parser->err), "%s at byte %zu", message, parser->index);
    }
}

static void skip_ws(Parser *parser) {
    while (parser->index < parser->len) {
        char ch = parser->text[parser->index];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
            break;
        }
        parser->index++;
    }
}

static JsonValue *parse_value(Parser *parser);

static int append_codepoint(Buf *buf, unsigned cp) {
    char encoded[4];
    int n = utf8_encode(cp, encoded);
    if (n < 0) {
        return -1;
    }
    return buf_append(buf, encoded, (size_t)n);
}

static JsonValue *parse_string(Parser *parser) {
    Buf buf;
    JsonValue *value;

    buf_init(&buf);
    if (parser->index >= parser->len || parser->text[parser->index] != '"') {
        parser_fail(parser, "expected string");
        return NULL;
    }
    parser->index++;
    while (parser->index < parser->len) {
        unsigned char ch = (unsigned char)parser->text[parser->index];
        if (ch == '"') {
            parser->index++;
            value = json_string(buf.data ? buf.data : "", buf.len);
            buf_free(&buf);
            return value;
        }
        if (ch == '\\') {
            unsigned cp;
            int hex[4];
            int i;

            if (parser->index + 1 >= parser->len) {
                parser_fail(parser, "unterminated escape");
                buf_free(&buf);
                return NULL;
            }
            parser->index++;
            ch = (unsigned char)parser->text[parser->index++];
            switch (ch) {
            case '"':
            case '\\':
            case '/':
                if (buf_putc(&buf, (char)ch) != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            case 'b':
                if (buf_putc(&buf, '\b') != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            case 'f':
                if (buf_putc(&buf, '\f') != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            case 'n':
                if (buf_putc(&buf, '\n') != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            case 'r':
                if (buf_putc(&buf, '\r') != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            case 't':
                if (buf_putc(&buf, '\t') != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            case 'u':
                if (parser->index + 4 > parser->len) {
                    parser_fail(parser, "short unicode escape");
                    buf_free(&buf);
                    return NULL;
                }
                cp = 0;
                for (i = 0; i < 4; i++) {
                    hex[i] = hex_value((unsigned char)parser->text[parser->index + (size_t)i]);
                    if (hex[i] < 0) {
                        parser_fail(parser, "invalid unicode escape");
                        buf_free(&buf);
                        return NULL;
                    }
                    cp = (cp << 4) | (unsigned)hex[i];
                }
                parser->index += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && parser->index + 6 <= parser->len &&
                    parser->text[parser->index] == '\\' && parser->text[parser->index + 1] == 'u') {
                    unsigned low = 0;
                    int valid = 1;
                    for (i = 0; i < 4; i++) {
                        int digit = hex_value((unsigned char)parser->text[parser->index + 2 + (size_t)i]);
                        if (digit < 0) {
                            valid = 0;
                            break;
                        }
                        low = (low << 4) | (unsigned)digit;
                    }
                    if (valid && low >= 0xDC00 && low <= 0xDFFF) {
                        parser->index += 6;
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                    }
                }
                if (append_codepoint(&buf, cp) != 0) {
                    buf_free(&buf);
                    return NULL;
                }
                break;
            default:
                parser_fail(parser, "invalid escape");
                buf_free(&buf);
                return NULL;
            }
            continue;
        }
        if (ch < 0x20) {
            parser_fail(parser, "raw control character in string");
            buf_free(&buf);
            return NULL;
        }
        if (ch < 0x80) {
            if (buf_putc(&buf, (char)ch) != 0) {
                buf_free(&buf);
                return NULL;
            }
            parser->index++;
            continue;
        }
        {
            unsigned cp = 0;
            size_t adv = 0;
            if (utf8_decode((const unsigned char *)parser->text + parser->index, parser->len - parser->index, &cp, &adv) != 0) {
                parser_fail(parser, "invalid UTF-8 in string");
                buf_free(&buf);
                return NULL;
            }
            if (buf_append(&buf, parser->text + parser->index, adv) != 0) {
                buf_free(&buf);
                return NULL;
            }
            parser->index += adv;
        }
    }
    parser_fail(parser, "unterminated string");
    buf_free(&buf);
    return NULL;
}

static JsonValue *parse_number(Parser *parser) {
    size_t start = parser->index;
    size_t i = parser->index;
    char *lexeme;
    JsonValue *value;

    if (i < parser->len && parser->text[i] == '-') {
        i++;
    }
    if (i >= parser->len || !isdigit((unsigned char)parser->text[i])) {
        parser_fail(parser, "invalid number");
        return NULL;
    }
    if (parser->text[i] == '0') {
        i++;
    } else {
        while (i < parser->len && isdigit((unsigned char)parser->text[i])) {
            i++;
        }
    }
    if (i < parser->len && parser->text[i] == '.') {
        size_t frac = i + 1;
        if (frac >= parser->len || !isdigit((unsigned char)parser->text[frac])) {
            parser_fail(parser, "invalid fraction");
            return NULL;
        }
        i = frac + 1;
        while (i < parser->len && isdigit((unsigned char)parser->text[i])) {
            i++;
        }
    }
    if (i < parser->len && (parser->text[i] == 'e' || parser->text[i] == 'E')) {
        size_t exp = i + 1;
        if (exp < parser->len && (parser->text[exp] == '+' || parser->text[exp] == '-')) {
            exp++;
        }
        if (exp >= parser->len || !isdigit((unsigned char)parser->text[exp])) {
            parser_fail(parser, "invalid exponent");
            return NULL;
        }
        i = exp + 1;
        while (i < parser->len && isdigit((unsigned char)parser->text[i])) {
            i++;
        }
    }
    lexeme = (char *)malloc(i - start + 1);
    if (!lexeme) {
        parser_fail(parser, "out of memory");
        return NULL;
    }
    memcpy(lexeme, parser->text + start, i - start);
    lexeme[i - start] = '\0';
    parser->index = i;
    value = json_number(lexeme);
    free(lexeme);
    return value;
}

static JsonValue *parse_literal(Parser *parser, const char *literal, JsonValue *value) {
    size_t n = strlen(literal);
    if (parser->index + n > parser->len || memcmp(parser->text + parser->index, literal, n) != 0) {
        json_free(value);
        parser_fail(parser, "invalid literal");
        return NULL;
    }
    parser->index += n;
    return value;
}

static JsonValue *parse_array(Parser *parser) {
    JsonValue *array = json_array();

    if (!array) {
        return NULL;
    }
    parser->index++;
    skip_ws(parser);
    if (parser->index < parser->len && parser->text[parser->index] == ']') {
        parser->index++;
        return array;
    }
    while (parser->index < parser->len) {
        JsonValue *item = parse_value(parser);
        if (!item || json_array_add(array, item) != 0) {
            json_free(array);
            return NULL;
        }
        skip_ws(parser);
        if (parser->index < parser->len && parser->text[parser->index] == ',') {
            parser->index++;
            skip_ws(parser);
            continue;
        }
        if (parser->index < parser->len && parser->text[parser->index] == ']') {
            parser->index++;
            return array;
        }
        parser_fail(parser, "expected comma or end of array");
        json_free(array);
        return NULL;
    }
    parser_fail(parser, "unterminated array");
    json_free(array);
    return NULL;
}

static JsonValue *parse_object(Parser *parser) {
    JsonValue *object = json_object();

    if (!object) {
        return NULL;
    }
    parser->index++;
    skip_ws(parser);
    if (parser->index < parser->len && parser->text[parser->index] == '}') {
        parser->index++;
        return object;
    }
    while (parser->index < parser->len) {
        JsonValue *key;
        JsonValue *item;
        size_t key_len = 0;
        const char *key_text;

        skip_ws(parser);
        key = parse_string(parser);
        if (!key) {
            json_free(object);
            return NULL;
        }
        key_text = json_string_get(key, &key_len);
        skip_ws(parser);
        if (parser->index >= parser->len || parser->text[parser->index] != ':') {
            parser_fail(parser, "expected colon");
            json_free(key);
            json_free(object);
            return NULL;
        }
        parser->index++;
        item = parse_value(parser);
        if (!item) {
            json_free(key);
            json_free(object);
            return NULL;
        }
        if (object_insert(object, object->u.object.count, key_text ? key_text : "", item) != 0) {
            json_free(key);
            json_free(object);
            return NULL;
        }
        json_free(key);
        skip_ws(parser);
        if (parser->index < parser->len && parser->text[parser->index] == ',') {
            parser->index++;
            continue;
        }
        if (parser->index < parser->len && parser->text[parser->index] == '}') {
            parser->index++;
            return object;
        }
        parser_fail(parser, "expected comma or end of object");
        json_free(object);
        return NULL;
    }
    parser_fail(parser, "unterminated object");
    json_free(object);
    return NULL;
}

static JsonValue *parse_value(Parser *parser) {
    char ch;

    if (parser->depth > 64) {
        parser_fail(parser, "JSON nesting is too deep");
        return NULL;
    }
    skip_ws(parser);
    if (parser->index >= parser->len) {
        parser_fail(parser, "unexpected end of JSON");
        return NULL;
    }
    ch = parser->text[parser->index];
    parser->depth++;
    if (ch == '"') {
        JsonValue *value = parse_string(parser);
        parser->depth--;
        return value;
    }
    if (ch == '{') {
        JsonValue *value = parse_object(parser);
        parser->depth--;
        return value;
    }
    if (ch == '[') {
        JsonValue *value = parse_array(parser);
        parser->depth--;
        return value;
    }
    if (ch == 't') {
        JsonValue *value = parse_literal(parser, "true", json_bool(1));
        parser->depth--;
        return value;
    }
    if (ch == 'f') {
        JsonValue *value = parse_literal(parser, "false", json_bool(0));
        parser->depth--;
        return value;
    }
    if (ch == 'n') {
        JsonValue *value = parse_literal(parser, "null", json_null());
        parser->depth--;
        return value;
    }
    if (ch == '-' || isdigit((unsigned char)ch)) {
        JsonValue *value = parse_number(parser);
        parser->depth--;
        return value;
    }
    parser->depth--;
    parser_fail(parser, "unexpected JSON value");
    return NULL;
}

static int scan_unicode_style(const char *text, size_t len, int *escape_unicode, int *upper) {
    size_t i;

    *escape_unicode = contains_seq(text, len, "\\u00") || contains_seq(text, len, "\\u20");
    *upper = 0;
    for (i = 0; i + 6 <= len; i++) {
        int digit;
        if (text[i] != '\\' || text[i + 1] != 'u') {
            continue;
        }
        for (digit = 0; digit < 4; digit++) {
            unsigned char ch = (unsigned char)text[i + 2 + (size_t)digit];
            if (hex_value(ch) < 0) {
                break;
            }
            if (ch >= 'A' && ch <= 'F') {
                *upper = 1;
            }
        }
    }
    return 0;
}

JsonDoc *json_parse(const char *text, size_t len, char *err, size_t err_sz) {
    Parser parser;
    JsonDoc *doc;
    JsonValue *root;

    memset(&parser, 0, sizeof(parser));
    if (!text) {
        set_err(err, err_sz, "JSON text is missing");
        return NULL;
    }
    parser.text = text;
    parser.len = len;
    root = parse_value(&parser);
    if (!root) {
        set_err(err, err_sz, parser.err[0] ? parser.err : "JSON parse failed");
        return NULL;
    }
    skip_ws(&parser);
    if (parser.index != parser.len) {
        json_free(root);
        set_err(err, err_sz, "trailing data after JSON value");
        return NULL;
    }
    doc = json_doc_new(root);
    if (!doc) {
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    scan_unicode_style(text, len, &doc->escape_unicode, &doc->unicode_hex_upper);
    return doc;
}

static int emit_u(Buf *buf, unsigned cp, int upper) {
    const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[6];

    tmp[0] = '\\';
    tmp[1] = 'u';
    tmp[2] = hex[(cp >> 12) & 0xF];
    tmp[3] = hex[(cp >> 8) & 0xF];
    tmp[4] = hex[(cp >> 4) & 0xF];
    tmp[5] = hex[cp & 0xF];
    return buf_append(buf, tmp, 6);
}

static int emit_unicode(Buf *buf, unsigned cp, int upper) {
    if (cp <= 0xFFFF) {
        return emit_u(buf, cp, upper);
    }
    cp -= 0x10000;
    if (emit_u(buf, 0xD800 + (cp >> 10), upper) != 0) {
        return -1;
    }
    return emit_u(buf, 0xDC00 + (cp & 0x3FF), upper);
}

static int emit_string_bytes(Buf *buf, const char *text, size_t len, const JsonDoc *doc) {
    size_t i = 0;

    if (buf_putc(buf, '"') != 0) {
        return -1;
    }
    while (i < len) {
        unsigned char ch = (unsigned char)text[i];
        if (ch < 0x80) {
            const char *escaped = NULL;
            char tmp[7];
            if (ch == '"' || ch == '\\') {
                if (buf_putc(buf, '\\') != 0 || buf_putc(buf, (char)ch) != 0) {
                    return -1;
                }
                i++;
                continue;
            }
            if (ch == '/') {
                if (buf_puts(buf, "\\/") != 0) {
                    return -1;
                }
                i++;
                continue;
            }
            if (ch == '\b') {
                escaped = "\\b";
            } else if (ch == '\f') {
                escaped = "\\f";
            } else if (ch == '\n') {
                escaped = "\\n";
            } else if (ch == '\r') {
                escaped = "\\r";
            } else if (ch == '\t') {
                escaped = "\\t";
            }
            if (escaped) {
                if (buf_puts(buf, escaped) != 0) {
                    return -1;
                }
                i++;
                continue;
            }
            if (ch < 0x20) {
                snprintf(tmp, sizeof(tmp), "\\u%04x", ch);
                if (doc->unicode_hex_upper) {
                    size_t k;
                    for (k = 0; tmp[k]; k++) {
                        if (tmp[k] >= 'a' && tmp[k] <= 'f') {
                            tmp[k] = (char)(tmp[k] - 'a' + 'A');
                        }
                    }
                }
                if (buf_puts(buf, tmp) != 0) {
                    return -1;
                }
                i++;
                continue;
            }
            if (buf_putc(buf, (char)ch) != 0) {
                return -1;
            }
            i++;
            continue;
        }
        if (doc->escape_unicode) {
            unsigned cp = 0;
            size_t adv = 0;
            if (utf8_decode((const unsigned char *)text + i, len - i, &cp, &adv) != 0) {
                return -1;
            }
            if (emit_unicode(buf, cp, doc->unicode_hex_upper) != 0) {
                return -1;
            }
            i += adv;
        } else {
            if (buf_putc(buf, (char)ch) != 0) {
                return -1;
            }
            i++;
        }
    }
    return buf_putc(buf, '"');
}

static int emit_value(Buf *buf, const JsonValue *value, const JsonDoc *doc) {
    size_t i;

    if (!value) {
        return buf_puts(buf, "null");
    }
    switch (value->type) {
    case JSON_NULL:
        return buf_puts(buf, "null");
    case JSON_BOOL:
        return buf_puts(buf, value->u.boolean ? "true" : "false");
    case JSON_NUMBER:
        return buf_puts(buf, value->u.number ? value->u.number : "0");
    case JSON_STRING:
        return emit_string_bytes(buf, value->u.string.data ? value->u.string.data : "", value->u.string.len, doc);
    case JSON_ARRAY:
        if (buf_putc(buf, '[') != 0) {
            return -1;
        }
        for (i = 0; i < value->u.array.count; i++) {
            if (i && buf_putc(buf, ',') != 0) {
                return -1;
            }
            if (emit_value(buf, value->u.array.items[i], doc) != 0) {
                return -1;
            }
        }
        return buf_putc(buf, ']');
    case JSON_OBJECT:
        if (buf_putc(buf, '{') != 0) {
            return -1;
        }
        for (i = 0; i < value->u.object.count; i++) {
            if (i && buf_putc(buf, ',') != 0) {
                return -1;
            }
            if (emit_string_bytes(buf, value->u.object.members[i].key, value->u.object.members[i].key_len, doc) != 0) {
                return -1;
            }
            if (buf_putc(buf, ':') != 0) {
                return -1;
            }
            if (emit_value(buf, value->u.object.members[i].value, doc) != 0) {
                return -1;
            }
        }
        return buf_putc(buf, '}');
    }
    return -1;
}

char *json_stringify(const JsonDoc *doc, size_t *out_len) {
    Buf buf;
    JsonDoc empty;

    buf_init(&buf);
    if (!doc) {
        memset(&empty, 0, sizeof(empty));
        doc = &empty;
    }
    if (!doc->root || emit_value(&buf, doc->root, doc) != 0) {
        buf_free(&buf);
        return NULL;
    }
    if (out_len) {
        *out_len = buf.len;
    }
    return buf.data;
}
