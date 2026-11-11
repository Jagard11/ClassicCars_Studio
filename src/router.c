#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "router.h"

#include "preview_draft.h"
#include "proxy.h"
#include "settings.h"
#include "ssi.h"

#if defined(__has_include)
#  if __has_include("api.h")
#    include "api.h"
#  endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

/* Declared in api.h once Workstream 2 lands. */
int api_handle_request(const HttpRequest *req, HttpResponse *res, const char *repo_root);

static int memcasecmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = a;
    const unsigned char *y = b;
    size_t i;
    for (i = 0; i < n; i++) {
        unsigned char ca = x[i];
        unsigned char cb = y[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
    }
    return 0;
}

static int str_ieq(const char *a, const char *b) {
    size_t na;
    size_t nb;
    if (!a || !b) return 0;
    na = strlen(a);
    nb = strlen(b);
    return na == nb && memcasecmp(a, b, na) == 0;
}

static int ends_with_ci(const char *s, const char *ext) {
    size_t n;
    size_t m;
    if (!s || !ext) return 0;
    n = strlen(s);
    m = strlen(ext);
    if (n < m) return 0;
    return memcasecmp(s + (n - m), ext, m) == 0;
}

static void copy_trunc(char *dst, size_t dst_sz, const char *src) {
    size_t n;
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static int copy_fit(char *dst, size_t dst_sz, const char *src) {
    size_t n;
    if (!dst || dst_sz == 0) return -1;
    if (!src) src = "";
    n = strlen(src);
    if (n >= dst_sz) return -1;
    memcpy(dst, src, n + 1);
    return 0;
}

static int set_plain(HttpResponse *res, int status, const char *msg) {
    res->status = status;
    copy_trunc(res->content_type, sizeof res->content_type, "text/plain; charset=UTF-8");
    if (!msg) msg = "";
    http_response_set_body(res, msg, strlen(msg));
    return 0;
}

static int set_redirect(HttpResponse *res, int status, const char *location, const char *query) {
    char loc[1024];
    size_t ll;
    size_t ql;
    if (!location) location = "/";
    ll = strlen(location);
    ql = (query && query[0]) ? strlen(query) : 0;
    if (ql) {
        if (ll + 1 + ql >= sizeof loc) return set_plain(res, 414, "URI Too Long\n");
        memcpy(loc, location, ll);
        loc[ll] = '?';
        memcpy(loc + ll + 1, query, ql + 1);
    } else if (copy_fit(loc, sizeof loc, location) != 0) {
        return set_plain(res, 414, "URI Too Long\n");
    }
    res->status = status;
    copy_trunc(res->location, sizeof res->location, loc);
    copy_trunc(res->content_type, sizeof res->content_type, "text/plain; charset=UTF-8");
    http_response_set_body(res, "Redirect\n", 9);
    return 0;
}

static void add_cache(HttpResponse *res) {
    const char *line = "Cache-Control: no-cache\r\n";
    size_t have = strlen(res->extra_headers);
    size_t n = strlen(line);
    if (strstr(res->extra_headers, "Cache-Control:")) return;
    if (have + n >= sizeof res->extra_headers) return;
    memcpy(res->extra_headers + have, line, n + 1);
}

static int has_ctl(const char *s) {
    const unsigned char *p;
    for (p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) return 1;
    }
    return 0;
}

static int blocked_prefix(const char *path, const char *pfx) {
    size_t n = strlen(pfx);
    if (strncmp(path, pfx, n) != 0) return 0;
    return path[n] == '\0' || path[n] == '/';
}

static int forbid_status(const char *path) {
    if (!path || path[0] != '/' || has_ctl(path)) return 400;
    if (strstr(path, "..") || strchr(path, '\\') || strstr(path, ".htaccess")) return 403;
    if (blocked_prefix(path, "/data")) return 403;
    if (blocked_prefix(path, "/admin")) return 403;
    if (blocked_prefix(path, "/studio/state")) return 403;
    if (blocked_prefix(path, "/studio/src")) return 403;
    if (blocked_prefix(path, "/studio/build")) return 403;
    return 0;
}

static int method_is(const HttpRequest *req, const char *name) {
    return str_ieq(req->method, name);
}

static int allows_read(const HttpRequest *req) {
    return method_is(req, "GET") || method_is(req, "HEAD");
}

static int allows_php(const HttpRequest *req) {
    return allows_read(req) || method_is(req, "POST");
}

static int is_html_ct(const char *ct) {
    const char *p = "text/html";
    if (!ct) return 0;
    while (*ct == ' ' || *ct == '\t') ct++;
    for (; *p; p++, ct++) {
        char c = *ct;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != *p) return 0;
    }
    return *ct == '\0' || *ct == ';' || *ct == ' ' || *ct == '\t';
}

static int is_php_path(const char *path) { return ends_with_ci(path, ".php"); }

static int is_html_path(const char *path) {
    return ends_with_ci(path, ".html") || ends_with_ci(path, ".htm") || ends_with_ci(path, ".shtml");
}

static const char *mime_type(const char *path) {
    if (ends_with_ci(path, ".jpg") || ends_with_ci(path, ".jpeg")) return "image/jpeg";
    if (ends_with_ci(path, ".png")) return "image/png";
    if (ends_with_ci(path, ".gif")) return "image/gif";
    if (ends_with_ci(path, ".webp")) return "image/webp";
    if (ends_with_ci(path, ".svg")) return "image/svg+xml";
    if (ends_with_ci(path, ".ico")) return "image/x-icon";
    if (ends_with_ci(path, ".css")) return "text/css; charset=UTF-8";
    if (ends_with_ci(path, ".js")) return "text/javascript; charset=UTF-8";
    if (ends_with_ci(path, ".woff")) return "font/woff";
    if (ends_with_ci(path, ".woff2")) return "font/woff2";
    if (ends_with_ci(path, ".ttf")) return "font/ttf";
    if (ends_with_ci(path, ".json")) return "application/json";
    if (ends_with_ci(path, ".xml")) return "application/xml";
    if (ends_with_ci(path, ".txt")) return "text/plain; charset=UTF-8";
    if (ends_with_ci(path, ".pdf")) return "application/pdf";
    if (ends_with_ci(path, ".mp4") || ends_with_ci(path, ".m4v")) return "video/mp4";
    if (ends_with_ci(path, ".webm")) return "video/webm";
    if (ends_with_ci(path, ".mov")) return "video/quicktime";
    if (ends_with_ci(path, ".mp3")) return "audio/mpeg";
    if (ends_with_ci(path, ".wav")) return "audio/wav";
    if (ends_with_ci(path, ".doc")) return "application/msword";
    if (ends_with_ci(path, ".docx")) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (is_html_path(path)) return "text/html; charset=UTF-8";
    return "application/octet-stream";
}

static int path_join_url(char *dst, size_t dst_sz, const char *root, const char *url) {
    size_t nr;
    size_t nu;
    if (!dst || !root || !url || url[0] != '/') return -1;
    nr = strlen(root);
    while (nr > 1 && (root[nr - 1] == '/' || root[nr - 1] == '\\')) nr--;
    nu = strlen(url);
    if (strcmp(url, "/") == 0) {
        if (nr + 1 > dst_sz) return -1;
        memcpy(dst, root, nr);
        dst[nr] = '\0';
        return 0;
    }
    if (nr + nu + 1 > dst_sz) return -1;
    memcpy(dst, root, nr);
    memcpy(dst + nr, url, nu + 1);
    return 0;
}

static int file_inside(const char *root, const char *fs) {
    char rr[PATH_MAX];
    char fr[PATH_MAX];
    size_t n;
#ifdef _WIN32
    size_t i;
    if (!_fullpath(rr, root, PATH_MAX) || !_fullpath(fr, fs, PATH_MAX)) return 0;
    for (i = 0; rr[i]; i++) {
        if (rr[i] == '\\') rr[i] = '/';
        if (rr[i] >= 'A' && rr[i] <= 'Z') rr[i] = (char)(rr[i] - 'A' + 'a');
    }
    for (i = 0; fr[i]; i++) {
        if (fr[i] == '\\') fr[i] = '/';
        if (fr[i] >= 'A' && fr[i] <= 'Z') fr[i] = (char)(fr[i] - 'A' + 'a');
    }
#else
    if (!realpath(root, rr) || !realpath(fs, fr)) return 0;
#endif
    n = strlen(rr);
    if (n > 1 && rr[n - 1] == '/') rr[--n] = '\0';
    if (strncmp(fr, rr, n) != 0) return 0;
    if (n == 1 && rr[0] == '/') return 1;
    return fr[n] == '\0' || fr[n] == '/';
}

static int path_kind(const char *fs) {
    struct stat st;
    if (stat(fs, &st) != 0) return 0;
    if (S_ISREG(st.st_mode)) return 1;
    if (S_ISDIR(st.st_mode)) return 2;
    return 0;
}

static int request_raw_path(const HttpRequest *req, char *dst, size_t dst_sz) {
    const char *uri = req->raw_uri[0] ? req->raw_uri : req->path;
    const char *q = strchr(uri, '?');
    size_t n = q ? (size_t)(q - uri) : strlen(uri);
    if (n == 0 || n >= dst_sz) return -1;
    memcpy(dst, uri, n);
    dst[n] = '\0';
    return 0;
}

static int is_api_path(const char *path) {
    if (strcmp(path, "/api") == 0 || strncmp(path, "/api/", 5) == 0) return 1;
    if (strcmp(path, "/__studio/api") == 0 || strncmp(path, "/__studio/api/", 14) == 0) return 1;
    return 0;
}

static int is_landing_alias(const char *path) {
    return strcmp(path, "/index.html") == 0 ||
           strcmp(path, "/index.htm") == 0 ||
           strcmp(path, "/index.shtml") == 0 ||
           strcmp(path, "/index.php") == 0 ||
           strcmp(path, "/sales") == 0 ||
           strcmp(path, "/sales/") == 0 ||
           strcmp(path, "/sales/index.html") == 0 ||
           strcmp(path, "/sales/index.htm") == 0 ||
           strcmp(path, "/sales/index.shtml") == 0;
}

static int is_indexed_section(const char *path) {
    static const char *bases[] = {"/articles", "/awards", "/projects", NULL};
    size_t i;
    for (i = 0; bases[i]; i++) {
        size_t n = strlen(bases[i]);
        if (strncmp(path, bases[i], n) == 0 && (path[n] == '\0' || path[n] == '/')) return 1;
    }
    return 0;
}

static int sales_vehicle(const char *path, char *slug, size_t slug_sz, int *trailing) {
    const char *rest;
    const char *slash;
    size_t n;
    if (strncmp(path, "/sales/", 7) != 0) return 0;
    rest = path + 7;
    if (rest[0] == '\0') return 0;
    slash = strchr(rest, '/');
    if (!slash) {
        n = strlen(rest);
        if (n == 0 || n >= slug_sz) return 0;
        memcpy(slug, rest, n + 1);
        *trailing = 0;
        return 1;
    }
    if (slash[1] != '\0') return 0;
    n = (size_t)(slash - rest);
    if (n == 0 || n >= slug_sz) return 0;
    memcpy(slug, rest, n);
    slug[n] = '\0';
    *trailing = 1;
    return 1;
}

static int serve_php(const HttpRequest *req, HttpResponse *res, const char *repo_root,
                     const char *raw_override, const char *top_level) {
    HttpRequest tmp = *req;
    char *out = NULL;
    size_t out_len = 0;
    if (raw_override && raw_override[0]) {
        if (copy_fit(tmp.raw_uri, sizeof tmp.raw_uri, raw_override) != 0) {
            return set_plain(res, 414, "URI Too Long\n");
        }
    }
    if (!top_level || !top_level[0]) top_level = req->path;
    if (proxy_to_php(&tmp, res, NULL) != 0) return -1;
    if (is_html_ct(res->content_type) && res->body && res->body_len > 0) {
        if (ssi_splice(res->body, res->body_len, repo_root, top_level, &out, &out_len) == 0) {
            http_response_set_body(res, out, out_len);
            free(out);
        } else {
            free(out);
        }
    }
    add_cache(res);
    return 0;
}

static int serve_html_bytes(HttpResponse *res, const char *data, size_t len, const char *repo_root,
                            const char *top_level) {
    char *out = NULL;
    size_t out_len = 0;
    if (ssi_splice(data, len, repo_root, top_level, &out, &out_len) != 0) {
        free(out);
        return set_plain(res, 500, "SSI processing failed\n");
    }
    res->status = 200;
    copy_trunc(res->content_type, sizeof res->content_type, "text/html; charset=UTF-8");
    add_cache(res);
    http_response_set_body(res, out ? out : "", out ? out_len : 0);
    free(out);
    return 0;
}

static int serve_html_file(HttpResponse *res, const char *fs, const char *repo_root,
                           const char *top_level) {
    char *data = NULL;
    size_t len = 0;
    int rc;
    if (http_read_file(fs, &data, &len, 16u * 1024u * 1024u) != 0) {
        return set_plain(res, 500, "Failed to read file\n");
    }
    rc = serve_html_bytes(res, data, len, repo_root, top_level);
    free(data);
    return rc;
}

static int serve_raw_file(HttpResponse *res, const char *fs, const char *url_path) {
    char *data = NULL;
    size_t len = 0;
    if (http_read_file(fs, &data, &len, 64u * 1024u * 1024u) != 0) {
        return set_plain(res, 500, "Failed to read file\n");
    }
    res->status = 200;
    copy_trunc(res->content_type, sizeof res->content_type, mime_type(url_path));
    add_cache(res);
    http_response_set_body(res, data, len);
    free(data);
    return 0;
}

static int join_url_name(char *dst, size_t dst_sz, const char *dir_url, const char *name) {
    size_t n = strlen(dir_url);
    size_t m = strlen(name);
    int slash = n > 0 && dir_url[n - 1] == '/';
    size_t o;
    if (n + (slash ? 0 : 1) + m >= dst_sz) return -1;
    memcpy(dst, dir_url, n);
    o = n;
    if (!slash) dst[o++] = '/';
    memcpy(dst + o, name, m + 1);
    return 0;
}

static int build_proxied_uri(char *dst, size_t dst_sz, const char *raw_dir,
                             const char *name, const char *query) {
    size_t n = strlen(raw_dir);
    size_t m = strlen(name);
    size_t ql = (query && query[0]) ? strlen(query) : 0;
    int slash = n > 0 && raw_dir[n - 1] == '/';
    size_t o;
    size_t extra = (slash ? 0u : 1u) + m + (ql ? ql + 1 : 0);
    if (n > dst_sz || extra >= dst_sz - n) return -1;
    memcpy(dst, raw_dir, n);
    o = n;
    if (!slash) dst[o++] = '/';
    memcpy(dst + o, name, m);
    o += m;
    if (ql) {
        dst[o++] = '?';
        memcpy(dst + o, query, ql);
        o += ql;
    }
    dst[o] = '\0';
    return 0;
}

static int serve_not_found(HttpResponse *res, const char *repo_root) {
    char fs[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    char *out = NULL;
    size_t out_len = 0;
    if (path_join_url(fs, sizeof fs, repo_root, "/404.shtml") == 0 &&
        path_kind(fs) == 1 && file_inside(repo_root, fs) &&
        http_read_file(fs, &data, &len, 16u * 1024u * 1024u) == 0) {
        if (ssi_splice(data, len, repo_root, "/404.shtml", &out, &out_len) == 0) {
            res->status = 404;
            copy_trunc(res->content_type, sizeof res->content_type, "text/html; charset=UTF-8");
            add_cache(res);
            http_response_set_body(res, out ? out : "", out ? out_len : 0);
            free(data);
            free(out);
            return 0;
        }
        free(out);
    }
    free(data);
    return set_plain(res, 404, "Not Found\n");
}

static int serve_disk(const HttpRequest *req, HttpResponse *res, const char *repo_root,
                      const char *url_path, const char *fs, const char *raw_override,
                      const char *top_level) {
    if (!file_inside(repo_root, fs)) return set_plain(res, 403, "Forbidden\n");
    if (is_php_path(url_path)) {
        if (!allows_php(req)) return set_plain(res, 405, "Method Not Allowed\n");
        return serve_php(req, res, repo_root, raw_override, top_level ? top_level : url_path);
    }
    if (!allows_read(req)) return set_plain(res, 405, "Method Not Allowed\n");
    if (is_html_path(url_path)) {
        char *draft = preview_draft_content(repo_root, url_path);
        if (draft) {
            int rc = serve_html_bytes(res, draft, strlen(draft), repo_root, top_level ? top_level : url_path);
            free(draft);
            return rc;
        }
        return serve_html_file(res, fs, repo_root, top_level ? top_level : url_path);
    }
    return serve_raw_file(res, fs, url_path);
}

static int resolve_directory_index(const HttpRequest *req, HttpResponse *res,
                                   const char *repo_root) {
    static const char *names[] = {"index.php", "index.html", "index.shtml", "index.htm", NULL};
    char raw[2048];
    size_t i;
    if (request_raw_path(req, raw, sizeof raw) != 0) {
        return set_plain(res, 414, "URI Too Long\n");
    }
    for (i = 0; names[i]; i++) {
        char url[1024];
        char fs[PATH_MAX];
        char proxied[2048];
        if (join_url_name(url, sizeof url, req->path, names[i]) != 0) continue;
        if (path_join_url(fs, sizeof fs, repo_root, url) != 0) continue;
        if (path_kind(fs) != 1) continue;
        if (is_php_path(url)) {
            if (build_proxied_uri(proxied, sizeof proxied, raw, names[i], req->query) != 0) {
                return set_plain(res, 414, "URI Too Long\n");
            }
            return serve_disk(req, res, repo_root, url, fs, proxied, url);
        }
        return serve_disk(req, res, repo_root, url, fs, NULL, url);
    }
    return serve_not_found(res, repo_root);
}

/* UI assets stay in the studio installation when a different website is open. */
static const char *studio_install_root(const char *repo_root) {
    const char *home = settings_get_studio_home();
    if (home && home[0]) return home;
    return repo_root;
}

static int serve_studio(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    const char *rel = req->path + strlen("/__studio");
    const char *install = studio_install_root(repo_root);
    char web[PATH_MAX];
    char fs[PATH_MAX];
    char index_url[1024];
    int kind;
    if (rel[0] == '\0') rel = "/";
    if (settings_install_path(install, "web", web, sizeof web) != 0 || path_kind(web) != 2) {
        return set_plain(res, 500, "Studio web root is unavailable\n");
    }
    if (strcmp(rel, "/") == 0) {
        copy_trunc(index_url, sizeof index_url, "/index.html");
    } else if (copy_fit(index_url, sizeof index_url, rel) != 0) {
        return set_plain(res, 414, "URI Too Long\n");
    }
    if (path_join_url(fs, sizeof fs, web, index_url[0] ? index_url : "/") != 0) {
        return set_plain(res, 414, "URI Too Long\n");
    }
    kind = path_kind(fs);
    if (kind == 2) {
        char with_index[1024];
        if (index_url[0] && index_url[strlen(index_url) - 1] != '/') {
            char raw[2048];
            char loc[2048];
            size_t n;
            if (request_raw_path(req, raw, sizeof raw) != 0) {
                return set_plain(res, 414, "URI Too Long\n");
            }
            n = strlen(raw);
            if (n + 2 >= sizeof loc) return set_plain(res, 414, "URI Too Long\n");
            memcpy(loc, raw, n);
            loc[n] = '/';
            loc[n + 1] = '\0';
            return set_redirect(res, 302, loc, req->query);
        }
        if (join_url_name(with_index, sizeof with_index, index_url, "index.html") != 0) {
            return set_plain(res, 404, "Not Found\n");
        }
        if (path_join_url(fs, sizeof fs, web, with_index) != 0) {
            return set_plain(res, 404, "Not Found\n");
        }
        kind = path_kind(fs);
        copy_trunc(index_url, sizeof index_url, with_index);
    }
    if (kind != 1) return set_plain(res, 404, "Not Found\n");
    if (!file_inside(web, fs)) return set_plain(res, 403, "Forbidden\n");
    if (!allows_read(req)) return set_plain(res, 405, "Method Not Allowed\n");
    return serve_raw_file(res, fs, index_url);
}

int router_handle(const HttpRequest *req, HttpResponse *res, const char *repo_root) {
    int blocked;
    char slug[256];
    int trailing = 0;
    char fs[PATH_MAX];
    int kind;
    if (!req || !res || !repo_root || !repo_root[0]) return -1;
    if (req->method[0] == '\0' || req->path[0] == '\0') {
        return set_plain(res, 400, "Bad Request\n");
    }
    blocked = forbid_status(req->path);
    if (blocked == 400) return set_plain(res, 400, "Bad Request\n");
    if (blocked == 403) return set_plain(res, 403, "Forbidden\n");

    if (strcmp(req->path, "/") == 0) {
        char preview[32];
        if (http_query_param(req->query, "preview", preview, sizeof preview) &&
            strcmp(preview, "1") == 0) {
            return set_redirect(res, 302, "/sales/index.php", NULL);
        }
        return set_redirect(res, 302, "/__studio/", NULL);
    }
    if (strcmp(req->path, "/__studio") == 0) {
        return set_redirect(res, 302, "/__studio/", req->query);
    }
    if (is_api_path(req->path)) {
        int rc = api_handle_request(req, res, repo_root);
        if (rc != 0 && res->status < 400) {
            http_response_set_error(res, 500, "API error");
        }
        if (res->status <= 0) res->status = 500;
        return 0;
    }
    if (strncmp(req->path, "/__studio/", 10) == 0) {
        return serve_studio(req, res, repo_root);
    }
    if (is_landing_alias(req->path)) {
        return set_redirect(res, 301, "/sales/index.php", req->query);
    }
    if (sales_vehicle(req->path, slug, sizeof slug, &trailing)) {
        char url[1024];
        size_t ns = strlen(slug);
        if (7 + ns + 10 + 1 > sizeof url) return set_plain(res, 414, "URI Too Long\n");
        memcpy(url, "/sales/", 7);
        memcpy(url + 7, slug, ns);
        memcpy(url + 7 + ns, "/index.php", 11);
        if (path_join_url(fs, sizeof fs, repo_root, url) == 0 && path_kind(fs) == 1) {
            if (!trailing) {
                char raw[2048];
                char loc[2048];
                if (request_raw_path(req, raw, sizeof raw) != 0) {
                    return set_plain(res, 414, "URI Too Long\n");
                }
                if (build_proxied_uri(loc, sizeof loc, raw, "index.php", NULL) != 0) {
                    return set_plain(res, 414, "URI Too Long\n");
                }
                return set_redirect(res, 301, loc, req->query);
            }
            {
                char raw[2048];
                char proxied[2048];
                if (request_raw_path(req, raw, sizeof raw) != 0) {
                    return set_plain(res, 414, "URI Too Long\n");
                }
                if (build_proxied_uri(proxied, sizeof proxied, raw, "index.php", req->query) != 0) {
                    return set_plain(res, 414, "URI Too Long\n");
                }
                return serve_disk(req, res, repo_root, url, fs, proxied, url);
            }
        }
    }

    if (path_join_url(fs, sizeof fs, repo_root, req->path) != 0) {
        return set_plain(res, 414, "URI Too Long\n");
    }
    kind = path_kind(fs);
    if (kind == 2 && is_indexed_section(req->path)) {
        size_t n = strlen(req->path);
        if (n == 0 || req->path[n - 1] != '/') {
            char raw[2048];
            char loc[2048];
            size_t rn;
            if (request_raw_path(req, raw, sizeof raw) != 0) {
                return set_plain(res, 414, "URI Too Long\n");
            }
            rn = strlen(raw);
            if (rn + 2 >= sizeof loc) return set_plain(res, 414, "URI Too Long\n");
            memcpy(loc, raw, rn);
            if (rn == 0 || loc[rn - 1] != '/') loc[rn++] = '/';
            loc[rn] = '\0';
            return set_redirect(res, 301, loc, req->query);
        }
        return resolve_directory_index(req, res, repo_root);
    }
    if (kind == 2) return serve_not_found(res, repo_root);
    if (kind == 1) return serve_disk(req, res, repo_root, req->path, fs, NULL, req->path);
    return serve_not_found(res, repo_root);
}
