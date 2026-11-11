#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "fs_rename.h"

#include "config.h"
#include "fs_ops.h"
#include "gallery.h"
#include "preview_draft.h"

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
#include <strings.h>
#include <unistd.h>
#endif

#include "compat.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *const RENAME_CONTENT_SECTIONS[] = {
    "sales", "articles", "awards", "projects", "video",
    "whatsnew", "parts", "contact", "newsletter"
};

static const char *const RENAME_ALLOWED_EXTS[] = {
    ".html", ".htm", ".shtml", ".php",
    ".txt", ".json", ".css", ".js", ".xml", ".csv", ".md",
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".svg", ".bmp", ".tif", ".tiff", ".ico", ".avif",
    ".pdf", ".mp4", ".m4v", ".webm", ".mov", ".mp3", ".wav", ".swf", ".rm", ".doc", ".docx", ".rtf"
};

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (!err || err_sz == 0) return;
    snprintf(err, err_sz, "%s", msg && msg[0] ? msg : "Rename failed");
}

static int file_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path) {
    struct stat st;
    return path && path[0] && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *canon_path(const char *path) {
#ifdef _WIN32
    char buf[4096];
    char *p;
    DWORD n;
    if (!path || !path[0]) return NULL;
    n = GetFullPathNameA(path, (DWORD)sizeof(buf), buf, NULL);
    if (n == 0 || n >= sizeof(buf)) return NULL;
    for (p = buf; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    return strdup(buf);
#else
    if (!path || !path[0]) return NULL;
    return realpath(path, NULL);
#endif
}

static int path_within(const char *root, const char *path) {
    char *root_real = canon_path(root);
    char *path_real = canon_path(path);
    size_t n;
    int ok = 0;

    if (root_real && path_real) {
        n = strlen(root_real);
        if (strncmp(path_real, root_real, n) == 0 && (path_real[n] == '\0' || path_real[n] == '/')) {
            ok = 1;
        }
    }
    free(root_real);
    free(path_real);
    return ok;
}

static int fs_inside_repo(const char *repo, const char *fs) {
    char *copy;
    char *slash;
    int ok;
    if (!repo || !fs) return 0;
    if (file_exists(fs) || dir_exists(fs)) {
        return path_within(repo, fs);
    }
    copy = strdup(fs);
    if (!copy) return 0;
    slash = strrchr(copy, '/');
    if (!slash || slash == copy) {
        free(copy);
        return 0;
    }
    *slash = '\0';
    ok = dir_exists(copy) && path_within(repo, copy);
    free(copy);
    return ok;
}

static char *join_path2(const char *a, const char *b) {
    size_t na;
    size_t nb;
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
    char *ab;
    char *abc;
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
    size_t total = 1 + ns + (nslug ? 1 + nslug : 0) + (nf ? 1 + nf : 0) + 1;
    char *out = (char *)malloc(total);
    size_t pos = 0;
    if (!out) return NULL;
    out[pos++] = '/';
    if (ns) {
        memcpy(out + pos, section, ns);
        pos += ns;
    }
    if (nslug) {
        out[pos++] = '/';
        memcpy(out + pos, slug, nslug);
        pos += nslug;
    }
    if (nf) {
        out[pos++] = '/';
        memcpy(out + pos, filename, nf);
        pos += nf;
    }
    out[pos] = '\0';
    return out;
}

static int is_content_section(const char *section) {
    size_t i;
    if (!section || !section[0]) return 0;
    for (i = 0; i < sizeof(RENAME_CONTENT_SECTIONS) / sizeof(RENAME_CONTENT_SECTIONS[0]); i++) {
        if (strcmp(section, RENAME_CONTENT_SECTIONS[i]) == 0) return 1;
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
    size_t n;
    size_t m;
    if (!text || !suffix) return 0;
    n = strlen(text);
    m = strlen(suffix);
    if (n < m) return 0;
    return strcasecmp(text + n - m, suffix) == 0;
}

static int starts_with_ci(const char *text, const char *prefix) {
    size_t n;
    if (!text || !prefix) return 0;
    n = strlen(prefix);
    return strncasecmp(text, prefix, n) == 0;
}

static int is_junk_filename(const char *name) {
    if (!name || !name[0] || name[0] == '.') return 1;
    if (strcasecmp(name, "Thumbs.db") == 0 || strcasecmp(name, "ehthumbs.db") == 0 ||
        strcasecmp(name, "desktop.ini") == 0 || strcasecmp(name, "lifeboat.zip") == 0 ||
        strcasecmp(name, "album.rss") == 0 || strcasecmp(name, "manifest.jmf") == 0) {
        return 1;
    }
    if (starts_with_ci(name, ".fuse_hidden") || starts_with_ci(name, "folderimg") ||
        starts_with_ci(name, "folderimage")) {
        return 1;
    }
    return 0;
}

static int has_disallowed_chars(const char *s) {
    const unsigned char *p;
    if (!s) return 1;
    for (p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7F || *p == '\\' || *p == '?' || *p == '#') {
            return 1;
        }
    }
    return 0;
}

static int valid_single_segment(const char *name) {
    size_t n;
    if (!name || !name[0]) return 0;
    n = strlen(name);
    if (n > 255) return 0;
    if (name[0] == '.' || name[n - 1] == '.' || name[n - 1] == ' ') return 0;
    if (strstr(name, "..") != NULL) return 0;
    if (strchr(name, '/') != NULL || strchr(name, '\\') != NULL) return 0;
    if (has_disallowed_chars(name)) return 0;
    return gallery_valid_slug(name);
}

static int valid_rename_filename(const char *name) {
    const char *dot;
    size_t i;
    if (!valid_single_segment(name)) return 0;
    if (is_junk_filename(name)) return 0;
    dot = strrchr(name, '.');
    if (!dot || dot == name || dot[1] == '\0') return 0;
    for (i = 0; i < sizeof(RENAME_ALLOWED_EXTS) / sizeof(RENAME_ALLOWED_EXTS[0]); i++) {
        if (ends_with_ci(name, RENAME_ALLOWED_EXTS[i])) return 1;
    }
    return 0;
}

static int valid_slug_path(const char *slug) {
    const char *p;
    if (!slug || !slug[0]) return 0;
    if (slug[0] == '/' || strstr(slug, "//") != NULL || strstr(slug, "..") != NULL) return 0;
    if (has_disallowed_chars(slug)) return 0;
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
    size_t len;
    char *out;
    size_t i = 0;
    size_t w = 0;
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
                if (byte == 0) {
                    free(out);
                    return NULL;
                }
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
    while (n > 0 && s[n - 1] == ' ') {
        s[--n] = '\0';
    }
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
    char *best_index = NULL;
    int best_index_rank = 1000;
    char *slug_match = NULL;
    char *first_page = NULL;
    char *first_asset = NULL;

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
        if (!name[0] || name[0] == '.' || is_junk_filename(name)) continue;
        if (!valid_rename_filename(name)) continue;
        child = join_path2(folder_fs, name);
        if (!child || !file_exists(child)) {
            free(child);
            continue;
        }
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
            if (!first_page && !strstr(name, ".mailchimp.")) {
                first_page = strdup(name);
            }
        } else if (!first_asset) {
            first_asset = strdup(name);
        }
    }
    closedir(dir);

    {
        const char *chosen = best_index ? best_index
                           : (slug_match ? slug_match
                           : (first_page ? first_page : first_asset));
        char *result = chosen ? public_join(section, slug, chosen) : strdup("");
        free(best_index);
        free(slug_match);
        free(first_page);
        free(first_asset);
        return result ? result : strdup("");
    }
}

/* Split a public path "/section/..." into section + rest.
 * Returns 0 on success, 1 on validation error. */
static int split_public_path(const char *raw_path, char **section_out, char **rest_out,
                             char *err, size_t err_sz) {
    char *decoded;
    const char *p;
    const char *slash;
    size_t sec_len;

    *section_out = NULL;
    *rest_out = NULL;

    if (!raw_path || raw_path[0] != '/') {
        set_err(err, err_sz, "Path must start with /");
        return 1;
    }
    if (has_disallowed_chars(raw_path) || strstr(raw_path, "..") != NULL || strstr(raw_path, "//") != NULL) {
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    if (strcmp(raw_path, CONFIG_PUBLIC_PATH) == 0 || strncmp(raw_path, "/__studio", 9) == 0) {
        set_err(err, err_sz, "Studio configuration files cannot be renamed");
        return 1;
    }
    decoded = url_decode_str(raw_path);
    if (!decoded) {
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    if (has_disallowed_chars(decoded) || strstr(decoded, "..") != NULL || strstr(decoded, "//") != NULL) {
        free(decoded);
        set_err(err, err_sz, "Path is not allowed");
        return 1;
    }
    p = decoded + 1;
    while (*p && p[strlen(p) - 1] == '/') {
        decoded[strlen(decoded) - 1] = '\0';
    }
    if (*p == '\0') {
        free(decoded);
        set_err(err, err_sz, "Top-level website sections cannot be renamed");
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
        free(*section_out);
        free(*rest_out);
        *section_out = NULL;
        *rest_out = NULL;
        set_err(err, err_sz, "Top-level website sections cannot be renamed");
        return 1;
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

static int perform_folder_rename(const char *repo_root, const char *section, const char *old_slug,
                                 const char *new_input, JsonDoc **out, char *err, size_t err_sz) {
    char *final_new_slug = NULL;
    char *old_fs = NULL;
    char *new_fs = NULL;
    char *old_public = NULL;
    char *new_public = NULL;
    char *primary_public = NULL;
    const char *old_slash;
    const char *new_slash;
    JsonValue *root = NULL;
    int rc = 0;

    if (!section || !section[0] || !old_slug || !old_slug[0]) {
        set_err(err, err_sz, "Top-level website sections cannot be renamed");
        return 1;
    }
    if (!is_content_section(section)) {
        set_err(err, err_sz, "Unknown or non-renamable section");
        return 1;
    }
    if (!valid_slug_path(old_slug)) {
        set_err(err, err_sz, "Folder path is not allowed");
        return 1;
    }
    if (!new_input || !new_input[0]) {
        set_err(err, err_sz, "New folder name is required");
        return 1;
    }
    if (has_disallowed_chars(new_input) || strstr(new_input, "..") != NULL) {
        set_err(err, err_sz, "Folder name is not allowed");
        return 1;
    }

    old_slash = strrchr(old_slug, '/');
    new_slash = strrchr(new_input, '/');

    if (!old_slash) {
        if (new_slash || !valid_single_segment(new_input)) {
            set_err(err, err_sz, "Folder name is not allowed");
            return 1;
        }
        if (is_reserved_dir_name(new_input)) {
            set_err(err, err_sz, "That folder name is reserved for gallery assets");
            return 1;
        }
        final_new_slug = strdup(new_input);
    } else {
        size_t prefix_len = (size_t)(old_slash - old_slug);
        if (!new_slash) {
            if (!valid_single_segment(new_input) || is_reserved_dir_name(new_input)) {
                set_err(err, err_sz, "Folder name is not allowed");
                return 1;
            }
            final_new_slug = (char *)malloc(prefix_len + 1 + strlen(new_input) + 1);
            if (final_new_slug) {
                memcpy(final_new_slug, old_slug, prefix_len);
                final_new_slug[prefix_len] = '/';
                memcpy(final_new_slug + prefix_len + 1, new_input, strlen(new_input) + 1);
            }
        } else {
            size_t new_prefix_len = (size_t)(new_slash - new_input);
            const char *leaf = new_slash + 1;
            if (new_prefix_len != prefix_len || strncmp(old_slug, new_input, prefix_len) != 0 ||
                !valid_single_segment(leaf) || is_reserved_dir_name(leaf)) {
                set_err(err, err_sz, "Folder name is not allowed");
                return 1;
            }
            final_new_slug = strdup(new_input);
        }
    }

    if (!final_new_slug) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    if (strcmp(old_slug, final_new_slug) == 0) {
        free(final_new_slug);
        set_err(err, err_sz, "New folder name must be different");
        return 1;
    }

    old_fs = join_path3(repo_root, section, old_slug);
    new_fs = join_path3(repo_root, section, final_new_slug);
    old_public = public_join(section, old_slug, NULL);
    new_public = public_join(section, final_new_slug, NULL);

    if (!old_fs || !new_fs || !old_public || !new_public) {
        set_err(err, err_sz, "out of memory");
        rc = -1;
        goto cleanup;
    }
    if (!dir_exists(old_fs)) {
        set_err(err, err_sz, "Folder not found");
        rc = 2;
        goto cleanup;
    }
    if (!fs_inside_repo(repo_root, old_fs) || !fs_inside_repo(repo_root, new_fs)) {
        set_err(err, err_sz, "Path is outside the repository");
        rc = 1;
        goto cleanup;
    }
    if (file_exists(new_fs) || dir_exists(new_fs)) {
        set_err(err, err_sz, "A folder or file with that name already exists");
        rc = 3;
        goto cleanup;
    }

    if (rename(old_fs, new_fs) != 0) {
        set_err(err, err_sz, "Could not rename folder");
        rc = -1;
        goto cleanup;
    }

    preview_draft_rename(repo_root, old_public, new_public, 1);
    primary_public = find_folder_primary(new_fs, section, final_new_slug);

    root = json_object();
    if (!root) {
        set_err(err, err_sz, "out of memory");
        rc = -1;
        goto cleanup;
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "kind", json_string_cstr("folder"));
    json_object_put(root, "oldPath", json_string_cstr(old_public));
    json_object_put(root, "newPath", json_string_cstr(new_public));
    json_object_put(root, "section", json_string_cstr(section));
    json_object_put(root, "oldSlug", json_string_cstr(old_slug));
    json_object_put(root, "newSlug", json_string_cstr(final_new_slug));
    json_object_put(root, "primaryFile", json_string_cstr(primary_public ? primary_public : ""));
    fs_ops_record_rename(repo_root, "folder", section, section, old_slug, final_new_slug,
                         old_public, new_public, old_slug, final_new_slug, root);

    if (out) {
        *out = json_doc_new(root);
        if (!*out) {
            json_free(root);
            set_err(err, err_sz, "out of memory");
            rc = -1;
        }
    } else {
        json_free(root);
    }

cleanup:
    free(final_new_slug);
    free(old_fs);
    free(new_fs);
    free(old_public);
    free(new_public);
    free(primary_public);
    return rc;
}

static int perform_file_rename(const char *repo_root, const char *section, const char *slug,
                               const char *old_filename, const char *new_filename,
                               JsonDoc **out, char *err, size_t err_sz) {
    char *old_fs = NULL;
    char *new_fs = NULL;
    char *old_public = NULL;
    char *new_public = NULL;
    JsonValue *root = NULL;
    int rc = 0;

    if (!section || !section[0] || !is_editable_section(section)) {
        set_err(err, err_sz, "Path must stay inside a content section");
        return 1;
    }
    if (slug && slug[0] && !valid_slug_path(slug)) {
        set_err(err, err_sz, "Folder path is not allowed");
        return 1;
    }
    if (!old_filename || !old_filename[0] || !valid_single_segment(old_filename)) {
        set_err(err, err_sz, "File path is not allowed");
        return 1;
    }
    if (!new_filename || !new_filename[0]) {
        set_err(err, err_sz, "New filename is required");
        return 1;
    }
    if (!valid_rename_filename(new_filename)) {
        if (strchr(new_filename, '/') || strchr(new_filename, '\\') || strstr(new_filename, "..") ||
            new_filename[0] == '.' || has_disallowed_chars(new_filename)) {
            set_err(err, err_sz, "Filename is not allowed");
        } else {
            set_err(err, err_sz, "Filename must include a valid file extension");
        }
        return 1;
    }
    if (strcmp(old_filename, new_filename) == 0) {
        set_err(err, err_sz, "New filename must be different");
        return 1;
    }

    old_fs = join_path3(repo_root, section, slug && slug[0] ? slug : "");
    if (old_fs) {
        char *tmp = join_path2(old_fs, old_filename);
        free(old_fs);
        old_fs = tmp;
    }
    new_fs = join_path3(repo_root, section, slug && slug[0] ? slug : "");
    if (new_fs) {
        char *tmp = join_path2(new_fs, new_filename);
        free(new_fs);
        new_fs = tmp;
    }
    old_public = public_join(section, slug, old_filename);
    new_public = public_join(section, slug, new_filename);

    if (!old_fs || !new_fs || !old_public || !new_public) {
        set_err(err, err_sz, "out of memory");
        rc = -1;
        goto cleanup;
    }
    if (!file_exists(old_fs)) {
        set_err(err, err_sz, "File not found");
        rc = 2;
        goto cleanup;
    }
    if (!fs_inside_repo(repo_root, old_fs) || !fs_inside_repo(repo_root, new_fs)) {
        set_err(err, err_sz, "Path is outside the repository");
        rc = 1;
        goto cleanup;
    }
    if (file_exists(new_fs) || dir_exists(new_fs)) {
        set_err(err, err_sz, "A file or folder with that name already exists");
        rc = 3;
        goto cleanup;
    }

    if (rename(old_fs, new_fs) != 0) {
        set_err(err, err_sz, "Could not rename file");
        rc = -1;
        goto cleanup;
    }

    preview_draft_rename(repo_root, old_public, new_public, 0);

    root = json_object();
    if (!root) {
        set_err(err, err_sz, "out of memory");
        rc = -1;
        goto cleanup;
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "kind", json_string_cstr("file"));
    json_object_put(root, "oldPath", json_string_cstr(old_public));
    json_object_put(root, "newPath", json_string_cstr(new_public));
    json_object_put(root, "path", json_string_cstr(new_public));
    json_object_put(root, "section", json_string_cstr(section));
    json_object_put(root, "oldSlug", json_string_cstr(slug ? slug : ""));
    json_object_put(root, "newSlug", json_string_cstr(slug ? slug : ""));
    json_object_put(root, "oldName", json_string_cstr(old_filename));
    json_object_put(root, "newName", json_string_cstr(new_filename));
    fs_ops_record_rename(repo_root, "file", section, section, slug ? slug : "", slug ? slug : "",
                         old_public, new_public, old_filename, new_filename, root);

    if (out) {
        *out = json_doc_new(root);
        if (!*out) {
            json_free(root);
            set_err(err, err_sz, "out of memory");
            rc = -1;
        }
    } else {
        json_free(root);
    }

cleanup:
    free(old_fs);
    free(new_fs);
    free(old_public);
    free(new_public);
    return rc;
}

int fs_rename_item(const char *repo_root, const JsonValue *body, JsonDoc **out,
                   char *err, size_t err_sz) {
    char *kind = NULL;
    char *raw_path = NULL;
    char *section = NULL;
    char *old_slug = NULL;
    char *new_name = NULL;
    char *new_slug = NULL;
    char *new_path = NULL;
    char *path_sec = NULL;
    char *path_rest = NULL;
    int is_folder = -1;
    int rc = 1;

    if (out) *out = NULL;
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return -1;
    }
    if (!body || json_type(body) != JSON_OBJECT) {
        set_err(err, err_sz, "JSON body must be an object");
        return 1;
    }

    kind = json_str_dup(body, "kind");
    raw_path = json_str_dup(body, "path");
    if (!raw_path[0]) {
        free(raw_path);
        raw_path = json_str_dup(body, "oldPath");
    }
    section = json_str_dup(body, "section");
    old_slug = json_str_dup(body, "oldSlug");
    if (!old_slug[0]) {
        free(old_slug);
        old_slug = json_str_dup(body, "slug");
    }
    new_name = json_str_dup(body, "newName");
    if (!new_name[0]) {
        free(new_name);
        new_name = json_str_dup(body, "name");
    }
    new_slug = json_str_dup(body, "newSlug");
    new_path = json_str_dup(body, "newPath");

    if (!has_disallowed_chars(new_name)) trim_spaces(new_name);
    if (!has_disallowed_chars(new_slug)) trim_spaces(new_slug);
    trim_spaces(kind);

    if (strcasecmp(kind, "section") == 0) {
        set_err(err, err_sz, "Top-level website sections cannot be renamed");
        rc = 1;
        goto done;
    }
    if (strcasecmp(kind, "folder") == 0 || strcasecmp(kind, "directory") == 0) {
        is_folder = 1;
    } else if (strcasecmp(kind, "file") == 0 || strcasecmp(kind, "page") == 0) {
        is_folder = 0;
    }

    if (raw_path[0]) {
        rc = split_public_path(raw_path, &path_sec, &path_rest, err, err_sz);
        if (rc != 0) goto done;
    }

    /* Extract new_name / new_slug from newPath if provided instead of newName/newSlug */
    if (!new_name[0] && !new_slug[0] && new_path[0]) {
        char *np_sec = NULL;
        char *np_rest = NULL;
        rc = split_public_path(new_path, &np_sec, &np_rest, err, err_sz);
        if (rc != 0) goto done;
        if ((path_sec && strcmp(path_sec, np_sec) != 0) || (section[0] && strcmp(section, np_sec) != 0)) {
            free(np_sec);
            free(np_rest);
            set_err(err, err_sz, "Cannot move items across sections");
            rc = 1;
            goto done;
        }
        if (is_folder == 1) {
            free(new_slug);
            new_slug = np_rest;
            np_rest = NULL;
        } else {
            const char *slash = strrchr(np_rest, '/');
            free(new_name);
            new_name = strdup(slash ? slash + 1 : np_rest);
            if (!new_slug[0]) {
                free(new_slug);
                new_slug = strdup(np_rest);
            }
        }
        free(np_sec);
        free(np_rest);
    }

    /* Infer whether this is a folder or file rename if kind was omitted */
    if (is_folder < 0) {
        if (path_sec && path_rest) {
            char *probe_fs = join_path3(repo_root, path_sec, path_rest);
            if (probe_fs && dir_exists(probe_fs)) {
                is_folder = 1;
            } else if (probe_fs && file_exists(probe_fs)) {
                is_folder = 0;
            } else {
                const char *last_seg = strrchr(path_rest, '/');
                last_seg = last_seg ? last_seg + 1 : path_rest;
                if (new_slug[0] || !strchr(last_seg, '.')) {
                    is_folder = 1;
                } else {
                    is_folder = 0;
                }
            }
            free(probe_fs);
        } else if (section[0] && (old_slug[0] || new_slug[0])) {
            is_folder = 1;
        } else {
            is_folder = 0;
        }
    }

    if (is_folder) {
        const char *sec = section[0] ? section : (path_sec ? path_sec : "");
        const char *oslug = old_slug[0] ? old_slug : (path_rest ? path_rest : "");
        const char *nslug = new_slug[0] ? new_slug : new_name;
        rc = perform_folder_rename(repo_root, sec, oslug, nslug, out, err, err_sz);
    } else {
        const char *sec = path_sec ? path_sec : section;
        char *fslug = NULL;
        char *old_fname = NULL;
        const char *target_new = new_name[0] ? new_name : new_slug;

        if (path_rest && path_rest[0]) {
            const char *slash = strrchr(path_rest, '/');
            if (slash) {
                fslug = strndup(path_rest, (size_t)(slash - path_rest));
                old_fname = strdup(slash + 1);
            } else {
                fslug = strdup("");
                old_fname = strdup(path_rest);
            }
        } else {
            char *old_name_param = json_str_dup(body, "oldName");
            if (!old_name_param[0]) {
                free(old_name_param);
                old_name_param = json_str_dup(body, "filename");
            }
            fslug = strdup(old_slug ? old_slug : "");
            old_fname = old_name_param;
        }

        if (!sec || !sec[0] || !old_fname || !old_fname[0]) {
            free(fslug);
            free(old_fname);
            set_err(err, err_sz, "path and newName are required");
            rc = 1;
            goto done;
        }
        rc = perform_file_rename(repo_root, sec, fslug ? fslug : "", old_fname, target_new, out, err, err_sz);
        free(fslug);
        free(old_fname);
    }

done:
    free(kind);
    free(raw_path);
    free(section);
    free(old_slug);
    free(new_name);
    free(new_slug);
    free(new_path);
    free(path_sec);
    free(path_rest);
    return rc;
}
