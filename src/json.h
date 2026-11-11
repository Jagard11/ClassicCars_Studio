#ifndef STUDIO_JSON_H
#define STUDIO_JSON_H

#include <stddef.h>

/* JSON DOM that keeps object key order and the original number lexeme so
 * sales deep-data.json files can be serialized byte-for-byte. */

typedef enum JsonType {
    JSON_NULL = 0,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct JsonDoc {
    JsonValue *root;
    int escape_unicode;    /* Non-ASCII code points written as \uXXXX. */
    int unicode_hex_upper; /* Hex digits A-F rather than a-f. */
} JsonDoc;

JsonDoc *json_parse(const char *text, size_t len, char *err, size_t err_sz);
void json_doc_free(JsonDoc *doc);
char *json_stringify(const JsonDoc *doc, size_t *out_len);

JsonDoc *json_doc_new(JsonValue *root);

JsonType json_type(const JsonValue *value);
JsonValue *json_null(void);
JsonValue *json_bool(int value);
JsonValue *json_number(const char *lexeme);
JsonValue *json_number_int(long long value);
JsonValue *json_string(const char *text, size_t len);
JsonValue *json_string_cstr(const char *text);
JsonValue *json_array(void);
JsonValue *json_object(void);
void json_free(JsonValue *value);
JsonValue *json_clone(const JsonValue *value);

int json_bool_value(const JsonValue *value);
const char *json_number_lexeme(const JsonValue *value);
int json_number_set_int(JsonValue *value, long long number);
int json_number_set_lexeme(JsonValue *value, const char *lexeme);
const char *json_string_get(const JsonValue *value, size_t *len);
int json_string_set(JsonValue *value, const char *text, size_t len);
int json_as_int(const JsonValue *value, long long *out);
char *json_as_text(const JsonValue *value);

size_t json_array_len(const JsonValue *array);
JsonValue *json_array_at(const JsonValue *array, size_t index);
int json_array_add(JsonValue *array, JsonValue *item);
JsonValue *json_array_detach(JsonValue *array, size_t index);
void json_array_clear(JsonValue *array);
void json_array_transfer(JsonValue *dst, JsonValue *src);

size_t json_object_len(const JsonValue *object);
const char *json_object_key(const JsonValue *object, size_t index);
JsonValue *json_object_val(const JsonValue *object, size_t index);
JsonValue *json_object_get(const JsonValue *object, const char *key);
int json_object_has(const JsonValue *object, const char *key);
int json_object_put(JsonValue *object, const char *key, JsonValue *value);
int json_object_put_after(JsonValue *object, const char *after_key, const char *key, JsonValue *value);
void json_object_remove(JsonValue *object, const char *key);

#endif
