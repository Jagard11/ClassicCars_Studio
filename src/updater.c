#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "updater.h"

#include "app_config.h"
#include "compat.h"
#include "http.h"
#include "settings.h"
#include "version.h"
#include "window.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct CachedRelease {
    int checked;
    char version[64];
    char tag_name[64];
    char name[256];
    char notes[16384];
    char html_url[512];
    char published_at[64];
    char asset_name[256];
    char asset_url[1024];
    long long asset_size;
    char sha256_url[1024];
    char sha256_hex[128];
} CachedRelease;

static CachedRelease g_cached;
static volatile int g_restart_requested = 0;

void updater_request_restart(void) {
    g_restart_requested = 1;
}

int updater_restart_requested(void) {
    return g_restart_requested;
}

#ifdef _WIN32
static CRITICAL_SECTION g_upd_lock;
static INIT_ONCE g_upd_once = INIT_ONCE_STATIC_INIT;
static BOOL CALLBACK upd_lock_init(PINIT_ONCE once, PVOID p, PVOID *ctx) {
    (void)once; (void)p; (void)ctx;
    InitializeCriticalSection(&g_upd_lock);
    return TRUE;
}
static void upd_lock(void) {
    InitOnceExecuteOnce(&g_upd_once, upd_lock_init, NULL, NULL);
    EnterCriticalSection(&g_upd_lock);
}
static void upd_unlock(void) { LeaveCriticalSection(&g_upd_lock); }
#else
static pthread_mutex_t g_upd_lock = PTHREAD_MUTEX_INITIALIZER;
static void upd_lock(void) { pthread_mutex_lock(&g_upd_lock); }
static void upd_unlock(void) { pthread_mutex_unlock(&g_upd_lock); }
#endif

void updater_reset_cache(void) {
    upd_lock();
    memset(&g_cached, 0, sizeof(g_cached));
    upd_unlock();
}

static void set_err(char *err, size_t err_sz, const char *msg) {
    if (err && err_sz > 0) snprintf(err, err_sz, "%s", msg ? msg : "");
}

static const char *effective_home(const char *studio_home) {
    const char *h = settings_get_studio_home();
    if (studio_home && studio_home[0]) return studio_home;
    if (h && h[0]) return h;
    return ".";
}

static void parse_semver(const char *v, int out[3]) {
    int i = 0;
    out[0] = out[1] = out[2] = 0;
    if (!v) return;
    while (*v && (*v == ' ' || *v == '\t' || *v == 'v' || *v == 'V')) v++;
    while (*v && i < 3) {
        if (isdigit((unsigned char)*v)) {
            long val = strtol(v, (char **)&v, 10);
            out[i++] = (int)(val < 0 ? 0 : val);
            if (*v == '.') v++;
            else break;
        } else {
            break;
        }
    }
}

int updater_compare_versions(const char *v1, const char *v2) {
    int a[3], b[3], i;
    parse_semver(v1, a);
    parse_semver(v2, b);
    for (i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

/* Compact portable SHA-256 implementation */
static uint32_t rotr32(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

static void sha256_block(uint32_t st[8], const unsigned char blk[64]) {
    static const uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    int i;
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blk[i * 4] << 24) | ((uint32_t)blk[i * 4 + 1] << 16) |
               ((uint32_t)blk[i * 4 + 2] << 8) | (uint32_t)blk[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = st[0]; b = st[1]; c = st[2]; d = st[3];
    e = st[4]; f = st[5]; g = st[6]; h = st[7];
    for (i = 0; i < 64; i++) {
        uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        t1 = h + S1 + ch + k[i] + w[i];
        t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d;
    st[4] += e; st[5] += f; st[6] += g; st[7] += h;
}

int updater_sha256_file(const char *path, char hex_out[65]) {
    uint32_t st[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    unsigned char buf[4096], blk[64];
    uint64_t total = 0;
    size_t rem = 0, n;
    int i;
    FILE *fp;
    if (!path || !hex_out) return -1;
    hex_out[0] = '\0';
    fp = fopen(path, "rb");
    if (!fp) return -1;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        size_t off = 0;
        total += n;
        while (off < n) {
            size_t take = 64 - rem;
            if (take > n - off) take = n - off;
            memcpy(blk + rem, buf + off, take);
            rem += take;
            off += take;
            if (rem == 64) {
                sha256_block(st, blk);
                rem = 0;
            }
        }
    }
    if (ferror(fp)) { fclose(fp); return -1; }
    fclose(fp);
    blk[rem++] = 0x80;
    if (rem > 56) {
        memset(blk + rem, 0, 64 - rem);
        sha256_block(st, blk);
        rem = 0;
    }
    memset(blk + rem, 0, 56 - rem);
    {
        uint64_t bits = total * 8;
        for (i = 0; i < 8; i++) blk[56 + i] = (unsigned char)((bits >> ((7 - i) * 8)) & 0xffu);
    }
    sha256_block(st, blk);
    for (i = 0; i < 8; i++) snprintf(hex_out + i * 8, 9, "%08x", st[i]);
    hex_out[64] = '\0';
    return 0;
}

static int fetch_url_to_file(const char *url, const char *dest_path, char *err, size_t err_sz) {
    if (!url || !url[0] || !dest_path || !dest_path[0]) {
        set_err(err, err_sz, "Invalid download URL or destination");
        return -1;
    }
    if (strncmp(url, "file://", 7) == 0 || (url[0] == '/' || (isalpha((unsigned char)url[0]) && url[1] == ':'))) {
        const char *src_path = strncmp(url, "file://", 7) == 0 ? url + 7 : url;
        char *data = NULL;
        size_t len = 0;
        FILE *fp;
        if (http_read_file(src_path, &data, &len, 256u * 1024u * 1024u) != 0 || !data) {
            set_err(err, err_sz, "Could not read local release file");
            return -1;
        }
        fp = fopen(dest_path, "wb");
        if (!fp || (len > 0 && fwrite(data, 1, len, fp) != len)) {
            if (fp) fclose(fp);
            free(data);
            set_err(err, err_sz, "Could not write downloaded file");
            return -1;
        }
        fclose(fp);
        free(data);
        return 0;
    }
#ifndef _WIN32
    {
        pid_t pid = fork();
        int status = 0;
        if (pid == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2) close(devnull);
            }
            execlp("curl", "curl", "-fsSL", "--max-time", "60",
                   "-H", "User-Agent: ClassicCars-Studio-Updater",
                   "-H", "Accept: application/vnd.github+json, application/octet-stream, */*",
                   "-o", dest_path, url, (char *)NULL);
            _exit(127);
        }
        if (pid > 0 && waitpid(pid, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        }
        pid = fork();
        if (pid == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2) close(devnull);
            }
            execlp("python3", "python3", "-c",
                   "import sys, urllib.request\n"
                   "req = urllib.request.Request(sys.argv[1], headers={'User-Agent': 'ClassicCars-Studio-Updater'})\n"
                   "with urllib.request.urlopen(req, timeout=60) as r, open(sys.argv[2], 'wb') as f:\n"
                   "    f.write(r.read())\n",
                   url, dest_path, (char *)NULL);
            _exit(127);
        }
        if (pid > 0 && waitpid(pid, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        }
    }
#else
    {
        char cmd[2048];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD code = 1;
        snprintf(cmd, sizeof(cmd),
                 "curl.exe -fsSL --max-time 60 -H \"User-Agent: ClassicCars-Studio-Updater\" -o \"%s\" \"%s\"",
                 dest_path, url);
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 65000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code == 0) return 0;
        }
        code = 1;
        snprintf(cmd, sizeof(cmd),
                 "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                 "\"[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12; "
                 "Invoke-WebRequest -UseBasicParsing -Headers @{'User-Agent'='ClassicCars-Studio-Updater'} "
                 "-Uri '%s' -OutFile '%s'\"",
                 url, dest_path);
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 65000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code == 0) return 0;
        }
    }
#endif
    set_err(err, err_sz, "Failed to download from GitHub Releases");
    return -1;
}

static int upd_is_allowed_root_entry(const char *name) {
    static const char *allowed[] = {
        "classiccars-studio",
        "classiccars-studio.exe",
        "VERSION",
        "README.md",
        "launcher.py",
        "launcher.sh",
        "launcher.bat",
        "launch-studio.sh",
        "web",
        "preview",
        "assets",
        "installers",
    };
    size_t i;
    if (!name || !name[0]) return 0;
    for (i = 0; i < sizeof(allowed) / sizeof(allowed[0]); i++) {
        if (strcmp(name, allowed[i]) == 0) return 1;
    }
    return 0;
}

#ifndef _WIN32
static int upd_is_exec_name(const char *name) {
    size_t len;
    if (!name || !name[0]) return 0;
    if (strcmp(name, "classiccars-studio") == 0 ||
        strcmp(name, "classiccars-studio.exe") == 0 ||
        strcmp(name, "launcher.py") == 0 ||
        strcmp(name, "launcher.sh") == 0 ||
        strcmp(name, "launch-studio.sh") == 0) {
        return 1;
    }
    len = strlen(name);
    if (len > 3 && (strcmp(name + len - 3, ".sh") == 0 || strcmp(name + len - 3, ".py") == 0)) {
        return 1;
    }
    return 0;
}
#endif

static int upd_mkdir_p(const char *dir) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;
    if (!dir || !dir[0]) return -1;
    len = strlen(dir);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, dir, len + 1);
    for (p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char ch = *p;
            *p = '\0';
#ifdef _WIN32
            if (!(isalpha((unsigned char)tmp[0]) && tmp[1] == ':' && tmp[2] == '\0')) _mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = ch;
        }
    }
#ifdef _WIN32
    _mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
    return 0;
}

static int upd_rm_rf(const char *path, int depth) {
    struct stat st;
    if (!path || !path[0] || depth > 24) return -1;
#ifndef _WIN32
    if (lstat(path, &st) != 0) return 0;
#else
    if (stat(path, &st) != 0) return 0;
#endif
    if (S_ISDIR(st.st_mode)) {
#ifdef _WIN32
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        if (snprintf(pattern, sizeof(pattern), "%s/*", path) >= (int)sizeof(pattern)) return -1;
        h = FindFirstFileA(pattern, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                char child[PATH_MAX];
                if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
                if (snprintf(child, sizeof(child), "%s/%s", path, fd.cFileName) < (int)sizeof(child)) {
                    upd_rm_rf(child, depth + 1);
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
        return _rmdir(path);
#else
        DIR *d = opendir(path);
        struct dirent *ent;
        if (!d) return -1;
        while ((ent = readdir(d)) != NULL) {
            char child[PATH_MAX];
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (snprintf(child, sizeof(child), "%s/%s", path, ent->d_name) < (int)sizeof(child)) {
                upd_rm_rf(child, depth + 1);
            }
        }
        closedir(d);
        return rmdir(path);
#endif
    }
    return remove(path);
}

static int upd_copy_file_atomic(const char *src, const char *dst, const char *leaf_name) {
    char tmp[PATH_MAX];
    unsigned char buf[16384];
    size_t n;
    FILE *in, *out;
    if (snprintf(tmp, sizeof(tmp), "%s.upd-tmp", dst) >= (int)sizeof(tmp)) return -1;
    in = fopen(src, "rb");
    if (!in) return -1;
    out = fopen(tmp, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            fclose(out);
            remove(tmp);
            return -1;
        }
    }
    if (ferror(in) || fflush(out) != 0) {
        fclose(in);
        fclose(out);
        remove(tmp);
        return -1;
    }
    fclose(in);
    fclose(out);
#ifndef _WIN32
    if (upd_is_exec_name(leaf_name)) chmod(tmp, 0755);
    else chmod(tmp, 0644);
#else
    (void)leaf_name;
#endif
    if (strcmp(leaf_name, "classiccars-studio.exe") == 0) {
        struct stat st;
        if (stat(dst, &st) == 0) {
            char old_exe[PATH_MAX];
            if (snprintf(old_exe, sizeof(old_exe), "%s.old", dst) < (int)sizeof(old_exe)) {
                remove(old_exe);
#ifdef _WIN32
                MoveFileExA(dst, old_exe, MOVEFILE_REPLACE_EXISTING);
#else
                rename(dst, old_exe);
#endif
            }
        }
    }
#ifdef _WIN32
    if (!MoveFileExA(tmp, dst, MOVEFILE_REPLACE_EXISTING)) {
        remove(tmp);
        return -1;
    }
#else
    if (rename(tmp, dst) != 0) {
        remove(tmp);
        return -1;
    }
#endif
    return 0;
}

static int upd_copy_tree_recursive(const char *src_dir, const char *dst_dir, int depth) {
    if (depth > 20) return -1;
    if (upd_mkdir_p(dst_dir) != 0) return -1;
#ifdef _WIN32
    {
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        if (snprintf(pattern, sizeof(pattern), "%s/*", src_dir) >= (int)sizeof(pattern)) return -1;
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            char s_child[PATH_MAX], d_child[PATH_MAX];
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (snprintf(s_child, sizeof(s_child), "%s/%s", src_dir, fd.cFileName) >= (int)sizeof(s_child) ||
                snprintf(d_child, sizeof(d_child), "%s/%s", dst_dir, fd.cFileName) >= (int)sizeof(d_child)) {
                FindClose(h);
                return -1;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (upd_copy_tree_recursive(s_child, d_child, depth + 1) != 0) {
                    FindClose(h);
                    return -1;
                }
            } else {
                if (upd_copy_file_atomic(s_child, d_child, fd.cFileName) != 0) {
                    FindClose(h);
                    return -1;
                }
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
        return 0;
    }
#else
    {
        DIR *d = opendir(src_dir);
        struct dirent *ent;
        if (!d) return -1;
        while ((ent = readdir(d)) != NULL) {
            char s_child[PATH_MAX], d_child[PATH_MAX];
            struct stat st;
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (snprintf(s_child, sizeof(s_child), "%s/%s", src_dir, ent->d_name) >= (int)sizeof(s_child) ||
                snprintf(d_child, sizeof(d_child), "%s/%s", dst_dir, ent->d_name) >= (int)sizeof(d_child)) {
                closedir(d);
                return -1;
            }
            if (lstat(s_child, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (upd_copy_tree_recursive(s_child, d_child, depth + 1) != 0) {
                    closedir(d);
                    return -1;
                }
            } else if (S_ISREG(st.st_mode)) {
                if (upd_copy_file_atomic(s_child, d_child, ent->d_name) != 0) {
                    closedir(d);
                    return -1;
                }
            }
        }
        closedir(d);
        return 0;
    }
#endif
}

static int upd_sync_whitelisted_entries(const char *src_root, const char *dst_root) {
    int copied_any = 0;
    if (upd_mkdir_p(dst_root) != 0) return -1;
#ifdef _WIN32
    {
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        if (snprintf(pattern, sizeof(pattern), "%s/*", src_root) >= (int)sizeof(pattern)) return -1;
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return -1;
        do {
            char s_path[PATH_MAX], d_path[PATH_MAX];
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (strcmp(fd.cFileName, "state") == 0 || !upd_is_allowed_root_entry(fd.cFileName)) continue;
            if (snprintf(s_path, sizeof(s_path), "%s/%s", src_root, fd.cFileName) >= (int)sizeof(s_path) ||
                snprintf(d_path, sizeof(d_path), "%s/%s", dst_root, fd.cFileName) >= (int)sizeof(d_path)) {
                FindClose(h);
                return -1;
            }
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (upd_copy_tree_recursive(s_path, d_path, 0) != 0) {
                    FindClose(h);
                    return -1;
                }
            } else {
                if (upd_copy_file_atomic(s_path, d_path, fd.cFileName) != 0) {
                    FindClose(h);
                    return -1;
                }
            }
            copied_any = 1;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        DIR *d = opendir(src_root);
        struct dirent *ent;
        if (!d) return -1;
        while ((ent = readdir(d)) != NULL) {
            char s_path[PATH_MAX], d_path[PATH_MAX];
            struct stat st;
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, "state") == 0 || !upd_is_allowed_root_entry(ent->d_name)) continue;
            if (snprintf(s_path, sizeof(s_path), "%s/%s", src_root, ent->d_name) >= (int)sizeof(s_path) ||
                snprintf(d_path, sizeof(d_path), "%s/%s", dst_root, ent->d_name) >= (int)sizeof(d_path)) {
                closedir(d);
                return -1;
            }
            if (lstat(s_path, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (upd_copy_tree_recursive(s_path, d_path, 0) != 0) {
                    closedir(d);
                    return -1;
                }
            } else if (S_ISREG(st.st_mode)) {
                if (upd_copy_file_atomic(s_path, d_path, ent->d_name) != 0) {
                    closedir(d);
                    return -1;
                }
            }
            copied_any = 1;
        }
        closedir(d);
    }
#endif
    return copied_any ? 0 : -1;
}

static void upd_resolve_payload_root(const char *extract_dir, char *payload_out, size_t payload_sz) {
    char single_dir[PATH_MAX];
    int dir_count = 0, allowed_at_root = 0;
    snprintf(payload_out, payload_sz, "%s", extract_dir);
    single_dir[0] = '\0';
#ifdef _WIN32
    {
        char pattern[PATH_MAX];
        WIN32_FIND_DATAA fd;
        HANDLE h;
        if (snprintf(pattern, sizeof(pattern), "%s/*", extract_dir) >= (int)sizeof(pattern)) return;
        h = FindFirstFileA(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            if (upd_is_allowed_root_entry(fd.cFileName)) allowed_at_root = 1;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                dir_count++;
                snprintf(single_dir, sizeof(single_dir), "%s/%s", extract_dir, fd.cFileName);
            } else {
                dir_count += 2;
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    {
        DIR *d = opendir(extract_dir);
        struct dirent *ent;
        if (!d) return;
        while ((ent = readdir(d)) != NULL) {
            char child[PATH_MAX];
            struct stat st;
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (upd_is_allowed_root_entry(ent->d_name)) allowed_at_root = 1;
            if (snprintf(child, sizeof(child), "%s/%s", extract_dir, ent->d_name) < (int)sizeof(child) &&
                lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
                dir_count++;
                snprintf(single_dir, sizeof(single_dir), "%s", child);
            } else {
                dir_count += 2;
            }
        }
        closedir(d);
    }
#endif
    if (!allowed_at_root && dir_count == 1 && single_dir[0]) {
        snprintf(payload_out, payload_sz, "%s", single_dir);
    }
}

static int stage_archive_contents(const char *archive_path, const char *asset_name,
                                  const char *staged_dir, char *err, size_t err_sz) {
    char extract_tmp[PATH_MAX], payload_root[PATH_MAX], low[256];
    size_t i, n;
    int extracted = 0;

    if (!archive_path || !asset_name || !staged_dir) {
        set_err(err, err_sz, "Invalid archive staging parameters");
        return -1;
    }
    for (i = 0; asset_name[i] && i + 1 < sizeof(low); i++) {
        low[i] = (char)tolower((unsigned char)asset_name[i]);
    }
    low[i] = '\0';
    n = strlen(low);

    upd_rm_rf(staged_dir, 0);
    if (upd_mkdir_p(staged_dir) != 0) {
        set_err(err, err_sz, "Could not create staging directory");
        return -1;
    }

    if (strcmp(asset_name, "classiccars-studio") == 0 || strcmp(asset_name, "classiccars-studio.exe") == 0) {
        char dst[PATH_MAX];
        if (snprintf(dst, sizeof(dst), "%s/%s", staged_dir, asset_name) >= (int)sizeof(dst) ||
            upd_copy_file_atomic(archive_path, dst, asset_name) != 0) {
            set_err(err, err_sz, "Could not stage standalone binary asset");
            return -1;
        }
        return 0;
    }

    if (snprintf(extract_tmp, sizeof(extract_tmp), "%s.extract-tmp", staged_dir) >= (int)sizeof(extract_tmp)) {
        set_err(err, err_sz, "Staging path is too long");
        return -1;
    }
    upd_rm_rf(extract_tmp, 0);
    if (upd_mkdir_p(extract_tmp) != 0) {
        set_err(err, err_sz, "Could not create temporary extraction directory");
        return -1;
    }

#ifndef _WIN32
    if ((n > 7 && strcmp(low + n - 7, ".tar.gz") == 0) || (n > 4 && strcmp(low + n - 4, ".tgz") == 0)) {
        pid_t pid = fork();
        int status = 0;
        if (pid == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2) close(devnull);
            }
            execlp("tar", "tar", "-xzf", archive_path, "-C", extract_tmp, (char *)NULL);
            _exit(127);
        }
        if (pid > 0 && waitpid(pid, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            extracted = 1;
        }
    } else if (n > 4 && strcmp(low + n - 4, ".zip") == 0) {
        pid_t pid = fork();
        int status = 0;
        if (pid == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) {
                dup2(devnull, STDIN_FILENO);
                dup2(devnull, STDOUT_FILENO);
                dup2(devnull, STDERR_FILENO);
                if (devnull > 2) close(devnull);
            }
            execlp("unzip", "unzip", "-q", "-o", archive_path, "-d", extract_tmp, (char *)NULL);
            _exit(127);
        }
        if (pid > 0 && waitpid(pid, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            extracted = 1;
        }
    }
    if (!extracted) {
        pid_t pid = fork();
        int status = 0;
        if (pid == 0) {
            execlp("python3", "python3", "-c",
                   "import sys, tarfile, zipfile\n"
                   "arc, out = sys.argv[1], sys.argv[2]\n"
                   "if arc.lower().endswith('.zip'):\n"
                   "    with zipfile.ZipFile(arc, 'r') as zf: zf.extractall(out)\n"
                   "else:\n"
                   "    with tarfile.open(arc, 'r:*') as tf: tf.extractall(out)\n",
                   archive_path, extract_tmp, (char *)NULL);
            _exit(127);
        }
        if (pid > 0 && waitpid(pid, &status, 0) > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            extracted = 1;
        }
    }
#else
    {
        char cmd[2048];
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        DWORD code = 1;
        snprintf(cmd, sizeof(cmd), "tar.exe -xf \"%s\" -C \"%s\"", archive_path, extract_tmp);
        memset(&si, 0, sizeof(si));
        memset(&pi, 0, sizeof(pi));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            WaitForSingleObject(pi.hProcess, 60000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            if (code == 0) extracted = 1;
        }
        if (!extracted && n > 4 && strcmp(low + n - 4, ".zip") == 0) {
            code = 1;
            snprintf(cmd, sizeof(cmd),
                     "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
                     "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
                     archive_path, extract_tmp);
            memset(&si, 0, sizeof(si));
            memset(&pi, 0, sizeof(pi));
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, 60000);
                GetExitCodeProcess(pi.hProcess, &code);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                if (code == 0) extracted = 1;
            }
        }
    }
#endif

    if (!extracted) {
        upd_rm_rf(extract_tmp, 0);
        set_err(err, err_sz, "Could not extract release archive");
        return -1;
    }

    upd_resolve_payload_root(extract_tmp, payload_root, sizeof(payload_root));
    if (upd_sync_whitelisted_entries(payload_root, staged_dir) != 0) {
        upd_rm_rf(extract_tmp, 0);
        set_err(err, err_sz, "Release archive did not contain valid ClassicCars Studio files");
        return -1;
    }
    upd_rm_rf(extract_tmp, 0);
    return 0;
}

static void read_staged_ready(const char *base, char *ver_out, size_t ver_sz,
                              char *at_out, size_t at_sz, char *sha_out, size_t sha_sz) {
    char ready_path[PATH_MAX], staged_dir[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    char perr[64];
    JsonDoc *doc;
    struct stat st;
    if (ver_out && ver_sz) ver_out[0] = '\0';
    if (at_out && at_sz) at_out[0] = '\0';
    if (sha_out && sha_sz) sha_out[0] = '\0';
    if (settings_install_path(base, "state/pending-update/ready.json", ready_path, sizeof(ready_path)) != 0 ||
        settings_install_path(base, "state/pending-update/staged", staged_dir, sizeof(staged_dir)) != 0) {
        return;
    }
    if (stat(staged_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return;
    if (http_read_file(ready_path, &data, &len, 64u * 1024u) != 0 || !data) return;
    doc = json_parse(data, len, perr, sizeof(perr));
    free(data);
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        return;
    }
    {
        JsonValue *v = json_object_get(doc->root, "version");
        const char *s = v && json_type(v) == JSON_STRING ? json_string_get(v, NULL) : NULL;
        if (s && ver_out && ver_sz) snprintf(ver_out, ver_sz, "%s", s);
        v = json_object_get(doc->root, "stagedAt");
        s = v && json_type(v) == JSON_STRING ? json_string_get(v, NULL) : NULL;
        if (s && at_out && at_sz) snprintf(at_out, at_sz, "%s", s);
        v = json_object_get(doc->root, "sha256");
        s = v && json_type(v) == JSON_STRING ? json_string_get(v, NULL) : NULL;
        if (s && sha_out && sha_sz) snprintf(sha_out, sha_sz, "%s", s);
    }
    json_doc_free(doc);
}

int updater_status(const char *studio_home, JsonDoc **doc_out, char *err, size_t err_sz) {
    const char *base = effective_home(studio_home);
    const StudioAppConfig *cfg;
    char staged_ver[64], staged_at[64], staged_sha[128];
    CachedRelease snap;
    JsonValue *root;
    int update_avail = 0, is_dismissed = 0;

    if (!doc_out) {
        set_err(err, err_sz, "Invalid output pointer");
        return -1;
    }
    *doc_out = NULL;
    app_config_load(base);
    cfg = app_config_get();
    read_staged_ready(base, staged_ver, sizeof(staged_ver),
                      staged_at, sizeof(staged_at), staged_sha, sizeof(staged_sha));

    upd_lock();
    snap = g_cached;
    upd_unlock();

    if (snap.checked && snap.version[0]) {
        update_avail = updater_compare_versions(STUDIO_VERSION, snap.version) < 0 ? 1 : 0;
        if (cfg->dismissed_release_version[0] &&
            updater_compare_versions(cfg->dismissed_release_version, snap.version) == 0) {
            is_dismissed = 1;
        }
    }

    root = json_object();
    if (!root) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "installedVersion", json_string_cstr(STUDIO_VERSION));
    json_object_put(root, "updateRepo", json_string_cstr(cfg->update_repo));
    json_object_put(root, "autoCheckUpdates", json_bool(cfg->auto_check_updates ? 1 : 0));
    json_object_put(root, "dismissedVersion", json_string_cstr(cfg->dismissed_release_version));
    json_object_put(root, "stagedVersion", json_string_cstr(staged_ver));
    json_object_put(root, "stagedAt", json_string_cstr(staged_at));
    json_object_put(root, "stagedSha256", json_string_cstr(staged_sha));
    json_object_put(root, "readyToApply", json_bool(staged_ver[0] ? 1 : 0));
    json_object_put(root, "checked", json_bool(snap.checked ? 1 : 0));
    json_object_put(root, "updateAvailable", json_bool(update_avail));
    json_object_put(root, "isDismissed", json_bool(is_dismissed));

    if (snap.checked && snap.version[0]) {
        JsonValue *rel = json_object();
        if (rel) {
            json_object_put(rel, "version", json_string_cstr(snap.version));
            json_object_put(rel, "tagName", json_string_cstr(snap.tag_name));
            json_object_put(rel, "name", json_string_cstr(snap.name));
            json_object_put(rel, "body", json_string_cstr(snap.notes));
            json_object_put(rel, "notes", json_string_cstr(snap.notes));
            json_object_put(rel, "htmlUrl", json_string_cstr(snap.html_url));
            json_object_put(rel, "publishedAt", json_string_cstr(snap.published_at));
            json_object_put(rel, "assetName", json_string_cstr(snap.asset_name));
            json_object_put(rel, "assetUrl", json_string_cstr(snap.asset_url));
            json_object_put(rel, "assetSize", json_number_int(snap.asset_size));
            json_object_put(rel, "sha256Url", json_string_cstr(snap.sha256_url));
            json_object_put(root, "latestRelease", rel);
        }
    } else {
        json_object_put(root, "latestRelease", json_null());
    }

    *doc_out = json_doc_new(root);
    return *doc_out ? 0 : -1;
}

static int select_best_asset(const JsonValue *assets, CachedRelease *out) {
    size_t i, n;
    int best_score = 0;
    if (!assets || json_type(assets) != JSON_ARRAY || !out) return 0;
    n = json_array_len(assets);
    for (i = 0; i < n; i++) {
        JsonValue *item = json_array_at(assets, i);
        JsonValue *nv = item ? json_object_get(item, "name") : NULL;
        JsonValue *uv = item ? json_object_get(item, "browser_download_url") : NULL;
        JsonValue *sv = item ? json_object_get(item, "size") : NULL;
        const char *name, *url;
        long long sz = 0;
        char low[256];
        size_t k;
        int score = 0;
        if (!uv) uv = item ? json_object_get(item, "url") : NULL;
        name = nv && json_type(nv) == JSON_STRING ? json_string_get(nv, NULL) : NULL;
        url = uv && json_type(uv) == JSON_STRING ? json_string_get(uv, NULL) : NULL;
        if (!name || !url || !name[0] || !url[0]) continue;
        if (sv && json_type(sv) == JSON_NUMBER) json_as_int(sv, &sz);
        for (k = 0; name[k] && k + 1 < sizeof(low); k++) low[k] = (char)tolower((unsigned char)name[k]);
        low[k] = '\0';
        if (strcmp(low, "sha256sums") == 0 || strstr(low, "sha256") != NULL) {
            snprintf(out->sha256_url, sizeof(out->sha256_url), "%s", url);
            continue;
        }
#ifdef _WIN32
        if ((strstr(low, "windows") || strstr(low, "win")) && strstr(low, ".zip")) score = 100;
        else if (strcmp(low, "classiccars-studio.exe") == 0) score = 80;
#else
        if (strstr(low, "linux") && (strstr(low, ".tar.gz") || strstr(low, ".tgz"))) score = 100;
        else if (strstr(low, "linux") && strstr(low, ".zip")) score = 90;
        else if (strcmp(low, "classiccars-studio") == 0) score = 80;
#endif
        if (score > best_score) {
            best_score = score;
            snprintf(out->asset_name, sizeof(out->asset_name), "%s", name);
            snprintf(out->asset_url, sizeof(out->asset_url), "%s", url);
            out->asset_size = sz;
        }
    }
    return best_score > 0;
}

int updater_check(const char *studio_home, const JsonValue *body,
                  JsonDoc **doc_out, char *err, size_t err_sz) {
    const char *base = effective_home(studio_home);
    const char *manifest_env = getenv("STUDIO_UPDATE_MANIFEST_URL");
    const StudioAppConfig *cfg;
    char url[1024], tmp_manifest[PATH_MAX];
    char *data = NULL;
    size_t len = 0;
    char perr[128];
    JsonDoc *doc;
    CachedRelease next;

    (void)body;
    app_config_load(base);
    cfg = app_config_get();
    if (manifest_env && manifest_env[0]) {
        snprintf(url, sizeof(url), "%s", manifest_env);
    } else {
        snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", cfg->update_repo);
    }
    if (settings_ensure_dirs(base) != 0 ||
        settings_install_path(base, "state/tmp-traffic/release-latest.json", tmp_manifest, sizeof(tmp_manifest)) != 0) {
        set_err(err, err_sz, "Could not prepare temporary manifest path");
        return -1;
    }
    if (fetch_url_to_file(url, tmp_manifest, err, err_sz) != 0) {
        remove(tmp_manifest);
        return -1;
    }
    if (http_read_file(tmp_manifest, &data, &len, 4u * 1024u * 1024u) != 0 || !data) {
        remove(tmp_manifest);
        set_err(err, err_sz, "Could not read GitHub release response");
        return -1;
    }
    remove(tmp_manifest);
    doc = json_parse(data, len, perr, sizeof(perr));
    free(data);
    if (!doc || json_type(doc->root) != JSON_OBJECT) {
        json_doc_free(doc);
        set_err(err, err_sz, "Invalid JSON in GitHub release response");
        return -1;
    }
    memset(&next, 0, sizeof(next));
    next.checked = 1;
    {
        JsonValue *tv = json_object_get(doc->root, "tag_name");
        JsonValue *nv = json_object_get(doc->root, "name");
        JsonValue *bv = json_object_get(doc->root, "body");
        JsonValue *hv = json_object_get(doc->root, "html_url");
        JsonValue *pv = json_object_get(doc->root, "published_at");
        JsonValue *av = json_object_get(doc->root, "assets");
        const char *tag = tv && json_type(tv) == JSON_STRING ? json_string_get(tv, NULL) : NULL;
        const char *ver;
        if (!tag || !tag[0]) {
            json_doc_free(doc);
            set_err(err, err_sz, "Release manifest does not contain tag_name");
            return -1;
        }
        snprintf(next.tag_name, sizeof(next.tag_name), "%s", tag);
        ver = (tag[0] == 'v' || tag[0] == 'V') && isdigit((unsigned char)tag[1]) ? tag + 1 : tag;
        snprintf(next.version, sizeof(next.version), "%s", ver);
        if (nv && json_type(nv) == JSON_STRING && json_string_get(nv, NULL)) {
            snprintf(next.name, sizeof(next.name), "%s", json_string_get(nv, NULL));
        } else {
            snprintf(next.name, sizeof(next.name), "ClassicCars Studio v%s", next.version);
        }
        if (bv && json_type(bv) == JSON_STRING && json_string_get(bv, NULL)) {
            snprintf(next.notes, sizeof(next.notes), "%s", json_string_get(bv, NULL));
        }
        if (hv && json_type(hv) == JSON_STRING && json_string_get(hv, NULL)) {
            snprintf(next.html_url, sizeof(next.html_url), "%s", json_string_get(hv, NULL));
        }
        if (pv && json_type(pv) == JSON_STRING && json_string_get(pv, NULL)) {
            snprintf(next.published_at, sizeof(next.published_at), "%s", json_string_get(pv, NULL));
        }
        select_best_asset(av, &next);
    }
    json_doc_free(doc);

    upd_lock();
    g_cached = next;
    upd_unlock();
    return updater_status(base, doc_out, err, err_sz);
}

int updater_dismiss(const char *studio_home, const JsonValue *body,
                    JsonDoc **doc_out, char *err, size_t err_sz) {
    const char *base = effective_home(studio_home);
    const char *version = NULL;
    CachedRelease snap;
    JsonValue *patch;
    int rc;

    if (body && json_type(body) == JSON_OBJECT) {
        JsonValue *v = json_object_get(body, "version");
        if (v && json_type(v) == JSON_STRING) version = json_string_get(v, NULL);
    }
    if (!version) {
        upd_lock();
        snap = g_cached;
        upd_unlock();
        version = snap.version;
    }
    patch = json_object();
    if (!patch) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    json_object_put(patch, "dismissedReleaseVersion", json_string_cstr(version ? version : ""));
    rc = app_config_apply_update(base, patch, err, err_sz);
    json_free(patch);
    if (rc != 0) return rc;
    return updater_status(base, doc_out, err, err_sz);
}

static int parse_expected_sha256(const char *sums_path, const char *asset_name, char hex_out[65]) {
    char *data = NULL;
    size_t len = 0;
    char *line, *saveptr = NULL;
    hex_out[0] = '\0';
    if (http_read_file(sums_path, &data, &len, 256u * 1024u) != 0 || !data) return -1;
    for (line = strtok_r(data, "\r\n", &saveptr); line; line = strtok_r(NULL, "\r\n", &saveptr)) {
        size_t i = 0;
        while (*line == ' ' || *line == '\t') line++;
        while (isxdigit((unsigned char)line[i])) i++;
        if (i == 64 && strstr(line + 64, asset_name) != NULL) {
            size_t k;
            for (k = 0; k < 64; k++) hex_out[k] = (char)tolower((unsigned char)line[k]);
            hex_out[64] = '\0';
            free(data);
            return 0;
        }
    }
    free(data);
    return -1;
}

int updater_download(const char *studio_home, const JsonValue *body,
                     JsonDoc **doc_out, char *err, size_t err_sz) {
    const char *base = effective_home(studio_home);
    CachedRelease snap;
    char pending_dir[PATH_MAX], archive_path[PATH_MAX], sums_path[PATH_MAX];
    char staged_dir[PATH_MAX], ready_path[PATH_MAX];
    char actual_sha[65], expected_sha[65], iso_now[64];
    time_t now_t;
    struct tm tm_utc;

    upd_lock();
    snap = g_cached;
    upd_unlock();
    if (!snap.checked || !snap.version[0]) {
        if (updater_check(base, body, doc_out, err, err_sz) != 0) return -1;
        json_doc_free(*doc_out);
        *doc_out = NULL;
        upd_lock();
        snap = g_cached;
        upd_unlock();
    }
    if (!snap.asset_url[0] || !snap.asset_name[0]) {
        set_err(err, err_sz, "No compatible release package found for this operating system");
        return 1;
    }
    if (settings_ensure_dirs(base) != 0 ||
        settings_install_path(base, "state/pending-update", pending_dir, sizeof(pending_dir)) != 0 ||
        settings_install_path(base, "state/pending-update/staged", staged_dir, sizeof(staged_dir)) != 0 ||
        settings_install_path(base, "state/pending-update/ready.json", ready_path, sizeof(ready_path)) != 0) {
        set_err(err, err_sz, "Could not resolve state/pending-update directory");
        return -1;
    }
#ifdef _WIN32
    _mkdir(pending_dir);
#else
    mkdir(pending_dir, 0700);
#endif
    remove(ready_path);
    if (snprintf(archive_path, sizeof(archive_path), "%s/%s", pending_dir, snap.asset_name) >= (int)sizeof(archive_path)) {
        set_err(err, err_sz, "Archive path is too long");
        return -1;
    }
    if (fetch_url_to_file(snap.asset_url, archive_path, err, err_sz) != 0) {
        remove(archive_path);
        return -1;
    }
    if (updater_sha256_file(archive_path, actual_sha) != 0) {
        remove(archive_path);
        set_err(err, err_sz, "Could not compute SHA-256 checksum of downloaded archive");
        return -1;
    }
    if (snap.sha256_url[0]) {
        if (snprintf(sums_path, sizeof(sums_path), "%s/SHA256SUMS", pending_dir) < (int)sizeof(sums_path) &&
            fetch_url_to_file(snap.sha256_url, sums_path, err, err_sz) == 0) {
            if (parse_expected_sha256(sums_path, snap.asset_name, expected_sha) == 0 &&
                strcasecmp(actual_sha, expected_sha) != 0) {
                remove(archive_path);
                remove(sums_path);
                set_err(err, err_sz, "SHA-256 checksum verification failed for downloaded update");
                return -1;
            }
        }
    }
    if (stage_archive_contents(archive_path, snap.asset_name, staged_dir, err, err_sz) != 0) {
        return -1;
    }
    now_t = time(NULL);
#ifdef _WIN32
    gmtime_s(&tm_utc, &now_t);
#else
    gmtime_r(&now_t, &tm_utc);
#endif
    strftime(iso_now, sizeof(iso_now), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    {
        FILE *fp = fopen(ready_path, "wb");
        if (!fp) {
            set_err(err, err_sz, "Could not write state/pending-update/ready.json");
            return -1;
        }
        fprintf(fp,
                "{\n"
                "  \"version\": \"%s\",\n"
                "  \"stagedAt\": \"%s\",\n"
                "  \"sha256\": \"%s\",\n"
                "  \"assetName\": \"%s\"\n"
                "}\n",
                snap.version, iso_now, actual_sha, snap.asset_name);
        fclose(fp);
    }
    return updater_status(base, doc_out, err, err_sz);
}

int updater_apply_pending_on_boot(const char *studio_home, char *err, size_t err_sz) {
    const char *base = effective_home(studio_home);
    char staged_ver[64], staged_at[64], staged_sha[128];
    char pending_dir[PATH_MAX], staged_dir[PATH_MAX], install_root[PATH_MAX], old_exe[PATH_MAX];
    char *slash;

    if (settings_install_path(base, "VERSION", install_root, sizeof(install_root)) == 0 &&
        (slash = strrchr(install_root, '/')) != NULL && slash != install_root) {
        *slash = '\0';
    } else {
        snprintf(install_root, sizeof(install_root), "%s", base);
    }
    if (snprintf(old_exe, sizeof(old_exe), "%s/classiccars-studio.exe.old", install_root) < (int)sizeof(old_exe)) {
#ifdef _WIN32
        DeleteFileA(old_exe);
#else
        remove(old_exe);
#endif
    }

    read_staged_ready(base, staged_ver, sizeof(staged_ver),
                      staged_at, sizeof(staged_at), staged_sha, sizeof(staged_sha));
    if (!staged_ver[0]) return 0;
    if (settings_install_path(base, "state/pending-update", pending_dir, sizeof(pending_dir)) != 0 ||
        settings_install_path(base, "state/pending-update/staged", staged_dir, sizeof(staged_dir)) != 0) {
        return 0;
    }
    if (upd_sync_whitelisted_entries(staged_dir, install_root) != 0) {
        set_err(err, err_sz, "Could not apply staged update");
        return -1;
    }
    upd_rm_rf(pending_dir, 0);
    return 1;
}

int updater_apply(const char *studio_home, const JsonValue *body,
                  JsonDoc **doc_out, char *err, size_t err_sz) {
    const char *base = effective_home(studio_home);
    char staged_ver[64], staged_at[64], staged_sha[128];
    int restart = 1;
    int rc;
    JsonValue *root;

    if (!doc_out) {
        set_err(err, err_sz, "Invalid output pointer");
        return -1;
    }
    *doc_out = NULL;
    if (body && json_type(body) == JSON_OBJECT) {
        JsonValue *rv = json_object_get(body, "restart");
        if (rv && json_type(rv) == JSON_BOOL) {
            restart = json_bool_value(rv) ? 1 : 0;
        }
    }
    read_staged_ready(base, staged_ver, sizeof(staged_ver),
                      staged_at, sizeof(staged_at), staged_sha, sizeof(staged_sha));
    if (!staged_ver[0]) {
        set_err(err, err_sz, "No staged update is waiting to be applied");
        return 1;
    }
    rc = updater_apply_pending_on_boot(base, err, err_sz);
    if (rc < 0) return -1;

    root = json_object();
    if (!root) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }
    json_object_put(root, "ok", json_bool(1));
    json_object_put(root, "applied", json_bool(1));
    json_object_put(root, "appliedVersion", json_string_cstr(staged_ver));
    json_object_put(root, "restarting", json_bool(restart ? 1 : 0));
    *doc_out = json_doc_new(root);
    if (!*doc_out) {
        set_err(err, err_sz, "Out of memory");
        return -1;
    }

    if (restart) {
        updater_request_restart();
        studio_window_request_close();
    }
    return 0;
}
