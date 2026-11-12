#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "preflight.h"

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
#include <winreg.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

static void trim_crlf(char *s) {
    size_t n;
    if (!s) return;
    n = strlen(s);
    while (n > 0 && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
        s[--n] = '\0';
    }
}

#ifdef _WIN32

static int file_exists_a(const char *path) {
    DWORD attr;
    if (!path || !path[0]) return 0;
    attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int dir_has_exe(const char *dir, const char *exe_name, char *full_out, size_t full_sz) {
    char candidate[MAX_PATH];
    size_t len;
    if (!dir || !dir[0] || !exe_name) return 0;
    len = strlen(dir);
    while (len > 0 && (dir[len - 1] == '\\' || dir[len - 1] == '/')) len--;
    if (len == 0 || len + 1 + strlen(exe_name) + 1 > sizeof candidate) return 0;
    memcpy(candidate, dir, len);
    candidate[len] = '\\';
    strcpy(candidate + len + 1, exe_name);
    if (!file_exists_a(candidate)) return 0;
    if (full_out && full_sz > 0) snprintf(full_out, full_sz, "%.*s", (int)len, dir);
    return 1;
}

static int path_contains_dir(const char *path_env, const char *dir) {
    const char *p = path_env;
    size_t dlen;
    if (!path_env || !dir || !dir[0]) return 0;
    dlen = strlen(dir);
    while (dlen > 0 && (dir[dlen - 1] == '\\' || dir[dlen - 1] == '/')) dlen--;
    if (dlen == 0) return 0;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t elen = semi ? (size_t)(semi - p) : strlen(p);
        while (elen > 0 && (p[elen - 1] == '\\' || p[elen - 1] == '/')) elen--;
        if (elen == dlen && _strnicmp(p, dir, dlen) == 0) return 1;
        if (!semi) break;
        p = semi + 1;
    }
    return 0;
}

static void prepend_to_process_path(const char *dir) {
    char *cur;
    char *next;
    size_t dlen;
    size_t clen;
    DWORD need;
    if (!dir || !dir[0]) return;
    need = GetEnvironmentVariableA("PATH", NULL, 0);
    cur = (char *)calloc(need + 1, 1);
    if (!cur) return;
    if (need > 0) GetEnvironmentVariableA("PATH", cur, need + 1);
    if (path_contains_dir(cur, dir)) {
        free(cur);
        return;
    }
    dlen = strlen(dir);
    clen = strlen(cur);
    next = (char *)malloc(dlen + 1 + clen + 1);
    if (next) {
        snprintf(next, dlen + 1 + clen + 1, "%s%s%s", dir, clen ? ";" : "", cur);
        SetEnvironmentVariableA("PATH", next);
        _putenv_s("PATH", next);
        free(next);
    }
    free(cur);
}

static int search_wildcard_for_exe(const char *base_dir, const char *pattern, const char *subpath,
                                   const char *exe_name, int search_children, char *out, size_t out_sz) {
    char glob[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (!base_dir || !base_dir[0]) return 0;
    if (snprintf(glob, sizeof glob, "%s\\%s", base_dir, pattern) <= 0) return 0;
    h = FindFirstFileA(glob, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char dir[MAX_PATH];
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
        if (subpath && subpath[0]) {
            snprintf(dir, sizeof dir, "%s\\%s\\%s", base_dir, fd.cFileName, subpath);
        } else {
            snprintf(dir, sizeof dir, "%s\\%s", base_dir, fd.cFileName);
        }
        if (dir_has_exe(dir, exe_name, out, out_sz)) {
            FindClose(h);
            return 1;
        }
        if (search_children) {
            char subglob[MAX_PATH];
            WIN32_FIND_DATAA subfd;
            HANDLE subh;
            snprintf(subglob, sizeof subglob, "%s\\*", dir);
            subh = FindFirstFileA(subglob, &subfd);
            if (subh != INVALID_HANDLE_VALUE) {
                do {
                    char subdir[MAX_PATH];
                    if ((subfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
                    if (strcmp(subfd.cFileName, ".") == 0 || strcmp(subfd.cFileName, "..") == 0) continue;
                    snprintf(subdir, sizeof subdir, "%s\\%s", dir, subfd.cFileName);
                    if (dir_has_exe(subdir, exe_name, out, out_sz)) {
                        FindClose(subh);
                        FindClose(h);
                        return 1;
                    }
                } while (FindNextFileA(subh, &subfd));
                FindClose(subh);
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

static void sync_registry_path_for_exe(HKEY root, const char *subkey, const char *exe_name) {
    char raw[8192];
    char expanded[8192];
    DWORD bytes = sizeof raw - 1;
    const char *p;
    memset(raw, 0, sizeof raw);
    if (RegGetValueA(root, subkey, "Path", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND,
                     NULL, raw, &bytes) != ERROR_SUCCESS) {
        bytes = sizeof raw - 1;
        if (RegGetValueA(root, subkey, "Path", RRF_RT_ANY, NULL, raw, &bytes) != ERROR_SUCCESS) return;
    }
    if (!ExpandEnvironmentStringsA(raw, expanded, sizeof expanded)) {
        snprintf(expanded, sizeof expanded, "%s", raw);
    }
    p = expanded;
    while (*p) {
        char entry[MAX_PATH];
        char found_dir[MAX_PATH];
        const char *semi = strchr(p, ';');
        size_t len = semi ? (size_t)(semi - p) : strlen(p);
        if (len > 0 && len < sizeof entry) {
            memcpy(entry, p, len);
            entry[len] = '\0';
            if (dir_has_exe(entry, exe_name, found_dir, sizeof found_dir)) {
                prepend_to_process_path(found_dir);
            }
        }
        if (!semi) break;
        p = semi + 1;
    }
}

static void discover_php_on_windows(void) {
    char dummy[MAX_PATH];
    char found[MAX_PATH];
    char localapp[MAX_PATH];
    char prog[MAX_PATH];
    char prog86[MAX_PATH];
    char userprof[MAX_PATH];
    char candidate[MAX_PATH];
    if (SearchPathA(NULL, "php.exe", NULL, MAX_PATH, dummy, NULL) > 0) return;

    sync_registry_path_for_exe(HKEY_CURRENT_USER, "Environment", "php.exe");
    sync_registry_path_for_exe(HKEY_LOCAL_MACHINE,
                               "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", "php.exe");
    if (SearchPathA(NULL, "php.exe", NULL, MAX_PATH, dummy, NULL) > 0) return;

    localapp[0] = prog[0] = prog86[0] = userprof[0] = '\0';
    GetEnvironmentVariableA("LOCALAPPDATA", localapp, sizeof localapp);
    GetEnvironmentVariableA("ProgramFiles", prog, sizeof prog);
    GetEnvironmentVariableA("ProgramFiles(x86)", prog86, sizeof prog86);
    GetEnvironmentVariableA("USERPROFILE", userprof, sizeof userprof);

    if (localapp[0]) {
        snprintf(candidate, sizeof candidate, "%s\\Microsoft\\WinGet\\Links", localapp);
        if (dir_has_exe(candidate, "php.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
        snprintf(candidate, sizeof candidate, "%s\\Microsoft\\WinGet\\Packages", localapp);
        if (search_wildcard_for_exe(candidate, "PHP.*", "", "php.exe", 1, found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
        snprintf(candidate, sizeof candidate, "%s\\Programs", localapp);
        if (search_wildcard_for_exe(candidate, "php*", "", "php.exe", 1, found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
    }
    if (search_wildcard_for_exe("C:\\tools", "php*", "", "php.exe", 1, found, sizeof found) ||
        search_wildcard_for_exe("C:", "php*", "", "php.exe", 0, found, sizeof found) ||
        (prog[0] && search_wildcard_for_exe(prog, "php*", "", "php.exe", 1, found, sizeof found)) ||
        (prog86[0] && search_wildcard_for_exe(prog86, "php*", "", "php.exe", 1, found, sizeof found)) ||
        dir_has_exe("C:\\xampp\\php", "php.exe", found, sizeof found) ||
        search_wildcard_for_exe("C:\\laragon\\bin\\php", "php*", "", "php.exe", 0, found, sizeof found)) {
        prepend_to_process_path(found);
        return;
    }
    if (userprof[0]) {
        snprintf(candidate, sizeof candidate, "%s\\scoop\\apps\\php\\current", userprof);
        if (dir_has_exe(candidate, "php.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
    }
}

static void discover_git_on_windows(void) {
    char dummy[MAX_PATH];
    char found[MAX_PATH];
    char localapp[MAX_PATH];
    char prog[MAX_PATH];
    char prog86[MAX_PATH];
    char userprof[MAX_PATH];
    char candidate[MAX_PATH];
    if (SearchPathA(NULL, "git.exe", NULL, MAX_PATH, dummy, NULL) > 0) return;

    sync_registry_path_for_exe(HKEY_CURRENT_USER, "Environment", "git.exe");
    sync_registry_path_for_exe(HKEY_LOCAL_MACHINE,
                               "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment", "git.exe");
    if (SearchPathA(NULL, "git.exe", NULL, MAX_PATH, dummy, NULL) > 0) return;

    localapp[0] = prog[0] = prog86[0] = userprof[0] = '\0';
    GetEnvironmentVariableA("LOCALAPPDATA", localapp, sizeof localapp);
    GetEnvironmentVariableA("ProgramFiles", prog, sizeof prog);
    GetEnvironmentVariableA("ProgramFiles(x86)", prog86, sizeof prog86);
    GetEnvironmentVariableA("USERPROFILE", userprof, sizeof userprof);

    if (prog[0]) {
        snprintf(candidate, sizeof candidate, "%s\\Git\\cmd", prog);
        if (dir_has_exe(candidate, "git.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
    }
    if (prog86[0]) {
        snprintf(candidate, sizeof candidate, "%s\\Git\\cmd", prog86);
        if (dir_has_exe(candidate, "git.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
    }
    if (localapp[0]) {
        snprintf(candidate, sizeof candidate, "%s\\Programs\\Git\\cmd", localapp);
        if (dir_has_exe(candidate, "git.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
        snprintf(candidate, sizeof candidate, "%s\\Microsoft\\WinGet\\Links", localapp);
        if (dir_has_exe(candidate, "git.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
    }
    if (userprof[0]) {
        snprintf(candidate, sizeof candidate, "%s\\scoop\\apps\\git\\current\\cmd", userprof);
        if (dir_has_exe(candidate, "git.exe", found, sizeof found)) {
            prepend_to_process_path(found);
            return;
        }
    }
    if (dir_has_exe("C:\\tools\\git\\cmd", "git.exe", found, sizeof found)) {
        prepend_to_process_path(found);
    }
}

static int run_cmd_capture_win(const char *cmdline, char *out, size_t out_sz) {
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    HANDLE rd = NULL;
    HANDLE wr = NULL;
    HANDLE nul = NULL;
    char mutable_cmd[512];
    DWORD code = 1;
    size_t total = 0;
    if (!out || out_sz == 0) return -1;
    out[0] = '\0';
    snprintf(mutable_cmd, sizeof mutable_cmd, "%s", cmdline);
    memset(&sa, 0, sizeof sa);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    sa.nLength = sizeof sa;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                      &sa, OPEN_EXISTING, 0, NULL);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nul != INVALID_HANDLE_VALUE ? nul : GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = nul != INVALID_HANDLE_VALUE ? nul : GetStdHandle(STD_ERROR_HANDLE);
    if (!CreateProcessA(NULL, mutable_cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        CloseHandle(rd);
        CloseHandle(wr);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        return -1;
    }
    CloseHandle(wr);
    while (total + 1 < out_sz) {
        DWORD got = 0;
        if (!ReadFile(rd, out + total, (DWORD)(out_sz - 1 - total), &got, NULL) || got == 0) break;
        total += (size_t)got;
    }
    out[total] = '\0';
    CloseHandle(rd);
    if (WaitForSingleObject(pi.hProcess, 4000) != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    trim_crlf(out);
    return code == 0 ? 0 : -1;
}

static void probe_webview_and_browser_win(StudioPreflightStatus *st) {
    static const char *keys[] = {
        "Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        "Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}",
        "Software\\Microsoft\\EdgeUpdate\\Clients\\{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}",
    };
    const HKEY roots[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    char pv[64];
    char prog86[MAX_PATH];
    char prog[MAX_PATH];
    char localapp[MAX_PATH];
    size_t r, k;

    for (r = 0; r < 2 && !st->webview_ok; r++) {
        for (k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
            DWORD bytes = sizeof pv - 1;
            memset(pv, 0, sizeof pv);
            if (RegGetValueA(roots[r], keys[k], "pv", RRF_RT_REG_SZ, NULL, pv, &bytes) == ERROR_SUCCESS &&
                pv[0] && strcmp(pv, "0.0.0.0") != 0) {
                st->webview_ok = 1;
                snprintf(st->webview_info, sizeof st->webview_info, "WebView2 / EdgeCore v%s", pv);
                break;
            }
        }
    }

    prog86[0] = prog[0] = localapp[0] = '\0';
    GetEnvironmentVariableA("ProgramFiles(x86)", prog86, sizeof prog86);
    GetEnvironmentVariableA("ProgramFiles", prog, sizeof prog);
    GetEnvironmentVariableA("LOCALAPPDATA", localapp, sizeof localapp);

    if (!st->webview_ok) {
        const char *env_roots[] = {prog86, prog, localapp};
        const char *app_dirs[] = {
            "Microsoft\\EdgeWebView\\Application",
            "Microsoft\\EdgeCore",
            "Microsoft\\Edge\\Application",
        };
        for (r = 0; r < 3 && !st->webview_ok; r++) {
            if (!env_roots[r][0]) continue;
            for (k = 0; k < 3 && !st->webview_ok; k++) {
                char base[MAX_PATH];
                char found[MAX_PATH];
                snprintf(base, sizeof base, "%s\\%s", env_roots[r], app_dirs[k]);
                if (search_wildcard_for_exe(base, "*", "EBWebView\\x64", "EmbeddedBrowserWebView.dll",
                                            0, found, sizeof found)) {
                    st->webview_ok = 1;
                    snprintf(st->webview_info, sizeof st->webview_info, "EmbeddedBrowserWebView (%s)", app_dirs[k]);
                }
            }
        }
    }

    {
        const char *browser_paths[] = {
            "Microsoft\\Edge\\Application\\msedge.exe",
            "Google\\Chrome\\Application\\chrome.exe",
        };
        const char *browser_names[] = {"Microsoft Edge", "Google Chrome"};
        const char *env_roots[] = {prog86, prog, localapp};
        for (k = 0; k < 2 && !st->browser_ok; k++) {
            for (r = 0; r < 3 && !st->browser_ok; r++) {
                char candidate[MAX_PATH];
                if (!env_roots[r][0]) continue;
                snprintf(candidate, sizeof candidate, "%s\\%s", env_roots[r], browser_paths[k]);
                if (file_exists_a(candidate)) {
                    st->browser_ok = 1;
                    snprintf(st->browser_info, sizeof st->browser_info, "%s (--app host ready)", browser_names[k]);
                }
            }
        }
    }
}

#endif /* _WIN32 */

void studio_preflight_discover_path(void) {
#ifdef _WIN32
    discover_php_on_windows();
    discover_git_on_windows();
#endif
}

void studio_preflight_probe(StudioPreflightStatus *st) {
    if (!st) return;
    memset(st, 0, sizeof *st);
    studio_preflight_discover_path();
#ifdef _WIN32
    {
        char out[512];
        if (run_cmd_capture_win("php -r \"echo PHP_VERSION;\"", out, sizeof out) == 0 && out[0]) {
            st->php_ok = 1;
            snprintf(st->php_version, sizeof st->php_version, "%.*s", (int)(sizeof st->php_version - 1), out);
            if (SearchPathA(NULL, "php.exe", NULL, (DWORD)sizeof st->php_path, st->php_path, NULL) == 0) {
                snprintf(st->php_path, sizeof st->php_path, "php.exe");
            }
        }
        if (run_cmd_capture_win("git --version", out, sizeof out) == 0 && out[0]) {
            const char *ver = out;
            if (strncmp(ver, "git version ", 12) == 0) ver += 12;
            st->git_ok = 1;
            snprintf(st->git_version, sizeof st->git_version, "%.*s", (int)(sizeof st->git_version - 1), ver);
            if (SearchPathA(NULL, "git.exe", NULL, (DWORD)sizeof st->git_path, st->git_path, NULL) == 0) {
                snprintf(st->git_path, sizeof st->git_path, "git.exe");
            }
        }
        probe_webview_and_browser_win(st);
    }
#else
    {
        FILE *fp = popen("php -r 'echo PHP_VERSION . \"\\n\" . PHP_BINARY;' 2>/dev/null", "r");
        if (fp) {
            if (fgets(st->php_version, sizeof st->php_version, fp)) {
                trim_crlf(st->php_version);
                if (fgets(st->php_path, sizeof st->php_path, fp)) trim_crlf(st->php_path);
            }
            if (pclose(fp) == 0 && st->php_version[0]) st->php_ok = 1;
        }
        fp = popen("git --version 2>/dev/null", "r");
        if (fp) {
            char line[128];
            if (fgets(line, sizeof line, fp)) {
                const char *v = line;
                trim_crlf(line);
                if (strncmp(v, "git version ", 12) == 0) v += 12;
                snprintf(st->git_version, sizeof st->git_version, "%.*s", (int)(sizeof st->git_version - 1), v);
            }
            if (pclose(fp) == 0 && st->git_version[0]) {
                st->git_ok = 1;
                snprintf(st->git_path, sizeof st->git_path, "git");
            }
        }
        {
            void *gtk = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_LOCAL);
            void *wk = dlopen("libwebkit2gtk-4.1.so.0", RTLD_LAZY | RTLD_LOCAL);
            if (!wk) wk = dlopen("libwebkit2gtk-4.0.so.37", RTLD_LAZY | RTLD_LOCAL);
            if (gtk && wk) {
                st->webview_ok = 1;
                snprintf(st->webview_info, sizeof st->webview_info, "GTK 3 + WebKitGTK");
            }
            if (wk) dlclose(wk);
            if (gtk) dlclose(gtk);
        }
        if (access("/usr/bin/xdg-open", X_OK) == 0) {
            st->browser_ok = 1;
            snprintf(st->browser_info, sizeof st->browser_info, "xdg-open");
        }
    }
#endif
}

void studio_preflight_format_report(const StudioPreflightStatus *st, int require_php,
                                    char *out, size_t out_sz) {
    if (!st || !out || out_sz == 0) return;
    snprintf(
        out, out_sz,
        "ClassicCars Studio — All-at-Once Prerequisite Check:\r\n\r\n"
        "  1. PHP Runtime (%s):\r\n"
        "     %s%s%s%s\r\n\r\n"
        "  2. Git Version Control (Optional — for Sync / Publish):\r\n"
        "     %s%s%s%s\r\n\r\n"
        "  3. Display Engine (Embedded WebView2 / GTK):\r\n"
        "     %s\r\n\r\n"
        "  4. Standalone Browser App Host:\r\n"
        "     %s\r\n",
        require_php ? "Required for live PHP preview" : "Optional (--no-php enabled)",
        st->php_ok ? "[OK] Found PHP v" : "[MISSING] PHP was not found on PATH or standard install folders",
        st->php_ok ? st->php_version : "",
        (st->php_ok && st->php_path[0]) ? " at " : "",
        (st->php_ok && st->php_path[0]) ? st->php_path : "",
        st->git_ok ? "[OK] Found Git v" : "[NOT FOUND] Git is not installed (optional)",
        st->git_ok ? st->git_version : "",
        (st->git_ok && st->git_path[0]) ? " at " : "",
        (st->git_ok && st->git_path[0]) ? st->git_path : "",
        st->webview_ok ? st->webview_info : "[FALLBACK] Embedded runtime not found; will use Browser --app window",
        st->browser_ok ? st->browser_info : "System default browser"
    );
}

int studio_preflight_require(int require_php, int headless) {
    StudioPreflightStatus st;
    char report[2048];
    char prompt[2560];
    studio_preflight_probe(&st);
    if (!require_php || st.php_ok) return 0;

    studio_preflight_format_report(&st, require_php, report, sizeof report);
#ifdef _WIN32
    if (!headless) {
        int choice;
        snprintf(
            prompt, sizeof prompt,
            "%s\r\n"
            "Would you like ClassicCars Studio to install PHP automatically now via Windows Package Manager (winget)?\r\n\r\n"
            "  - Click YES to install PHP%s automatically now and continue.\r\n"
            "  - Click NO to open ClassicCars Studio right now in Static Preview mode (--no-php).\r\n"
            "  - Click CANCEL to exit.",
            report,
            st.git_ok ? "" : " (and Git)"
        );
        choice = MessageBoxA(NULL, prompt, "ClassicCars Studio — Prerequisite Check",
                             MB_YESNOCANCEL | MB_ICONWARNING | MB_SETFOREGROUND);
        if (choice == IDYES) {
            STARTUPINFOA si;
            PROCESS_INFORMATION pi;
            char cmd[512];
            memset(&si, 0, sizeof si);
            memset(&pi, 0, sizeof pi);
            si.cb = sizeof si;
            snprintf(cmd, sizeof cmd,
                     "cmd.exe /c \"echo Installing prerequisites for ClassicCars Studio... && "
                     "winget install --id PHP.PHP.8.3 -e --accept-source-agreements --accept-package-agreements%s\"",
                     st.git_ok ? "" : " && winget install --id Git.Git -e --accept-source-agreements --accept-package-agreements");
            if (CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                WaitForSingleObject(pi.hProcess, INFINITE);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            studio_preflight_probe(&st);
            if (st.php_ok) return 0;
            choice = MessageBoxA(
                NULL,
                "PHP was still not detected after running winget.\r\n\r\n"
                "Click OK to open ClassicCars Studio in Static Preview mode (--no-php), or Cancel to exit.",
                "ClassicCars Studio",
                MB_OKCANCEL | MB_ICONINFORMATION | MB_SETFOREGROUND
            );
            return choice == IDOK ? 1 : -1;
        }
        if (choice == IDNO) return 1;
        return -1;
    }
#endif
    (void)headless;
    (void)prompt;
    fprintf(stderr, "%s\n", report);
    return -1;
}
