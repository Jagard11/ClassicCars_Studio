#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "settings.h"

#include "http.h"
#include "json.h"

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
#include <direct.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

static int g_php_port = 8771;

static int path_join(char *dst, size_t dst_sz, const char *a, const char *b) {
    size_t na;
    size_t nb;
    if (!dst || dst_sz == 0 || !a || !b) return -1;
    na = strlen(a);
    while (na > 0 && (a[na - 1] == '/' || a[na - 1] == '\\')) na--;
    while (*b == '/' || *b == '\\') b++;
    nb = strlen(b);
    if (na + 1 + nb + 1 > dst_sz) return -1;
    memcpy(dst, a, na);
    dst[na] = '/';
    memcpy(dst + na + 1, b, nb);
    dst[na + 1 + nb] = '\0';
    return 0;
}

static int mkdir_one(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
#ifdef _WIN32
    if (_mkdir(path) == 0) return 0;
#else
    if (mkdir(path, 0700) == 0) return 0;
#endif
    if (errno == EEXIST) {
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return 0;
    }
    return -1;
}

static int copy_fit(char *dst, size_t dst_sz, const char *src) {
    size_t n;
    if (!dst || dst_sz == 0 || !src) return -1;
    n = strlen(src);
    if (n + 1 > dst_sz) return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

static int path_stat_kind(const char *path, int want_dir) {
    struct stat st;
    if (!path || !path[0] || stat(path, &st) != 0) return 0;
    if (want_dir) return S_ISDIR(st.st_mode);
    return S_ISREG(st.st_mode);
}

static int install_has(const char *base, const char *rel, int want_dir) {
    char path[PATH_MAX];
    if (path_join(path, sizeof path, base, rel) != 0) return 0;
    return path_stat_kind(path, want_dir);
}

/* A standalone checkout keeps web/, preview/, state/, and VERSION at its root. */
static int install_standalone(const char *base) {
    return install_has(base, "web/index.html", 0) || install_has(base, "VERSION", 0) ||
           install_has(base, "preview/prepend.php", 0);
}

/* Older checkouts keep those files under <root>/studio/. */
static int install_nested(const char *base) {
    return install_has(base, "studio/web/index.html", 0) || install_has(base, "studio/VERSION", 0) ||
           install_has(base, "studio/preview/prepend.php", 0) || install_has(base, "studio/web", 1);
}

int settings_install_path(const char *base, const char *rel, char *out, size_t out_sz) {
    char direct[PATH_MAX];
    char nested_rel[PATH_MAX];
    char nested[PATH_MAX];
    int n;
    int use_nested = 0;
    if (!base || !base[0] || !rel || !rel[0] || !out || out_sz == 0) return -1;
    if (path_join(direct, sizeof direct, base, rel) != 0) return -1;
    n = snprintf(nested_rel, sizeof nested_rel, "studio/%s", rel);
    if (n < 0 || (size_t)n >= sizeof nested_rel) return -1;
    if (path_join(nested, sizeof nested, base, nested_rel) != 0) return -1;
    if (install_standalone(base)) use_nested = 0;
    else if (install_nested(base)) use_nested = 1;
    return copy_fit(out, out_sz, use_nested ? nested : direct);
}

int settings_ensure_dirs(const char *repo_root) {
    char state[PATH_MAX];
    char traffic[PATH_MAX];
    if (!repo_root || !repo_root[0]) return -1;
    if (settings_install_path(repo_root, "state", state, sizeof state) != 0) return -1;
    if (mkdir_one(state) != 0) return -1;
    if (path_join(traffic, sizeof traffic, state, "tmp-traffic") != 0) return -1;
    if (mkdir_one(traffic) != 0) return -1;
    return 0;
}

static int theme_paths(const char *repo_root, char *final_path, size_t final_sz,
                       char *tmp_path, size_t tmp_sz) {
    if (settings_install_path(repo_root, "state/theme.json", final_path, final_sz) != 0) {
        return -1;
    }
    if (tmp_path) {
        if (settings_install_path(repo_root, "state/theme.json.tmp", tmp_path, tmp_sz) != 0) {
            return -1;
        }
    }
    return 0;
}

char *settings_read_theme(const char *repo_root) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    if (!repo_root) return NULL;
    if (theme_paths(repo_root, path, sizeof path, NULL, 0) != 0) return NULL;
    if (http_read_file(path, &data, &len, 1024u * 1024u) != 0) return NULL;
    (void)len;
    return data;
}

static int write_atomic(const char *final_path, const char *tmp_path, const char *text) {
    FILE *f;
    size_t n;
    size_t wrote;
    if (!final_path || !tmp_path || !text) return -1;
    f = fopen(tmp_path, "wb");
    if (!f) return -1;
    n = strlen(text);
    wrote = n ? fwrite(text, 1, n, f) : 0;
    if (wrote != n) {
        fclose(f);
        remove(tmp_path);
        return -1;
    }
    if (fflush(f) != 0) {
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

int settings_write_theme(const char *repo_root, const char *json_str) {
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    if (!repo_root || !json_str) return -1;
    if (settings_ensure_dirs(repo_root) != 0) return -1;
    if (theme_paths(repo_root, path, sizeof path, tmp, sizeof tmp) != 0) return -1;
    return write_atomic(path, tmp, json_str);
}

static void window_state_defaults(StudioWindowState *st) {
    st->has_pos = 0;
    st->x = 0;
    st->y = 0;
    st->width = 1440;
    st->height = 900;
    st->maximized = 0;
}

static int clamp_span(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static int json_value_boundary(char c) {
    return c == '\0' || c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Points at the value of the first "key" in a flat JSON object, or NULL. */
static const char *json_find_value(const char *json, const char *key) {
    size_t klen;
    const char *p;
    if (!json || !key || !key[0]) return NULL;
    klen = strlen(key);
    p = json;
    while ((p = strchr(p, '"')) != NULL) {
        if (strncmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
            const char *v = p + 2 + klen;
            while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
            if (*v == ':') {
                v++;
                while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
                return v;
            }
        }
        p++;
    }
    return NULL;
}

static int json_read_int(const char *json, const char *key, int *out) {
    const char *v;
    char *end = NULL;
    long n;
    if (!out) return -1;
    v = json_find_value(json, key);
    if (!v) return -1;
    if (strncmp(v, "true", 4) == 0 && json_value_boundary(v[4])) {
        *out = 1;
        return 0;
    }
    if (strncmp(v, "false", 5) == 0 && json_value_boundary(v[5])) {
        *out = 0;
        return 0;
    }
    errno = 0;
    n = strtol(v, &end, 10);
    if (!end || end == v || errno == ERANGE || !json_value_boundary(*end)) return -1;
    if (n < (long)INT_MIN || n > (long)INT_MAX) return -1;
    *out = (int)n;
    return 0;
}

int settings_read_window_state(const char *repo_root, StudioWindowState *out) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    int value = 0;
    if (!out) return -1;
    window_state_defaults(out);
    if (!repo_root || !repo_root[0]) return -1;
    if (settings_install_path(repo_root, "state/window.json", path, sizeof path) != 0) return 0;
    if (http_read_file(path, &data, &len, 64u * 1024u) != 0 || !data) return 0;
    (void)len;
    if (json_read_int(data, "x", &value) == 0) out->x = value;
    if (json_read_int(data, "y", &value) == 0) out->y = value;
    if (json_read_int(data, "width", &value) == 0) out->width = clamp_span(value, 960, 7680);
    if (json_read_int(data, "height", &value) == 0) out->height = clamp_span(value, 600, 4320);
    if (json_read_int(data, "maximized", &value) == 0) out->maximized = value ? 1 : 0;
    if (json_read_int(data, "hasPos", &value) == 0 || json_read_int(data, "has_pos", &value) == 0) {
        out->has_pos = value ? 1 : 0;
    }
    free(data);
    return 0;
}

int settings_write_window_state(const char *repo_root, const StudioWindowState *st) {
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    char body[192];
    StudioWindowState norm;
    int n;
    if (!repo_root || !repo_root[0] || !st) return -1;
    if (settings_ensure_dirs(repo_root) != 0) return -1;
    norm = *st;
    norm.width = clamp_span(norm.width, 960, 7680);
    norm.height = clamp_span(norm.height, 600, 4320);
    norm.maximized = norm.maximized ? 1 : 0;
    norm.has_pos = norm.has_pos ? 1 : 0;
    if (settings_install_path(repo_root, "state/window.json", path, sizeof path) != 0) return -1;
    if (settings_install_path(repo_root, "state/window.json.tmp", tmp, sizeof tmp) != 0) return -1;
    n = snprintf(body, sizeof body,
                 "{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"maximized\":%d,\"hasPos\":%d}\n",
                 norm.x, norm.y, norm.width, norm.height, norm.maximized, norm.has_pos);
    if (n < 0 || (size_t)n >= sizeof body) return -1;
    return write_atomic(path, tmp, body);
}

int settings_read_last_page(const char *repo_root, char *out, size_t out_sz) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    JsonDoc *doc = NULL;
    char err[128];
    const char *text = "";
    size_t text_len = 0;
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';
    if (!repo_root || !repo_root[0]) return -1;
    if (settings_install_path(repo_root, "state/session.json", path, sizeof path) != 0) return 0;
    if (http_read_file(path, &data, &len, 64u * 1024u) != 0 || !data) return 0;
    err[0] = '\0';
    doc = json_parse(data, len, err, sizeof err);
    free(data);
    if (doc && json_type(doc->root) == JSON_OBJECT) {
        JsonValue *value = json_object_get(doc->root, "lastPage");
        if (value && json_type(value) == JSON_STRING) {
            text = json_string_get(value, &text_len);
            if (!text) {
                text = "";
                text_len = 0;
            }
        }
    }
    if (text_len + 1 <= out_sz) {
        if (text_len) memcpy(out, text, text_len);
        out[text_len] = '\0';
    }
    json_doc_free(doc);
    return 0;
}

int settings_write_last_page(const char *repo_root, const char *last_page) {
    char *raw = NULL;
    char err[128];
    JsonDoc *existing = NULL;
    JsonValue *root = NULL;
    JsonValue *page = NULL;
    JsonDoc *doc = NULL;
    char *text = NULL;
    int rc;
    if (!repo_root || !repo_root[0] || !last_page) return -1;
    if (settings_ensure_dirs(repo_root) != 0) return -1;
    raw = settings_read_session(repo_root);
    if (raw) {
        err[0] = '\0';
        existing = json_parse(raw, strlen(raw), err, sizeof err);
        free(raw);
        if (existing && json_type(existing->root) == JSON_OBJECT) root = json_clone(existing->root);
        json_doc_free(existing);
    }
    if (!root) root = json_object();
    page = json_string_cstr(last_page);
    if (!root || !page) {
        json_free(root);
        json_free(page);
        return -1;
    }
    json_object_remove(root, "openTabs");
    json_object_remove(root, "ok");
    if (json_object_put(root, "lastPage", page) != 0) {
        json_free(root);
        return -1;
    }
    doc = json_doc_new(root);
    if (!doc) return -1;
    text = json_stringify(doc, NULL);
    json_doc_free(doc);
    if (!text) return -1;
    rc = settings_write_session(repo_root, text);
    free(text);
    return rc;
}

char *settings_read_session(const char *repo_root) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    if (!repo_root || !repo_root[0]) return NULL;
    if (settings_install_path(repo_root, "state/session.json", path, sizeof path) != 0) return NULL;
    if (http_read_file(path, &data, &len, 256u * 1024u) != 0) return NULL;
    (void)len;
    return data;
}

/* Stores the session object as given. widgetOrder stays beside lastPage,
   the panel flags, rightPanelWidth, and widgetCollapsed. */
int settings_write_session(const char *repo_root, const char *json_str) {
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    char *stored;
    size_t len;
    int rc;
    if (!repo_root || !repo_root[0] || !json_str || !json_str[0]) return -1;
    if (settings_ensure_dirs(repo_root) != 0) return -1;
    if (settings_install_path(repo_root, "state/session.json", path, sizeof path) != 0) return -1;
    if (settings_install_path(repo_root, "state/session.json.tmp", tmp, sizeof tmp) != 0) return -1;
    len = strlen(json_str);
    stored = (char *)malloc(len + 2);
    if (!stored) return -1;
    memcpy(stored, json_str, len);
    if (stored[len - 1] != '\n') stored[len++] = '\n';
    stored[len] = '\0';
    rc = write_atomic(path, tmp, stored);
    free(stored);
    return rc;
}

int settings_get_php_port(void) {
    return g_php_port;
}

void settings_set_php_port(int port) {
    g_php_port = port;
}

static char g_project_roots[2][PATH_MAX];
static char g_studio_homes[2][PATH_MAX];
static int g_project_i = 0;
static int g_studio_i = 0;
static int g_cli_root = 0;

#ifdef _WIN32
static CRITICAL_SECTION g_root_lock;
static INIT_ONCE g_root_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK root_lock_init(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once;
    (void)param;
    (void)context;
    InitializeCriticalSection(&g_root_lock);
    return TRUE;
}

static void root_lock(void) {
    InitOnceExecuteOnce(&g_root_once, root_lock_init, NULL, NULL);
    EnterCriticalSection(&g_root_lock);
}

static void root_unlock(void) {
    LeaveCriticalSection(&g_root_lock);
}
#else
static pthread_mutex_t g_root_lock = PTHREAD_MUTEX_INITIALIZER;

static void root_lock(void) {
    pthread_mutex_lock(&g_root_lock);
}

static void root_unlock(void) {
    pthread_mutex_unlock(&g_root_lock);
}
#endif

static void memory_set(char slots[2][PATH_MAX], int *index, const char *text) {
    int next = 1 - *index;
    size_t n = 0;
    if (!text) text = "";
    n = strlen(text);
    if (n >= PATH_MAX) n = PATH_MAX - 1;
    memcpy(slots[next], text, n);
    slots[next][n] = '\0';
    *index = next;
}

static const char *memory_get(char slots[2][PATH_MAX], int *index) {
    int current;
    root_lock();
    current = *index;
    root_unlock();
    return slots[current];
}

int settings_canonicalize_dir(const char *path, char *out, size_t out_sz) {
#ifdef _WIN32
    char resolved[4096];
    char *slash;
    DWORD n;
#else
    char resolved[PATH_MAX];
#endif
    struct stat st;
    size_t len;
    if (!path || !path[0] || !out || out_sz == 0) return -1;
#ifdef _WIN32
    n = GetFullPathNameA(path, (DWORD)sizeof resolved, resolved, NULL);
    if (n == 0 || n >= sizeof resolved) return -1;
    for (slash = resolved; *slash; slash++) {
        if (*slash == '\\') *slash = '/';
    }
#else
    if (!realpath(path, resolved)) return -1;
#endif
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    len = strlen(resolved);
    if (len + 1 > out_sz) return -1;
    memcpy(out, resolved, len + 1);
    return 0;
}

int settings_set_studio_home(const char *home) {
    char canon[PATH_MAX];
    if (!home || !home[0]) {
        root_lock();
        memory_set(g_studio_homes, &g_studio_i, "");
        root_unlock();
        return 0;
    }
    if (settings_canonicalize_dir(home, canon, sizeof canon) != 0) return -1;
    root_lock();
    memory_set(g_studio_homes, &g_studio_i, canon);
    root_unlock();
    return 0;
}

const char *settings_get_studio_home(void) {
    return memory_get(g_studio_homes, &g_studio_i);
}

const char *settings_get_project_root(void) {
    return memory_get(g_project_roots, &g_project_i);
}

static int copy_studio_home(char *dst, size_t dst_sz) {
    const char *home = settings_get_studio_home();
    size_t n;
    if (!dst || dst_sz == 0) return -1;
    dst[0] = '\0';
    if (!home || !home[0]) return -1;
    n = strlen(home);
    if (n + 1 > dst_sz) return -1;
    memcpy(dst, home, n + 1);
    return 0;
}

static int write_project_file(const char *project_path) {
    char home[PATH_MAX];
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    JsonValue *root;
    JsonValue *value;
    JsonDoc *doc;
    char *text;
    char *stored;
    size_t len = 0;
    int rc;
    if (copy_studio_home(home, sizeof home) != 0) return 0;
    if (settings_ensure_dirs(home) != 0) return -1;
    if (settings_install_path(home, "state/project.json", path, sizeof path) != 0) return -1;
    if (settings_install_path(home, "state/project.json.tmp", tmp, sizeof tmp) != 0) return -1;
    root = json_object();
    value = json_string_cstr(project_path ? project_path : "");
    if (!root || !value) {
        json_free(root);
        json_free(value);
        return -1;
    }
    if (json_object_put(root, "path", value) != 0) {
        json_free(root);
        return -1;
    }
    doc = json_doc_new(root);
    if (!doc) return -1;
    text = json_stringify(doc, &len);
    json_doc_free(doc);
    if (!text) return -1;
    stored = (char *)malloc(len + 2);
    if (!stored) {
        free(text);
        return -1;
    }
    memcpy(stored, text, len);
    stored[len] = '\n';
    stored[len + 1] = '\0';
    free(text);
    rc = write_atomic(path, tmp, stored);
    free(stored);
    return rc;
}

static int take_dir_text(const char *text, char *out, size_t out_sz) {
    char buf[PATH_MAX];
    size_t n;
    if (!text || !out || out_sz == 0) return -1;
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        text += 3;
    }
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ' || text[n - 1] == '\t')) n--;
    if (n == 0 || n >= sizeof buf) return -1;
    memcpy(buf, text, n);
    buf[n] = '\0';
    return settings_canonicalize_dir(buf, out, out_sz);
}

static int read_project_json(const char *path, char *out, size_t out_sz) {
    char *data = NULL;
    size_t len = 0;
    JsonDoc *doc = NULL;
    char err[128];
    const char *text = NULL;
    int rc = -1;
    if (http_read_file(path, &data, &len, 64u * 1024u) != 0 || !data) return -1;
    err[0] = '\0';
    doc = json_parse(data, len, err, sizeof err);
    free(data);
    if (doc && json_type(doc->root) == JSON_OBJECT) {
        JsonValue *value = json_object_get(doc->root, "path");
        if (!value) value = json_object_get(doc->root, "projectRoot");
        if (value && json_type(value) == JSON_STRING) text = json_string_get(value, NULL);
    }
    if (text && take_dir_text(text, out, out_sz) == 0) rc = 0;
    json_doc_free(doc);
    return rc;
}

static int read_project_text(const char *path, char *out, size_t out_sz) {
    char *data = NULL;
    size_t len = 0;
    int rc;
    if (http_read_file(path, &data, &len, 64u * 1024u) != 0 || !data) return -1;
    rc = take_dir_text(data, out, out_sz);
    free(data);
    return rc == 0 ? 0 : -1;
}

int settings_read_saved_project_root(const char *studio_home, char *out, size_t out_sz) {
    static const char *json_rels[] = {"state/project.json", "studio/state/project.json"};
    static const char *text_rels[] = {"state/project_root.txt", "studio/state/project_root.txt"};
    char path[PATH_MAX];
    size_t i;
    if (!studio_home || !studio_home[0] || !out || out_sz == 0) return -1;
    for (i = 0; i < sizeof json_rels / sizeof json_rels[0]; i++) {
        if (path_join(path, sizeof path, studio_home, json_rels[i]) != 0) continue;
        if (read_project_json(path, out, out_sz) == 0) return 0;
    }
    for (i = 0; i < sizeof text_rels / sizeof text_rels[0]; i++) {
        if (path_join(path, sizeof path, studio_home, text_rels[i]) != 0) continue;
        if (read_project_text(path, out, out_sz) == 0) return 0;
    }
    return -1;
}

static int read_saved_project(char *out, size_t out_sz) {
    char home[PATH_MAX];
    if (copy_studio_home(home, sizeof home) != 0) return -1;
    return settings_read_saved_project_root(home, out, out_sz);
}

static void remember_project(const char *path) {
    root_lock();
    memory_set(g_project_roots, &g_project_i, path ? path : "");
    root_unlock();
}

static int store_project(const char *path, int persist) {
    char canon[PATH_MAX];
    char previous[PATH_MAX];
    const char *current;
    if (!path || !path[0]) {
        remember_project("");
        return 0;
    }
    if (settings_canonicalize_dir(path, canon, sizeof canon) != 0) return -1;
    current = settings_get_project_root();
    if (!current) current = "";
    if (strlen(current) >= sizeof previous) return -1;
    memcpy(previous, current, strlen(current) + 1);
    remember_project(canon);
    if (!persist) return 0;
    if (write_project_file(canon) != 0) {
        remember_project(previous);
        return -1;
    }
    return 0;
}

int settings_set_project_root(const char *new_root) {
    return store_project(new_root, 1);
}

int settings_load_project_root(void) {
    char saved[PATH_MAX];
    if (g_cli_root) return 1;
    if (read_saved_project(saved, sizeof saved) != 0) return 1;
    if (store_project(saved, 0) != 0) return -1;
    return 0;
}

int settings_init_roots(const char *studio_home, const char *cli_root) {
    if (settings_set_studio_home(studio_home) != 0) return -1;
    if (cli_root && cli_root[0]) {
        g_cli_root = 1;
        return store_project(cli_root, 0);
    }
    g_cli_root = 0;
    if (settings_load_project_root() == 0) return 0;
    return store_project(studio_home, 0);
}
