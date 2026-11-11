#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "proxy.h"

#include "settings.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_BAD (-1)
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

#define PROXY_MAX_RESPONSE (32u * 1024u * 1024u)
#define PROXY_BUF_MAX (PROXY_MAX_RESPONSE + 65536u)

#ifndef _WIN32
static pthread_mutex_t g_proxy_mu = PTHREAD_MUTEX_INITIALIZER;
static void proxy_lock(void) { pthread_mutex_lock(&g_proxy_mu); }
static void proxy_unlock(void) { pthread_mutex_unlock(&g_proxy_mu); }
#else
static CRITICAL_SECTION g_proxy_mu;
static INIT_ONCE g_proxy_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK proxy_init_mu(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once;
    (void)param;
    (void)ctx;
    InitializeCriticalSection(&g_proxy_mu);
    return TRUE;
}
static void proxy_lock(void) {
    InitOnceExecuteOnce(&g_proxy_once, proxy_init_mu, NULL, NULL);
    EnterCriticalSection(&g_proxy_mu);
}
static void proxy_unlock(void) { LeaveCriticalSection(&g_proxy_mu); }
#endif

static int net_init(void) {
#ifdef _WIN32
    static int ready = 0;
    if (!ready) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
        ready = 1;
    }
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

static int io_in_progress(void) {
#ifdef _WIN32
    int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS || e == WSAEALREADY;
#else
    return errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK;
#endif
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

static int poll_out(sock_t fd, int timeout_ms) {
#ifdef _WIN32
    WSAPOLLFD pfd;
    int rc;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    rc = WSAPoll(&pfd, 1, timeout_ms);
    if (rc > 0 && (pfd.revents & POLLOUT)) return 1;
    return 0;
#else
    struct pollfd pfd;
    int rc;
    pfd.fd = fd;
    pfd.events = POLLOUT;
    pfd.revents = 0;
    rc = poll(&pfd, 1, timeout_ms);
    if (rc > 0 && (pfd.revents & POLLOUT)) return 1;
    return 0;
#endif
}

static int io_recv(sock_t fd, void *buf, size_t len) {
#ifdef _WIN32
    int n;
    if (len > 65536u) len = 65536u;
    n = recv(fd, (char *)buf, (int)len, 0);
    if (n == SOCKET_ERROR) {
        if (WSAGetLastError() == WSAEINTR) return -2;
        return -1;
    }
    return n;
#else
    ssize_t n;
    if (len > 65536u) len = 65536u;
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
        if (chunk > 65536u) chunk = 65536u;
        n = send(fd, p + off, (int)chunk, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() == WSAEINTR) continue;
            return -1;
        }
#else
        ssize_t n;
        if (chunk > 65536u) chunk = 65536u;
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

static int connect_php(int port) {
    sock_t fd;
    struct sockaddr_in addr;
    int rc;
    if (net_init() != 0) return -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == SOCK_BAD) return -1;
#ifndef _WIN32
    {
        int flags = fcntl(fd, F_GETFD);
        if (flags >= 0) fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
        {
            int one = 1;
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
        }
#endif
    }
#endif
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) {
        sock_close(fd);
        return -1;
    }
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
        if (!poll_out(fd, 2000)) {
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
            socklen_t len = (socklen_t)sizeof err;
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
                sock_close(fd);
                return -1;
            }
        }
#endif
    }
    if (set_nonblock(fd, 0) != 0) {
        sock_close(fd);
        return -1;
    }
    set_timeouts(fd, 30000);
    return (int)fd;
}

static int method_ok(const char *method) {
    size_t i;
    if (!method || !method[0] || strlen(method) >= 16) return 0;
    for (i = 0; method[i]; i++) {
        char c = method[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    }
    return 1;
}

static int uri_ok(const char *uri) {
    size_t i;
    if (!uri || uri[0] != '/') return 0;
    for (i = 0; uri[i]; i++) {
        unsigned char c = (unsigned char)uri[i];
        if (c <= 0x20 || c == 0x7f) return 0;
        if (i >= 8000) return 0;
    }
    return 1;
}

/* php -S maps %26 onto a directory name that contains '&'.
 * Encode a raw '&' in the path and leave the query string unchanged.
 * An already encoded %26 is left as-is. */
static int php_request_uri(const char *uri, char *dst, size_t dst_sz) {
    const char *q;
    size_t o = 0;
    if (!uri || !dst || dst_sz == 0) return -1;
    q = strchr(uri, '?');
    for (; *uri && uri != q; uri++) {
        if (*uri == '&') {
            if (o + 3 >= dst_sz) return -1;
            dst[o++] = '%';
            dst[o++] = '2';
            dst[o++] = '6';
        } else {
            if (o + 1 >= dst_sz) return -1;
            dst[o++] = *uri;
        }
    }
    if (q) {
        size_t n = strlen(q);
        if (o + n >= dst_sz) return -1;
        memcpy(dst + o, q, n);
        o += n;
    }
    dst[o] = '\0';
    return 0;
}

static int header_safe(const char *s) {
    const unsigned char *p;
    if (!s) return 0;
    for (p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || *p == 0x7f) return 0;
    }
    return 1;
}

static int add_text(char **buf, size_t *len, size_t *cap, const char *s) {
    size_t n = strlen(s);
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap ? *cap : 1024u;
        char *p;
        while (ncap < *len + n + 1) {
            if (ncap > (SIZE_MAX / 2)) return -1;
            ncap *= 2;
        }
        p = realloc(*buf, ncap);
        if (!p) return -1;
        *buf = p;
        *cap = ncap;
    }
    memcpy(*buf + *len, s, n);
    *len += n;
    (*buf)[*len] = '\0';
    return 0;
}

typedef struct NetBuf {
    char *data;
    size_t len;
    size_t cap;
    size_t pos;
} NetBuf;

static void nb_free(NetBuf *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->pos = 0;
}

static int nb_recv(sock_t fd, NetBuf *b) {
    int n;
    if (b->len == b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 8192u;
        char *p;
        if (b->cap >= PROXY_BUF_MAX) return -1;
        if (ncap < b->cap || ncap > PROXY_BUF_MAX) ncap = PROXY_BUF_MAX;
        if (ncap <= b->cap) return -1;
        p = realloc(b->data, ncap);
        if (!p) return -1;
        b->data = p;
        b->cap = ncap;
    }
    n = io_recv(fd, b->data + b->len, b->cap - b->len);
    if (n == -2) return nb_recv(fd, b);
    if (n < 0) return -1;
    if (n == 0) return 0;
    b->len += (size_t)n;
    return 1;
}

static int nb_line(sock_t fd, NetBuf *b, char *line, size_t line_sz) {
    if (line_sz == 0) return -1;
    for (;;) {
        size_t i;
        for (i = b->pos; i + 1 < b->len; i++) {
            if (b->data[i] == '\r' && b->data[i + 1] == '\n') {
                size_t n = i - b->pos;
                if (n >= line_sz) return -1;
                if (n) memcpy(line, b->data + b->pos, n);
                line[n] = '\0';
                b->pos = i + 2;
                return 0;
            }
        }
        if (b->len - b->pos >= line_sz) return -1;
        if (nb_recv(fd, b) <= 0) return -1;
    }
}

static int nb_ensure(sock_t fd, NetBuf *b, size_t need) {
    while (b->len < need) {
        if (nb_recv(fd, b) <= 0) return -1;
    }
    return 0;
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

static void trim_trailing(char *s) {
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

static int parse_i64(const char *s, long long *out) {
    long long v = 0;
    size_t i;
    size_t n = strlen(s);
    if (n == 0 || n > 18) return -1;
    for (i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return -1;
        v = v * 10 + (s[i] - '0');
    }
    *out = v;
    return 0;
}

static int parse_hex_size(const char *s, size_t *out) {
    size_t v = 0;
    int any = 0;
    const char *p = s;
    while (*p == ' ' || *p == '\t') p++;
    for (; *p && *p != ';'; p++) {
        int d;
        if (*p == ' ' || *p == '\t') continue;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else return -1;
        if (v > (SIZE_MAX >> 4)) return -1;
        v = (v << 4) + (size_t)d;
        any = 1;
    }
    if (!any) return -1;
    *out = v;
    return 0;
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

static int extra_add(char *extra, size_t extra_sz, const char *name, const char *value) {
    size_t have;
    size_t need;
    if (!extra || extra_sz == 0 || !name || !value) return -1;
    have = strlen(extra);
    need = strlen(name) + 2 + strlen(value) + 2;
    if (have + need >= extra_sz) return -1;
    memcpy(extra + have, name, strlen(name));
    have += strlen(name);
    extra[have++] = ':';
    extra[have++] = ' ';
    memcpy(extra + have, value, strlen(value));
    have += strlen(value);
    extra[have++] = '\r';
    extra[have++] = '\n';
    extra[have] = '\0';
    return 0;
}

static int read_chunked(sock_t fd, NetBuf *b, char **out, size_t *out_len) {
    char *body = NULL;
    size_t len = 0;
    size_t cap = 0;
    for (;;) {
        char line[128];
        size_t sz = 0;
        if (nb_line(fd, b, line, sizeof line) != 0) {
            free(body);
            return -1;
        }
        if (parse_hex_size(line, &sz) != 0) {
            free(body);
            return -1;
        }
        if (sz == 0) {
            for (;;) {
                char trailer[1024];
                if (nb_line(fd, b, trailer, sizeof trailer) != 0) {
                    free(body);
                    return -1;
                }
                if (trailer[0] == '\0') break;
            }
            break;
        }
        if (sz > PROXY_MAX_RESPONSE || len > PROXY_MAX_RESPONSE - sz) {
            free(body);
            return -1;
        }
        if (b->pos > SIZE_MAX - sz || nb_ensure(fd, b, b->pos + sz) != 0) {
            free(body);
            return -1;
        }
        if (len + sz + 1 > cap) {
            size_t ncap = cap ? cap : 1024u;
            char *p;
            while (ncap < len + sz + 1) {
                if (ncap > SIZE_MAX / 2) {
                    free(body);
                    return -1;
                }
                ncap *= 2;
            }
            p = realloc(body, ncap);
            if (!p) {
                free(body);
                return -1;
            }
            body = p;
            cap = ncap;
        }
        memcpy(body + len, b->data + b->pos, sz);
        len += sz;
        body[len] = '\0';
        b->pos += sz;
        if (b->pos > SIZE_MAX - 2 || nb_ensure(fd, b, b->pos + 2) != 0 ||
            b->data[b->pos] != '\r' || b->data[b->pos + 1] != '\n') {
            free(body);
            return -1;
        }
        b->pos += 2;
    }
    if (!body) {
        body = calloc(1, 1);
        if (!body) return -1;
    }
    *out = body;
    *out_len = len;
    return 0;
}

static int read_exact(sock_t fd, NetBuf *b, size_t n, char **out, size_t *out_len) {
    char *body;
    if (n > PROXY_MAX_RESPONSE) return -1;
    if (b->pos > SIZE_MAX - n) return -1;
    if (nb_ensure(fd, b, b->pos + n) != 0) return -1;
    body = malloc(n + 1);
    if (!body) return -1;
    if (n) memcpy(body, b->data + b->pos, n);
    body[n] = '\0';
    *out = body;
    *out_len = n;
    return 0;
}

static int read_to_eof(sock_t fd, NetBuf *b, char **out, size_t *out_len) {
    char *body;
    size_t n;
    for (;;) {
        int rc = nb_recv(fd, b);
        if (rc < 0) return -1;
        if (rc == 0) break;
        if (b->len > b->pos && b->len - b->pos > PROXY_MAX_RESPONSE) return -1;
    }
    n = b->len - b->pos;
    body = malloc(n + 1);
    if (!body) return -1;
    if (n) memcpy(body, b->data + b->pos, n);
    body[n] = '\0';
    *out = body;
    *out_len = n;
    return 0;
}

static int read_response(sock_t fd, int *status, char *content_type, size_t content_type_sz,
                         char *location, size_t location_sz, char *extra, size_t extra_sz,
                         char **body, size_t *body_len) {
    NetBuf nb;
    char line[4096];
    int code = 0;
    int major = 0;
    int minor = 0;
    long long cl = -1;
    int chunked = 0;
    int saw_cl = 0;
    int rc = -1;
    memset(&nb, 0, sizeof nb);
    *body = NULL;
    *body_len = 0;
    if (nb_line(fd, &nb, line, sizeof line) != 0) goto done;
    if (sscanf(line, "HTTP/%d.%d %d", &major, &minor, &code) != 3 || major != 1 ||
        code < 100 || code > 599) {
        goto done;
    }
    for (;;) {
        char name[128];
        const char *val;
        size_t i = 0;
        size_t nlen = 0;
        if (nb_line(fd, &nb, line, sizeof line) != 0) goto done;
        if (line[0] == '\0') break;
        trim_trailing(line);
        while (line[i] && line[i] != ':') {
            if (nlen + 1 >= sizeof name) goto done;
            name[nlen++] = line[i++];
        }
        if (line[i] != ':') goto done;
        name[nlen] = '\0';
        val = line + i + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (!header_safe(val)) continue;
        if (memcasecmp(name, "Content-Type", nlen) == 0 && nlen == strlen("Content-Type")) {
            if (content_type && content_type_sz) copy_trunc(content_type, content_type_sz, val);
        } else if (memcasecmp(name, "Location", nlen) == 0 && nlen == strlen("Location")) {
            if (location && location_sz) copy_trunc(location, location_sz, val);
        } else if (memcasecmp(name, "Content-Length", nlen) == 0 && nlen == strlen("Content-Length")) {
            long long v = 0;
            if (parse_i64(val, &v) != 0) goto done;
            if (saw_cl && cl != v) goto done;
            saw_cl = 1;
            cl = v;
        } else if (memcasecmp(name, "Transfer-Encoding", nlen) == 0 &&
                   nlen == strlen("Transfer-Encoding")) {
            if (memcasecmp(val, "identity", strlen(val) < 8 ? strlen(val) : 8) != 0 ||
                strlen(val) != 8) {
                chunked = 1;
            }
        } else if (memcasecmp(name, "Set-Cookie", nlen) == 0 && nlen == strlen("Set-Cookie")) {
            if (extra && extra_sz) extra_add(extra, extra_sz, "Set-Cookie", val);
        }
    }
    if (chunked) {
        if (read_chunked(fd, &nb, body, body_len) != 0) goto done;
    } else if (saw_cl) {
        if (cl < 0 || (unsigned long long)cl > PROXY_MAX_RESPONSE) goto done;
        if (read_exact(fd, &nb, (size_t)cl, body, body_len) != 0) goto done;
    } else {
        if (read_to_eof(fd, &nb, body, body_len) != 0) goto done;
    }
    if (status) *status = code;
    rc = 0;
done:
    if (rc != 0) {
        free(*body);
        *body = NULL;
        *body_len = 0;
    }
    nb_free(&nb);
    (void)minor;
    return rc;
}

static int proxy_exchange(const char *method, const char *uri,
                          const char *content_type, const char *cookie,
                          const char *user_agent, const char *preview_document_uri,
                          const void *req_body, size_t req_body_len,
                          int *status, char *content_type_out, size_t content_type_out_sz,
                          char *location, size_t location_sz,
                          char *extra, size_t extra_sz,
                          char **out_body, size_t *out_len) {
    int port;
    int fd = -1;
    char *hdr = NULL;
    size_t hlen = 0;
    size_t hcap = 0;
    char scratch[96];
    int n;
    int rc = -1;
    if (status) *status = 0;
    if (content_type_out && content_type_out_sz) content_type_out[0] = '\0';
    if (location && location_sz) location[0] = '\0';
    if (extra && extra_sz) extra[0] = '\0';
    if (out_body) *out_body = NULL;
    if (out_len) *out_len = 0;
    port = settings_get_php_port();
    if (port <= 0 || port > 65535) return -1;
    {
        char encoded[8192];
        if (php_request_uri(uri, encoded, sizeof encoded) != 0) return -1;
        if (!method_ok(method) || !uri_ok(encoded)) return -1;
        if (req_body_len && !req_body) return -1;
        if (add_text(&hdr, &hlen, &hcap, method) != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, " ") != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, encoded) != 0) goto done;
    }
    if (add_text(&hdr, &hlen, &hcap, " HTTP/1.0\r\n") != 0) goto done;
    n = snprintf(scratch, sizeof scratch, "Host: 127.0.0.1:%d\r\n", port);
    if (n < 0 || (size_t)n >= sizeof scratch || add_text(&hdr, &hlen, &hcap, scratch) != 0) goto done;
    if (add_text(&hdr, &hlen, &hcap, "Connection: close\r\n") != 0) goto done;
    if (user_agent && user_agent[0] && header_safe(user_agent)) {
        if (add_text(&hdr, &hlen, &hcap, "User-Agent: ") != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, user_agent) != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, "\r\n") != 0) goto done;
    } else if (add_text(&hdr, &hlen, &hcap, "User-Agent: ClassicCarsStudio\r\n") != 0) {
        goto done;
    }
    if (content_type && content_type[0] && header_safe(content_type)) {
        if (add_text(&hdr, &hlen, &hcap, "Content-Type: ") != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, content_type) != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, "\r\n") != 0) goto done;
    }
    if (cookie && cookie[0] && header_safe(cookie)) {
        if (add_text(&hdr, &hlen, &hcap, "Cookie: ") != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, cookie) != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, "\r\n") != 0) goto done;
    }
    if (preview_document_uri && preview_document_uri[0] && header_safe(preview_document_uri)) {
        if (add_text(&hdr, &hlen, &hcap, "X-Preview-Document-Uri: ") != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, preview_document_uri) != 0) goto done;
        if (add_text(&hdr, &hlen, &hcap, "\r\n") != 0) goto done;
    }
    if (uri && (strstr(uri, "__draft=1") || strstr(uri, "__preview_draft=1"))) {
        if (add_text(&hdr, &hlen, &hcap, "X-Preview-Draft: 1\r\n") != 0) goto done;
    }
    if (req_body_len) {
        n = snprintf(scratch, sizeof scratch, "Content-Length: %zu\r\n", req_body_len);
        if (n < 0 || (size_t)n >= sizeof scratch || add_text(&hdr, &hlen, &hcap, scratch) != 0) {
            goto done;
        }
    }
    if (add_text(&hdr, &hlen, &hcap, "\r\n") != 0) goto done;
    fd = connect_php(port);
    if (fd < 0) goto done;
    if (send_all((sock_t)fd, hdr, hlen) != 0) goto done;
    if (req_body_len && send_all((sock_t)fd, req_body, req_body_len) != 0) goto done;
    if (read_response((sock_t)fd, status, content_type_out, content_type_out_sz,
                      location, location_sz, extra, extra_sz, out_body, out_len) != 0) {
        goto done;
    }
    rc = 0;
done:
    if (fd >= 0) sock_close((sock_t)fd);
    free(hdr);
    return rc;
}

int proxy_to_php(const HttpRequest *req, HttpResponse *res, const char *preview_document_uri) {
    int status = 0;
    char ctype[128];
    char location[1024];
    char extra[512];
    char *body = NULL;
    size_t body_len = 0;
    const char *uri;
    const char *method;
    int rc;
    if (!res) return -1;
    ctype[0] = '\0';
    location[0] = '\0';
    extra[0] = '\0';
    if (!req) {
        http_response_set_error(res, 400, "Bad request");
        return 0;
    }
    uri = req->raw_uri[0] ? req->raw_uri : req->path;
    method = req->method[0] ? req->method : "GET";
    proxy_lock();
    rc = proxy_exchange(method, uri, req->content_type, req->cookie, req->user_agent,
                        preview_document_uri, req->body, req->body_len,
                        &status, ctype, sizeof ctype, location, sizeof location,
                        extra, sizeof extra, &body, &body_len);
    proxy_unlock();
    if (rc != 0) {
        free(body);
        http_response_set_error(res, 502,
                                settings_get_php_port() <= 0
                                    ? "PHP preview server is disabled"
                                    : "PHP preview server is unavailable");
        return 0;
    }
    res->status = status > 0 ? status : 200;
    copy_trunc(res->content_type, sizeof res->content_type, ctype);
    copy_trunc(res->location, sizeof res->location, location);
    copy_trunc(res->extra_headers, sizeof res->extra_headers, extra);
    if (body) {
        http_response_set_body(res, body, body_len);
        free(body);
    } else {
        http_response_set_body(res, "", 0);
    }
    return 0;
}

int proxy_php_get(const char *virtual_url, const char *preview_document_uri,
                  int *status, char *content_type, size_t content_type_sz,
                  char **body, size_t *body_len) {
    int st = 0;
    char ctype[128];
    char *got = NULL;
    size_t got_len = 0;
    int rc;
    ctype[0] = '\0';
    if (body) *body = NULL;
    if (body_len) *body_len = 0;
    if (status) *status = 0;
    if (!virtual_url) return -1;
    proxy_lock();
    rc = proxy_exchange("GET", virtual_url, NULL, NULL, NULL, preview_document_uri,
                        NULL, 0, &st, ctype, sizeof ctype, NULL, 0, NULL, 0,
                        &got, &got_len);
    proxy_unlock();
    if (rc != 0) {
        free(got);
        return -1;
    }
    if (status) *status = st;
    if (content_type && content_type_sz) copy_trunc(content_type, content_type_sz, ctype);
    if (body) {
        *body = got;
    } else {
        free(got);
    }
    if (body_len) *body_len = got_len;
    return 0;
}
