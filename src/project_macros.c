#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "project_macros.h"

#include "http.h"
#include "settings.h"

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
#else
#include <pthread.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_MACROS_PER_PROJECT 64
#define MAX_FIELDS_PER_MACRO   32

#ifdef _WIN32
static CRITICAL_SECTION g_macros_lock;
static INIT_ONCE g_macros_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK macros_lock_init(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_macros_lock);
    return TRUE;
}

static void macros_lock(void) {
    InitOnceExecuteOnce(&g_macros_once, macros_lock_init, NULL, NULL);
    EnterCriticalSection(&g_macros_lock);
}

static void macros_unlock(void) {
    LeaveCriticalSection(&g_macros_lock);
}
#else
static pthread_mutex_t g_macros_lock = PTHREAD_MUTEX_INITIALIZER;

static void macros_lock(void) {
    pthread_mutex_lock(&g_macros_lock);
}

static void macros_unlock(void) {
    pthread_mutex_unlock(&g_macros_lock);
}
#endif

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg ? msg : "");
    }
}

static const char *effective_studio_home(const char *fallback_root) {
    const char *home = settings_get_studio_home();
    if (home && home[0]) return home;
    return fallback_root;
}

int project_macros_resolve_path(const char *fallback_root, char *file_out, size_t file_sz) {
    const char *base = effective_studio_home(fallback_root);
    char cfg_file[PATH_MAX];
    if (!base || !base[0] || !file_out || file_sz == 0) return -1;
    if (settings_install_path(base, "state/project-macros.json", cfg_file, sizeof(cfg_file)) != 0) return -1;
    if (strlen(cfg_file) + 1 > file_sz) return -1;
    memcpy(file_out, cfg_file, strlen(cfg_file) + 1);
    return 0;
}

static int is_dir_path(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int detect_classiccars_site(const char *repo_root) {
    char sales[PATH_MAX];
    char includes[PATH_MAX];
    if (!repo_root || !repo_root[0]) return 0;
    snprintf(sales, sizeof(sales), "%s/sales", repo_root);
    snprintf(includes, sizeof(includes), "%s/includes", repo_root);
    return is_dir_path(sales) && is_dir_path(includes);
}

static int valid_macro_id(const char *id) {
    size_t i, n;
    if (!id || !id[0]) return 0;
    n = strlen(id);
    if (n > 56) return 0;
    if (!isalpha((unsigned char)id[0])) return 0;
    for (i = 1; i < n; i++) {
        unsigned char c = (unsigned char)id[i];
        if (isalnum(c) || c == '_' || c == '-') continue;
        return 0;
    }
    return 1;
}

static const char *jstr_or(const JsonValue *obj, const char *key, const char *def) {
    JsonValue *v;
    const char *s;
    if (!obj || json_type(obj) != JSON_OBJECT) return def;
    v = json_object_get(obj, key);
    if (!v || json_type(v) != JSON_STRING) return def;
    s = json_string_get(v, NULL);
    return s ? s : def;
}

static int jbool_or(const JsonValue *obj, const char *key, int def) {
    JsonValue *v;
    if (!obj || json_type(obj) != JSON_OBJECT) return def;
    v = json_object_get(obj, key);
    if (!v) return def;
    if (json_type(v) == JSON_BOOL) return json_bool_value(v) ? 1 : 0;
    return def;
}

static JsonValue *sanitize_field(const JsonValue *raw, char *err, size_t err_sz) {
    const char *key;
    const char *label;
    const char *type;
    JsonValue *out;
    JsonValue *opts;
    if (!raw || json_type(raw) != JSON_OBJECT) {
        set_err(err, err_sz, "Macro field must be an object");
        return NULL;
    }
    key = jstr_or(raw, "key", "");
    if (!valid_macro_id(key)) {
        set_err(err, err_sz, "Field key must start with a letter and contain only letters, digits, _ or -");
        return NULL;
    }
    label = jstr_or(raw, "label", key);
    type = jstr_or(raw, "type", "text");
    out = json_object();
    if (!out) return NULL;
    json_object_put(out, "key", json_string_cstr(key));
    json_object_put(out, "label", json_string_cstr(label[0] ? label : key));
    json_object_put(out, "type", json_string_cstr(type[0] ? type : "text"));
    json_object_put(out, "defaultValue", json_string_cstr(jstr_or(raw, "defaultValue", "")));
    json_object_put(out, "required", json_bool(jbool_or(raw, "required", 0)));
    json_object_put(out, "pattern", json_string_cstr(jstr_or(raw, "pattern", "")));
    json_object_put(out, "hint", json_string_cstr(jstr_or(raw, "hint", "")));
    opts = json_object_get(raw, "options");
    if (opts && json_type(opts) == JSON_ARRAY) {
        json_object_put(out, "options", json_clone(opts));
    } else if (opts && json_type(opts) == JSON_STRING) {
        json_object_put(out, "options", json_string_cstr(json_string_get(opts, NULL)));
    } else {
        json_object_put(out, "options", json_array());
    }
    return out;
}

static JsonValue *sanitize_macro(const JsonValue *raw, char *err, size_t err_sz) {
    const char *id;
    const char *title;
    const char *template_str;
    const char *insert_target;
    JsonValue *out;
    JsonValue *applies_in;
    JsonValue *applies_out;
    JsonValue *val_in;
    JsonValue *val_out;
    JsonValue *fields_in;
    JsonValue *fields_out;
    size_t i, n;

    if (!raw || json_type(raw) != JSON_OBJECT) {
        set_err(err, err_sz, "Macro definition must be an object");
        return NULL;
    }
    id = jstr_or(raw, "id", "");
    if (!valid_macro_id(id)) {
        set_err(err, err_sz, "Macro id must start with a letter and contain only letters, digits, _ or -");
        return NULL;
    }
    title = jstr_or(raw, "title", "");
    if (!title[0]) {
        set_err(err, err_sz, "Macro title is required");
        return NULL;
    }
    template_str = jstr_or(raw, "template", "");
    if (!template_str[0]) {
        set_err(err, err_sz, "Macro template is required");
        return NULL;
    }
    insert_target = jstr_or(raw, "insertTarget", "cursor");
    out = json_object();
    applies_out = json_object();
    val_out = json_object();
    fields_out = json_array();
    if (!out || !applies_out || !val_out || !fields_out) {
        json_free(out);
        json_free(applies_out);
        json_free(val_out);
        json_free(fields_out);
        set_err(err, err_sz, "Out of memory");
        return NULL;
    }
    json_object_put(out, "id", json_string_cstr(id));
    json_object_put(out, "title", json_string_cstr(title));
    json_object_put(out, "description", json_string_cstr(jstr_or(raw, "description", "")));
    json_object_put(out, "category", json_string_cstr(jstr_or(raw, "category", "Macro")));
    json_object_put(out, "enabled", json_bool(jbool_or(raw, "enabled", 1)));
    json_object_put(out, "insertTarget", json_string_cstr(insert_target[0] ? insert_target : "cursor"));
    json_object_put(out, "startMarker", json_string_cstr(jstr_or(raw, "startMarker", "")));
    json_object_put(out, "endMarker", json_string_cstr(jstr_or(raw, "endMarker", "")));
    json_object_put(out, "template", json_string_cstr(template_str));

    applies_in = json_object_get(raw, "appliesTo");
    json_object_put(applies_out, "extensions",
                    json_string_cstr(jstr_or(applies_in, "extensions", ".html, .htm, .shtml, .php")));
    json_object_put(applies_out, "sections",
                    json_string_cstr(jstr_or(applies_in, "sections", "*")));
    json_object_put(out, "appliesTo", applies_out);

    val_in = json_object_get(raw, "validation");
    json_object_put(val_out, "requiredOnPage", json_bool(jbool_or(val_in, "requiredOnPage", 0)));
    json_object_put(val_out, "checkFormatting", json_bool(jbool_or(val_in, "checkFormatting", 1)));
    {
        const char *sev = jstr_or(val_in, "severity", "warning");
        if (strcmp(sev, "error") != 0 && strcmp(sev, "warning") != 0) sev = "warning";
        json_object_put(val_out, "severity", json_string_cstr(sev));
    }
    json_object_put(out, "validation", val_out);

    fields_in = json_object_get(raw, "fields");
    if (fields_in && json_type(fields_in) == JSON_ARRAY) {
        n = json_array_len(fields_in);
        if (n > MAX_FIELDS_PER_MACRO) n = MAX_FIELDS_PER_MACRO;
        for (i = 0; i < n; i++) {
            JsonValue *f = sanitize_field(json_array_at(fields_in, i), err, err_sz);
            if (!f) {
                json_free(out);
                json_free(fields_out);
                return NULL;
            }
            json_array_add(fields_out, f);
        }
    }
    json_object_put(out, "fields", fields_out);
    return out;
}

static const char *PRESETS_JSON =
    "["
    "{\"id\":\"page-meta\",\"title\":\"Page Metadata Comment\","
    "\"description\":\"Structured HTML comment block for page metadata (author, status, date, notes).\","
    "\"category\":\"Metadata\",\"enabled\":true,\"insertTarget\":\"head-end\","
    "\"startMarker\":\"<!-- page-meta\",\"endMarker\":\"-->\","
    "\"appliesTo\":{\"extensions\":\".html, .htm, .shtml, .php\",\"sections\":\"*\"},"
    "\"template\":\"<!-- page-meta\\n  author: {{author}}\\n  status: {{status}}\\n  updated: {{updated}}\\n  notes: {{notes}}\\n-->\","
    "\"fields\":["
    "{\"key\":\"author\",\"label\":\"Author\",\"type\":\"text\",\"defaultValue\":\"\",\"required\":true,\"pattern\":\"\",\"hint\":\"Page author or editor\",\"options\":[]},"
    "{\"key\":\"status\",\"label\":\"Status\",\"type\":\"select\",\"defaultValue\":\"draft\",\"required\":true,\"pattern\":\"\",\"hint\":\"Editorial status\",\"options\":[\"draft\",\"review\",\"published\",\"archived\"]},"
    "{\"key\":\"updated\",\"label\":\"Updated Date\",\"type\":\"date\",\"defaultValue\":\"\",\"required\":false,\"pattern\":\"^\\\\d{4}-\\\\d{2}-\\\\d{2}$\",\"hint\":\"YYYY-MM-DD\",\"options\":[]},"
    "{\"key\":\"notes\",\"label\":\"Notes\",\"type\":\"textarea\",\"defaultValue\":\"\",\"required\":false,\"pattern\":\"\",\"hint\":\"Optional notes\",\"options\":[]}"
    "],\"validation\":{\"requiredOnPage\":false,\"checkFormatting\":true,\"severity\":\"warning\"}},"
    "{\"id\":\"ssi-include\",\"title\":\"Server-Side Include (SSI)\","
    "\"description\":\"Virtual include directive for shared site headers, footers, or embeds.\","
    "\"category\":\"Include\",\"enabled\":true,\"insertTarget\":\"cursor\","
    "\"startMarker\":\"<!--#include virtual=\\\"\",\"endMarker\":\"\\\"-->\","
    "\"appliesTo\":{\"extensions\":\".html, .htm, .shtml, .php\",\"sections\":\"*\"},"
    "\"template\":\"<!--#include virtual=\\\"{{includePath}}\\\"-->\","
    "\"fields\":["
    "{\"key\":\"includePath\",\"label\":\"Virtual Path\",\"type\":\"text\",\"defaultValue\":\"/includes/header.html\",\"required\":true,\"pattern\":\"^/[A-Za-z0-9_./?=&-]+$\",\"hint\":\"Root-relative path starting with /\",\"options\":[]}"
    "],\"validation\":{\"requiredOnPage\":false,\"checkFormatting\":true,\"severity\":\"error\"}},"
    "{\"id\":\"callout-section\",\"title\":\"Callout / Feature Section\","
    "\"description\":\"Customizable HTML section snippet with CSS class, heading, and body copy.\","
    "\"category\":\"Snippet\",\"enabled\":true,\"insertTarget\":\"cursor\","
    "\"startMarker\":\"<!-- macro:callout-start -->\",\"endMarker\":\"<!-- macro:callout-end -->\","
    "\"appliesTo\":{\"extensions\":\".html, .htm, .shtml, .php\",\"sections\":\"*\"},"
    "\"template\":\"<!-- macro:callout-start -->\\n<section class=\\\"{{className}}\\\">\\n  <h2>{{heading}}</h2>\\n  <p>{{body}}</p>\\n</section>\\n<!-- macro:callout-end -->\","
    "\"fields\":["
    "{\"key\":\"className\",\"label\":\"CSS Class\",\"type\":\"text\",\"defaultValue\":\"feature-callout\",\"required\":true,\"pattern\":\"^[A-Za-z0-9_ -]+$\",\"hint\":\"Section class name(s)\",\"options\":[]},"
    "{\"key\":\"heading\",\"label\":\"Heading\",\"type\":\"text\",\"defaultValue\":\"Featured Note\",\"required\":true,\"pattern\":\"\",\"hint\":\"Section heading\",\"options\":[]},"
    "{\"key\":\"body\",\"label\":\"Body Text\",\"type\":\"textarea\",\"defaultValue\":\"Add your callout details here.\",\"required\":false,\"pattern\":\"\",\"hint\":\"Paragraph content\",\"options\":[]}"
    "],\"validation\":{\"requiredOnPage\":false,\"checkFormatting\":true,\"severity\":\"warning\"}},"
    "{\"id\":\"custom-analytics\",\"title\":\"Analytics / Tracking Script\","
    "\"description\":\"Custom analytics or tracking script tag injected before </head>.\","
    "\"category\":\"Tracking\",\"enabled\":true,\"insertTarget\":\"head-end\","
    "\"startMarker\":\"<!-- macro:analytics-start -->\",\"endMarker\":\"<!-- macro:analytics-end -->\","
    "\"appliesTo\":{\"extensions\":\".html, .htm, .shtml, .php\",\"sections\":\"*\"},"
    "\"template\":\"<!-- macro:analytics-start -->\\n<script defer data-domain=\\\"{{domain}}\\\" src=\\\"{{scriptUrl}}\\\"></script>\\n<!-- macro:analytics-end -->\","
    "\"fields\":["
    "{\"key\":\"domain\",\"label\":\"Site Domain\",\"type\":\"text\",\"defaultValue\":\"example.com\",\"required\":true,\"pattern\":\"^[A-Za-z0-9.-]+\\\\.[A-Za-z]{2,}$\",\"hint\":\"Tracked domain name\",\"options\":[]},"
    "{\"key\":\"scriptUrl\",\"label\":\"Script URL\",\"type\":\"text\",\"defaultValue\":\"https://plausible.io/js/script.js\",\"required\":true,\"pattern\":\"^https?://.+\",\"hint\":\"Full https:// URL to script\",\"options\":[]}"
    "],\"validation\":{\"requiredOnPage\":false,\"checkFormatting\":true,\"severity\":\"warning\"}},"
    "{\"id\":\"php-page-vars\",\"title\":\"PHP Page Variables\","
    "\"description\":\"Top-of-file PHP configuration block for template variables.\","
    "\"category\":\"PHP\",\"enabled\":true,\"insertTarget\":\"top\","
    "\"startMarker\":\"/* macro:php-vars-start */\",\"endMarker\":\"/* macro:php-vars-end */\","
    "\"appliesTo\":{\"extensions\":\".php\",\"sections\":\"*\"},"
    "\"template\":\"<?php\\n/* macro:php-vars-start */\\n$pageTitle = '{{pageTitle}}';\\n$pageLayout = '{{pageLayout}}';\\n/* macro:php-vars-end */\\n?>\","
    "\"fields\":["
    "{\"key\":\"pageTitle\",\"label\":\"Page Title ($pageTitle)\",\"type\":\"text\",\"defaultValue\":\"\",\"required\":true,\"pattern\":\"\",\"hint\":\"PHP $pageTitle value\",\"options\":[]},"
    "{\"key\":\"pageLayout\",\"label\":\"Layout ($pageLayout)\",\"type\":\"select\",\"defaultValue\":\"default\",\"required\":true,\"pattern\":\"\",\"hint\":\"Template layout\",\"options\":[\"default\",\"wide\",\"landing\",\"article\"]}"
    "],\"validation\":{\"requiredOnPage\":false,\"checkFormatting\":true,\"severity\":\"error\"}}"
    "]";

static JsonValue *build_presets_array(void) {
    char err[128];
    JsonDoc *doc = json_parse(PRESETS_JSON, strlen(PRESETS_JSON), err, sizeof(err));
    JsonValue *arr;
    if (!doc || json_type(doc->root) != JSON_ARRAY) {
        json_doc_free(doc);
        return json_array();
    }
    arr = json_clone(doc->root);
    json_doc_free(doc);
    return arr ? arr : json_array();
}

static int write_atomic_file(const char *final_path, const char *tmp_path, const char *text) {
    FILE *f;
    size_t n, wrote;
    if (!final_path || !tmp_path || !text) return -1;
    f = fopen(tmp_path, "wb");
    if (!f) return -1;
    n = strlen(text);
    wrote = n ? fwrite(text, 1, n, f) : 0;
    if (wrote != n || fflush(f) != 0) {
        fclose(f);
        remove(tmp_path);
        return -1;
    }
#ifndef _WIN32
    if (fsync(fileno(f)) != 0) {
        fclose(f);
        remove(tmp_path);
        return -1;
    }
#endif
    if (fclose(f) != 0) {
        remove(tmp_path);
        return -1;
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp_path, final_path, MOVEFILE_REPLACE_EXISTING)) {
#else
    if (rename(tmp_path, final_path) != 0) {
#endif
        remove(tmp_path);
        return -1;
    }
    return 0;
}

static JsonValue *load_all_projects_locked(const char *repo_root) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    char err[128];
    JsonDoc *doc;
    JsonValue *root;
    if (project_macros_resolve_path(repo_root, path, sizeof(path)) != 0 ||
        http_read_file(path, &data, &len, 512u * 1024u) != 0 || !data) {
        root = json_object();
        if (root) json_object_put(root, "projects", json_object());
        return root;
    }
    err[0] = '\0';
    doc = json_parse(data, len, err, sizeof(err));
    free(data);
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        root = json_object();
        if (root) json_object_put(root, "projects", json_object());
        return root;
    }
    root = json_clone(doc->root);
    json_doc_free(doc);
    if (!root) return NULL;
    if (!json_object_get(root, "projects") || json_type(json_object_get(root, "projects")) != JSON_OBJECT) {
        json_object_put(root, "projects", json_object());
    }
    return root;
}

static int save_all_projects_locked(const char *repo_root, JsonValue *store_root, char *err, size_t err_sz) {
    const char *base = effective_studio_home(repo_root);
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    JsonDoc *doc;
    char *text;
    char *formatted;
    size_t len = 0;
    if (!base || !base[0] || settings_ensure_dirs(base) != 0 ||
        project_macros_resolve_path(base, path, sizeof(path)) != 0 ||
        settings_install_path(base, "state/project-macros.json.tmp", tmp, sizeof(tmp)) != 0) {
        set_err(err, err_sz, "Could not resolve state/project-macros.json");
        return -1;
    }
    doc = json_doc_new(json_clone(store_root));
    if (!doc) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    text = json_stringify(doc, &len);
    json_doc_free(doc);
    if (!text) {
        set_err(err, err_sz, "Could not serialize state/project-macros.json");
        return -1;
    }
    formatted = (char *)malloc(len + 2);
    if (!formatted) {
        free(text);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    memcpy(formatted, text, len);
    formatted[len] = '\n';
    formatted[len + 1] = '\0';
    free(text);
    if (write_atomic_file(path, tmp, formatted) != 0) {
        free(formatted);
        set_err(err, err_sz, "Could not write state/project-macros.json");
        return -1;
    }
    free(formatted);
    return 0;
}

static JsonDoc *build_response_doc(const char *repo_root, const JsonValue *proj_entry) {
    int is_cc = detect_classiccars_site(repo_root);
    int show_builtin = 1;
    JsonValue *root = json_object();
    JsonValue *macros_arr = NULL;
    JsonValue *presets_arr = build_presets_array();
    if (!root || !presets_arr) {
        json_free(root);
        json_free(presets_arr);
        return NULL;
    }
    if (proj_entry && json_type(proj_entry) == JSON_OBJECT) {
        JsonValue *sb = json_object_get(proj_entry, "showBuiltinWidgets");
        JsonValue *m = json_object_get(proj_entry, "macros");
        if (sb && json_type(sb) == JSON_BOOL) {
            show_builtin = json_bool_value(sb) ? 1 : 0;
        }
        if (m && json_type(m) == JSON_ARRAY) {
            macros_arr = json_clone(m);
        }
    }
    if (!macros_arr) macros_arr = json_array();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "projectRoot", json_string_cstr(repo_root ? repo_root : ""));
    json_object_put(root, "isClassicCars", json_bool(is_cc));
    json_object_put(root, "showBuiltinWidgets", json_bool(show_builtin));
    json_object_put(root, "macros", macros_arr);
    json_object_put(root, "presets", presets_arr);
    return json_doc_new(root);
}

JsonDoc *project_macros_get_document(const char *repo_root, char *err, size_t err_sz) {
    JsonValue *store;
    JsonValue *projects;
    JsonValue *proj_entry = NULL;
    JsonDoc *doc;
    const char *key = (repo_root && repo_root[0]) ? repo_root : "default";
    macros_lock();
    store = load_all_projects_locked(repo_root);
    if (!store) {
        macros_unlock();
        set_err(err, err_sz, "Out of memory");
        return NULL;
    }
    projects = json_object_get(store, "projects");
    if (projects && json_type(projects) == JSON_OBJECT) {
        proj_entry = json_object_get(projects, key);
    }
    doc = build_response_doc(repo_root, proj_entry);
    json_free(store);
    macros_unlock();
    if (!doc) set_err(err, err_sz, "Out of memory");
    return doc;
}

static int apply_action_to_entry(JsonValue *entry, const JsonValue *body, char *err, size_t err_sz) {
    const char *action = jstr_or(body, "action", "");
    JsonValue *macros_list = json_object_get(entry, "macros");
    if (!macros_list || json_type(macros_list) != JSON_ARRAY) {
        macros_list = json_array();
        json_object_put(entry, "macros", macros_list);
        macros_list = json_object_get(entry, "macros");
    }
    if (json_object_has(body, "showBuiltinWidgets")) {
        JsonValue *sb = json_object_get(body, "showBuiltinWidgets");
        if (!sb || json_type(sb) != JSON_BOOL) {
            set_err(err, err_sz, "showBuiltinWidgets must be a boolean");
            return 1;
        }
        json_object_put(entry, "showBuiltinWidgets", json_bool(json_bool_value(sb) ? 1 : 0));
    }
    if (json_object_has(body, "macros")) {
        JsonValue *in_arr = json_object_get(body, "macros");
        JsonValue *clean_arr;
        size_t i, n;
        if (!in_arr || json_type(in_arr) != JSON_ARRAY) {
            set_err(err, err_sz, "macros must be an array");
            return 1;
        }
        n = json_array_len(in_arr);
        if (n > MAX_MACROS_PER_PROJECT) {
            set_err(err, err_sz, "Too many macros in project");
            return 1;
        }
        clean_arr = json_array();
        if (!clean_arr) {
            set_err(err, err_sz, "Out of memory");
            return -1;
        }
        for (i = 0; i < n; i++) {
            JsonValue *m = sanitize_macro(json_array_at(in_arr, i), err, err_sz);
            if (!m) {
                json_free(clean_arr);
                return 1;
            }
            json_array_add(clean_arr, m);
        }
        json_object_put(entry, "macros", clean_arr);
        return 0;
    }
    if (strcmp(action, "save") == 0 || strcmp(action, "upsert") == 0) {
        JsonValue *raw_macro = json_object_get(body, "macro");
        JsonValue *clean = sanitize_macro(raw_macro, err, err_sz);
        const char *target_id;
        JsonValue *rebuilt;
        size_t i, n;
        int replaced = 0;
        if (!clean) return 1;
        target_id = jstr_or(clean, "id", "");
        rebuilt = json_array();
        n = json_array_len(macros_list);
        for (i = 0; i < n; i++) {
            JsonValue *cur = json_array_at(macros_list, i);
            if (!replaced && strcmp(jstr_or(cur, "id", ""), target_id) == 0) {
                json_array_add(rebuilt, clean);
                replaced = 1;
            } else {
                json_array_add(rebuilt, json_clone(cur));
            }
        }
        if (!replaced) {
            if (n >= MAX_MACROS_PER_PROJECT) {
                json_free(clean);
                json_free(rebuilt);
                set_err(err, err_sz, "Too many macros in project");
                return 1;
            }
            json_array_add(rebuilt, clean);
        }
        json_object_put(entry, "macros", rebuilt);
        return 0;
    }
    if (strcmp(action, "delete") == 0 || strcmp(action, "remove") == 0) {
        const char *del_id = jstr_or(body, "id", "");
        JsonValue *rebuilt;
        size_t i, n;
        if (!del_id[0]) {
            set_err(err, err_sz, "Macro id is required for delete");
            return 1;
        }
        rebuilt = json_array();
        n = json_array_len(macros_list);
        for (i = 0; i < n; i++) {
            JsonValue *cur = json_array_at(macros_list, i);
            if (strcmp(jstr_or(cur, "id", ""), del_id) != 0) {
                json_array_add(rebuilt, json_clone(cur));
            }
        }
        json_object_put(entry, "macros", rebuilt);
        return 0;
    }
    if (strcmp(action, "reset") == 0) {
        json_object_put(entry, "macros", json_array());
        json_object_put(entry, "showBuiltinWidgets", json_bool(1));
        return 0;
    }
    if (json_object_has(body, "showBuiltinWidgets")) {
        return 0;
    }
    set_err(err, err_sz, "Provide macros array, showBuiltinWidgets, or a valid action (save, delete, reset)");
    return 1;
}

int project_macros_apply_update(const char *repo_root, const JsonValue *body,
                                JsonDoc **out_doc, char *err, size_t err_sz) {
    JsonValue *store;
    JsonValue *projects;
    JsonValue *entry;
    const char *key = (repo_root && repo_root[0]) ? repo_root : "default";
    int rc;
    if (out_doc) *out_doc = NULL;
    if (!body || json_type(body) != JSON_OBJECT) {
        set_err(err, err_sz, "JSON body must be an object");
        return 1;
    }
    macros_lock();
    store = load_all_projects_locked(repo_root);
    if (!store) {
        macros_unlock();
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    projects = json_object_get(store, "projects");
    entry = json_object_get(projects, key);
    if (!entry || json_type(entry) != JSON_OBJECT) {
        entry = json_object();
        json_object_put(entry, "showBuiltinWidgets", json_bool(1));
        json_object_put(entry, "macros", json_array());
        json_object_put(projects, key, entry);
        entry = json_object_get(projects, key);
    }
    rc = apply_action_to_entry(entry, body, err, err_sz);
    if (rc != 0) {
        json_free(store);
        macros_unlock();
        return rc;
    }
    if (save_all_projects_locked(repo_root, store, err, err_sz) != 0) {
        json_free(store);
        macros_unlock();
        return -1;
    }
    if (out_doc) {
        *out_doc = build_response_doc(repo_root, entry);
    }
    json_free(store);
    macros_unlock();
    return (out_doc && !*out_doc) ? -1 : 0;
}

