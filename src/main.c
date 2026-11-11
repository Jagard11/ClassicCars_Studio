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
#include "config.h"
#include "http.h"
#include "router.h"
#include "settings.h"
#include "ssi.h"
#include "proxy.h"
#include "version.h"
#include "window.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
#include <direct.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
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

static char g_root[PATH_MAX];

#ifdef _WIN32
static PROCESS_INFORMATION g_php_pi;
static HANDLE g_php_job = NULL;
static int g_php_alive = 0;
#else
static pid_t g_php_pid = -1;
#endif
static long long g_restart_after = 0;
static int g_php_fail_streak = 0;
static long long g_php_fail_window = 0;
static char g_php_docroot[PATH_MAX];
static int g_php_enabled = 0;

#define PHP_PORT_MIN 8771
#define PHP_PORT_MAX 8850
#define PHP_RESTART_COOLDOWN_MS 1000

typedef struct App {
    const char *root;
    int php_port;
    int studio_port;
    int php_on;
} App;

static int g_checks = 0;
static int g_failed = 0;

static void expect_true(int cond, const char *msg) {
    g_checks++;
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failed++;
    }
}

static long long now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
#endif
}

static void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
#endif
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

static int path_join(char *dst, size_t dst_sz, const char *a, const char *b) {
    size_t na;
    size_t nb;
    if (!dst || !a || !b) return -1;
    na = strlen(a);
    while (na > 0 && (a[na - 1] == '/' || a[na - 1] == '\\')) na--;
    while (*b == '/' || *b == '\\') b++;
    nb = strlen(b);
    if (na + 1 + nb + 1 > dst_sz) return -1;
    memcpy(dst, a, na);
    dst[na] = '/';
    memcpy(dst + na + 1, b, nb + 1);
    return 0;
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void php_stop(void) {
#ifdef _WIN32
    if (!g_php_alive) return;
    TerminateProcess(g_php_pi.hProcess, 1);
    WaitForSingleObject(g_php_pi.hProcess, 2000);
    CloseHandle(g_php_pi.hProcess);
    CloseHandle(g_php_pi.hThread);
    memset(&g_php_pi, 0, sizeof g_php_pi);
    g_php_alive = 0;
#else
    pid_t pid = g_php_pid;
    int i;
    if (pid <= 0) return;
    g_php_pid = -1;
    kill(pid, SIGTERM);
    for (i = 0; i < 20; i++) {
        int status = 0;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid || (r < 0 && errno == ECHILD)) return;
        sleep_ms(100);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
#endif
}

static int php_reap_if_exited(int *status_out) {
#ifdef _WIN32
    DWORD code = 0;
    if (!g_php_alive) return 0;
    if (WaitForSingleObject(g_php_pi.hProcess, 0) != WAIT_OBJECT_0) return 0;
    GetExitCodeProcess(g_php_pi.hProcess, &code);
    CloseHandle(g_php_pi.hProcess);
    CloseHandle(g_php_pi.hThread);
    memset(&g_php_pi, 0, sizeof g_php_pi);
    g_php_alive = 0;
    if (status_out) *status_out = (int)code;
    return 1;
#else
    int status = 0;
    pid_t r;
    if (g_php_pid <= 0) return 0;
    r = waitpid(g_php_pid, &status, WNOHANG);
    if (r == 0) return 0;
    if (r < 0 && errno == EINTR) return 0;
    if (r == g_php_pid || (r < 0 && errno == ECHILD)) {
        g_php_pid = -1;
        if (status_out) *status_out = status;
        return 1;
    }
    return 0;
#endif
}

static int php_child_alive(void) {
#ifdef _WIN32
    if (!g_php_alive) return 0;
    return WaitForSingleObject(g_php_pi.hProcess, 0) == WAIT_TIMEOUT;
#else
    if (g_php_pid <= 0) return 0;
    if (kill(g_php_pid, 0) == 0 || errno == EPERM) return 1;
    return 0;
#endif
}

static int php_spawn(const char *root, int port) {
    char d_arg[PATH_MAX + 64];
    char prepend[PATH_MAX];
    char addr[64];
    const char *prefix = "auto_prepend_file=";
    const char *install = settings_get_studio_home();
    const char *prepend_root;
    size_t pl = strlen(prefix);
    size_t rl;
    int n;
    if (!root || !root[0]) return -1;
    /* -t follows the website. The prepend file stays in the studio install. */
    prepend_root = (install && install[0]) ? install : root;
    prepend[0] = '\0';
    if (settings_install_path(prepend_root, "preview/prepend.php", prepend, sizeof prepend) != 0) return -1;
    rl = strlen(prepend);
    /* A live child is left alone. studio_restart_php_for_root stops it first. */
    if (!php_reap_if_exited(NULL) && php_child_alive()) return 0;
    if (pl + rl + 1 > sizeof d_arg) return -1;
    memcpy(d_arg, prefix, pl);
    memcpy(d_arg + pl, prepend, rl + 1);
    n = snprintf(addr, sizeof addr, "127.0.0.1:%d", port);
    if (n < 0 || (size_t)n >= sizeof addr) return -1;
#ifdef _WIN32
    {
        char cmd[8192];
        char quoted_d[PATH_MAX + 80];
        char quoted_root[PATH_MAX];
        STARTUPINFOA si;
        size_t o = 0;
        if (strchr(root, '"') || strchr(d_arg, '"')) return -1;
        if (strlen(d_arg) + 3 >= sizeof quoted_d || strlen(root) + 3 >= sizeof quoted_root) return -1;
        quoted_d[0] = '"';
        memcpy(quoted_d + 1, d_arg, strlen(d_arg));
        quoted_d[1 + strlen(d_arg)] = '"';
        quoted_d[2 + strlen(d_arg)] = '\0';
        quoted_root[0] = '"';
        memcpy(quoted_root + 1, root, strlen(root));
        quoted_root[1 + strlen(root)] = '"';
        quoted_root[2 + strlen(root)] = '\0';
        o = 0;
#define APPEND(s) do { \
            size_t _n = strlen(s); \
            if (o + _n + 1 > sizeof cmd) return -1; \
            memcpy(cmd + o, s, _n); \
            o += _n; \
            cmd[o] = '\0'; \
        } while (0)
        APPEND("php -d ");
        APPEND(quoted_d);
        APPEND(" -S ");
        APPEND(addr);
        APPEND(" -t ");
        APPEND(quoted_root);
#undef APPEND
        memset(&si, 0, sizeof si);
        si.cb = sizeof si;
        memset(&g_php_pi, 0, sizeof g_php_pi);
        si.dwFlags |= STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW, NULL, NULL,
                            &si, &g_php_pi)) {
            fprintf(stderr, "classiccars-studio: CreateProcess php failed (%lu)\n", GetLastError());
            return -1;
        }
        if (!g_php_job) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info;
            g_php_job = CreateJobObjectA(NULL, NULL);
            if (g_php_job) {
                memset(&info, 0, sizeof info);
                info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(g_php_job, JobObjectExtendedLimitInformation, &info, sizeof info);
            }
        }
        if (g_php_job) AssignProcessToJobObject(g_php_job, g_php_pi.hProcess);
        g_php_alive = 1;
        return 0;
    }
#else
    {
        pid_t pid = fork();
        if (pid < 0) return -1;
        if (pid == 0) {
            setpgid(0, 0);
#ifdef __linux__
            /* SIGKILL of studio must take php -S with it, so the port cannot stay orphaned. */
            if (prctl(PR_SET_PDEATHSIG, SIGTERM) == 0 && getppid() == 1) _exit(1);
#endif
            execlp("php", "php", "-d", d_arg, "-S", addr, "-t", root, (char *)NULL);
            _exit(127);
        }
        setpgid(pid, pid);
        g_php_pid = pid;
        return 0;
    }
#endif
}

static int php_is_available(void) {
#ifdef _WIN32
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char cmd[] = "php --version";
    DWORD code = 1;
    HANDLE nul;
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (nul != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = nul;
        si.hStdError = nul;
    }
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        return 0;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    return code == 0;
#else
    pid_t pid = fork();
    int status = 0;
    if (pid < 0) return 0;
    if (pid == 0) {
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            if (fd > STDERR_FILENO) close(fd);
        }
        execlp("php", "php", "--version", (char *)NULL);
        _exit(127);
    }
    if (waitpid(pid, &status, 0) < 0) return 0;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

static int wait_for_php(int port, int timeout_ms) {
    long long start = now_ms();
    while (now_ms() - start < timeout_ms) {
        int status = 0;
        if (php_reap_if_exited(&status) || !php_child_alive()) {
            fprintf(stderr,
                    "classiccars-studio: php exited before it accepted connections (status %d)\n",
                    status);
            return -1;
        }
        /* A foreign listener on this port is not our child. */
        if (http_try_connect("127.0.0.1", port, 100) == 0 && php_child_alive() &&
            !php_reap_if_exited(&status)) {
            return 0;
        }
        if (!php_child_alive()) {
            fprintf(stderr,
                    "classiccars-studio: php exited before it accepted connections (status %d)\n",
                    status);
            return -1;
        }
        sleep_ms(50);
    }
    return -1;
}

static int loopback_port_free(int port) {
    struct sockaddr_in addr;
    int rc;
#ifdef _WIN32
    SOCKET fd;
#else
    int fd;
#endif
    if (port < 1 || port > 65535) return 0;
    if (http_try_connect("127.0.0.1", port, 50) == 0) return 0;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr) != 1) return 0;
#ifdef _WIN32
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) return 0;
    rc = bind(fd, (struct sockaddr *)&addr, sizeof addr);
    closesocket(fd);
#else
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    rc = bind(fd, (struct sockaddr *)&addr, sizeof addr);
    close(fd);
#endif
    return rc == 0;
}

/* Prefer `preferred` when it can be bound. Otherwise walk 8771..8850. */
static int choose_php_port(int preferred, int avoid) {
    int port;
    int start;
    if (preferred < 1 || preferred > 65535) preferred = PHP_PORT_MIN;
    if (preferred != avoid && loopback_port_free(preferred)) return preferred;
    start = preferred;
    if (start < PHP_PORT_MIN || start > PHP_PORT_MAX) start = PHP_PORT_MIN;
    for (port = start; port <= PHP_PORT_MAX; port++) {
        if (port == avoid || port == preferred) continue;
        if (loopback_port_free(port)) return port;
    }
    for (port = PHP_PORT_MIN; port < start; port++) {
        if (port == avoid) continue;
        if (loopback_port_free(port)) return port;
    }
    return -1;
}

static void supervise_php(void *ud) {
    App *app = ud;
    int status = 0;
    long long now;
    int saw_exit;
    int alt;
    if (!app || !app->php_on) return;
    saw_exit = php_reap_if_exited(&status);
    if (!saw_exit && php_child_alive()) return;
    if (!saw_exit && g_restart_after == 0) return;
    now = now_ms();
    if (now < g_restart_after) return;
    if (g_php_fail_window == 0 || now - g_php_fail_window > 5000) {
        g_php_fail_window = now;
        g_php_fail_streak = 0;
    }
    g_php_fail_streak++;
    if (g_php_fail_streak > 3) {
        alt = choose_php_port(app->php_port >= PHP_PORT_MAX ? PHP_PORT_MIN : app->php_port + 1,
                              app->studio_port);
        if (alt > 0 && alt != app->php_port) {
            fprintf(stderr,
                    "classiccars-studio: php exited %d times quickly; using 127.0.0.1:%d\n",
                    g_php_fail_streak, alt);
            app->php_port = alt;
            settings_set_php_port(alt);
        }
        g_php_fail_streak = 0;
        g_php_fail_window = now;
    }
    if (saw_exit) {
        fprintf(stderr, "classiccars-studio: php exited (%d), restarting\n", status);
    } else {
        fprintf(stderr, "classiccars-studio: php is not running, restarting\n");
    }
    g_restart_after = now + PHP_RESTART_COOLDOWN_MS;
    if (php_spawn(g_php_docroot[0] ? g_php_docroot : app->root, app->php_port) != 0) return;
    if (wait_for_php(app->php_port, 1000) != 0) php_stop();
}

int studio_restart_php_for_root(const char *new_root) {
    int port;
    int attempt;
    if (!new_root || !new_root[0]) return -1;
    if (copy_fit(g_php_docroot, sizeof g_php_docroot, new_root) != 0) return -1;
    if (!g_php_enabled) return 0;
    port = settings_get_php_port();
    if (port <= 0) return 0;
    for (attempt = 0; attempt < 2; attempt++) {
        php_stop();
        sleep_ms(attempt == 0 ? 50 : 200);
        if (php_spawn(g_php_docroot, port) != 0) continue;
        if (wait_for_php(port, 3000) == 0) return 0;
        php_stop();
    }
    return -1;
}

static int on_request(const HttpRequest *req, HttpResponse *res, void *ud) {
    App *app = ud;
    const char *root = settings_get_project_root();
    if (!root || !root[0]) root = app ? app->root : NULL;
    return router_handle(req, res, root);
}

static void test_http_units(void) {
    char dst[64];
    char small[2];
    char val[64];
    HttpResponse res;
    HttpRequest req;
    const char *post =
        "POST /api/x?y=1 HTTP/1.1\r\n"
        "Host: 127.0.0.1\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 2\r\n"
        "Cookie: a=b\r\n"
        "User-Agent: t\r\n"
        "\r\n"
        "{}";
    const char *chunked = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n";
    const char *huge = "GET / HTTP/1.1\r\nContent-Length: 999999999\r\n\r\n";
    const char *short_body = "POST / HTTP/1.1\r\nContent-Length: 4\r\n\r\nno";
    const char *encoded = "GET /sales/a%20b HTTP/1.1\r\nHost: x\r\n\r\n";

    expect_true(http_url_decode("a%20b%2Fc", dst, sizeof dst) == 0 && strcmp(dst, "a b/c") == 0,
                "decode space and slash");
    expect_true(http_url_decode("100%25", dst, sizeof dst) == 0 && strcmp(dst, "100%") == 0,
                "decode percent");
    expect_true(http_url_decode("a+b", dst, sizeof dst) == 0 && strcmp(dst, "a+b") == 0,
                "plus stays plus in paths");
    expect_true(http_url_decode("a%00b", dst, sizeof dst) != 0, "reject NUL");
    expect_true(http_url_decode("abc", small, sizeof small) != 0, "truncate");

    expect_true(http_query_param("a=1&preview=1&b=2", "preview", val, sizeof val) == 1 &&
                    strcmp(val, "1") == 0,
                "preview param");
    expect_true(http_query_param("preview=1&name=a%2Bb+c", "name", val, sizeof val) == 1 &&
                    strcmp(val, "a+b c") == 0,
                "query decode");
    expect_true(http_query_param("previewX=1", "preview", val, sizeof val) == 0, "key boundary");
    expect_true(http_query_param("a=", "a", val, sizeof val) == 1 && val[0] == '\0', "empty value");
    expect_true(http_query_param("", "a", val, sizeof val) == 0, "missing key");

    http_response_init(&res);
    expect_true(http_response_set_json(&res, 201, "{\"ok\":true}") == 0, "set json");
    expect_true(res.status == 201 && res.body && strcmp(res.body, "{\"ok\":true}") == 0, "json body");
    expect_true(strcmp(res.content_type, "application/json; charset=UTF-8") == 0, "json type");
    http_response_free(&res);

    http_response_init(&res);
    expect_true(http_response_set_error(&res, 400, "say \"hi\"\n") == 0, "set error");
    expect_true(res.status == 400 && res.body &&
                    strcmp(res.body, "{\"error\":\"say \\\"hi\\\"\\n\"}") == 0,
                "escaped error");
    http_response_free(&res);

    http_request_init(&req);
    expect_true(http_parse_request_buffer(post, strlen(post), &req) == HTTP_PARSE_OK, "parse post");
    expect_true(strcmp(req.method, "POST") == 0 && strcmp(req.path, "/api/x") == 0, "post target");
    expect_true(strcmp(req.query, "y=1") == 0 && req.body_len == 2 && memcmp(req.body, "{}", 2) == 0,
                "post body");
    expect_true(strcmp(req.content_type, "application/json") == 0, "post type");
    expect_true(strcmp(req.cookie, "a=b") == 0 && strcmp(req.user_agent, "t") == 0, "post headers");
    http_request_free(&req);

    http_request_init(&req);
    expect_true(http_parse_request_buffer(chunked, strlen(chunked), &req) == HTTP_PARSE_NO_LENGTH,
                "reject chunked request");
    http_request_free(&req);

    http_request_init(&req);
    expect_true(http_parse_request_buffer(huge, strlen(huge), &req) == HTTP_PARSE_TOO_LARGE,
                "reject huge body");
    http_request_free(&req);

    http_request_init(&req);
    expect_true(http_parse_request_buffer(short_body, strlen(short_body), &req) == HTTP_PARSE_BAD,
                "incomplete body");
    http_request_free(&req);

    http_request_init(&req);
    expect_true(http_parse_request_buffer(encoded, strlen(encoded), &req) == HTTP_PARSE_OK &&
                    strcmp(req.path, "/sales/a b") == 0,
                "decode request path");
    http_request_free(&req);

    settings_set_php_port(4242);
    expect_true(settings_get_php_port() == 4242, "php port");
    settings_set_php_port(8771);
}

#ifndef _WIN32
static int write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb");
    size_t n;
    if (!f) return -1;
    n = strlen(text);
    if (n && fwrite(text, 1, n, f) != n) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}

static int mkdir_p(const char *path) {
    char tmp[PATH_MAX];
    size_t len;
    size_t i;
    if (copy_fit(tmp, sizeof tmp, path) != 0) return -1;
    len = strlen(tmp);
    for (i = 1; i < len; i++) {
        if (tmp[i] != '/') continue;
        tmp[i] = '\0';
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
        tmp[i] = '/';
    }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) return -1;
    return 0;
}

static void rm_rf(const char *path) {
    DIR *d;
    struct dirent *de;
    if (!path || strncmp(path, "/tmp/", 5) != 0) return;
    d = opendir(path);
    if (!d) {
        unlink(path);
        return;
    }
    while ((de = readdir(d)) != NULL) {
        char child[PATH_MAX];
        struct stat st;
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (path_join(child, sizeof child, path, de->d_name) != 0) continue;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) rm_rf(child);
        else unlink(child);
    }
    closedir(d);
    rmdir(path);
}

typedef struct Fake {
    int listen_fd;
    const char *response;
    char request[16384];
    size_t request_len;
    int failed;
} Fake;

static void *fake_thread(void *arg) {
    Fake *f = arg;
    struct pollfd pfd;
    int client;
    char buf[16384];
    size_t len = 0;
    size_t header_len = 0;
    long content_length = 0;
    struct timeval tv;
    pfd.fd = f->listen_fd;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 5000) <= 0) {
        f->failed = 1;
        close(f->listen_fd);
        f->listen_fd = -1;
        return NULL;
    }
    client = accept(f->listen_fd, NULL, NULL);
    close(f->listen_fd);
    f->listen_fd = -1;
    if (client < 0) {
        f->failed = 1;
        return NULL;
    }
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    while (len + 1 < sizeof buf) {
        ssize_t n = recv(client, buf + len, sizeof buf - 1 - len, 0);
        char *end;
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (n == 0) break;
        len += (size_t)n;
        buf[len] = '\0';
        if (header_len == 0) {
            end = strstr(buf, "\r\n\r\n");
            if (!end) continue;
            header_len = (size_t)(end - buf) + 4;
            end = strstr(buf, "Content-Length:");
            if (end && (size_t)(end - buf) < header_len) {
                content_length = strtol(end + strlen("Content-Length:"), NULL, 10);
                if (content_length < 0) content_length = 0;
            }
        }
        if (header_len && len >= header_len + (size_t)content_length) break;
    }
    if (len >= sizeof f->request) len = sizeof f->request - 1;
    memcpy(f->request, buf, len);
    f->request[len] = '\0';
    f->request_len = len;
    if (f->response) {
        size_t left = strlen(f->response);
        size_t off = 0;
        while (off < left) {
            ssize_t n = send(client, f->response + off, left - off, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) break;
            off += (size_t)n;
        }
    }
    close(client);
    return NULL;
}

static int listen_loopback(int *port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    socklen_t len;
    int one = 1;
    if (fd < 0) return -1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    len = sizeof addr;
    if (getsockname(fd, (struct sockaddr *)&addr, &len) != 0) {
        close(fd);
        return -1;
    }
    *port = (int)ntohs(addr.sin_port);
    return fd;
}

static int with_fake(const char *response, const HttpRequest *req, const char *preview,
                     HttpResponse *res, char *captured, size_t captured_sz) {
    Fake fake;
    pthread_t thread;
    int port = 0;
    int saved = settings_get_php_port();
    int rc;
    memset(&fake, 0, sizeof fake);
    fake.response = response;
    fake.listen_fd = listen_loopback(&port);
    if (fake.listen_fd < 0) return -1;
    if (pthread_create(&thread, NULL, fake_thread, &fake) != 0) {
        close(fake.listen_fd);
        return -1;
    }
    settings_set_php_port(port);
    rc = proxy_to_php(req, res, preview);
    pthread_join(thread, NULL);
    settings_set_php_port(saved);
    if (captured && captured_sz) {
        captured[0] = '\0';
        copy_fit(captured, captured_sz, fake.request);
    }
    return fake.failed ? -1 : rc;
}

static void test_proxy(void) {
    HttpRequest req;
    HttpResponse res;
    char seen[16384];
    const char *created =
        "HTTP/1.0 201 Created\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Location: /next\r\n"
        "Set-Cookie: a=b\r\n"
        "Content-Length: 4\r\n"
        "Connection: close\r\n"
        "\r\n"
        "BODY";
    const char *chunked =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Transfer-Encoding: chunked\r\n"
        "Connection: close\r\n"
        "\r\n"
        "5\r\n"
        "Hello\r\n"
        "0\r\n"
        "\r\n";
    const char *eof_body =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Connection: close\r\n"
        "\r\n"
        "EOF";
    const char *log =
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 3\r\n"
        "Connection: close\r\n"
        "\r\n"
        "LOG";
    char *spliced = NULL;
    size_t slen = 0;
    Fake fake;
    pthread_t thread;
    int port = 0;
    const char *page = "A<!--#include virtual=\"/php/logview.php?embed=1\"-->B";

    http_request_init(&req);
    memcpy(req.method, "POST", 5);
    memcpy(req.raw_uri, "/form.php?x=1", 14);
    memcpy(req.content_type, "application/json", 17);
    memcpy(req.cookie, "sid=1", 6);
    memcpy(req.user_agent, "TestAgent", 10);
    req.body = malloc(2);
    expect_true(req.body != NULL, "post body alloc");
    if (req.body) memcpy(req.body, "hi", 2);
    req.body_len = 2;
    http_response_init(&res);
    expect_true(with_fake(created, &req, "/articles/a.html", &res, seen, sizeof seen) == 0,
                "proxy post");
    expect_true(res.status == 201 && strcmp(res.location, "/next") == 0, "proxy status");
    expect_true(res.body && strcmp(res.body, "BODY") == 0, "proxy body");
    expect_true(strstr(res.content_type, "text/html") != NULL, "proxy type");
    expect_true(strstr(res.extra_headers, "Set-Cookie: a=b") != NULL, "proxy cookie");
    expect_true(strstr(seen, "POST /form.php?x=1 HTTP/1.0") != NULL, "forwarded target");
    expect_true(strstr(seen, "Content-Type: application/json") != NULL, "forwarded type");
    expect_true(strstr(seen, "Cookie: sid=1") != NULL, "forwarded cookie");
    expect_true(strstr(seen, "User-Agent: TestAgent") != NULL, "forwarded agent");
    expect_true(strstr(seen, "X-Preview-Document-Uri: /articles/a.html") != NULL, "preview header");
    expect_true(strstr(seen, "\r\n\r\nhi") != NULL, "forwarded body");
    http_response_free(&res);
    http_request_free(&req);

    http_request_init(&req);
    memcpy(req.method, "GET", 4);
    memcpy(req.raw_uri, "/chunked.php", 13);
    memcpy(req.path, "/chunked.php", 13);
    http_response_init(&res);
    expect_true(with_fake(chunked, &req, NULL, &res, NULL, 0) == 0, "proxy chunked");
    expect_true(res.status == 200 && res.body && strcmp(res.body, "Hello") == 0, "chunked body");
    http_response_free(&res);
    http_request_free(&req);

    http_request_init(&req);
    memcpy(req.method, "GET", 4);
    memcpy(req.raw_uri, "/eof.php", 9);
    http_response_init(&res);
    expect_true(with_fake(eof_body, &req, NULL, &res, NULL, 0) == 0, "proxy eof");
    expect_true(res.body && strcmp(res.body, "EOF") == 0, "eof body");
    http_response_free(&res);
    http_request_free(&req);

    http_request_init(&req);
    memcpy(req.method, "GET", 4);
    memcpy(req.raw_uri, "/sales/1901thomas1&halfhp/index.php?x=1&y=2", 44);
    memcpy(req.path, "/sales/1901thomas1&halfhp/index.php", 36);
    http_response_init(&res);
    expect_true(with_fake(eof_body, &req, NULL, &res, seen, sizeof seen) == 0, "proxy amp path");
    expect_true(strstr(seen, "GET /sales/1901thomas1%26halfhp/index.php?x=1&y=2 HTTP/1.0") != NULL,
                "amp path encoded, query kept");
    http_response_free(&res);
    http_request_free(&req);

    http_request_init(&req);
    memcpy(req.method, "GET", 4);
    memcpy(req.raw_uri, "/sales/1901thomas1%26halfhp/index.php", 38);
    memcpy(req.path, "/sales/1901thomas1&halfhp/index.php", 36);
    http_response_init(&res);
    expect_true(with_fake(eof_body, &req, NULL, &res, seen, sizeof seen) == 0, "proxy encoded amp");
    expect_true(strstr(seen, "GET /sales/1901thomas1%26halfhp/index.php HTTP/1.0") != NULL,
                "pct26 not double encoded");
    http_response_free(&res);
    http_request_free(&req);

    memset(&fake, 0, sizeof fake);
    fake.response = log;
    fake.listen_fd = listen_loopback(&port);
    expect_true(fake.listen_fd >= 0 && pthread_create(&thread, NULL, fake_thread, &fake) == 0,
                "ssi fake");
    if (fake.listen_fd >= 0) {
        char php_root[] = "/tmp/cc-ssi-php-XXXXXX";
        char php_dir[PATH_MAX];
        char php_file[PATH_MAX];
        settings_set_php_port(port);
        if (!mkdtemp(php_root) ||
            path_join(php_dir, sizeof php_dir, php_root, "php") != 0 ||
            mkdir_p(php_dir) != 0 ||
            path_join(php_file, sizeof php_file, php_root, "php/logview.php") != 0 ||
            write_file(php_file, "<?php echo 'LOG';\n") != 0) {
            expect_true(0, "ssi php fixture");
        } else {
            expect_true(ssi_splice(page, strlen(page), php_root,
                                   "/articles/greenwichconcours2026/greenwichconcours2026.html",
                                   &spliced, &slen) == 0,
                        "ssi php splice");
            expect_true(spliced && strcmp(spliced, "ALOGB") == 0, "ssi php body");
        }
        pthread_join(thread, NULL);
        expect_true(strstr(fake.request, "GET /php/logview.php?embed=1") != NULL, "ssi target");
        expect_true(strstr(fake.request,
                           "X-Preview-Document-Uri: /articles/greenwichconcours2026/greenwichconcours2026.html") != NULL,
                    "ssi document uri");
        free(spliced);
        rm_rf(php_root);
    }
    settings_set_php_port(0);
}

static int set_req(HttpRequest *req, const char *method, const char *path, const char *query) {
    size_t ml = strlen(method);
    size_t pl = strlen(path);
    size_t ql = query ? strlen(query) : 0;
    if (ml >= sizeof req->method || pl >= sizeof req->path || ql >= sizeof req->query) return -1;
    if (pl + (ql ? ql + 1 : 0) >= sizeof req->raw_uri) return -1;
    memcpy(req->method, method, ml + 1);
    memcpy(req->path, path, pl + 1);
    if (ql) {
        memcpy(req->query, query, ql + 1);
        memcpy(req->raw_uri, path, pl);
        req->raw_uri[pl] = '?';
        memcpy(req->raw_uri + pl + 1, query, ql + 1);
    } else {
        req->query[0] = '\0';
        memcpy(req->raw_uri, path, pl + 1);
    }
    return 0;
}

static void check_api_body(const char *root, const char *method, const char *path, const char *body,
                           int status, const char *body_has, const char *label) {
    HttpRequest req;
    HttpResponse res;
    http_request_init(&req);
    http_response_init(&res);
    expect_true(set_req(&req, method, path, NULL) == 0, label);
    if (body) {
        req.body = strdup(body);
        req.body_len = req.body ? strlen(req.body) : 0;
        expect_true(req.body != NULL, label);
    }
    expect_true(router_handle(&req, &res, root) == 0, label);
    expect_true(res.status == status, label);
    if (body_has) expect_true(res.body && strstr(res.body, body_has) != NULL, label);
    http_response_free(&res);
    http_request_free(&req);
}

static void check_route(const char *root, const char *method, const char *path, const char *query,
                        int status, const char *location, const char *body_has, const char *body_lacks,
                        const char *type_has, const char *label) {
    HttpRequest req;
    HttpResponse res;
    http_request_init(&req);
    http_response_init(&res);
    expect_true(set_req(&req, method, path, query) == 0, label);
    expect_true(router_handle(&req, &res, root) == 0, label);
    expect_true(res.status == status, label);
    if (location) expect_true(strcmp(res.location, location) == 0, label);
    if (body_has) expect_true(res.body && strstr(res.body, body_has) != NULL, label);
    if (body_lacks) expect_true(!res.body || strstr(res.body, body_lacks) == NULL, label);
    if (type_has) expect_true(strstr(res.content_type, type_has) != NULL, label);
    http_response_free(&res);
    http_request_free(&req);
}

static void check_api_query(const char *root, const char *path, const char *query, int status,
                            const char *body_has, const char *body_lacks, const char *label) {
    HttpRequest req;
    HttpResponse res;
    http_request_init(&req);
    http_response_init(&res);
    expect_true(set_req(&req, "GET", path, query) == 0, label);
    expect_true(router_handle(&req, &res, root) == 0, label);
    expect_true(res.status == status, label);
    if (body_has) expect_true(res.body && strstr(res.body, body_has) != NULL, label);
    if (body_lacks) expect_true(!res.body || strstr(res.body, body_lacks) == NULL, label);
    http_response_free(&res);
    http_request_free(&req);
}

static int smoke_handle(const HttpRequest *req, HttpResponse *res, void *ud) {
    return router_handle(req, res, (const char *)ud);
}

typedef struct Smoke {
    HttpServer *srv;
    const char *root;
} Smoke;

static void *smoke_serve(void *arg) {
    Smoke *s = arg;
    http_server_serve(s->srv, smoke_handle, (void *)s->root, NULL, NULL);
    return NULL;
}

static int http_exchange(int port, const char *request, char *resp, size_t resp_sz) {
    int fd;
    struct sockaddr_in addr;
    size_t req_len = strlen(request);
    size_t sent = 0;
    size_t len = 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(fd);
        return -1;
    }
    while (sent < req_len) {
        ssize_t n = send(fd, request + sent, req_len - sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        sent += (size_t)n;
    }
    while (len + 1 < resp_sz) {
        ssize_t n = recv(fd, resp + len, resp_sz - 1 - len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return -1;
        }
        if (n == 0) break;
        len += (size_t)n;
    }
    resp[len] = '\0';
    close(fd);
    return 0;
}

static void test_tree(void) {
    char dir[] = "/tmp/cc-studio-XXXXXX";
    char path[PATH_MAX];
    char *out = NULL;
    size_t out_len = 0;
    char *theme = NULL;
    HttpServer *srv = NULL;
    Smoke smoke;
    pthread_t thread;
    char resp[8192];
    int i;
    const char *page = "1<!--#include virtual=\"/includes/bit.html\"-->2";
    const char *nested = "1<!--#include virtual=\"/inc/mid.html\"-->2";

    if (!mkdtemp(dir)) {
        expect_true(0, "mkdtemp");
        return;
    }
    expect_true(mkdir_p(path_join(path, sizeof path, dir, "studio/web") == 0 ? path : "") == 0 ||
                    path_join(path, sizeof path, dir, "studio/web") == 0,
                "mkdir web");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "includes");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "inc");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "style");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "sales/somecar");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "articles/story");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "data");
    mkdir_p(path);

    path_join(path, sizeof path, dir, "studio/web/index.html");
    write_file(path, "<h1>STUDIO</h1><!--#include virtual=\"/includes/bit.html\"-->");
    path_join(path, sizeof path, dir, "includes/bit.html");
    write_file(path, "BIT");
    path_join(path, sizeof path, dir, "hello.shtml");
    write_file(path, "A<!--#include virtual=\"/includes/bit.html\"-->B");
    path_join(path, sizeof path, dir, "style/app.css");
    write_file(path, "body{}");
    path_join(path, sizeof path, dir, "sales/somecar/index.php");
    write_file(path, "<?php echo 'CAR';");
    path_join(path, sizeof path, dir, "articles/story/index.html");
    write_file(path, "STORY");
    path_join(path, sizeof path, dir, "articles/story/pic.png");
    write_file(path, "PNG");
    path_join(path, sizeof path, dir, "articles/mediaonly");
    mkdir_p(path);
    path_join(path, sizeof path, dir, "articles/mediaonly/note.pdf");
    write_file(path, "PDFBYTES");
    path_join(path, sizeof path, dir, "articles/mediaonly/hero.jpg");
    write_file(path, "JPGDATA");
    path_join(path, sizeof path, dir, "articles/mediaonly/PI Mag Cover.JPG");
    write_file(path, "COVERJPG");
    path_join(path, sizeof path, dir, "articles/mediaonly/notes.doc");
    write_file(path, "DOCBYTES");
    path_join(path, sizeof path, dir, "articles/mediaonly/lifeboat.zip");
    write_file(path, "ZIP");
    path_join(path, sizeof path, dir, "articles/mediaonly/folderimage.jpg");
    write_file(path, "JUNK");
    path_join(path, sizeof path, dir, "articles/mediaonly/album.rss");
    write_file(path, "RSS");
    path_join(path, sizeof path, dir, "data/secret.txt");
    write_file(path, "SECRET");
    path_join(path, sizeof path, dir, "404.shtml");
    write_file(path, "MISSING <!--#include virtual=\"/includes/bit.html\"-->");
    path_join(path, sizeof path, dir, "inc/in.html");
    write_file(path, "X");
    path_join(path, sizeof path, dir, "inc/mid.html");
    write_file(path, "M<!--#include virtual=\"/inc/in.html\"-->");
    path_join(path, sizeof path, dir, "loop.html");
    write_file(path, "<!--#include virtual=\"/loop.html\"-->");
    for (i = 9; i >= 0; i--) {
        char name[32];
        char body[64];
        snprintf(name, sizeof name, "d%d.html", i);
        path_join(path, sizeof path, dir, name);
        if (i == 9) {
            write_file(path, "END");
        } else {
            snprintf(body, sizeof body, "<!--#include virtual=\"/d%d.html\"-->", i + 1);
            write_file(path, body);
        }
    }

    settings_set_php_port(0);
    expect_true(ssi_splice(page, strlen(page), dir, "/hello.shtml", &out, &out_len) == 0 &&
                    out && strcmp(out, "1BIT2") == 0,
                "static include");
    free(out);
    out = NULL;
    expect_true(ssi_splice(nested, strlen(nested), dir, "/page.html", &out, &out_len) == 0 &&
                    out && strcmp(out, "1MX2") == 0,
                "nested include");
    free(out);
    out = NULL;
    path_join(path, sizeof path, dir, "d0.html");
    {
        char *file = NULL;
        size_t flen = 0;
        expect_true(http_read_file(path, &file, &flen, 1024) == 0, "read d0");
        expect_true(ssi_splice(file, flen, dir, "/d0.html", &out, &out_len) == 0 &&
                        out && strcmp(out, "<!--#include virtual=\"/d9.html\"-->") == 0,
                    "depth limit");
        free(file);
        free(out);
        out = NULL;
    }
    path_join(path, sizeof path, dir, "loop.html");
    {
        char *file = NULL;
        size_t flen = 0;
        http_read_file(path, &file, &flen, 1024);
        alarm(5);
        expect_true(ssi_splice(file, flen, dir, "/loop.html", &out, &out_len) == 0 &&
                        out && strcmp(out, "<!--#include virtual=\"/loop.html\"-->") == 0,
                    "self include stops");
        alarm(0);
        free(file);
        free(out);
        out = NULL;
    }
    {
        const char *missing = "<!--#include virtual=\"/missing.html\"-->";
        expect_true(ssi_splice(missing, strlen(missing), dir, "/x.html", &out, &out_len) == 0 &&
                        out && strstr(out, "SSI") != NULL,
                    "missing include");
    }
    free(out);

    check_route(dir, "GET", "/", NULL, 302, "/__studio/", NULL, NULL, NULL, "root studio");
    check_route(dir, "GET", "/", "preview=1", 302, "/sales/index.php", NULL, NULL, NULL, "preview");
    check_route(dir, "GET", "/", "a=1&preview=1", 302, "/sales/index.php", NULL, NULL, NULL,
                "preview among params");
    check_route(dir, "GET", "/__studio", NULL, 302, "/__studio/", NULL, NULL, NULL, "studio slash");
    check_route(dir, "GET", "/__studio/", NULL, 200, NULL, "STUDIO", NULL, "text/html", "studio index");
    check_route(dir, "GET", "/__studio/", NULL, 200, NULL,
                "<!--#include virtual=\"/includes/bit.html\"-->", NULL, NULL, "studio raw ssi");
    check_route(dir, "GET", "/hello.shtml", NULL, 200, NULL, "ABITB", NULL, "text/html", "shtml");
    check_route(dir, "GET", "/style/app.css", NULL, 200, NULL, "body{}", NULL, "text/css", "css");
    {
        HttpRequest req;
        HttpResponse res;
        http_request_init(&req);
        http_response_init(&res);
        set_req(&req, "GET", "/style/app.css", NULL);
        router_handle(&req, &res, dir);
        expect_true(strstr(res.extra_headers, "Cache-Control: no-cache") != NULL, "css cache");
        http_response_free(&res);
        http_request_free(&req);
    }
    check_route(dir, "GET", "/sales/somecar", NULL, 301, "/sales/somecar/index.php", NULL, NULL, NULL,
                "sales slug");
    check_route(dir, "GET", "/sales/somecar", "ref=1", 301, "/sales/somecar/index.php?ref=1", NULL,
                NULL, NULL, "sales query");
    check_route(dir, "GET", "/sales/somecar/", NULL, 502, NULL, "disabled", NULL, "application/json",
                "sales php");
    check_route(dir, "GET", "/articles/story", NULL, 301, "/articles/story/", NULL, NULL, NULL,
                "article slash");
    check_route(dir, "GET", "/articles/story/", NULL, 200, NULL, "STORY", NULL, "text/html",
                "article index");
    check_route(dir, "GET", "/articles/story/pic.png", NULL, 200, NULL, "PNG", NULL, "image/png",
                "png");
    check_route(dir, "GET", "/articles/mediaonly/note.pdf", NULL, 200, NULL, "PDFBYTES", NULL, "application/pdf",
                "pdf");
    check_route(dir, "GET", "/articles/mediaonly/PI Mag Cover.JPG", NULL, 200, NULL, "COVERJPG", NULL,
                "image/jpeg", "jpg spaces");
    {
        HttpRequest req;
        HttpResponse res;
        const char *hit;
        const char *next;
        char window[4000];
        size_t n;
        http_request_init(&req);
        http_response_init(&res);
        expect_true(set_req(&req, "GET", "/__studio/api/tree", NULL) == 0, "media tree req");
        expect_true(router_handle(&req, &res, dir) == 0 && res.status == 200 && res.body, "media tree");
        hit = res.body ? strstr(res.body, "\"slug\":\"mediaonly\"") : NULL;
        next = hit ? strstr(hit + 18, "\"slug\":") : NULL;
        n = hit ? (next ? (size_t)(next - hit) : strlen(hit)) : 0;
        if (n >= sizeof window) n = sizeof window - 1;
        if (hit && n) memcpy(window, hit, n);
        if (n < sizeof window) window[n] = '\0';
        else window[sizeof window - 1] = '\0';
        expect_true(hit != NULL, "media tree slug");
        expect_true(hit && strstr(window, "\\/articles\\/mediaonly\\/note.pdf") != NULL, "media primary pdf");
        expect_true(hit && strstr(window, "hero.jpg") != NULL, "media tree jpg");
        expect_true(hit && strstr(window, "PI Mag Cover.JPG") != NULL, "media tree cover");
        expect_true(hit && strstr(window, "notes.doc") != NULL, "media tree doc");
        expect_true(hit && strstr(window, "\"type\":\"pdf\"") != NULL, "media pdf badge");
        expect_true(hit && strstr(window, "\"type\":\"jpg\"") != NULL, "media jpg badge");
        expect_true(hit && strstr(window, "\"issueCount\":0") != NULL, "media issue count");
        expect_true(hit && strstr(window, "lifeboat.zip") == NULL, "media skips lifeboat");
        expect_true(hit && strstr(window, "folderimage.jpg") == NULL, "media skips folderimage");
        expect_true(hit && strstr(window, "album.rss") == NULL, "media skips album rss");
        expect_true(hit && strstr(window, "mediaonly\\/mediaonly") == NULL, "media no phantom");
        http_response_free(&res);
        http_request_free(&req);
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2Fnote.pdf", 200,
                        "\"isMedia\":true", "PDFBYTES", "media pdf page");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2Fnote.pdf", 200,
                        "\"mediaKind\":\"pdf\"", NULL, "media pdf kind");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2Fnote.pdf", 200,
                        "\"errors\":[]", NULL, "media pdf errors");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2Fnote.pdf", 200,
                        "\"fileSize\":8", NULL, "media pdf size");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2FPI%20Mag%20Cover.JPG", 200,
                        "\"fileType\":\"image\"", "COVERJPG", "media jpg page");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2Fnotes.doc", 200,
                        "\"mediaKind\":\"document\"", "DOCBYTES", "media doc page");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fmediaonly%2Fmediaonly", 400,
                        NULL, NULL, "media phantom rejected");
        check_api_query(dir, "/__studio/api/page", "path=%2Farticles%2Fstory%2Fpic.png", 200,
                        "\"isMedia\":true", "\"content\":\"PNG\"", "story png page");
    }
    check_route(dir, "GET", "/data/secret.txt", NULL, 403, NULL, NULL, "SECRET", NULL, "data blocked");
    check_route(dir, "GET", "/admin", NULL, 403, NULL, NULL, NULL, NULL, "admin");
    check_route(dir, "GET", "/studio/state/theme.json", NULL, 403, NULL, NULL, NULL, NULL, "state");
    check_route(dir, "GET", "/studio/src/x", NULL, 403, NULL, NULL, NULL, NULL, "src");
    check_route(dir, "GET", "/studio/build/x", NULL, 403, NULL, NULL, NULL, NULL, "build");
    check_route(dir, "GET", "/../x", NULL, 403, NULL, NULL, NULL, NULL, "dotdot");
    check_route(dir, "GET", "/a\\b", NULL, 403, NULL, NULL, NULL, NULL, "backslash");
    check_route(dir, "GET", "/.htaccess", NULL, 403, NULL, NULL, NULL, NULL, "htaccess");
    check_route(dir, "GET", "/foo/.htaccess", NULL, 403, NULL, NULL, NULL, NULL, "nested htaccess");
    check_route(dir, "GET", "/index.html", NULL, 301, "/sales/index.php", NULL, NULL, NULL, "index alias");
    check_route(dir, "GET", "/sales/", NULL, 301, "/sales/index.php", NULL, NULL, NULL, "sales root");
    check_route(dir, "GET", "/database", NULL, 404, NULL, "MISSING", NULL, NULL, "not data prefix");
    check_route(dir, "GET", "/nope", NULL, 404, NULL, "MISSING BIT", NULL, "text/html", "404 page");
    check_route(dir, "GET", "/bad\npath", NULL, 400, NULL, NULL, NULL, NULL, "control char");

    expect_true(settings_ensure_dirs(dir) == 0, "ensure dirs");
    path_join(path, sizeof path, dir, "studio/state/tmp-traffic");
    expect_true(is_dir(path), "tmp-traffic exists");
    expect_true(settings_write_theme(dir, "{\"mode\":\"dark\"}") == 0, "write theme");
    theme = settings_read_theme(dir);
    expect_true(theme && strcmp(theme, "{\"mode\":\"dark\"}") == 0, "read theme");
    free(theme);
    expect_true(settings_write_theme(dir, NULL) != 0, "reject null theme");
    {
        char last[1200];
        char *file = NULL;
        size_t flen = 0;
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200, "\"lastPage\":\"\"", "session empty");
        expect_true(settings_read_last_page(NULL, last, sizeof last) != 0, "session null root");
        expect_true(settings_read_last_page(dir, NULL, sizeof last) != 0, "session null out");
        expect_true(settings_read_last_page(dir, last, 0) != 0, "session empty out");
        expect_true(settings_write_last_page(dir, NULL) != 0, "session reject null page");
        expect_true(settings_write_last_page("", "/sales/index.php") != 0, "session reject empty root");
        expect_true(settings_write_last_page(dir, "/sales/a\"b.php") == 0, "session write quote");
        expect_true(settings_read_last_page(dir, last, sizeof last) == 0 && strcmp(last, "/sales/a\"b.php") == 0,
                    "session read quote");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"lastPage\":\"/articles/greenwichconcours2026/greenwichconcours2026.html\"}",
                       200, "greenwichconcours2026.html",
                       "session post page");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "greenwichconcours2026.html",
                       "session get page");
        path_join(path, sizeof path, dir, "studio/state/session.json");
        expect_true(http_read_file(path, &file, &flen, 8192) == 0 && file &&
                        strstr(file, "greenwichconcours2026.html") != NULL,
                    "session file page");
        free(file);
        check_api_body(dir, "POST", "/__studio/api/session", "{\"lastPage\":\"/../secret\"}", 400,
                       "lastPage", "session reject traversal");
        check_api_body(dir, "POST", "/__studio/api/session", "{}", 400, "lastPage", "session require page");
        check_api_body(dir, "PUT", "/__studio/api/session", NULL, 405, "GET or POST", "session method");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "greenwichconcours2026.html",
                       "session kept page");
        expect_true(settings_read_last_page(dir, last, sizeof last) == 0 &&
                        strcmp(last, "/articles/greenwichconcours2026/greenwichconcours2026.html") == 0,
                    "session read kept page");
        expect_true(settings_read_session(NULL) == NULL, "session file null root");
        expect_true(settings_write_session(NULL, "{\"lastPage\":\"/sales/index.php\"}") != 0,
                    "session file null root write");
        expect_true(settings_write_session(dir, NULL) != 0, "session file null json");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"lastPage\":\"/articles/greenwichconcours2026.html\",\"openTabs\":["
                       "{\"path\":\"/sales/index.php\",\"isPreview\":false},"
                       "{\"path\":\"/articles/greenwichconcours2026.html\",\"isPreview\":true}]}",
                       200, "\"isPreview\":true", "session post tabs");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200, "\"openTabs\":[", "session get tabs");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200, "\\/sales\\/index.php",
                       "session get pinned tab");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"lastPage\":\"/sales/index.php\",\"openTabs\":[{\"path\":\"/../secret\",\"isPreview\":false}]}",
                       400, "openTabs", "session reject tab path");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "greenwichconcours2026.html", "session tabs kept");
        expect_true(settings_write_last_page(dir, "/parts/index.html") == 0, "session legacy write");
        {
            HttpRequest req;
            HttpResponse res;
            http_request_init(&req);
            http_response_init(&res);
            expect_true(set_req(&req, "GET", "/__studio/api/session", NULL) == 0, "session legacy get");
            expect_true(router_handle(&req, &res, dir) == 0, "session legacy route");
            expect_true(res.status == 200 && res.body && strstr(res.body, "\\/parts\\/index.html") != NULL,
                        "session legacy page");
            expect_true(res.body && strstr(res.body, "openTabs") == NULL, "session legacy omits tabs");
            http_response_free(&res);
            http_request_free(&req);
        }
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"lastPage\":\"/sales/index.php\",\"openTabs\":["
                       "{\"path\":\"/sales/index.php\",\"isPreview\":false}],"
                       "\"rightPanelOpen\":false,\"leftPanelOpen\":true,"
                       "\"widgetCollapsed\":{\"googleTracking\":true,\"pageValues\":false}}",
                       200, "\"googleTracking\":true", "session post shelf");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"lastPage\":\"/parts/index.html\"}",
                       200, "\\/parts\\/index.html", "session page merge");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"googleTracking\":true", "session merge keeps widgets");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"openTabs\":[", "session merge keeps tabs");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"rightPanelOpen\":false", "session merge keeps shelf");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"widgetCollapsed\":{\"extremeTracking\":true,\"pageValues\":false}}",
                       200, "\"extremeTracking\":true", "session widget update");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\\/parts\\/index.html", "session widget keeps page");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"googleTracking\":true", "session widget merge keeps others");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"lastPage\":\"/sales/index.php\",\"rightPanelOpen\":\"no\"}",
                       400, "rightPanelOpen", "session reject shelf flag");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"leftPanelWidth\":312,\"rightPanelWidth\":390,\"gitCommitsHeight\":220,\"gitCommitsOpen\":true}",
                       200, "\"leftPanelWidth\":312", "session post analogue shelf widths");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"gitCommitsHeight\":220", "session get analogue git commits height");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"leftPanelWidth\":\"wide\"}",
                       400, "leftPanelWidth", "session reject non-number leftPanelWidth");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"extremeTracking\":true", "session reject keeps widgets");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"widgetOrder\":[\"extremeTracking\",\"error-check\",\"pageValues\"]}",
                       200, "\"widgetOrder\":[\"extremeTracking\",\"error-check\",\"pageValues\"]",
                       "session post widget order");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"googleTracking\":true", "session order keeps collapsed");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"widgetOrder\":[\"pageValues\",\"pageValues\"]}",
                       400, "widgetOrder", "session reject duplicate widget order");
        check_api_body(dir, "POST", "/__studio/api/session",
                       "{\"widgetOrder\":\"extremeTracking\"}",
                       400, "widgetOrder", "session reject widget order type");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"widgetOrder\":[\"extremeTracking\",\"error-check\",\"pageValues\"]",
                       "session reject keeps widget order");
        expect_true(settings_write_last_page(dir, "/awards/index.html") == 0, "session last page keeps shelf");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\\/awards\\/index.html", "session last page path");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"extremeTracking\":true", "session last page keeps widgets");
        check_api_body(dir, "GET", "/__studio/api/session", NULL, 200,
                       "\"widgetOrder\":[\"extremeTracking\",\"error-check\",\"pageValues\"]",
                       "session last page keeps widget order");
        {
            HttpRequest req;
            HttpResponse res;
            http_request_init(&req);
            http_response_init(&res);
            expect_true(set_req(&req, "GET", "/__studio/api/session", NULL) == 0, "session last page drops tabs");
            expect_true(router_handle(&req, &res, dir) == 0, "session last page drops tabs route");
            expect_true(res.status == 200 && res.body && strstr(res.body, "openTabs") == NULL,
                        "session last page omits tabs");
            http_response_free(&res);
            http_request_free(&req);
        }
    }
    {
        StudioWindowState st;
        StudioWindowState got;
        char raw[PATH_MAX];
        char tmp[PATH_MAX];
        memset(&st, 0, sizeof st);
        expect_true(settings_read_window_state(NULL, &st) != 0, "window state null root");
        expect_true(st.width == 1440 && st.height == 900 && st.maximized == 0 && st.has_pos == 0,
                    "window state null defaults");
        expect_true(settings_read_window_state(dir, NULL) != 0, "window state null out");
        expect_true(settings_read_window_state(dir, &got) == 0, "window state missing file");
        expect_true(got.width == 1440 && got.height == 900 && got.maximized == 0 && got.has_pos == 0 &&
                        got.x == 0 && got.y == 0,
                    "window state defaults");
        st.has_pos = 1;
        st.x = -40;
        st.y = 80;
        st.width = 1280;
        st.height = 720;
        st.maximized = 2;
        expect_true(settings_write_window_state(dir, &st) == 0, "write window state");
        path_join(tmp, sizeof tmp, dir, "studio/state/window.json.tmp");
        expect_true(!is_file(tmp), "window state tmp removed");
        expect_true(settings_read_window_state(dir, &got) == 0, "read window state");
        expect_true(got.has_pos == 1 && got.x == -40 && got.y == 80 && got.width == 1280 &&
                        got.height == 720 && got.maximized == 1,
                    "window state roundtrip");
        st.width = 10;
        st.height = 99999;
        expect_true(settings_write_window_state(dir, &st) == 0, "write clamped window state");
        expect_true(settings_read_window_state(dir, &got) == 0 && got.width == 960 && got.height == 4320,
                    "write clamps window size");
        path_join(raw, sizeof raw, dir, "studio/state/window.json");
        expect_true(write_file(raw,
                               "{ \"x\" : 120 , \"y\" : 80 , \"width\" : 100 , \"height\" : 50000 , "
                               "\"maximized\" : true , \"hasPos\" : 1 }") == 0,
                    "plant window json");
        expect_true(settings_read_window_state(dir, &got) == 0 && got.x == 120 && got.y == 80 &&
                        got.width == 960 && got.height == 4320 && got.maximized == 1 && got.has_pos == 1,
                    "read clamps window json");
        expect_true(write_file(raw, "{\"x\":15,\"y\":25,\"has_pos\":1,\"maximized\":false}") == 0,
                    "plant partial window json");
        expect_true(settings_read_window_state(dir, &got) == 0 && got.x == 15 && got.y == 25 &&
                        got.width == 1440 && got.height == 900 && got.maximized == 0 && got.has_pos == 1,
                    "partial window json defaults");
        expect_true(write_file(raw, "not-json") == 0, "plant bad window json");
        expect_true(settings_read_window_state(dir, &got) == 0 && got.width == 1440 && got.height == 900 &&
                        got.has_pos == 0 && got.maximized == 0,
                    "bad window json defaults");
        expect_true(write_file(raw, "{\"x\":120,\"y\":80,\"width\":1440,\"height\":900,\"maximized\":0,\"hasPos\":1}") ==
                        0,
                    "plant example window json");
        expect_true(settings_read_window_state(dir, &got) == 0 && got.x == 120 && got.y == 80 &&
                        got.width == 1440 && got.height == 900 && got.maximized == 0 && got.has_pos == 1,
                    "example window json");
        expect_true(settings_write_window_state(dir, NULL) != 0, "reject null window state");
        expect_true(settings_write_window_state("", &st) != 0, "reject empty window root");
    }
    {
        char browse[PATH_MAX];
        char marked[PATH_MAX];
        char query[PATH_MAX + 64];
        char body[PATH_MAX + 64];
        char *file = NULL;
        size_t flen = 0;
        const char *home;
        const char *n2;
        const char *n10;
        HttpRequest req;
        HttpResponse res;

        expect_true(settings_init_roots(dir, NULL) == 0, "init project root");
        home = settings_get_studio_home();
        expect_true(home && home[0] && is_dir(home), "studio home");
        expect_true(strcmp(settings_get_project_root(), home) == 0, "project defaults home");
        path_join(browse, sizeof browse, dir, "browse");
        expect_true(mkdir_p(browse) == 0, "mkdir browse");
        path_join(path, sizeof path, browse, "n10");
        expect_true(mkdir_p(path) == 0, "mkdir n10");
        path_join(path, sizeof path, browse, "n2");
        expect_true(mkdir_p(path) == 0, "mkdir n2");
        path_join(path, sizeof path, browse, "sales");
        expect_true(mkdir_p(path) == 0, "mkdir sales");
        path_join(path, sizeof path, browse, ".git");
        expect_true(mkdir_p(path) == 0, "mkdir git");
        path_join(marked, sizeof marked, browse, "marked");
        expect_true(mkdir_p(marked) == 0, "mkdir marked");
        path_join(path, sizeof path, marked, "index.html");
        expect_true(write_file(path, "<p>marked</p>") == 0, "write marked");
        path_join(path, sizeof path, browse, "plain.txt");
        expect_true(write_file(path, "x") == 0, "write plain");

        snprintf(query, sizeof query, "path=%s", browse);
        http_request_init(&req);
        http_response_init(&res);
        expect_true(set_req(&req, "GET", "/__studio/api/fs/dirs", query) == 0, "dirs set");
        expect_true(router_handle(&req, &res, dir) == 0 && res.status == 200 && res.body, "dirs status");
        expect_true(res.body && strstr(res.body, "\"name\":\"..\"") != NULL, "dirs parent");
        expect_true(res.body && strstr(res.body, ".git") == NULL, "dirs hide dot");
        expect_true(res.body && strstr(res.body, "plain.txt") == NULL, "dirs skip files");
        n2 = res.body ? strstr(res.body, "\"name\":\"n2\"") : NULL;
        n10 = res.body ? strstr(res.body, "\"name\":\"n10\"") : NULL;
        expect_true(n2 && n10 && n2 < n10, "dirs natural sort");
        expect_true(res.body && strstr(res.body, "\"website\":true") != NULL, "dirs website");
        http_response_free(&res);
        http_request_free(&req);

        snprintf(query, sizeof query, "path=%s&hidden=1", browse);
        http_request_init(&req);
        http_response_init(&res);
        expect_true(set_req(&req, "GET", "/__studio/api/fs/dirs", query) == 0, "dirs hidden set");
        expect_true(router_handle(&req, &res, dir) == 0 && res.status == 200 && res.body &&
                        strstr(res.body, "\"name\":\".git\"") != NULL,
                    "dirs show dot");
        http_response_free(&res);
        http_request_free(&req);

        snprintf(query, sizeof query, "path=%s/plain.txt", browse);
        http_request_init(&req);
        http_response_init(&res);
        expect_true(set_req(&req, "GET", "/__studio/api/fs/dirs", query) == 0, "dirs file set");
        expect_true(router_handle(&req, &res, dir) == 0 && res.status == 400, "dirs reject file");
        http_response_free(&res);
        http_request_free(&req);

        snprintf(body, sizeof body, "{\"path\":\"%s\"}", marked);
        check_api_body(dir, "POST", "/__studio/api/project/open", body, 200, "\"ok\":true", "open project");
        expect_true(strstr(settings_get_project_root(), "/marked") != NULL, "project switched");
        expect_true(studio_restart_php_for_root(settings_get_project_root()) == 0, "php restart idle");
        path_join(path, sizeof path, settings_get_studio_home(), "studio/state/project.json");
        expect_true(http_read_file(path, &file, &flen, 8192) == 0 && file && strstr(file, "marked") != NULL,
                    "project.json saved");
        free(file);
        file = NULL;
        expect_true(settings_init_roots(dir, browse) == 0, "cli project root");
        expect_true(strstr(settings_get_project_root(), "/browse") != NULL &&
                        strstr(settings_get_project_root(), "/marked") == NULL,
                    "cli root wins");
        expect_true(http_read_file(path, &file, &flen, 8192) == 0 && file && strstr(file, "marked") != NULL,
                    "cli keeps saved folder");
        free(file);
        file = NULL;
        expect_true(settings_init_roots(dir, NULL) == 0, "reload saved project");
        expect_true(strstr(settings_get_project_root(), "/marked") != NULL, "saved project restored");
        check_api_body(dir, "GET", "/__studio/api/fs/pick-folder", NULL, 405, "POST", "pick method");
        check_api_body(dir, "POST", "/__studio/api/project/open", "{\"path\":\"/no/such/cc-studio-dir\"}", 400,
                       "not found", "open missing");
        check_api_body(dir, "GET", "/__studio/api/config", NULL, 200, "\"config\":{", "config get");
        check_api_query(dir, "/__studio/api/page", "path=%2F__studio%2Fconfig.json", 200,
                        "\"fileType\":\"json\"", NULL, "config page get");
        check_api_body(dir, "POST", "/__studio/api/page/save",
                       "{\"path\":\"/__studio/config.json\",\"content\":\"{\\n  // Comment header\\n  \\\"googleMeasurementId\\\": \\\"G-TEST1234\\\",\\n  /* block comment */\\n  \\\"extremeLogin\\\": \\\"testacct\\\",\\n  \\\"extremeTrackerHost\\\": \\\"tracker.example.com\\\"\\n}\"}",
                       200, "G-TEST1234", "config page save with comments");
        expect_true(strcmp(config_get()->google_measurement_id, "G-TEST1234") == 0 &&
                        strcmp(config_get()->extreme_login, "testacct") == 0,
                    "config reloaded in memory");
        check_api_body(dir, "POST", "/__studio/api/config/backup", "{}", 200, "\"backupPath\":", "config backup");
        check_api_body(dir, "POST", "/__studio/api/config/clear", "{}", 200, "\"googleMeasurementId\":\"\"", "config clear");
        expect_true(config_get()->google_measurement_id[0] == '\0' && config_get()->extreme_login[0] == '\0',
                    "config cleared in memory");
        check_api_body(dir, "POST", "/__studio/api/config/recover", "{}", 200, "G-TEST1234", "config recover");
        expect_true(strcmp(config_get()->google_measurement_id, "G-TEST1234") == 0 &&
                        strcmp(config_get()->extreme_login, "testacct") == 0,
                    "config recovered in memory");
        check_api_body(dir, "POST", "/__studio/api/page/save",
                       "{\"path\":\"/__studio/config.json\",\"content\":\"not-json\"}",
                       400, "JSON object", "config reject invalid json");

        /* App-wide config self-checks (stored in studio_home/state/app-config.json, not project root) */
        check_api_body(marked, "GET", "/__studio/api/app-config", NULL, 200,
                       "\"gitAutoFetch\":false", "app-config defaults disabled");
        check_api_body(marked, "GET", "/__studio/api/app-config", NULL, 200,
                       "\"gitAutoFetchPrompted\":false", "app-config defaults unprompted");
        check_api_body(marked, "POST", "/__studio/api/app-config",
                       "{\"gitAutoFetch\":true,\"gitAutoFetchPrompted\":true,\"gitAutoFetchIntervalSec\":120}",
                       200, "\"gitAutoFetch\":true", "app-config enable periodic fetch");
        expect_true(app_config_get()->git_auto_fetch == 1 &&
                        app_config_get()->git_auto_fetch_prompted == 1 &&
                        app_config_get()->git_auto_fetch_interval_sec == 120,
                    "app-config updated in memory");
        path_join(path, sizeof path, settings_get_studio_home(), "studio/state/app-config.json");
        expect_true(is_file(path), "app-config saved in studio home state");
        path_join(path, sizeof path, marked, "state/app-config.json");
        expect_true(!is_file(path), "app-config not saved in project root");
        check_api_body(marked, "POST", "/__studio/api/app-config",
                       "{\"gitAutoFetch\":\"invalid\"}",
                       400, "boolean", "app-config reject non-boolean");
        check_api_body(marked, "POST", "/__studio/api/app-config",
                       "{\"gitAutoFetch\":false,\"gitAutoFetchPrompted\":false,\"gitAutoFetchIntervalSec\":180}",
                       200, "\"gitAutoFetch\":false", "app-config reset defaults");

        expect_true(settings_set_project_root(path) != 0, "reject project file");
        settings_set_studio_home("");
        settings_set_project_root("");
        config_load("");
        app_config_load("");

        /* Project Search & Replace + Git SCM self-checks */
        check_api_body(dir, "POST", "/__studio/api/search",
                       "{\"query\":\"STORY\",\"mode\":\"literal\"}",
                       200, "\"totalMatches\":1", "search literal story");
        check_api_body(dir, "POST", "/__studio/api/search",
                       "{\"query\":\"(ST)ORY\",\"replacement\":\"$1-REPL\",\"mode\":\"regex\"}",
                       200, "\"replacePreview\":\"ST-REPL\"", "search regex capture preview");
        check_api_body(dir, "POST", "/__studio/api/search",
                       "{\"query\":\"ST*Y\",\"replacement\":\"W-$1\",\"mode\":\"wildcard\"}",
                       200, "\"replacePreview\":\"W-OR\"", "search wildcard capture preview");
        check_api_body(dir, "POST", "/__studio/api/search/replace",
                       "{\"query\":\"ST*Y\",\"replacement\":\"STORY_UPDATED_$1\",\"mode\":\"wildcard\",\"path\":\"/articles/story/index.html\"}",
                       200, "\"replacedMatches\":1", "search replace wildcard");
        check_api_body(dir, "POST", "/__studio/api/search",
                       "{\"query\":\"STORY_UPDATED_OR\",\"mode\":\"literal\"}",
                       200, "\"totalMatches\":1", "search verify replaced");
        check_api_body(dir, "GET", "/__studio/api/git/status", NULL, 200, "\"ok\":true", "git status endpoint");
        check_api_body(dir, "GET", "/__studio/api/git/status", NULL, 200, "\"repoName\":", "git status includes repoName");
        check_api_body(dir, "POST", "/__studio/api/git/fetch", "{\"background\":true}", 200, "\"ok\":true", "git fetch endpoint");

        /* Terminal API self-checks */
        check_api_body(dir, "GET", "/__studio/api/terminal/info", NULL, 200, "\"shell\":", "terminal info endpoint");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"echo STUDIO_TERM_OK\"}",
                       200, "STUDIO_TERM_OK", "terminal exec echo");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"cd articles/story\"}",
                       200, "articles\\/story\"", "terminal exec cd updates cwd");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"exit 7\"}",
                       200, "\"exitCode\":7", "terminal exec non-zero exit code");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"   \"}",
                       400, "command is required", "terminal exec rejects empty command");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"printf 'OUT1\\\\n'; printf 'ERR2\\\\n' >&2; printf 'OUT3\\\\n'\"}",
                       200, "OUT1\\nERR2\\nOUT3\\n", "terminal exec interleaves stdout and stderr in chronological order");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"[ -t 1 ] && [ -t 2 ] && echo IS_TTY_YES\"}",
                       200, "IS_TTY_YES", "terminal exec runs child on a PTY");
        check_api_body(dir, "POST", "/__studio/api/terminal/exec",
                       "{\"command\":\"printf 'PROG_0\\\\rPROG_100\\\\n'\"}",
                       200, "PROG_0\\rPROG_100\\n", "terminal exec preserves carriage returns for in-place progress");
        {
            HttpRequest req_s;
            HttpResponse res_s;
            int job_id = 0;
            const char *id_pos;
            http_request_init(&req_s);
            http_response_init(&res_s);
            expect_true(set_req(&req_s, "POST", "/__studio/api/terminal/start", NULL) == 0, "terminal start req");
            req_s.body = strdup("{\"command\":\"printf 'STEP1\\\\rSTEP2\\\\n' && cd articles/story\"}");
            req_s.body_len = req_s.body ? strlen(req_s.body) : 0;
            expect_true(router_handle(&req_s, &res_s, dir) == 0 && res_s.status == 200 && res_s.body, "terminal start 200");
            id_pos = res_s.body ? strstr(res_s.body, "\"id\":") : NULL;
            if (id_pos) job_id = (int)strtol(id_pos + 5, NULL, 10);
            expect_true(job_id > 0, "terminal start returns job id");
            http_response_free(&res_s);
            http_request_free(&req_s);

            if (job_id > 0) {
                char poll_q[128];
                int tries;
                int done = 0;
                for (tries = 0; tries < 50 && !done; tries++) {
                    HttpRequest req_p;
                    HttpResponse res_p;
                    snprintf(poll_q, sizeof(poll_q), "id=%d&offset=0", job_id);
                    http_request_init(&req_p);
                    http_response_init(&res_p);
                    set_req(&req_p, "GET", "/__studio/api/terminal/poll", poll_q);
                    if (router_handle(&req_p, &res_p, dir) == 0 && res_p.status == 200 && res_p.body) {
                        if (strstr(res_p.body, "\"running\":false") != NULL) {
                            done = 1;
                            expect_true(strstr(res_p.body, "STEP1\\rSTEP2\\n") != NULL, "terminal poll chunk has CR output");
                            expect_true(strstr(res_p.body, "articles\\/story") != NULL, "terminal poll updates cwd");
                            expect_true(strstr(res_p.body, "\"exitCode\":0") != NULL, "terminal poll exit code 0");
                        }
                    }
                    http_response_free(&res_p);
                    http_request_free(&req_p);
                    if (!done) sleep_ms(20);
                }
                expect_true(done == 1, "terminal poll completes");
            }
        }
    }

    srv = http_server_new();
    expect_true(srv && http_server_listen(srv, "127.0.0.1", 0) == 0, "listen");
    if (srv && http_server_port(srv) > 0) {
        smoke.srv = srv;
        smoke.root = dir;
        expect_true(pthread_create(&thread, NULL, smoke_serve, &smoke) == 0, "smoke thread");
        expect_true(http_exchange(http_server_port(srv),
                                  "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
                                  resp, sizeof resp) == 0,
                    "socket exchange");
        expect_true(strstr(resp, "HTTP/1.1 302") != NULL && strstr(resp, "Location: /__studio/") != NULL,
                    "socket redirect");
        expect_true(http_exchange(http_server_port(srv),
                                  "GET /hello.shtml HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",
                                  resp, sizeof resp) == 0 &&
                        strstr(resp, "ABITB") != NULL,
                    "socket shtml");
        http_server_request_stop();
        pthread_join(thread, NULL);
    }
    http_server_free(srv);
    settings_set_php_port(8771);
    rm_rf(dir);
}
#endif

static int run_self_check(void) {
    test_http_units();
#ifndef _WIN32
    test_proxy();
    test_tree();
#endif
    if (g_failed) {
        fprintf(stderr, "classiccars-studio: self-check failed (%d of %d)\n", g_failed, g_checks);
        return 1;
    }
    printf("classiccars-studio: self-check ok (%d checks)\n", g_checks);
    return 0;
}

static int parse_port(const char *text, int *out) {
    char *end = NULL;
    long v;
    errno = 0;
    if (!text || !text[0]) return -1;
    v = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || v < 1 || v > 65535) return -1;
    *out = (int)v;
    return 0;
}

static void studio_url(char *dst, size_t dst_sz, int port) {
    snprintf(dst, dst_sz, "http://127.0.0.1:%d/__studio/", port);
}

static int g_console = 0;

static void ensure_console(void) {
#ifdef _WIN32
    if (g_console) return;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        (void)freopen("CONOUT$", "w", stdout);
        (void)freopen("CONOUT$", "w", stderr);
        (void)freopen("CONIN$", "r", stdin);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
        g_console = 1;
        return;
    }
    (void)freopen("NUL", "w", stdout);
    (void)freopen("NUL", "w", stderr);
#else
    g_console = 1;
#endif
}

static void silence_gui_stdio(void) {
#ifdef _WIN32
    if (g_console) return;
    (void)freopen("NUL", "w", stdout);
    (void)freopen("NUL", "w", stderr);
#endif
}

static void report_error(const char *msg) {
    fprintf(stderr, "%s\n", msg);
#ifdef _WIN32
    if (!g_console) MessageBoxA(NULL, msg, "ClassicCars Studio", MB_OK | MB_ICONERROR);
#endif
}

static void studio_title(char *dst, size_t dst_sz) {
    snprintf(dst, dst_sz, "ClassicCars Studio v%s", STUDIO_VERSION);
}

static int icon_file(char *dst, size_t dst_sz, const char *path) {
    if (!path || !path[0] || !is_file(path)) return 0;
    return copy_fit(dst, dst_sz, path) == 0;
}

static void studio_icon(char *dst, size_t dst_sz) {
    const char *home = settings_get_studio_home();
    char path[PATH_MAX];
    dst[0] = '\0';
    if (home && home[0]) {
        if (settings_install_path(home, "assets/favicon-32x32.png", path, sizeof path) == 0 &&
            icon_file(dst, dst_sz, path)) return;
        if (settings_install_path(home, "assets/icon-256.png", path, sizeof path) == 0 &&
            icon_file(dst, dst_sz, path)) return;
        if (path_join(path, sizeof path, home, "favicon-32x32.png") == 0 && icon_file(dst, dst_sz, path)) return;
    }
    if (g_root[0] && path_join(path, sizeof path, g_root, "favicon-32x32.png") == 0 && icon_file(dst, dst_sz, path)) {
        return;
    }
    dst[0] = '\0';
}

static void usage(void) {
    ensure_console();
    fprintf(stderr,
            "Usage: classiccars-studio [options]\n"
            "  --port <port>       Listen port (default 8770)\n"
            "  --php-port <port>   php -S port (default 8771)\n"
            "  --root <path>       Website folder for this launch (saved folder is kept)\n"
            "  --no-php            Do not spawn php -S\n"
            "  --headless          Serve only. Do not open a window\n"
            "  --no-window         Same as --headless\n"
            "  --server-only       Same as --headless\n"
            "  --no-open           Same as --headless\n"
            "  --check             Run self-checks and exit\n"
            "  --version           Print the version and exit\n"
            "  --help              Show this help\n"
            "\n"
            "Launching the program opens a standalone window. Closing that\n"
            "window stops the server. If the port is already in use, a window\n"
            "attaches to the running studio and this process exits when that\n"
            "window closes. --headless leaves the server in this terminal.\n");
#ifdef _WIN32
    if (!g_console) {
        MessageBoxA(NULL,
                    "ClassicCars Studio opens its own window.\n\n"
                    "Close the window to quit. Pass --headless to serve without a window.\n"
                    "Run classiccars-studio --help from a terminal for every option.",
                    "ClassicCars Studio", MB_OK | MB_ICONINFORMATION);
    }
#endif
}

static int dirname_inplace(char *path) {
    char *slash = strrchr(path, '/');
#ifdef _WIN32
    char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (!slash) return -1;
    if (slash == path) {
        path[1] = '\0';
        return 0;
    }
    *slash = '\0';
    return 0;
}

static int exe_path(char *out, size_t out_sz) {
#ifdef _WIN32
    DWORD n = GetModuleFileNameA(NULL, out, (DWORD)out_sz);
    if (n == 0 || (size_t)n >= out_sz) return -1;
    return 0;
#elif defined(__APPLE__)
    uint32_t sz = (uint32_t)out_sz;
    if (_NSGetExecutablePath(out, &sz) != 0) return -1;
    return 0;
#else
    ssize_t n = readlink("/proc/self/exe", out, out_sz - 1);
    if (n < 0 || (size_t)n >= out_sz - 1) return -1;
    out[n] = '\0';
    return 0;
#endif
}

static int last_segment_is(const char *dir, const char *name) {
    const char *slash = strrchr(dir, '/');
#ifdef _WIN32
    const char *bslash = strrchr(dir, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    if (!slash || !slash[1] || !name) return 0;
    return strcmp(slash + 1, name) == 0;
}

static int canonicalize(const char *in, char *out, size_t out_sz) {
#ifdef _WIN32
    char resolved[4096];
    char *p;
    DWORD n = GetFullPathNameA(in, (DWORD)sizeof resolved, resolved, NULL);
    if (n == 0 || n >= sizeof resolved) return -1;
    for (p = resolved; *p; p++) {
        if (*p == '\\') *p = '/';
    }
    return copy_fit(out, out_sz, resolved);
#else
    char resolved[PATH_MAX];
    if (!realpath(in, resolved)) return -1;
    return copy_fit(out, out_sz, resolved);
#endif
}

#ifndef _WIN32
static void on_signal(int sig) {
    (void)sig;
    http_server_request_stop();
}

static void install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}
#else
static BOOL WINAPI on_console(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        http_server_request_stop();
        return TRUE;
    }
    return FALSE;
}

static void install_signals(void) {
    SetConsoleCtrlHandler(on_console, TRUE);
}
#endif


typedef struct ServeJob {
    HttpServer *srv;
    App *app;
    int rc;
} ServeJob;

#ifndef _WIN32
static void *serve_thread(void *arg) {
    ServeJob *job = arg;
    job->rc = http_server_serve(job->srv, on_request, job->app, job->app->php_on ? supervise_php : NULL, job->app);
    return NULL;
}
#else
static unsigned __stdcall serve_thread(void *arg) {
    ServeJob *job = arg;
    job->rc = http_server_serve(job->srv, on_request, job->app, job->app->php_on ? supervise_php : NULL, job->app);
    return 0;
}
#endif

/* Directory that contains the running binary. build/classiccars-studio maps to its parent. */
static int detect_studio_home(char *out, size_t out_sz) {
    char dir[PATH_MAX];
    if (exe_path(dir, sizeof dir) != 0 || dirname_inplace(dir) != 0) {
#ifdef _WIN32
        if (!_getcwd(dir, (int)sizeof dir)) return -1;
#else
        if (!getcwd(dir, sizeof dir)) return -1;
#endif
    }
    if (last_segment_is(dir, "build") && dirname_inplace(dir) != 0) return -1;
    if (canonicalize(dir, out, out_sz) == 0) return 0;
    return copy_fit(out, out_sz, dir);
}

static int is_website_dir(const char *dir) {
    char sales[PATH_MAX];
    char includes[PATH_MAX];
    if (!dir || !dir[0] || !is_dir(dir)) return 0;
    if (path_join(sales, sizeof sales, dir, "sales") != 0 || !is_dir(sales)) return 0;
    if (path_join(includes, sizeof includes, dir, "includes") == 0 && is_dir(includes)) return 1;
    return 0;
}

/* Sibling of Apps/ClassicCars_Studio under GIT/. */
static int default_website_root(const char *studio_home, char *out, size_t out_sz) {
    char rel[PATH_MAX];
    if (studio_home && studio_home[0] &&
        path_join(rel, sizeof rel, studio_home, "../../Websites/ClassicCars") == 0 && is_website_dir(rel)) {
        if (canonicalize(rel, out, out_sz) == 0) return 0;
        if (copy_fit(out, out_sz, rel) == 0) return 0;
    }
    return -1;
}

static int prepare_roots(const char *root_arg, char *err, size_t err_sz) {
    char home[PATH_MAX];
    char cli[PATH_MAX];
    char saved[PATH_MAX];
    const char *cli_ptr = NULL;
    const char *project;
    home[0] = '\0';
    if (err && err_sz) err[0] = '\0';
    if (detect_studio_home(home, sizeof home) != 0) home[0] = '\0';
    if (root_arg && root_arg[0]) {
        if (canonicalize(root_arg, cli, sizeof cli) != 0 || !is_dir(cli)) {
            if (err && err_sz) snprintf(err, err_sz, "classiccars-studio: root is not a directory: %s", root_arg);
            return -1;
        }
        cli_ptr = cli;
        if (!home[0] && copy_fit(home, sizeof home, cli) != 0) {
            if (err && err_sz) snprintf(err, err_sz, "classiccars-studio: could not determine the repository root");
            return -1;
        }
    }
    if (!home[0] || !is_dir(home)) {
        if (err && err_sz) snprintf(err, err_sz, "classiccars-studio: could not determine the repository root");
        return -1;
    }
    if (settings_init_roots(home, cli_ptr) != 0) {
        if (err && err_sz) snprintf(err, err_sz, "classiccars-studio: could not use the project folder");
        return -1;
    }
    config_load(home);
    app_config_load(home);
    if (!cli_ptr && settings_read_saved_project_root(home, saved, sizeof saved) != 0) {
        char site[PATH_MAX];
        if (default_website_root(home, site, sizeof site) == 0 && settings_set_project_root(site) != 0) {
            if (err && err_sz) snprintf(err, err_sz, "classiccars-studio: could not use the project folder");
            return -1;
        }
    }
    project = settings_get_project_root();
    if (!project || !project[0] || copy_fit(g_root, sizeof g_root, project) != 0) {
        if (err && err_sz) snprintf(err, err_sz, "classiccars-studio: could not use the project folder");
        return -1;
    }
    return 0;
}

static int open_studio_window(int port) {
    char url[160];
    char title[128];
    char icon[PATH_MAX];
    const char *home = settings_get_studio_home();
    studio_url(url, sizeof url, port);
    studio_title(title, sizeof title);
    studio_icon(icon, sizeof icon);
    if (studio_window_run(url, title, icon[0] ? icon : NULL, home && home[0] ? home : g_root) != 0) {
        report_error("classiccars-studio: could not open a native window. On Linux this needs GTK 3 and "
                     "WebKitGTK. Pass --headless to serve without a window.");
        return -1;
    }
    return 0;
}

static int attach_running(int port, int headless) {
    char url[160];
    studio_url(url, sizeof url, port);
    printf("ClassicCars Studio is already running\n  Studio: %s\n", url);
    fflush(stdout);
    if (headless) return 0;
    return open_studio_window(port) == 0 ? 0 : 1;
}


int main(int argc, char **argv) {
    int port = 8770;
    int php_port = 8771;
    int no_php = 0;
    int check = 0;
    int headless = 0;
    int help = 0;
    int version = 0;
    const char *root_arg = NULL;
    int i;
    HttpServer *srv;
    App app;
    ServeJob job;
    char prepend[PATH_MAX];
    char url[160];
    char errbuf[512];
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            help = 1;
        } else if (strcmp(argv[i], "--version") == 0) {
            version = 1;
        } else if (strcmp(argv[i], "--check") == 0) {
            check = 1;
        } else if (strcmp(argv[i], "--no-php") == 0) {
            no_php = 1;
        } else if (strcmp(argv[i], "--headless") == 0 || strcmp(argv[i], "--no-window") == 0 ||
                   strcmp(argv[i], "--server-only") == 0 || strcmp(argv[i], "--no-open") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--open") == 0) {
            /* The studio window is the default. Kept so older launchers still start. */
        } else if (strcmp(argv[i], "--port") == 0) {
            if (++i >= argc || parse_port(argv[i], &port) != 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--php-port") == 0) {
            if (++i >= argc || parse_port(argv[i], &php_port) != 0) {
                usage();
                return 2;
            }
        } else if (strcmp(argv[i], "--root") == 0) {
            if (++i >= argc) {
                usage();
                return 2;
            }
            root_arg = argv[i];
        } else {
            ensure_console();
            fprintf(stderr, "classiccars-studio: unknown argument %s\n", argv[i]);
            usage();
            return 2;
        }
    }
    if (help) {
        usage();
        return 0;
    }
    if (version) {
        char text[128];
        ensure_console();
        snprintf(text, sizeof text, "ClassicCars Studio v%s", STUDIO_VERSION);
        printf("%s\n", text);
#ifdef _WIN32
        if (!g_console) MessageBoxA(NULL, text, "ClassicCars Studio", MB_OK | MB_ICONINFORMATION);
#endif
        return 0;
    }
    if (check) {
        ensure_console();
        return run_self_check();
    }
    if (headless) ensure_console();
    else silence_gui_stdio();

    {
        int roots_rc = prepare_roots(root_arg, errbuf, sizeof errbuf);
        if (http_try_connect("127.0.0.1", port, 300) == 0) return attach_running(port, headless);
        if (roots_rc != 0) {
            report_error(errbuf[0] ? errbuf : "classiccars-studio: could not determine the repository root");
            return 1;
        }
    }
    if (!no_php && port == php_port) {
        report_error("classiccars-studio: --port and --php-port must differ");
        return 1;
    }
    {
        const char *install = settings_get_studio_home();
        if (!install || !install[0]) install = g_root;
        if (settings_ensure_dirs(install) != 0) {
            report_error("classiccars-studio: could not create state");
            return 1;
        }
    }
    settings_set_php_port(0);
    if (!no_php) {
        int requested_php = php_port;
        int chosen;
        const char *install = settings_get_studio_home();
        if (!install || !install[0]) install = g_root;
        if (!php_is_available()) {
            report_error("classiccars-studio: php was not found on PATH (install PHP or pass --no-php)");
            return 1;
        }
        prepend[0] = '\0';
        if (settings_install_path(install, "preview/prepend.php", prepend, sizeof prepend) != 0 || !is_file(prepend)) {
            char missing[PATH_MAX + 96];
            int wrote = snprintf(missing, sizeof missing, "classiccars-studio: missing %s",
                                 prepend[0] ? prepend : "preview/prepend.php");
            report_error(wrote > 0 && (size_t)wrote < sizeof missing
                             ? missing
                             : "classiccars-studio: missing preview/prepend.php");
            return 1;
        }
        chosen = choose_php_port(php_port, port);
        if (chosen < 0) {
            report_error("classiccars-studio: no free loopback port for php (tried 8771-8850)");
            return 1;
        }
        if (chosen != requested_php) {
            fprintf(stderr, "classiccars-studio: 127.0.0.1:%d is in use, php will listen on %d\n",
                    requested_php, chosen);
        }
        php_port = chosen;
        settings_set_php_port(php_port);
        if (copy_fit(g_php_docroot, sizeof g_php_docroot, g_root) != 0) {
            report_error("classiccars-studio: could not use the project folder");
            return 1;
        }
        g_php_enabled = 1;
        if (php_spawn(g_php_docroot, php_port) != 0) {
            g_php_enabled = 0;
            report_error("classiccars-studio: failed to spawn php");
            return 1;
        }
        atexit(php_stop);
        if (wait_for_php(php_port, 3000) != 0) {
            snprintf(errbuf, sizeof errbuf,
                     "classiccars-studio: php did not accept connections on 127.0.0.1:%d within 3 seconds", php_port);
            report_error(errbuf);
            g_php_enabled = 0;
            php_stop();
            return 1;
        }
    }
    srv = http_server_new();
    if (!srv || http_server_listen(srv, "127.0.0.1", port) != 0) {
        if (http_try_connect("127.0.0.1", port, 300) == 0) {
            http_server_free(srv);
            php_stop();
            return attach_running(port, headless);
        }
#ifdef _WIN32
        snprintf(errbuf, sizeof errbuf, "classiccars-studio: could not listen on 127.0.0.1:%d (WSA %d)", port,
                 WSAGetLastError());
#else
        snprintf(errbuf, sizeof errbuf, "classiccars-studio: could not listen on 127.0.0.1:%d: %s", port, strerror(errno));
#endif
        report_error(errbuf);
        http_server_free(srv);
        php_stop();
        return 1;
    }
    printf("ClassicCars Studio v%s\n", STUDIO_VERSION);
    printf("  Root:   %s\n", g_root);
    {
        const char *home = settings_get_studio_home();
        if (home && home[0] && strcmp(home, g_root) != 0) printf("  Home:   %s\n", home);
    }
    studio_url(url, sizeof url, http_server_port(srv));
    printf("  Studio: %s\n", url);
    if (no_php) printf("  PHP:    disabled\n");
    else printf("  PHP:    127.0.0.1:%d\n", php_port);
    fflush(stdout);
    app.root = g_root;
    app.php_port = php_port;
    app.studio_port = http_server_port(srv);
    app.php_on = !no_php;
    if (headless) {
        install_signals();
        if (http_server_serve(srv, on_request, &app, no_php ? NULL : supervise_php, &app) != 0) {
            report_error("classiccars-studio: server stopped because of a socket error");
            http_server_free(srv);
            php_stop();
            return 1;
        }
        http_server_free(srv);
        php_stop();
        return 0;
    }
    job.srv = srv;
    job.app = &app;
    job.rc = 0;
    {
        int window_rc;
#ifdef _WIN32
        uintptr_t thread = _beginthreadex(NULL, 0, serve_thread, &job, 0, NULL);
        if (thread == 0) {
            report_error("classiccars-studio: could not start the server thread");
            http_server_free(srv);
            php_stop();
            return 1;
        }
        window_rc = open_studio_window(http_server_port(srv));
        http_server_request_stop();
        WaitForSingleObject((HANDLE)thread, INFINITE);
        CloseHandle((HANDLE)thread);
#else
        pthread_t thread;
        if (pthread_create(&thread, NULL, serve_thread, &job) != 0) {
            report_error("classiccars-studio: could not start the server thread");
            http_server_free(srv);
            php_stop();
            return 1;
        }
        window_rc = open_studio_window(http_server_port(srv));
        http_server_request_stop();
        pthread_join(thread, NULL);
#endif
        http_server_free(srv);
        php_stop();
        if (window_rc != 0) return 1;
        if (job.rc != 0) {
            report_error("classiccars-studio: server stopped because of a socket error");
            return 1;
        }
        return 0;
    }
}
