#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "fs_ops.h"

#include "config.h"
#include "gallery.h"
#include "preview_draft.h"
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
#include <direct.h>
#ifndef _MSC_VER
#include <dirent.h>
#endif
#else
#include <dirent.h>
#include <pthread.h>
#include <strings.h>
#include <unistd.h>
#endif

#include "compat.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *const OPS_CONTENT_SECTIONS[] = {
    "sales", "articles", "awards", "projects", "video",
    "whatsnew", "parts", "contact", "newsletter"
};

static const char *const OPS_ALLOWED_EXTS[] = {
    ".html", ".htm", ".shtml", ".php",
    ".txt", ".json", ".css", ".js", ".xml", ".csv", ".md",
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".svg", ".bmp", ".tif", ".tiff", ".ico", ".avif",
    ".pdf", ".mp4", ".m4v", ".webm", ".mov", ".mp3", ".wav", ".swf", ".rm", ".doc", ".docx", ".rtf"
};

#define FS_HISTORY_MAX 64

typedef enum {
    FS_OP_RENAME = 1,
    FS_OP_DUPLICATE,
    FS_OP_COPY,
    FS_OP_MOVE,
    FS_OP_DELETE
} FsOpType;

typedef struct {
    int id;
    FsOpType op;
    int is_folder;
    char repo_root[PATH_MAX];
    char old_section[64];
    char new_section[64];
    char old_slug[512];
    char new_slug[512];
    char old_path[1200];
    char new_path[1200];
    char old_name[256];
    char new_name[256];
    char trash_fs[PATH_MAX];
    char label[256];
} FsHistoryEntry;

static FsHistoryEntry g_history[FS_HISTORY_MAX];
static int g_hist_count = 0;
static int g_hist_cursor = 0;
static int g_next_op_id = 1;
static char g_hist_repo[PATH_MAX];

#ifdef _WIN32
static CRITICAL_SECTION g_ops_lock;
static INIT_ONCE g_ops_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK ops_lock_init(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    InitializeCriticalSection(&g_ops_lock);
    return TRUE;
}
static void ops_lock(void) {
    InitOnceExecuteOnce(&g_ops_once, ops_lock_init, NULL, NULL);
    EnterCriticalSection(&g_ops_lock);
}
static void ops_unlock(void) { LeaveCriticalSection(&g_ops_lock); }
#else
static pthread_mutex_t g_ops_lock = PTHREAD_MUTEX_INITIALIZER;
static void ops_lock(void) { pthread_mutex_lock(&g_ops_lock); }
static void ops_unlock(void) { pthread_mutex_unlock(&g_ops_lock); }
#endif

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (!err || err_sz == 0) return;
    snprintf(err, err_sz, "%s", msg && msg[0] ? msg : "Operation failed");
}

static int file_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int any_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0;
}

static int mkdir_single(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
#ifdef _WIN32
    if (_mkdir(path) == 0) return 0;
#else
    if (mkdir(path, 0755) == 0) return 0;
#endif
    return (errno == EEXIST && dir_exists(path)) ? 0 : -1;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;
    if (!path || !path[0]) return -1;
    len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    while (len > 1 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) tmp[--len] = '\0';
    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
#ifdef _WIN32
            if (p == tmp + 2 && tmp[1] == ':') continue;
#endif
            *p = '\0';
            if (mkdir_single(tmp) != 0) return -1;
            *p = '/';
        }
    }
    return mkdir_single(tmp);
}

static char *join_path2(const char *a, const char *b) {
    size_t na, nb;
    char *out;
    if (!a || !b) return NULL;
    na = strlen(a);
    while (na > 0 && (a[na - 1] == '/' || a[na - 1] == '\\')) na--;
    while (*b == '/' || *b == '\\') b++;
    nb = strlen(b);
    out = (char *)malloc(na + 1 + nb + 1);
    if (!out) return NULL;
    memcpy(out, a, na);
    out[na] = '/';
    memcpy(out + na + 1, b, nb + 1);
    return out;
}

static char *join_path3(const char *a, const char *b, const char *c) {
    char *ab, *abc;
    if (!b || !b[0]) return join_path2(a, c);
    ab = join_path2(a, b);
    if (!ab) return NULL;
    abc = join_path2(ab, c);
    free(ab);
    return abc;
}

static char *public_join(const char *section, const char *slug, const char *filename) {
    size_t ns = section ? strlen(section) : 0;
    size_t nslug = slug ? strlen(slug) : 0;
    size_t nf = filename ? strlen(filename) : 0;
    char *out = (char *)malloc(1 + ns + (nslug ? 1 + nslug : 0) + (nf ? 1 + nf : 0) + 1);
    size_t pos = 0;
    if (!out) return NULL;
    out[pos++] = '/';
    if (ns) { memcpy(out + pos, section, ns); pos += ns; }
    if (nslug) { out[pos++] = '/'; memcpy(out + pos, slug, nslug); pos += nslug; }
    if (nf) { out[pos++] = '/'; memcpy(out + pos, filename, nf); pos += nf; }
    out[pos] = '\0';
    return out;
}

static char *canon_path(const char *path) {
#ifdef _WIN32
    char buf[4096];
    char *p;
    DWORD n;
    if (!path || !path[0]) return NULL;
    n = GetFullPathNameA(path, (DWORD)sizeof(buf), buf, NULL);
    if (n == 0 || n >= sizeof(buf)) return NULL;
    for (p = buf; *p; p++) if (*p == '\\') *p = '/';
    return strdup(buf);
#else
    if (!path || !path[0]) return NULL;
    return realpath(path, NULL);
#endif
}

static int path_within(const char *root, const char *path) {
    char *root_real = canon_path(root);
    char *path_real = canon_path(path);
    int ok = 0;
    if (root_real && path_real) {
        size_t n = strlen(root_real);
        if (strncmp(path_real, root_real, n) == 0 && (path_real[n] == '\0' || path_real[n] == '/')) ok = 1;
    }
    free(root_real);
    free(path_real);
    return ok;
}

static int fs_inside_repo(const char *repo, const char *fs) {
    char *copy, *slash;
    int ok;
    if (!repo || !fs) return 0;
    if (any_exists(fs)) return path_within(repo, fs);
    copy = strdup(fs);
    if (!copy) return 0;
    slash = strrchr(copy, '/');
    if (!slash || slash == copy) { free(copy); return 0; }
    *slash = '\0';
    ok = dir_exists(copy) && path_within(repo, copy);
    free(copy);
    return ok;
}

static int is_content_section(const char *section) {
    size_t i;
    if (!section || !section[0]) return 0;
    for (i = 0; i < sizeof(OPS_CONTENT_SECTIONS) / sizeof(OPS_CONTENT_SECTIONS[0]); i++) {
        if (strcmp(section, OPS_CONTENT_SECTIONS[i]) == 0) return 1;
    }
    return 0;
}

static int is_editable_section(const char *section) {
    if (is_content_section(section)) return 1;
    if (!section || !section[0]) return 0;
    return strcmp(section, "includes") == 0 || strcmp(section, "style") == 0 ||
           strcmp(section, "php") == 0 || strcmp(section, "scripts") == 0 ||
           strcmp(section, "js") == 0 || strcmp(section, "css") == 0;
}

static int is_reserved_dir_name(const char *name) {
    if (!name || !name[0]) return 1;
    return strcasecmp(name, "slides") == 0 || strcasecmp(name, "thumbs") == 0 ||
           strcasecmp(name, "res") == 0 || strcasecmp(name, "album") == 0 ||
           strcasecmp(name, "temphidden") == 0;
}

static int ends_with_ci(const char *text, const char *suffix) {
    size_t n, m;
    if (!text || !suffix) return 0;
    n = strlen(text);
    m = strlen(suffix);
    return n >= m && strcasecmp(text + n - m, suffix) == 0;
}

static int starts_with_ci(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    return strncasecmp(text, prefix, strlen(prefix)) == 0;
}

static int is_junk_filename(const char *name) {
    if (!name || !name[0] || name[0] == '.') return 1;
    if (strcasecmp(name, "Thumbs.db") == 0 || strcasecmp(name, "ehthumbs.db") == 0 ||
        strcasecmp(name, "desktop.ini") == 0 || strcasecmp(name, "lifeboat.zip") == 0 ||
        strcasecmp(name, "album.rss") == 0 || strcasecmp(name, "manifest.jmf") == 0) return 1;
    if (starts_with_ci(name, ".fuse_hidden") || starts_with_ci(name, "folderimg") ||
        starts_with_ci(name, "folderimage")) return 1;
    return 0;
}

static int has_disallowed_chars(const char *s) {
    const unsigned char *p;
    if (!s) return 1;
    for (p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7F || *p == '\\' || *p == '?' || *p == '#') return 1;
    }
    return 0;
}

static int valid_single_segment(const char *name) {
    size_t n;
    if (!name || !name[0]) return 0;
    n = strlen(name);
    if (n > 255 || name[0] == '.' || name[n - 1] == '.' || name[n - 1] == ' ') return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\') || has_disallowed_chars(name)) return 0;
    return gallery_valid_slug(name);
}

static int valid_filename(const char *name) {
    const char *dot;
    size_t i;
    if (!valid_single_segment(name) || is_junk_filename(name)) return 0;
    dot = strrchr(name, '.');
    if (!dot || dot == name || dot[1] == '\0') return 0;
    for (i = 0; i < sizeof(OPS_ALLOWED_EXTS) / sizeof(OPS_ALLOWED_EXTS[0]); i++) {
        if (ends_with_ci(name, OPS_ALLOWED_EXTS[i])) return 1;
    }
    return 0;
}

static int valid_slug_path(const char *slug) {
    const char *p;
    if (!slug || !slug[0] || slug[0] == '/' || strstr(slug, "//") || strstr(slug, "..") || has_disallowed_chars(slug))
        return 0;
    p = slug;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        char seg[512];
        if (len == 0 || len >= sizeof(seg)) return 0;
        memcpy(seg, p, len);
        seg[len] = '\0';
        if (!valid_single_segment(seg)) return 0;
        if (!slash) break;
        p = slash + 1;
        if (*p == '\0') return 0;
    }
    return 1;
}

static int hex_val(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static char *url_decode_str(const char *src) {
    size_t len, i = 0, w = 0;
    char *out;
    if (!src) return NULL;
    len = strlen(src);
    out = (char *)malloc(len + 1);
    if (!out) return NULL;
    while (i < len) {
        if (src[i] == '%' && i + 2 < len) {
            int hi = hex_val((unsigned char)src[i + 1]);
            int lo = hex_val((unsigned char)src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                int byte = (hi << 4) | lo;
                if (byte == 0) { free(out); return NULL; }
                out[w++] = (char)byte;
                i += 3;
                continue;
            }
        }
        out[w++] = src[i++];
    }
    out[w] = '\0';
    return out;
}

static char *json_str_dup(const JsonValue *obj, const char *key) {
    JsonValue *val;
    const char *text;
    if (!obj || json_type(obj) != JSON_OBJECT) return strdup("");
    val = json_object_get(obj, key);
    if (!val || json_type(val) != JSON_STRING) return strdup("");
    text = json_string_get(val, NULL);
    return strdup(text ? text : "");
}

static void trim_spaces(char *s) {
    char *start;
    size_t n;
    if (!s) return;
    start = s;
    while (*start == ' ') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    n = strlen(s);
    while (n > 0 && s[n - 1] == ' ') s[--n] = '\0';
}

static int copy_single_file(const char *src_fs, const char *dst_fs) {
    FILE *in, *out;
    char buf[16384];
    size_t n;
    int rc = 0;
    in = fopen(src_fs, "rb");
    if (!in) return -1;
    out = fopen(dst_fs, "wb");
    if (!out) { fclose(in); return -1; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    if (ferror(in)) rc = -1;
    if (fflush(out) != 0) rc = -1;
    fclose(out);
    fclose(in);
    if (rc != 0) remove(dst_fs);
    return rc;
}

static int copy_dir_recursive(const char *src_dir, const char *dst_dir, int depth) {
    DIR *dir;
    struct dirent *ent;
    int rc = 0;
    if (depth > 16) return -1;
    if (mkdir_p(dst_dir) != 0) return -1;
    dir = opendir(src_dir);
    if (!dir) return -1;
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        char *s_child, *d_child;
        struct stat st;
        if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        s_child = join_path2(src_dir, name);
        d_child = join_path2(dst_dir, name);
        if (!s_child || !d_child) {
            free(s_child);
            free(d_child);
            rc = -1;
            break;
        }
#ifndef _WIN32
        if (lstat(s_child, &st) != 0) {
#else
        if (stat(s_child, &st) != 0) {
#endif
            free(s_child);
            free(d_child);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (copy_dir_recursive(s_child, d_child, depth + 1) != 0) rc = -1;
        } else if (S_ISREG(st.st_mode)) {
            if (copy_single_file(s_child, d_child) != 0) rc = -1;
        }
        free(s_child);
        free(d_child);
        if (rc != 0) break;
    }
    closedir(dir);
    return rc;
}

static int remove_recursive(const char *path, int depth) {
    struct stat st;
    if (!path || !path[0] || depth > 20) return -1;
#ifndef _WIN32
    if (lstat(path, &st) != 0) return 0;
#else
    if (stat(path, &st) != 0) return 0;
#endif
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        struct dirent *ent;
        if (!dir) return -1;
        while ((ent = readdir(dir)) != NULL) {
            char *child;
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            child = join_path2(path, ent->d_name);
            if (child) {
                remove_recursive(child, depth + 1);
                free(child);
            }
        }
        closedir(dir);
#ifdef _WIN32
        return _rmdir(path);
#else
        return rmdir(path);
#endif
    }
    return remove(path);
}

static int move_fs_entry(const char *src_fs, const char *dst_fs, int is_folder) {
    if (rename(src_fs, dst_fs) == 0) return 0;
    if (is_folder) {
        if (copy_dir_recursive(src_fs, dst_fs, 0) != 0) {
            remove_recursive(dst_fs, 0);
            return -1;
        }
        return remove_recursive(src_fs, 0);
    }
    if (copy_single_file(src_fs, dst_fs) != 0) return -1;
    return remove(src_fs);
}

static int is_page_ext(const char *name) {
    return ends_with_ci(name, ".html") || ends_with_ci(name, ".htm") ||
           ends_with_ci(name, ".shtml") || ends_with_ci(name, ".php");
}

static int index_priority(const char *name) {
    if (strcmp(name, "index.php") == 0) return 0;
    if (strcmp(name, "index.html") == 0) return 1;
    if (strcmp(name, "index.shtml") == 0) return 2;
    if (strcmp(name, "index.htm") == 0) return 3;
    return 100;
}

static char *find_folder_primary(const char *folder_fs, const char *section, const char *slug) {
    DIR *dir;
    struct dirent *ent;
    const char *slug_leaf;
    char *best_index = NULL, *slug_match = NULL, *first_page = NULL, *first_asset = NULL;
    int best_index_rank = 1000;

    if (!folder_fs || !dir_exists(folder_fs)) return strdup("");
    if (section && strcmp(section, "sales") == 0) {
        char *idx = join_path2(folder_fs, "index.php");
        if (idx && file_exists(idx)) {
            free(idx);
            return public_join(section, slug, "index.php");
        }
        free(idx);
    }
    slug_leaf = slug ? strrchr(slug, '/') : NULL;
    slug_leaf = slug_leaf ? slug_leaf + 1 : (slug ? slug : "");
    dir = opendir(folder_fs);
    if (!dir) return strdup("");
    while ((ent = readdir(dir)) != NULL) {
        const char *name = ent->d_name;
        char *child;
        if (!name[0] || name[0] == '.' || is_junk_filename(name) || !valid_filename(name)) continue;
        child = join_path2(folder_fs, name);
        if (!child || !file_exists(child)) { free(child); continue; }
        free(child);
        if (is_page_ext(name)) {
            int rank = index_priority(name);
            if (rank < best_index_rank) {
                best_index_rank = rank;
                free(best_index);
                best_index = strdup(name);
            }
            if (slug_leaf[0] && !slug_match) {
                char probe[600];
                snprintf(probe, sizeof(probe), "%s.html", slug_leaf);
                if (strcmp(name, probe) == 0) slug_match = strdup(name);
                snprintf(probe, sizeof(probe), "%s.shtml", slug_leaf);
                if (!slug_match && strcmp(name, probe) == 0) slug_match = strdup(name);
                snprintf(probe, sizeof(probe), "%s.htm", slug_leaf);
                if (!slug_match && strcmp(name, probe) == 0) slug_match = strdup(name);
                snprintf(probe, sizeof(probe), "%s.php", slug_leaf);
                if (!slug_match && strcmp(name, probe) == 0) slug_match = strdup(name);
            }
            if (!first_page && !strstr(name, ".mailchimp.")) first_page = strdup(name);
        } else if (!first_asset) {
            first_asset = strdup(name);
        }
    }
    closedir(dir);
    {
        const char *chosen = best_index ? best_index : (slug_match ? slug_match : (first_page ? first_page : first_asset));
        char *result = chosen ? public_join(section, slug, chosen) : strdup("");
        free(best_index);
        free(slug_match);
        free(first_page);
        free(first_asset);
        return result ? result : strdup("");
    }
}

static int split_public_path(const char *raw_path, char **section_out, char **rest_out,
                             int allow_section_only, char *err, size_t err_sz) {
    char *decoded;
    const char *p, *slash;
    size_t sec_len;

    *section_out = NULL;
    *rest_out = NULL;

    if (!raw_path || raw_path[0] != '/') {
        set_err(err, err_sz, "Path must start with /");
        return 1;
    }
    if (has_disallowed_chars(raw_path) || strstr(raw_path, "..") || strstr(raw_path, "//")) {
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    if (strcmp(raw_path, CONFIG_PUBLIC_PATH) == 0 || strncmp(raw_path, "/__studio", 9) == 0) {
        set_err(err, err_sz, "Studio configuration files cannot be modified here");
        return 1;
    }
    decoded = url_decode_str(raw_path);
    if (!decoded) {
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    if (has_disallowed_chars(decoded) || strstr(decoded, "..") || strstr(decoded, "//")) {
        free(decoded);
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    p = decoded + 1;
    while (*p && p[strlen(p) - 1] == '/') decoded[strlen(decoded) - 1] = '\0';
    if (*p == '\0') {
        free(decoded);
        set_err(err, err_sz, "Top-level website sections cannot be modified");
        return 1;
    }
    slash = strchr(p, '/');
    sec_len = slash ? (size_t)(slash - p) : strlen(p);
    *section_out = strndup(p, sec_len);
    *rest_out = slash ? strdup(slash + 1) : strdup("");
    free(decoded);

    if (!*section_out || !*rest_out) {
        free(*section_out);
        free(*rest_out);
        *section_out = NULL;
        *rest_out = NULL;
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    if (!valid_single_segment(*section_out) || !is_editable_section(*section_out)) {
        free(*section_out);
        free(*rest_out);
        *section_out = NULL;
        *rest_out = NULL;
        set_err(err, err_sz, "Path must stay inside a content section");
        return 1;
    }
    if ((*rest_out)[0] == '\0') {
        if (!allow_section_only) {
            free(*section_out);
            free(*rest_out);
            *section_out = NULL;
            *rest_out = NULL;
            set_err(err, err_sz, "Top-level website sections cannot be modified");
            return 1;
        }
        return 0;
    }
    if (!valid_slug_path(*rest_out)) {
        free(*section_out);
        free(*rest_out);
        *section_out = NULL;
        *rest_out = NULL;
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    return 0;
}

static int resolve_trash_root(const char *repo_root, char *out, size_t out_sz) {
    const char *home = settings_get_studio_home();
    const char *base = (home && home[0]) ? home : repo_root;
    char state_dir[PATH_MAX];
    if (!base || !base[0]) return -1;
    if (settings_ensure_dirs(base) != 0) return -1;
    if (settings_install_path(base, "state", state_dir, sizeof(state_dir)) != 0) return -1;
    if (snprintf(out, out_sz, "%s/tree-trash", state_dir) >= (int)out_sz) return -1;
    return mkdir_p(out);
}

static void cleanup_history_entry(FsHistoryEntry *ent) {
    if (!ent) return;
    if (ent->trash_fs[0] && any_exists(ent->trash_fs)) {
        remove_recursive(ent->trash_fs, 0);
    }
    ent->trash_fs[0] = '\0';
}

static void ensure_repo_history_locked(const char *repo_root) {
    if (!repo_root || !repo_root[0]) return;
    if (g_hist_repo[0] && strcmp(g_hist_repo, repo_root) != 0) {
        int i;
        for (i = 0; i < g_hist_count; i++) cleanup_history_entry(&g_history[i]);
        g_hist_count = 0;
        g_hist_cursor = 0;
    }
    snprintf(g_hist_repo, sizeof(g_hist_repo), "%s", repo_root);
}

static void push_history_locked(const FsHistoryEntry *entry) {
    int i;
    ensure_repo_history_locked(entry->repo_root);
    for (i = g_hist_cursor; i < g_hist_count; i++) {
        cleanup_history_entry(&g_history[i]);
    }
    g_hist_count = g_hist_cursor;
    if (g_hist_count >= FS_HISTORY_MAX) {
        cleanup_history_entry(&g_history[0]);
        memmove(&g_history[0], &g_history[1], sizeof(FsHistoryEntry) * (FS_HISTORY_MAX - 1));
        g_hist_count = FS_HISTORY_MAX - 1;
        g_hist_cursor = g_hist_count;
    }
    g_history[g_hist_count] = *entry;
    g_history[g_hist_count].id = g_next_op_id++;
    g_hist_count++;
    g_hist_cursor = g_hist_count;
}

static const char *op_kind_name(FsOpType op) {
    switch (op) {
        case FS_OP_RENAME: return "rename";
        case FS_OP_DUPLICATE: return "duplicate";
        case FS_OP_COPY: return "copy";
        case FS_OP_MOVE: return "move";
        case FS_OP_DELETE: return "delete";
        default: return "operation";
    }
}

static void attach_history_locked(JsonValue *root) {
    int can_undo = g_hist_cursor > 0;
    int can_redo = g_hist_cursor < g_hist_count;
    const char *undo_label = can_undo ? g_history[g_hist_cursor - 1].label : "";
    const char *redo_label = can_redo ? g_history[g_hist_cursor].label : "";
    if (!root || json_type(root) != JSON_OBJECT) return;
    json_object_put(root, "canUndo", json_bool(can_undo));
    json_object_put(root, "canRedo", json_bool(can_redo));
    json_object_put(root, "undoLabel", json_string_cstr(undo_label));
    json_object_put(root, "redoLabel", json_string_cstr(redo_label));
    json_object_put(root, "undoCount", json_number_int(g_hist_cursor));
    json_object_put(root, "redoCount", json_number_int(g_hist_count - g_hist_cursor));
}

void fs_ops_record_rename(const char *repo_root, const char *kind,
                          const char *old_section, const char *new_section,
                          const char *old_slug, const char *new_slug,
                          const char *old_path, const char *new_path,
                          const char *old_name, const char *new_name,
                          JsonValue *response_root) {
    FsHistoryEntry ent;
    memset(&ent, 0, sizeof(ent));
    ent.op = FS_OP_RENAME;
    ent.is_folder = (kind && strcmp(kind, "folder") == 0) ? 1 : 0;
    snprintf(ent.repo_root, sizeof(ent.repo_root), "%s", repo_root ? repo_root : "");
    snprintf(ent.old_section, sizeof(ent.old_section), "%s", old_section ? old_section : "");
    snprintf(ent.new_section, sizeof(ent.new_section), "%s", new_section ? new_section : "");
    snprintf(ent.old_slug, sizeof(ent.old_slug), "%s", old_slug ? old_slug : "");
    snprintf(ent.new_slug, sizeof(ent.new_slug), "%s", new_slug ? new_slug : "");
    snprintf(ent.old_path, sizeof(ent.old_path), "%s", old_path ? old_path : "");
    snprintf(ent.new_path, sizeof(ent.new_path), "%s", new_path ? new_path : "");
    snprintf(ent.old_name, sizeof(ent.old_name), "%s", old_name ? old_name : "");
    snprintf(ent.new_name, sizeof(ent.new_name), "%s", new_name ? new_name : "");
    snprintf(ent.label, sizeof(ent.label), "Rename %s to %s",
             old_name && old_name[0] ? old_name : (old_slug ? old_slug : "item"),
             new_name && new_name[0] ? new_name : (new_slug ? new_slug : "item"));
    ops_lock();
    push_history_locked(&ent);
    attach_history_locked(response_root);
    ops_unlock();
}

static char *choose_unique_file_name(const char *parent_fs, const char *orig_name, int force_copy_suffix) {
    const char *dot = strrchr(orig_name, '.');
    size_t stem_len = (dot && dot > orig_name) ? (size_t)(dot - orig_name) : strlen(orig_name);
    const char *ext = (dot && dot > orig_name) ? dot : "";
    char stem[256];
    int n;

    if (stem_len >= sizeof(stem)) stem_len = sizeof(stem) - 1;
    memcpy(stem, orig_name, stem_len);
    stem[stem_len] = '\0';

    if (!force_copy_suffix) {
        char *probe = join_path2(parent_fs, orig_name);
        if (probe && !any_exists(probe)) {
            free(probe);
            return strdup(orig_name);
        }
        free(probe);
    }

    for (n = 1; n <= 999; n++) {
        char candidate[320];
        char *probe;
        if (n == 1) {
            snprintf(candidate, sizeof(candidate), "%s-copy%s", stem, ext);
        } else {
            snprintf(candidate, sizeof(candidate), "%s-copy-%d%s", stem, n, ext);
        }
        if (!valid_filename(candidate)) continue;
        probe = join_path2(parent_fs, candidate);
        if (probe && !any_exists(probe)) {
            free(probe);
            return strdup(candidate);
        }
        free(probe);
    }
    return NULL;
}

static char *choose_unique_folder_slug(const char *repo_root, const char *section,
                                       const char *base_slug, int force_copy_suffix) {
    const char *slash = strrchr(base_slug, '/');
    char prefix[384];
    const char *leaf;
    int n;

    if (slash) {
        size_t plen = (size_t)(slash - base_slug);
        if (plen >= sizeof(prefix)) plen = sizeof(prefix) - 1;
        memcpy(prefix, base_slug, plen);
        prefix[plen] = '\0';
        leaf = slash + 1;
    } else {
        prefix[0] = '\0';
        leaf = base_slug;
    }

    if (!force_copy_suffix && valid_single_segment(leaf) && !is_reserved_dir_name(leaf)) {
        char *probe = join_path3(repo_root, section, base_slug);
        if (probe && !any_exists(probe)) {
            free(probe);
            return strdup(base_slug);
        }
        free(probe);
    }

    for (n = 1; n <= 999; n++) {
        char new_leaf[280];
        char candidate[600];
        char *probe;
        if (n == 1) {
            snprintf(new_leaf, sizeof(new_leaf), "%s-copy", leaf);
        } else {
            snprintf(new_leaf, sizeof(new_leaf), "%s-copy-%d", leaf, n);
        }
        if (!valid_single_segment(new_leaf) || is_reserved_dir_name(new_leaf)) continue;
        if (prefix[0]) {
            snprintf(candidate, sizeof(candidate), "%s/%s", prefix, new_leaf);
        } else {
            snprintf(candidate, sizeof(candidate), "%s", new_leaf);
        }
        probe = join_path3(repo_root, section, candidate);
        if (probe && !any_exists(probe)) {
            free(probe);
            return strdup(candidate);
        }
        free(probe);
    }
    return NULL;
}

static int resolve_item_target(const char *repo_root, const JsonValue *body,
                               int *is_folder_out, char **sec_out, char **slug_out,
                               char **fname_out, char **fs_out, char **pub_out,
                               char *err, size_t err_sz) {
    char *kind = json_str_dup(body, "kind");
    char *raw_path = json_str_dup(body, "path");
    char *sec = json_str_dup(body, "section");
    char *slug = json_str_dup(body, "slug");
    char *path_sec = NULL, *path_rest = NULL;
    int is_folder = -1;
    int rc = 0;

    *sec_out = NULL; *slug_out = NULL; *fname_out = NULL; *fs_out = NULL; *pub_out = NULL;
    trim_spaces(kind);
    if (strcasecmp(kind, "section") == 0) {
        set_err(err, err_sz, "Top-level website sections cannot be modified");
        rc = 1;
        goto fail;
    }
    if (strcasecmp(kind, "folder") == 0 || strcasecmp(kind, "directory") == 0) is_folder = 1;
    else if (strcasecmp(kind, "file") == 0 || strcasecmp(kind, "page") == 0) is_folder = 0;

    if (raw_path[0]) {
        rc = split_public_path(raw_path, &path_sec, &path_rest, 0, err, err_sz);
        if (rc != 0) goto fail;
    } else if (sec[0] && slug[0]) {
        if (!valid_single_segment(sec) || !is_editable_section(sec) || !valid_slug_path(slug)) {
            set_err(err, err_sz, "Path is not allowed");
            rc = 1;
            goto fail;
        }
        path_sec = strdup(sec);
        path_rest = strdup(slug);
    } else {
        set_err(err, err_sz, "path is required");
        rc = 1;
        goto fail;
    }

    if (is_folder < 0) {
        char *probe = join_path3(repo_root, path_sec, path_rest);
        if (probe && dir_exists(probe)) is_folder = 1;
        else if (probe && file_exists(probe)) is_folder = 0;
        else {
            const char *last = strrchr(path_rest, '/');
            last = last ? last + 1 : path_rest;
            is_folder = strchr(last, '.') ? 0 : 1;
        }
        free(probe);
    }

    if (is_folder) {
        if (!is_content_section(path_sec)) {
            set_err(err, err_sz, "Folder operations are only supported in content sections");
            rc = 1;
            goto fail;
        }
        *sec_out = path_sec; path_sec = NULL;
        *slug_out = path_rest; path_rest = NULL;
        *fname_out = strdup("");
        *fs_out = join_path3(repo_root, *sec_out, *slug_out);
        *pub_out = public_join(*sec_out, *slug_out, NULL);
        if (!*fs_out || !dir_exists(*fs_out)) {
            set_err(err, err_sz, "Folder not found");
            rc = 2;
            goto fail;
        }
    } else {
        const char *slash = strrchr(path_rest, '/');
        *sec_out = path_sec; path_sec = NULL;
        if (slash) {
            *slug_out = strndup(path_rest, (size_t)(slash - path_rest));
            *fname_out = strdup(slash + 1);
        } else {
            *slug_out = strdup("");
            *fname_out = strdup(path_rest);
        }
        free(path_rest); path_rest = NULL;
        if (!valid_filename(*fname_out)) {
            set_err(err, err_sz, "File path is not allowed");
            rc = 1;
            goto fail;
        }
        {
            char *dir_fs = join_path3(repo_root, *sec_out, (*slug_out)[0] ? *slug_out : "");
            *fs_out = dir_fs ? join_path2(dir_fs, *fname_out) : NULL;
            free(dir_fs);
        }
        *pub_out = public_join(*sec_out, (*slug_out)[0] ? *slug_out : NULL, *fname_out);
        if (!*fs_out || !file_exists(*fs_out)) {
            set_err(err, err_sz, "File not found");
            rc = 2;
            goto fail;
        }
    }
    if (!fs_inside_repo(repo_root, *fs_out)) {
        set_err(err, err_sz, "Path is outside the repository");
        rc = 1;
        goto fail;
    }
    *is_folder_out = is_folder;
    free(kind); free(raw_path); free(sec); free(slug);
    return 0;

fail:
    free(kind); free(raw_path); free(sec); free(slug);
    free(path_sec); free(path_rest);
    free(*sec_out); free(*slug_out); free(*fname_out); free(*fs_out); free(*pub_out);
    *sec_out = *slug_out = *fname_out = *fs_out = *pub_out = NULL;
    return rc;
}

int fs_ops_duplicate(const char *repo_root, const JsonValue *body, JsonDoc **out,
                     char *err, size_t err_sz) {
    int is_folder = 0;
    char *sec = NULL, *slug = NULL, *fname = NULL, *src_fs = NULL, *src_pub = NULL;
    char *new_slug = NULL, *new_fname = NULL, *dst_fs = NULL, *dst_pub = NULL, *primary_pub = NULL;
    char *custom_name = NULL;
    JsonValue *root = NULL;
    FsHistoryEntry hist;
    int rc;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) { set_err(err, err_sz, "Repository root is not configured"); return -1; }
    if (!body || json_type(body) != JSON_OBJECT) { set_err(err, err_sz, "JSON body must be an object"); return 1; }

    rc = resolve_item_target(repo_root, body, &is_folder, &sec, &slug, &fname, &src_fs, &src_pub, err, err_sz);
    if (rc != 0) return rc;

    custom_name = json_str_dup(body, "newName");
    trim_spaces(custom_name);

    if (is_folder) {
        if (custom_name[0]) {
            const char *slash = strrchr(slug, '/');
            if (!valid_single_segment(custom_name) || is_reserved_dir_name(custom_name)) {
                set_err(err, err_sz, "Folder name is not allowed");
                rc = 1; goto cleanup;
            }
            if (slash) {
                size_t plen = (size_t)(slash - slug);
                new_slug = (char *)malloc(plen + 1 + strlen(custom_name) + 1);
                if (new_slug) {
                    memcpy(new_slug, slug, plen);
                    new_slug[plen] = '/';
                    memcpy(new_slug + plen + 1, custom_name, strlen(custom_name) + 1);
                }
            } else {
                new_slug = strdup(custom_name);
            }
        } else {
            new_slug = choose_unique_folder_slug(repo_root, sec, slug, 1);
        }
        if (!new_slug) { set_err(err, err_sz, "Could not generate duplicate folder name"); rc = -1; goto cleanup; }
        dst_fs = join_path3(repo_root, sec, new_slug);
        dst_pub = public_join(sec, new_slug, NULL);
        if (!dst_fs || !dst_pub) { set_err(err, err_sz, "out of memory"); rc = -1; goto cleanup; }
        if (any_exists(dst_fs)) { set_err(err, err_sz, "Target folder already exists"); rc = 3; goto cleanup; }
        if (!fs_inside_repo(repo_root, dst_fs) || copy_dir_recursive(src_fs, dst_fs, 0) != 0) {
            remove_recursive(dst_fs, 0);
            set_err(err, err_sz, "Could not duplicate folder");
            rc = -1; goto cleanup;
        }
        primary_pub = find_folder_primary(dst_fs, sec, new_slug);
    } else {
        char *parent_fs = join_path3(repo_root, sec, slug[0] ? slug : "");
        if (custom_name[0]) {
            if (!valid_filename(custom_name)) {
                free(parent_fs);
                set_err(err, err_sz, "Filename is not allowed");
                rc = 1; goto cleanup;
            }
            new_fname = strdup(custom_name);
        } else {
            new_fname = parent_fs ? choose_unique_file_name(parent_fs, fname, 1) : NULL;
        }
        if (!parent_fs || !new_fname) {
            free(parent_fs);
            set_err(err, err_sz, "Could not generate duplicate filename");
            rc = -1; goto cleanup;
        }
        dst_fs = join_path2(parent_fs, new_fname);
        free(parent_fs);
        dst_pub = public_join(sec, slug[0] ? slug : NULL, new_fname);
        if (!dst_fs || !dst_pub) { set_err(err, err_sz, "out of memory"); rc = -1; goto cleanup; }
        if (any_exists(dst_fs)) { set_err(err, err_sz, "Target file already exists"); rc = 3; goto cleanup; }
        if (!fs_inside_repo(repo_root, dst_fs) || copy_single_file(src_fs, dst_fs) != 0) {
            set_err(err, err_sz, "Could not duplicate file");
            rc = -1; goto cleanup;
        }
    }

    memset(&hist, 0, sizeof(hist));
    hist.op = FS_OP_DUPLICATE;
    hist.is_folder = is_folder;
    snprintf(hist.repo_root, sizeof(hist.repo_root), "%s", repo_root);
    snprintf(hist.old_section, sizeof(hist.old_section), "%s", sec);
    snprintf(hist.new_section, sizeof(hist.new_section), "%s", sec);
    snprintf(hist.old_slug, sizeof(hist.old_slug), "%s", slug);
    snprintf(hist.new_slug, sizeof(hist.new_slug), "%s", is_folder ? new_slug : slug);
    snprintf(hist.old_path, sizeof(hist.old_path), "%s", src_pub);
    snprintf(hist.new_path, sizeof(hist.new_path), "%s", dst_pub);
    snprintf(hist.old_name, sizeof(hist.old_name), "%s", is_folder ? slug : fname);
    snprintf(hist.new_name, sizeof(hist.new_name), "%s", is_folder ? new_slug : new_fname);
    snprintf(hist.label, sizeof(hist.label), "Duplicate %s", is_folder ? slug : fname);

    root = json_object();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "op", json_string_cstr("duplicate"));
    json_object_put(root, "kind", json_string_cstr(is_folder ? "folder" : "file"));
    json_object_put(root, "oldPath", json_string_cstr(src_pub));
    json_object_put(root, "newPath", json_string_cstr(dst_pub));
    json_object_put(root, "path", json_string_cstr(dst_pub));
    json_object_put(root, "section", json_string_cstr(sec));
    json_object_put(root, "oldSlug", json_string_cstr(slug));
    json_object_put(root, "newSlug", json_string_cstr(is_folder ? new_slug : slug));
    json_object_put(root, "oldName", json_string_cstr(is_folder ? slug : fname));
    json_object_put(root, "newName", json_string_cstr(is_folder ? new_slug : new_fname));
    json_object_put(root, "primaryFile", json_string_cstr(is_folder ? (primary_pub ? primary_pub : "") : dst_pub));

    ops_lock();
    push_history_locked(&hist);
    attach_history_locked(root);
    ops_unlock();

    if (out) *out = json_doc_new(root);
    else json_free(root);

cleanup:
    free(custom_name);
    free(sec); free(slug); free(fname); free(src_fs); free(src_pub);
    free(new_slug); free(new_fname); free(dst_fs); free(dst_pub); free(primary_pub);
    return rc;
}

static int resolve_dest_container(const char *repo_root, const JsonValue *body,
                                  const char *fallback_sec, char **target_sec_out,
                                  char **target_slug_out, char *err, size_t err_sz) {
    char *target_path = json_str_dup(body, "targetPath");
    char *target_sec = json_str_dup(body, "targetSection");
    char *target_slug = json_str_dup(body, "targetSlug");
    int rc = 0;

    *target_sec_out = NULL;
    *target_slug_out = NULL;

    if (!target_path[0]) {
        free(target_path);
        target_path = json_str_dup(body, "destination");
    }
    trim_spaces(target_path);
    trim_spaces(target_sec);
    trim_spaces(target_slug);

    if (target_path[0]) {
        char *psec = NULL, *prest = NULL;
        rc = split_public_path(target_path, &psec, &prest, 1, err, err_sz);
        if (rc != 0) goto done;
        if (prest[0]) {
            char *probe = join_path3(repo_root, psec, prest);
            if (probe && file_exists(probe)) {
                const char *slash = strrchr(prest, '/');
                if (slash) {
                    char *folder_slug = strndup(prest, (size_t)(slash - prest));
                    free(prest);
                    prest = folder_slug ? folder_slug : strdup("");
                } else {
                    free(prest);
                    prest = strdup("");
                }
            }
            free(probe);
        }
        *target_sec_out = psec;
        *target_slug_out = prest;
    } else {
        const char *sec_use = target_sec[0] ? target_sec : fallback_sec;
        if (!sec_use || !sec_use[0] || !valid_single_segment(sec_use) || !is_editable_section(sec_use)) {
            set_err(err, err_sz, "Target section is not allowed");
            rc = 1;
            goto done;
        }
        if (target_slug[0] && !valid_slug_path(target_slug)) {
            set_err(err, err_sz, "Target folder is not allowed");
            rc = 1;
            goto done;
        }
        *target_sec_out = strdup(sec_use);
        *target_slug_out = strdup(target_slug);
    }

done:
    free(target_path);
    free(target_sec);
    free(target_slug);
    return rc;
}

int fs_ops_copy(const char *repo_root, const JsonValue *body, JsonDoc **out,
                char *err, size_t err_sz) {
    int is_folder = 0;
    char *sec = NULL, *slug = NULL, *fname = NULL, *src_fs = NULL, *src_pub = NULL;
    char *tsec = NULL, *tslug = NULL;
    char *new_slug = NULL, *new_fname = NULL, *dst_fs = NULL, *dst_pub = NULL, *primary_pub = NULL;
    JsonValue *root = NULL;
    FsHistoryEntry hist;
    int rc;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) { set_err(err, err_sz, "Repository root is not configured"); return -1; }
    if (!body || json_type(body) != JSON_OBJECT) { set_err(err, err_sz, "JSON body must be an object"); return 1; }

    rc = resolve_item_target(repo_root, body, &is_folder, &sec, &slug, &fname, &src_fs, &src_pub, err, err_sz);
    if (rc != 0) return rc;

    rc = resolve_dest_container(repo_root, body, sec, &tsec, &tslug, err, err_sz);
    if (rc != 0) goto cleanup;

    if (is_folder) {
        const char *src_leaf = strrchr(slug, '/');
        src_leaf = src_leaf ? src_leaf + 1 : slug;
        if (!is_content_section(tsec)) {
            set_err(err, err_sz, "Folders can only be pasted into content sections");
            rc = 1; goto cleanup;
        }
        new_slug = choose_unique_folder_slug(repo_root, tsec, src_leaf, 0);
        if (!new_slug) { set_err(err, err_sz, "Could not generate destination folder name"); rc = -1; goto cleanup; }
        dst_fs = join_path3(repo_root, tsec, new_slug);
        dst_pub = public_join(tsec, new_slug, NULL);
        if (!dst_fs || !dst_pub || !fs_inside_repo(repo_root, dst_fs) || copy_dir_recursive(src_fs, dst_fs, 0) != 0) {
            remove_recursive(dst_fs, 0);
            set_err(err, err_sz, "Could not copy folder");
            rc = -1; goto cleanup;
        }
        primary_pub = find_folder_primary(dst_fs, tsec, new_slug);
    } else {
        char *parent_fs = join_path3(repo_root, tsec, tslug[0] ? tslug : "");
        if (!parent_fs || !dir_exists(parent_fs)) {
            free(parent_fs);
            set_err(err, err_sz, "Target folder not found");
            rc = 2; goto cleanup;
        }
        new_fname = choose_unique_file_name(parent_fs, fname, 0);
        if (!new_fname) {
            free(parent_fs);
            set_err(err, err_sz, "Could not generate destination filename");
            rc = -1; goto cleanup;
        }
        dst_fs = join_path2(parent_fs, new_fname);
        free(parent_fs);
        dst_pub = public_join(tsec, tslug[0] ? tslug : NULL, new_fname);
        if (!dst_fs || !dst_pub || !fs_inside_repo(repo_root, dst_fs) || copy_single_file(src_fs, dst_fs) != 0) {
            set_err(err, err_sz, "Could not copy file");
            rc = -1; goto cleanup;
        }
    }

    memset(&hist, 0, sizeof(hist));
    hist.op = FS_OP_COPY;
    hist.is_folder = is_folder;
    snprintf(hist.repo_root, sizeof(hist.repo_root), "%s", repo_root);
    snprintf(hist.old_section, sizeof(hist.old_section), "%s", sec);
    snprintf(hist.new_section, sizeof(hist.new_section), "%s", tsec);
    snprintf(hist.old_slug, sizeof(hist.old_slug), "%s", slug);
    snprintf(hist.new_slug, sizeof(hist.new_slug), "%s", is_folder ? new_slug : tslug);
    snprintf(hist.old_path, sizeof(hist.old_path), "%s", src_pub);
    snprintf(hist.new_path, sizeof(hist.new_path), "%s", dst_pub);
    snprintf(hist.old_name, sizeof(hist.old_name), "%s", is_folder ? slug : fname);
    snprintf(hist.new_name, sizeof(hist.new_name), "%s", is_folder ? new_slug : new_fname);
    snprintf(hist.label, sizeof(hist.label), "Copy %s", is_folder ? slug : fname);

    root = json_object();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "op", json_string_cstr("copy"));
    json_object_put(root, "kind", json_string_cstr(is_folder ? "folder" : "file"));
    json_object_put(root, "oldPath", json_string_cstr(src_pub));
    json_object_put(root, "newPath", json_string_cstr(dst_pub));
    json_object_put(root, "path", json_string_cstr(dst_pub));
    json_object_put(root, "oldSection", json_string_cstr(sec));
    json_object_put(root, "section", json_string_cstr(tsec));
    json_object_put(root, "oldSlug", json_string_cstr(slug));
    json_object_put(root, "newSlug", json_string_cstr(is_folder ? new_slug : tslug));
    json_object_put(root, "oldName", json_string_cstr(is_folder ? slug : fname));
    json_object_put(root, "newName", json_string_cstr(is_folder ? new_slug : new_fname));
    json_object_put(root, "primaryFile", json_string_cstr(is_folder ? (primary_pub ? primary_pub : "") : dst_pub));

    ops_lock();
    push_history_locked(&hist);
    attach_history_locked(root);
    ops_unlock();

    if (out) *out = json_doc_new(root);
    else json_free(root);

cleanup:
    free(sec); free(slug); free(fname); free(src_fs); free(src_pub);
    free(tsec); free(tslug);
    free(new_slug); free(new_fname); free(dst_fs); free(dst_pub); free(primary_pub);
    return rc;
}

int fs_ops_move(const char *repo_root, const JsonValue *body, JsonDoc **out,
                char *err, size_t err_sz) {
    int is_folder = 0;
    char *sec = NULL, *slug = NULL, *fname = NULL, *src_fs = NULL, *src_pub = NULL;
    char *tsec = NULL, *tslug = NULL;
    char *new_slug = NULL, *new_fname = NULL, *dst_fs = NULL, *dst_pub = NULL, *primary_pub = NULL;
    JsonValue *root = NULL;
    FsHistoryEntry hist;
    int rc;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) { set_err(err, err_sz, "Repository root is not configured"); return -1; }
    if (!body || json_type(body) != JSON_OBJECT) { set_err(err, err_sz, "JSON body must be an object"); return 1; }

    rc = resolve_item_target(repo_root, body, &is_folder, &sec, &slug, &fname, &src_fs, &src_pub, err, err_sz);
    if (rc != 0) return rc;

    rc = resolve_dest_container(repo_root, body, sec, &tsec, &tslug, err, err_sz);
    if (rc != 0) goto cleanup;

    if (is_folder) {
        const char *src_leaf = strrchr(slug, '/');
        src_leaf = src_leaf ? src_leaf + 1 : slug;
        if (!is_content_section(tsec)) {
            set_err(err, err_sz, "Folders can only be moved into content sections");
            rc = 1; goto cleanup;
        }
        new_slug = strdup(src_leaf);
        dst_fs = join_path3(repo_root, tsec, new_slug);
        dst_pub = public_join(tsec, new_slug, NULL);
        if (!new_slug || !dst_fs || !dst_pub) { set_err(err, err_sz, "out of memory"); rc = -1; goto cleanup; }
        if (strcmp(src_pub, dst_pub) == 0) {
            set_err(err, err_sz, "Folder is already in that section");
            rc = 1; goto cleanup;
        }
        if (any_exists(dst_fs)) {
            set_err(err, err_sz, "A folder or file with that name already exists in destination");
            rc = 3; goto cleanup;
        }
        if (!fs_inside_repo(repo_root, dst_fs) || move_fs_entry(src_fs, dst_fs, 1) != 0) {
            set_err(err, err_sz, "Could not move folder");
            rc = -1; goto cleanup;
        }
        preview_draft_rename(repo_root, src_pub, dst_pub, 1);
        primary_pub = find_folder_primary(dst_fs, tsec, new_slug);
    } else {
        char *parent_fs = join_path3(repo_root, tsec, tslug[0] ? tslug : "");
        if (!parent_fs || !dir_exists(parent_fs)) {
            free(parent_fs);
            set_err(err, err_sz, "Target folder not found");
            rc = 2; goto cleanup;
        }
        new_fname = strdup(fname);
        dst_fs = join_path2(parent_fs, new_fname);
        free(parent_fs);
        dst_pub = public_join(tsec, tslug[0] ? tslug : NULL, new_fname);
        if (!new_fname || !dst_fs || !dst_pub) { set_err(err, err_sz, "out of memory"); rc = -1; goto cleanup; }
        if (strcmp(src_pub, dst_pub) == 0) {
            set_err(err, err_sz, "File is already in that folder");
            rc = 1; goto cleanup;
        }
        if (any_exists(dst_fs)) {
            set_err(err, err_sz, "A file or folder with that name already exists in destination");
            rc = 3; goto cleanup;
        }
        if (!fs_inside_repo(repo_root, dst_fs) || move_fs_entry(src_fs, dst_fs, 0) != 0) {
            set_err(err, err_sz, "Could not move file");
            rc = -1; goto cleanup;
        }
        preview_draft_rename(repo_root, src_pub, dst_pub, 0);
    }

    memset(&hist, 0, sizeof(hist));
    hist.op = FS_OP_MOVE;
    hist.is_folder = is_folder;
    snprintf(hist.repo_root, sizeof(hist.repo_root), "%s", repo_root);
    snprintf(hist.old_section, sizeof(hist.old_section), "%s", sec);
    snprintf(hist.new_section, sizeof(hist.new_section), "%s", tsec);
    snprintf(hist.old_slug, sizeof(hist.old_slug), "%s", slug);
    snprintf(hist.new_slug, sizeof(hist.new_slug), "%s", is_folder ? new_slug : tslug);
    snprintf(hist.old_path, sizeof(hist.old_path), "%s", src_pub);
    snprintf(hist.new_path, sizeof(hist.new_path), "%s", dst_pub);
    snprintf(hist.old_name, sizeof(hist.old_name), "%s", is_folder ? slug : fname);
    snprintf(hist.new_name, sizeof(hist.new_name), "%s", is_folder ? new_slug : new_fname);
    snprintf(hist.label, sizeof(hist.label), "Move %s", is_folder ? slug : fname);

    root = json_object();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "op", json_string_cstr("move"));
    json_object_put(root, "kind", json_string_cstr(is_folder ? "folder" : "file"));
    json_object_put(root, "oldPath", json_string_cstr(src_pub));
    json_object_put(root, "newPath", json_string_cstr(dst_pub));
    json_object_put(root, "path", json_string_cstr(dst_pub));
    json_object_put(root, "oldSection", json_string_cstr(sec));
    json_object_put(root, "section", json_string_cstr(tsec));
    json_object_put(root, "oldSlug", json_string_cstr(slug));
    json_object_put(root, "newSlug", json_string_cstr(is_folder ? new_slug : tslug));
    json_object_put(root, "oldName", json_string_cstr(is_folder ? slug : fname));
    json_object_put(root, "newName", json_string_cstr(is_folder ? new_slug : new_fname));
    json_object_put(root, "primaryFile", json_string_cstr(is_folder ? (primary_pub ? primary_pub : "") : dst_pub));

    ops_lock();
    push_history_locked(&hist);
    attach_history_locked(root);
    ops_unlock();

    if (out) *out = json_doc_new(root);
    else json_free(root);

cleanup:
    free(sec); free(slug); free(fname); free(src_fs); free(src_pub);
    free(tsec); free(tslug);
    free(new_slug); free(new_fname); free(dst_fs); free(dst_pub); free(primary_pub);
    return rc;
}

int fs_ops_delete(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz) {
    int is_folder = 0;
    char *sec = NULL, *slug = NULL, *fname = NULL, *src_fs = NULL, *src_pub = NULL;
    char trash_root[PATH_MAX];
    char trash_item[PATH_MAX];
    JsonValue *root = NULL;
    FsHistoryEntry hist;
    int rc;
    int trash_id;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) { set_err(err, err_sz, "Repository root is not configured"); return -1; }
    if (!body || json_type(body) != JSON_OBJECT) { set_err(err, err_sz, "JSON body must be an object"); return 1; }

    rc = resolve_item_target(repo_root, body, &is_folder, &sec, &slug, &fname, &src_fs, &src_pub, err, err_sz);
    if (rc != 0) return rc;

    if (resolve_trash_root(repo_root, trash_root, sizeof(trash_root)) != 0) {
        set_err(err, err_sz, "Could not initialize Studio trash directory");
        rc = -1; goto cleanup;
    }

    ops_lock();
    trash_id = g_next_op_id++;
    ops_unlock();

    snprintf(trash_item, sizeof(trash_item), "%s/item-%d", trash_root, trash_id);
    if (any_exists(trash_item)) remove_recursive(trash_item, 0);

    if (move_fs_entry(src_fs, trash_item, is_folder) != 0) {
        set_err(err, err_sz, is_folder ? "Could not delete folder" : "Could not delete file");
        rc = -1; goto cleanup;
    }

    memset(&hist, 0, sizeof(hist));
    hist.op = FS_OP_DELETE;
    hist.is_folder = is_folder;
    snprintf(hist.repo_root, sizeof(hist.repo_root), "%s", repo_root);
    snprintf(hist.old_section, sizeof(hist.old_section), "%s", sec);
    snprintf(hist.new_section, sizeof(hist.new_section), "%s", sec);
    snprintf(hist.old_slug, sizeof(hist.old_slug), "%s", slug);
    snprintf(hist.new_slug, sizeof(hist.new_slug), "%s", slug);
    snprintf(hist.old_path, sizeof(hist.old_path), "%s", src_pub);
    snprintf(hist.new_path, sizeof(hist.new_path), "%s", "");
    snprintf(hist.old_name, sizeof(hist.old_name), "%s", is_folder ? slug : fname);
    snprintf(hist.new_name, sizeof(hist.new_name), "%s", "");
    snprintf(hist.trash_fs, sizeof(hist.trash_fs), "%s", trash_item);
    snprintf(hist.label, sizeof(hist.label), "Delete %s", is_folder ? slug : fname);

    root = json_object();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "op", json_string_cstr("delete"));
    json_object_put(root, "kind", json_string_cstr(is_folder ? "folder" : "file"));
    json_object_put(root, "oldPath", json_string_cstr(src_pub));
    json_object_put(root, "deletedPath", json_string_cstr(src_pub));
    json_object_put(root, "section", json_string_cstr(sec));
    json_object_put(root, "oldSlug", json_string_cstr(slug));
    json_object_put(root, "oldName", json_string_cstr(is_folder ? slug : fname));

    ops_lock();
    push_history_locked(&hist);
    attach_history_locked(root);
    ops_unlock();

    if (out) *out = json_doc_new(root);
    else json_free(root);

cleanup:
    free(sec); free(slug); free(fname); free(src_fs); free(src_pub);
    return rc;
}

static char *public_to_fs(const char *repo_root, const char *pub_path) {
    if (!repo_root || !pub_path || pub_path[0] != '/') return NULL;
    return join_path2(repo_root, pub_path + 1);
}

int fs_ops_undo(const char *repo_root, JsonDoc **out, char *err, size_t err_sz) {
    FsHistoryEntry ent;
    char *old_fs = NULL, *new_fs = NULL, *primary_pub = NULL;
    JsonValue *root = NULL;
    int rc = 0;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) { set_err(err, err_sz, "Repository root is not configured"); return -1; }

    ops_lock();
    ensure_repo_history_locked(repo_root);
    if (g_hist_cursor <= 0) {
        ops_unlock();
        set_err(err, err_sz, "Nothing to undo in file tree");
        return 1;
    }
    ent = g_history[g_hist_cursor - 1];
    ops_unlock();

    old_fs = ent.old_path[0] ? public_to_fs(repo_root, ent.old_path) : NULL;
    new_fs = ent.new_path[0] ? public_to_fs(repo_root, ent.new_path) : NULL;

    if (ent.op == FS_OP_RENAME || ent.op == FS_OP_MOVE) {
        if (!old_fs || !new_fs || !any_exists(new_fs)) {
            set_err(err, err_sz, "Cannot undo: item was modified or moved externally");
            rc = 2; goto cleanup;
        }
        if (any_exists(old_fs)) {
            set_err(err, err_sz, "Cannot undo: original path already exists");
            rc = 3; goto cleanup;
        }
        if (move_fs_entry(new_fs, old_fs, ent.is_folder) != 0) {
            set_err(err, err_sz, "Could not undo move/rename");
            rc = -1; goto cleanup;
        }
        preview_draft_rename(repo_root, ent.new_path, ent.old_path, ent.is_folder);
        if (ent.is_folder) primary_pub = find_folder_primary(old_fs, ent.old_section, ent.old_slug);
    } else if (ent.op == FS_OP_DUPLICATE || ent.op == FS_OP_COPY) {
        char trash_root[PATH_MAX];
        if (!new_fs || !any_exists(new_fs)) {
            set_err(err, err_sz, "Cannot undo: copied item no longer exists");
            rc = 2; goto cleanup;
        }
        if (resolve_trash_root(repo_root, trash_root, sizeof(trash_root)) != 0) {
            set_err(err, err_sz, "Could not initialize trash for undo");
            rc = -1; goto cleanup;
        }
        if (!ent.trash_fs[0]) {
            snprintf(ent.trash_fs, sizeof(ent.trash_fs), "%s/undo-copy-%d", trash_root, ent.id);
        }
        if (any_exists(ent.trash_fs)) remove_recursive(ent.trash_fs, 0);
        if (move_fs_entry(new_fs, ent.trash_fs, ent.is_folder) != 0) {
            set_err(err, err_sz, "Could not undo copy/duplicate");
            rc = -1; goto cleanup;
        }
        if (ent.is_folder && old_fs && dir_exists(old_fs)) {
            primary_pub = find_folder_primary(old_fs, ent.old_section, ent.old_slug);
        }
    } else if (ent.op == FS_OP_DELETE) {
        if (!old_fs || !ent.trash_fs[0] || !any_exists(ent.trash_fs)) {
            set_err(err, err_sz, "Cannot undo delete: backup in trash is missing");
            rc = 2; goto cleanup;
        }
        if (any_exists(old_fs)) {
            set_err(err, err_sz, "Cannot restore deleted item: a file or folder already exists at that path");
            rc = 3; goto cleanup;
        }
        if (move_fs_entry(ent.trash_fs, old_fs, ent.is_folder) != 0) {
            set_err(err, err_sz, "Could not restore item from trash");
            rc = -1; goto cleanup;
        }
        if (ent.is_folder) primary_pub = find_folder_primary(old_fs, ent.old_section, ent.old_slug);
    }

    root = json_object();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "action", json_string_cstr("undo"));
    json_object_put(root, "undoneOp", json_string_cstr(op_kind_name(ent.op)));
    json_object_put(root, "kind", json_string_cstr(ent.is_folder ? "folder" : "file"));
    json_object_put(root, "label", json_string_cstr(ent.label));
    json_object_put(root, "section", json_string_cstr(ent.old_section));
    json_object_put(root, "oldSection", json_string_cstr(ent.new_section));
    json_object_put(root, "slug", json_string_cstr(ent.old_slug));
    json_object_put(root, "oldSlug", json_string_cstr(ent.new_slug));
    json_object_put(root, "newSlug", json_string_cstr(ent.old_slug));
    json_object_put(root, "oldPath", json_string_cstr(ent.new_path));
    json_object_put(root, "newPath", json_string_cstr(ent.old_path));
    json_object_put(root, "path", json_string_cstr(ent.old_path));
    json_object_put(root, "primaryFile", json_string_cstr(ent.is_folder ? (primary_pub ? primary_pub : "") : ent.old_path));

    ops_lock();
    if (g_hist_cursor > 0) {
        g_history[g_hist_cursor - 1] = ent;
        g_hist_cursor--;
    }
    attach_history_locked(root);
    ops_unlock();

    if (out) *out = json_doc_new(root);
    else json_free(root);

cleanup:
    free(old_fs);
    free(new_fs);
    free(primary_pub);
    return rc;
}

int fs_ops_redo(const char *repo_root, JsonDoc **out, char *err, size_t err_sz) {
    FsHistoryEntry ent;
    char *old_fs = NULL, *new_fs = NULL, *primary_pub = NULL;
    JsonValue *root = NULL;
    int rc = 0;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) { set_err(err, err_sz, "Repository root is not configured"); return -1; }

    ops_lock();
    ensure_repo_history_locked(repo_root);
    if (g_hist_cursor >= g_hist_count) {
        ops_unlock();
        set_err(err, err_sz, "Nothing to redo in file tree");
        return 1;
    }
    ent = g_history[g_hist_cursor];
    ops_unlock();

    old_fs = ent.old_path[0] ? public_to_fs(repo_root, ent.old_path) : NULL;
    new_fs = ent.new_path[0] ? public_to_fs(repo_root, ent.new_path) : NULL;

    if (ent.op == FS_OP_RENAME || ent.op == FS_OP_MOVE) {
        if (!old_fs || !new_fs || !any_exists(old_fs)) {
            set_err(err, err_sz, "Cannot redo: source item is missing");
            rc = 2; goto cleanup;
        }
        if (any_exists(new_fs)) {
            set_err(err, err_sz, "Cannot redo: destination path already exists");
            rc = 3; goto cleanup;
        }
        if (move_fs_entry(old_fs, new_fs, ent.is_folder) != 0) {
            set_err(err, err_sz, "Could not redo move/rename");
            rc = -1; goto cleanup;
        }
        preview_draft_rename(repo_root, ent.old_path, ent.new_path, ent.is_folder);
        if (ent.is_folder) primary_pub = find_folder_primary(new_fs, ent.new_section, ent.new_slug);
    } else if (ent.op == FS_OP_DUPLICATE || ent.op == FS_OP_COPY) {
        if (!new_fs) { set_err(err, err_sz, "Invalid redo path"); rc = -1; goto cleanup; }
        if (any_exists(new_fs)) {
            set_err(err, err_sz, "Cannot redo: destination path already exists");
            rc = 3; goto cleanup;
        }
        if (ent.trash_fs[0] && any_exists(ent.trash_fs)) {
            if (move_fs_entry(ent.trash_fs, new_fs, ent.is_folder) != 0) {
                set_err(err, err_sz, "Could not redo copy/duplicate");
                rc = -1; goto cleanup;
            }
        } else if (old_fs && any_exists(old_fs)) {
            int cp_rc = ent.is_folder ? copy_dir_recursive(old_fs, new_fs, 0) : copy_single_file(old_fs, new_fs);
            if (cp_rc != 0) {
                set_err(err, err_sz, "Could not redo copy/duplicate");
                rc = -1; goto cleanup;
            }
        } else {
            set_err(err, err_sz, "Cannot redo: source item is missing");
            rc = 2; goto cleanup;
        }
        if (ent.is_folder) primary_pub = find_folder_primary(new_fs, ent.new_section, ent.new_slug);
    } else if (ent.op == FS_OP_DELETE) {
        char trash_root[PATH_MAX];
        if (!old_fs || !any_exists(old_fs)) {
            set_err(err, err_sz, "Cannot redo delete: item is missing");
            rc = 2; goto cleanup;
        }
        if (!ent.trash_fs[0]) {
            if (resolve_trash_root(repo_root, trash_root, sizeof(trash_root)) != 0) {
                set_err(err, err_sz, "Could not initialize Studio trash");
                rc = -1; goto cleanup;
            }
            snprintf(ent.trash_fs, sizeof(ent.trash_fs), "%s/item-%d", trash_root, ent.id);
        }
        if (any_exists(ent.trash_fs)) remove_recursive(ent.trash_fs, 0);
        if (move_fs_entry(old_fs, ent.trash_fs, ent.is_folder) != 0) {
            set_err(err, err_sz, "Could not redo delete");
            rc = -1; goto cleanup;
        }
    }

    root = json_object();
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "action", json_string_cstr("redo"));
    json_object_put(root, "redoneOp", json_string_cstr(op_kind_name(ent.op)));
    json_object_put(root, "kind", json_string_cstr(ent.is_folder ? "folder" : "file"));
    json_object_put(root, "label", json_string_cstr(ent.label));
    json_object_put(root, "section", json_string_cstr(ent.new_section));
    json_object_put(root, "oldSection", json_string_cstr(ent.old_section));
    json_object_put(root, "slug", json_string_cstr(ent.new_slug));
    json_object_put(root, "oldSlug", json_string_cstr(ent.old_slug));
    json_object_put(root, "newSlug", json_string_cstr(ent.new_slug));
    json_object_put(root, "oldPath", json_string_cstr(ent.old_path));
    json_object_put(root, "newPath", json_string_cstr(ent.new_path));
    json_object_put(root, "path", json_string_cstr(ent.new_path));
    json_object_put(root, "primaryFile", json_string_cstr(ent.is_folder ? (primary_pub ? primary_pub : "") : ent.new_path));

    ops_lock();
    if (g_hist_cursor < g_hist_count) {
        g_history[g_hist_cursor] = ent;
        g_hist_cursor++;
    }
    attach_history_locked(root);
    ops_unlock();

    if (out) *out = json_doc_new(root);
    else json_free(root);

cleanup:
    free(old_fs);
    free(new_fs);
    free(primary_pub);
    return rc;
}

int fs_ops_history(const char *repo_root, JsonDoc **out, char *err, size_t err_sz) {
    JsonValue *root = json_object();
    if (out) *out = NULL;
    if (!root) { set_err(err, err_sz, "out of memory"); return -1; }
    json_object_put(root, "ok", json_bool(1));
    ops_lock();
    ensure_repo_history_locked(repo_root);
    attach_history_locked(root);
    ops_unlock();
    if (out) *out = json_doc_new(root);
    else json_free(root);
    return 0;
}

