#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "config.h"

#include "http.h"
#include "settings.h"

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
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static StudioConfig g_configs[2];
static int g_config_i = 0;
static int g_config_inited = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_cfg_lock;
static INIT_ONCE g_cfg_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK cfg_lock_init(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_cfg_lock);
    return TRUE;
}

static void cfg_lock(void) {
    InitOnceExecuteOnce(&g_cfg_once, cfg_lock_init, NULL, NULL);
    EnterCriticalSection(&g_cfg_lock);
}

static void cfg_unlock(void) {
    LeaveCriticalSection(&g_cfg_lock);
}
#else
static pthread_mutex_t g_cfg_lock = PTHREAD_MUTEX_INITIALIZER;

static void cfg_lock(void) {
    pthread_mutex_lock(&g_cfg_lock);
}

static void cfg_unlock(void) {
    pthread_mutex_unlock(&g_cfg_lock);
}
#endif

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (err && err_sz > 0) {
        snprintf(err, err_sz, "%s", msg ? msg : "");
    }
}

static void copy_trimmed(char *dst, size_t dst_sz, const char *src) {
    size_t len;
    if (!dst || dst_sz == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    len = strlen(src);
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r' || src[len - 1] == '\n')) {
        len--;
    }
    if (len >= dst_sz) len = dst_sz - 1;
    if (len) memcpy(dst, src, len);
    dst[len] = '\0';
}

void config_init_defaults(StudioConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    copy_trimmed(cfg->traffic_exclude_include, sizeof(cfg->traffic_exclude_include),
                 "/includes/traffic_exclude.php");
}

static void ensure_inited_locked(void) {
    if (!g_config_inited) {
        config_init_defaults(&g_configs[0]);
        config_init_defaults(&g_configs[1]);
        g_config_inited = 1;
    }
}

static void store_config(const StudioConfig *next) {
    int slot;
    cfg_lock();
    ensure_inited_locked();
    slot = 1 - g_config_i;
    g_configs[slot] = *next;
    g_config_i = slot;
    cfg_unlock();
}

const StudioConfig *config_get(void) {
    int slot;
    cfg_lock();
    ensure_inited_locked();
    slot = g_config_i;
    cfg_unlock();
    return &g_configs[slot];
}

static const char *effective_base(const char *base_root) {
    const char *home = settings_get_studio_home();
    if (home && home[0]) return home;
    return base_root;
}

int config_resolve_paths(const char *base_root, char *file_out, size_t file_sz,
                         char *dir_out, size_t dir_sz) {
    const char *base = effective_base(base_root);
    char state_dir[PATH_MAX];
    char cfg_file[PATH_MAX];
    if (!base || !base[0]) return -1;
    if (settings_install_path(base, "state", state_dir, sizeof(state_dir)) != 0) return -1;
    if (settings_install_path(base, "state/config.json", cfg_file, sizeof(cfg_file)) != 0) return -1;
    if (file_out && file_sz > 0) {
        if (strlen(cfg_file) + 1 > file_sz) return -1;
        memcpy(file_out, cfg_file, strlen(cfg_file) + 1);
    }
    if (dir_out && dir_sz > 0) {
        if (strlen(state_dir) + 1 > dir_sz) return -1;
        memcpy(dir_out, state_dir, strlen(state_dir) + 1);
    }
    return 0;
}

static void read_json_str(const JsonValue *obj, const char *key, char *dst, size_t dst_sz) {
    JsonValue *val = json_object_get(obj, key);
    if (!val || json_type(val) != JSON_STRING) return;
    copy_trimmed(dst, dst_sz, json_string_get(val, NULL));
}

static void parse_config_object(const JsonValue *obj, StudioConfig *out) {
    JsonValue *hosts;
    size_t n;
    config_init_defaults(out);
    if (!obj || json_type(obj) != JSON_OBJECT) return;
    read_json_str(obj, "brandName", out->brand_name, sizeof(out->brand_name));
    read_json_str(obj, "siteOrigin", out->site_origin, sizeof(out->site_origin));
    while ((n = strlen(out->site_origin)) > 0 && out->site_origin[n - 1] == '/') {
        out->site_origin[n - 1] = '\0';
    }
    read_json_str(obj, "siteHost", out->site_host, sizeof(out->site_host));
    read_json_str(obj, "defaultAuthor", out->default_author, sizeof(out->default_author));
    read_json_str(obj, "defaultWebsiteRoot", out->default_website_root, sizeof(out->default_website_root));
    read_json_str(obj, "googleMeasurementId", out->google_measurement_id, sizeof(out->google_measurement_id));
    read_json_str(obj, "extremeLogin", out->extreme_login, sizeof(out->extreme_login));
    read_json_str(obj, "extremeTrackerHost", out->extreme_tracker_host, sizeof(out->extreme_tracker_host));
    read_json_str(obj, "trafficExcludeInclude", out->traffic_exclude_include, sizeof(out->traffic_exclude_include));
    hosts = json_object_get(obj, "siteHosts");
    if (hosts && json_type(hosts) == JSON_ARRAY) {
        size_t i;
        size_t len = json_array_len(hosts);
        for (i = 0; i < len && out->site_hosts_count < 8; i++) {
            JsonValue *item = json_array_at(hosts, i);
            char host[128];
            if (!item || json_type(item) != JSON_STRING) continue;
            copy_trimmed(host, sizeof(host), json_string_get(item, NULL));
            if (!host[0]) continue;
            memcpy(out->site_hosts[out->site_hosts_count], host, strlen(host) + 1);
            out->site_hosts_count++;
        }
    }
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

int config_load(const char *base_root) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    char err[128];
    JsonDoc *doc;
    StudioConfig next;
    if (config_resolve_paths(base_root, path, sizeof(path), NULL, 0) != 0) {
        config_init_defaults(&next);
        store_config(&next);
        return -1;
    }
    if (http_read_file(path, &data, &len, 256u * 1024u) != 0 || !data) {
        config_init_defaults(&next);
        store_config(&next);
        return 1;
    }
    err[0] = '\0';
    doc = json_parse(data, len, err, sizeof(err));
    free(data);
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        return -1;
    }
    parse_config_object(doc->root, &next);
    json_doc_free(doc);
    store_config(&next);
    return 0;
}

JsonValue *config_to_json(void) {
    const StudioConfig *cfg = config_get();
    JsonValue *root = json_object();
    JsonValue *hosts = json_array();
    size_t i;
    if (!root || !hosts) {
        json_free(root);
        json_free(hosts);
        return NULL;
    }
    for (i = 0; i < cfg->site_hosts_count; i++) {
        json_array_add(hosts, json_string_cstr(cfg->site_hosts[i]));
    }
    json_object_put(root, "brandName", json_string_cstr(cfg->brand_name));
    json_object_put(root, "siteOrigin", json_string_cstr(cfg->site_origin));
    json_object_put(root, "siteHost", json_string_cstr(cfg->site_host));
    json_object_put(root, "siteHosts", hosts);
    json_object_put(root, "defaultAuthor", json_string_cstr(cfg->default_author));
    json_object_put(root, "defaultWebsiteRoot", json_string_cstr(cfg->default_website_root));
    json_object_put(root, "googleMeasurementId", json_string_cstr(cfg->google_measurement_id));
    json_object_put(root, "extremeLogin", json_string_cstr(cfg->extreme_login));
    json_object_put(root, "extremeTrackerHost", json_string_cstr(cfg->extreme_tracker_host));
    json_object_put(root, "trafficExcludeInclude", json_string_cstr(cfg->traffic_exclude_include));
    return root;
}

int config_ensure_file(const char *base_root, char *file_out, size_t file_sz) {
    const char *base = effective_base(base_root);
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    struct stat st;
    JsonValue *obj;
    JsonDoc *doc;
    char *raw;
    char *with_nl;
    size_t len = 0;
    int rc;
    if (!base || !base[0]) return -1;
    if (settings_ensure_dirs(base) != 0) return -1;
    if (config_resolve_paths(base, path, sizeof(path), NULL, 0) != 0) return -1;
    if (file_out && file_sz > 0) {
        if (strlen(path) + 1 > file_sz) return -1;
        memcpy(file_out, path, strlen(path) + 1);
    }
    if (stat(path, &st) == 0) return 0;
    if (settings_install_path(base, "state/config.json.tmp", tmp, sizeof(tmp)) != 0) return -1;
    obj = config_to_json();
    doc = json_doc_new(obj);
    if (!doc) return -1;
    raw = json_stringify(doc, &len);
    json_doc_free(doc);
    if (!raw) return -1;
    with_nl = (char *)malloc(len + 2);
    if (!with_nl) {
        free(raw);
        return -1;
    }
    memcpy(with_nl, raw, len);
    with_nl[len] = '\n';
    with_nl[len + 1] = '\0';
    free(raw);
    rc = write_atomic_file(path, tmp, with_nl);
    free(with_nl);
    return rc;
}

int config_save_text(const char *base_root, const char *json_text, char *err, size_t err_sz) {
    const char *base = effective_base(base_root);
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    char parse_err[256];
    JsonDoc *doc;
    StudioConfig next;
    char *stored;
    size_t len;
    int rc;
    if (!base || !base[0]) {
        set_err(err, err_sz, "Studio root is not configured");
        return -1;
    }
    if (!json_text || !json_text[0]) {
        set_err(err, err_sz, "Config file content is required");
        return 1;
    }
    parse_err[0] = '\0';
    len = strlen(json_text);
    doc = json_parse(json_text, len, parse_err, sizeof(parse_err));
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        if (parse_err[0]) {
            snprintf(err, err_sz, "Config file must be a valid JSON object (%s)", parse_err);
        } else {
            set_err(err, err_sz, "Config file must be a JSON object");
        }
        return 1;
    }
    parse_config_object(doc->root, &next);
    json_doc_free(doc);
    if (settings_ensure_dirs(base) != 0 ||
        config_resolve_paths(base, path, sizeof(path), NULL, 0) != 0 ||
        settings_install_path(base, "state/config.json.tmp", tmp, sizeof(tmp)) != 0) {
        set_err(err, err_sz, "Could not resolve state/config.json");
        return -1;
    }
    stored = (char *)malloc(len + 2);
    if (!stored) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    memcpy(stored, json_text, len);
    if (stored[len - 1] != '\n') stored[len++] = '\n';
    stored[len] = '\0';
    rc = write_atomic_file(path, tmp, stored);
    free(stored);
    if (rc != 0) {
        set_err(err, err_sz, "Could not write state/config.json");
        return -1;
    }
    store_config(&next);
    return 0;
}

JsonDoc *config_page_document(const char *base_root, char *err, size_t err_sz) {
    char path[PATH_MAX];
    char *content = NULL;
    size_t len = 0;
    JsonValue *root;
    JsonValue *widgets;
    JsonValue *errors;
    JsonValue *cfg_val;
    if (config_ensure_file(base_root, path, sizeof(path)) != 0) {
        set_err(err, err_sz, "Could not prepare state/config.json");
        return NULL;
    }
    config_load(base_root);
    if (http_read_file(path, &content, &len, 256u * 1024u) != 0 || !content) {
        set_err(err, err_sz, "Could not read state/config.json");
        return NULL;
    }
    root = json_object();
    widgets = json_object();
    errors = json_array();
    cfg_val = config_to_json();
    if (!root || !widgets || !errors || !cfg_val) {
        free(content);
        json_free(root);
        json_free(widgets);
        json_free(errors);
        json_free(cfg_val);
        set_err(err, err_sz, "out of memory");
        return NULL;
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "path", json_string_cstr(CONFIG_PUBLIC_PATH));
    json_object_put(root, "fsPath", json_string_cstr(path));
    json_object_put(root, "previewUrl", json_string_cstr(""));
    json_object_put(root, "section", json_string_cstr(""));
    json_object_put(root, "slug", json_string_cstr(""));
    json_object_put(root, "fileType", json_string_cstr("json"));
    json_object_put(root, "isMedia", json_bool(0));
    json_object_put(root, "mediaKind", json_string_cstr(""));
    json_object_put(root, "fileSize", json_number_int((long long)len));
    json_object_put(root, "width", json_number_int(0));
    json_object_put(root, "height", json_number_int(0));
    json_object_put(root, "content", json_string_cstr(content));
    json_object_put(root, "hasGallery", json_bool(0));
    json_object_put(root, "isJalbum", json_bool(0));
    json_object_put(root, "jalbum", json_bool(0));
    json_object_put(root, "dirty", json_bool(0));
    json_object_put(root, "widgets", widgets);
    json_object_put(root, "errors", errors);
    json_object_put(root, "config", cfg_val);
    free(content);
    return json_doc_new(root);
}

int config_open_folder(const char *base_root, char *dir_out, size_t dir_sz,
                       char *err, size_t err_sz) {
    char file_path[PATH_MAX];
    char dir_path[PATH_MAX];
    if (config_ensure_file(base_root, file_path, sizeof(file_path)) != 0 ||
        config_resolve_paths(base_root, NULL, 0, dir_path, sizeof(dir_path)) != 0) {
        set_err(err, err_sz, "Could not prepare config folder");
        return -1;
    }
    if (dir_out && dir_sz > 0) {
        if (strlen(dir_path) + 1 > dir_sz) {
            set_err(err, err_sz, "Config folder path is too long");
            return -1;
        }
        memcpy(dir_out, dir_path, strlen(dir_path) + 1);
    }
#ifdef _WIN32
    {
        char win_dir[PATH_MAX];
        char cmd[PATH_MAX + 64];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        size_t i;
        if (strlen(dir_path) >= sizeof(win_dir) || strchr(dir_path, '"') != NULL) {
            set_err(err, err_sz, "Invalid config folder path");
            return -1;
        }
        for (i = 0; dir_path[i]; i++) {
            win_dir[i] = (dir_path[i] == '/') ? '\\' : dir_path[i];
        }
        win_dir[i] = '\0';
        snprintf(cmd, sizeof(cmd), "explorer.exe \"%s\"", win_dir);
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            set_err(err, err_sz, "Could not launch Windows Explorer");
            return -1;
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 0;
    }
#else
    {
#ifndef __APPLE__
        const char *display = getenv("DISPLAY");
        const char *wayland = getenv("WAYLAND_DISPLAY");
        if ((!display || !display[0]) && (!wayland || !wayland[0])) {
            set_err(err, err_sz, "No graphical display is available to open the file manager");
            return -1;
        }
#endif
        int fds[2];
        pid_t pid;
        unsigned char failed = 0;
        ssize_t n;
        if (pipe(fds) != 0) {
            set_err(err, err_sz, "Could not start file manager");
            return -1;
        }
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        pid = fork();
        if (pid < 0) {
            close(fds[0]);
            close(fds[1]);
            set_err(err, err_sz, "Could not fork process");
            return -1;
        }
        if (pid == 0) {
            pid_t grand;
            close(fds[0]);
            grand = fork();
            if (grand < 0) {
                unsigned char b = 1;
                ssize_t w = write(fds[1], &b, 1);
                (void)w;
                _exit(127);
            }
            if (grand > 0) {
                _exit(0);
            }
            setsid();
            {
                int devnull = open("/dev/null", O_RDWR);
                if (devnull >= 0) {
                    dup2(devnull, STDIN_FILENO);
                    dup2(devnull, STDOUT_FILENO);
                    dup2(devnull, STDERR_FILENO);
                    if (devnull > STDERR_FILENO) close(devnull);
                }
            }
#ifdef __APPLE__
            {
                char *argv[] = {"open", dir_path, NULL};
                execvp(argv[0], argv);
            }
#else
            {
                char *argv1[] = {"xdg-open", dir_path, NULL};
                char *argv2[] = {"gio", "open", dir_path, NULL};
                execvp(argv1[0], argv1);
                execvp(argv2[0], argv2);
            }
#endif
            {
                unsigned char b = 1;
                ssize_t w = write(fds[1], &b, 1);
                (void)w;
            }
            _exit(127);
        }
        close(fds[1]);
        while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
        }
        do {
            n = read(fds[0], &failed, 1);
        } while (n < 0 && errno == EINTR);
        close(fds[0]);
        if (n > 0 && failed) {
            set_err(err, err_sz, "Could not launch file manager (xdg-open not found)");
            return -1;
        }
        return 0;
    }
#endif
}
