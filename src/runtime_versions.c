#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "runtime_versions.h"

#include <errno.h>
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
#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

/* Version sources, read from the library itself:
 *   PHP       php -r "echo PHP_VERSION;"
 *   GLib      glib_major/minor/micro_version in libglib-2.0.so.0
 *   GTK       gtk_get_major/minor/micro_version in libgtk-3.so.0
 *   WebKitGTK webkit_get_major/minor/micro_version in the soname window.c loads
 * GObject and libX11 are loaded too, but neither exports its own release
 * version (GObject resolves GLib's symbols; XkbLibraryVersion is the XKB
 * protocol version). They are not given a copied or substituted number.
 */

static int append_library(JsonValue *list, const char *name, int ok, const char *version, const char *error) {
    JsonValue *item = json_object();
    const char *failure = error && error[0] ? error : "Version was not reported";
    if (!item) return -1;
    if (json_object_put(item, "name", json_string_cstr(name)) != 0) {
        json_free(item);
        return -1;
    }
    if (ok) {
        if (!version || !version[0] || json_object_put(item, "version", json_string_cstr(version)) != 0) {
            json_free(item);
            return -1;
        }
    } else if (json_object_put(item, "error", json_string_cstr(failure)) != 0) {
        json_free(item);
        return -1;
    }
    if (json_array_add(list, item) != 0) return -1;
    return 0;
}

static int accept_version_token(char *text, char *dst, size_t dst_sz, char *err, size_t err_sz) {
    size_t n;
    size_t i;
    if (!text) {
        snprintf(err, err_sz, "php reported an empty PHP_VERSION");
        return -1;
    }
    n = strlen(text);
    while (n > 0 && (text[n - 1] == '\n' || text[n - 1] == '\r')) {
        text[--n] = '\0';
    }
    if (n == 0) {
        snprintf(err, err_sz, "php reported an empty PHP_VERSION");
        return -1;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c <= ' ' || c == '"' || c == '\'') {
            snprintf(err, err_sz, "php PHP_VERSION was not a single token");
            return -1;
        }
    }
    if (n + 1 > dst_sz) {
        snprintf(err, err_sz, "php PHP_VERSION did not fit");
        return -1;
    }
    memcpy(dst, text, n + 1);
    return 0;
}

#ifdef _WIN32

static int read_php_version(char *dst, size_t dst_sz, char *err, size_t err_sz) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE read_h = NULL;
    HANDLE write_h = NULL;
    HANDLE nul = NULL;
    char cmd[] = "php -r \"echo PHP_VERSION;\"";
    char buf[128];
    DWORD code = 1;
    size_t len = 0;
    memset(&sa, 0, sizeof sa);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    memset(buf, 0, sizeof buf);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&read_h, &write_h, &sa, 0)) {
        snprintf(err, err_sz, "CreatePipe failed (%lu)", GetLastError());
        return -1;
    }
    SetHandleInformation(read_h, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    if (nul == INVALID_HANDLE_VALUE) {
        snprintf(err, err_sz, "could not open NUL (%lu)", GetLastError());
        CloseHandle(read_h);
        CloseHandle(write_h);
        return -1;
    }
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul;
    si.hStdOutput = write_h;
    si.hStdError = nul;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        snprintf(err, err_sz, "php PHP_VERSION failed to start (%lu)", GetLastError());
        CloseHandle(read_h);
        CloseHandle(write_h);
        if (nul) CloseHandle(nul);
        return -1;
    }
    CloseHandle(write_h);
    write_h = NULL;
    for (;;) {
        DWORD got = 0;
        if (len + 1 >= sizeof buf) {
            snprintf(err, err_sz, "php PHP_VERSION output was not a single token");
            TerminateProcess(pi.hProcess, 1);
            CloseHandle(read_h);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (nul) CloseHandle(nul);
            return -1;
        }
        if (!ReadFile(read_h, buf + len, (DWORD)(sizeof buf - 1 - len), &got, NULL) || got == 0) break;
        len += (size_t)got;
    }
    CloseHandle(read_h);
    if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        if (nul) CloseHandle(nul);
        snprintf(err, err_sz, "php PHP_VERSION timed out");
        return -1;
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (nul) CloseHandle(nul);
    buf[len] = '\0';
    if (code != 0) {
        snprintf(err, err_sz, "php -r echo PHP_VERSION exited %lu", code);
        return -1;
    }
    return accept_version_token(buf, dst, dst_sz, err, err_sz);
}

#else

static long long mono_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + (long long)(ts.tv_nsec / 1000000L);
}

static int format_triplet(char *dst, size_t dst_sz, unsigned int major, unsigned int minor, unsigned int micro,
                          char *err, size_t err_sz) {
    int wrote = snprintf(dst, dst_sz, "%u.%u.%u", major, minor, micro);
    if (wrote < 0 || (size_t)wrote >= dst_sz) {
        snprintf(err, err_sz, "version did not fit");
        return -1;
    }
    return 0;
}

typedef unsigned int (*StudioUintFn)(void);

static int triplet_from_functions(void *lib, const char *major_name, const char *minor_name, const char *micro_name,
                                  char *dst, size_t dst_sz, char *err, size_t err_sz) {
    StudioUintFn major;
    StudioUintFn minor;
    StudioUintFn micro;
    dlerror();
    major = (StudioUintFn)dlsym(lib, major_name);
    minor = (StudioUintFn)dlsym(lib, minor_name);
    micro = (StudioUintFn)dlsym(lib, micro_name);
    if (!major || !minor || !micro) {
        snprintf(err, err_sz, "missing %s", !major ? major_name : !minor ? minor_name : micro_name);
        return -1;
    }
    return format_triplet(dst, dst_sz, major(), minor(), micro(), err, err_sz);
}

static int triplet_from_data(void *lib, const char *major_name, const char *minor_name, const char *micro_name,
                             char *dst, size_t dst_sz, char *err, size_t err_sz) {
    const unsigned int *major;
    const unsigned int *minor;
    const unsigned int *micro;
    dlerror();
    major = (const unsigned int *)dlsym(lib, major_name);
    minor = (const unsigned int *)dlsym(lib, minor_name);
    micro = (const unsigned int *)dlsym(lib, micro_name);
    if (!major || !minor || !micro) {
        snprintf(err, err_sz, "missing %s", !major ? major_name : !minor ? minor_name : micro_name);
        return -1;
    }
    return format_triplet(dst, dst_sz, *major, *minor, *micro, err, err_sz);
}

static int read_so(const char *soname, int data, const char *major_name, const char *minor_name, const char *micro_name,
                   char *dst, size_t dst_sz, char *err, size_t err_sz) {
    void *lib;
    int rc;
    dlerror();
    lib = dlopen(soname, RTLD_LAZY | RTLD_LOCAL);
    if (!lib) {
        const char *why = dlerror();
        snprintf(err, err_sz, "%s: %s", soname, why ? why : "dlopen failed");
        return -1;
    }
    rc = data ? triplet_from_data(lib, major_name, minor_name, micro_name, dst, dst_sz, err, err_sz)
              : triplet_from_functions(lib, major_name, minor_name, micro_name, dst, dst_sz, err, err_sz);
    dlclose(lib);
    return rc;
}

static int read_webkit(char *dst, size_t dst_sz, char *err, size_t err_sz) {
    /* Same soname order as studio_window_run. A loaded library with missing
     * version symbols is reported as that failure; the other soname is only
     * tried when dlopen itself fails. */
    static const char *names[] = {"libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37"};
    size_t i;
    err[0] = '\0';
    for (i = 0; i < 2; i++) {
        void *lib;
        int rc;
        const char *why;
        size_t used;
        dlerror();
        lib = dlopen(names[i], RTLD_LAZY | RTLD_LOCAL);
        if (!lib) {
            why = dlerror();
            used = strlen(err);
            if (used + 1 < err_sz) {
                snprintf(err + used, err_sz - used, "%s%s: %s", used ? "; " : "", names[i], why ? why : "dlopen failed");
            }
            continue;
        }
        rc = triplet_from_functions(lib, "webkit_get_major_version", "webkit_get_minor_version",
                                    "webkit_get_micro_version", dst, dst_sz, err, err_sz);
        dlclose(lib);
        return rc;
    }
    if (!err[0]) snprintf(err, err_sz, "WebKitGTK did not load");
    return -1;
}

static int read_php_version(char *dst, size_t dst_sz, char *err, size_t err_sz) {
    int fds[2];
    pid_t pid;
    char buf[128];
    size_t len = 0;
    int status = 0;
    int timed_out = 0;
    long long deadline;
    if (pipe(fds) != 0) {
        snprintf(err, err_sz, "pipe failed: %s", strerror(errno));
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        snprintf(err, err_sz, "fork failed: %s", strerror(saved));
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDWR);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (devnull >= 0) {
            dup2(devnull, STDIN_FILENO);
            dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        close(fds[0]);
        close(fds[1]);
        execlp("php", "php", "-r", "echo PHP_VERSION;", (char *)NULL);
        _exit(127);
    }
    close(fds[1]);
    deadline = mono_ms() + 5000LL;
    for (;;) {
        struct pollfd pfd;
        long long rem;
        ssize_t got;
        int prc;
        rem = deadline - mono_ms();
        if (rem <= 0) {
            timed_out = 1;
            break;
        }
        if (len + 1 >= sizeof buf) {
            close(fds[0]);
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
            snprintf(err, err_sz, "php PHP_VERSION output was not a single token");
            return -1;
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
        got = read(fds[0], buf + len, sizeof buf - 1 - len);
        if (got < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (got == 0) break;
        len += (size_t)got;
    }
    close(fds[0]);
    if (timed_out) kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            snprintf(err, err_sz, "waitpid failed: %s", strerror(errno));
            return -1;
        }
    }
    if (timed_out) {
        snprintf(err, err_sz, "php PHP_VERSION timed out");
        return -1;
    }
    buf[len] = '\0';
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        int code = WIFEXITED(status) ? (int)WEXITSTATUS(status) : -1;
        snprintf(err, err_sz, "php -r echo PHP_VERSION exited %d", code);
        return -1;
    }
    return accept_version_token(buf, dst, dst_sz, err, err_sz);
}

#endif

JsonValue *studio_runtime_versions(void) {
    JsonValue *list = json_array();
    char version[64];
    char error[384];
    int ok;
    if (!list) return NULL;
    version[0] = '\0';
    error[0] = '\0';
    ok = read_php_version(version, sizeof version, error, sizeof error);
    if (append_library(list, "PHP", ok == 0, version, error) != 0) {
        json_free(list);
        return NULL;
    }
#ifndef _WIN32
    version[0] = '\0';
    error[0] = '\0';
    ok = read_so("libglib-2.0.so.0", 1, "glib_major_version", "glib_minor_version", "glib_micro_version",
                 version, sizeof version, error, sizeof error);
    if (append_library(list, "GLib", ok == 0, version, error) != 0) {
        json_free(list);
        return NULL;
    }
    version[0] = '\0';
    error[0] = '\0';
    ok = read_so("libgtk-3.so.0", 0, "gtk_get_major_version", "gtk_get_minor_version", "gtk_get_micro_version",
                 version, sizeof version, error, sizeof error);
    if (append_library(list, "GTK", ok == 0, version, error) != 0) {
        json_free(list);
        return NULL;
    }
    version[0] = '\0';
    error[0] = '\0';
    ok = read_webkit(version, sizeof version, error, sizeof error);
    if (append_library(list, "WebKitGTK", ok == 0, version, error) != 0) {
        json_free(list);
        return NULL;
    }
#endif
    return list;
}
