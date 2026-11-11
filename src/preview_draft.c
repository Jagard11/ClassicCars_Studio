#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "preview_draft.h"

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
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct Draft {
    char *path;
    char *content;
    char *images_json;
    long long revision;
    int live;
    struct Draft *next;
} Draft;

static Draft *g_drafts = NULL;
static char g_root[PATH_MAX];
static int g_loaded = 0;
static long long g_floor = 0;

static void draft_free(Draft *draft) {
    if (!draft) return;
    free(draft->path);
    free(draft->content);
    free(draft->images_json);
    free(draft);
}

static void drafts_reset(void) {
    Draft *draft = g_drafts;
    while (draft) {
        Draft *next = draft->next;
        draft_free(draft);
        draft = next;
    }
    g_drafts = NULL;
    g_root[0] = '\0';
    g_loaded = 0;
    g_floor = 0;
}

static const char *draft_base(const char *repo_root) {
    const char *home = settings_get_studio_home();
    if (home && home[0]) return home;
    return repo_root;
}

static int draft_file(const char *repo_root, char *dst, size_t dst_sz) {
    const char *base = draft_base(repo_root);
    if (!base || !base[0]) return -1;
    return settings_install_path(base, "state/draft.json", dst, dst_sz);
}

static int write_atomic(const char *final_path, const char *tmp_path, const char *text) {
    FILE *file;
    size_t n;
    size_t wrote;
    if (!final_path || !tmp_path || !text) return -1;
    file = fopen(tmp_path, "wb");
    if (!file) return -1;
    n = strlen(text);
    wrote = n ? fwrite(text, 1, n, file) : 0;
    if (wrote != n || fflush(file) != 0) {
        fclose(file);
        remove(tmp_path);
        return -1;
    }
#ifndef _WIN32
    if (fsync(fileno(file)) != 0) {
        fclose(file);
        remove(tmp_path);
        return -1;
    }
#endif
    if (fclose(file) != 0) {
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

static int normalize_path(const char *path, char *dst, size_t dst_sz) {
    size_t n;
    size_t i;
    if (!path || !dst || dst_sz < 2) return -1;
    n = strlen(path);
    if (n < 2 || n >= dst_sz || path[0] != '/') return -1;
    if (strstr(path, "..") || strstr(path, "//")) return -1;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)path[i];
        if (c < 0x20 || c == 0x7f || c == '\\' || c == '?' || c == '#') return -1;
    }
    memcpy(dst, path, n + 1);
    return 0;
}

static Draft *find_draft(const char *path) {
    Draft *draft;
    for (draft = g_drafts; draft; draft = draft->next) {
        if (strcmp(draft->path, path) == 0) return draft;
    }
    return NULL;
}

static char *stringify_images(const JsonValue *images) {
    JsonValue *clone;
    JsonDoc *doc;
    char *text;
    if (!images || json_type(images) != JSON_ARRAY) return NULL;
    clone = json_clone(images);
    if (!clone) return NULL;
    doc = json_doc_new(clone);
    if (!doc) {
        json_free(clone);
        return NULL;
    }
    text = json_stringify(doc, NULL);
    json_doc_free(doc);
    return text;
}

static int persist_drafts(const char *repo_root) {
    JsonValue *root;
    JsonValue *drafts;
    JsonDoc *doc;
    Draft *draft;
    char *text;
    char path[PATH_MAX];
    char tmp[PATH_MAX];
    int rc;
    root = json_object();
    drafts = json_object();
    if (!root || !drafts) {
        json_free(root);
        json_free(drafts);
        return -1;
    }
    if (json_object_put(root, "drafts", drafts) != 0) {
        json_free(root);
        return -1;
    }
    for (draft = g_drafts; draft; draft = draft->next) {
        JsonValue *item;
        JsonValue *images;
        if (!draft->live) continue;
        item = json_object();
        if (!item) {
            json_free(root);
            return -1;
        }
        if (draft->content) {
            JsonValue *text = json_string_cstr(draft->content);
            if (!text) {
                json_free(item);
                json_free(root);
                return -1;
            }
            if (json_object_put(item, "content", text) != 0) {
                json_free(item);
                json_free(root);
                return -1;
            }
        }
        if (draft->images_json) {
            JsonDoc *parsed = json_parse(draft->images_json, strlen(draft->images_json), NULL, 0);
            images = (parsed && json_type(parsed->root) == JSON_ARRAY) ? json_clone(parsed->root) : json_array();
            json_doc_free(parsed);
            if (!images || json_object_put(item, "images", images) != 0) {
                json_free(item);
                json_free(root);
                return -1;
            }
        }
        if (json_object_put(drafts, draft->path, item) != 0) {
            json_free(root);
            return -1;
        }
    }
    doc = json_doc_new(root);
    if (!doc) return -1;
    text = json_stringify(doc, NULL);
    json_doc_free(doc);
    if (!text) return -1;
    if (settings_ensure_dirs(draft_base(repo_root)) != 0 || draft_file(repo_root, path, sizeof path) != 0) {
        free(text);
        return -1;
    }
    if (strlen(path) + 5 >= sizeof tmp) {
        free(text);
        return -1;
    }
    memcpy(tmp, path, strlen(path));
    memcpy(tmp + strlen(path), ".tmp", 5);
    rc = write_atomic(path, tmp, text);
    free(text);
    return rc;
}

static int load_drafts(const char *repo_root) {
    char path[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    JsonDoc *doc;
    JsonValue *drafts;
    size_t i;
    size_t n;
    drafts_reset();
    if (!repo_root || !repo_root[0] || strlen(repo_root) >= sizeof g_root) return -1;
    memcpy(g_root, repo_root, strlen(repo_root) + 1);
    g_loaded = 1;
    if (draft_file(repo_root, path, sizeof path) != 0) return 0;
    if (http_read_file(path, &data, &len, 32u * 1024u * 1024u) != 0) return 0;
    doc = json_parse(data, len, NULL, 0);
    free(data);
    if (!doc) return 0;
    drafts = json_object_get(doc->root, "drafts");
    if (drafts && json_type(drafts) == JSON_OBJECT) {
        n = json_object_len(drafts);
        for (i = 0; i < n; i++) {
            const char *key = json_object_key(drafts, i);
            JsonValue *item = json_object_val(drafts, i);
            JsonValue *content;
            JsonValue *images;
            Draft *draft;
            char normal[1200];
            if (!key || !item || json_type(item) != JSON_OBJECT) continue;
            if (normalize_path(key, normal, sizeof normal) != 0) continue;
            draft = calloc(1, sizeof *draft);
            if (!draft) continue;
            draft->path = strdup(normal);
            draft->live = 1;
            content = json_object_get(item, "content");
            if (content && json_type(content) == JSON_STRING) {
                draft->content = json_as_text(content);
            }
            images = json_object_get(item, "images");
            if (images && json_type(images) == JSON_ARRAY) {
                draft->images_json = stringify_images(images);
            }
            if (!draft->path) {
                draft_free(draft);
                continue;
            }
            draft->next = g_drafts;
            g_drafts = draft;
        }
    }
    json_doc_free(doc);
    return 0;
}

static int ensure(const char *repo_root) {
    if (!repo_root || !repo_root[0]) return -1;
    if (g_loaded && strcmp(g_root, repo_root) == 0) return 0;
    return load_drafts(repo_root);
}

static Draft *ensure_slot(const char *path) {
    Draft *draft = find_draft(path);
    if (draft) return draft;
    draft = calloc(1, sizeof *draft);
    if (!draft) return NULL;
    draft->path = strdup(path);
    if (!draft->path) {
        free(draft);
        return NULL;
    }
    draft->next = g_drafts;
    g_drafts = draft;
    return draft;
}

int preview_draft_stage(const char *repo_root, const char *path,
                        const char *content, int has_content,
                        const JsonValue *images, int has_images,
                        long long revision) {
    char normal[1200];
    Draft *draft;
    char *next_content = NULL;
    char *next_images = NULL;
    if (ensure(repo_root) != 0) return -1;
    if (normalize_path(path, normal, sizeof normal) != 0) return -1;
    if (revision < g_floor) return 0;
    draft = find_draft(normal);
    if (draft && revision < draft->revision) return 0;
    if (has_content) {
        next_content = strdup(content ? content : "");
        if (!next_content) return -1;
    }
    if (has_images) {
        next_images = stringify_images(images);
        if (!next_images) {
            free(next_content);
            return -1;
        }
    }
    if (!draft) {
        draft = ensure_slot(normal);
        if (!draft) {
            free(next_content);
            free(next_images);
            return -1;
        }
    }
    if (has_content) {
        free(draft->content);
        draft->content = next_content;
    }
    if (has_images) {
        free(draft->images_json);
        draft->images_json = next_images;
    }
    draft->revision = revision;
    draft->live = 1;
    return persist_drafts(repo_root);
}

int preview_draft_clear(const char *repo_root, const char *path, long long revision) {
    char normal[1200];
    if (ensure(repo_root) != 0) return -1;
    if (revision < g_floor) return 0;
    if (!path || !path[0]) {
        Draft *draft;
        g_floor = revision;
        for (draft = g_drafts; draft; draft = draft->next) {
            if (revision < draft->revision) continue;
            free(draft->content);
            free(draft->images_json);
            draft->content = NULL;
            draft->images_json = NULL;
            draft->live = 0;
            draft->revision = revision;
        }
        return persist_drafts(repo_root);
    }
    if (normalize_path(path, normal, sizeof normal) != 0) return -1;
    {
        Draft *draft = find_draft(normal);
        if (!draft) {
            draft = ensure_slot(normal);
            if (!draft) return -1;
        }
        if (revision < draft->revision) return 0;
        free(draft->content);
        free(draft->images_json);
        draft->content = NULL;
        draft->images_json = NULL;
        draft->live = 0;
        draft->revision = revision;
    }
    return persist_drafts(repo_root);
}

char *preview_draft_content(const char *repo_root, const char *path) {
    char normal[1200];
    Draft *draft;
    if (ensure(repo_root) != 0) return NULL;
    if (normalize_path(path, normal, sizeof normal) != 0) return NULL;
    draft = find_draft(normal);
    if (!draft || !draft->live || !draft->content) return NULL;
    return strdup(draft->content);
}

int preview_draft_rename(const char *repo_root, const char *old_path, const char *new_path, int is_prefix) {
    char old_norm[1200];
    char new_norm[1200];
    size_t old_len;
    size_t new_len;
    Draft *draft;
    int changed = 0;

    if (ensure(repo_root) != 0) return -1;
    if (normalize_path(old_path, old_norm, sizeof old_norm) != 0) return -1;
    if (normalize_path(new_path, new_norm, sizeof new_norm) != 0) return -1;
    old_len = strlen(old_norm);
    new_len = strlen(new_norm);

    for (draft = g_drafts; draft; draft = draft->next) {
        if (!draft->path) continue;
        if (strcmp(draft->path, old_norm) == 0) {
            char * updated = strdup(new_norm);
            if (!updated) return -1;
            free(draft->path);
            draft->path = updated;
            changed = 1;
        } else if (is_prefix && strncmp(draft->path, old_norm, old_len) == 0 && draft->path[old_len] == '/') {
            const char *rest = draft->path + old_len;
            size_t rest_len = strlen(rest);
            char *updated = (char *)malloc(new_len + rest_len + 1);
            if (!updated) return -1;
            memcpy(updated, new_norm, new_len);
            memcpy(updated + new_len, rest, rest_len + 1);
            free(draft->path);
            draft->path = updated;
            changed = 1;
        }
    }

    if (changed) {
        return persist_drafts(repo_root);
    }
    return 0;
}
