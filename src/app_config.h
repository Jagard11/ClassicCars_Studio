#ifndef STUDIO_APP_CONFIG_H
#define STUDIO_APP_CONFIG_H

#include "json.h"

#include <stddef.h>

/* App-wide Studio preferences stored in <studio_home>/state/app-config.json.
 * Unlike project-specific state, these settings apply across all projects.
 */
typedef struct StudioAppConfig {
    int git_auto_fetch;
    int git_auto_fetch_prompted;
    int git_auto_fetch_interval_sec;
    int auto_check_updates;
    char update_repo[128];
    char dismissed_release_version[64];
} StudioAppConfig;

void app_config_init_defaults(StudioAppConfig *cfg);

/* Active in-memory app-wide configuration. Always returns a valid pointer. */
const StudioAppConfig *app_config_get(void);

/* Resolve <studio_home>/state/app-config.json path. */
int app_config_resolve_path(const char *fallback_root, char *file_out, size_t file_sz);

/* Load <studio_home>/state/app-config.json if present.
 * Returns 0 when loaded from disk, 1 when file is absent, -1 on error. */
int app_config_load(const char *fallback_root);

/* Apply partial or full JSON object update and atomically persist to <studio_home>/state/app-config.json.
 * Returns 0 on success, 1 on validation error, -1 on I/O error. */
int app_config_apply_update(const char *fallback_root, const JsonValue *body,
                            char *err, size_t err_sz);

/* Build a JSON_OBJECT representing the active app-wide configuration. */
JsonValue *app_config_to_json(void);

#endif
