#ifndef STUDIO_PREFLIGHT_H
#define STUDIO_PREFLIGHT_H

#include <stddef.h>

typedef struct StudioPreflightStatus {
    int php_ok;
    char php_version[64];
    char php_path[512];
    int git_ok;
    char git_version[64];
    char git_path[512];
    int webview_ok;
    char webview_info[128];
    int browser_ok;
    char browser_info[128];
} StudioPreflightStatus;

/* Searches common installation directories (WinGet, C:\tools\php*, C:\php*,
 * XAMPP, Scoop, Program Files, Git\cmd, etc.) and refreshes process PATH
 * so PHP and Git are found even when PATH was not updated in Explorer. */
void studio_preflight_discover_path(void);

/* Probes all runtime dependencies at once (PHP, Git, WebView2/GTK, Browser). */
void studio_preflight_probe(StudioPreflightStatus *st);

/* Formats a human-readable all-at-once diagnostic summary of all dependencies. */
void studio_preflight_format_report(const StudioPreflightStatus *st, int require_php,
                                    char *out, size_t out_sz);

/* Runs discovery + all-at-once check.
 * Returns:
 *   0  -> all required dependencies are ready (proceed normally)
 *   1  -> user elected to continue in --no-php mode
 *  -1  -> required dependency missing and user cancelled / headless abort
 */
int studio_preflight_require(int require_php, int headless);

#endif
