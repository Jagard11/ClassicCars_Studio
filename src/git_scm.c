#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "git_scm.h"

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
#include <windows.h>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

#include "compat.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define MAX_GIT_OUTPUT (2u * 1024u * 1024u)

typedef struct GBuf {
    char *data;
    size_t len;
    size_t cap;
} GBuf;

static void gbuf_init(GBuf *b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static void gbuf_free(GBuf *b) {
    if (!b) return;
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static int gbuf_reserve(GBuf *b, size_t add) {
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

static int gbuf_append(GBuf *b, const char *s, size_t n) {
    if (gbuf_reserve(b, n) != 0) return -1;
    if (n) memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int gbuf_putc(GBuf *b, char c) {
    return gbuf_append(b, &c, 1);
}

static int gbuf_puts(GBuf *b, const char *s) {
    return s ? gbuf_append(b, s, strlen(s)) : 0;
}

static char *gbuf_steal(GBuf *b) {
    char *out;
    if (!b) return NULL;
    if (!b->data) return strdup("");
    out = b->data;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    return out;
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

#ifdef _WIN32
static int win_append_arg(GBuf *cmd, const char *arg) {
    const char *p;
    int need_quote = (*arg == '\0');
    for (p = arg; *p; p++) {
        if (*p == ' ' || *p == '\t' || *p == '"') {
            need_quote = 1;
            break;
        }
    }
    if (!need_quote) return gbuf_puts(cmd, arg);
    if (gbuf_putc(cmd, '"') != 0) return -1;
    for (p = arg; *p; p++) {
        size_t slashes = 0;
        while (*p == '\\') {
            slashes++;
            p++;
        }
        if (!*p) {
            while (slashes-- > 0) {
                if (gbuf_puts(cmd, "\\\\") != 0) return -1;
            }
            break;
        }
        if (*p == '"') {
            while (slashes-- > 0) {
                if (gbuf_puts(cmd, "\\\\") != 0) return -1;
            }
            if (gbuf_puts(cmd, "\\\"") != 0) return -1;
        } else {
            while (slashes-- > 0) {
                if (gbuf_putc(cmd, '\\') != 0) return -1;
            }
            if (gbuf_putc(cmd, *p) != 0) return -1;
        }
    }
    return gbuf_putc(cmd, '"');
}

static int git_exec_capture_timeout(char *const argv[], int merge_stderr, int timeout_ms,
                                    char **out, int *exit_code) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_h = NULL;
    HANDLE write_h = NULL;
    HANDLE nul_h = NULL;
    GBuf cmd;
    GBuf buf;
    char tmp[4096];
    DWORD got = 0;
    DWORD code = 1;
    DWORD wait_ms = timeout_ms > 0 ? (DWORD)timeout_ms : 30000;
    size_t i;

    if (out) *out = NULL;
    if (exit_code) *exit_code = -1;
    SetEnvironmentVariableA("GIT_TERMINAL_PROMPT", "0");
    SetEnvironmentVariableA("GCM_INTERACTIVE", "never");
    gbuf_init(&cmd);
    for (i = 0; argv[i]; i++) {
        if (i && gbuf_putc(&cmd, ' ') != 0) {
            gbuf_free(&cmd);
            return -1;
        }
        if (win_append_arg(&cmd, argv[i]) != 0) {
            gbuf_free(&cmd);
            return -1;
        }
    }
    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read_h, &write_h, &sa, 0)) {
        gbuf_free(&cmd);
        return -1;
    }
    SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0);
    if (!merge_stderr) {
        nul_h = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    }
    memset(&si, 0, sizeof(si));
    memset(&pi, 0, sizeof(pi));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = NULL;
    si.hStdOutput = write_h;
    si.hStdError = merge_stderr ? write_h : nul_h;
    if (!CreateProcessA(NULL, cmd.data, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(read_h);
        CloseHandle(write_h);
        if (nul_h && nul_h != INVALID_HANDLE_VALUE) CloseHandle(nul_h);
        gbuf_free(&cmd);
        return -1;
    }
    gbuf_free(&cmd);
    CloseHandle(write_h);
    if (nul_h && nul_h != INVALID_HANDLE_VALUE) CloseHandle(nul_h);
    gbuf_init(&buf);
    while (ReadFile(read_h, tmp, sizeof(tmp), &got, NULL) && got > 0) {
        if (buf.len < MAX_GIT_OUTPUT) {
            size_t take = (size_t)got;
            if (buf.len + take > MAX_GIT_OUTPUT) take = MAX_GIT_OUTPUT - buf.len;
            if (gbuf_append(&buf, tmp, take) != 0) break;
        }
    }
    CloseHandle(read_h);
    if (WaitForSingleObject(pi.hProcess, wait_ms) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exit_code) *exit_code = (int)code;
    if (out) *out = gbuf_steal(&buf);
    else gbuf_free(&buf);
    return 0;
}
#else
static long long git_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static int git_exec_capture_timeout(char *const argv[], int merge_stderr, int timeout_ms,
                                    char **out, int *exit_code) {
    int fds[2];
    pid_t pid;
    GBuf buf;
    char tmp[4096];
    ssize_t got;
    int status = 0;
    long long deadline;
    int timed_out = 0;

    if (out) *out = NULL;
    if (exit_code) *exit_code = -1;
    if (timeout_ms <= 0) timeout_ms = 30000;
    if (pipe(fds) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        setenv("GIT_TERMINAL_PROMPT", "0", 1);
        setenv("GCM_INTERACTIVE", "never", 1);
        setenv("GIT_SSH_COMMAND", "ssh -oBatchMode=yes", 0);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (merge_stderr) {
            if (dup2(fds[1], STDERR_FILENO) < 0) _exit(127);
        } else if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
        }
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        close(fds[0]);
        close(fds[1]);
        execvp(argv[0], argv);
        _exit(127);
    }
    close(fds[1]);
    gbuf_init(&buf);
    deadline = git_now_ms() + (long long)timeout_ms;
    for (;;) {
        struct pollfd pfd;
        long long rem = deadline - git_now_ms();
        int prc;
        if (rem <= 0) {
            timed_out = 1;
            break;
        }
        pfd.fd = fds[0];
        pfd.events = POLLIN;
        pfd.revents = 0;
        prc = poll(&pfd, 1, rem > 250 ? 250 : (int)rem);
        if (prc < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (prc == 0) continue;
        got = read(fds[0], tmp, sizeof(tmp));
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (got == 0) break;
        if (buf.len < MAX_GIT_OUTPUT) {
            size_t take = (size_t)got;
            if (buf.len + take > MAX_GIT_OUTPUT) take = MAX_GIT_OUTPUT - buf.len;
            if (gbuf_append(&buf, tmp, take) != 0) break;
        }
    }
    close(fds[0]);
    if (timed_out) {
        kill(pid, SIGTERM);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            status = -1;
            break;
        }
    }
    if (exit_code) {
        if (timed_out) *exit_code = 124;
        else if (status >= 0 && WIFEXITED(status)) *exit_code = WEXITSTATUS(status);
        else *exit_code = -1;
    }
    if (out) *out = gbuf_steal(&buf);
    else gbuf_free(&buf);
    return 0;
}
#endif

static int git_exec_capture(char *const argv[], int merge_stderr, char **out, int *exit_code) {
    return git_exec_capture_timeout(argv, merge_stderr, 30000, out, exit_code);
}

static char *git_unquote_path(const char *text, size_t len) {
    GBuf buf;
    size_t i = 0;
    size_t end = len;
    if (len >= 2 && text[0] == '"' && text[len - 1] == '"') {
        i = 1;
        end = len - 1;
    }
    gbuf_init(&buf);
    while (i < end) {
        if (text[i] == '\\' && i + 1 < end) {
            unsigned char esc = (unsigned char)text[i + 1];
            if (esc >= '0' && esc <= '7') {
                int value = esc - '0';
                size_t k;
                i += 2;
                for (k = 0; k < 2 && i < end && text[i] >= '0' && text[i] <= '7'; k++, i++) {
                    value = (value << 3) | (text[i] - '0');
                }
                if (gbuf_putc(&buf, (char)value) != 0) {
                    gbuf_free(&buf);
                    return NULL;
                }
                continue;
            }
            if (esc == 'n') esc = '\n';
            else if (esc == 't') esc = '\t';
            else if (esc == 'r') esc = '\r';
            if (gbuf_putc(&buf, (char)esc) != 0) {
                gbuf_free(&buf);
                return NULL;
            }
            i += 2;
            continue;
        }
        if (gbuf_putc(&buf, text[i]) != 0) {
            gbuf_free(&buf);
            return NULL;
        }
        i++;
    }
    return gbuf_steal(&buf);
}

static int sanitize_rel_path(const char *in, char *out, size_t out_sz) {
    const char *p = in;
    size_t n;
    if (!p || !out || out_sz == 0) return -1;
    while (*p == '/') p++;
    if (!*p || *p == '-') return -1;
    if (strstr(p, "..") != NULL || strchr(p, '\\') != NULL) return -1;
    n = strlen(p);
    if (n >= out_sz) return -1;
    memcpy(out, p, n + 1);
    return 0;
}

static int is_internal_studio_state(const char *rel) {
    if (!rel) return 0;
    while (*rel == '/') rel++;
    if (strncmp(rel, "studio/state/", 13) == 0 || strcmp(rel, "studio/state") == 0) return 1;
    if (strstr(rel, ".studio-tmp") != NULL) return 1;
    return 0;
}

static const char *status_code_label(char code, int untracked) {
    if (untracked) return "Untracked";
    switch (code) {
        case 'M': return "Modified";
        case 'A': return "Added";
        case 'D': return "Deleted";
        case 'R': return "Renamed";
        case 'C': return "Copied";
        case 'U': return "Conflict";
        default:  return "Changed";
    }
}

static void parse_branch_header(const char *line, size_t len,
                                char *branch, size_t branch_sz,
                                char *upstream, size_t upstream_sz,
                                long long *ahead, long long *behind) {
    const char *p = line;
    const char *end = line + len;
    const char *dots = NULL;
    const char *bracket = NULL;
    size_t i;

    branch[0] = '\0';
    upstream[0] = '\0';
    *ahead = 0;
    *behind = 0;

    if (len >= 3 && memcmp(p, "## ", 3) == 0) {
        p += 3;
    }
    if ((size_t)(end - p) > 18 && memcmp(p, "No commits yet on ", 18) == 0) {
        size_t n = (size_t)(end - (p + 18));
        if (n >= branch_sz) n = branch_sz - 1;
        memcpy(branch, p + 18, n);
        branch[n] = '\0';
        return;
    }
    if ((size_t)(end - p) > 18 && memcmp(p, "Initial commit on ", 18) == 0) {
        size_t n = (size_t)(end - (p + 18));
        if (n >= branch_sz) n = branch_sz - 1;
        memcpy(branch, p + 18, n);
        branch[n] = '\0';
        return;
    }
    for (i = 0; p + i < end; i++) {
        if (p + i + 3 <= end && memcmp(p + i, "...", 3) == 0 && !dots) {
            dots = p + i;
        }
        if (p[i] == '[' && !bracket) {
            bracket = p + i;
            break;
        }
    }
    if (dots) {
        size_t bn = (size_t)(dots - p);
        const char *up_start = dots + 3;
        const char *up_end = bracket ? bracket : end;
        size_t un;
        while (up_end > up_start && up_end[-1] == ' ') up_end--;
        un = (size_t)(up_end - up_start);
        if (bn >= branch_sz) bn = branch_sz - 1;
        memcpy(branch, p, bn);
        branch[bn] = '\0';
        if (un >= upstream_sz) un = upstream_sz - 1;
        memcpy(upstream, up_start, un);
        upstream[un] = '\0';
    } else {
        const char *b_end = bracket ? bracket : end;
        size_t bn;
        while (b_end > p && b_end[-1] == ' ') b_end--;
        bn = (size_t)(b_end - p);
        if (bn >= branch_sz) bn = branch_sz - 1;
        memcpy(branch, p, bn);
        branch[bn] = '\0';
    }
    if (bracket && bracket < end) {
        const char *ah = strstr(bracket, "ahead ");
        const char *bh = strstr(bracket, "behind ");
        if (ah && ah < end) *ahead = strtoll(ah + 6, NULL, 10);
        if (bh && bh < end) *behind = strtoll(bh + 7, NULL, 10);
    }
}

static void extract_basename_no_git(const char *path_or_url, char *out, size_t out_sz) {
    const char *p;
    const char *end;
    const char *last_sep = NULL;
    size_t len;

    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!path_or_url || !path_or_url[0]) return;

    end = path_or_url + strlen(path_or_url);
    while (end > path_or_url && (end[-1] == '/' || end[-1] == '\\' || end[-1] == '\r' ||
                                 end[-1] == '\n' || end[-1] == ' ' || end[-1] == '\t')) {
        end--;
    }
    if (end - path_or_url > 4 && memcmp(end - 4, ".git", 4) == 0) {
        end -= 4;
    }
    for (p = path_or_url; p < end; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') {
            last_sep = p;
        }
    }
    p = last_sep ? (last_sep + 1) : path_or_url;
    len = (size_t)(end - p);
    if (len >= out_sz) len = out_sz - 1;
    if (len > 0) memcpy(out, p, len);
    out[len] = '\0';
}

static void resolve_repo_metadata(const char *repo_root, char *repo_name, size_t name_sz,
                                  char *remote_url, size_t url_sz) {
    char *argv_top[7];
    char *argv_rem[8];
    char *out_top = NULL;
    char *out_rem = NULL;
    int code = 1;

    if (repo_name && name_sz) repo_name[0] = '\0';
    if (remote_url && url_sz) remote_url[0] = '\0';
    if (!repo_root || !repo_root[0]) return;

    argv_rem[0] = "git";
    argv_rem[1] = "-C";
    argv_rem[2] = (char *)repo_root;
    argv_rem[3] = "config";
    argv_rem[4] = "--get";
    argv_rem[5] = "remote.origin.url";
    argv_rem[6] = NULL;
    if (git_exec_capture(argv_rem, 0, &out_rem, &code) == 0 && code == 0 && out_rem) {
        size_t len = strlen(out_rem);
        while (len > 0 && (out_rem[len - 1] == '\r' || out_rem[len - 1] == '\n' ||
                           out_rem[len - 1] == ' ' || out_rem[len - 1] == '\t')) {
            out_rem[--len] = '\0';
        }
        if (remote_url && url_sz && len > 0) {
            if (len >= url_sz) len = url_sz - 1;
            memcpy(remote_url, out_rem, len);
            remote_url[len] = '\0';
        }
    }
    free(out_rem);

    argv_top[0] = "git";
    argv_top[1] = "-C";
    argv_top[2] = (char *)repo_root;
    argv_top[3] = "rev-parse";
    argv_top[4] = "--show-toplevel";
    argv_top[5] = NULL;
    if (git_exec_capture(argv_top, 0, &out_top, &code) == 0 && code == 0 && out_top && out_top[0]) {
        extract_basename_no_git(out_top, repo_name, name_sz);
    }
    free(out_top);

    if (repo_name && name_sz && !repo_name[0]) {
        extract_basename_no_git(repo_root, repo_name, name_sz);
    }
    if (repo_name && name_sz && !repo_name[0] && remote_url && remote_url[0]) {
        extract_basename_no_git(remote_url, repo_name, name_sz);
    }
}

static JsonValue *parse_parent_hashes(const char *parents_str) {
    JsonValue *arr = json_array();
    const char *p = parents_str;
    if (!arr || !p) return arr;
    while (*p) {
        const char *sp;
        size_t n;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        sp = p;
        while (*sp && *sp != ' ' && *sp != '\t') sp++;
        n = (size_t)(sp - p);
        if (n > 0) {
            JsonValue *s = json_string(p, n);
            if (s) json_array_add(arr, s);
        }
        p = sp;
    }
    return arr;
}

static void populate_recent_commits(const char *repo_root, JsonValue *commits_arr) {
    char *argv[10];
    char *out = NULL;
    int code = 1;
    const char *line;

    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "log";
    argv[4] = "--topo-order";
    argv[5] = "-n";
    argv[6] = "25";
    argv[7] = "--pretty=format:%h\t%H\t%P\t%D\t%an\t%ar\t%s";
    argv[8] = NULL;
    if (git_exec_capture(argv, 0, &out, &code) != 0 || code != 0 || !out) {
        free(out);
        return;
    }
    line = out;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len > 0) {
            char row[2048];
            char *t1;
            char *t2;
            char *t3;
            char *t4;
            char *t5;
            char *t6;
            if (len >= sizeof(row)) len = sizeof(row) - 1;
            memcpy(row, line, len);
            row[len] = '\0';
            t1 = strchr(row, '\t');
            t2 = t1 ? strchr(t1 + 1, '\t') : NULL;
            t3 = t2 ? strchr(t2 + 1, '\t') : NULL;
            t4 = t3 ? strchr(t3 + 1, '\t') : NULL;
            t5 = t4 ? strchr(t4 + 1, '\t') : NULL;
            t6 = t5 ? strchr(t5 + 1, '\t') : NULL;
            if (t1 && t2 && t3 && t4 && t5 && t6) {
                JsonValue *c = json_object();
                *t1 = '\0';
                *t2 = '\0';
                *t3 = '\0';
                *t4 = '\0';
                *t5 = '\0';
                *t6 = '\0';
                if (c) {
                    JsonValue *parents = parse_parent_hashes(t2 + 1);
                    jput_s(c, "shortHash", row);
                    jput_s(c, "hash", t1 + 1);
                    jput_s(c, "parentHashes", t2 + 1);
                    if (parents) json_object_put(c, "parents", parents);
                    jput_s(c, "refs", t3 + 1);
                    jput_s(c, "author", t4 + 1);
                    jput_s(c, "relativeDate", t5 + 1);
                    jput_s(c, "subject", t6 + 1);
                    json_array_add(commits_arr, c);
                }
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    free(out);
}

int git_scm_status(const char *repo_root, JsonDoc **out, char *err, size_t err_sz) {
    char *argv[8];
    char *porcelain = NULL;
    int code = 1;
    JsonValue *root = NULL;
    JsonValue *staged_arr = NULL;
    JsonValue *changes_arr = NULL;
    JsonValue *commits_arr = NULL;
    char branch[256];
    char upstream[256];
    char repo_name[256];
    char remote_url[512];
    long long ahead = 0;
    long long behind = 0;
    long long staged_count = 0;
    long long unstaged_count = 0;
    long long total_changed = 0;
    const char *line;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }

    root = json_object();
    staged_arr = json_array();
    changes_arr = json_array();
    commits_arr = json_array();
    if (!root || !staged_arr || !changes_arr || !commits_arr) {
        json_free(root);
        json_free(staged_arr);
        json_free(changes_arr);
        json_free(commits_arr);
        set_err(err, err_sz, "out of memory");
        return -1;
    }

    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "status";
    argv[4] = "--porcelain";
    argv[5] = "-b";
    argv[6] = "-uall";
    argv[7] = NULL;
    if (git_exec_capture(argv, 0, &porcelain, &code) != 0 || code != 0 || !porcelain) {
        free(porcelain);
        jput_b(root, "ok", 1);
        jput_b(root, "isRepo", 0);
        jput_s(root, "repoName", "");
        jput_s(root, "remoteUrl", "");
        jput_s(root, "branch", "");
        jput_s(root, "upstream", "");
        jput_i(root, "ahead", 0);
        jput_i(root, "behind", 0);
        jput_i(root, "stagedCount", 0);
        jput_i(root, "unstagedCount", 0);
        jput_i(root, "totalChanged", 0);
        json_object_put(root, "staged", staged_arr);
        json_object_put(root, "changes", changes_arr);
        json_object_put(root, "commits", commits_arr);
        *out = json_doc_new(root);
        return *out ? 0 : -1;
    }

    branch[0] = '\0';
    upstream[0] = '\0';
    line = porcelain;
    while (*line) {
        const char *nl = strchr(line, '\n');
        size_t len = nl ? (size_t)(nl - line) : strlen(line);
        if (len > 0 && line[len - 1] == '\r') len--;
        if (len >= 3 && memcmp(line, "## ", 3) == 0) {
            parse_branch_header(line, len, branch, sizeof(branch), upstream, sizeof(upstream), &ahead, &behind);
        } else if (len >= 4 && line[2] == ' ') {
            char x = line[0];
            char y = line[1];
            const char *raw_path = line + 3;
            size_t raw_len = len - 3;
            const char *arrow = NULL;
            char *orig_rel = NULL;
            char *rel = NULL;
            size_t k;

            for (k = 0; k + 4 <= raw_len; k++) {
                if (memcmp(raw_path + k, " -> ", 4) == 0) {
                    arrow = raw_path + k;
                    break;
                }
            }
            if (arrow) {
                orig_rel = git_unquote_path(raw_path, (size_t)(arrow - raw_path));
                rel = git_unquote_path(arrow + 4, raw_len - (size_t)(arrow + 4 - raw_path));
            } else {
                rel = git_unquote_path(raw_path, raw_len);
            }
            if (rel && rel[0] && !is_internal_studio_state(rel)) {
                char pub[PATH_MAX];
                int file_counted = 0;
                snprintf(pub, sizeof(pub), "/%s", rel);
                if (x != ' ' && x != '?' && x != '!') {
                    JsonValue *item = json_object();
                    char sc[2] = {x, '\0'};
                    if (item) {
                        jput_s(item, "path", pub);
                        jput_s(item, "relPath", rel);
                        if (orig_rel && orig_rel[0]) jput_s(item, "origPath", orig_rel);
                        jput_s(item, "status", sc);
                        jput_s(item, "statusLabel", status_code_label(x, 0));
                        jput_b(item, "staged", 1);
                        json_array_add(staged_arr, item);
                        staged_count++;
                        file_counted = 1;
                    }
                }
                if (y != ' ' && y != '!') {
                    JsonValue *item = json_object();
                    int untracked = (x == '?' && y == '?');
                    char sc[2] = {untracked ? 'U' : y, '\0'};
                    if (item) {
                        jput_s(item, "path", pub);
                        jput_s(item, "relPath", rel);
                        jput_s(item, "status", sc);
                        jput_s(item, "statusLabel", status_code_label(y, untracked));
                        jput_b(item, "untracked", untracked);
                        jput_b(item, "staged", 0);
                        json_array_add(changes_arr, item);
                        unstaged_count++;
                        file_counted = 1;
                    }
                }
                if (file_counted) total_changed++;
            }
            free(orig_rel);
            free(rel);
        }
        if (!nl) break;
        line = nl + 1;
    }
    free(porcelain);

    resolve_repo_metadata(repo_root, repo_name, sizeof(repo_name), remote_url, sizeof(remote_url));
    populate_recent_commits(repo_root, commits_arr);

    jput_b(root, "ok", 1);
    jput_b(root, "isRepo", 1);
    jput_s(root, "repoName", repo_name);
    jput_s(root, "remoteUrl", remote_url);
    jput_s(root, "branch", branch[0] ? branch : "HEAD");
    jput_s(root, "upstream", upstream);
    jput_i(root, "ahead", ahead);
    jput_i(root, "behind", behind);
    jput_i(root, "stagedCount", staged_count);
    jput_i(root, "unstagedCount", unstaged_count);
    jput_i(root, "totalChanged", total_changed);
    json_object_put(root, "staged", staged_arr);
    json_object_put(root, "changes", changes_arr);
    json_object_put(root, "commits", commits_arr);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

static int collect_request_paths(const JsonValue *body, char ***out_paths, size_t *out_count,
                                 int *out_all, char *err, size_t err_sz) {
    JsonValue *all_v;
    JsonValue *path_v;
    JsonValue *paths_v;
    char **list = NULL;
    size_t count = 0;

    *out_paths = NULL;
    *out_count = 0;
    *out_all = 0;
    if (!body || json_type(body) != JSON_OBJECT) {
        set_err(err, err_sz, "JSON body must be an object");
        return 1;
    }
    all_v = json_object_get(body, "all");
    if (all_v && json_type(all_v) == JSON_BOOL && json_bool_value(all_v)) {
        *out_all = 1;
        return 0;
    }
    path_v = json_object_get(body, "path");
    if (path_v && json_type(path_v) == JSON_STRING) {
        const char *raw = json_string_get(path_v, NULL);
        char rel[PATH_MAX];
        if (!raw || sanitize_rel_path(raw, rel, sizeof(rel)) != 0) {
            set_err(err, err_sz, "Invalid file path");
            return 1;
        }
        list = (char **)calloc(1, sizeof(*list));
        if (!list) {
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        list[0] = strdup(rel);
        *out_paths = list;
        *out_count = 1;
        return 0;
    }
    paths_v = json_object_get(body, "paths");
    if (paths_v && json_type(paths_v) == JSON_ARRAY && json_array_len(paths_v) > 0) {
        size_t n = json_array_len(paths_v);
        size_t i;
        if (n > 512) n = 512;
        list = (char **)calloc(n, sizeof(*list));
        if (!list) {
            set_err(err, err_sz, "out of memory");
            return -1;
        }
        for (i = 0; i < n; i++) {
            JsonValue *item = json_array_at(paths_v, i);
            const char *raw = item && json_type(item) == JSON_STRING ? json_string_get(item, NULL) : NULL;
            char rel[PATH_MAX];
            if (!raw || sanitize_rel_path(raw, rel, sizeof(rel)) != 0) {
                size_t j;
                for (j = 0; j < count; j++) free(list[j]);
                free(list);
                set_err(err, err_sz, "Invalid file path in paths");
                return 1;
            }
            list[count++] = strdup(rel);
        }
        *out_paths = list;
        *out_count = count;
        return 0;
    }
    set_err(err, err_sz, "path, paths, or all:true is required");
    return 1;
}

static void free_path_list(char **paths, size_t count) {
    size_t i;
    if (!paths) return;
    for (i = 0; i < count; i++) free(paths[i]);
    free(paths);
}

int git_scm_stage(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz) {
    char **paths = NULL;
    size_t count = 0;
    int all = 0;
    int rc;
    size_t i;

    rc = collect_request_paths(body, &paths, &count, &all, err, err_sz);
    if (rc != 0) return rc;

    if (all) {
        char *argv[6];
        char *cmd_out = NULL;
        int code = 1;
        argv[0] = "git";
        argv[1] = "-C";
        argv[2] = (char *)repo_root;
        argv[3] = "add";
        argv[4] = "-A";
        argv[5] = NULL;
        if (git_exec_capture(argv, 1, &cmd_out, &code) != 0 || code != 0) {
            set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git add -A failed");
            free(cmd_out);
            return -1;
        }
        free(cmd_out);
    } else {
        for (i = 0; i < count; i++) {
            char *argv[8];
            char *cmd_out = NULL;
            int code = 1;
            argv[0] = "git";
            argv[1] = "-C";
            argv[2] = (char *)repo_root;
            argv[3] = "add";
            argv[4] = "-A";
            argv[5] = "--";
            argv[6] = paths[i];
            argv[7] = NULL;
            if (git_exec_capture(argv, 1, &cmd_out, &code) != 0 || code != 0) {
                set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git add failed");
                free(cmd_out);
                free_path_list(paths, count);
                return -1;
            }
            free(cmd_out);
        }
    }
    free_path_list(paths, count);
    return git_scm_status(repo_root, out, err, err_sz);
}

static int repo_has_head(const char *repo_root) {
    char *argv[7];
    char *out = NULL;
    int code = 1;
    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "rev-parse";
    argv[4] = "--verify";
    argv[5] = "HEAD";
    argv[6] = NULL;
    git_exec_capture(argv, 0, &out, &code);
    free(out);
    return code == 0;
}

int git_scm_unstage(const char *repo_root, const JsonValue *body, JsonDoc **out,
                    char *err, size_t err_sz) {
    char **paths = NULL;
    size_t count = 0;
    int all = 0;
    int has_head;
    int rc;
    size_t i;

    rc = collect_request_paths(body, &paths, &count, &all, err, err_sz);
    if (rc != 0) return rc;
    has_head = repo_has_head(repo_root);

    if (all) {
        char *cmd_out = NULL;
        int code = 1;
        if (has_head) {
            char *argv[7];
            argv[0] = "git";
            argv[1] = "-C";
            argv[2] = (char *)repo_root;
            argv[3] = "reset";
            argv[4] = "-q";
            argv[5] = "HEAD";
            argv[6] = NULL;
            git_exec_capture(argv, 1, &cmd_out, &code);
        } else {
            char *argv[9];
            argv[0] = "git";
            argv[1] = "-C";
            argv[2] = (char *)repo_root;
            argv[3] = "rm";
            argv[4] = "-r";
            argv[5] = "--cached";
            argv[6] = "-q";
            argv[7] = ".";
            argv[8] = NULL;
            git_exec_capture(argv, 1, &cmd_out, &code);
        }
        free(cmd_out);
    } else {
        for (i = 0; i < count; i++) {
            char *cmd_out = NULL;
            int code = 1;
            if (has_head) {
                char *argv[9];
                argv[0] = "git";
                argv[1] = "-C";
                argv[2] = (char *)repo_root;
                argv[3] = "reset";
                argv[4] = "-q";
                argv[5] = "HEAD";
                argv[6] = "--";
                argv[7] = paths[i];
                argv[8] = NULL;
                git_exec_capture(argv, 1, &cmd_out, &code);
            } else {
                char *argv[10];
                argv[0] = "git";
                argv[1] = "-C";
                argv[2] = (char *)repo_root;
                argv[3] = "rm";
                argv[4] = "-r";
                argv[5] = "--cached";
                argv[6] = "-q";
                argv[7] = "--";
                argv[8] = paths[i];
                argv[9] = NULL;
                git_exec_capture(argv, 1, &cmd_out, &code);
            }
            free(cmd_out);
        }
    }
    free_path_list(paths, count);
    return git_scm_status(repo_root, out, err, err_sz);
}

static int is_file_tracked(const char *repo_root, const char *rel_path) {
    char *argv[8];
    char *out = NULL;
    int code = 1;
    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "ls-files";
    argv[4] = "--error-unmatch";
    argv[5] = "--";
    argv[6] = (char *)rel_path;
    argv[7] = NULL;
    git_exec_capture(argv, 0, &out, &code);
    free(out);
    return code == 0;
}

int git_scm_discard(const char *repo_root, const JsonValue *body, JsonDoc **out,
                    char *err, size_t err_sz) {
    char **paths = NULL;
    size_t count = 0;
    int all = 0;
    int rc;
    size_t i;

    rc = collect_request_paths(body, &paths, &count, &all, err, err_sz);
    if (rc != 0) return rc;
    if (all) {
        free_path_list(paths, count);
        set_err(err, err_sz, "Specify path or paths to discard");
        return 1;
    }

    for (i = 0; i < count; i++) {
        if (is_file_tracked(repo_root, paths[i])) {
            char *argv[8];
            char *cmd_out = NULL;
            int code = 1;
            argv[0] = "git";
            argv[1] = "-C";
            argv[2] = (char *)repo_root;
            argv[3] = "checkout";
            argv[4] = "--";
            argv[5] = paths[i];
            argv[6] = NULL;
            if (git_exec_capture(argv, 1, &cmd_out, &code) != 0 || code != 0) {
                set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git checkout failed");
                free(cmd_out);
                free_path_list(paths, count);
                return -1;
            }
            free(cmd_out);
        } else {
            char fs_path[PATH_MAX];
            if (snprintf(fs_path, sizeof(fs_path), "%s/%s", repo_root, paths[i]) < (int)sizeof(fs_path)) {
                remove(fs_path);
            }
        }
    }
    free_path_list(paths, count);
    return git_scm_status(repo_root, out, err, err_sz);
}

int git_scm_commit(const char *repo_root, const JsonValue *body, JsonDoc **out,
                   char *err, size_t err_sz) {
    JsonValue *msg_v;
    JsonValue *stage_all_v;
    const char *raw_msg;
    char *msg_copy;
    size_t msg_len;
    int stage_all = 0;
    char *cmd_out = NULL;
    int code = 1;
    int rc;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!body || json_type(body) != JSON_OBJECT) {
        set_err(err, err_sz, "JSON body must be an object");
        return 1;
    }
    msg_v = json_object_get(body, "message");
    raw_msg = (msg_v && json_type(msg_v) == JSON_STRING) ? json_string_get(msg_v, NULL) : NULL;
    if (!raw_msg) {
        set_err(err, err_sz, "Commit message is required");
        return 1;
    }
    while (*raw_msg == ' ' || *raw_msg == '\t' || *raw_msg == '\r' || *raw_msg == '\n') raw_msg++;
    msg_copy = strdup(raw_msg);
    if (!msg_copy) {
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    msg_len = strlen(msg_copy);
    while (msg_len > 0 && (msg_copy[msg_len - 1] == ' ' || msg_copy[msg_len - 1] == '\t' ||
                           msg_copy[msg_len - 1] == '\r' || msg_copy[msg_len - 1] == '\n')) {
        msg_copy[--msg_len] = '\0';
    }
    if (msg_len == 0) {
        free(msg_copy);
        set_err(err, err_sz, "Commit message cannot be empty");
        return 1;
    }

    stage_all_v = json_object_get(body, "stageAll");
    if (stage_all_v && json_type(stage_all_v) == JSON_BOOL && json_bool_value(stage_all_v)) {
        stage_all = 1;
    }
    if (stage_all) {
        char *add_argv[6];
        char *add_out = NULL;
        int add_code = 1;
        add_argv[0] = "git";
        add_argv[1] = "-C";
        add_argv[2] = (char *)repo_root;
        add_argv[3] = "add";
        add_argv[4] = "-A";
        add_argv[5] = NULL;
        if (git_exec_capture(add_argv, 1, &add_out, &add_code) != 0 || add_code != 0) {
            set_err(err, err_sz, "%s", (add_out && add_out[0]) ? add_out : "git add -A failed");
            free(add_out);
            free(msg_copy);
            return -1;
        }
        free(add_out);
    }

    {
        char *argv[7];
        argv[0] = "git";
        argv[1] = "-C";
        argv[2] = (char *)repo_root;
        argv[3] = "commit";
        argv[4] = "-m";
        argv[5] = msg_copy;
        argv[6] = NULL;
        git_exec_capture(argv, 1, &cmd_out, &code);
        if (code != 0 && cmd_out && (strstr(cmd_out, "user.email") || strstr(cmd_out, "user.name") ||
                                     strstr(cmd_out, " Tell me who you are") || strstr(cmd_out, "ident"))) {
            char *fallback_argv[11];
            free(cmd_out);
            cmd_out = NULL;
            fallback_argv[0] = "git";
            fallback_argv[1] = "-C";
            fallback_argv[2] = (char *)repo_root;
            fallback_argv[3] = "-c";
            fallback_argv[4] = "user.name=ClassicCars Studio";
            fallback_argv[5] = "-c";
            fallback_argv[6] = "user.email=studio@classiccars.local";
            fallback_argv[7] = "commit";
            fallback_argv[8] = "-m";
            fallback_argv[9] = msg_copy;
            fallback_argv[10] = NULL;
            git_exec_capture(fallback_argv, 1, &cmd_out, &code);
        }
    }
    free(msg_copy);

    if (code != 0) {
        set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git commit failed (ensure changes are staged)");
        free(cmd_out);
        return 1;
    }

    rc = git_scm_status(repo_root, out, err, err_sz);
    if (rc == 0 && out && *out && (*out)->root) {
        jput_b((*out)->root, "committed", 1);
        jput_s((*out)->root, "commitOutput", cmd_out ? cmd_out : "");
    }
    free(cmd_out);
    return rc;
}

static char *build_untracked_diff(const char *repo_root, const char *rel_path) {
    char fs_path[PATH_MAX];
    FILE *fp;
    long sz;
    char *content;
    size_t got;
    GBuf d;
    const char *bol;
    int line_count = 0;

    if (snprintf(fs_path, sizeof(fs_path), "%s/%s", repo_root, rel_path) >= (int)sizeof(fs_path)) return NULL;
    fp = fopen(fs_path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    sz = ftell(fp);
    if (sz < 0 || sz > 512 * 1024) {
        fclose(fp);
        return strdup("Binary or large untracked file.\n");
    }
    fseek(fp, 0, SEEK_SET);
    content = (char *)malloc((size_t)sz + 1);
    if (!content) {
        fclose(fp);
        return NULL;
    }
    got = sz > 0 ? fread(content, 1, (size_t)sz, fp) : 0;
    fclose(fp);
    if (got > 0 && memchr(content, '\0', got) != NULL) {
        free(content);
        return strdup("Binary untracked file.\n");
    }
    content[got] = '\0';

    for (bol = content; *bol; ) {
        const char *nl = strchr(bol, '\n');
        line_count++;
        if (!nl) break;
        bol = nl + 1;
    }

    gbuf_init(&d);
    gbuf_puts(&d, "--- /dev/null\n+++ b/");
    gbuf_puts(&d, rel_path);
    {
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "\n@@ -0,0 +1,%d @@\n", line_count);
        gbuf_puts(&d, hdr);
    }
    bol = content;
    while (*bol) {
        const char *nl = strchr(bol, '\n');
        size_t len = nl ? (size_t)(nl - bol) : strlen(bol);
        if (len > 0 && bol[len - 1] == '\r') len--;
        gbuf_putc(&d, '+');
        gbuf_append(&d, bol, len);
        gbuf_putc(&d, '\n');
        if (!nl) break;
        bol = nl + 1;
    }
    free(content);
    return gbuf_steal(&d);
}

static int commit_hash_ok(const char *commit) {
    size_t i;
    size_t n;
    if (!commit || !commit[0]) return 0;
    n = strlen(commit);
    if (n < 4 || n > 64) return 0;
    for (i = 0; i < n; i++) {
        if (!isalnum((unsigned char)commit[i])) return 0;
    }
    return 1;
}

int git_scm_diff(const char *repo_root, const char *path, int staged, const char *commit,
                 JsonDoc **out, char *err, size_t err_sz) {
    char *diff_text = NULL;
    int code = 1;
    JsonValue *root = NULL;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }

    if (commit && commit[0]) {
        char *argv[9];
        if (!commit_hash_ok(commit)) {
            set_err(err, err_sz, "Invalid commit hash");
            return 1;
        }
        argv[0] = "git";
        argv[1] = "-C";
        argv[2] = (char *)repo_root;
        argv[3] = "show";
        argv[4] = "--stat";
        argv[5] = "--patch";
        argv[6] = "-n";
        argv[7] = (char *)commit;
        argv[8] = NULL;
        if (git_exec_capture(argv, 0, &diff_text, &code) != 0 || code != 0) {
            free(diff_text);
            set_err(err, err_sz, "Could not load commit diff");
            return 2;
        }
    } else {
        char rel[PATH_MAX];
        if (!path || sanitize_rel_path(path, rel, sizeof(rel)) != 0) {
            set_err(err, err_sz, "Valid file path is required");
            return 1;
        }
        if (staged) {
            char *argv[8];
            argv[0] = "git";
            argv[1] = "-C";
            argv[2] = (char *)repo_root;
            argv[3] = "diff";
            argv[4] = "--cached";
            argv[5] = "--";
            argv[6] = rel;
            argv[7] = NULL;
            git_exec_capture(argv, 0, &diff_text, &code);
        } else {
            char *argv[7];
            argv[0] = "git";
            argv[1] = "-C";
            argv[2] = (char *)repo_root;
            argv[3] = "diff";
            argv[4] = "--";
            argv[5] = rel;
            argv[6] = NULL;
            git_exec_capture(argv, 0, &diff_text, &code);
            if ((!diff_text || !diff_text[0]) && !is_file_tracked(repo_root, rel)) {
                free(diff_text);
                diff_text = build_untracked_diff(repo_root, rel);
            }
        }
    }

    root = json_object();
    if (!root) {
        free(diff_text);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    jput_b(root, "ok", 1);
    jput_s(root, "path", path ? path : "");
    jput_b(root, "staged", staged ? 1 : 0);
    jput_s(root, "commit", commit ? commit : "");
    jput_s(root, "diff", diff_text ? diff_text : "");
    free(diff_text);

    *out = json_doc_new(root);
    if (!*out) {
        json_free(root);
        set_err(err, err_sz, "out of memory");
        return -1;
    }
    return 0;
}

static int repo_has_remote(const char *repo_root) {
    char *argv[5];
    char *out = NULL;
    int code = 1;
    int has = 0;
    const char *p;
    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "remote";
    argv[4] = NULL;
    if (git_exec_capture(argv, 0, &out, &code) == 0 && code == 0 && out) {
        for (p = out; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                has = 1;
                break;
            }
        }
    }
    free(out);
    return has;
}

static void trim_trailing_ws(char *s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

int git_scm_fetch(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz) {
    char *argv[7];
    char *cmd_out = NULL;
    int code = 1;
    int background = 0;
    int rc;

    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }
    if (body && json_type(body) == JSON_OBJECT) {
        JsonValue *bg = json_object_get(body, "background");
        if (bg && json_type(bg) == JSON_BOOL && json_bool_value(bg)) {
            background = 1;
        }
    }
    if (!repo_has_remote(repo_root)) {
        rc = git_scm_status(repo_root, out, err, err_sz);
        if (rc == 0 && out && *out && (*out)->root) {
            jput_b((*out)->root, "fetched", 0);
            jput_b((*out)->root, "noRemote", 1);
            jput_s((*out)->root, "fetchOutput", "");
        }
        return rc;
    }

    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "fetch";
    argv[4] = "--prune";
    argv[5] = "--quiet";
    argv[6] = NULL;
    git_exec_capture_timeout(argv, 1, 30000, &cmd_out, &code);
    trim_trailing_ws(cmd_out);

    if (code != 0 && !background) {
        set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git fetch failed");
        free(cmd_out);
        return 1;
    }

    rc = git_scm_status(repo_root, out, err, err_sz);
    if (rc == 0 && out && *out && (*out)->root) {
        jput_b((*out)->root, "fetched", code == 0 ? 1 : 0);
        jput_s((*out)->root, "fetchOutput", cmd_out ? cmd_out : "");
        if (code != 0 && cmd_out && cmd_out[0]) {
            jput_s((*out)->root, "fetchError", cmd_out);
        }
    }
    free(cmd_out);
    return rc;
}

int git_scm_pull(const char *repo_root, const JsonValue *body, JsonDoc **out,
                 char *err, size_t err_sz) {
    char *argv[6];
    char *cmd_out = NULL;
    int code = 1;
    int rc;

    (void)body;
    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }
    if (!repo_has_remote(repo_root)) {
        set_err(err, err_sz, "No Git remote is configured for this repository");
        return 1;
    }

    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "pull";
    argv[4] = "--no-edit";
    argv[5] = NULL;
    git_exec_capture_timeout(argv, 1, 60000, &cmd_out, &code);
    trim_trailing_ws(cmd_out);

    if (code != 0) {
        set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git pull failed");
        free(cmd_out);
        return 1;
    }

    rc = git_scm_status(repo_root, out, err, err_sz);
    if (rc == 0 && out && *out && (*out)->root) {
        jput_b((*out)->root, "pulled", 1);
        jput_s((*out)->root, "pullOutput", cmd_out ? cmd_out : "");
    }
    free(cmd_out);
    return rc;
}

int git_scm_push(const char *repo_root, const JsonValue *body, JsonDoc **out,
                 char *err, size_t err_sz) {
    char *argv[5];
    char *cmd_out = NULL;
    int code = 1;
    int rc;

    (void)body;
    if (out) *out = NULL;
    if (err && err_sz) err[0] = '\0';
    if (!repo_root || !repo_root[0]) {
        set_err(err, err_sz, "Repository root is not configured");
        return 1;
    }
    if (!repo_has_remote(repo_root)) {
        set_err(err, err_sz, "No Git remote is configured for this repository");
        return 1;
    }

    argv[0] = "git";
    argv[1] = "-C";
    argv[2] = (char *)repo_root;
    argv[3] = "push";
    argv[4] = NULL;
    git_exec_capture_timeout(argv, 1, 180000, &cmd_out, &code);
    trim_trailing_ws(cmd_out);

    if (code != 0) {
        set_err(err, err_sz, "%s", (cmd_out && cmd_out[0]) ? cmd_out : "git push failed");
        free(cmd_out);
        return 1;
    }

    rc = git_scm_status(repo_root, out, err, err_sz);
    if (rc == 0 && out && *out && (*out)->root) {
        jput_b((*out)->root, "pushed", 1);
        jput_s((*out)->root, "pushOutput", cmd_out ? cmd_out : "");
    }
    free(cmd_out);
    return rc;
}
