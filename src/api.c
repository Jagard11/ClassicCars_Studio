#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "api.h"

#include "app_config.h"
#include "config.h"
#include "fs_rename.h"
#include "gallery.h"
#include "git_scm.h"
#include "preview_draft.h"
#include "project_search.h"
#include "terminal.h"
#include "json.h"
#include "settings.h"
#include "version.h"
#include "window.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#ifndef _MSC_VER
#include <dirent.h>
#endif
#else
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "compat.h"

static int send_json_text(HttpResponse *res, int status, char *json_text) {
    if (!json_text) {
        http_response_set_error(res, 500, "Could not encode JSON");
        return 0;
    }
    if (http_response_set_json(res, status, json_text) != 0) {
        http_response_set_body(res, json_text, strlen(json_text));
        res->status = status;
        snprintf(res->content_type, sizeof(res->content_type), "application/json; charset=utf-8");
    }
    if (res->body != json_text) {
        free(json_text);
    }
    return 0;
}

static int send_doc(HttpResponse *res, int status, JsonDoc *doc) {
    size_t len = 0;
    char *text;
    if (!doc) {
        return send_json_text(res, 500, strdup("{\"ok\":false,\"error\":\"out of memory\"}"));
    }
    text = json_stringify(doc, &len);
    json_doc_free(doc);
    if (!text) {
        return send_json_text(res, 500, strdup("{\"ok\":false,\"error\":\"Could not encode JSON\"}"));
    }
    return send_json_text(res, status, text);
}

static int send_error(HttpResponse *res, int status, const char *message) {
    JsonValue *root = json_object();
    if (!root) {
        return send_json_text(res, 500, strdup("{\"ok\":false,\"error\":\"out of memory\"}"));
    }
    json_object_put(root, "ok", json_bool(0));
    json_object_put(root, "error", json_string_cstr(message && message[0] ? message : "Request failed"));
    return send_doc(res, status, json_doc_new(root));
}

static int send_gallery_status(HttpResponse *res, int code, const char *err) {
    int status = 500;
    if (code == 1) {
        status = 400;
    } else if (code == 2) {
        status = 404;
    }
    return send_error(res, status, err && err[0] ? err : "Request failed");
}

static const char *api_rest(const char *path) {
    if (!path) {
        return NULL;
    }
    if (strncmp(path, "/__studio/api", 13) == 0) {
        const char *rest = path + 13;
        if (*rest == '\0') {
            return "/";
        }
        if (*rest == '/') {
            return rest;
        }
        return NULL;
    }
    if (strncmp(path, "/api", 4) == 0) {
        const char *rest = path + 4;
        if (*rest == '\0') {
            return "/";
        }
        if (*rest == '/') {
            return rest;
        }
    }
    return NULL;
}

static int route_is(const char *rest, const char *route) {
    size_t len;
    if (!rest || !route) {
        return 0;
    }
    if (strcmp(rest, route) == 0) {
        return 1;
    }
    len = strlen(route);
    return len > 1 && strncmp(rest, route, len) == 0 && rest[len] == '/' && rest[len + 1] == '\0';
}

static int method_is(const HttpRequest *req, const char *method) {
    return req && strcasecmp(req->method, method) == 0;
}

static int hex_nibble(int ch) {
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

static void decode_component(const char *src, size_t len, char *out, size_t out_sz) {
    size_t i = 0;
    size_t w = 0;
    if (!out || out_sz == 0) {
        return;
    }
    while (i < len && w + 1 < out_sz) {
        if (src[i] == '%' && i + 2 < len) {
            int hi = hex_nibble((unsigned char)src[i + 1]);
            int lo = hex_nibble((unsigned char)src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[w++] = (char)((hi << 4) | lo);
                i += 3;
                continue;
            }
        }
        if (src[i] == '+') {
            out[w++] = ' ';
            i++;
            continue;
        }
        out[w++] = src[i++];
    }
    out[w] = '\0';
}

static void query_copy(const HttpRequest *req, const char *key, char *out, size_t out_sz) {
    const char *query;
    size_t key_len;
    if (!out || out_sz == 0) {
        return;
    }
    out[0] = '\0';
    if (!req || !key) {
        return;
    }
    http_query_param(req->query, key, out, out_sz);
    if (out[0] != '\0') {
        return;
    }
    query = req->query;
    key_len = strlen(key);
    while (query && *query) {
        const char *amp = strchr(query, '&');
        size_t part_len = amp ? (size_t)(amp - query) : strlen(query);
        const char *eq = memchr(query, '=', part_len);
        size_t name_len = eq ? (size_t)(eq - query) : part_len;
        if (name_len == key_len && memcmp(query, key, key_len) == 0) {
            char raw[1024];
            char decoded[1024];
            size_t value_len = eq ? part_len - name_len - 1 : 0;
            const char *value = eq ? eq + 1 : "";
            int rc;
            if (value_len >= sizeof(raw)) {
                value_len = sizeof(raw) - 1;
            }
            memcpy(raw, value, value_len);
            raw[value_len] = '\0';
            decoded[0] = '\0';
            rc = http_url_decode(raw, decoded, sizeof(decoded));
            if (rc == 0 || decoded[0] != '\0' || raw[0] == '\0') {
                snprintf(out, out_sz, "%s", decoded);
            } else {
                decode_component(raw, value_len, out, out_sz);
            }
            return;
        }
        if (!amp) {
            break;
        }
        query = amp + 1;
    }
}

static int json_flag_on(const JsonValue *value) {
    long long number = 0;
    const char *text;
    if (!value) return 0;
    if (json_type(value) == JSON_BOOL) return json_bool_value(value) ? 1 : 0;
    if (json_type(value) == JSON_NUMBER && json_as_int(value, &number) == 0) return number != 0;
    if (json_type(value) == JSON_STRING) {
        text = json_string_get(value, NULL);
        return text && (strcmp(text, "1") == 0 || strcasecmp(text, "true") == 0);
    }
    return 0;
}

static long long json_revision(const JsonValue *root) {
    JsonValue *value = json_object_get(root, "revision");
    long long number = 0;
    if (!value) return 0;
    if (json_type(value) == JSON_NUMBER && json_as_int(value, &number) == 0) return number;
    if (json_type(value) == JSON_STRING) {
        const char *text = json_string_get(value, NULL);
        if (text && text[0]) number = strtoll(text, NULL, 10);
    }
    return number;
}

static char *json_object_text(const JsonValue *object, const char *key) {
    JsonValue *value;
    char *text;
    if (!object || !json_object_has(object, key)) {
        return NULL;
    }
    value = json_object_get(object, key);
    text = json_as_text(value);
    return text ? text : strdup("");
}

static JsonDoc *parse_object_body(const HttpRequest *req, HttpResponse *res) {
    char err[256];
    JsonDoc *doc;
    err[0] = '\0';
    if (!req->body || req->body_len == 0) {
        send_error(res, 400, "JSON body is required");
        return NULL;
    }
    doc = json_parse(req->body, req->body_len, err, sizeof(err));
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        send_error(res, 400, err[0] ? err : "JSON body must be an object");
        return NULL;
    }
    return doc;
}

static int handle_health(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonValue *root;
    JsonValue *cfg;
    JsonValue *app_cfg;
    const char *home = settings_get_studio_home();
    const char *base = (home && home[0]) ? home : repo_root;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for health");
    }
    config_load(base);
    app_config_load(base);
    root = json_object();
    if (!root) {
        return send_error(res, 500, "out of memory");
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "version", json_string_cstr(STUDIO_VERSION));
    json_object_put(root, "phpPort", json_number_int(settings_get_php_port()));
    json_object_put(root, "repoRoot", json_string_cstr(repo_root ? repo_root : ""));
    cfg = config_to_json();
    if (cfg) {
        json_object_put(root, "config", cfg);
    }
    app_cfg = app_config_to_json();
    if (app_cfg) {
        json_object_put(root, "appConfig", app_cfg);
    }
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_folders(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char section[64];
    char err[256];
    JsonDoc *doc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for folders");
    }
    query_copy(req, "section", section, sizeof(section));
    if (section[0] == '\0') {
        snprintf(section, sizeof(section), "sales");
    }
    err[0] = '\0';
    doc = gallery_folders_document(repo_root, section, err, sizeof(err));
    if (!doc) {
        return send_gallery_status(res, strcmp(err, "Unknown section") == 0 ? 1 : -1, err);
    }
    return send_doc(res, 200, doc);
}

static int handle_gallery_get(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char section[64];
    char slug[512];
    char err[512];
    JsonDoc *doc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for gallery");
    }
    query_copy(req, "section", section, sizeof(section));
    query_copy(req, "slug", slug, sizeof(slug));
    if (section[0] == '\0') {
        snprintf(section, sizeof(section), "sales");
    }
    if (slug[0] == '\0') {
        return send_error(res, 400, "slug is required");
    }
    err[0] = '\0';
    doc = gallery_state_document(repo_root, section, slug, err, sizeof(err));
    if (!doc) {
        int code = -1;
        if (strstr(err, "not found")) {
            code = 2;
        } else if (strstr(err, "not allowed") || strstr(err, "Unknown section")) {
            code = 1;
        }
        return send_gallery_status(res, code, err);
    }
    return send_doc(res, 200, doc);
}

static int handle_gallery_save(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonValue *images;
    char *section;
    char *slug;
    char err[512];
    int code;
    JsonDoc *state;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for gallery save");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    section = json_object_text(body->root, "section");
    slug = json_object_text(body->root, "slug");
    images = json_object_get(body->root, "images");
    if (!section) {
        section = strdup("sales");
    }
    if (!slug || !slug[0]) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_error(res, 400, "slug is required");
    }
    if (!images || json_type(images) != JSON_ARRAY) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_error(res, 400, "images must be an array");
    }
    err[0] = '\0';
    code = gallery_save_images(repo_root, section, slug, images, err, sizeof(err));
    if (code != 0) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_gallery_status(res, code, err);
    }
    state = gallery_state_document(repo_root, section, slug, err, sizeof(err));
    free(section);
    free(slug);
    json_doc_free(body);
    if (!state) {
        return send_gallery_status(res, -1, err);
    }
    return send_doc(res, 200, state);
}

static int handle_details_save(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    char *section;
    char *slug;
    char err[1024];
    int code;
    JsonDoc *state;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for details save");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    section = json_object_text(body->root, "section");
    slug = json_object_text(body->root, "slug");
    if (!section) {
        section = strdup("sales");
    }
    if (!slug || !slug[0]) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_error(res, 400, "slug is required");
    }
    err[0] = '\0';
    code = gallery_save_details(repo_root, section, slug, json_object_get(body->root, "details"),
                                json_object_get(body->root, "pageValues"), json_object_get(body->root, "relatedSales"), err,
                                sizeof(err));
    if (code != 0) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_gallery_status(res, code, err);
    }
    state = gallery_state_document(repo_root, section, slug, err, sizeof(err));
    free(section);
    free(slug);
    json_doc_free(body);
    if (!state) {
        return send_error(res, 500, err[0] ? err : "Saved, but the gallery could not be reloaded");
    }
    return send_doc(res, 200, state);
}

static int handle_revert(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    char *section;
    char *slug;
    char err[512];
    int code;
    JsonDoc *state;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for revert");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    section = json_object_text(body->root, "section");
    slug = json_object_text(body->root, "slug");
    if (!section) {
        section = strdup("sales");
    }
    if (!slug || !slug[0]) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_error(res, 400, "slug is required");
    }
    err[0] = '\0';
    code = gallery_revert_folder(repo_root, section, slug, err, sizeof(err));
    if (code != 0) {
        free(section);
        free(slug);
        json_doc_free(body);
        return send_gallery_status(res, code, err);
    }
    state = gallery_state_document(repo_root, section, slug, err, sizeof(err));
    free(section);
    free(slug);
    json_doc_free(body);
    if (!state) {
        return send_error(res, 500, err[0] ? err : "Reverted, but the gallery could not be reloaded");
    }
    return send_doc(res, 200, state);
}

static int send_page_code(HttpResponse *res, int code, const char *err) {
    if (code == 3) {
        return send_error(res, 409, err && err[0] ? err : "That page already exists");
    }
    return send_gallery_status(res, code, err);
}

static int handle_tree(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char err[256];
    JsonDoc *doc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for the file tree");
    }
    err[0] = '\0';
    doc = gallery_tree_document(repo_root, err, sizeof(err));
    if (!doc) {
        return send_gallery_status(res, -1, err);
    }
    return send_doc(res, 200, doc);
}

static int handle_page_get(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char path[2048];
    char err[512];
    JsonDoc *doc = NULL;
    int code;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for a page");
    }
    query_copy(req, "path", path, sizeof(path));
    if (path[0] == '\0') {
        return send_error(res, 400, "path is required");
    }
    err[0] = '\0';
    if (strcmp(path, CONFIG_PUBLIC_PATH) == 0) {
        const char *home = settings_get_studio_home();
        doc = config_page_document((home && home[0]) ? home : repo_root, err, sizeof(err));
        if (!doc) {
            return send_error(res, 500, err[0] ? err : "Could not load config.json");
        }
        return send_doc(res, 200, doc);
    }
    code = gallery_page_document(repo_root, path, &doc, err, sizeof(err));
    if (code != 0) {
        return send_page_code(res, code, err);
    }
    gallery_attach_page_slides(repo_root, doc);
    return send_doc(res, 200, doc);
}

static int handle_preview_stage(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    char *path;
    JsonValue *content;
    JsonValue *images;
    int has_content;
    int has_images;
    const char *content_text = NULL;
    long long revision;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to stage a preview");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    path = json_object_text(body->root, "path");
    content = json_object_get(body->root, "content");
    images = json_object_get(body->root, "images");
    has_content = content && json_type(content) == JSON_STRING;
    has_images = images && json_type(images) == JSON_ARRAY;
    if (has_content) content_text = json_string_get(content, NULL);
    revision = json_revision(body->root);
    if (!path || !path[0] || (!has_content && !has_images)) {
        free(path);
        json_doc_free(body);
        return send_error(res, 400, "path and content or images are required");
    }
    if (images && !has_images) {
        free(path);
        json_doc_free(body);
        return send_error(res, 400, "images must be an array");
    }
    if (preview_draft_stage(repo_root, path, content_text, has_content, images, has_images, revision) != 0) {
        free(path);
        json_doc_free(body);
        return send_error(res, 500, "Could not stage the preview");
    }
    free(path);
    json_doc_free(body);
    return send_json_text(res, 200, strdup("{\"ok\":true}"));
}

static int handle_preview_clear(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    char *path = NULL;
    long long revision;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to clear a preview");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    if (json_object_has(body->root, "path")) path = json_object_text(body->root, "path");
    revision = json_revision(body->root);
    if (preview_draft_clear(repo_root, path, revision) != 0) {
        free(path);
        json_doc_free(body);
        return send_error(res, 500, "Could not clear the preview");
    }
    free(path);
    json_doc_free(body);
    return send_json_text(res, 200, strdup("{\"ok\":true}"));
}

static int handle_page_save(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    char *path;
    char *content;
    JsonValue *flag;
    int run_values = 0;
    char err[1024];
    JsonDoc *doc = NULL;
    int code;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to save a page");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    path = json_object_text(body->root, "path");
    content = json_object_text(body->root, "content");
    flag = json_object_get(body->root, "runPageValues");
    if (flag && json_type(flag) == JSON_BOOL) {
        run_values = json_bool_value(flag);
    } else if (flag && json_type(flag) == JSON_NUMBER) {
        long long number = 0;
        if (json_as_int(flag, &number) == 0) {
            run_values = number != 0;
        }
    } else if (flag && json_type(flag) == JSON_STRING) {
        const char *text = json_string_get(flag, NULL);
        run_values = text && (strcmp(text, "1") == 0 || strcasecmp(text, "true") == 0);
    }
    if (!path || !path[0]) {
        free(path);
        free(content);
        json_doc_free(body);
        return send_error(res, 400, "path is required");
    }
    if (!content) {
        free(path);
        json_doc_free(body);
        return send_error(res, 400, "content is required");
    }
    err[0] = '\0';
    if (strcmp(path, CONFIG_PUBLIC_PATH) == 0) {
        const char *home = settings_get_studio_home();
        const char *base = (home && home[0]) ? home : repo_root;
        code = config_save_text(base, content, err, sizeof(err));
        free(path);
        free(content);
        json_doc_free(body);
        if (code != 0) {
            return send_error(res, code == 1 ? 400 : 500, err[0] ? err : "Could not save config.json");
        }
        doc = config_page_document(base, err, sizeof(err));
        if (!doc) {
            return send_error(res, 500, err[0] ? err : "Saved, but config.json could not be reloaded");
        }
        return send_doc(res, 200, doc);
    }
    code = gallery_page_save(repo_root, path, content, run_values, &doc, err, sizeof(err));
    free(path);
    free(content);
    json_doc_free(body);
    if (code != 0) {
        return send_page_code(res, code, err);
    }
    gallery_attach_page_slides(repo_root, doc);
    return send_doc(res, 200, doc);
}

static int handle_widgets_apply(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    char *path;
    char *widget;
    char *content = NULL;
    int has_content = 0;
    JsonValue *data;
    char err[1024];
    JsonDoc *doc = NULL;
    int code;
    int dry_run;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to apply a widget");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    path = json_object_text(body->root, "path");
    widget = json_object_text(body->root, "widget");
    data = json_object_get(body->root, "data");
    if (json_object_has(body->root, "content") && json_type(json_object_get(body->root, "content")) == JSON_STRING) {
        content = json_object_text(body->root, "content");
        has_content = 1;
    }
    if (data && json_type(data) != JSON_OBJECT) {
        free(path);
        free(widget);
        free(content);
        json_doc_free(body);
        return send_error(res, 400, "data must be an object");
    }
    if (!path || !path[0] || !widget || !widget[0]) {
        free(path);
        free(widget);
        free(content);
        json_doc_free(body);
        return send_error(res, 400, "path and widget are required");
    }
    dry_run = json_flag_on(json_object_get(body->root, "dryRun")) || json_flag_on(json_object_get(body->root, "previewOnly"));
    err[0] = '\0';
    code = gallery_widget_apply(repo_root, path, content, has_content, widget, data, dry_run, &doc, err, sizeof(err));
    free(path);
    free(widget);
    free(content);
    json_doc_free(body);
    if (code != 0) {
        return send_page_code(res, code, err);
    }
    gallery_attach_page_slides(repo_root, doc);
    return send_doc(res, 200, doc);
}

static int handle_templates(const HttpRequest *req, HttpResponse *res) {
    char err[128];
    JsonDoc *doc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for templates");
    }
    err[0] = '\0';
    doc = gallery_templates_document(err, sizeof(err));
    if (!doc) {
        return send_error(res, 500, err[0] ? err : "Could not list templates");
    }
    return send_doc(res, 200, doc);
}

static int handle_page_create(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int code;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to create a page");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    err[0] = '\0';
    code = gallery_page_create(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (code != 0) {
        return send_page_code(res, code, err);
    }
    return send_doc(res, 200, doc);
}

static int handle_fs_rename(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int code;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to rename a file or folder");
    }
    body = parse_object_body(req, res);
    if (!body) {
        return 0;
    }
    err[0] = '\0';
    code = fs_rename_item(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (code != 0) {
        json_doc_free(doc);
        return send_page_code(res, code, err);
    }
    return send_doc(res, 200, doc);
}

static int last_page_ok(const char *path) {
    const char *p;
    size_t n;
    if (!path || path[0] != '/') return 0;
    n = strlen(path);
    if (n < 2 || n > 1024) return 0;
    if (strstr(path, "//") != NULL) return 0;
    for (p = path; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == 0x7f || c == '\\' || c == '?' || c == '#') return 0;
    }
    p = path;
    while (*p == '/') p++;
    if (*p == '\0') return 0;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 0) return 0;
        if ((len == 1 && p[0] == '.') || (len == 2 && p[0] == '.' && p[1] == '.')) return 0;
        if (!slash) break;
        p = slash + 1;
    }
    return 1;
}

#define SESSION_TAB_MAX 64

typedef struct SessionTab {
    char path[1200];
    int is_preview;
} SessionTab;

static int copy_json_string(const JsonValue *value, char *out, size_t out_sz) {
    const char *text;
    size_t len = 0;
    if (!value || json_type(value) != JSON_STRING || !out || out_sz == 0) return -1;
    text = json_string_get(value, &len);
    if (!text || len + 1 > out_sz) return -1;
    if (len) memcpy(out, text, len);
    out[len] = '\0';
    return 0;
}

static int session_put_tabs(JsonValue *root, const SessionTab *tabs, size_t count) {
    JsonValue *array = json_array();
    size_t i;
    if (!root || !array) {
        json_free(array);
        return -1;
    }
    for (i = 0; i < count; i++) {
        JsonValue *item = json_object();
        JsonValue *path = json_string_cstr(tabs[i].path);
        JsonValue *preview = json_bool(tabs[i].is_preview ? 1 : 0);
        if (!item || !path || !preview) {
            json_free(item);
            json_free(path);
            json_free(preview);
            json_free(array);
            return -1;
        }
        if (json_object_put(item, "path", path) != 0 || json_object_put(item, "isPreview", preview) != 0) {
            json_free(item);
            json_free(array);
            return -1;
        }
        if (json_array_add(array, item) != 0) {
            json_free(array);
            return -1;
        }
    }
    if (json_object_put(root, "openTabs", array) != 0) return -1;
    return 0;
}

static int parse_open_tabs(const JsonValue *list, SessionTab *tabs, size_t cap, size_t *count) {
    size_t n;
    size_t i;
    size_t kept = 0;
    int saw_preview = 0;
    if (!list || json_type(list) != JSON_ARRAY || !tabs || !count) return -1;
    n = json_array_len(list);
    if (n > cap) return -1;
    for (i = 0; i < n; i++) {
        JsonValue *item = json_array_at(list, i);
        JsonValue *path;
        JsonValue *preview;
        size_t j;
        if (!item || json_type(item) != JSON_OBJECT) return -1;
        path = json_object_get(item, "path");
        if (copy_json_string(path, tabs[kept].path, sizeof tabs[kept].path) != 0) return -1;
        if (!last_page_ok(tabs[kept].path)) return -1;
        for (j = 0; j < kept; j++) {
            if (strcmp(tabs[j].path, tabs[kept].path) == 0) return -1;
        }
        preview = json_object_get(item, "isPreview");
        if (preview && json_type(preview) != JSON_BOOL) return -1;
        tabs[kept].is_preview = (preview && json_bool_value(preview)) ? 1 : 0;
        if (tabs[kept].is_preview) {
            if (saw_preview) tabs[kept].is_preview = 0;
            else saw_preview = 1;
        }
        kept++;
    }
    *count = kept;
    return 0;
}

static int session_field_name_ok(const char *key) {
    size_t i;
    size_t n;
    unsigned char c;
    if (!key || !key[0]) return 0;
    n = strlen(key);
    if (n > 64) return 0;
    c = (unsigned char)key[0];
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    for (i = 1; i < n; i++) {
        c = (unsigned char)key[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') continue;
        return 0;
    }
    return 1;
}

static int widget_collapsed_ok(const JsonValue *value) {
    size_t i;
    size_t n;
    if (!value || json_type(value) != JSON_OBJECT) return 0;
    n = json_object_len(value);
    if (n > 64) return 0;
    for (i = 0; i < n; i++) {
        const char *key = json_object_key(value, i);
        JsonValue *item = json_object_val(value, i);
        if (!session_field_name_ok(key) || !item || json_type(item) != JSON_BOOL) return 0;
    }
    return 1;
}

static int widget_order_ok(const JsonValue *value) {
    size_t i;
    size_t n;
    if (!value || json_type(value) != JSON_ARRAY) return 0;
    n = json_array_len(value);
    if (n > 64) return 0;
    for (i = 0; i < n; i++) {
        char name[80];
        size_t j;
        if (copy_json_string(json_array_at(value, i), name, sizeof name) != 0) return 0;
        if (!session_field_name_ok(name)) return 0;
        for (j = 0; j < i; j++) {
            char prev[80];
            if (copy_json_string(json_array_at(value, j), prev, sizeof prev) != 0) return 0;
            if (strcmp(prev, name) == 0) return 0;
        }
    }
    return 1;
}

/* 0 valid, -1 bad request (err set), -2 out of memory. */
static int session_value_ok(const char *key, const JsonValue *value, char *err, size_t err_sz) {
    if (!key || !err || err_sz == 0) return -1;
    if (!session_field_name_ok(key)) {
        snprintf(err, err_sz, "Unsupported session field");
        return -1;
    }
    if (strcmp(key, "lastPage") == 0) {
        char path[1200];
        if (copy_json_string(value, path, sizeof path) != 0 || !last_page_ok(path)) {
            snprintf(err, err_sz, "lastPage must be a site path");
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "openTabs") == 0) {
        SessionTab *tabs;
        size_t count = 0;
        int parsed;
        tabs = (SessionTab *)calloc(SESSION_TAB_MAX, sizeof(*tabs));
        if (!tabs) {
            snprintf(err, err_sz, "out of memory");
            return -2;
        }
        parsed = parse_open_tabs(value, tabs, SESSION_TAB_MAX, &count);
        free(tabs);
        if (parsed != 0) {
            snprintf(err, err_sz, "openTabs must be a list of site paths");
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "rightPanelOpen") == 0 || strcmp(key, "leftPanelOpen") == 0 ||
        strcmp(key, "terminalOpen") == 0 || strcmp(key, "gitCommitsOpen") == 0) {
        if (!value || json_type(value) != JSON_BOOL) {
            snprintf(err, err_sz, "%s must be a boolean", key);
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "widgetCollapsed") == 0) {
        if (!widget_collapsed_ok(value)) {
            snprintf(err, err_sz, "widgetCollapsed must be an object of booleans");
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "widgetOrder") == 0) {
        if (!widget_order_ok(value)) {
            snprintf(err, err_sz, "widgetOrder must be an array of widget ids");
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "rightPanelWidth") == 0 || strcmp(key, "leftPanelWidth") == 0 ||
        strcmp(key, "treeWidth") == 0 || strcmp(key, "inspectorWidth") == 0) {
        if (!value || json_type(value) != JSON_NUMBER) {
            snprintf(err, err_sz, "%s must be a number", key);
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "openEditorsHeight") == 0 || strcmp(key, "fileTabbarHeight") == 0 ||
        strcmp(key, "terminalHeight") == 0 || strcmp(key, "gitCommitsHeight") == 0) {
        if (!value || json_type(value) != JSON_NUMBER) {
            snprintf(err, err_sz, "%s must be a number", key);
            return -1;
        }
        return 0;
    }
    if (strcmp(key, "leftShelfTab") == 0) {
        char tab[32];
        if (copy_json_string(value, tab, sizeof tab) != 0 ||
            (strcmp(tab, "files") != 0 && strcmp(tab, "search") != 0 && strcmp(tab, "git") != 0)) {
            snprintf(err, err_sz, "leftShelfTab must be files, search, or git");
            return -1;
        }
        return 0;
    }
    if (!value) {
        snprintf(err, err_sz, "Unsupported session field");
        return -1;
    }
    return 0;
}

static int session_body_ok(const JsonValue *body, char *err, size_t err_sz) {
    size_t i;
    size_t n;
    int meaningful = 0;
    if (!body || json_type(body) != JSON_OBJECT || !err || err_sz == 0) return -1;
    n = json_object_len(body);
    for (i = 0; i < n; i++) {
        const char *key = json_object_key(body, i);
        int rc;
        if (!key || strcmp(key, "ok") == 0) continue;
        meaningful++;
        rc = session_value_ok(key, json_object_val(body, i), err, err_sz);
        if (rc != 0) return rc;
    }
    if (!meaningful) {
        snprintf(err, err_sz, "lastPage is required");
        return -1;
    }
    return 0;
}

static JsonValue *session_load_object(const char *repo_root) {
    char *raw;
    char err[128];
    JsonDoc *doc;
    JsonValue *copy;
    raw = settings_read_session(repo_root);
    if (!raw) return json_object();
    err[0] = '\0';
    doc = json_parse(raw, strlen(raw), err, sizeof err);
    free(raw);
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        return json_object();
    }
    copy = json_clone(doc->root);
    json_doc_free(doc);
    if (!copy) return NULL;
    json_object_remove(copy, "ok");
    return copy;
}

static int session_store_object(const char *repo_root, JsonValue *root) {
    JsonDoc *doc;
    char *text;
    int rc;
    doc = json_doc_new(root);
    if (!doc) return -1;
    text = json_stringify(doc, NULL);
    json_doc_free(doc);
    if (!text) return -1;
    rc = settings_write_session(repo_root, text);
    free(text);
    return rc;
}

static JsonValue *session_response_root(const JsonValue *stored) {
    JsonValue *root;
    JsonValue *page;
    JsonValue *ok;
    char last[1200];
    root = stored ? json_clone(stored) : json_object();
    if (!root) return NULL;
    json_object_remove(root, "ok");
    page = json_object_get(root, "lastPage");
    last[0] = '\0';
    if (!page || json_type(page) != JSON_STRING || copy_json_string(page, last, sizeof last) != 0 ||
        (last[0] && !last_page_ok(last))) {
        JsonValue *empty = json_string_cstr("");
        if (!empty || json_object_put(root, "lastPage", empty) != 0) {
            json_free(empty);
            json_free(root);
            return NULL;
        }
    }
    ok = json_bool(1);
    if (!ok || json_object_put(root, "ok", ok) != 0) {
        json_free(ok);
        json_free(root);
        return NULL;
    }
    return root;
}

static int send_stored_session(HttpResponse *res, JsonValue *stored) {
    JsonValue *root = session_response_root(stored);
    json_free(stored);
    if (!root) return send_error(res, 500, "out of memory");
    return send_doc(res, 200, json_doc_new(root));
}

/* 0 applied, -1 out of memory, -2 too many fields. */
static int session_apply_body(JsonValue *stored, const JsonValue *body) {
    size_t i;
    size_t n;
    if (!stored || !body) return -1;
    n = json_object_len(body);
    for (i = 0; i < n; i++) {
        const char *key = json_object_key(body, i);
        JsonValue *value = json_object_val(body, i);
        JsonValue *copy;
        if (!key || strcmp(key, "ok") == 0) continue;
        if (strcmp(key, "openTabs") == 0) {
            SessionTab *tabs = (SessionTab *)calloc(SESSION_TAB_MAX, sizeof(*tabs));
            size_t count = 0;
            if (!tabs) return -1;
            if (parse_open_tabs(value, tabs, SESSION_TAB_MAX, &count) != 0 || session_put_tabs(stored, tabs, count) != 0) {
                free(tabs);
                return -1;
            }
            free(tabs);
            continue;
        }
        if (strcmp(key, "widgetCollapsed") == 0) {
            JsonValue *collapsed = json_object_get(stored, "widgetCollapsed");
            size_t w;
            size_t wn;
            if (!collapsed || json_type(collapsed) != JSON_OBJECT) {
                JsonValue *fresh = json_object();
                if (!fresh || json_object_put(stored, "widgetCollapsed", fresh) != 0) return -1;
                collapsed = json_object_get(stored, "widgetCollapsed");
            }
            if (!collapsed) return -1;
            wn = json_object_len(value);
            for (w = 0; w < wn; w++) {
                const char *wkey = json_object_key(value, w);
                JsonValue *flag = json_bool(json_bool_value(json_object_val(value, w)));
                if (!wkey || !flag) {
                    json_free(flag);
                    return -1;
                }
                if (json_object_put(collapsed, wkey, flag) != 0) return -1;
            }
            if (json_object_len(collapsed) > 64) return -2;
            continue;
        }
        copy = json_clone(value);
        if (!copy) return -1;
        if (json_object_put(stored, key, copy) != 0) return -1;
    }
    if (json_object_len(stored) > 64) return -2;
    if (!json_object_get(stored, "lastPage")) {
        JsonValue *page = json_string_cstr("");
        if (!page || json_object_put(stored, "lastPage", page) != 0) return -1;
    }
    return 0;
}

static const char *state_root(const char *repo_root) {
    const char *home = settings_get_studio_home();
    if (home && home[0]) return home;
    return repo_root;
}

static int handle_session_get(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonValue *stored;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for session");
    }
    repo_root = state_root(repo_root);
    stored = session_load_object(repo_root);
    if (!stored) return send_error(res, 500, "out of memory");
    return send_stored_session(res, stored);
}

static int handle_session_post(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonValue *stored;
    JsonValue *echo;
    char err[160];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for session");
    }
    repo_root = state_root(repo_root);
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = session_body_ok(body->root, err, sizeof err);
    if (rc != 0) {
        json_doc_free(body);
        if (rc == -2) return send_error(res, 500, "out of memory");
        return send_error(res, 400, err[0] ? err : "lastPage is required");
    }
    stored = session_load_object(repo_root);
    if (!stored) {
        json_doc_free(body);
        return send_error(res, 500, "out of memory");
    }
    rc = session_apply_body(stored, body->root);
    json_doc_free(body);
    if (rc != 0) {
        json_free(stored);
        if (rc == -2) return send_error(res, 400, "session has too many fields");
        return send_error(res, 500, "out of memory");
    }
    echo = json_clone(stored);
    if (!echo) {
        json_free(stored);
        return send_error(res, 500, "out of memory");
    }
    if (session_store_object(repo_root, stored) != 0) {
        json_free(echo);
        return send_error(res, 500, "Could not write session.json");
    }
    return send_stored_session(res, echo);
}


static int handle_theme_get(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char *theme;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for theme");
    }
    repo_root = state_root(repo_root);
    theme = settings_read_theme(repo_root);
    if (!theme || !theme[0]) {
        free(theme);
        theme = strdup("{\"preset\":\"gold\",\"custom\":{},\"layout\":{}}");
    }
    return send_json_text(res, 200, theme);
}

static int handle_theme_post(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char *copy;
    JsonDoc *doc;
    char err[256];
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for theme");
    }
    repo_root = state_root(repo_root);
    if (!req->body || req->body_len == 0) {
        return send_error(res, 400, "JSON body is required");
    }
    copy = (char *)malloc(req->body_len + 1);
    if (!copy) {
        return send_error(res, 500, "out of memory");
    }
    memcpy(copy, req->body, req->body_len);
    copy[req->body_len] = '\0';
    err[0] = '\0';
    doc = json_parse(copy, req->body_len, err, sizeof(err));
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        free(copy);
        return send_error(res, 400, err[0] ? err : "Theme JSON must be an object");
    }
    json_doc_free(doc);
    settings_ensure_dirs(repo_root);
    if (settings_write_theme(repo_root, copy) != 0) {
        free(copy);
        return send_error(res, 500, "Could not write theme.json");
    }
    free(copy);
    return send_json_text(res, 200, strdup("{\"ok\":true}"));
}

#define FS_DIR_MAX 4096

typedef struct FsDir {
    char name[256];
    int website;
} FsDir;

static int fs_join(char *dst, size_t dst_sz, const char *dir, const char *name) {
    size_t nd;
    size_t nn;
    int need_slash;
    if (!dst || dst_sz == 0 || !dir || !name) return -1;
    nd = strlen(dir);
    nn = strlen(name);
    while (nd > 0 && (dir[nd - 1] == '/' || dir[nd - 1] == '\\')) {
        if (nd == 1) break;
#ifdef _WIN32
        if (nd == 3 && dir[1] == ':') break;
#endif
        nd--;
    }
    need_slash = !(nd > 0 && (dir[nd - 1] == '/' || dir[nd - 1] == '\\'));
    if (nd + (need_slash ? 1u : 0u) + nn + 1u > dst_sz) return -1;
    memcpy(dst, dir, nd);
    if (need_slash) dst[nd++] = '/';
    memcpy(dst + nd, name, nn + 1);
    return 0;
}

static int fs_is_dir(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int fs_is_file(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int looks_like_website(const char *dir) {
    char path[PATH_MAX];
    if (fs_join(path, sizeof path, dir, "sales") == 0 && fs_is_dir(path)) return 1;
    if (fs_join(path, sizeof path, dir, "articles") == 0 && fs_is_dir(path)) return 1;
    if (fs_join(path, sizeof path, dir, "index.php") == 0 && fs_is_file(path)) return 1;
    if (fs_join(path, sizeof path, dir, "index.html") == 0 && fs_is_file(path)) return 1;
    return 0;
}

static int fs_parent(const char *path, char *out, size_t out_sz) {
    char copy[PATH_MAX];
    size_t n;
    char *slash;
    if (!path || !out || out_sz == 0) return -1;
    n = strlen(path);
    if (n == 0 || n >= sizeof copy) return -1;
    memcpy(copy, path, n + 1);
    while (n > 1 && copy[n - 1] == '/') copy[--n] = '\0';
#ifdef _WIN32
    if ((n == 2 && copy[1] == ':') || (n == 3 && copy[1] == ':' && copy[2] == '/')) return -1;
#endif
    if (strcmp(copy, "/") == 0) return -1;
    slash = strrchr(copy, '/');
    if (!slash) return -1;
    if (slash == copy) {
        if (out_sz < 2) return -1;
        out[0] = '/';
        out[1] = '\0';
        return 0;
    }
#ifdef _WIN32
    if (slash == copy + 2 && copy[1] == ':') {
        if (out_sz < 4) return -1;
        out[0] = copy[0];
        out[1] = ':';
        out[2] = '/';
        out[3] = '\0';
        return 0;
    }
#endif
    *slash = '\0';
    n = strlen(copy);
    if (n + 1 > out_sz) return -1;
    memcpy(out, copy, n + 1);
    return 0;
}

static int natcasecmp(const char *a, const char *b) {
    if (!a) a = "";
    if (!b) b = "";
    while (*a && *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            const char *as = a;
            const char *bs = b;
            const char *ae;
            const char *be;
            size_t al;
            size_t bl;
            int cmp;
            while (*as == '0') as++;
            while (*bs == '0') bs++;
            ae = as;
            be = bs;
            while (isdigit((unsigned char)*ae)) ae++;
            while (isdigit((unsigned char)*be)) be++;
            al = (size_t)(ae - as);
            bl = (size_t)(be - bs);
            if (al != bl) return al < bl ? -1 : 1;
            cmp = al ? strncmp(as, bs, al) : 0;
            if (cmp) return cmp;
            a = ae;
            b = be;
            continue;
        }
        {
            unsigned char ca = (unsigned char)tolower((unsigned char)*a);
            unsigned char cb = (unsigned char)tolower((unsigned char)*b);
            if (ca != cb) return ca < cb ? -1 : 1;
        }
        a++;
        b++;
    }
    if (*a) return 1;
    if (*b) return -1;
    return 0;
}

static int fs_dir_cmp(const void *va, const void *vb) {
    return natcasecmp(((const FsDir *)va)->name, ((const FsDir *)vb)->name);
}

static int put_str(JsonValue *object, const char *key, const char *text) {
    JsonValue *value = json_string_cstr(text ? text : "");
    if (!value) return -1;
    return json_object_put(object, key, value);
}

static int put_bool(JsonValue *object, const char *key, int on) {
    JsonValue *value = json_bool(on ? 1 : 0);
    if (!value) return -1;
    return json_object_put(object, key, value);
}

static JsonValue *fs_entry(const char *name, const char *path, int parent, int website) {
    JsonValue *item = json_object();
    if (!item) return NULL;
    if (put_str(item, "name", name) != 0 || put_str(item, "path", path) != 0 ||
        put_bool(item, "parent", parent) != 0 || put_bool(item, "website", website) != 0) {
        json_free(item);
        return NULL;
    }
    return item;
}

static int resolve_directory(const char *in, char *out, size_t out_sz, const char **err_out) {
    struct stat st;
    if (err_out) *err_out = "Folder was not found";
    if (!in || !in[0]) {
        if (err_out) *err_out = "Path is required";
        return -1;
    }
    if (stat(in, &st) != 0) {
        if (err_out) *err_out = "Folder was not found";
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (err_out) *err_out = "Path is not a directory";
        return -1;
    }
    if (settings_canonicalize_dir(in, out, out_sz) != 0) {
        if (err_out) *err_out = "Folder could not be resolved";
        return -1;
    }
    if (err_out) *err_out = NULL;
    return 0;
}

static void trim_inplace(char *text) {
    char *start;
    size_t n;
    if (!text) return;
    start = text;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != text) memmove(text, start, strlen(start) + 1);
    n = strlen(text);
    while (n > 0 && (text[n - 1] == ' ' || text[n - 1] == '\t' || text[n - 1] == '\r' || text[n - 1] == '\n')) {
        text[--n] = '\0';
    }
}

static int query_flag(const HttpRequest *req, const char *key) {
    char value[16];
    if (!req || !key) return 0;
    if (!http_query_param(req->query, key, value, sizeof value)) return 0;
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0 ||
           strcmp(value, "on") == 0;
}

static JsonDoc *fs_list_document(const char *resolved, int show_hidden, const char **err_out) {
    DIR *dir;
    struct dirent *ent;
    FsDir *rows;
    size_t count = 0;
    size_t i;
    int truncated = 0;
    char parent[PATH_MAX];
    JsonValue *root;
    JsonValue *entries;
    JsonDoc *doc;
    int has_parent;
    if (err_out) *err_out = "Could not list folders";
    dir = opendir(resolved);
    if (!dir) {
        if (err_out) *err_out = errno == EACCES || errno == EPERM ? "Folder cannot be read" : "Could not list folders";
        return NULL;
    }
    rows = (FsDir *)calloc(FS_DIR_MAX, sizeof(*rows));
    if (!rows) {
        closedir(dir);
        if (err_out) *err_out = "out of memory";
        return NULL;
    }
    while ((ent = readdir(dir)) != NULL) {
        char child[PATH_MAX];
        size_t name_len;
        if (!ent->d_name[0] || strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!show_hidden && ent->d_name[0] == '.') continue;
        name_len = strlen(ent->d_name);
        if (name_len == 0 || name_len >= sizeof rows[0].name) continue;
        if (fs_join(child, sizeof child, resolved, ent->d_name) != 0) continue;
        if (!fs_is_dir(child)) continue;
        if (count >= FS_DIR_MAX) {
            truncated = 1;
            break;
        }
        memcpy(rows[count].name, ent->d_name, name_len + 1);
        rows[count].website = looks_like_website(child);
        count++;
    }
    closedir(dir);
    if (count > 1) qsort(rows, count, sizeof(*rows), fs_dir_cmp);
    has_parent = fs_parent(resolved, parent, sizeof parent) == 0;
    root = json_object();
    entries = json_array();
    if (!root || !entries) {
        json_free(root);
        json_free(entries);
        free(rows);
        if (err_out) *err_out = "out of memory";
        return NULL;
    }
    if (put_bool(root, "ok", 1) != 0 || put_str(root, "path", resolved) != 0 ||
        put_str(root, "parent", has_parent ? parent : "") != 0 ||
        put_bool(root, "website", looks_like_website(resolved)) != 0 ||
        put_bool(root, "truncated", truncated) != 0) {
        json_free(root);
        json_free(entries);
        free(rows);
        if (err_out) *err_out = "out of memory";
        return NULL;
    }
    if (has_parent) {
        JsonValue *up = fs_entry("..", parent, 1, looks_like_website(parent));
        if (!up || json_array_add(entries, up) != 0) {
            json_free(root);
            json_free(entries);
            free(rows);
            if (err_out) *err_out = "out of memory";
            return NULL;
        }
    }
    for (i = 0; i < count; i++) {
        char child[PATH_MAX];
        JsonValue *item;
        if (fs_join(child, sizeof child, resolved, rows[i].name) != 0) continue;
        item = fs_entry(rows[i].name, child, 0, rows[i].website);
        if (!item || json_array_add(entries, item) != 0) {
            json_free(root);
            json_free(entries);
            free(rows);
            if (err_out) *err_out = "out of memory";
            return NULL;
        }
    }
    free(rows);
    if (json_object_put(root, "entries", entries) != 0) {
        json_free(root);
        if (err_out) *err_out = "out of memory";
        return NULL;
    }
    doc = json_doc_new(root);
    if (!doc && err_out) *err_out = "out of memory";
    return doc;
}

#ifndef _WIN32
static int run_capture(char *const argv[], char *out, size_t out_sz, int *status_out) {
    int fds[2];
    pid_t pid;
    size_t used = 0;
    int status = 0;
    char drain[256];
    if (out && out_sz) out[0] = '\0';
    if (pipe(fds) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        close(fds[0]);
        close(fds[1]);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    for (;;) {
        ssize_t n = read(fds[0], drain, sizeof drain);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        if (out && used + 1 < out_sz) {
            size_t take = (size_t)n;
            if (used + take >= out_sz) take = out_sz - 1 - used;
            memcpy(out + used, drain, take);
            used += take;
            out[used] = '\0';
        }
    }
    close(fds[0]);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            status = -1;
            break;
        }
    }
    if (status_out) {
        if (status >= 0 && WIFEXITED(status)) *status_out = WEXITSTATUS(status);
        else *status_out = -1;
    }
    return 0;
}
#else
static int run_capture_win(char *cmd, char *out, size_t out_sz, int *status_out) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_h = NULL;
    HANDLE write_h = NULL;
    DWORD code = 1;
    size_t used = 0;
    memset(&sa, 0, sizeof sa);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read_h, &write_h, &sa, 0)) return -1;
    SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = NULL;
    si.hStdOutput = write_h;
    si.hStdError = write_h;
    if (out && out_sz) out[0] = '\0';
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(read_h);
        CloseHandle(write_h);
        return -1;
    }
    CloseHandle(write_h);
    for (;;) {
        char drain[256];
        DWORD n = 0;
        if (!ReadFile(read_h, drain, sizeof drain, &n, NULL) || n == 0) break;
        if (out && used + 1 < out_sz) {
            size_t take = n;
            if (used + take >= out_sz) take = out_sz - 1 - used;
            memcpy(out + used, drain, take);
            used += take;
            out[used] = '\0';
        }
    }
    CloseHandle(read_h);
    if (WaitForSingleObject(pi.hProcess, 300000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (status_out) *status_out = (int)code;
    return 0;
}
#endif

static int fs_pick(char *out, size_t out_sz, const char **err_out) {
    char raw[PATH_MAX];
    int code = -1;
    const char *start = settings_get_project_root();
    raw[0] = '\0';
    if (err_out) *err_out = "No native folder picker is available";
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';
#ifdef _WIN32
    {
        char cmd[1200];
        int n = snprintf(cmd, sizeof cmd,
                         "powershell.exe -NoProfile -STA -Command \""
                         "Add-Type -AssemblyName System.Windows.Forms; "
                         "$d = New-Object System.Windows.Forms.FolderBrowserDialog; "
                         "$d.Description = 'Open Project Folder'; "
                         "$d.ShowNewFolderButton = $true; "
                         "if ($d.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { exit 1 }; "
                         "Write-Output $d.SelectedPath\"");
        (void)start;
        if (n < 0 || (size_t)n >= sizeof cmd || run_capture_win(cmd, raw, sizeof raw, &code) != 0) {
            if (err_out) *err_out = "No native folder picker is available";
            return -1;
        }
    }
#else
    {
        const char *display = getenv("DISPLAY");
        const char *wayland = getenv("WAYLAND_DISPLAY");
        int have_gui = (display && display[0]) || (wayland && wayland[0]);
        char start_buf[PATH_MAX];
        int ran = 0;
        if (!have_gui) {
            if (err_out) *err_out = "No native folder picker is available. Type a path or use the folder list.";
            return -1;
        }
        start_buf[0] = '\0';
        if (start && start[0] && strlen(start) + 2 < sizeof start_buf) {
            size_t n = strlen(start);
            memcpy(start_buf, start, n + 1);
            if (start_buf[n - 1] != '/') {
                start_buf[n] = '/';
                start_buf[n + 1] = '\0';
            }
        }
        if (start_buf[0]) {
            char *argv[] = {"zenity", "--file-selection", "--directory", "--title=Open Project Folder",
                            "--filename", start_buf, NULL};
            ran = run_capture(argv, raw, sizeof raw, &code) == 0;
        } else {
            char *argv[] = {"zenity", "--file-selection", "--directory", "--title=Open Project Folder", NULL};
            ran = run_capture(argv, raw, sizeof raw, &code) == 0;
        }
        if (!ran || code == 127 || code == -1) {
            char *argv[6];
            int argc = 0;
            argv[argc++] = "kdialog";
            argv[argc++] = "--title";
            argv[argc++] = "Open Project Folder";
            argv[argc++] = "--getexistingdirectory";
            if (start && start[0]) argv[argc++] = (char *)start;
            argv[argc] = NULL;
            raw[0] = '\0';
            if (run_capture(argv, raw, sizeof raw, &code) != 0) {
                if (err_out) *err_out = "No native folder picker is available. Type a path or use the folder list.";
                return -1;
            }
        }
    }
#endif
    trim_inplace(raw);
    {
        char *nl = strpbrk(raw, "\r\n");
        if (nl) *nl = '\0';
        trim_inplace(raw);
    }
    if (code == 1 || raw[0] == '\0') {
        if (err_out) *err_out = "Folder picker was cancelled";
        return 1;
    }
    if (code != 0) {
        if (err_out) *err_out = "No native folder picker is available. Type a path or use the folder list.";
        return -1;
    }
    if (settings_canonicalize_dir(raw, out, out_sz) != 0) {
        if (err_out) *err_out = "The chosen folder is not a directory";
        return -1;
    }
    return 0;
}

static int handle_dirs(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char query_path[PATH_MAX];
    char resolved[PATH_MAX];
    const char *err = NULL;
    const char *fallback;
    JsonDoc *doc;
    if (!method_is(req, "GET")) return send_error(res, 405, "Use GET to list folders");
    query_path[0] = '\0';
    http_query_param(req->query, "path", query_path, sizeof query_path);
    trim_inplace(query_path);
    if (!query_path[0]) {
        fallback = settings_get_project_root();
        if (!fallback || !fallback[0]) fallback = repo_root;
        if (!fallback || strlen(fallback) >= sizeof query_path) {
            return send_error(res, 400, "Path is required");
        }
        memcpy(query_path, fallback, strlen(fallback) + 1);
    }
    if (resolve_directory(query_path, resolved, sizeof resolved, &err) != 0) {
        return send_error(res, 400, err ? err : "Folder was not found");
    }
    doc = fs_list_document(resolved, query_flag(req, "hidden") || query_flag(req, "dot"), &err);
    if (!doc) {
        int status = err && strstr(err, "cannot be read") ? 403 : 500;
        return send_error(res, status, err ? err : "Could not list folders");
    }
    return send_doc(res, 200, doc);
}

static int handle_pick_folder(const HttpRequest *req, HttpResponse *res) {
    char chosen[PATH_MAX];
    const char *err = NULL;
    int rc;
    JsonValue *root;
    if (!method_is(req, "POST")) return send_error(res, 405, "Use POST to pick a folder");
    rc = fs_pick(chosen, sizeof chosen, &err);
    if (rc == 1) return send_error(res, 400, err ? err : "Folder picker was cancelled");
    if (rc != 0) {
        return send_error(res, 501, err ? err : "No native folder picker is available. Type a path or use the folder list.");
    }
    root = json_object();
    if (!root || put_bool(root, "ok", 1) != 0 || put_str(root, "path", chosen) != 0) {
        json_free(root);
        return send_error(res, 500, "out of memory");
    }
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_project_open(const HttpRequest *req, HttpResponse *res) {
    JsonDoc *body;
    char *text;
    char resolved[PATH_MAX];
    char previous[PATH_MAX];
    const char *err = NULL;
    const char *current;
    JsonValue *root;
    if (!method_is(req, "POST")) return send_error(res, 405, "Use POST to open a project folder");
    body = parse_object_body(req, res);
    if (!body) return 0;
    text = json_object_text(body->root, "path");
    json_doc_free(body);
    if (!text) return send_error(res, 400, "path is required");
    trim_inplace(text);
    if (!text[0]) {
        free(text);
        return send_error(res, 400, "path is required");
    }
    if (resolve_directory(text, resolved, sizeof resolved, &err) != 0) {
        free(text);
        return send_error(res, 400, err ? err : "Folder was not found");
    }
    free(text);
    current = settings_get_project_root();
    previous[0] = '\0';
    if (current && current[0] && strlen(current) < sizeof previous) {
        memcpy(previous, current, strlen(current) + 1);
    }
    if (settings_set_project_root(resolved) != 0) {
        return send_error(res, 500, "Could not save the project folder");
    }
    if (studio_restart_php_for_root(resolved) != 0) {
        if (previous[0]) {
            settings_set_project_root(previous);
            studio_restart_php_for_root(previous);
        }
        return send_error(res, 500, "PHP did not restart on the new folder");
    }
    current = settings_get_project_root();
    root = json_object();
    if (!root || put_bool(root, "ok", 1) != 0 || put_str(root, "repoRoot", current ? current : resolved) != 0 ||
        json_object_put(root, "phpPort", json_number_int(settings_get_php_port())) != 0) {
        json_free(root);
        return send_error(res, 500, "out of memory");
    }
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_quit(const HttpRequest *req, HttpResponse *res) {
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to quit");
    }
    studio_window_request_close();
    return send_json_text(res, 200, strdup("{\"ok\":true}"));
}

static int handle_config_get(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char file_path[PATH_MAX];
    char dir_path[PATH_MAX];
    JsonValue *root;
    JsonValue *cfg;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for config");
    }
    file_path[0] = '\0';
    dir_path[0] = '\0';
    config_resolve_paths(base, file_path, sizeof(file_path), dir_path, sizeof(dir_path));
    config_load(base);
    root = json_object();
    cfg = config_to_json();
    if (!root || !cfg) {
        json_free(root);
        json_free(cfg);
        return send_error(res, 500, "out of memory");
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "path", json_string_cstr(CONFIG_PUBLIC_PATH));
    json_object_put(root, "fsPath", json_string_cstr(file_path));
    json_object_put(root, "folder", json_string_cstr(dir_path));
    json_object_put(root, "config", cfg);
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_config_post(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char *copy;
    char err[256];
    int rc;
    JsonValue *root;
    JsonValue *cfg;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for config");
    }
    if (!req->body || req->body_len == 0) {
        return send_error(res, 400, "JSON body is required");
    }
    copy = (char *)malloc(req->body_len + 1);
    if (!copy) {
        return send_error(res, 500, "out of memory");
    }
    memcpy(copy, req->body, req->body_len);
    copy[req->body_len] = '\0';
    err[0] = '\0';
    rc = config_save_text(base, copy, err, sizeof(err));
    free(copy);
    if (rc != 0) {
        return send_error(res, rc == 1 ? 400 : 500, err[0] ? err : "Could not write config.json");
    }
    root = json_object();
    cfg = config_to_json();
    if (!root || !cfg) {
        json_free(root);
        json_free(cfg);
        return send_error(res, 500, "out of memory");
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "config", cfg);
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_config_open_folder(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char dir_path[PATH_MAX];
    char err[256];
    JsonValue *root;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to open the config folder");
    }
    dir_path[0] = '\0';
    err[0] = '\0';
    if (config_open_folder(base, dir_path, sizeof(dir_path), err, sizeof(err)) != 0) {
        return send_error(res, 500, err[0] ? err : "Could not open the config folder");
    }
    root = json_object();
    if (!root || put_bool(root, "ok", 1) != 0 || put_str(root, "folder", dir_path) != 0) {
        json_free(root);
        return send_error(res, 500, "out of memory");
    }
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_config_backup(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char backup_path[PATH_MAX];
    char err[256];
    JsonValue *root;
    JsonValue *cfg;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to back up config");
    }
    backup_path[0] = '\0';
    err[0] = '\0';
    if (config_backup(base, backup_path, sizeof(backup_path), err, sizeof(err)) != 0) {
        return send_error(res, 500, err[0] ? err : "Could not back up config.json");
    }
    root = json_object();
    cfg = config_to_json();
    if (!root || !cfg || put_bool(root, "ok", 1) != 0 || put_str(root, "backupPath", backup_path) != 0) {
        json_free(root);
        json_free(cfg);
        return send_error(res, 500, "out of memory");
    }
    json_object_put(root, "config", cfg);
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_config_recover(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char err[256];
    int rc;
    JsonDoc *doc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to recover config");
    }
    err[0] = '\0';
    rc = config_recover(base, err, sizeof(err));
    if (rc != 0) {
        int status = (rc == 2) ? 404 : (rc == 1 ? 400 : 500);
        return send_error(res, status, err[0] ? err : "Could not recover config.json");
    }
    doc = config_page_document(base, err, sizeof(err));
    if (!doc) {
        return send_error(res, 500, err[0] ? err : "Recovered, but config.json could not be reloaded");
    }
    return send_doc(res, 200, doc);
}

static int handle_config_clear(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char err[256];
    int rc;
    JsonDoc *doc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to clear config");
    }
    err[0] = '\0';
    rc = config_clear(base, err, sizeof(err));
    if (rc != 0) {
        return send_error(res, 500, err[0] ? err : "Could not clear config.json");
    }
    doc = config_page_document(base, err, sizeof(err));
    if (!doc) {
        return send_error(res, 500, err[0] ? err : "Cleared, but config.json could not be reloaded");
    }
    return send_doc(res, 200, doc);
}

static int handle_search_query(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char err[512];
    JsonDoc *doc = NULL;
    int rc;
    err[0] = '\0';
    if (method_is(req, "POST")) {
        JsonDoc *body = parse_object_body(req, res);
        if (!body) return 0;
        rc = project_search_query(repo_root, body->root, &doc, err, sizeof(err));
        json_doc_free(body);
    } else if (method_is(req, "GET")) {
        JsonValue *params = json_object();
        char buf[1024];
        if (!params) return send_error(res, 500, "out of memory");
        query_copy(req, "query", buf, sizeof(buf));
        if (buf[0]) json_object_put(params, "query", json_string_cstr(buf));
        query_copy(req, "replacement", buf, sizeof(buf));
        if (strstr(req->query, "replacement=") != NULL) {
            json_object_put(params, "replacement", json_string_cstr(buf));
        }
        query_copy(req, "mode", buf, sizeof(buf));
        if (buf[0]) json_object_put(params, "mode", json_string_cstr(buf));
        query_copy(req, "scope", buf, sizeof(buf));
        if (buf[0]) json_object_put(params, "scope", json_string_cstr(buf));
        query_copy(req, "include", buf, sizeof(buf));
        if (buf[0]) json_object_put(params, "include", json_string_cstr(buf));
        query_copy(req, "exclude", buf, sizeof(buf));
        if (buf[0]) json_object_put(params, "exclude", json_string_cstr(buf));
        json_object_put(params, "caseSensitive", json_bool(query_flag(req, "caseSensitive")));
        json_object_put(params, "wholeWord", json_bool(query_flag(req, "wholeWord")));
        rc = project_search_query(repo_root, params, &doc, err, sizeof(err));
        json_free(params);
    } else {
        return send_error(res, 405, "Use GET or POST for project search");
    }
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Search failed");
    }
    return send_doc(res, 200, doc);
}

static int handle_search_replace(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for project replace");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = project_search_replace(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Replace failed");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_status(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for git status");
    }
    err[0] = '\0';
    rc = git_scm_status(repo_root, &doc, err, sizeof(err));
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not read Git status");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_stage(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to stage changes");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = git_scm_stage(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not stage changes");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_unstage(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to unstage changes");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = git_scm_unstage(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not unstage changes");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_discard(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to discard changes");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = git_scm_discard(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not discard changes");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_commit(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to commit changes");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = git_scm_commit(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not commit changes");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_diff(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char path[2048];
    char commit[128];
    char err[512];
    JsonDoc *doc = NULL;
    int staged;
    int rc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for git diff");
    }
    query_copy(req, "path", path, sizeof(path));
    query_copy(req, "commit", commit, sizeof(commit));
    staged = query_flag(req, "staged");
    err[0] = '\0';
    rc = git_scm_diff(repo_root, path, staged, commit, &doc, err, sizeof(err));
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not read Git diff");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_fetch(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body = NULL;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to fetch Git remote updates");
    }
    if (req->body && req->body_len > 0) {
        char perr[128];
        perr[0] = '\0';
        body = json_parse(req->body, req->body_len, perr, sizeof(perr));
    }
    err[0] = '\0';
    rc = git_scm_fetch(repo_root, body ? body->root : NULL, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not fetch from Git remote");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_pull(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body = NULL;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to pull Git remote changes");
    }
    if (req->body && req->body_len > 0) {
        char perr[128];
        perr[0] = '\0';
        body = json_parse(req->body, req->body_len, perr, sizeof(perr));
    }
    err[0] = '\0';
    rc = git_scm_pull(repo_root, body ? body->root : NULL, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not pull from Git remote");
    }
    return send_doc(res, 200, doc);
}

static int handle_git_push(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body = NULL;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST to push Git commits");
    }
    if (req->body && req->body_len > 0) {
        char perr[128];
        perr[0] = '\0';
        body = json_parse(req->body, req->body_len, perr, sizeof(perr));
    }
    err[0] = '\0';
    rc = git_scm_push(repo_root, body ? body->root : NULL, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not push to Git remote");
    }
    return send_doc(res, 200, doc);
}

static int handle_app_config_get(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    char file_path[PATH_MAX];
    JsonValue *root;
    JsonValue *app_cfg;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for app-config");
    }
    file_path[0] = '\0';
    app_config_resolve_path(base, file_path, sizeof(file_path));
    app_config_load(base);
    root = json_object();
    app_cfg = app_config_to_json();
    if (!root || !app_cfg) {
        json_free(root);
        json_free(app_cfg);
        return send_error(res, 500, "out of memory");
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "fsPath", json_string_cstr(file_path));
    json_object_put(root, "appConfig", app_cfg);
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_app_config_post(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *base = state_root(repo_root);
    JsonDoc *body;
    char file_path[PATH_MAX];
    char err[256];
    int rc;
    JsonValue *root;
    JsonValue *app_cfg;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for app-config");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = app_config_apply_update(base, body->root, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0) {
        return send_error(res, rc == 1 ? 400 : 500, err[0] ? err : "Could not save app-config.json");
    }
    file_path[0] = '\0';
    app_config_resolve_path(base, file_path, sizeof(file_path));
    root = json_object();
    app_cfg = app_config_to_json();
    if (!root || !app_cfg) {
        json_free(root);
        json_free(app_cfg);
        return send_error(res, 500, "out of memory");
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "fsPath", json_string_cstr(file_path));
    json_object_put(root, "appConfig", app_cfg);
    return send_doc(res, 200, json_doc_new(root));
}

static int handle_terminal_info(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    char cwd[2048];
    char err[512];
    JsonDoc *doc = NULL;
    int rc;
    if (!method_is(req, "GET")) {
        return send_error(res, 405, "Use GET for terminal info");
    }
    query_copy(req, "cwd", cwd, sizeof(cwd));
    err[0] = '\0';
    rc = terminal_info(repo_root, cwd, &doc, err, sizeof(err));
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Could not read terminal info");
    }
    return send_doc(res, 200, doc);
}

static int handle_terminal_exec(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for terminal exec");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = terminal_exec(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Terminal command execution failed");
    }
    return send_doc(res, 200, doc);
}

static int handle_terminal_start(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for terminal start");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    err[0] = '\0';
    rc = terminal_start(repo_root, body->root, &doc, err, sizeof(err));
    json_doc_free(body);
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Terminal command start failed");
    }
    return send_doc(res, 200, doc);
}

static int handle_terminal_poll(const HttpRequest *req, HttpResponse *res) {
    char id_buf[64];
    char off_buf[64];
    char err[512];
    int job_id = 0;
    size_t offset = 0;
    JsonDoc *doc = NULL;
    int rc;

    if (method_is(req, "GET")) {
        query_copy(req, "id", id_buf, sizeof(id_buf));
        query_copy(req, "offset", off_buf, sizeof(off_buf));
        if (id_buf[0]) job_id = (int)strtol(id_buf, NULL, 10);
        if (off_buf[0]) {
            long long v = strtoll(off_buf, NULL, 10);
            if (v > 0) offset = (size_t)v;
        }
    } else if (method_is(req, "POST")) {
        JsonDoc *body = parse_object_body(req, res);
        long long v = 0;
        if (!body) return 0;
        if (json_as_int(json_object_get(body->root, "id"), &v) == 0) job_id = (int)v;
        if (json_as_int(json_object_get(body->root, "offset"), &v) == 0 && v > 0) offset = (size_t)v;
        json_doc_free(body);
    } else {
        return send_error(res, 405, "Use GET or POST for terminal poll");
    }

    err[0] = '\0';
    rc = terminal_poll(job_id, offset, &doc, err, sizeof(err));
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Terminal poll failed");
    }
    return send_doc(res, 200, doc);
}

static int handle_terminal_stop(const HttpRequest *req, HttpResponse *res) {
    JsonDoc *body;
    JsonDoc *doc = NULL;
    char err[512];
    long long v = 0;
    int job_id = 0;
    int rc;
    if (!method_is(req, "POST")) {
        return send_error(res, 405, "Use POST for terminal stop");
    }
    body = parse_object_body(req, res);
    if (!body) return 0;
    if (json_as_int(json_object_get(body->root, "id"), &v) == 0) job_id = (int)v;
    json_doc_free(body);
    err[0] = '\0';
    rc = terminal_stop(job_id, &doc, err, sizeof(err));
    if (rc != 0 || !doc) {
        json_doc_free(doc);
        return send_gallery_status(res, rc, err[0] ? err : "Terminal stop failed");
    }
    return send_doc(res, 200, doc);
}

int api_handle_request(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *rest;
    if (!req || !res) {
        return -1;
    }
    rest = api_rest(req->path);
    if (!rest) {
        return -1;
    }
    if (!repo_root || !repo_root[0]) {
        return send_error(res, 500, "Repository root is not configured");
    }
    if (route_is(rest, "/project/open")) {
        return handle_project_open(req, res);
    }
    if (route_is(rest, "/fs/dirs")) {
        return handle_dirs(req, res, repo_root);
    }
    if (route_is(rest, "/fs/pick-folder")) {
        return handle_pick_folder(req, res);
    }
    if (route_is(rest, "/health")) {
        return handle_health(req, res, repo_root);
    }
    if (route_is(rest, "/tree")) {
        return handle_tree(req, res, repo_root);
    }
    if (route_is(rest, "/templates")) {
        return handle_templates(req, res);
    }
    if (route_is(rest, "/page/create")) {
        return handle_page_create(req, res, repo_root);
    }
    if (route_is(rest, "/fs/rename") || route_is(rest, "/rename") ||
        route_is(rest, "/page/rename") || route_is(rest, "/folder/rename")) {
        return handle_fs_rename(req, res, repo_root);
    }
    if (route_is(rest, "/preview/stage")) {
        return handle_preview_stage(req, res, repo_root);
    }
    if (route_is(rest, "/preview/clear")) {
        return handle_preview_clear(req, res, repo_root);
    }
    if (route_is(rest, "/page/save")) {
        return handle_page_save(req, res, repo_root);
    }
    if (route_is(rest, "/page")) {
        return handle_page_get(req, res, repo_root);
    }
    if (route_is(rest, "/widgets/apply")) {
        return handle_widgets_apply(req, res, repo_root);
    }
    if (route_is(rest, "/folders")) {
        return handle_folders(req, res, repo_root);
    }
    if (route_is(rest, "/gallery/save")) {
        return handle_gallery_save(req, res, repo_root);
    }
    if (route_is(rest, "/gallery/revert")) {
        return handle_revert(req, res, repo_root);
    }
    if (route_is(rest, "/gallery")) {
        return handle_gallery_get(req, res, repo_root);
    }
    if (route_is(rest, "/details/save")) {
        return handle_details_save(req, res, repo_root);
    }
    if (route_is(rest, "/session")) {
        if (method_is(req, "POST")) {
            return handle_session_post(req, res, repo_root);
        }
        if (method_is(req, "GET")) {
            return handle_session_get(req, res, repo_root);
        }
        return send_error(res, 405, "Use GET or POST for session");
    }
    if (route_is(rest, "/theme")) {
        if (method_is(req, "POST")) {
            return handle_theme_post(req, res, repo_root);
        }
        return handle_theme_get(req, res, repo_root);
    }
    if (route_is(rest, "/quit")) {
        return handle_quit(req, res);
    }
    if (route_is(rest, "/config/open-folder")) {
        return handle_config_open_folder(req, res, repo_root);
    }
    if (route_is(rest, "/config/backup")) {
        return handle_config_backup(req, res, repo_root);
    }
    if (route_is(rest, "/config/recover")) {
        return handle_config_recover(req, res, repo_root);
    }
    if (route_is(rest, "/config/clear")) {
        return handle_config_clear(req, res, repo_root);
    }
    if (route_is(rest, "/app-config")) {
        if (method_is(req, "POST")) {
            return handle_app_config_post(req, res, repo_root);
        }
        if (method_is(req, "GET")) {
            return handle_app_config_get(req, res, repo_root);
        }
        return send_error(res, 405, "Use GET or POST for app-config");
    }
    if (route_is(rest, "/config")) {
        if (method_is(req, "POST")) {
            return handle_config_post(req, res, repo_root);
        }
        if (method_is(req, "GET")) {
            return handle_config_get(req, res, repo_root);
        }
        return send_error(res, 405, "Use GET or POST for config");
    }
    if (route_is(rest, "/search/replace")) {
        return handle_search_replace(req, res, repo_root);
    }
    if (route_is(rest, "/search")) {
        return handle_search_query(req, res, repo_root);
    }
    if (route_is(rest, "/git/status")) {
        return handle_git_status(req, res, repo_root);
    }
    if (route_is(rest, "/git/stage")) {
        return handle_git_stage(req, res, repo_root);
    }
    if (route_is(rest, "/git/unstage")) {
        return handle_git_unstage(req, res, repo_root);
    }
    if (route_is(rest, "/git/discard")) {
        return handle_git_discard(req, res, repo_root);
    }
    if (route_is(rest, "/git/commit")) {
        return handle_git_commit(req, res, repo_root);
    }
    if (route_is(rest, "/git/diff")) {
        return handle_git_diff(req, res, repo_root);
    }
    if (route_is(rest, "/git/fetch")) {
        return handle_git_fetch(req, res, repo_root);
    }
    if (route_is(rest, "/git/pull")) {
        return handle_git_pull(req, res, repo_root);
    }
    if (route_is(rest, "/git/push")) {
        return handle_git_push(req, res, repo_root);
    }
    if (route_is(rest, "/terminal/info") || route_is(rest, "/terminal/status")) {
        return handle_terminal_info(req, res, repo_root);
    }
    if (route_is(rest, "/terminal/exec")) {
        return handle_terminal_exec(req, res, repo_root);
    }
    if (route_is(rest, "/terminal/start")) {
        return handle_terminal_start(req, res, repo_root);
    }
    if (route_is(rest, "/terminal/poll")) {
        return handle_terminal_poll(req, res);
    }
    if (route_is(rest, "/terminal/stop")) {
        return handle_terminal_stop(req, res);
    }
    if (route_is(rest, "/terminal")) {
        if (method_is(req, "POST")) {
            return handle_terminal_exec(req, res, repo_root);
        }
        if (method_is(req, "GET")) {
            return handle_terminal_info(req, res, repo_root);
        }
        return send_error(res, 405, "Use GET or POST for terminal");
    }
    return send_error(res, 404, "Unknown API endpoint");
}
