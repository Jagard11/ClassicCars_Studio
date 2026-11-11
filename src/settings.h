#ifndef STUDIO_SETTINGS_H
#define STUDIO_SETTINGS_H

#include <stddef.h>

typedef struct {
    int has_pos;
    int x;
    int y;
    int width;
    int height;
    int maximized;
} StudioWindowState;

int settings_ensure_dirs(const char *repo_root);

/* Malloc'd theme.json contents, or NULL if the file is missing. Caller frees. */
char *settings_read_theme(const char *repo_root);

/* Writes json_str to state/theme.json under the studio install. Returns 0 on success. */
int settings_write_theme(const char *repo_root, const char *json_str);

/* Fills *out from state/window.json.
   A missing file or invalid field becomes a usable default: 1440x900, not
   maximized, and no saved position. Width is clamped to 960..7680 and height
   to 600..4320. Returns 0 when out is filled, or -1 if out is NULL or
   repo_root is missing. */
int settings_read_window_state(const char *repo_root, StudioWindowState *out);

/* Atomically writes state/window.json. Returns 0 on success. */
int settings_write_window_state(const char *repo_root, const StudioWindowState *st);

/* Copies lastPage from state/session.json into out.
   A missing file, missing key, or unreadable value becomes an empty string.
   A value that does not fit in out is also empty, so a path is never truncated.
   Returns 0 when out is filled, or -1 if out is NULL, out_sz is 0, or
   repo_root is missing. */
int settings_read_last_page(const char *repo_root, char *out, size_t out_sz);

/* Atomically updates lastPage in state/session.json.
   last_page may be empty. rightPanelOpen, leftPanelOpen, rightPanelWidth,
   widgetCollapsed, widgetOrder, and any other stored keys are kept.
   openTabs is removed.
   Returns 0 on success. */
int settings_write_last_page(const char *repo_root, const char *last_page);

/* Malloc'd state/session.json, or NULL if the file is missing.
   Caller frees. */
char *settings_read_session(const char *repo_root);

/* Atomically writes json_str to state/session.json.
   json_str is the full object: lastPage, openTabs, rightPanelOpen,
   leftPanelOpen, rightPanelWidth, widgetCollapsed, widgetOrder, and any
   other session keys.
   Returns 0 on success. */
int settings_write_session(const char *repo_root, const char *json_str);

int settings_get_php_port(void);
void settings_set_php_port(int port);

/*
 * Studio installation root: the checkout that contains web/, preview/prepend.php,
 * and state/ (or the same paths under studio/ when this process was started
 * from a parent directory). This does not change when the user opens a
 * different website folder.
 * An empty home clears it. Returns 0 on success.
 */
int settings_set_studio_home(const char *home);
const char *settings_get_studio_home(void);

/*
 * Active website directory. The pointer stays valid until the next successful
 * change and always refers to a complete path. Empty until one is set.
 */
const char *settings_get_project_root(void);

/*
 * Validate new_root as an existing directory, make it the active project, and
 * remember it in <studio home>/state/project.json. Returns 0 on success.
 * An empty path clears the in-memory root and does not write the file.
 */
int settings_set_project_root(const char *new_root);

/*
 * Resolve path to an absolute directory. Returns 0 on success.
 * Fails when path is missing, not a directory, or does not fit in out.
 */
int settings_canonicalize_dir(const char *path, char *out, size_t out_sz);

/*
 * studio_home is the installation checkout. cli_root, when non-NULL, is the
 * --root folder for this launch: it becomes the project root and the saved
 * folder is left untouched. Without cli_root, state/project.json (or
 * project_root.txt, including the studio/ fallback) is loaded, and the
 * installation checkout is the default.
 * Returns 0 on success.
 */
int settings_init_roots(const char *studio_home, const char *cli_root);

/*
 * Resolve rel inside a studio checkout. Standalone checkouts use <base>/<rel>
 * (web, preview/prepend.php, state, VERSION, assets/favicon-32x32.png).
 * A parent directory that still contains studio/ uses <base>/studio/<rel>.
 * Returns 0 when out is filled.
 */
int settings_install_path(const char *base, const char *rel, char *out, size_t out_sz);

/*
 * Copy the website directory saved in <studio_home>/state/project.json into out.
 * Also accepts studio/state/project.json and project_root.txt. Returns 0 when
 * out is an existing directory.
 */
int settings_read_saved_project_root(const char *studio_home, char *out, size_t out_sz);

/* Load the saved project folder. Returns 0 when applied, 1 when there is
 * nothing to load (including a --root launch), or -1 on error. */
int settings_load_project_root(void);

/*
 * Point php -S at new_root (-t) and keep auto_prepend_file in the studio
 * installation. Returns 0 when PHP is not running, or when the new server
 * accepts connections. Returns -1 when the new server does not come up.
 */
int studio_restart_php_for_root(const char *new_root);

#endif
