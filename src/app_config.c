#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "app_config.h"

#include "http.h"
#include "settings.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#define APP_CONFIG_DEFAULT_INTERVAL_SEC 180
#define APP_CONFIG_MIN_INTERVAL_SEC     30
#define APP_CONFIG_MAX_INTERVAL_SEC     3600
#define APP_CONFIG_DEFAULT_UPDATE_REPO  "Jagard11/ClassicCars_Studio"

static StudioAppConfig g_app_configs[2];
static int g_app_config_i = 0;
static int g_app_config_inited = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_app_cfg_lock;
static INIT_ONCE g_app_cfg_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK app_cfg_lock_init(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_app_cfg_lock);
    return TRUE;
}

static void app_cfg_lock(void) {
    InitOnceExecuteOnce(&g_app_cfg_once, app_cfg_lock_init, NULL, NULL);
    EnterCriticalSection(&g_app_cfg_lock);
}

static void app_cfg_unlock(void) {
    LeaveCriticalSection(&g_app_cfg_lock);
}
#else
static pthread_mutex_t g_app_cfg_lock = PTHREAD_MUTEX_INITIALIZER;

static void app_cfg_lock(void) {
    pthread_mutex_lock(&g_app_cfg_lock);
}

static void app_cfg_unlock(void) {
    pthread_mutex_unlock(&g_app_cfg_lock);
}
#endif

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg ? msg : "");
    }
}

static int clamp_interval(long long v) {
    if (v < APP_CONFIG_MIN_INTERVAL_SEC) return APP_CONFIG_MIN_INTERVAL_SEC;
    if (v > APP_CONFIG_MAX_INTERVAL_SEC) return APP_CONFIG_MAX_INTERVAL_SEC;
    return (int)v;
}

static int valid_repo_slug(const char *s) {
    const char *slash;
    const char *p;
    if (!s || !s[0] || strlen(s) >= 120) return 0;
    slash = strchr(s, '/');
    if (!slash || slash == s || !slash[1] || strchr(slash + 1, '/') != NULL) return 0;
    for (p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (p == slash) continue;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.') {
            continue;
        }
        return 0;
    }
    return 1;
}

static void normalize_version_str(const char *src, char *dst, size_t dst_sz) {
    size_t i = 0;
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    if ((*src == 'v' || *src == 'V') && src[1] >= '0' && src[1] <= '9') src++;
    while (src[i] && src[i] != ' ' && src[i] != '\t' && src[i] != '\r' && src[i] != '\n' && i + 1 < dst_sz) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void detect_git_update_repo(const char *studio_home, char *out, size_t out_sz) {
    char git_cfg[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    const char *p;
    const char *env_repo = getenv("STUDIO_UPDATE_REPO");
    if (!out || out_sz == 0) return;
    if (env_repo && valid_repo_slug(env_repo)) {
        snprintf(out, out_sz, "%s", env_repo);
        return;
    }
    if (!studio_home || !studio_home[0]) return;
    snprintf(git_cfg, sizeof(git_cfg), "%s/.git/config", studio_home);
    if (http_read_file(git_cfg, &data, &len, 64u * 1024u) != 0 || !data) return;
    p = strstr(data, "github.com");
    if (p) {
        p += 10;
        if (*p == '/' || *p == ':') {
            char slug[128];
            size_t i = 0;
            p++;
            while (p[i] && p[i] != '\r' && p[i] != '\n' && p[i] != ' ' && p[i] != '\t' && p[i] != '"' && i + 1 < sizeof(slug)) {
                slug[i] = p[i];
                i++;
            }
            slug[i] = '\0';
            if (i > 4 && strcmp(slug + i - 4, ".git") == 0) slug[i - 4] = '\0';
            while (i > 0 && slug[i - 1] == '/') slug[--i] = '\0';
            if (valid_repo_slug(slug)) {
                snprintf(out, out_sz, "%s", slug);
            }
        }
    }
    free(data);
}

void app_config_init_defaults(StudioAppConfig *cfg) {
    const char *env_repo;
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->git_auto_fetch = 0;
    cfg->git_auto_fetch_prompted = 0;
    cfg->git_auto_fetch_interval_sec = APP_CONFIG_DEFAULT_INTERVAL_SEC;
    cfg->auto_check_updates = 1;
    snprintf(cfg->update_repo, sizeof(cfg->update_repo), "%s", APP_CONFIG_DEFAULT_UPDATE_REPO);
    env_repo = getenv("STUDIO_UPDATE_REPO");
    if (env_repo && valid_repo_slug(env_repo)) {
        snprintf(cfg->update_repo, sizeof(cfg->update_repo), "%s", env_repo);
    }
    cfg->dismissed_release_version[0] = '\0';
}

static void ensure_inited_locked(void) {
    if (!g_app_config_inited) {
        app_config_init_defaults(&g_app_configs[0]);
        app_config_init_defaults(&g_app_configs[1]);
        g_app_config_inited = 1;
    }
}

static void store_app_config(const StudioAppConfig *next) {
    int slot;
    app_cfg_lock();
    ensure_inited_locked();
    slot = 1 - g_app_config_i;
    g_app_configs[slot] = *next;
    g_app_config_i = slot;
    app_cfg_unlock();
}

const StudioAppConfig *app_config_get(void) {
    int slot;
    app_cfg_lock();
    ensure_inited_locked();
    slot = g_app_config_i;
    app_cfg_unlock();
    return &g_app_configs[slot];
}

static const char *effective_studio_home(const char *fallback_root) {
    const char *home = settings_get_studio_home();
    if (home && home[0]) return home;
    return fallback_root;
}

int app_config_resolve_path(const char *fallback_root, char *file_out, size_t file_sz) {
    const char *base = effective_studio_home(fallback_root);
    char cfg_file[PATH_MAX];
    if (!base || !base[0] || !file_out || file_sz == 0) return -1;
    if (settings_install_path(base, "state/app-config.json", cfg_file, sizeof(cfg_file)) != 0) return -1;
    if (strlen(cfg_file) + 1 > file_sz) return -1;
    memcpy(file_out, cfg_file, strlen(cfg_file) + 1);
    return 0;
}

static int write_atomic_file(const char *final_path, const char *tmp_path, const char *text) {
    FILE *f;
    size_t n;
    size_t wrote;
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

static void parse_app_config_object(const JsonValue *obj, const char *base, StudioAppConfig *out) {
    JsonValue *v;
    long long num = 0;
    app_config_init_defaults(out);
    detect_git_update_repo(base, out->update_repo, sizeof(out->update_repo));
    if (!obj || json_type(obj) != JSON_OBJECT) return;
    v = json_object_get(obj, "gitAutoFetch");
    if (v && json_type(v) == JSON_BOOL) {
        out->git_auto_fetch = json_bool_value(v) ? 1 : 0;
    }
    v = json_object_get(obj, "gitAutoFetchPrompted");
    if (v && json_type(v) == JSON_BOOL) {
        out->git_auto_fetch_prompted = json_bool_value(v) ? 1 : 0;
    }
    v = json_object_get(obj, "gitAutoFetchIntervalSec");
    if (v && json_type(v) == JSON_NUMBER && json_as_int(v, &num) == 0) {
        out->git_auto_fetch_interval_sec = clamp_interval(num);
    }
    v = json_object_get(obj, "autoCheckUpdates");
    if (v && json_type(v) == JSON_BOOL) {
        out->auto_check_updates = json_bool_value(v) ? 1 : 0;
    }
    v = json_object_get(obj, "updateRepo");
    if (v && json_type(v) == JSON_STRING) {
        const char *repo = json_string_get(v, NULL);
        if (repo && valid_repo_slug(repo)) {
            snprintf(out->update_repo, sizeof(out->update_repo), "%s", repo);
        }
    }
    v = json_object_get(obj, "dismissedReleaseVersion");
    if (v && json_type(v) == JSON_STRING) {
        const char *ver = json_string_get(v, NULL);
        normalize_version_str(ver ? ver : "", out->dismissed_release_version, sizeof(out->dismissed_release_version));
    }
}

int app_config_load(const char *fallback_root) {
    const char *base = effective_studio_home(fallback_root);
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    char err[128];
    JsonDoc *doc;
    StudioAppConfig next;
    if (app_config_resolve_path(fallback_root, path, sizeof(path)) != 0) {
        app_config_init_defaults(&next);
        detect_git_update_repo(base, next.update_repo, sizeof(next.update_repo));
        store_app_config(&next);
        return -1;
    }
    if (http_read_file(path, &data, &len, 64u * 1024u) != 0 || !data) {
        app_config_init_defaults(&next);
        detect_git_update_repo(base, next.update_repo, sizeof(next.update_repo));
        store_app_config(&next);
        return 1;
    }
    err[0] = '\0';
    doc = json_parse(data, len, err, sizeof(err));
    free(data);
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        return -1;
    }
    parse_app_config_object(doc->root, base, &next);
    json_doc_free(doc);
    store_app_config(&next);
    return 0;
}

static JsonValue *app_config_struct_to_json(const StudioAppConfig *cfg) {
    JsonValue *root = json_object();
    if (!root || !cfg) {
        json_free(root);
        return NULL;
    }
    json_object_put(root, "gitAutoFetch", json_bool(cfg->git_auto_fetch ? 1 : 0));
    json_object_put(root, "gitAutoFetchPrompted", json_bool(cfg->git_auto_fetch_prompted ? 1 : 0));
    json_object_put(root, "gitAutoFetchIntervalSec", json_number_int(cfg->git_auto_fetch_interval_sec));
    json_object_put(root, "autoCheckUpdates", json_bool(cfg->auto_check_updates ? 1 : 0));
    json_object_put(root, "updateRepo", json_string_cstr(cfg->update_repo));
    json_object_put(root, "dismissedReleaseVersion", json_string_cstr(cfg->dismissed_release_version));
    return root;
}

JsonValue *app_config_to_json(void) {
    return app_config_struct_to_json(app_config_get());
}

int app_config_apply_update(const char *fallback_root, const JsonValue *body,
                            char *err, size_t err_sz) {
    const char *base = effective_studio_home(fallback_root);
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    StudioAppConfig next;
    JsonDoc *doc;
    char *json_text;
    char *formatted;
    size_t len = 0;

    if (!base || !base[0]) {
        set_err(err, err_sz, "Studio home is not configured");
        return -1;
    }
    if (!body || json_type(body) != JSON_OBJECT) {
        set_err(err, err_sz, "JSON body must be an object");
        return 1;
    }

    app_config_load(base);
    next = *app_config_get();

    if (json_object_has(body, "gitAutoFetch")) {
        JsonValue *v = json_object_get(body, "gitAutoFetch");
        if (!v || json_type(v) != JSON_BOOL) {
            set_err(err, err_sz, "gitAutoFetch must be a boolean");
            return 1;
        }
        next.git_auto_fetch = json_bool_value(v) ? 1 : 0;
    }
    if (json_object_has(body, "gitAutoFetchPrompted")) {
        JsonValue *v = json_object_get(body, "gitAutoFetchPrompted");
        if (!v || json_type(v) != JSON_BOOL) {
            set_err(err, err_sz, "gitAutoFetchPrompted must be a boolean");
            return 1;
        }
        next.git_auto_fetch_prompted = json_bool_value(v) ? 1 : 0;
    }
    if (json_object_has(body, "gitAutoFetchIntervalSec")) {
        JsonValue *v = json_object_get(body, "gitAutoFetchIntervalSec");
        long long num = 0;
        if (!v || json_type(v) != JSON_NUMBER || json_as_int(v, &num) != 0) {
            set_err(err, err_sz, "gitAutoFetchIntervalSec must be an integer");
            return 1;
        }
        next.git_auto_fetch_interval_sec = clamp_interval(num);
    }
    if (json_object_has(body, "autoCheckUpdates")) {
        JsonValue *v = json_object_get(body, "autoCheckUpdates");
        if (!v || json_type(v) != JSON_BOOL) {
            set_err(err, err_sz, "autoCheckUpdates must be a boolean");
            return 1;
        }
        next.auto_check_updates = json_bool_value(v) ? 1 : 0;
    }
    if (json_object_has(body, "updateRepo")) {
        JsonValue *v = json_object_get(body, "updateRepo");
        const char *repo;
        if (!v || json_type(v) != JSON_STRING) {
            set_err(err, err_sz, "updateRepo must be a string");
            return 1;
        }
        repo = json_string_get(v, NULL);
        if (!repo || !valid_repo_slug(repo)) {
            set_err(err, err_sz, "updateRepo must be in owner/repo format");
            return 1;
        }
        snprintf(next.update_repo, sizeof(next.update_repo), "%s", repo);
    }
    if (json_object_has(body, "dismissedReleaseVersion")) {
        JsonValue *v = json_object_get(body, "dismissedReleaseVersion");
        if (!v || (json_type(v) != JSON_STRING && json_type(v) != JSON_NULL)) {
            set_err(err, err_sz, "dismissedReleaseVersion must be a string");
            return 1;
        }
        if (json_type(v) == JSON_NULL) {
            next.dismissed_release_version[0] = '\0';
        } else {
            const char *ver = json_string_get(v, NULL);
            normalize_version_str(ver ? ver : "", next.dismissed_release_version, sizeof(next.dismissed_release_version));
        }
    }

    if (settings_ensure_dirs(base) != 0 ||
        app_config_resolve_path(base, path, sizeof(path)) != 0 ||
        settings_install_path(base, "state/app-config.json.tmp", tmp, sizeof(tmp)) != 0) {
        set_err(err, err_sz, "Could not resolve state/app-config.json");
        return -1;
    }

    doc = json_doc_new(app_config_struct_to_json(&next));
    if (!doc) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    json_text = json_stringify(doc, &len);
    json_doc_free(doc);
    if (!json_text) {
        set_err(err, err_sz, "Could not format state/app-config.json");
        return -1;
    }
    formatted = (char *)malloc(len + 2);
    if (!formatted) {
        free(json_text);
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    memcpy(formatted, json_text, len);
    formatted[len] = '\n';
    formatted[len + 1] = '\0';
    free(json_text);

    if (write_atomic_file(path, tmp, formatted) != 0) {
        free(formatted);
        set_err(err, err_sz, "Could not write state/app-config.json");
        return -1;
    }
    free(formatted);

    store_app_config(&next);
    return 0;
}
