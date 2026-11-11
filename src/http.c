#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "http.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <sys/time.h>
#endif

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#define HTTP_MAX_HEADER (64u * 1024u)

struct HttpServer {
    sock_t fd;
    int port;
};

static volatile sig_atomic_t g_stop = 0;

static void copy_cstr(char *dst, size_t dst_sz, const char *src) {
    size_t n;
    if (!dst || dst_sz == 0) return;
    if (!src) src = "";
    n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    if (n) memcpy(dst, src, n);
    dst[n] = '\0';
}

static void copy_span(char *dst, size_t dst_sz, const char *src, size_t src_len) {
    if (!dst || dst_sz == 0) return;
    if (!src) src_len = 0;
    if (src_len >= dst_sz) src_len = dst_sz - 1;
    if (src_len && src) memcpy(dst, src, src_len);
    dst[src_len] = '\0';
}

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

static int span_ieq(const char *s, size_t n, const char *lit) {
    size_t m = strlen(lit);
    return n == m && memcasecmp(s, lit, n) == 0;
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void http_response_init(HttpResponse *res) {
    if (!res) return;
    memset(res, 0, sizeof *res);
    res->status = 200;
}

void http_response_free(HttpResponse *res) {
    if (!res) return;
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
    res->body_cap = 0;
}

int http_response_set_body(HttpResponse *res, const void *data, size_t len) {
    char *nbuf;
    if (!res) return -1;
    if (len > 0 && !data) return -1;
    nbuf = malloc(len + 1);
    if (!nbuf) return -1;
    if (len) memcpy(nbuf, data, len);
    nbuf[len] = '\0';
    free(res->body);
    res->body = nbuf;
    res->body_len = len;
    res->body_cap = len + 1;
    return 0;
}

int http_response_set_json(HttpResponse *res, int status, const char *json_str) {
    if (!res) return -1;
    if (!json_str) json_str = "null";
    res->status = status;
    copy_cstr(res->content_type, sizeof res->content_type, "application/json; charset=UTF-8");
    return http_response_set_body(res, json_str, strlen(json_str));
}

int http_response_set_error(HttpResponse *res, int status, const char *message) {
    size_t mlen;
    size_t cap;
    size_t j;
    size_t i;
    char *buf;
    int rc;
    const char *prefix = "{\"error\":\"";
    if (!res) return -1;
    if (!message) message = "error";
    mlen = strlen(message);
    if (mlen > 1024) mlen = 1024;
    cap = mlen * 6 + 16;
    buf = malloc(cap);
    if (!buf) return -1;
    j = strlen(prefix);
    memcpy(buf, prefix, j);
    for (i = 0; i < mlen; i++) {
        unsigned char c = (unsigned char)message[i];
        char tmp[8];
        const char *esc = NULL;
        size_t elen = 0;
        if (c == '"' || c == '\\') {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            elen = 2;
            esc = tmp;
        } else if (c == '\n') {
            esc = "\\n";
            elen = 2;
        } else if (c == '\r') {
            esc = "\\r";
            elen = 2;
        } else if (c == '\t') {
            esc = "\\t";
            elen = 2;
        } else if (c < 0x20) {
            int n = snprintf(tmp, sizeof tmp, "\\u%04x", c);
            if (n < 0) {
                free(buf);
                return -1;
            }
            elen = (size_t)n;
            esc = tmp;
        }
        if (esc) {
            memcpy(buf + j, esc, elen);
            j += elen;
        } else {
            buf[j++] = (char)c;
        }
    }
    buf[j++] = '"';
    buf[j++] = '}';
    buf[j] = '\0';
    rc = http_response_set_json(res, status, buf);
    free(buf);
    return rc;
}

int http_url_decode(const char *src, char *dst, size_t dst_sz) {
    size_t i;
    size_t j = 0;
    if (!src || !dst || dst_sz == 0) return -1;
    for (i = 0; src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '%') {
            int hi;
            int lo;
            hi = hex_nibble(src[i + 1]);
            lo = src[i + 1] ? hex_nibble(src[i + 2]) : -1;
            if (hi >= 0 && lo >= 0) {
                c = (unsigned char)((hi << 4) | lo);
                i += 2;
            }
        }
        if (c == 0) {
            dst[0] = '\0';
            return -1;
        }
        if (j + 1 >= dst_sz) {
            dst[j] = '\0';
            return -1;
        }
        dst[j++] = (char)c;
    }
    dst[j] = '\0';
    return 0;
}

int http_query_param(const char *query, const char *key, char *out_val, size_t out_sz) {
    const char *p;
    size_t key_len;
    if (!out_val || out_sz == 0) return 0;
    out_val[0] = '\0';
    if (!query || !key) return 0;
    key_len = strlen(key);
    if (key_len == 0) return 0;
    p = query;
    while (*p) {
        const char *amp = strchr(p, '&');
        size_t pair_len = amp ? (size_t)(amp - p) : strlen(p);
        size_t klen = pair_len;
        const char *eq = NULL;
        size_t i;
        for (i = 0; i < pair_len; i++) {
            if (p[i] == '=') {
                eq = p + i;
                klen = i;
                break;
            }
        }
        if (klen == key_len && memcmp(p, key, key_len) == 0) {
            size_t vlen;
            char *tmp;
            int rc;
            if (!eq) return 1;
            vlen = pair_len - (klen + 1);
            tmp = malloc(vlen + 1);
            if (!tmp) return 0;
            if (vlen) memcpy(tmp, eq + 1, vlen);
            tmp[vlen] = '\0';
            for (i = 0; i < vlen; i++) {
                if (tmp[i] == '+') tmp[i] = ' ';
            }
            rc = http_url_decode(tmp, out_val, out_sz);
            free(tmp);
            if (rc != 0) {
                out_val[0] = '\0';
                return 0;
            }
            return 1;
        }
        if (!amp) break;
        p = amp + 1;
    }
    return 0;
}

void http_request_init(HttpRequest *req) {
    if (!req) return;
    memset(req, 0, sizeof *req);
}

void http_request_free(HttpRequest *req) {
    if (!req) return;
    free(req->body);
    req->body = NULL;
    req->body_len = 0;
}

int http_read_file(const char *path, char **out, size_t *out_len, size_t max_bytes) {
    struct stat st;
    FILE *f;
    char *buf;
    size_t n;
    size_t got = 0;
    if (!path || !out || !out_len || max_bytes == 0) return -1;
    *out = NULL;
    *out_len = 0;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0) return -1;
    if ((unsigned long long)st.st_size > (unsigned long long)max_bytes) return -1;
    n = (size_t)st.st_size;
    f = fopen(path, "rb");
    if (!f) return -1;
    buf = malloc(n + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    while (got < n) {
        size_t r = fread(buf + got, 1, n - got, f);
        if (r == 0) {
            free(buf);
            fclose(f);
            return -1;
        }
        got += r;
    }
    fclose(f);
    buf[n] = '\0';
    *out = buf;
    *out_len = n;
    return 0;
}

static int find_header_end(const char *buf, size_t len, size_t *header_len) {
    size_t i;
    if (len < 4) return 0;
    for (i = 0; i + 3 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') {
            *header_len = i + 4;
            return 1;
        }
    }
    return 0;
}

static int next_crlf(const char *buf, size_t end, size_t *pos, const char **line, size_t *line_len) {
    size_t i = *pos;
    size_t start = *pos;
    while (i + 1 < end) {
        if (buf[i] == '\n') return -1;
        if (buf[i] == '\r' && buf[i + 1] == '\n') {
            *line = buf + start;
            *line_len = i - start;
            *pos = i + 2;
            return 1;
        }
        i++;
    }
    return 0;
}

static int header_name_is(const char *line, size_t line_len, const char *name,
                          const char **val, size_t *val_len) {
    size_t nlen = strlen(name);
    size_t i;
    size_t end;
    if (line_len < nlen + 1) return 0;
    if (memcasecmp(line, name, nlen) != 0 || line[nlen] != ':') return 0;
    i = nlen + 1;
    while (i < line_len && (line[i] == ' ' || line[i] == '\t')) i++;
    end = line_len;
    while (end > i && (line[end - 1] == ' ' || line[end - 1] == '\t')) end--;
    *val = line + i;
    *val_len = end - i;
    return 1;
}

static int parse_i64(const char *s, size_t n, long long *out) {
    long long v = 0;
    size_t i;
    if (n == 0 || n > 18) return -1;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 0;
}

static int split_target(HttpRequest *req) {
    const char *uri = req->raw_uri;
    const char *q = strchr(uri, '?');
    size_t plen = q ? (size_t)(q - uri) : strlen(uri);
    char encoded[1024];
    if (plen == 0 || plen >= sizeof encoded) return -1;
    memcpy(encoded, uri, plen);
    encoded[plen] = '\0';
    if (http_url_decode(encoded, req->path, sizeof req->path) != 0) return -1;
    if (req->path[0] != '/') return -1;
    if (q) {
        size_t ql = strlen(q + 1);
        if (ql >= sizeof req->query) return -1;
        memcpy(req->query, q + 1, ql + 1);
    } else {
        req->query[0] = '\0';
    }
    return 0;
}

static int parse_request_line(const char *line, size_t len, HttpRequest *req) {
    size_t i = 0;
    size_t m = 0;
    size_t u = 0;
    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char)line[i];
        if (c < 0x20 || c == 0x7f) return -1;
    }
    i = 0;
    while (i < len && line[i] != ' ' && line[i] != '\t') {
        unsigned char c = (unsigned char)line[i];
        int alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!alpha || m + 1 >= sizeof req->method) return -1;
        req->method[m++] = (char)c;
        i++;
    }
    if (m == 0) return -1;
    req->method[m] = '\0';
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= len || line[i] != '/') return -1;
    while (i < len && line[i] != ' ' && line[i] != '\t') {
        if (u + 1 >= sizeof req->raw_uri) return -1;
        req->raw_uri[u++] = line[i++];
    }
    if (u == 0) return -1;
    req->raw_uri[u] = '\0';
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i + 8 > len || memcmp(line + i, "HTTP/1.", 7) != 0) return -1;
    if (line[i + 7] != '0' && line[i + 7] != '1') return -1;
    i += 8;
    while (i < len && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i != len) return -1;
    return split_target(req);
}

static int parse_headers(const char *data, size_t len, HttpRequest *req,
                         size_t *header_len, long long *cl, int *chunked) {
    size_t hend = 0;
    size_t pos = 0;
    const char *line = NULL;
    size_t line_len = 0;
    int saw_cl = 0;
    int lines = 0;
    long long content_length = -1;
    int is_chunked = 0;

    if (!find_header_end(data, len, &hend)) return 1;
    if (next_crlf(data, hend, &pos, &line, &line_len) != 1) return -1;
    if (parse_request_line(line, line_len, req) != 0) return -1;
    while (pos < hend) {
        const char *val = NULL;
        size_t val_len = 0;
        size_t k;
        int nr = next_crlf(data, hend, &pos, &line, &line_len);
        if (nr != 1) return -1;
        if (line_len == 0) break;
        if (++lines > 128) return -1;
        if (line[0] == ' ' || line[0] == '\t') return -1;
        for (k = 0; k < line_len; k++) {
            if (line[k] == '\0') return -1;
        }
        if (header_name_is(line, line_len, "Content-Length", &val, &val_len)) {
            long long v = 0;
            if (parse_i64(val, val_len, &v) != 0) return -1;
            if (saw_cl && content_length != v) return -1;
            saw_cl = 1;
            content_length = v;
        } else if (header_name_is(line, line_len, "Transfer-Encoding", &val, &val_len)) {
            if (!span_ieq(val, val_len, "identity")) is_chunked = 1;
        } else if (header_name_is(line, line_len, "Content-Type", &val, &val_len)) {
            copy_span(req->content_type, sizeof req->content_type, val, val_len);
        } else if (header_name_is(line, line_len, "Cookie", &val, &val_len)) {
            copy_span(req->cookie, sizeof req->cookie, val, val_len);
        } else if (header_name_is(line, line_len, "User-Agent", &val, &val_len)) {
            copy_span(req->user_agent, sizeof req->user_agent, val, val_len);
        }
    }
    if (is_chunked && saw_cl) return -1;
    *header_len = hend;
    *cl = content_length;
    *chunked = is_chunked;
    return 0;
}

static int attach_body(HttpRequest *req, const char *data, size_t header_len, long long cl) {
    size_t n;
    char *body;
    if (cl < 0) cl = 0;
    if (cl > (long long)HTTP_MAX_BODY) return HTTP_PARSE_TOO_LARGE;
    n = (size_t)cl;
    if (n == 0) return HTTP_PARSE_OK;
    body = malloc(n + 1);
    if (!body) return HTTP_PARSE_BAD;
    memcpy(body, data + header_len, n);
    body[n] = '\0';
    req->body = body;
    req->body_len = n;
    return HTTP_PARSE_OK;
}

int http_parse_request_buffer(const char *data, size_t len, HttpRequest *req) {
    size_t header_len = 0;
    long long cl = -1;
    int chunked = 0;
    int rc;
    if (!data || !req) return HTTP_PARSE_BAD;
    http_request_free(req);
    http_request_init(req);
    rc = parse_headers(data, len, req, &header_len, &cl, &chunked);
    if (rc != 0) return HTTP_PARSE_BAD;
    if (chunked) return HTTP_PARSE_NO_LENGTH;
    if (cl < 0) cl = 0;
    if (cl > (long long)HTTP_MAX_BODY) return HTTP_PARSE_TOO_LARGE;
    if (header_len > len || (size_t)cl > len - header_len) return HTTP_PARSE_BAD;
    return attach_body(req, data, header_len, cl);
}

static int net_init(void) {
#ifdef _WIN32
    static int ready = 0;
    if (!ready) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
        ready = 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    return 0;
}

static void sock_close(sock_t fd) {
    if (fd == SOCK_BAD) return;
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

static int io_again(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEINTR || e == WSAEWOULDBLOCK || e == WSAEINPROGRESS;
#else
    return errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

static int io_in_progress(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
#else
    return errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK;
#endif
}

static int io_recv(sock_t fd, void *buf, size_t len) {
#ifdef _WIN32
    int n;
    if (len > 65536) len = 65536;
    n = recv(fd, (char *)buf, (int)len, 0);
    if (n == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEINTR) return -2;
        return -1;
    }
    return n;
#else
    ssize_t n;
    if (len > 65536) len = 65536;
    n = recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) return -2;
        return -1;
    }
    return (int)n;
#endif
}

static int send_all(sock_t fd, const void *buf, size_t len) {
    const char *p = buf;
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
#ifdef _WIN32
        int n;
        if (chunk > 65536) chunk = 65536;
        n = send(fd, p + off, (int)chunk, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) continue;
            return -1;
        }
#else
        ssize_t n;
        if (chunk > 65536) chunk = 65536;
        n = send(fd, p + off, chunk, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
#endif
        if (n == 0) return -1;
        off += (size_t)n;
    }
    return 0;
}

static int set_nonblock(sock_t fd, int on) {
#ifdef _WIN32
    u_long mode = on ? 1 : 0;
    return ioctlsocket(fd, FIONBIO, &mode) == 0 ? 0 : -1;
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (on) flags |= O_NONBLOCK;
    else flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
#endif
}

static void set_cloexec(sock_t fd) {
#ifdef _WIN32
    SetHandleInformation((HANDLE)fd, HANDLE_FLAG_INHERIT, 0);
#else
    int flags = fcntl(fd, F_GETFD);
    if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
#endif
    (void)fd;
}

static void set_timeouts(sock_t fd, int timeout_ms) {
#ifdef _WIN32
    DWORD t = timeout_ms < 0 ? 0 : (DWORD)timeout_ms;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t, sizeof t);
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
#endif
}

static void set_int_opt(sock_t fd, int level, int opt) {
    int one = 1;
#ifdef _WIN32
    setsockopt(fd, level, opt, (const char *)&one, sizeof one);
#else
    setsockopt(fd, level, opt, &one, sizeof one);
#endif
}

static int poll_fd(sock_t fd, short events, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD pfd;
    int rc;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    rc = WSAPoll(&pfd, 1, timeout_ms);
    if (rc > 0 && (pfd.revents & events)) return 1;
    return rc == 0 ? 0 : -1;
#else
    struct pollfd pfd;
    int rc;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0) return -1;
    if (rc == 0) return 0;
    if (pfd.revents & events) return 1;
    return -1;
#endif
}

static int buf_grow(char **buf, size_t *cap, size_t need) {
    char *p;
    size_t ncap;
    if (need <= *cap) return 0;
    ncap = *cap ? *cap : 4096u;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2)) return -1;
        ncap *= 2;
    }
    p = realloc(*buf, ncap);
    if (!p) return -1;
    *buf = p;
    *cap = ncap;
    return 0;
}

static const char *reason_phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    default: return status >= 400 ? "Error" : "OK";
    }
}

typedef struct HBuf {
    char data[4096];
    size_t len;
} HBuf;

static int hb_add(HBuf *h, const char *s, size_t n) {
    if (h->len + n >= sizeof h->data) return -1;
    if (n) memcpy(h->data + h->len, s, n);
    h->len += n;
    h->data[h->len] = '\0';
    return 0;
}

static int hb_str(HBuf *h, const char *s) {
    return hb_add(h, s, strlen(s));
}

static int send_response(sock_t fd, const HttpResponse *res) {
    HBuf h;
    char line[160];
    int n;
    int status;
    size_t body_len;
    const char *reason;
    memset(&h, 0, sizeof h);
    status = res && res->status > 0 ? res->status : 500;
    reason = reason_phrase(status);
    body_len = (res && res->body) ? res->body_len : 0;
    n = snprintf(line, sizeof line, "HTTP/1.1 %d %s\r\n", status, reason);
    if (n < 0 || (size_t)n >= sizeof line || hb_str(&h, line) != 0) return -1;
    if (res && res->content_type[0]) {
        if (hb_str(&h, "Content-Type: ") != 0) return -1;
        if (hb_str(&h, res->content_type) != 0) return -1;
        if (hb_str(&h, "\r\n") != 0) return -1;
    }
    n = snprintf(line, sizeof line, "Content-Length: %zu\r\n", body_len);
    if (n < 0 || (size_t)n >= sizeof line || hb_str(&h, line) != 0) return -1;
    if (hb_str(&h, "Connection: close\r\n") != 0) return -1;
    if (hb_str(&h, "Server: ClassicCarsStudio\r\n") != 0) return -1;
    if (res && res->location[0]) {
        if (hb_str(&h, "Location: ") != 0) return -1;
        if (hb_str(&h, res->location) != 0) return -1;
        if (hb_str(&h, "\r\n") != 0) return -1;
    }
    if (res && res->extra_headers[0]) {
        size_t elen = strlen(res->extra_headers);
        if (hb_add(&h, res->extra_headers, elen) != 0) return -1;
        if (res->extra_headers[elen - 1] != '\n' && hb_str(&h, "\r\n") != 0) return -1;
    }
    if (hb_str(&h, "\r\n") != 0) return -1;
    if (send_all(fd, h.data, h.len) != 0) return -1;
    if (body_len && send_all(fd, res->body, body_len) != 0) return -1;
    return 0;
}

static void send_simple(sock_t fd, int status, const char *msg) {
    HttpResponse res;
    http_response_init(&res);
    if (http_response_set_error(&res, status, msg) != 0) res.status = status;
    send_response(fd, &res);
    http_response_free(&res);
}

static int read_request(sock_t fd, HttpRequest *req, int *err_status) {
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;
    size_t header_len = 0;
    long long cl = -1;
    int chunked = 0;
    int headers_done = 0;
    int rc;

    http_request_free(req);
    http_request_init(req);
    while (!headers_done) {
        int n;
        if (len > HTTP_MAX_HEADER) {
            free(buf);
            *err_status = 400;
            return -1;
        }
        if (buf_grow(&buf, &cap, len + 4096) != 0) {
            free(buf);
            *err_status = 500;
            return -1;
        }
        n = io_recv(fd, buf + len, cap - len);
        if (n == -2) continue;
        if (n < 0) {
            free(buf);
            *err_status = 400;
            return -1;
        }
        if (n == 0) {
            free(buf);
            *err_status = len == 0 ? 0 : 400;
            return -1;
        }
        len += (size_t)n;
        if (find_header_end(buf, len, &header_len)) headers_done = 1;
    }
    rc = parse_headers(buf, len, req, &header_len, &cl, &chunked);
    if (rc != 0) {
        free(buf);
        *err_status = 400;
        return -1;
    }
    if (chunked) {
        free(buf);
        *err_status = 411;
        return -1;
    }
    if (cl < 0) cl = 0;
    if (cl > (long long)HTTP_MAX_BODY) {
        free(buf);
        *err_status = 413;
        return -1;
    }
    if ((size_t)cl > SIZE_MAX - header_len) {
        free(buf);
        *err_status = 413;
        return -1;
    }
    while (len < header_len + (size_t)cl) {
        int n;
        if (buf_grow(&buf, &cap, header_len + (size_t)cl) != 0) {
            free(buf);
            *err_status = 500;
            return -1;
        }
        n = io_recv(fd, buf + len, cap - len);
        if (n == -2) continue;
        if (n <= 0) {
            free(buf);
            *err_status = 400;
            return -1;
        }
        len += (size_t)n;
    }
    rc = attach_body(req, buf, header_len, cl);
    free(buf);
    if (rc != HTTP_PARSE_OK) {
        *err_status = rc == HTTP_PARSE_TOO_LARGE ? 413 : 500;
        return -1;
    }
    return 0;
}

static void handle_client(sock_t fd, HttpHandler handler, void *ud) {
    HttpRequest req;
    HttpResponse res;
    int err = 0;
    int rc;
    http_request_init(&req);
    if (read_request(fd, &req, &err) != 0) {
        if (err > 0) {
            const char *msg = "Bad request";
            if (err == 411) msg = "Length required";
            else if (err == 413) msg = "Payload too large";
            else if (err == 500) msg = "Out of memory";
            send_simple(fd, err, msg);
        }
        http_request_free(&req);
        return;
    }
    http_response_init(&res);
    rc = handler(&req, &res, ud);
    if (rc != 0) {
        http_response_free(&res);
        http_response_init(&res);
        http_response_set_error(&res, 500, "handler failed");
    }
    if (res.status <= 0) res.status = 500;
    printf("%s %s -> %d\n",
           req.method[0] ? req.method : "-",
           req.raw_uri[0] ? req.raw_uri : req.path,
           res.status);
    fflush(stdout);
    send_response(fd, &res);
    http_response_free(&res);
    http_request_free(&req);
}

static int fill_addr(struct sockaddr_in *addr, const char *host, int port) {
    memset(addr, 0, sizeof *addr);
    addr->sin_family = AF_INET;
    addr->sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host ? host : "127.0.0.1", &addr->sin_addr) != 1) return -1;
    return 0;
}

int http_try_connect(const char *host, int port, int timeout_ms) {
    sock_t fd;
    struct sockaddr_in addr;
    int rc;
    if (port < 1 || port > 65535) return -1;
    if (net_init() != 0) return -1;
    if (fill_addr(&addr, host, port) != 0) return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_BAD) return -1;
    if (set_nonblock(fd, 1) != 0) {
        sock_close(fd);
        return -1;
    }
    rc = connect(fd, (struct sockaddr *)&addr, sizeof addr);
    if (rc != 0 && !io_in_progress()) {
        sock_close(fd);
        return -1;
    }
    if (rc != 0) {
        int err = 0;
        if (poll_fd(fd, POLLOUT, timeout_ms) != 1) {
            sock_close(fd);
            return -1;
        }
#ifdef _WIN32
        {
            int len = (int)sizeof err;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0 || err != 0) {
                sock_close(fd);
                return -1;
            }
        }
#else
        {
            socklen_t len = sizeof err;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                sock_close(fd);
                return -1;
            }
        }
#endif
    }
    sock_close(fd);
    return 0;
}

HttpServer *http_server_new(void) {
    HttpServer *srv = calloc(1, sizeof *srv);
    if (!srv) return NULL;
    srv->fd = SOCK_BAD;
    srv->port = -1;
    return srv;
}

void http_server_free(HttpServer *srv) {
    if (!srv) return;
    if (srv->fd != SOCK_BAD) sock_close(srv->fd);
    srv->fd = SOCK_BAD;
    free(srv);
}

int http_server_port(const HttpServer *srv) {
    if (!srv) return -1;
    return srv->port;
}

void http_server_request_stop(void) {
    g_stop = 1;
}

int http_server_listen(HttpServer *srv, const char *bind_host, int port) {
    sock_t fd;
    struct sockaddr_in addr;
    struct sockaddr_in bound;
#ifdef _WIN32
    int blen = (int)sizeof bound;
#else
    socklen_t blen = sizeof bound;
#endif
    if (!srv || port < 0 || port > 65535) return -1;
    if (net_init() != 0) return -1;
    g_stop = 0;
    if (srv->fd != SOCK_BAD) {
        sock_close(srv->fd);
        srv->fd = SOCK_BAD;
    }
    if (fill_addr(&addr, bind_host ? bind_host : "127.0.0.1", port) != 0) return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_BAD) return -1;
    set_cloexec(fd);
    set_int_opt(fd, SOL_SOCKET, SO_REUSEADDR);
    if (set_nonblock(fd, 1) != 0 ||
        bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 ||
        listen(fd, 128) != 0) {
        sock_close(fd);
        return -1;
    }
    memset(&bound, 0, sizeof bound);
    if (getsockname(fd, (struct sockaddr *)&bound, &blen) != 0) {
        sock_close(fd);
        return -1;
    }
    srv->fd = fd;
    srv->port = (int)ntohs(bound.sin_port);
    return 0;
}

int http_server_serve(HttpServer *srv, HttpHandler handler, void *handler_ud,
                      HttpTick tick, void *tick_ud) {
    if (!srv || srv->fd == SOCK_BAD || !handler) return -1;
    while (!g_stop) {
        sock_t client;
        int pr;
        if (tick) tick(tick_ud);
        if (g_stop) break;
        pr = poll_fd(srv->fd, POLLIN, 200);
        if (pr < 0) {
            if (io_again()) continue;
            return -1;
        }
        if (pr == 0) continue;
        client = accept(srv->fd, NULL, NULL);
        if (client == SOCK_BAD) {
            if (io_again()) continue;
            return -1;
        }
        set_cloexec(client);
        set_nonblock(client, 0);
        set_timeouts(client, 30000);
        set_int_opt(client, IPPROTO_TCP, TCP_NODELAY);
        handle_client(client, handler, handler_ud);
        sock_close(client);
    }
    return 0;
}
