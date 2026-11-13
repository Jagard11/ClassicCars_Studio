#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <process.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winreg.h>

#include "../src/preflight.h"
#include "../src/version.h"

#define ID_EDIT_PREREQ      101
#define ID_BTN_RECHECK      102
#define ID_BTN_INSTALL_DEPS 103
#define ID_EDIT_DIR         104
#define ID_BTN_BROWSE       105
#define ID_CHK_DESKTOP      106
#define ID_CHK_STARTMENU    107
#define ID_CHK_USERPATH     108
#define ID_CHK_AUTOPHP      109
#define ID_CHK_LAUNCH       110
#define ID_STATUS_BANNER    111
#define ID_PROGRESS         112
#define ID_EDIT_LOG         113
#define ID_BTN_INSTALL      114
#define ID_BTN_CLOSE        115

#define WM_APP_LOG          (WM_APP + 1)
#define WM_APP_PROGRESS     (WM_APP + 2)
#define WM_APP_PREREQ_DONE  (WM_APP + 3)
#define WM_APP_INSTALL_DONE (WM_APP + 4)

static HWND g_hwnd = NULL;
static HWND g_edit_prereq = NULL;
static HWND g_btn_recheck = NULL;
static HWND g_btn_deps = NULL;
static HWND g_edit_dir = NULL;
static HWND g_btn_browse = NULL;
static HWND g_chk_desktop = NULL;
static HWND g_chk_startmenu = NULL;
static HWND g_chk_userpath = NULL;
static HWND g_chk_autophp = NULL;
static HWND g_chk_launch = NULL;
static HWND g_lbl_status = NULL;
static HWND g_progress = NULL;
static HWND g_edit_log = NULL;
static HWND g_btn_install = NULL;
static HWND g_btn_close = NULL;

static HFONT g_font_title = NULL;
static HFONT g_font_bold = NULL;
static HFONT g_font_ui = NULL;
static HFONT g_font_mono = NULL;
static HICON g_icon_big = NULL;
static HICON g_icon_sm = NULL;

static volatile LONG g_busy = 0;
static int g_install_finished = 0;
static char g_source_root[MAX_PATH];
static char g_installed_exe[MAX_PATH];
static StudioPreflightStatus g_preflight;

typedef struct InstallOptions {
    char source_root[MAX_PATH];
    char target_dir[MAX_PATH];
    int create_desktop;
    int create_startmenu;
    int update_path;
    int auto_install_php;
    int launch_after;
} InstallOptions;

static void utf8_to_wide(const char *s, wchar_t *out, size_t out_cch) {
    if (!out || out_cch == 0) return;
    out[0] = L'\0';
    if (!s) return;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)out_cch);
    out[out_cch - 1] = L'\0';
}

static void wide_to_utf8(const wchar_t *w, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!w) return;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)out_sz, NULL, NULL);
    out[out_sz - 1] = '\0';
}

static int file_exists_a(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int dir_exists_a(const char *path) {
    DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static int ensure_dir_a(const char *path) {
    if (!path || !path[0]) return 0;
    if (CreateDirectoryA(path, NULL)) return 1;
    return GetLastError() == ERROR_ALREADY_EXISTS || dir_exists_a(path);
}

static int ensure_tree_a(const char *path) {
    char tmp[MAX_PATH];
    char *p;
    if (!path || !path[0]) return 0;
    snprintf(tmp, sizeof tmp, "%s", path);
    for (p = tmp + 1; *p; p++) {
        if (*p != '\\' && *p != '/') continue;
        if (p > tmp && *(p - 1) == ':') continue;
        *p = '\0';
        ensure_dir_a(tmp);
        *p = '\\';
    }
    return ensure_dir_a(tmp);
}

static void post_log(const char *line) {
    char *copy;
    if (!g_hwnd || !line) return;
    copy = _strdup(line);
    if (copy) PostMessageW(g_hwnd, WM_APP_LOG, 0, (LPARAM)copy);
}

static void post_progress(int pct, const char *banner) {
    char *copy = banner ? _strdup(banner) : NULL;
    if (!g_hwnd) {
        free(copy);
        return;
    }
    PostMessageW(g_hwnd, WM_APP_PROGRESS, (WPARAM)pct, (LPARAM)copy);
}

static void append_log_ui(const char *line) {
    wchar_t wline[1024];
    wchar_t wbuf[1050];
    int len;
    if (!g_edit_log || !line) return;
    utf8_to_wide(line, wline, 1024);
    swprintf(wbuf, 1050, L"%s\r\n", wline);
    len = GetWindowTextLengthW(g_edit_log);
    SendMessageW(g_edit_log, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageW(g_edit_log, EM_REPLACESEL, FALSE, (LPARAM)wbuf);
    SendMessageW(g_edit_log, EM_SCROLLCARET, 0, 0);
}

static void resolve_source_root(char *out, size_t out_sz) {
    char exe[MAX_PATH];
    char dir[MAX_PATH];
    char probe[MAX_PATH];
    char *slash;
    GetModuleFileNameA(NULL, exe, sizeof exe);
    snprintf(dir, sizeof dir, "%s", exe);
    slash = strrchr(dir, '\\');
    if (slash) *slash = '\0';
    snprintf(probe, sizeof probe, "%s\\web\\index.html", dir);
    if (file_exists_a(probe)) {
        snprintf(out, out_sz, "%s", dir);
        return;
    }
    slash = strrchr(dir, '\\');
    if (slash) {
        *slash = '\0';
        snprintf(probe, sizeof probe, "%s\\web\\index.html", dir);
        if (file_exists_a(probe)) {
            snprintf(out, out_sz, "%s", dir);
            return;
        }
    }
    GetCurrentDirectoryA((DWORD)out_sz, out);
}

static void default_install_dir(char *out, size_t out_sz) {
    char localapp[MAX_PATH];
    if (GetEnvironmentVariableA("LOCALAPPDATA", localapp, sizeof localapp) > 0) {
        snprintf(out, out_sz, "%s\\Programs\\ClassicCars Studio", localapp);
    } else {
        snprintf(out, out_sz, "C:\\ClassicCars Studio");
    }
}

static int run_hidden_cmd(const char *cmdline, DWORD timeout_ms) {
    char mutable_cmd[2048];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    if (!cmdline || !cmdline[0]) return -1;
    snprintf(mutable_cmd, sizeof mutable_cmd, "%s", cmdline);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessA(NULL, mutable_cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, timeout_ms);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 ? 0 : -1;
}

static void kill_running_studio_processes(void) {
    post_log("Stopping any running ClassicCars Studio instances...");
    run_hidden_cmd("taskkill.exe /F /IM classiccars-studio.exe /T", 5000);
    Sleep(250);
}

static void prepare_overwrite_file(const char *dst_path) {
    char old_path[MAX_PATH];
    if (!dst_path || !file_exists_a(dst_path)) return;
    if (DeleteFileA(dst_path)) return;
    snprintf(old_path, sizeof old_path, "%s.old", dst_path);
    DeleteFileA(old_path);
    MoveFileExA(dst_path, old_path, MOVEFILE_REPLACE_EXISTING);
    DeleteFileA(old_path);
}

static int extract_zip_to_dir(const char *zip_path, const char *target_dir) {
    char cmd[2048];
    char probe[MAX_PATH];
    if (!ensure_tree_a(target_dir)) return -1;
    snprintf(probe, sizeof probe, "%s\\classiccars-studio.exe", target_dir);
    prepare_overwrite_file(probe);
    snprintf(cmd, sizeof cmd, "tar.exe -xf \"%s\" -C \"%s\"", zip_path, target_dir);
    if (run_hidden_cmd(cmd, 60000) == 0 && file_exists_a(probe)) return 0;
    snprintf(cmd, sizeof cmd,
             "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
             "\"Expand-Archive -LiteralPath '%s' -DestinationPath '%s' -Force\"",
             zip_path, target_dir);
    if (run_hidden_cmd(cmd, 60000) == 0 && file_exists_a(probe)) return 0;
    return -1;
}

/* Reads a self-contained zip payload appended to ClassicCarsStudio-Setup.exe
 * with trailer: 8-byte magic "CCSZIP01" + 8-byte little-endian uint64 size. */
static int extract_embedded_payload_to_dir(const char *target_dir) {
    char self_exe[MAX_PATH];
    char tmp_dir[MAX_PATH];
    char tmp_zip[MAX_PATH];
    char dst_setup[MAX_PATH];
    unsigned char footer[16];
    unsigned char buf[32768];
    uint64_t zip_size = 0;
    uint64_t remaining;
    long long total_size;
    FILE *in = NULL;
    FILE *out = NULL;
    int i;
    int rc = -1;

    if (GetModuleFileNameA(NULL, self_exe, sizeof self_exe) == 0) return -1;
    in = fopen(self_exe, "rb");
    if (!in) return -1;
    if (_fseeki64(in, 0, SEEK_END) != 0) {
        fclose(in);
        return -1;
    }
    total_size = _ftelli64(in);
    if (total_size <= 1024 + 16 || _fseeki64(in, total_size - 16, SEEK_SET) != 0 ||
        fread(footer, 1, 16, in) != 16 || memcmp(footer, "CCSZIP01", 8) != 0) {
        fclose(in);
        return -1;
    }
    for (i = 0; i < 8; i++) {
        zip_size |= ((uint64_t)footer[8 + i]) << (i * 8);
    }
    if (zip_size < 1024 || (long long)zip_size + 16 >= total_size) {
        fclose(in);
        return -1;
    }
    if (_fseeki64(in, total_size - 16 - (long long)zip_size, SEEK_SET) != 0) {
        fclose(in);
        return -1;
    }
    if (GetTempPathA(sizeof tmp_dir, tmp_dir) == 0) {
        fclose(in);
        return -1;
    }
    snprintf(tmp_zip, sizeof tmp_zip, "%sccs-setup-payload-%lu.zip", tmp_dir, (unsigned long)GetCurrentProcessId());
    out = fopen(tmp_zip, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    remaining = zip_size;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof buf ? sizeof buf : (size_t)remaining;
        if (fread(buf, 1, chunk, in) != chunk || fwrite(buf, 1, chunk, out) != chunk) {
            fclose(out);
            fclose(in);
            DeleteFileA(tmp_zip);
            return -1;
        }
        remaining -= chunk;
    }
    fclose(out);
    fclose(in);

    post_log("  Extracting self-contained application payload from Setup.exe...");
    rc = extract_zip_to_dir(tmp_zip, target_dir);
    DeleteFileA(tmp_zip);
    if (rc == 0) {
        snprintf(dst_setup, sizeof dst_setup, "%s\\ClassicCarsStudio-Setup.exe", target_dir);
        if (_stricmp(self_exe, dst_setup) != 0) {
            prepare_overwrite_file(dst_setup);
            CopyFileA(self_exe, dst_setup, FALSE);
        }
    }
    return rc;
}

static int copy_directory_recursive(const char *src_dir, const char *dst_dir, int *count_io) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    if (!ensure_tree_a(dst_dir)) return -1;
    snprintf(pattern, sizeof pattern, "%s\\*", src_dir);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    do {
        char s_item[MAX_PATH];
        char d_item[MAX_PATH];
        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0 ||
            strcmp(fd.cFileName, "__pycache__") == 0) {
            continue;
        }
        snprintf(s_item, sizeof s_item, "%s\\%s", src_dir, fd.cFileName);
        snprintf(d_item, sizeof d_item, "%s\\%s", dst_dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (copy_directory_recursive(s_item, d_item, count_io) != 0) {
                FindClose(h);
                return -1;
            }
        } else {
            if (!CopyFileA(s_item, d_item, FALSE)) {
                FindClose(h);
                return -1;
            }
            if (count_io) (*count_io)++;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return 0;
}

static int create_shortcut_lnk(const char *lnk_path, const char *target_exe, const char *work_dir) {
    IShellLinkW *psl = NULL;
    IPersistFile *ppf = NULL;
    wchar_t wtarget[MAX_PATH];
    wchar_t wwork[MAX_PATH];
    wchar_t wlnk[MAX_PATH];
    HRESULT hr;
    int ok = 0;

    utf8_to_wide(target_exe, wtarget, MAX_PATH);
    utf8_to_wide(work_dir, wwork, MAX_PATH);
    utf8_to_wide(lnk_path, wlnk, MAX_PATH);

    hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&psl);
    if (SUCCEEDED(hr) && psl) {
        IShellLinkW_SetPath(psl, wtarget);
        IShellLinkW_SetWorkingDirectory(psl, wwork);
        IShellLinkW_SetDescription(psl, L"Open ClassicCars Studio in its own window");
        IShellLinkW_SetIconLocation(psl, wtarget, 0);
        hr = IShellLinkW_QueryInterface(psl, &IID_IPersistFile, (void **)&ppf);
        if (SUCCEEDED(hr) && ppf) {
            if (SUCCEEDED(IPersistFile_Save(ppf, wlnk, TRUE))) ok = 1;
            IPersistFile_Release(ppf);
        }
        IShellLinkW_Release(psl);
    }
    return ok ? 0 : -1;
}

static void add_dir_to_user_path_reg(const char *dir_to_add) {
    HKEY hkey = NULL;
    char current[8192];
    char updated[8704];
    DWORD sz = sizeof current - 1;
    DWORD type = REG_EXPAND_SZ;
    const char *p;
    size_t dlen;
    if (!dir_to_add || !dir_to_add[0]) return;
    dlen = strlen(dir_to_add);
    while (dlen > 0 && (dir_to_add[dlen - 1] == '\\' || dir_to_add[dlen - 1] == '/')) dlen--;
    if (dlen == 0) return;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0, KEY_READ | KEY_WRITE, &hkey) != ERROR_SUCCESS) return;
    memset(current, 0, sizeof current);
    if (RegQueryValueExA(hkey, "Path", NULL, &type, (LPBYTE)current, &sz) != ERROR_SUCCESS) {
        current[0] = '\0';
        type = REG_EXPAND_SZ;
    }
    p = current;
    while (*p) {
        const char *semi = strchr(p, ';');
        size_t elen = semi ? (size_t)(semi - p) : strlen(p);
        while (elen > 0 && (p[elen - 1] == '\\' || p[elen - 1] == '/')) elen--;
        if (elen == dlen && _strnicmp(p, dir_to_add, dlen) == 0) {
            RegCloseKey(hkey);
            return;
        }
        if (!semi) break;
        p = semi + 1;
    }
    snprintf(updated, sizeof updated, "%s%s%.*s", current, current[0] ? ";" : "", (int)dlen, dir_to_add);
    RegSetValueExA(hkey, "Path", 0, type, (const BYTE *)updated, (DWORD)strlen(updated) + 1);
    RegCloseKey(hkey);
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0, (LPARAM)"Environment",
                        SMTO_ABORTIFHUNG, 1000, NULL);
}

static int run_winget_package(const char *pkg_id, const char *label) {
    char cmd[512];
    char msg[256];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    snprintf(msg, sizeof msg, "Running winget install for %s (%s)...", label, pkg_id);
    post_log(msg);
    snprintf(cmd, sizeof cmd,
             "winget install --id %s -e --accept-source-agreements --accept-package-agreements", pkg_id);
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        post_log("  ! Could not launch winget.exe (install manually or check App Installer).");
        return -1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0 ? 0 : -1;
}

static unsigned __stdcall prereq_worker(void *arg) {
    int install_missing = arg != NULL;
    StudioPreflightStatus st;
    studio_preflight_probe(&st);
    if (install_missing) {
        if (!st.php_ok) {
            post_progress(25, "Installing PHP Runtime via winget...");
            run_winget_package("PHP.PHP.8.3", "PHP 8.3 Runtime");
        }
        if (!st.git_ok) {
            post_progress(60, "Installing Git via winget...");
            run_winget_package("Git.Git", "Git Version Control");
        }
        if (!st.webview_ok && !st.browser_ok) {
            post_progress(80, "Installing Microsoft Edge WebView2 Runtime...");
            run_winget_package("Microsoft.EdgeWebView2Runtime", "Edge WebView2 Runtime");
        }
        studio_preflight_probe(&st);
    }
    g_preflight = st;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_PREREQ_DONE, (WPARAM)install_missing, 0);
    return 0;
}

static void upgrade_legacy_install_dirs(const char *installed_dir) {
    char localapp[MAX_PATH];
    char userprof[MAX_PATH];
    char candidates[5][MAX_PATH];
    const char *dirs[] = {"web", "preview", "assets"};
    int i, d;

    localapp[0] = userprof[0] = '\0';
    GetEnvironmentVariableA("LOCALAPPDATA", localapp, sizeof localapp);
    GetEnvironmentVariableA("USERPROFILE", userprof, sizeof userprof);

    for (i = 0; i < 5; i++) candidates[i][0] = '\0';
    if (localapp[0]) {
        snprintf(candidates[0], MAX_PATH, "%s\\Programs\\ClassicCars Studio", localapp);
        snprintf(candidates[1], MAX_PATH, "%s\\ClassicCarsStudio", localapp);
        snprintf(candidates[2], MAX_PATH, "%s\\ClassicCars Studio", localapp);
        snprintf(candidates[3], MAX_PATH, "%s\\opt\\ClassicCars-Studio", localapp);
    }
    if (userprof[0]) {
        snprintf(candidates[4], MAX_PATH, "%s\\.local\\opt\\ClassicCars-Studio", userprof);
    }

    for (i = 0; i < 5; i++) {
        char old_exe[MAX_PATH];
        char src_file[MAX_PATH];
        char dst_file[MAX_PATH];
        if (!candidates[i][0] || _stricmp(candidates[i], installed_dir) == 0) continue;
        snprintf(old_exe, sizeof old_exe, "%s\\classiccars-studio.exe", candidates[i]);
        if (!file_exists_a(old_exe)) continue;
        prepare_overwrite_file(old_exe);
        snprintf(src_file, sizeof src_file, "%s\\classiccars-studio.exe", installed_dir);
        CopyFileA(src_file, old_exe, FALSE);
        snprintf(src_file, sizeof src_file, "%s\\VERSION", installed_dir);
        snprintf(dst_file, sizeof dst_file, "%s\\VERSION", candidates[i]);
        CopyFileA(src_file, dst_file, FALSE);
        for (d = 0; d < 3; d++) {
            snprintf(src_file, sizeof src_file, "%s\\%s", installed_dir, dirs[d]);
            snprintf(dst_file, sizeof dst_file, "%s\\%s", candidates[i], dirs[d]);
            if (dir_exists_a(src_file)) copy_directory_recursive(src_file, dst_file, NULL);
        }
    }
}

static unsigned __stdcall install_worker(void *arg) {
    InstallOptions *opt = (InstallOptions *)arg;
    char msg[512];
    char path[MAX_PATH];
    char dst[MAX_PATH];
    int i;
    int extracted_embedded = 0;
    const char *root_files[] = {
        "classiccars-studio.exe",
        "ClassicCarsStudio-Setup.exe",
        "launcher.py",
        "launcher.sh",
        "launcher.bat",
        "launch-studio.sh",
        "VERSION",
        "README.md",
    };
    const char *dirs[] = {"web", "preview", "assets", "installers"};

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    post_progress(5, "Step 1/6: Checking system prerequisites...");
    post_log("=== Starting ClassicCars Studio v" STUDIO_VERSION " Installation ===");
    kill_running_studio_processes();
    studio_preflight_probe(&g_preflight);
    if (!g_preflight.php_ok && opt->auto_install_php) {
        post_progress(12, "Step 1/6: Installing missing PHP Runtime via winget...");
        run_winget_package("PHP.PHP.8.3", "PHP 8.3 Runtime");
        studio_preflight_probe(&g_preflight);
    }
    if (g_preflight.php_ok) {
        snprintf(msg, sizeof msg, "  [OK] PHP v%s (%s)", g_preflight.php_version, g_preflight.php_path);
        post_log(msg);
    } else {
        post_log("  [NOTE] PHP not found yet; Studio can install it on first launch or run in --no-php mode.");
    }

    post_progress(20, "Step 2/6: Preparing installation directory...");
    snprintf(msg, sizeof msg, "Creating target folder: %s", opt->target_dir);
    post_log(msg);
    if (!ensure_tree_a(opt->target_dir)) {
        post_log("ERROR: Could not create installation directory.");
        CoUninitialize();
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_INSTALL_DONE, 0, 0);
        free(opt);
        return 0;
    }
    snprintf(path, sizeof path, "%s\\state\\tmp-traffic", opt->target_dir);
    ensure_tree_a(path);
    post_log("  [OK] Preserved state\\ configuration directory.");

    post_progress(35, "Step 3/6: Installing ClassicCars Studio v" STUDIO_VERSION " files...");
    if (extract_embedded_payload_to_dir(opt->target_dir) == 0) {
        extracted_embedded = 1;
        post_progress(75, "Step 4/6: Extracted self-contained Studio v" STUDIO_VERSION " bundle.");
        post_log("  [OK] Unpacked self-contained ClassicCars Studio v" STUDIO_VERSION " payload.");
    }
    if (!extracted_embedded && _stricmp(opt->source_root, opt->target_dir) != 0) {
        for (i = 0; i < (int)(sizeof(root_files) / sizeof(root_files[0])); i++) {
            snprintf(path, sizeof path, "%s\\%s", opt->source_root, root_files[i]);
            snprintf(dst, sizeof dst, "%s\\%s", opt->target_dir, root_files[i]);
            if (file_exists_a(path)) {
                prepare_overwrite_file(dst);
                if (CopyFileA(path, dst, FALSE)) {
                    snprintf(msg, sizeof msg, "  Copied %s", root_files[i]);
                    post_log(msg);
                }
            }
        }
        for (i = 0; i < 4; i++) {
            int count = 0;
            int pct = 45 + i * 8;
            snprintf(msg, sizeof msg, "Step 4/6: Copying %s\\ directory...", dirs[i]);
            post_progress(pct, msg);
            snprintf(path, sizeof path, "%s\\%s", opt->source_root, dirs[i]);
            snprintf(dst, sizeof dst, "%s\\%s", opt->target_dir, dirs[i]);
            if (dir_exists_a(path)) {
                copy_directory_recursive(path, dst, &count);
                snprintf(msg, sizeof msg, "  Copied %s\\ (%d files)", dirs[i], count);
                post_log(msg);
            }
        }
    }

    snprintf(g_installed_exe, sizeof g_installed_exe, "%s\\classiccars-studio.exe", opt->target_dir);
    if (!file_exists_a(g_installed_exe)) {
        post_log("ERROR: classiccars-studio.exe was not found in the installer payload or source folder.");
        post_progress(0, "Installation failed: classiccars-studio.exe missing.");
        CoUninitialize();
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_INSTALL_DONE, 0, 0);
        free(opt);
        return 0;
    }
    upgrade_legacy_install_dirs(opt->target_dir);
    {
        FILE *fp;
        snprintf(dst, sizeof dst, "%s\\classiccars-studio.cmd", opt->target_dir);
        fp = fopen(dst, "wb");
        if (fp) {
            fprintf(fp, "@echo off\r\n\"%s\" %%*\r\n", g_installed_exe);
            fclose(fp);
        }
        snprintf(dst, sizeof dst, "%s\\uninstall.bat", opt->target_dir);
        fp = fopen(dst, "wb");
        if (fp) {
            fprintf(fp,
                    "@echo off\r\n"
                    "powershell -NoProfile -ExecutionPolicy Bypass -File \"%s\\installers\\install-windows.ps1\" -Uninstall -InstallDir \"%s\" %%*\r\n",
                    opt->target_dir, opt->target_dir);
            fclose(fp);
        }
    }

    post_progress(85, "Step 5/6: Creating Windows shortcuts...");
    if (opt->create_desktop) {
        char desktop[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
            snprintf(dst, sizeof dst, "%s\\ClassicCars Studio.lnk", desktop);
            if (create_shortcut_lnk(dst, g_installed_exe, opt->target_dir) == 0) {
                snprintf(msg, sizeof msg, "  [OK] Created Desktop shortcut: %s", dst);
                post_log(msg);
            }
        }
    }
    if (opt->create_startmenu) {
        char programs[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_PROGRAMS, NULL, 0, programs))) {
            snprintf(dst, sizeof dst, "%s\\ClassicCars Studio.lnk", programs);
            if (create_shortcut_lnk(dst, g_installed_exe, opt->target_dir) == 0) {
                snprintf(msg, sizeof msg, "  [OK] Created Start Menu shortcut: %s", dst);
                post_log(msg);
            }
        }
    }

    post_progress(95, "Step 6/6: Updating User PATH environment...");
    if (opt->update_path) {
        add_dir_to_user_path_reg(opt->target_dir);
        if (g_preflight.php_ok && g_preflight.php_path[0]) {
            char php_dir[MAX_PATH];
            char *sl;
            snprintf(php_dir, sizeof php_dir, "%s", g_preflight.php_path);
            sl = strrchr(php_dir, '\\');
            if (sl) {
                *sl = '\0';
                add_dir_to_user_path_reg(php_dir);
            }
        }
        post_log("  [OK] Updated User PATH in HKCU\\Environment.");
    }

    post_progress(100, "Installation Complete! ClassicCars Studio v" STUDIO_VERSION " is ready.");
    post_log("=== Installation Complete! ===");
    CoUninitialize();
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_INSTALL_DONE, 1, (LPARAM)opt->launch_after);
    free(opt);
    return 0;
}

static void refresh_prereq_display(void) {
    char text[1536];
    wchar_t wtext[1536];
    snprintf(
        text, sizeof text,
        "1. PHP Runtime:      %s%s%s%s\r\n"
        "2. Git CLI:          %s%s%s%s\r\n"
        "3. Display Engine:   %s\r\n"
        "4. Browser App Host: %s",
        g_preflight.php_ok ? "[OK] Found PHP v" : "[MISSING] Not found (check box below or click Install Prerequisites)",
        g_preflight.php_ok ? g_preflight.php_version : "",
        (g_preflight.php_ok && g_preflight.php_path[0]) ? " — " : "",
        (g_preflight.php_ok && g_preflight.php_path[0]) ? g_preflight.php_path : "",
        g_preflight.git_ok ? "[OK] Found Git v" : "[OPTIONAL] Not found (can be installed via winget)",
        g_preflight.git_ok ? g_preflight.git_version : "",
        (g_preflight.git_ok && g_preflight.git_path[0]) ? " — " : "",
        (g_preflight.git_ok && g_preflight.git_path[0]) ? g_preflight.git_path : "",
        g_preflight.webview_ok ? g_preflight.webview_info : "[FALLBACK] Will use standalone Browser --app window",
        g_preflight.browser_ok ? g_preflight.browser_info : "System default browser"
    );
    utf8_to_wide(text, wtext, 1536);
    if (g_edit_prereq) SetWindowTextW(g_edit_prereq, wtext);
}

static void set_controls_enabled(BOOL enabled) {
    EnableWindow(g_btn_recheck, enabled);
    EnableWindow(g_btn_deps, enabled);
    EnableWindow(g_edit_dir, enabled);
    EnableWindow(g_btn_browse, enabled);
    EnableWindow(g_chk_desktop, enabled);
    EnableWindow(g_chk_startmenu, enabled);
    EnableWindow(g_chk_userpath, enabled);
    EnableWindow(g_chk_autophp, enabled);
    EnableWindow(g_chk_launch, enabled);
    EnableWindow(g_btn_install, enabled);
    EnableWindow(g_btn_close, enabled);
}

static void launch_installed_studio(void) {
    wchar_t wexe[MAX_PATH];
    wchar_t wdir[MAX_PATH];
    wchar_t *sl;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    if (!g_installed_exe[0]) return;
    utf8_to_wide(g_installed_exe, wexe, MAX_PATH);
    wcsncpy(wdir, wexe, MAX_PATH - 1);
    wdir[MAX_PATH - 1] = L'\0';
    sl = wcsrchr(wdir, L'\\');
    if (sl) *sl = L'\0';
    memset(&si, 0, sizeof si);
    memset(&pi, 0, sizeof pi);
    si.cb = sizeof si;
    if (CreateProcessW(wexe, NULL, NULL, NULL, FALSE, 0, NULL, wdir, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        append_log_ui("Launched ClassicCars Studio.");
    } else {
        ShellExecuteW(g_hwnd, L"open", wexe, NULL, wdir, SW_SHOWNORMAL);
    }
}

static void browse_for_folder(HWND hwnd) {
    BROWSEINFOW bi;
    LPITEMIDLIST pidl;
    wchar_t picked[MAX_PATH];
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"Select the installation folder for ClassicCars Studio:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
    pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        if (SHGetPathFromIDListW(pidl, picked) && picked[0]) {
            wchar_t final_path[MAX_PATH];
            const wchar_t *leaf = wcsrchr(picked, L'\\');
            leaf = leaf ? leaf + 1 : picked;
            if (_wcsicmp(leaf, L"ClassicCars Studio") != 0 && _wcsicmp(leaf, L"ClassicCars_Studio") != 0) {
                swprintf(final_path, MAX_PATH, L"%s\\ClassicCars Studio", picked);
                SetWindowTextW(g_edit_dir, final_path);
            } else {
                SetWindowTextW(g_edit_dir, picked);
            }
        }
        CoTaskMemFree(pidl);
    }
}

static void start_install(void) {
    wchar_t wdir[MAX_PATH];
    InstallOptions *opt;
    if (InterlockedCompareExchange(&g_busy, 1, 0) != 0) return;
    opt = (InstallOptions *)calloc(1, sizeof *opt);
    if (!opt) {
        g_busy = 0;
        return;
    }
    GetWindowTextW(g_edit_dir, wdir, MAX_PATH);
    wide_to_utf8(wdir, opt->target_dir, sizeof opt->target_dir);
    if (!opt->target_dir[0]) default_install_dir(opt->target_dir, sizeof opt->target_dir);
    snprintf(opt->source_root, sizeof opt->source_root, "%s", g_source_root);
    opt->create_desktop = SendMessageW(g_chk_desktop, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt->create_startmenu = SendMessageW(g_chk_startmenu, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt->update_path = SendMessageW(g_chk_userpath, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt->auto_install_php = SendMessageW(g_chk_autophp, BM_GETCHECK, 0, 0) == BST_CHECKED;
    opt->launch_after = SendMessageW(g_chk_launch, BM_GETCHECK, 0, 0) == BST_CHECKED;
    set_controls_enabled(FALSE);
    SendMessageW(g_progress, PBM_SETPOS, 2, 0);
    if (_beginthreadex(NULL, 0, install_worker, opt, 0, NULL) == 0) {
        free(opt);
        g_busy = 0;
        set_controls_enabled(TRUE);
    }
}

static LRESULT CALLBACK setup_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
        InvalidateRect(hwnd, NULL, TRUE);
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wparam;
        if ((HWND)lparam == g_edit_prereq || (HWND)lparam == g_edit_log) {
            SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
            SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        SetTextColor(hdc, GetSysColor(COLOR_BTNTEXT));
        SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wparam;
        SetTextColor(hdc, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor(hdc, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_BTN_RECHECK:
            if (InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
                set_controls_enabled(FALSE);
                SetWindowTextW(g_lbl_status, L"Checking prerequisites (PHP, Git, WebView2, Browser)...");
                _beginthreadex(NULL, 0, prereq_worker, NULL, 0, NULL);
            }
            return 0;
        case ID_BTN_INSTALL_DEPS:
            if (InterlockedCompareExchange(&g_busy, 1, 0) == 0) {
                set_controls_enabled(FALSE);
                SetWindowTextW(g_lbl_status, L"Installing missing prerequisites via winget...");
                _beginthreadex(NULL, 0, prereq_worker, (void *)1, 0, NULL);
            }
            return 0;
        case ID_BTN_BROWSE:
            browse_for_folder(hwnd);
            return 0;
        case ID_BTN_INSTALL:
            if (g_install_finished) {
                launch_installed_studio();
            } else {
                start_install();
            }
            return 0;
        case ID_BTN_CLOSE:
        case IDCANCEL:
            if (!g_busy) DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_APP_LOG: {
        char *line = (char *)lparam;
        if (line) {
            append_log_ui(line);
            free(line);
        }
        return 0;
    }
    case WM_APP_PROGRESS: {
        char *banner = (char *)lparam;
        SendMessageW(g_progress, PBM_SETPOS, wparam, 0);
        if (banner) {
            wchar_t wbanner[512];
            utf8_to_wide(banner, wbanner, 512);
            SetWindowTextW(g_lbl_status, wbanner);
            free(banner);
        }
        return 0;
    }
    case WM_APP_PREREQ_DONE:
        refresh_prereq_display();
        g_busy = 0;
        set_controls_enabled(TRUE);
        SendMessageW(g_progress, PBM_SETPOS, 0, 0);
        SetWindowTextW(g_lbl_status,
                       wparam ? L"Prerequisite installation finished. Ready to install ClassicCars Studio."
                              : L"Prerequisite check complete. Ready to install ClassicCars Studio.");
        append_log_ui("Prerequisite check updated.");
        SetFocus(g_btn_install);
        return 0;
    case WM_APP_INSTALL_DONE:
        refresh_prereq_display();
        g_busy = 0;
        set_controls_enabled(TRUE);
        if (wparam) {
            g_install_finished = 1;
            SetWindowTextW(g_btn_install, L"&Launch ClassicCars Studio");
            SetWindowTextW(g_btn_close, L"&Close Setup");
            SetFocus(g_btn_install);
            if (lparam) launch_installed_studio();
        }
        return 0;
    case WM_CLOSE:
        if (!g_busy) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int main(int argc, char **argv) {
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    INITCOMMONCONTROLSEX icc;
    MSG msg;
    char def_dir[MAX_PATH];
    wchar_t wdef_dir[MAX_PATH];
    HWND h_title, h_sub, h_lbl_pre, h_lbl_dir, h_lbl_log;
    int win_w = 760;
    int win_h = 720;
    int sx, sy;
    (void)argc;
    (void)argv;

    SetProcessDPIAware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    memset(&icc, 0, sizeof icc);
    icc.dwSize = sizeof icc;
    icc.dwICC = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    resolve_source_root(g_source_root, sizeof g_source_root);
    default_install_dir(def_dir, sizeof def_dir);
    utf8_to_wide(def_dir, wdef_dir, MAX_PATH);
    studio_preflight_probe(&g_preflight);

    g_icon_big = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    g_icon_sm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);

    g_font_title = CreateFontW(-24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_bold = CreateFontW(-17, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_ui = CreateFontW(-17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_mono = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Consolas");

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = setup_wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ClassicCarsStudioSetupWnd";
    wc.hIcon = g_icon_big;
    wc.hIconSm = g_icon_sm ? g_icon_sm : g_icon_big;
    RegisterClassExW(&wc);

    sx = (GetSystemMetrics(SM_CXSCREEN) - win_w) / 2;
    sy = (GetSystemMetrics(SM_CYSCREEN) - win_h) / 2;
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;

    g_hwnd = CreateWindowExW(
        WS_EX_CONTROLPARENT | WS_EX_APPWINDOW,
        L"ClassicCarsStudioSetupWnd",
        L"ClassicCars Studio v" STUDIO_VERSION L" — Setup & Prerequisite Checker",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        sx, sy, win_w, win_h, NULL, NULL, inst, NULL
    );
    if (!g_hwnd) return 1;

    h_title = CreateWindowExW(0, L"STATIC", L"ClassicCars Studio v" STUDIO_VERSION L" Setup",
                              WS_CHILD | WS_VISIBLE, 24, 16, 700, 32, g_hwnd, NULL, inst, NULL);
    h_sub = CreateWindowExW(0, L"STATIC",
                            L"Accessible Windows Installer — High Contrast & Screen Reader Ready (Tab to navigate)",
                            WS_CHILD | WS_VISIBLE, 24, 50, 700, 24, g_hwnd, NULL, inst, NULL);

    h_lbl_pre = CreateWindowExW(0, L"STATIC", L"System Prerequisites (All-at-Once Check):",
                                WS_CHILD | WS_VISIBLE, 24, 82, 700, 22, g_hwnd, NULL, inst, NULL);
    g_edit_prereq = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY,
                                    24, 108, 696, 86, g_hwnd, (HMENU)(INT_PTR)ID_EDIT_PREREQ, inst, NULL);
    g_btn_recheck = CreateWindowExW(0, L"BUTTON", L"&Re-check Prerequisites",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    24, 200, 220, 34, g_hwnd, (HMENU)(INT_PTR)ID_BTN_RECHECK, inst, NULL);
    g_btn_deps = CreateWindowExW(0, L"BUTTON", L"Install &Missing Prerequisites (winget)",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 254, 200, 320, 34, g_hwnd, (HMENU)(INT_PTR)ID_BTN_INSTALL_DEPS, inst, NULL);

    h_lbl_dir = CreateWindowExW(0, L"STATIC", L"Installation Folder:",
                                WS_CHILD | WS_VISIBLE, 24, 244, 700, 22, g_hwnd, NULL, inst, NULL);
    g_edit_dir = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wdef_dir,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 24, 268, 576, 32, g_hwnd, (HMENU)(INT_PTR)ID_EDIT_DIR, inst, NULL);
    g_btn_browse = CreateWindowExW(0, L"BUTTON", L"&Browse...",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   610, 267, 110, 34, g_hwnd, (HMENU)(INT_PTR)ID_BTN_BROWSE, inst, NULL);

    g_chk_desktop = CreateWindowExW(0, L"BUTTON", L"Create &Desktop shortcut (ClassicCars Studio)",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                    24, 310, 696, 26, g_hwnd, (HMENU)(INT_PTR)ID_CHK_DESKTOP, inst, NULL);
    g_chk_startmenu = CreateWindowExW(0, L"BUTTON", L"Create &Start Menu shortcut",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                      24, 338, 696, 26, g_hwnd, (HMENU)(INT_PTR)ID_CHK_STARTMENU, inst, NULL);
    g_chk_userpath = CreateWindowExW(0, L"BUTTON", L"Add ClassicCars Studio (and discovered PHP/Git) to &User PATH",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     24, 366, 696, 26, g_hwnd, (HMENU)(INT_PTR)ID_CHK_USERPATH, inst, NULL);
    g_chk_autophp = CreateWindowExW(0, L"BUTTON", L"Automatically install &PHP via winget if missing during setup",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                    24, 394, 696, 26, g_hwnd, (HMENU)(INT_PTR)ID_CHK_AUTOPHP, inst, NULL);
    g_chk_launch = CreateWindowExW(0, L"BUTTON", L"&Launch ClassicCars Studio when installation finishes",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                   24, 422, 696, 26, g_hwnd, (HMENU)(INT_PTR)ID_CHK_LAUNCH, inst, NULL);

    SendMessageW(g_chk_desktop, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_startmenu, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_userpath, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_autophp, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g_chk_launch, BM_SETCHECK, BST_CHECKED, 0);

    g_lbl_status = CreateWindowExW(0, L"STATIC", L"Ready to install ClassicCars Studio v" STUDIO_VERSION L".",
                                   WS_CHILD | WS_VISIBLE, 24, 456, 696, 24, g_hwnd,
                                   (HMENU)(INT_PTR)ID_STATUS_BANNER, inst, NULL);
    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, NULL,
                                 WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                                 24, 482, 696, 24, g_hwnd, (HMENU)(INT_PTR)ID_PROGRESS, inst, NULL);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    h_lbl_log = CreateWindowExW(0, L"STATIC", L"Installation Progress Log:",
                                WS_CHILD | WS_VISIBLE, 24, 512, 696, 22, g_hwnd, NULL, inst, NULL);
    g_edit_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                 ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                 24, 536, 696, 86, g_hwnd, (HMENU)(INT_PTR)ID_EDIT_LOG, inst, NULL);

    g_btn_install = CreateWindowExW(0, L"BUTTON", L"&Install ClassicCars Studio",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                    320, 632, 260, 40, g_hwnd, (HMENU)(INT_PTR)ID_BTN_INSTALL, inst, NULL);
    g_btn_close = CreateWindowExW(0, L"BUTTON", L"&Cancel",
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                  592, 632, 128, 40, g_hwnd, (HMENU)(INT_PTR)ID_BTN_CLOSE, inst, NULL);

    SendMessageW(h_title, WM_SETFONT, (WPARAM)g_font_title, TRUE);
    SendMessageW(h_sub, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(h_lbl_pre, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    SendMessageW(g_edit_prereq, WM_SETFONT, (WPARAM)g_font_mono, TRUE);
    SendMessageW(g_btn_recheck, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_btn_deps, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(h_lbl_dir, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    SendMessageW(g_edit_dir, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_btn_browse, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_chk_desktop, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_chk_startmenu, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_chk_userpath, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_chk_autophp, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_chk_launch, WM_SETFONT, (WPARAM)g_font_ui, TRUE);
    SendMessageW(g_lbl_status, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    SendMessageW(h_lbl_log, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    SendMessageW(g_edit_log, WM_SETFONT, (WPARAM)g_font_mono, TRUE);
    SendMessageW(g_btn_install, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    SendMessageW(g_btn_close, WM_SETFONT, (WPARAM)g_font_ui, TRUE);

    refresh_prereq_display();
    append_log_ui("ClassicCars Studio Setup initialized. Review options above and click Install.");

    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetFocus(g_btn_install);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageW(g_hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_font_title) DeleteObject(g_font_title);
    if (g_font_bold) DeleteObject(g_font_bold);
    if (g_font_ui) DeleteObject(g_font_ui);
    if (g_font_mono) DeleteObject(g_font_mono);
    CoUninitialize();
    return 0;
}
