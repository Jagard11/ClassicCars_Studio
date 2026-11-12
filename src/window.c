#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif

#include "window.h"
#include "settings.h"
#include "http.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Last restored/normal geometry. Updated while the window is mapped and
   written to studio/state/window.json. Shared by the Win32 and GTK backends. */
static char g_repo_root[PATH_MAX];
static StudioWindowState g_geom;
static int g_geom_ready = 0;

static void geometry_defaults(StudioWindowState *st) {
    st->has_pos = 0;
    st->x = 0;
    st->y = 0;
    st->width = 1440;
    st->height = 900;
    st->maximized = 0;
}

static int geometry_same(const StudioWindowState *a, const StudioWindowState *b) {
    return a->has_pos == b->has_pos && a->x == b->x && a->y == b->y && a->width == b->width &&
           a->height == b->height && a->maximized == b->maximized;
}

static void remember_repo(const char *repo_root) {
    g_repo_root[0] = '\0';
    if (!repo_root || !repo_root[0]) return;
    snprintf(g_repo_root, sizeof g_repo_root, "%s", repo_root);
}

static void load_window_geometry(const char *repo_root) {
    geometry_defaults(&g_geom);
    remember_repo(repo_root);
    g_geom_ready = 0;
    if (!g_repo_root[0]) return;
    if (settings_read_window_state(g_repo_root, &g_geom) != 0) geometry_defaults(&g_geom);
    g_geom_ready = 1;
}

static void store_window_geometry(void) {
    if (!g_geom_ready || !g_repo_root[0]) return;
    settings_write_window_state(g_repo_root, &g_geom);
}

#ifdef _WIN32

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <winreg.h>

/* Native Win32 window with an embedded WebView2 control.
 * WebView2Loader.dll is used when it sits beside the exe. Otherwise the
 * Evergreen runtime's EmbeddedBrowserWebView.dll is loaded directly
 * (checking EdgeWebView\Application, EdgeCore, and Edge\Application).
 * If WebView2 still cannot be embedded, Studio automatically launches a
 * standalone Edge/Chrome --app=<url> window and turns this window into an
 * accessible server controller. */

#define ID_BTN_OPEN_APP 1001
#define ID_BTN_STOP_SRV 1002
#define ID_TIMER_APP_PROC 2001

static HWND g_hwnd = NULL;
static HWND g_fallback = NULL;
static HWND g_btn_open = NULL;
static HWND g_btn_quit = NULL;
static void *g_controller = NULL;
static void *g_environment = NULL;
static HICON g_icon = NULL;
static HICON g_icon_sm = NULL;
static int g_icon_owned = 0;
static HFONT g_font = NULL;
static int g_com_ready = 0;
static volatile LONG g_closed = 0;
static int g_app_launched = 0;
static HANDLE g_app_proc = NULL;
static ULONGLONG g_app_start_tick = 0;
static wchar_t g_url_w[2048];

static const GUID STUDIO_IID_IUNKNOWN =
    {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID STUDIO_IID_ENV_HANDLER =
    {0x4e8a3389, 0xc9d8, 0x4bd2, {0xb6, 0xb5, 0x12, 0x4f, 0xee, 0x6c, 0xc1, 0x4d}};
static const GUID STUDIO_IID_CTL_HANDLER =
    {0x6c4819f3, 0xc9b7, 0x4260, {0x81, 0x27, 0xc9, 0xf5, 0xbd, 0xe7, 0xf6, 0x8c}};
static const GUID STUDIO_IID_TITLE_HANDLER =
    {0xf5f2b923, 0x953e, 0x4042, {0x9f, 0x95, 0xf3, 0xa1, 0x18, 0xe1, 0xaf, 0xd4}};

typedef struct StudioToken {
    long long value;
} StudioToken;

typedef struct ComHandler {
    const void *lpVtbl;
    LONG ref;
} ComHandler;

typedef HRESULT(STDMETHODCALLTYPE *HandlerQI)(void *self, const GUID *iid, void **out);
typedef ULONG(STDMETHODCALLTYPE *HandlerCount)(void *self);
typedef HRESULT(STDMETHODCALLTYPE *EnvInvoke)(void *self, HRESULT hr, void *result);
typedef HRESULT(STDMETHODCALLTYPE *TitleInvoke)(void *self, void *sender, void *args);

typedef struct EnvVtbl {
    HandlerQI QueryInterface;
    HandlerCount AddRef;
    HandlerCount Release;
    EnvInvoke Invoke;
} EnvVtbl;

typedef struct TitleVtbl {
    HandlerQI QueryInterface;
    HandlerCount AddRef;
    HandlerCount Release;
    TitleInvoke Invoke;
} TitleVtbl;

static void utf8_to_wide(const char *text, wchar_t *out, int out_cch) {
    if (!out || out_cch <= 0) return;
    out[0] = L'\0';
    if (!text) return;
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, out, out_cch) <= 0) out[0] = L'\0';
    out[out_cch - 1] = L'\0';
}

static void *dll_proc(HMODULE module, const char *name) {
    FARPROC proc = module ? GetProcAddress(module, name) : NULL;
    void *fn = NULL;
    if (!proc) return NULL;
    memcpy(&fn, &proc, sizeof fn);
    return fn;
}

static void *vt_slot(void *object, int index) {
    void **table;
    if (!object) return NULL;
    table = *(void ***)object;
    return table ? table[index] : NULL;
}

static ULONG com_release(void *object) {
    typedef ULONG(STDMETHODCALLTYPE *RelFn)(void *);
    RelFn rel;
    if (!object) return 0;
    rel = (RelFn)vt_slot(object, 2);
    return rel ? rel(object) : 0;
}

static int wide_append(wchar_t *dst, size_t dst_cch, const wchar_t *src) {
    size_t have;
    size_t add;
    if (!dst || dst_cch == 0) return -1;
    have = wcslen(dst);
    add = src ? wcslen(src) : 0;
    if (have + add + 1 > dst_cch) return -1;
    if (add) memcpy(dst + have, src, (add + 1) * sizeof(wchar_t));
    return 0;
}

static void layout_fallback(HWND hwnd) {
    RECT rc;
    int width;
    int height;
    int btn_y;
    if (!g_fallback) return;
    GetClientRect(hwnd, &rc);
    width = rc.right - 32;
    btn_y = rc.bottom - 56;
    if (btn_y < 80) btn_y = 80;
    height = btn_y - 28;
    if (width < 32) width = 32;
    if (height < 32) height = 32;
    MoveWindow(g_fallback, 16, 16, width, height, TRUE);
    if (g_btn_open) MoveWindow(g_btn_open, 16, btn_y, 220, 38, TRUE);
    if (g_btn_quit) MoveWindow(g_btn_quit, 248, btn_y, 200, 38, TRUE);
}

static void launch_standalone_app_window(void);

static void show_fallback(HRESULT hr) {
    wchar_t text[1600];
    wchar_t code[40];
    HINSTANCE inst;
    if (g_closed || !g_hwnd || g_controller) return;
    if (!g_app_launched) {
        g_app_launched = 1;
        launch_standalone_app_window();
    }
    if (g_fallback) return;
    text[0] = L'\0';
    code[0] = L'\0';
    swprintf(code, sizeof code / sizeof code[0], L"0x%08lX", (unsigned long)hr);
    wide_append(text, sizeof text / sizeof text[0],
                L"ClassicCars Studio is running in Standalone App Window mode.\r\n\r\n"
                L"Studio URL: ");
    wide_append(text, sizeof text / sizeof text[0], g_url_w);
    wide_append(text, sizeof text / sizeof text[0],
                L"\r\n\r\n"
                L"ClassicCars Studio automatically opened in a standalone Microsoft Edge / Chrome app window "
                L"(embedded WebView2 status: ");
    wide_append(text, sizeof text / sizeof text[0], code);
    wide_append(text, sizeof text / sizeof text[0],
                L").\r\n\r\n"
                L"Use the buttons below (or Tab + Enter) to re-open the Studio window or stop the server.");
    inst = GetModuleHandleW(NULL);
    g_fallback = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                                 16, 16, 560, 180, g_hwnd, NULL, inst, NULL);
    g_btn_open = CreateWindowExW(0, L"BUTTON", L"&Open Studio Window",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 16, 210, 220, 38, g_hwnd, (HMENU)(INT_PTR)ID_BTN_OPEN_APP, inst, NULL);
    g_btn_quit = CreateWindowExW(0, L"BUTTON", L"&Stop Server && Quit",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 248, 210, 200, 38, g_hwnd, (HMENU)(INT_PTR)ID_BTN_STOP_SRV, inst, NULL);
    if (g_fallback && g_font) SendMessageW(g_fallback, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_btn_open && g_font) SendMessageW(g_btn_open, WM_SETFONT, (WPARAM)g_font, TRUE);
    if (g_btn_quit && g_font) SendMessageW(g_btn_quit, WM_SETFONT, (WPARAM)g_font, TRUE);
    layout_fallback(g_hwnd);
    ShowWindow(g_hwnd, SW_MINIMIZE);
}

static void resize_controller(HWND hwnd) {
    typedef HRESULT(STDMETHODCALLTYPE *PutBoundsFn)(void *, RECT);
    PutBoundsFn put_bounds;
    RECT rc;
    if (!g_controller || !hwnd) return;
    GetClientRect(hwnd, &rc);
    put_bounds = (PutBoundsFn)vt_slot(g_controller, 6);
    if (put_bounds) put_bounds(g_controller, rc);
}

static HRESULT STDMETHODCALLTYPE title_qi(void *self, const GUID *iid, void **out) {
    ComHandler *handler = self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid || (!IsEqualGUID(iid, &STUDIO_IID_IUNKNOWN) && !IsEqualGUID(iid, &STUDIO_IID_TITLE_HANDLER))) {
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&handler->ref);
    *out = self;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE title_add(void *self) {
    return (ULONG)InterlockedIncrement(&((ComHandler *)self)->ref);
}

static ULONG STDMETHODCALLTYPE title_release(void *self) {
    ComHandler *handler = self;
    LONG left = InterlockedDecrement(&handler->ref);
    if (left == 0) free(handler);
    return (ULONG)left;
}

static HRESULT STDMETHODCALLTYPE title_invoke(void *self, void *sender, void *args) {
    typedef HRESULT(STDMETHODCALLTYPE *GetTitleFn)(void *, wchar_t **);
    GetTitleFn get_title;
    wchar_t *title = NULL;
    (void)self;
    (void)args;
    if (g_closed || !g_hwnd || !sender) return S_OK;
    get_title = (GetTitleFn)vt_slot(sender, 48);
    if (!get_title || FAILED(get_title(sender, &title))) return S_OK;
    if (title && title[0]) SetWindowTextW(g_hwnd, title);
    if (title) CoTaskMemFree(title);
    return S_OK;
}

static const TitleVtbl TITLE_VTBL = {title_qi, title_add, title_release, title_invoke};

static void *new_title_handler(void) {
    ComHandler *handler = (ComHandler *)calloc(1, sizeof *handler);
    if (!handler) return NULL;
    handler->lpVtbl = &TITLE_VTBL;
    handler->ref = 1;
    return handler;
}

static void adopt_webview(void *webview) {
    typedef HRESULT(STDMETHODCALLTYPE *GetSettingsFn)(void *, void **);
    typedef HRESULT(STDMETHODCALLTYPE *PutBoolFn)(void *, BOOL);
    typedef HRESULT(STDMETHODCALLTYPE *NavigateFn)(void *, const wchar_t *);
    typedef HRESULT(STDMETHODCALLTYPE *AddTitleFn)(void *, void *, StudioToken *);
    GetSettingsFn get_settings;
    NavigateFn navigate;
    AddTitleFn add_title;
    void *settings = NULL;
    void *title_handler;
    StudioToken token;
    if (!webview) return;
    get_settings = (GetSettingsFn)vt_slot(webview, 3);
    if (get_settings && SUCCEEDED(get_settings(webview, &settings)) && settings) {
        PutBoolFn put_devtools = (PutBoolFn)vt_slot(settings, 12);
        if (put_devtools) put_devtools(settings, TRUE);
        com_release(settings);
    }
    title_handler = new_title_handler();
    add_title = (AddTitleFn)vt_slot(webview, 46);
    token.value = 0;
    if (title_handler && add_title) add_title(webview, title_handler, &token);
    if (title_handler) title_release(title_handler);
    navigate = (NavigateFn)vt_slot(webview, 5);
    if (navigate) navigate(webview, g_url_w);
}

static HRESULT STDMETHODCALLTYPE ctl_qi(void *self, const GUID *iid, void **out) {
    ComHandler *handler = self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid || (!IsEqualGUID(iid, &STUDIO_IID_IUNKNOWN) && !IsEqualGUID(iid, &STUDIO_IID_CTL_HANDLER))) {
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&handler->ref);
    *out = self;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ctl_add(void *self) {
    return (ULONG)InterlockedIncrement(&((ComHandler *)self)->ref);
}

static ULONG STDMETHODCALLTYPE ctl_release(void *self) {
    ComHandler *handler = self;
    LONG left = InterlockedDecrement(&handler->ref);
    if (left == 0) free(handler);
    return (ULONG)left;
}

static HRESULT STDMETHODCALLTYPE ctl_invoke(void *self, HRESULT hr, void *controller) {
    typedef HRESULT(STDMETHODCALLTYPE *PutBoolFn)(void *, BOOL);
    typedef HRESULT(STDMETHODCALLTYPE *GetWebViewFn)(void *, void **);
    PutBoolFn put_visible;
    GetWebViewFn get_webview;
    void *webview = NULL;
    (void)self;
    if (g_closed) {
        if (controller) com_release(controller);
        return S_OK;
    }
    if (FAILED(hr) || !controller) {
        show_fallback(FAILED(hr) ? hr : E_FAIL);
        return S_OK;
    }
    g_controller = controller;
    put_visible = (PutBoolFn)vt_slot(controller, 4);
    if (put_visible) put_visible(controller, TRUE);
    resize_controller(g_hwnd);
    get_webview = (GetWebViewFn)vt_slot(controller, 25);
    if (!get_webview || FAILED(get_webview(controller, &webview)) || !webview) {
        show_fallback(E_FAIL);
        return S_OK;
    }
    adopt_webview(webview);
    com_release(webview);
    if (g_fallback) {
        DestroyWindow(g_fallback);
        g_fallback = NULL;
    }
    if (g_btn_open) {
        DestroyWindow(g_btn_open);
        g_btn_open = NULL;
    }
    if (g_btn_quit) {
        DestroyWindow(g_btn_quit);
        g_btn_quit = NULL;
    }
    return S_OK;
}

static const EnvVtbl CTL_VTBL = {ctl_qi, ctl_add, ctl_release, ctl_invoke};

static void *new_controller_handler(void) {
    ComHandler *handler = (ComHandler *)calloc(1, sizeof *handler);
    if (!handler) return NULL;
    handler->lpVtbl = &CTL_VTBL;
    handler->ref = 1;
    return handler;
}

static HRESULT STDMETHODCALLTYPE env_qi(void *self, const GUID *iid, void **out) {
    ComHandler *handler = self;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid || (!IsEqualGUID(iid, &STUDIO_IID_IUNKNOWN) && !IsEqualGUID(iid, &STUDIO_IID_ENV_HANDLER))) {
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&handler->ref);
    *out = self;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE env_add(void *self) {
    return (ULONG)InterlockedIncrement(&((ComHandler *)self)->ref);
}

static ULONG STDMETHODCALLTYPE env_release(void *self) {
    ComHandler *handler = self;
    LONG left = InterlockedDecrement(&handler->ref);
    if (left == 0) free(handler);
    return (ULONG)left;
}

static HRESULT STDMETHODCALLTYPE env_invoke(void *self, HRESULT hr, void *environment) {
    typedef HRESULT(STDMETHODCALLTYPE *CreateCtlFn)(void *, HWND, void *);
    CreateCtlFn create;
    void *handler;
    HRESULT rc;
    (void)self;
    if (g_closed) {
        if (environment) com_release(environment);
        return S_OK;
    }
    if (FAILED(hr) || !environment || !g_hwnd) {
        if (environment) com_release(environment);
        show_fallback(FAILED(hr) ? hr : E_FAIL);
        return S_OK;
    }
    g_environment = environment;
    create = (CreateCtlFn)vt_slot(environment, 3);
    handler = new_controller_handler();
    if (!create || !handler) {
        if (handler) ctl_release(handler);
        show_fallback(E_FAIL);
        return S_OK;
    }
    rc = create(environment, g_hwnd, handler);
    ctl_release(handler);
    if (FAILED(rc)) show_fallback(rc);
    return S_OK;
}

static const EnvVtbl ENV_VTBL = {env_qi, env_add, env_release, env_invoke};

static int ensure_dir(const wchar_t *path) {
    if (!path || !path[0]) return 0;
    if (CreateDirectoryW(path, NULL)) return 1;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static int ensure_tree(wchar_t *path) {
    wchar_t *cursor;
    if (!path || !path[0]) return 0;
    for (cursor = path + 1; *cursor; cursor++) {
        if (*cursor != L'\\' && *cursor != L'/') continue;
        *cursor = L'\0';
        if (path[0] && !ensure_dir(path) && GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
            *cursor = L'\\';
            return 0;
        }
        *cursor = L'\\';
    }
    return ensure_dir(path);
}

static void user_data_dir(wchar_t *out, size_t out_cch) {
    wchar_t root[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", root, MAX_PATH);
    out[0] = L'\0';
    if (n == 0 || n >= MAX_PATH) {
        n = GetTempPathW(MAX_PATH, root);
        if (n == 0 || n >= MAX_PATH) return;
    }
    if (swprintf(out, out_cch, L"%s%sClassicCarsStudio\\WebView2", root,
                 (root[0] && root[wcslen(root) - 1] == L'\\') ? L"" : L"\\") < 0) {
        out[0] = L'\0';
    }
}

static int file_exists_w(const wchar_t *path) {
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int reg_str_w(HKEY root, const wchar_t *subkey, const wchar_t *val, wchar_t *out, DWORD out_cch) {
    DWORD bytes = out_cch * sizeof(wchar_t);
    if (RegGetValueW(root, subkey, val, RRF_RT_REG_SZ, NULL, out, &bytes) != ERROR_SUCCESS) return 0;
    out[out_cch - 1] = L'\0';
    return out[0] != L'\0';
}

static int reg_pv(HKEY root, const wchar_t *subkey, wchar_t *out, DWORD out_cch) {
    if (!reg_str_w(root, subkey, L"pv", out, out_cch)) return 0;
    if (wcscmp(out, L"0.0.0.0") == 0) return 0;
    return 1;
}

static int probe_dll_in_folder(const wchar_t *folder, wchar_t *out, size_t out_cch) {
    static const wchar_t *suffixes[] = {
#if defined(_WIN64)
        L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll",
        L"\\x64\\EmbeddedBrowserWebView.dll",
        L"\\EBWebView\\x86\\EmbeddedBrowserWebView.dll",
#else
        L"\\EBWebView\\x86\\EmbeddedBrowserWebView.dll",
        L"\\x86\\EmbeddedBrowserWebView.dll",
        L"\\EBWebView\\x64\\EmbeddedBrowserWebView.dll",
#endif
        L"\\EmbeddedBrowserWebView.dll",
    };
    size_t i;
    if (!folder || !folder[0]) return 0;
    if (file_exists_w(folder)) {
        wcsncpy(out, folder, out_cch - 1);
        out[out_cch - 1] = L'\0';
        return 1;
    }
    for (i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        if (swprintf(out, out_cch, L"%s%s", folder, suffixes[i]) > 0 && file_exists_w(out)) return 1;
    }
    out[0] = L'\0';
    return 0;
}

static int dll_from_version_dir(const wchar_t *application_dir, const wchar_t *version, wchar_t *out, size_t out_cch);
static void consider_application_dir(const wchar_t *application_dir, wchar_t *best, size_t best_cch,
                                     wchar_t *best_version, size_t best_version_cch);

static int find_embedded_dll(wchar_t *out, size_t out_cch) {
    static const wchar_t *client_keys[] = {
        L"Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"Software\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}",
        L"Software\\WOW6432Node\\Microsoft\\EdgeUpdate\\Clients\\{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}",
        L"Software\\Microsoft\\EdgeUpdate\\Clients\\{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}",
    };
    static const wchar_t *app_subdirs[] = {
        L"\\Microsoft\\EdgeWebView\\Application",
        L"\\Microsoft\\EdgeCore",
        L"\\Microsoft\\Edge\\Application",
    };
    const HKEY hkeys[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    wchar_t version[64];
    wchar_t best_version[64];
    wchar_t folder[MAX_PATH];
    wchar_t roots[3][MAX_PATH];
    wchar_t application[MAX_PATH];
    DWORD n;
    size_t h, k, r, s;

    out[0] = L'\0';
    n = GetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", folder, MAX_PATH);
    if (n > 0 && n < MAX_PATH && probe_dll_in_folder(folder, out, out_cch)) return 1;

    /* 1. Direct EBWebView / location registry paths on Windows 10 & Windows 11 */
    for (h = 0; h < 2; h++) {
        for (k = 0; k < sizeof(client_keys) / sizeof(client_keys[0]); k++) {
            if (reg_str_w(hkeys[h], client_keys[k], L"EBWebView", folder, MAX_PATH) &&
                probe_dll_in_folder(folder, out, out_cch)) {
                return 1;
            }
            if (reg_str_w(hkeys[h], client_keys[k], L"location", folder, MAX_PATH) &&
                probe_dll_in_folder(folder, out, out_cch)) {
                return 1;
            }
        }
    }

    roots[0][0] = roots[1][0] = roots[2][0] = L'\0';
    GetEnvironmentVariableW(L"ProgramFiles(x86)", roots[0], MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles", roots[1], MAX_PATH);
    GetEnvironmentVariableW(L"LOCALAPPDATA", roots[2], MAX_PATH);

    /* 2. Check registered pv versions across EdgeWebView\Application, EdgeCore, Edge\Application */
    for (h = 0; h < 2; h++) {
        for (k = 0; k < sizeof(client_keys) / sizeof(client_keys[0]); k++) {
            if (!reg_pv(hkeys[h], client_keys[k], version, 64)) continue;
            for (r = 0; r < 3; r++) {
                if (!roots[r][0]) continue;
                for (s = 0; s < sizeof(app_subdirs) / sizeof(app_subdirs[0]); s++) {
                    if (swprintf(application, MAX_PATH, L"%s%s", roots[r], app_subdirs[s]) < 0) continue;
                    if (dll_from_version_dir(application, version, out, out_cch)) return 1;
                }
            }
        }
    }

    /* 3. Scan version folders across all three directories */
    out[0] = L'\0';
    best_version[0] = L'\0';
    for (r = 0; r < 3; r++) {
        if (!roots[r][0]) continue;
        for (s = 0; s < sizeof(app_subdirs) / sizeof(app_subdirs[0]); s++) {
            if (swprintf(application, MAX_PATH, L"%s%s", roots[r], app_subdirs[s]) < 0) continue;
            consider_application_dir(application, out, out_cch, best_version, 64);
        }
    }
    return out[0] != L'\0';
}

static void maybe_pin_webview_folder_from_dll(const wchar_t *dll_path) {
    wchar_t existing[MAX_PATH];
    wchar_t dir[MAX_PATH];
    wchar_t exe[MAX_PATH];
    wchar_t *eb;
    if (!dll_path || !dll_path[0]) return;
    if (GetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", existing, MAX_PATH) > 0) return;
    wcsncpy(dir, dll_path, MAX_PATH - 1);
    dir[MAX_PATH - 1] = L'\0';
    eb = wcsstr(dir, L"\\EBWebView\\");
    if (!eb) return;
    *eb = L'\0';
    if (swprintf(exe, MAX_PATH, L"%s\\msedgewebview2.exe", dir) > 0 && file_exists_w(exe)) {
        SetEnvironmentVariableW(L"WEBVIEW2_BROWSER_EXECUTABLE_FOLDER", dir);
    }
}

static int find_browser_exe(wchar_t *out, size_t out_cch) {
    static const wchar_t *app_paths[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe",
    };
    static const wchar_t *rel_paths[] = {
        L"\\Microsoft\\Edge\\Application\\msedge.exe",
        L"\\Google\\Chrome\\Application\\chrome.exe",
    };
    const HKEY hkeys[] = {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER};
    wchar_t roots[3][MAX_PATH];
    size_t i, h, r;

    for (i = 0; i < 2; i++) {
        for (h = 0; h < 2; h++) {
            if (reg_str_w(hkeys[h], app_paths[i], NULL, out, (DWORD)out_cch) && file_exists_w(out)) return 1;
        }
    }
    roots[0][0] = roots[1][0] = roots[2][0] = L'\0';
    GetEnvironmentVariableW(L"ProgramFiles(x86)", roots[0], MAX_PATH);
    GetEnvironmentVariableW(L"ProgramFiles", roots[1], MAX_PATH);
    GetEnvironmentVariableW(L"LOCALAPPDATA", roots[2], MAX_PATH);
    for (i = 0; i < 2; i++) {
        for (r = 0; r < 3; r++) {
            if (!roots[r][0]) continue;
            if (swprintf(out, out_cch, L"%s%s", roots[r], rel_paths[i]) > 0 && file_exists_w(out)) return 1;
        }
    }
    out[0] = L'\0';
    return 0;
}

static void launch_standalone_app_window(void) {
    wchar_t browser[MAX_PATH];
    wchar_t profile_dir[MAX_PATH];
    wchar_t root[MAX_PATH];
    wchar_t cmdline[3072];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD n;
    if (!g_url_w[0]) return;
    if (find_browser_exe(browser, MAX_PATH)) {
        profile_dir[0] = L'\0';
        n = GetEnvironmentVariableW(L"LOCALAPPDATA", root, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            swprintf(profile_dir, MAX_PATH, L"%s\\ClassicCarsStudio\\AppWindow", root);
            ensure_tree(profile_dir);
        }
        if (profile_dir[0]) {
            swprintf(cmdline, sizeof cmdline / sizeof cmdline[0],
                     L"\"%s\" --app=%s --new-window --user-data-dir=\"%s\"",
                     browser, g_url_w, profile_dir);
        } else {
            swprintf(cmdline, sizeof cmdline / sizeof cmdline[0],
                     L"\"%s\" --app=%s --new-window", browser, g_url_w);
        }
        memset(&si, 0, sizeof si);
        memset(&pi, 0, sizeof pi);
        si.cb = sizeof si;
        if (CreateProcessW(browser, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            if (g_app_proc) CloseHandle(g_app_proc);
            g_app_proc = pi.hProcess;
            g_app_start_tick = GetTickCount64();
            if (g_hwnd) SetTimer(g_hwnd, ID_TIMER_APP_PROC, 600, NULL);
            return;
        }
    }
    ShellExecuteW(NULL, L"open", g_url_w, NULL, NULL, SW_SHOWNORMAL);
}

static int dll_from_version_dir(const wchar_t *application_dir, const wchar_t *version, wchar_t *out, size_t out_cch) {
    if (swprintf(out, out_cch, L"%s\\%s\\EBWebView\\"
#if defined(_WIN64)
                              L"x64"
#else
                              L"x86"
#endif
                              L"\\EmbeddedBrowserWebView.dll",
                 application_dir, version) < 0) {
        return 0;
    }
    return file_exists_w(out);
}

static int version_cmp(const wchar_t *a, const wchar_t *b) {
    while ((a && *a) || (b && *b)) {
        unsigned long av = 0;
        unsigned long bv = 0;
        while (a && *a && *a != L'.') {
            if (*a >= L'0' && *a <= L'9') av = av * 10u + (unsigned long)(*a - L'0');
            a++;
        }
        while (b && *b && *b != L'.') {
            if (*b >= L'0' && *b <= L'9') bv = bv * 10u + (unsigned long)(*b - L'0');
            b++;
        }
        if (av != bv) return av < bv ? -1 : 1;
        if (a && *a == L'.') a++;
        if (b && *b == L'.') b++;
    }
    return 0;
}

static void consider_application_dir(const wchar_t *application_dir, wchar_t *best, size_t best_cch,
                                    wchar_t *best_version, size_t best_version_cch) {
    wchar_t pattern[MAX_PATH];
    wchar_t candidate[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    if (!application_dir || !application_dir[0] || !best_version || best_version_cch == 0) return;
    if (swprintf(pattern, MAX_PATH, L"%s\\*", application_dir) < 0) return;
    find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        if (!dll_from_version_dir(application_dir, data.cFileName, candidate, MAX_PATH)) continue;
        if (!best[0] || version_cmp(data.cFileName, best_version) > 0) {
            wcsncpy(best, candidate, best_cch - 1);
            best[best_cch - 1] = L'\0';
            wcsncpy(best_version, data.cFileName, best_version_cch - 1);
            best_version[best_version_cch - 1] = L'\0';
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

typedef HRESULT(STDMETHODCALLTYPE *CreateEnvWithOptionsFn)(const wchar_t *, const wchar_t *, void *, void *);
typedef HRESULT(STDMETHODCALLTYPE *CreateEnvInternalFn)(int, int, const wchar_t *, void *, void *);

static int start_webview(void) {
    wchar_t data_dir[MAX_PATH];
    wchar_t dll_path[MAX_PATH];
    wchar_t loader_path[MAX_PATH];
    HMODULE loader = NULL;
    HMODULE embedded = NULL;
    void *handler;
    HRESULT hr = E_FAIL;
    HRESULT com;
    DWORD n;
    wchar_t *slash;
    user_data_dir(data_dir, MAX_PATH);
    if (!data_dir[0] || !ensure_tree(data_dir)) {
        show_fallback(E_FAIL);
        return 0;
    }
    com = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(com)) g_com_ready = 1;
    else if (com != RPC_E_CHANGED_MODE) {
        show_fallback(com);
        return 0;
    }
    handler = calloc(1, sizeof(ComHandler));
    if (!handler) {
        show_fallback(E_OUTOFMEMORY);
        return 0;
    }
    ((ComHandler *)handler)->lpVtbl = &ENV_VTBL;
    ((ComHandler *)handler)->ref = 1;
    n = GetModuleFileNameW(NULL, loader_path, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        slash = wcsrchr(loader_path, L'\\');
        if (slash && (size_t)(slash - loader_path) + 22 < MAX_PATH) {
            wcscpy(slash + 1, L"WebView2Loader.dll");
            loader = LoadLibraryW(loader_path);
        }
    }
    if (!loader) loader = LoadLibraryW(L"WebView2Loader.dll");
    if (loader) {
        CreateEnvWithOptionsFn create =
            (CreateEnvWithOptionsFn)dll_proc(loader, "CreateCoreWebView2EnvironmentWithOptions");
        if (create) hr = create(NULL, data_dir, NULL, handler);
    }
    if (!loader || FAILED(hr)) {
        if (find_embedded_dll(dll_path, MAX_PATH)) {
            CreateEnvInternalFn create_internal;
            maybe_pin_webview_folder_from_dll(dll_path);
            embedded = LoadLibraryExW(dll_path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
            create_internal = embedded
                                  ? (CreateEnvInternalFn)dll_proc(embedded, "CreateWebViewEnvironmentWithOptionsInternal")
                                  : NULL;
            if (create_internal) {
                /* bool checkRunningInstance is the low byte of the first
                 * argument. runtimeType 0 selects the evergreen runtime. */
                hr = create_internal(1, 0, data_dir, NULL, handler);
            }
        }
    }
    env_release(handler);
    if (FAILED(hr)) {
        show_fallback(hr);
        return 0;
    }
    return 1;
}

static HICON icon_from_png(const char *path) {
    typedef int(WINAPI *GpStartupFn)(ULONG_PTR *, const void *, void *);
    typedef void(WINAPI *GpShutdownFn)(ULONG_PTR);
    typedef int(WINAPI *GpBitmapFn)(const wchar_t *, void **);
    typedef int(WINAPI *GpIconFn)(void *, HICON *);
    typedef int(WINAPI *GpDisposeFn)(void *);
    struct GpInput {
        UINT32 GdiplusVersion;
        void *DebugEventCallback;
        BOOL SuppressBackgroundThread;
        BOOL SuppressExternalCodecs;
    } input;
    HMODULE gdip;
    GpStartupFn startup;
    GpShutdownFn shutdown;
    GpBitmapFn from_file;
    GpIconFn to_icon;
    GpDisposeFn dispose;
    ULONG_PTR token = 0;
    void *bitmap = NULL;
    HICON icon = NULL;
    wchar_t wide[MAX_PATH];
    if (!path || !path[0]) return NULL;
    utf8_to_wide(path, wide, MAX_PATH);
    if (!wide[0] || !file_exists_w(wide)) return NULL;
    gdip = LoadLibraryW(L"gdiplus.dll");
    if (!gdip) return NULL;
    startup = (GpStartupFn)dll_proc(gdip, "GdiplusStartup");
    shutdown = (GpShutdownFn)dll_proc(gdip, "GdiplusShutdown");
    from_file = (GpBitmapFn)dll_proc(gdip, "GdipCreateBitmapFromFile");
    to_icon = (GpIconFn)dll_proc(gdip, "GdipCreateHICONFromBitmap");
    dispose = (GpDisposeFn)dll_proc(gdip, "GdipDisposeImage");
    memset(&input, 0, sizeof input);
    input.GdiplusVersion = 1;
    if (!startup || !from_file || !to_icon || !dispose || startup(&token, &input, NULL) != 0) {
        FreeLibrary(gdip);
        return NULL;
    }
    if (from_file(wide, &bitmap) == 0 && bitmap && to_icon(bitmap, &icon) != 0) icon = NULL;
    if (bitmap) dispose(bitmap);
    if (shutdown) shutdown(token);
    FreeLibrary(gdip);
    return icon;
}

static void release_webview(void) {
    typedef HRESULT(STDMETHODCALLTYPE *CloseFn)(void *);
    CloseFn close_fn;
    if (g_controller) {
        close_fn = (CloseFn)vt_slot(g_controller, 24);
        if (close_fn) close_fn(g_controller);
        com_release(g_controller);
        g_controller = NULL;
    }
    if (g_environment) {
        com_release(g_environment);
        g_environment = NULL;
    }
}

/* rcNormalPosition is the restored rect even while maximized or minimized. */
static void persist_placement(HWND hwnd, int force) {
    WINDOWPLACEMENT wp;
    StudioWindowState next;
    int width;
    int height;
    if (!g_geom_ready || !g_repo_root[0] || !hwnd || g_closed) return;
    if (!force && !IsWindowVisible(hwnd)) return;
    memset(&wp, 0, sizeof wp);
    wp.length = sizeof wp;
    if (!GetWindowPlacement(hwnd, &wp)) return;
    width = (int)(wp.rcNormalPosition.right - wp.rcNormalPosition.left);
    height = (int)(wp.rcNormalPosition.bottom - wp.rcNormalPosition.top);
    if (width < 960 || height < 600) return;
    if (width > 7680) width = 7680;
    if (height > 4320) height = 4320;
    if (wp.rcNormalPosition.left < -30000 || wp.rcNormalPosition.top < -30000 ||
        wp.rcNormalPosition.left > 30000 || wp.rcNormalPosition.top > 30000) {
        return;
    }
    next = g_geom;
    next.has_pos = 1;
    next.x = (int)wp.rcNormalPosition.left;
    next.y = (int)wp.rcNormalPosition.top;
    next.width = width;
    next.height = height;
    next.maximized = IsZoomed(hwnd) ? 1 : 0;
    if (geometry_same(&next, &g_geom)) return;
    g_geom = next;
    store_window_geometry();
}

static int placement_on_virtual_screen(int x, int y, int width, int height) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    long long left = x;
    long long top = y;
    long long right = left + (long long)width;
    long long bottom = top + (long long)height;
    long long vright;
    long long vbottom;
    if (vw <= 0 || vh <= 0 || width <= 0 || height <= 0) return 0;
    vright = (long long)vx + (long long)vw;
    vbottom = (long long)vy + (long long)vh;
    if (right <= vx || left >= vright || bottom <= vy || top >= vbottom) return 0;
    return 1;
}

static LRESULT CALLBACK studio_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_MOVE:
        persist_placement(hwnd, 0);
        return 0;
    case WM_SIZE:
        persist_placement(hwnd, 0);
        resize_controller(hwnd);
        layout_fallback(hwnd);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == ID_BTN_OPEN_APP) {
            launch_standalone_app_window();
            return 0;
        }
        if (LOWORD(wparam) == ID_BTN_STOP_SRV) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == ID_TIMER_APP_PROC && g_app_proc) {
            if (WaitForSingleObject(g_app_proc, 0) == WAIT_OBJECT_0) {
                ULONGLONG elapsed = GetTickCount64() - g_app_start_tick;
                KillTimer(hwnd, ID_TIMER_APP_PROC);
                CloseHandle(g_app_proc);
                g_app_proc = NULL;
                /* If the standalone --app process ran for >= 3s and then closed,
                 * the user closed the Studio app window; stop the server cleanly. */
                if (elapsed >= 3000) {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
            }
            return 0;
        }
        break;
    case WM_CLOSE:
        /* Placement is still valid here. WM_DESTROY runs after the window is gone. */
        persist_placement(hwnd, 1);
        store_window_geometry();
        DestroyWindow(hwnd);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        if (info) {
            info->ptMinTrackSize.x = 640;
            info->ptMinTrackSize.y = 320;
        }
        return 0;
    }
    case WM_DESTROY:
        g_closed = 1;
        if (g_app_proc) {
            CloseHandle(g_app_proc);
            g_app_proc = NULL;
        }
        release_webview();
        http_server_request_stop();
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int studio_window_run(const char *url, const char *initial_title, const char *icon_path,
                      const char *repo_root) {
    WNDCLASSEXW wc;
    HWND hwnd;
    MSG msg;
    wchar_t title[512];
    int pos_x = CW_USEDEFAULT;
    int pos_y = CW_USEDEFAULT;
    HINSTANCE instance = GetModuleHandleW(NULL);
    g_hwnd = NULL;
    g_fallback = NULL;
    g_btn_open = NULL;
    g_btn_quit = NULL;
    g_controller = NULL;
    g_environment = NULL;
    g_icon = NULL;
    g_icon_sm = NULL;
    g_icon_owned = 0;
    g_font = NULL;
    g_com_ready = 0;
    g_closed = 0;
    g_app_launched = 0;
    g_app_proc = NULL;
    g_app_start_tick = 0;
    g_url_w[0] = L'\0';
    g_geom_ready = 0;
    if (!url || !url[0]) return -1;
    SetProcessDPIAware();
    load_window_geometry(repo_root);
    if (g_geom.has_pos && placement_on_virtual_screen(g_geom.x, g_geom.y, g_geom.width, g_geom.height)) {
        pos_x = g_geom.x;
        pos_y = g_geom.y;
    }
    utf8_to_wide(url, g_url_w, (int)(sizeof g_url_w / sizeof g_url_w[0]));
    utf8_to_wide(initial_title ? initial_title : "ClassicCars Studio", title, (int)(sizeof title / sizeof title[0]));
    if (!title[0]) wcscpy(title, L"ClassicCars Studio");
    g_icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_SHARED);
    g_icon_sm = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!g_icon) {
        g_icon = icon_from_png(icon_path);
        if (g_icon) g_icon_owned = 1;
    }
    if (!g_icon_sm) g_icon_sm = g_icon;
    g_font = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = studio_wndproc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"ClassicCarsStudioWindow";
    wc.hIcon = g_icon;
    wc.hIconSm = g_icon_sm;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return -1;
    hwnd = CreateWindowExW(0, L"ClassicCarsStudioWindow", title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, pos_x,
                           pos_y, g_geom.width, g_geom.height, NULL, NULL, instance, NULL);
    if (!hwnd) {
        if (g_icon && g_icon_owned) DestroyIcon(g_icon);
        if (g_font) DeleteObject(g_font);
        g_icon = NULL;
        g_icon_sm = NULL;
        g_font = NULL;
        return -1;
    }
    g_hwnd = hwnd;
    ShowWindow(hwnd, g_geom.maximized ? SW_SHOWMAXIMIZED : SW_SHOW);
    UpdateWindow(hwnd);
    persist_placement(hwnd, 1);
    start_webview();
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (g_fallback && IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_closed = 1;
    release_webview();
    if (g_icon && g_icon_owned) DestroyIcon(g_icon);
    if (g_font) DeleteObject(g_font);
    g_icon = NULL;
    g_icon_sm = NULL;
    g_font = NULL;
    g_hwnd = NULL;
    if (g_com_ready) CoUninitialize();
    g_com_ready = 0;
    return 0;
}

void studio_window_request_close(void) {
    HWND hwnd = g_hwnd;
    http_server_request_stop();
    if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

#else

#include <dlfcn.h>
#include <signal.h>

/* GTK 3 + WebKitGTK, loaded at runtime so the binary does not need the -dev
 * packages. The HTTP server keeps running on another thread; closing this
 * window stops it. */

typedef int (*GtkInitCheckFn)(int *, char ***);
typedef void *(*GtkWindowNewFn)(int);
typedef void (*GtkSetTitleFn)(void *, const char *);
typedef void (*GtkSetSizeFn)(void *, int, int);
typedef int (*GtkSetIconFn)(void *, const char *, void **);
typedef void (*GtkSizeRequestFn)(void *, int, int);
typedef void (*GtkContainerAddFn)(void *, void *);
typedef void (*GtkShowAllFn)(void *);
typedef void (*GtkMainFn)(void);
typedef void (*GtkQuitFn)(void);
typedef void (*GtkDestroyFn)(void *);
typedef void (*GtkGrabFocusFn)(void *);
typedef void (*GtkSetExpandFn)(void *, int);
typedef void *(*WebViewNewFn)(void);
typedef void *(*WebViewSettingsFn)(void *);
typedef void (*WebViewSetBoolFn)(void *, int);
typedef void (*WebViewLoadFn)(void *, const char *);
typedef const char *(*WebViewTitleFn)(void *);
typedef void (*VoidFn)(void);
typedef unsigned long (*SignalConnectFn)(void *, const char *, VoidFn, void *, void *, unsigned);
typedef int (*SourceFn)(void *);
typedef unsigned int (*TimeoutAddFn)(unsigned int, SourceFn, void *);
typedef int (*XInitThreadsFn)(void);
typedef void (*GtkMoveFn)(void *, int, int);
typedef void (*GtkGetPosFn)(void *, int *, int *);
typedef void (*GtkGetSizeFn)(void *, int *, int *);
typedef void (*GtkMaximizeFn)(void *);
typedef int (*GtkIsMaximizedFn)(void *);
typedef int (*GtkWidgetFlagFn)(void *);

static GtkSetTitleFn g_set_title = NULL;
static WebViewTitleFn g_get_title = NULL;
static GtkQuitFn g_quit = NULL;
static GtkDestroyFn g_destroy = NULL;
static GtkMoveFn g_window_move = NULL;
static GtkGetPosFn g_window_get_position = NULL;
static GtkGetSizeFn g_window_get_size = NULL;
static GtkMaximizeFn g_window_maximize = NULL;
static GtkIsMaximizedFn g_window_is_maximized = NULL;
static GtkWidgetFlagFn g_widget_get_mapped = NULL;
static GtkWidgetFlagFn g_widget_get_visible = NULL;
typedef int (*HitTestEditableFn)(void *);
static HitTestEditableFn g_hit_test_is_editable = NULL;
static void *g_window = NULL;
static volatile sig_atomic_t g_leave = 0;
static int g_left = 0;
static int g_hold_restore = 0;
static int g_in_capture = 0;
static int g_restore_x = 0;
static int g_restore_y = 0;
static int g_restore_pos = 0;
static int g_restore_max = 0;
static int g_did_map_place = 0;
/* Last normal (unmaximized) rect. A maximize transition emits configure-event
   before gtk_window_is_maximized flips, so the in-flight position is kept
   pending until it stays put or the window is actually maximized. */
static StudioWindowState g_committed;
static int g_have_committed = 0;
static StudioWindowState g_pending;
static int g_pending_valid = 0;
static int g_pending_polls = 0;
static int g_seen_maximized = 0;
static int g_max_wait = 0;

static void publish_geometry(const StudioWindowState *next, int force) {
    if (!force && geometry_same(next, &g_geom)) return;
    g_geom = *next;
    store_window_geometry();
}

static int window_is_maximized(void *window) {
    if (g_window_is_maximized) return g_window_is_maximized(window) ? 1 : 0;
    /* GTK builds before 3.12 have no getter. Honor the restored flag. */
    return g_restore_max ? 1 : 0;
}

static int read_normal_rect(void *window, StudioWindowState *out) {
    int x = g_geom.x;
    int y = g_geom.y;
    int w = g_geom.width;
    int h = g_geom.height;
    if (!g_window_get_position || !g_window_get_size) return 0;
    g_window_get_position(window, &x, &y);
    g_window_get_size(window, &w, &h);
    if (w < 960 || h < 600) return 0;
    if (w > 7680) w = 7680;
    if (h > 4320) h = 4320;
    out->has_pos = 1;
    out->x = x;
    out->y = y;
    out->width = w;
    out->height = h;
    out->maximized = 0;
    return 1;
}

static void capture_geometry(void *window, int force) {
    StudioWindowState next;
    int maximized;
    if (g_in_capture || !window || !g_geom_ready || (g_hold_restore && !force)) return;
    if (!force) {
        if (g_widget_get_mapped && !g_widget_get_mapped(window)) return;
        if (g_widget_get_visible && !g_widget_get_visible(window)) return;
    }
    g_in_capture = 1;
    maximized = window_is_maximized(window);
    if (maximized) {
        /* Drop the transitional rect captured before the maximized bit flipped. */
        g_seen_maximized = 1;
        g_pending_valid = 0;
        next = g_have_committed ? g_committed : g_geom;
        next.maximized = 1;
        publish_geometry(&next, force);
        g_in_capture = 0;
        return;
    }
    if (g_restore_max && !g_seen_maximized) {
        /* Opening straight into maximize reports the monitor origin before
           gtk_window_is_maximized flips. Keep the loaded normal rect. */
        if (force) {
            next = g_have_committed ? g_committed : g_geom;
            next.maximized = 1;
            publish_geometry(&next, 1);
        }
        g_in_capture = 0;
        return;
    }
    if (!read_normal_rect(window, &next)) {
        g_in_capture = 0;
        return;
    }
    if (force) {
        /* delete-event / quit: the window is still mapped and not maximized. */
        g_committed = next;
        g_have_committed = 1;
        g_pending_valid = 0;
        publish_geometry(&next, 1);
        g_in_capture = 0;
        return;
    }
    /* One configure during maximize reports the monitor origin before
       is_maximized becomes true again. Keep that sample pending so the
       following maximized event can discard it. */
    if (!g_pending_valid || !geometry_same(&next, &g_pending)) {
        g_pending = next;
        g_pending_valid = 1;
        g_pending_polls = 0;
    }
    g_in_capture = 0;
}

static void commit_pending_if_stable(void) {
    if (!g_pending_valid || g_hold_restore || !g_geom_ready) return;
    g_pending_polls++;
    if (g_pending_polls < 2) return;
    g_committed = g_pending;
    g_have_committed = 1;
    g_pending_valid = 0;
    publish_geometry(&g_committed, 0);
}

static void place_saved_window(void *window, int mapped) {
    if (!window) return;
    /* Moving a window that is already maximized makes some window managers
       drop the maximized state. Apply the restored origin before map, and
       again after map only while it is a normal window. */
    if (g_restore_pos && g_window_move && !(mapped && g_restore_max)) {
        g_window_move(window, g_restore_x, g_restore_y);
    }
    if (g_restore_max && g_window_maximize) g_window_maximize(window);
}

static void request_leave(void) {
    void *window;
    if (g_left) return;
    g_left = 1;
    /* Query only while the GdkWindow is still mapped. destroy is too late:
       gtk_window_get_position/get_size then report 0,0 or a stale size. */
    if (g_window) capture_geometry(g_window, 1);
    store_window_geometry();
    http_server_request_stop();
    window = g_window;
    g_window = NULL;
    if (window && g_destroy) g_destroy(window);
    if (g_quit) g_quit();
}

static void on_window_closed(void *widget, void *data) {
    (void)widget;
    (void)data;
    g_window = NULL;
    request_leave();
}

static void on_webview_title_changed(void *webview, void *pspec, void *window) {
    const char *title;
    (void)pspec;
    if (!window || !g_get_title || !g_set_title) return;
    title = g_get_title(webview);
    if (title && title[0]) g_set_title(window, title);
}

static int on_geometry_event(void *widget, void *event, void *data) {
    (void)event;
    (void)data;
    capture_geometry(widget, 0);
    return 0;
}

static int on_map_event(void *widget, void *event, void *data) {
    (void)event;
    (void)data;
    if (g_did_map_place) return 0;
    g_did_map_place = 1;
    place_saved_window(widget, 1);
    return 0;
}

/* TRUE skips WebKit's native menu. Its Reload item navigates the Studio
   shell (/__studio/). Editable fields keep Cut, Copy, and Paste. */
static int on_webview_context_menu(void *webview, void *menu, void *event, void *hit, void *data) {
    (void)webview;
    (void)menu;
    (void)event;
    (void)data;
    if (hit && g_hit_test_is_editable && g_hit_test_is_editable(hit)) return 0;
    return 1;
}

static int on_delete_event(void *widget, void *event, void *data) {
    (void)event;
    (void)data;
    /* delete-event runs before the window is unmapped. Save the live
       placement here, then let GTK destroy the window and quit. */
    g_hold_restore = 0;
    capture_geometry(widget, 1);
    store_window_geometry();
    return 0;
}

static int on_poll_signals(void *data) {
    (void)data;
    if (g_window) {
        capture_geometry(g_window, 0);
        if (g_restore_max && !g_seen_maximized) {
            g_max_wait++;
            if (g_max_wait >= 5) g_restore_max = 0;
        }
        commit_pending_if_stable();
    }
    if (!g_leave) return 1;
    request_leave();
    return 0;
}

static void on_posix_signal(int sig) {
    (void)sig;
    g_leave = 1;
    http_server_request_stop();
}

static void install_window_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_posix_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);
}

static void *must_symbol(void *handle, const char *name) {
    void *symbol = dlsym(handle, name);
    if (!symbol) fprintf(stderr, "classiccars-studio: missing symbol %s\n", name);
    return symbol;
}

int studio_window_run(const char *url, const char *initial_title, const char *icon_path,
                      const char *repo_root) {
    void *glib = NULL;
    void *gobject = NULL;
    void *gtk = NULL;
    void *webkit = NULL;
    void *x11;
    GtkInitCheckFn gtk_init_check;
    GtkWindowNewFn gtk_window_new;
    GtkSetSizeFn gtk_window_set_default_size;
    GtkSetIconFn gtk_window_set_icon_from_file;
    GtkSizeRequestFn gtk_widget_set_size_request;
    GtkContainerAddFn gtk_container_add;
    GtkShowAllFn gtk_widget_show_all;
    GtkMainFn gtk_main;
    WebViewNewFn webkit_web_view_new;
    WebViewSettingsFn webkit_web_view_get_settings;
    WebViewSetBoolFn set_developer_extras;
    WebViewSetBoolFn set_smooth_scrolling;
    WebViewLoadFn webkit_web_view_load_uri;
    SignalConnectFn g_signal_connect_data;
    TimeoutAddFn g_timeout_add;
    GtkSetExpandFn set_hexpand;
    GtkSetExpandFn set_vexpand;
    GtkGrabFocusFn grab_focus;
    void *webview;
    void *settings;
    int argc = 1;
    char arg0[] = "classiccars-studio";
    char *argv_store[] = {arg0, NULL};
    char **argv = argv_store;
    const char *webkit_names[] = {"libwebkit2gtk-4.1.so.0", "libwebkit2gtk-4.0.so.37"};
    char first_error[256];
    int i;

    g_set_title = NULL;
    g_get_title = NULL;
    g_quit = NULL;
    g_destroy = NULL;
    g_window_move = NULL;
    g_window_get_position = NULL;
    g_window_get_size = NULL;
    g_window_maximize = NULL;
    g_window_is_maximized = NULL;
    g_widget_get_mapped = NULL;
    g_widget_get_visible = NULL;
    g_hit_test_is_editable = NULL;
    g_window = NULL;
    g_leave = 0;
    g_left = 0;
    g_hold_restore = 1;
    g_in_capture = 0;
    g_restore_x = 0;
    g_restore_y = 0;
    g_restore_pos = 0;
    g_restore_max = 0;
    g_did_map_place = 0;
    g_have_committed = 0;
    g_pending_valid = 0;
    g_pending_polls = 0;
    g_seen_maximized = 0;
    g_max_wait = 0;
    g_geom_ready = 0;
    if (!url || !url[0]) return -1;
    if (!initial_title || !initial_title[0]) initial_title = "ClassicCars Studio";
    load_window_geometry(repo_root);
    g_committed = g_geom;
    g_have_committed = 1;
    g_restore_x = g_geom.x;
    g_restore_y = g_geom.y;
    g_restore_pos = g_geom.has_pos;
    g_restore_max = g_geom.maximized;
    install_window_signals();

    x11 = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (x11) {
        XInitThreadsFn init_threads = (XInitThreadsFn)dlsym(x11, "XInitThreads");
        if (init_threads) init_threads();
    }

    glib = dlopen("libglib-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    gobject = dlopen("libgobject-2.0.so.0", RTLD_LAZY | RTLD_LOCAL);
    gtk = dlopen("libgtk-3.so.0", RTLD_LAZY | RTLD_LOCAL);
    if (!glib || !gobject || !gtk) {
        const char *err = dlerror();
        fprintf(stderr, "classiccars-studio: could not load GTK 3 (%s)\n", err ? err : "library missing");
        return -1;
    }
    /* Threaded compositing paints overflow:auto as tiles and leaves the
       panel background in tiles that are not ready during a fast scroll.
       Set before WebKit is loaded so the web process inherits it. 0 keeps
       an explicit environment override. Smooth scrolling stays on; without
       the tiled compositor it repaints on the main thread. */
    setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 0);
    first_error[0] = '\0';
    for (i = 0; i < 2 && !webkit; i++) {
        webkit = dlopen(webkit_names[i], RTLD_LAZY | RTLD_LOCAL);
        if (!webkit && i == 0) {
            const char *err = dlerror();
            snprintf(first_error, sizeof first_error, "%s", err ? err : "dlopen failed");
        }
    }
    if (!webkit) {
        fprintf(stderr, "classiccars-studio: %s: %s\n", webkit_names[0], first_error[0] ? first_error : "dlopen failed");
        {
            const char *err = dlerror();
            fprintf(stderr, "classiccars-studio: %s: %s\n", webkit_names[1], err ? err : "dlopen failed");
        }
        return -1;
    }

    gtk_init_check = (GtkInitCheckFn)must_symbol(gtk, "gtk_init_check");
    gtk_window_new = (GtkWindowNewFn)must_symbol(gtk, "gtk_window_new");
    g_set_title = (GtkSetTitleFn)must_symbol(gtk, "gtk_window_set_title");
    gtk_window_set_default_size = (GtkSetSizeFn)must_symbol(gtk, "gtk_window_set_default_size");
    gtk_widget_set_size_request = (GtkSizeRequestFn)must_symbol(gtk, "gtk_widget_set_size_request");
    gtk_container_add = (GtkContainerAddFn)must_symbol(gtk, "gtk_container_add");
    gtk_widget_show_all = (GtkShowAllFn)must_symbol(gtk, "gtk_widget_show_all");
    gtk_main = (GtkMainFn)must_symbol(gtk, "gtk_main");
    g_quit = (GtkQuitFn)must_symbol(gtk, "gtk_main_quit");
    g_destroy = (GtkDestroyFn)must_symbol(gtk, "gtk_widget_destroy");
    g_signal_connect_data = (SignalConnectFn)must_symbol(gobject, "g_signal_connect_data");
    g_timeout_add = (TimeoutAddFn)must_symbol(glib, "g_timeout_add");
    webkit_web_view_new = (WebViewNewFn)must_symbol(webkit, "webkit_web_view_new");
    webkit_web_view_get_settings = (WebViewSettingsFn)must_symbol(webkit, "webkit_web_view_get_settings");
    webkit_web_view_load_uri = (WebViewLoadFn)must_symbol(webkit, "webkit_web_view_load_uri");
    g_get_title = (WebViewTitleFn)must_symbol(webkit, "webkit_web_view_get_title");
    if (!gtk_init_check || !gtk_window_new || !g_set_title || !gtk_window_set_default_size ||
        !gtk_widget_set_size_request || !gtk_container_add || !gtk_widget_show_all || !gtk_main || !g_quit ||
        !g_destroy || !g_signal_connect_data || !g_timeout_add || !webkit_web_view_new ||
        !webkit_web_view_get_settings || !webkit_web_view_load_uri || !g_get_title) {
        return -1;
    }
    gtk_window_set_icon_from_file = (GtkSetIconFn)dlsym(gtk, "gtk_window_set_icon_from_file");
    g_window_move = (GtkMoveFn)dlsym(gtk, "gtk_window_move");
    g_window_get_position = (GtkGetPosFn)dlsym(gtk, "gtk_window_get_position");
    g_window_get_size = (GtkGetSizeFn)dlsym(gtk, "gtk_window_get_size");
    g_window_maximize = (GtkMaximizeFn)dlsym(gtk, "gtk_window_maximize");
    g_window_is_maximized = (GtkIsMaximizedFn)dlsym(gtk, "gtk_window_is_maximized");
    g_widget_get_mapped = (GtkWidgetFlagFn)dlsym(gtk, "gtk_widget_get_mapped");
    g_widget_get_visible = (GtkWidgetFlagFn)dlsym(gtk, "gtk_widget_get_visible");
    set_hexpand = (GtkSetExpandFn)dlsym(gtk, "gtk_widget_set_hexpand");
    set_vexpand = (GtkSetExpandFn)dlsym(gtk, "gtk_widget_set_vexpand");
    grab_focus = (GtkGrabFocusFn)dlsym(gtk, "gtk_widget_grab_focus");
    g_hit_test_is_editable = (HitTestEditableFn)dlsym(webkit, "webkit_hit_test_result_context_is_editable");
    set_developer_extras = (WebViewSetBoolFn)dlsym(webkit, "webkit_settings_set_enable_developer_extras");
    set_smooth_scrolling = (WebViewSetBoolFn)dlsym(webkit, "webkit_settings_set_enable_smooth_scrolling");

    if (!gtk_init_check(&argc, &argv)) {
        fprintf(stderr, "classiccars-studio: GTK could not open a display\n");
        return -1;
    }

    g_window = gtk_window_new(0); /* GTK_WINDOW_TOPLEVEL */
    if (!g_window) return -1;
    g_set_title(g_window, initial_title);
    gtk_window_set_default_size(g_window, g_geom.width, g_geom.height);
    gtk_widget_set_size_request(g_window, 960, 600);
    place_saved_window(g_window, 0);
    if (icon_path && icon_path[0] && gtk_window_set_icon_from_file) {
        gtk_window_set_icon_from_file(g_window, icon_path, NULL);
    }
    webview = webkit_web_view_new();
    if (!webview) return -1;
    settings = webkit_web_view_get_settings(webview);
    if (settings && set_developer_extras) set_developer_extras(settings, 1);
    if (settings && set_smooth_scrolling) set_smooth_scrolling(settings, 1);
    if (set_hexpand) set_hexpand(webview, 1);
    if (set_vexpand) set_vexpand(webview, 1);
    gtk_container_add(g_window, webview);
    g_signal_connect_data(g_window, "configure-event", (VoidFn)on_geometry_event, NULL, NULL, 0);
    g_signal_connect_data(g_window, "window-state-event", (VoidFn)on_geometry_event, NULL, NULL, 0);
    g_signal_connect_data(g_window, "map-event", (VoidFn)on_map_event, NULL, NULL, 0);
    g_signal_connect_data(g_window, "delete-event", (VoidFn)on_delete_event, NULL, NULL, 0);
    g_signal_connect_data(g_window, "destroy", (VoidFn)on_window_closed, NULL, NULL, 0);
    g_signal_connect_data(webview, "notify::title", (VoidFn)on_webview_title_changed, g_window, NULL, 0);
    g_signal_connect_data(webview, "context-menu", (VoidFn)on_webview_context_menu, NULL, NULL, 0);
    webkit_web_view_load_uri(webview, url);
    g_timeout_add(100, on_poll_signals, NULL);
    gtk_widget_show_all(g_window);
    place_saved_window(g_window, 1);
    g_hold_restore = 0;
    if (grab_focus) grab_focus(webview);
    gtk_main();
    store_window_geometry();
    return 0;
}

void studio_window_request_close(void) {
    g_leave = 1;
    http_server_request_stop();
}

#endif
