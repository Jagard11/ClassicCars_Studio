#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "terminal.h"
#include "settings.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
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
#include <process.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#endif

#include "compat.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define TERMINAL_MAX_OUTPUT (512u * 1024u)
#define TERMINAL_DEFAULT_TIMEOUT_MS 30000
#define TERMINAL_MIN_TIMEOUT_MS 100
#define TERMINAL_MAX_TIMEOUT_MS 600000
#define TERMINAL_MAX_JOBS 16

typedef struct TBuf {
    char *data;
    size_t len;
    size_t cap;
} TBuf;

static void tbuf_init(TBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void tbuf_free(TBuf *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int tbuf_reserve(TBuf *b, size_t add) {
    size_t need;
    size_t next;
    char *grown;
    if (!b) return -1;
    if (add > SIZE_MAX - b->len - 1) return -1;
    need = b->len + add + 1;
    if (need <= b->cap) return 0;
    next = b->cap ? b->cap : 256;
    while (next < need) {
        if (next > SIZE_MAX / 2) {
            next = need;
            break;
        }
        next *= 2;
    }
    grown = (char *)realloc(b->data, next);
    if (!grown) return -1;
    b->data = grown;
    b->cap = next;
    return 0;
}

static int tbuf_append(TBuf *b, const char *s, size_t n) {
    if (tbuf_reserve(b, n) != 0) return -1;
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int tbuf_puts(TBuf *b, const char *s) {
    return s ? tbuf_append(b, s, strlen(s)) : 0;
}

static void set_err(char *err, size_t err_sz, const char *fmt, ...) {
    va_list ap;
    if (!err || err_sz == 0) return;
    va_start(ap, fmt);
    vsnprintf(err, err_sz, fmt, ap);
    va_end(ap);
}

static int jput_s(JsonValue *obj, const char *k, const char *v) {
    JsonValue *s = json_string_cstr(v ? v : "");
    if (!s) return -1;
    return json_object_put(obj, k, s);
}

static int jput_s_len(JsonValue *obj, const char *k, const char *v, size_t len) {
    JsonValue *s = json_string(v ? v : "", len);
    if (!s) return -1;
    return json_object_put(obj, k, s);
}

static int jput_i(JsonValue *obj, const char *k, long long v) {
    JsonValue *num = json_number_int(v);
    if (!num) return -1;
    return json_object_put(obj, k, num);
}

static int jput_b(JsonValue *obj, const char *k, int v) {
    JsonValue *b = json_bool(v ? 1 : 0);
    if (!b) return -1;
    return json_object_put(obj, k, b);
}

static long long term_now_ms(void) {
#ifdef _WIN32
    return (long long)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
#endif
}

static void trim_spaces(char *s) {
    char *start;
    size_t n;
    if (!s) return;
    start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
}

static int is_abs_path(const char *p) {
    if (!p || !p[0]) return 0;
    if (p[0] == '/' || p[0] == '\\') return 1;
#ifdef _WIN32
    if (isalpha((unsigned char)p[0]) && p[1] == ':' && (p[2] == '/' || p[2] == '\\' || p[2] == '\0')) {
        return 1;
    }
#endif
    return 0;
}

static void resolve_cwd_or_root(const char *repo_root, const char *req_cwd,
                                char *canon_root, size_t canon_root_sz,
                                char *out_cwd, size_t out_cwd_sz) {
    char raw[PATH_MAX];
    char candidate[PATH_MAX];

    canon_root[0] = '\0';
    out_cwd[0] = '\0';

    if (repo_root && repo_root[0]) {
        if (settings_canonicalize_dir(repo_root, canon_root, canon_root_sz) != 0) {
            snprintf(canon_root, canon_root_sz, "%s", repo_root);
        }
    }

    if (!req_cwd || !req_cwd[0]) {
        snprintf(out_cwd, out_cwd_sz, "%s", canon_root);
        return;
    }

    snprintf(raw, sizeof(raw), "%s", req_cwd);
    trim_spaces(raw);
    if (!raw[0]) {
        snprintf(out_cwd, out_cwd_sz, "%s", canon_root);
        return;
    }

    if (raw[0] == '~' && (raw[1] == '\0' || raw[1] == '/' || raw[1] == '\\')) {
        const char *home = getenv("HOME");
#ifdef _WIN32
        if (!home || !home[0]) home = getenv("USERPROFILE");
#endif
        if (home && home[0]) {
            if (raw[1] == '\0') {
                snprintf(candidate, sizeof(candidate), "%s", home);
            } else {
                snprintf(candidate, sizeof(candidate), "%s/%s", home, raw + 2);
            }
        } else {
            snprintf(candidate, sizeof(candidate), "%s", canon_root);
        }
    } else if (is_abs_path(raw)) {
        snprintf(candidate, sizeof(candidate), "%s", raw);
    } else {
        size_t rl = strlen(canon_root);
        size_t wl = strlen(raw);
        if (rl + 1 + wl + 1 <= sizeof(candidate)) {
            memcpy(candidate, canon_root, rl);
            candidate[rl] = '/';
            memcpy(candidate + rl + 1, raw, wl + 1);
        } else {
            snprintf(candidate, sizeof(candidate), "%s", canon_root);
        }
    }

    if (settings_canonicalize_dir(candidate, out_cwd, out_cwd_sz) != 0) {
        snprintf(out_cwd, out_cwd_sz, "%s", canon_root);
    }
}

static const char *detect_platform(void) {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "posix";
#endif
}

static const char *detect_shell_name(void) {
#ifdef _WIN32
    return "cmd.exe";
#else
    if (access("/bin/bash", X_OK) == 0) return "bash";
    return "sh";
#endif
}

static void detect_user_host(char *user, size_t user_sz, char *host, size_t host_sz) {
    const char *u = getenv("USER");
    const char *h = NULL;
    if (!u || !u[0]) u = getenv("LOGNAME");
    if (!u || !u[0]) u = getenv("USERNAME");
    snprintf(user, user_sz, "%s", (u && u[0]) ? u : "studio");

    host[0] = '\0';
#ifndef _WIN32
    if (gethostname(host, host_sz - 1) == 0) {
        host[host_sz - 1] = '\0';
    }
#endif
    if (!host[0]) {
        h = getenv("HOSTNAME");
        if (!h || !h[0]) h = getenv("COMPUTERNAME");
        snprintf(host, host_sz, "%s", (h && h[0]) ? h : "localhost");
    }
}

int terminal_info(const char *repo_root, const char *req_cwd, JsonDoc **out,
                  char *err, size_t err_sz) {
    char canon_root[PATH_MAX];
    char cwd[PATH_MAX];
    char user[128];
    char host[128];
    JsonValue *root;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }

    resolve_cwd_or_root(repo_root, req_cwd, canon_root, sizeof(canon_root), cwd, sizeof(cwd));
    detect_user_host(user, sizeof(user), host, sizeof(host));

    root = json_object();
    if (!root) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    jput_b(root, "ok", 1);
    jput_s(root, "cwd", cwd);
    jput_s(root, "repoRoot", canon_root);
    jput_s(root, "platform", detect_platform());
    jput_s(root, "shell", detect_shell_name());
    jput_s(root, "user", user);
    jput_s(root, "host", host);
    jput_i(root, "defaultTimeoutMs", TERMINAL_DEFAULT_TIMEOUT_MS);
    jput_i(root, "maxOutputBytes", (long long)TERMINAL_MAX_OUTPUT);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

typedef struct TerminalJob {
    int id;
    int in_use;
    int running;
    int worker_active;
    int stop_requested;
    int timeout_ms;
    long long start_ms;
    long long duration_ms;
    long long finished_ms;
    char *command;
    char repo_root[PATH_MAX];
    char start_cwd[PATH_MAX];
    char final_cwd[PATH_MAX];
    TBuf out_buf;
    int exit_code;
    int timed_out;
    int truncated;
#ifdef _WIN32
    HANDLE h_process;
    HANDLE h_thread;
    HANDLE out_read;
#else
    pid_t pid;
    int out_fd;
    int cwd_fd;
#endif
} TerminalJob;

static TerminalJob g_jobs[TERMINAL_MAX_JOBS];
static int g_next_job_id = 1;

#ifdef _WIN32
static CRITICAL_SECTION g_jobs_cs;
static volatile LONG g_jobs_cs_init = 0;

static void job_lock(void) {
    while (InterlockedCompareExchange(&g_jobs_cs_init, 1, 0) != 2) {
        if (InterlockedCompareExchange(&g_jobs_cs_init, 0, 0) == 1) {
            InitializeCriticalSection(&g_jobs_cs);
            InterlockedExchange(&g_jobs_cs_init, 2);
            break;
        }
        Sleep(1);
    }
    EnterCriticalSection(&g_jobs_cs);
}

static void job_unlock(void) {
    LeaveCriticalSection(&g_jobs_cs);
}
#else
static pthread_mutex_t g_jobs_mutex = PTHREAD_MUTEX_INITIALIZER;

static void job_lock(void) {
    pthread_mutex_lock(&g_jobs_mutex);
}

static void job_unlock(void) {
    pthread_mutex_unlock(&g_jobs_mutex);
}
#endif

static void job_reset_unlocked(TerminalJob *job) {
    if (!job) return;
    free(job->command);
    job->command = NULL;
    tbuf_free(&job->out_buf);
    memset(job, 0, sizeof(*job));
#ifndef _WIN32
    job->pid = -1;
    job->out_fd = -1;
    job->cwd_fd = -1;
#endif
}

static TerminalJob *job_alloc_unlocked(void) {
    size_t i;
    TerminalJob *oldest = NULL;

    for (i = 0; i < TERMINAL_MAX_JOBS; i++) {
        if (!g_jobs[i].in_use) {
            job_reset_unlocked(&g_jobs[i]);
            g_jobs[i].in_use = 1;
            g_jobs[i].id = g_next_job_id++;
            if (g_next_job_id <= 0) g_next_job_id = 1;
            return &g_jobs[i];
        }
    }

    for (i = 0; i < TERMINAL_MAX_JOBS; i++) {
        if (!g_jobs[i].running && !g_jobs[i].worker_active) {
            if (!oldest || g_jobs[i].finished_ms < oldest->finished_ms) {
                oldest = &g_jobs[i];
            }
        }
    }

    if (oldest) {
        job_reset_unlocked(oldest);
        oldest->in_use = 1;
        oldest->id = g_next_job_id++;
        if (g_next_job_id <= 0) g_next_job_id = 1;
        return oldest;
    }

    return NULL;
}

static TerminalJob *job_find_unlocked(int id) {
    size_t i;
    if (id <= 0) return NULL;
    for (i = 0; i < TERMINAL_MAX_JOBS; i++) {
        if (g_jobs[i].in_use && g_jobs[i].id == id) {
            return &g_jobs[i];
        }
    }
    return NULL;
}

/* Ensure we never slice in the middle of an incomplete multi-byte UTF-8 character while running. */
static size_t utf8_safe_end(const char *data, size_t offset, size_t len, int is_running) {
    size_t back;
    if (!is_running || !data || len <= offset) return len;
    back = 0;
    while (back < 3 && (len - 1 - back) >= offset) {
        unsigned char c = (unsigned char)data[len - 1 - back];
        if ((c & 0xC0u) == 0x80u) {
            back++;
        } else {
            size_t seq_avail = back + 1;
            size_t seq_need = 1;
            if ((c & 0x80u) == 0x00u) seq_need = 1;
            else if ((c & 0xE0u) == 0xC0u) seq_need = 2;
            else if ((c & 0xF0u) == 0xE0u) seq_need = 3;
            else if ((c & 0xF8u) == 0xF0u) seq_need = 4;
            if (seq_avail < seq_need) {
                return len - seq_avail;
            }
            return len;
        }
    }
    return len;
}

#ifndef _WIN32
static void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

static int drain_fd(int fd, TBuf *buf, size_t max_bytes, int *truncated) {
    char tmp[4096];
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        if (n == 0) return -1;
        if (buf->len < max_bytes) {
            size_t take = (size_t)n;
            if (buf->len + take > max_bytes) {
                take = max_bytes - buf->len;
                if (truncated) *truncated = 1;
            }
            (void)tbuf_append(buf, tmp, take);
        } else if (truncated) {
            *truncated = 1;
        }
    }
}

static int drain_fd_job(int fd, TerminalJob *job, int use_lock) {
    char tmp[4096];
    for (;;) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1; /* EIO when slave PTY closes, or error */
        }
        if (n == 0) return -1; /* EOF */
        if (use_lock) job_lock();
        if (job->out_buf.len < TERMINAL_MAX_OUTPUT) {
            size_t take = (size_t)n;
            if (job->out_buf.len + take > TERMINAL_MAX_OUTPUT) {
                take = TERMINAL_MAX_OUTPUT - job->out_buf.len;
                job->truncated = 1;
            }
            (void)tbuf_append(&job->out_buf, tmp, take);
        } else {
            job->truncated = 1;
        }
        if (use_lock) job_unlock();
    }
}

static void extract_last_cwd_line(const char *raw, const char *fallback,
                                  char *out_cwd, size_t out_cwd_sz) {
    char line[PATH_MAX];
    const char *p;
    const char *last_start = NULL;
    size_t last_len = 0;

    snprintf(out_cwd, out_cwd_sz, "%s", fallback);
    if (!raw || !raw[0]) return;

    p = raw;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        while (len > 0 && (p[len - 1] == '\r' || p[len - 1] == '\n')) len--;
        if (len > 0) {
            last_start = p;
            last_len = len;
        }
        if (!nl) break;
        p = nl + 1;
    }

    if (!last_start || last_len == 0 || last_len >= sizeof(line)) return;
    memcpy(line, last_start, last_len);
    line[last_len] = '\0';

    if (settings_canonicalize_dir(line, out_cwd, out_cwd_sz) != 0) {
        snprintf(out_cwd, out_cwd_sz, "%s", fallback);
    }
}

/* Open a pseudo-terminal pair so child stdout/stderr are unified in chronological order
 * and isatty(1)/isatty(2) are true for progress bars (deploy_push.py, curl -#, tqdm, git).
 * Falls back to a unified pipe if /dev/ptmx is unavailable.
 */
static int open_child_tty_or_pipe(int *read_fd_out, int *write_fd_out) {
    int master_fd;
    int slave_fd;
    char *slave_name;
    struct termios tio;
    struct winsize ws;
    int pipe_fds[2];

    *read_fd_out = -1;
    *write_fd_out = -1;

    master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd >= 0) {
        if (grantpt(master_fd) == 0 && unlockpt(master_fd) == 0) {
            slave_name = ptsname(master_fd);
            if (slave_name) {
                slave_fd = open(slave_name, O_RDWR | O_NOCTTY);
                if (slave_fd >= 0) {
                    if (tcgetattr(slave_fd, &tio) == 0) {
                        tio.c_lflag &= ~(tcflag_t)(ECHO | ECHOE | ECHOK | ECHONL | ICANON);
                        tio.c_oflag &= ~(tcflag_t)(ONLCR);
                        (void)tcsetattr(slave_fd, TCSANOW, &tio);
                    }
                    memset(&ws, 0, sizeof(ws));
                    ws.ws_row = 24;
                    ws.ws_col = 100;
                    (void)ioctl(slave_fd, TIOCSWINSZ, &ws);
                    *read_fd_out = master_fd;
                    *write_fd_out = slave_fd;
                    return 0;
                }
            }
        }
        close(master_fd);
    }

    if (pipe(pipe_fds) != 0) return -1;
    *read_fd_out = pipe_fds[0];
    *write_fd_out = pipe_fds[1];
    return 0;
}

static int spawn_job_posix(TerminalJob *job) {
    int out_read_fd = -1;
    int out_write_fd = -1;
    int cwd_fds[2] = {-1, -1};
    pid_t pid;
    TBuf script;

    if (open_child_tty_or_pipe(&out_read_fd, &out_write_fd) != 0) return -1;
    if (pipe(cwd_fds) != 0) {
        close(out_read_fd);
        close(out_write_fd);
        return -1;
    }

    tbuf_init(&script);
    if (tbuf_puts(&script, "trap '__studio_ec=$?; pwd -P >&3 2>/dev/null; exit $__studio_ec' EXIT\n") != 0 ||
        tbuf_puts(&script, job->command) != 0) {
        tbuf_free(&script);
        close(out_read_fd);
        close(out_write_fd);
        close(cwd_fds[0]);
        close(cwd_fds[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        tbuf_free(&script);
        close(out_read_fd);
        close(out_write_fd);
        close(cwd_fds[0]);
        close(cwd_fds[1]);
        return -1;
    }

    if (pid == 0) {
        int devnull;
        const char *shell_bin = (access("/bin/bash", X_OK) == 0) ? "/bin/bash" : "/bin/sh";

        if (setsid() < 0) {
            setpgid(0, 0);
        }
        devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        if (dup2(out_write_fd, STDOUT_FILENO) < 0) _exit(127);
        if (dup2(out_write_fd, STDERR_FILENO) < 0) _exit(127);

        close(out_read_fd);
        if (out_write_fd > STDERR_FILENO) close(out_write_fd);
        close(cwd_fds[0]);

        if (cwd_fds[1] != 3) {
            if (dup2(cwd_fds[1], 3) < 0) _exit(127);
            close(cwd_fds[1]);
        }

        if (job->start_cwd[0]) {
            if (chdir(job->start_cwd) != 0) {
                const char *msg = "terminal: could not change to working directory\n";
                ssize_t ign = write(STDERR_FILENO, msg, strlen(msg));
                (void)ign;
                _exit(1);
            }
            setenv("PWD", job->start_cwd, 1);
        }
        setenv("TERM", "xterm-256color", 1);
        setenv("COLUMNS", "100", 1);
        setenv("LINES", "24", 1);
        setenv("PAGER", "cat", 1);
        setenv("GIT_PAGER", "cat", 1);
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        setenv("PYTHONUNBUFFERED", "1", 1);

        if (strcmp(shell_bin, "/bin/bash") == 0) {
            execl(shell_bin, "bash", "--norc", "--noprofile", "-c", script.data, (char *)NULL);
        } else {
            execl(shell_bin, "sh", "-c", script.data, (char *)NULL);
        }
        _exit(127);
    }

    tbuf_free(&script);
    close(out_write_fd);
    close(cwd_fds[1]);

    set_nonblocking(out_read_fd);
    set_nonblocking(cwd_fds[0]);

    job->pid = pid;
    job->out_fd = out_read_fd;
    job->cwd_fd = cwd_fds[0];
    return 0;
}

static void monitor_job_posix(TerminalJob *job, int use_lock) {
    TBuf cwd_buf;
    char final_cwd[PATH_MAX];
    int out_open = 1;
    int cwd_open = 1;
    int child_exited = 0;
    int status = 0;
    int timed_out = 0;
    int stopped = 0;
    int exit_code = 0;
    long long duration_ms;

    tbuf_init(&cwd_buf);

    while (out_open || !child_exited) {
        struct pollfd pfds[2];
        nfds_t nfds = 0;
        long long elapsed = term_now_ms() - job->start_ms;
        int wait_ms = 20;
        int stop_req = 0;

        if (use_lock) {
            job_lock();
            stop_req = job->stop_requested;
            job_unlock();
        } else {
            stop_req = job->stop_requested;
        }

        if (stop_req || elapsed >= (long long)job->timeout_ms) {
            if (stop_req) stopped = 1;
            else timed_out = 1;
            kill(-job->pid, SIGKILL);
            kill(job->pid, SIGKILL);
            while (waitpid(job->pid, &status, 0) < 0 && errno == EINTR) {}
            child_exited = 1;
            break;
        }

        if (out_open) {
            pfds[nfds].fd = job->out_fd;
            pfds[nfds].events = POLLIN | POLLHUP;
            pfds[nfds].revents = 0;
            nfds++;
        }
        if (cwd_open) {
            pfds[nfds].fd = job->cwd_fd;
            pfds[nfds].events = POLLIN | POLLHUP;
            pfds[nfds].revents = 0;
            nfds++;
        }

        if (nfds > 0) {
            long long rem = (long long)job->timeout_ms - elapsed;
            if (rem < (long long)wait_ms) wait_ms = (int)rem;
            if (wait_ms < 1) wait_ms = 1;
            (void)poll(pfds, nfds, wait_ms);
        }

        if (out_open && drain_fd_job(job->out_fd, job, use_lock) != 0) {
            close(job->out_fd);
            job->out_fd = -1;
            out_open = 0;
        }
        if (cwd_open && drain_fd(job->cwd_fd, &cwd_buf, PATH_MAX * 2, NULL) != 0) {
            close(job->cwd_fd);
            job->cwd_fd = -1;
            cwd_open = 0;
        }

        if (!child_exited) {
            pid_t w = waitpid(job->pid, &status, WNOHANG);
            if (w == job->pid) {
                child_exited = 1;
                if (out_open) (void)drain_fd_job(job->out_fd, job, use_lock);
                if (cwd_open) (void)drain_fd(job->cwd_fd, &cwd_buf, PATH_MAX * 2, NULL);
                break;
            }
        }
    }

    if (out_open && job->out_fd >= 0) {
        close(job->out_fd);
        job->out_fd = -1;
    }
    if (cwd_open && job->cwd_fd >= 0) {
        close(job->cwd_fd);
        job->cwd_fd = -1;
    }

    if (!child_exited) {
        while (waitpid(job->pid, &status, 0) < 0 && errno == EINTR) {}
    }

    extract_last_cwd_line(cwd_buf.data, job->start_cwd, final_cwd, sizeof(final_cwd));
    tbuf_free(&cwd_buf);

    if (timed_out) {
        exit_code = 124;
    } else if (stopped) {
        exit_code = 130;
    } else if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
    } else {
        exit_code = -1;
    }

    duration_ms = term_now_ms() - job->start_ms;
    if (duration_ms < 0) duration_ms = 0;

    if (use_lock) job_lock();
    snprintf(job->final_cwd, sizeof(job->final_cwd), "%s", final_cwd);
    job->exit_code = exit_code;
    job->timed_out = timed_out;
    job->duration_ms = duration_ms;
    job->running = 0;
    job->worker_active = 0;
    job->finished_ms = term_now_ms();
    if (use_lock) job_unlock();
}

static void *job_thread_posix(void *arg) {
    TerminalJob *job = (TerminalJob *)arg;
    monitor_job_posix(job, 1);
    return NULL;
}

#else
#define WIN_CWD_MARKER "__STUDIO_CWD_MARKER__"

static size_t visible_win_output_len(const TBuf *out_buf) {
    const char *marker;
    if (!out_buf || !out_buf->data || out_buf->len == 0) return 0;
    marker = strstr(out_buf->data, WIN_CWD_MARKER);
    if (!marker) return out_buf->len;
    while (marker > out_buf->data && (marker[-1] == '\n' || marker[-1] == '\r')) {
        marker--;
    }
    return (size_t)(marker - out_buf->data);
}

static void extract_win_cwd_marker(TBuf *out_buf, const char *fallback,
                                   char *final_cwd, size_t final_cwd_sz) {
    char *marker;
    snprintf(final_cwd, final_cwd_sz, "%s", fallback);
    if (!out_buf || !out_buf->data) return;

    marker = strstr(out_buf->data, WIN_CWD_MARKER);
    if (!marker) return;

    {
        char *path_start = marker + strlen(WIN_CWD_MARKER);
        char *end = path_start;
        char candidate[PATH_MAX];
        size_t len;
        while (*end && *end != '\r' && *end != '\n') end++;
        len = (size_t)(end - path_start);
        if (len > 0 && len < sizeof(candidate)) {
            memcpy(candidate, path_start, len);
            candidate[len] = '\0';
            trim_spaces(candidate);
            if (settings_canonicalize_dir(candidate, final_cwd, final_cwd_sz) != 0) {
                snprintf(final_cwd, final_cwd_sz, "%s", fallback);
            }
        }
        if (marker > out_buf->data && marker[-1] == '\n') {
            marker--;
            if (marker > out_buf->data && marker[-1] == '\r') marker--;
        }
        *marker = '\0';
        out_buf->len = (size_t)(marker - out_buf->data);
    }
}

static int spawn_job_win(TerminalJob *job) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE out_read = NULL, out_write = NULL;
    HANDLE nul_in = NULL;
    TBuf cmd;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    if (!CreatePipe(&out_read, &out_write, &sa, 0)) return -1;
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

    nul_in = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    tbuf_init(&cmd);
    if (tbuf_puts(&cmd, "cmd.exe /v:on /d /s /c \"") != 0 ||
        tbuf_puts(&cmd, job->command) != 0 ||
        tbuf_puts(&cmd, " & echo " WIN_CWD_MARKER "!CD!& exit /b !ERRORLEVEL!\"") != 0) {
        tbuf_free(&cmd);
        CloseHandle(out_read);
        CloseHandle(out_write);
        if (nul_in && nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
        return -1;
    }

    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = (nul_in && nul_in != INVALID_HANDLE_VALUE) ? nul_in : NULL;
    si.hStdOutput = out_write;
    si.hStdError = out_write;

    if (!CreateProcessA(NULL, cmd.data, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL,
                        job->start_cwd[0] ? job->start_cwd : NULL, &si, &pi)) {
        tbuf_free(&cmd);
        CloseHandle(out_read);
        CloseHandle(out_write);
        if (nul_in && nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);
        return -1;
    }
    tbuf_free(&cmd);
    CloseHandle(out_write);
    if (nul_in && nul_in != INVALID_HANDLE_VALUE) CloseHandle(nul_in);

    job->h_process = pi.hProcess;
    job->h_thread = pi.hThread;
    job->out_read = out_read;
    return 0;
}

static void monitor_job_win(TerminalJob *job, int use_lock) {
    DWORD code = 1;
    int timed_out = 0;
    int stopped = 0;
    char final_cwd[PATH_MAX];
    long long duration_ms;

    for (;;) {
        DWORD avail = 0;
        char tmp[4096];
        DWORD got = 0;
        int did_read = 0;
        int stop_req = 0;

        if (use_lock) {
            job_lock();
            stop_req = job->stop_requested;
            job_unlock();
        } else {
            stop_req = job->stop_requested;
        }

        if (stop_req || (term_now_ms() - job->start_ms >= (long long)job->timeout_ms)) {
            if (stop_req) stopped = 1;
            else timed_out = 1;
            TerminateProcess(job->h_process, stop_req ? 130 : 124);
            WaitForSingleObject(job->h_process, 2000);
            break;
        }

        if (PeekNamedPipe(job->out_read, NULL, 0, NULL, &avail, NULL) && avail > 0) {
            if (ReadFile(job->out_read, tmp, sizeof(tmp), &got, NULL) && got > 0) {
                did_read = 1;
                if (use_lock) job_lock();
                if (job->out_buf.len < TERMINAL_MAX_OUTPUT) {
                    size_t take = (size_t)got;
                    if (job->out_buf.len + take > TERMINAL_MAX_OUTPUT) {
                        take = TERMINAL_MAX_OUTPUT - job->out_buf.len;
                        job->truncated = 1;
                    }
                    (void)tbuf_append(&job->out_buf, tmp, take);
                } else {
                    job->truncated = 1;
                }
                if (use_lock) job_unlock();
            }
        }

        if (WaitForSingleObject(job->h_process, did_read ? 0 : 15) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(job->out_read, NULL, 0, NULL, &avail, NULL) && avail > 0 &&
                   ReadFile(job->out_read, tmp, sizeof(tmp), &got, NULL) && got > 0) {
                if (use_lock) job_lock();
                if (job->out_buf.len < TERMINAL_MAX_OUTPUT) {
                    size_t take = (size_t)got;
                    if (job->out_buf.len + take > TERMINAL_MAX_OUTPUT) {
                        take = TERMINAL_MAX_OUTPUT - job->out_buf.len;
                        job->truncated = 1;
                    }
                    (void)tbuf_append(&job->out_buf, tmp, take);
                } else {
                    job->truncated = 1;
                }
                if (use_lock) job_unlock();
            }
            break;
        }
    }

    CloseHandle(job->out_read);
    job->out_read = NULL;
    GetExitCodeProcess(job->h_process, &code);
    CloseHandle(job->h_process);
    CloseHandle(job->h_thread);
    job->h_process = NULL;
    job->h_thread = NULL;

    duration_ms = term_now_ms() - job->start_ms;
    if (duration_ms < 0) duration_ms = 0;

    if (use_lock) job_lock();
    extract_win_cwd_marker(&job->out_buf, job->start_cwd, final_cwd, sizeof(final_cwd));
    snprintf(job->final_cwd, sizeof(job->final_cwd), "%s", final_cwd);
    job->exit_code = timed_out ? 124 : (stopped ? 130 : (int)code);
    job->timed_out = timed_out;
    job->duration_ms = duration_ms;
    job->running = 0;
    job->worker_active = 0;
    job->finished_ms = term_now_ms();
    if (use_lock) job_unlock();
}

static unsigned __stdcall job_thread_win(void *arg) {
    TerminalJob *job = (TerminalJob *)arg;
    monitor_job_win(job, 1);
    return 0;
}
#endif

static int parse_exec_request(const char *repo_root, const JsonValue *body,
                              char **cmd_out, char *canon_root, size_t canon_root_sz,
                              char *start_cwd, size_t start_cwd_sz, int *timeout_out,
                              char *err, size_t err_sz) {
    JsonValue *cmd_v;
    JsonValue *cwd_v;
    JsonValue *timeout_v;
    const char *raw_cmd;
    const char *raw_cwd = NULL;
    char *cmd_copy;
    int timeout_ms = TERMINAL_DEFAULT_TIMEOUT_MS;

    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }
    if (!body || json_type(body) != JSON_OBJECT) {
        set_err(err, err_sz, "JSON body must be an object");
        return 1;
    }

    cmd_v = json_object_get(body, "command");
    raw_cmd = (cmd_v && json_type(cmd_v) == JSON_STRING) ? json_string_get(cmd_v, NULL) : NULL;
    if (!raw_cmd) {
        set_err(err, err_sz, "command is required");
        return 1;
    }
    cmd_copy = strdup(raw_cmd);
    if (!cmd_copy) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    trim_spaces(cmd_copy);
    if (!cmd_copy[0]) {
        free(cmd_copy);
        set_err(err, err_sz, "command is required");
        return 1;
    }

    cwd_v = json_object_get(body, "cwd");
    if (cwd_v && json_type(cwd_v) == JSON_STRING) {
        raw_cwd = json_string_get(cwd_v, NULL);
    }

    timeout_v = json_object_get(body, "timeoutMs");
    if (timeout_v && json_type(timeout_v) == JSON_NUMBER) {
        long long val = 0;
        if (json_as_int(timeout_v, &val) == 0) {
            if (val < TERMINAL_MIN_TIMEOUT_MS) val = TERMINAL_MIN_TIMEOUT_MS;
            if (val > TERMINAL_MAX_TIMEOUT_MS) val = TERMINAL_MAX_TIMEOUT_MS;
            timeout_ms = (int)val;
        }
    }

    resolve_cwd_or_root(repo_root, raw_cwd, canon_root, canon_root_sz, start_cwd, start_cwd_sz);
    *cmd_out = cmd_copy;
    *timeout_out = timeout_ms;
    return 0;
}

int terminal_exec(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz) {
    TerminalJob job;
    char *cmd_copy = NULL;
    char canon_root[PATH_MAX];
    char start_cwd[PATH_MAX];
    int timeout_ms = TERMINAL_DEFAULT_TIMEOUT_MS;
    int rc;
    JsonValue *root;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';

    rc = parse_exec_request(repo_root, body, &cmd_copy, canon_root, sizeof(canon_root),
                            start_cwd, sizeof(start_cwd), &timeout_ms, err, err_sz);
    if (rc != 0) return rc;

    memset(&job, 0, sizeof(job));
    tbuf_init(&job.out_buf);
    job.command = cmd_copy;
    snprintf(job.repo_root, sizeof(job.repo_root), "%s", canon_root);
    snprintf(job.start_cwd, sizeof(job.start_cwd), "%s", start_cwd);
    snprintf(job.final_cwd, sizeof(job.final_cwd), "%s", start_cwd);
    job.timeout_ms = timeout_ms;
    job.start_ms = term_now_ms();
    job.running = 1;

#ifdef _WIN32
    rc = spawn_job_win(&job);
    if (rc == 0) monitor_job_win(&job, 0);
#else
    rc = spawn_job_posix(&job);
    if (rc == 0) monitor_job_posix(&job, 0);
#endif

    if (rc != 0) {
        free(job.command);
        tbuf_free(&job.out_buf);
        set_err(err, err_sz, "Failed to spawn terminal shell");
        return -1;
    }

    root = json_object();
    if (!root) {
        free(job.command);
        tbuf_free(&job.out_buf);
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    jput_b(root, "ok", 1);
    jput_s(root, "command", job.command);
    jput_s(root, "cwd", job.final_cwd);
    jput_s(root, "repoRoot", job.repo_root);
    jput_s(root, "stdout", job.out_buf.data ? job.out_buf.data : "");
    jput_s(root, "stderr", "");
    jput_s(root, "output", job.out_buf.data ? job.out_buf.data : "");
    jput_i(root, "exitCode", (long long)job.exit_code);
    jput_i(root, "durationMs", job.duration_ms);
    jput_b(root, "timedOut", job.timed_out);
    jput_b(root, "truncated", job.truncated);

    free(job.command);
    tbuf_free(&job.out_buf);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

int terminal_start(const char *repo_root, const JsonValue *body, JsonDoc **out,
                   char *err, size_t err_sz) {
    TerminalJob *job;
    char *cmd_copy = NULL;
    char canon_root[PATH_MAX];
    char start_cwd[PATH_MAX];
    int timeout_ms = TERMINAL_DEFAULT_TIMEOUT_MS;
    int job_id;
    int rc;
    JsonValue *root;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';

    rc = parse_exec_request(repo_root, body, &cmd_copy, canon_root, sizeof(canon_root),
                            start_cwd, sizeof(start_cwd), &timeout_ms, err, err_sz);
    if (rc != 0) return rc;

    root = json_object();
    if (!root) {
        free(cmd_copy);
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    job_lock();
    job = job_alloc_unlocked();
    if (!job) {
        job_unlock();
        json_free(root);
        free(cmd_copy);
        set_err(err, err_sz, "Too many concurrent terminal commands");
        return -1;
    }

    tbuf_init(&job->out_buf);
    job->command = cmd_copy;
    snprintf(job->repo_root, sizeof(job->repo_root), "%s", canon_root);
    snprintf(job->start_cwd, sizeof(job->start_cwd), "%s", start_cwd);
    snprintf(job->final_cwd, sizeof(job->final_cwd), "%s", start_cwd);
    job->timeout_ms = timeout_ms;
    job->start_ms = term_now_ms();
    job->running = 1;
    job->worker_active = 1;
    job_id = job->id;

    jput_b(root, "ok", 1);
    jput_i(root, "id", (long long)job_id);
    jput_s(root, "command", cmd_copy);
    jput_s(root, "cwd", start_cwd);
    jput_s(root, "repoRoot", canon_root);
    jput_b(root, "running", 1);

#ifdef _WIN32
    rc = spawn_job_win(job);
#else
    rc = spawn_job_posix(job);
#endif
    if (rc != 0) {
        job_reset_unlocked(job);
        job_unlock();
        json_free(root);
        set_err(err, err_sz, "Failed to spawn terminal shell");
        return -1;
    }
    job_unlock();

#ifdef _WIN32
    {
        uintptr_t th = _beginthreadex(NULL, 0, job_thread_win, job, 0, NULL);
        if (th == 0) {
            monitor_job_win(job, 1);
        } else {
            CloseHandle((HANDLE)th);
        }
    }
#else
    {
        pthread_t th;
        if (pthread_create(&th, NULL, job_thread_posix, job) != 0) {
            monitor_job_posix(job, 1);
        } else {
            pthread_detach(th);
        }
    }
#endif

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

int terminal_poll(int job_id, size_t offset, JsonDoc **out,
                  char *err, size_t err_sz) {
    TerminalJob *job;
    JsonValue *root;
    size_t avail_len;
    size_t safe_end;
    size_t chunk_len = 0;
    long long duration_ms;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (job_id <= 0) {
        set_err(err, err_sz, "Job id is required");
        return 1;
    }

    job_lock();
    job = job_find_unlocked(job_id);
    if (!job) {
        job_unlock();
        set_err(err, err_sz, "Terminal job not found");
        return 2;
    }

#ifdef _WIN32
    avail_len = job->running ? visible_win_output_len(&job->out_buf) : job->out_buf.len;
#else
    avail_len = job->out_buf.len;
#endif
    if (offset > avail_len) offset = avail_len;
    safe_end = utf8_safe_end(job->out_buf.data, offset, avail_len, job->running);
    if (safe_end > offset) chunk_len = safe_end - offset;

    duration_ms = job->running ? (term_now_ms() - job->start_ms) : job->duration_ms;
    if (duration_ms < 0) duration_ms = 0;

    root = json_object();
    if (!root) {
        job_unlock();
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    jput_b(root, "ok", 1);
    jput_i(root, "id", (long long)job->id);
    jput_s(root, "command", job->command ? job->command : "");
    jput_s(root, "cwd", job->final_cwd);
    jput_s(root, "repoRoot", job->repo_root);
    jput_b(root, "running", job->running);
    jput_s_len(root, "chunk",
               (job->out_buf.data && chunk_len > 0) ? (job->out_buf.data + offset) : "",
               chunk_len);
    jput_i(root, "offset", (long long)(offset + chunk_len));
    jput_i(root, "durationMs", duration_ms);
    jput_b(root, "timedOut", job->timed_out);
    jput_b(root, "truncated", job->truncated);
    if (!job->running) {
        jput_i(root, "exitCode", (long long)job->exit_code);
        jput_s(root, "stdout", job->out_buf.data ? job->out_buf.data : "");
        jput_s(root, "stderr", "");
        jput_s(root, "output", job->out_buf.data ? job->out_buf.data : "");
    }

    job_unlock();

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

int terminal_stop(int job_id, JsonDoc **out, char *err, size_t err_sz) {
    TerminalJob *job;
    JsonValue *root;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (job_id <= 0) {
        set_err(err, err_sz, "Job id is required");
        return 1;
    }

    job_lock();
    job = job_find_unlocked(job_id);
    if (!job) {
        job_unlock();
        set_err(err, err_sz, "Terminal job not found");
        return 2;
    }

    job->stop_requested = 1;
#ifndef _WIN32
    if (job->running && job->pid > 0) {
        kill(-job->pid, SIGTERM);
        kill(job->pid, SIGTERM);
    }
#endif
    job_unlock();

    root = json_object();
    if (!root) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    jput_b(root, "ok", 1);
    jput_i(root, "id", (long long)job_id);
    jput_b(root, "stopped", 1);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}
