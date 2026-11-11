#ifndef STUDIO_CONFIG_H
#define STUDIO_CONFIG_H

#include "json.h"

#include <stddef.h>

#define CONFIG_PUBLIC_PATH "/__studio/config.json"

typedef struct StudioConfig {
    char google_measurement_id[64];
    char extreme_login[64];
    char extreme_tracker_host[128];
} StudioConfig;

void config_init_defaults(StudioConfig *cfg);

/* Active in-memory configuration. Always returns a valid pointer. */
const StudioConfig *config_get(void);

/* Resolve state/config.json and its parent state/ directory. */
int config_resolve_paths(const char *base_root, char *file_out, size_t file_sz,
                         char *dir_out, size_t dir_sz);

/* Resolve state/config.backup.json path. */
int config_resolve_backup_path(const char *base_root, char *backup_out, size_t backup_sz);

/* Load state/config.json if present (supporting // and block comments).
 * Returns 0 when loaded from disk, 1 when file is absent, -1 on error. */
int config_load(const char *base_root);

/* Ensure state/config.json exists on disk, writing the commented template if absent. */
int config_ensure_file(const char *base_root, char *file_out, size_t file_sz);

/* Validate json_text (allowing // and block comments), write state/config.json, and reload. */
int config_save_text(const char *base_root, const char *json_text, char *err, size_t err_sz);

/* Save a backup copy of state/config.json to state/config.backup.json. */
int config_backup(const char *base_root, char *backup_out, size_t backup_sz,
                  char *err, size_t err_sz);

/* Restore state/config.json from state/config.backup.json and reload. */
int config_recover(const char *base_root, char *err, size_t err_sz);

/* Clear state/config.json back to an empty commented template and reload. */
int config_clear(const char *base_root, char *err, size_t err_sz);

/* Build a JSON_OBJECT representing the active configuration. */
JsonValue *config_to_json(void);

/* Build the Studio code-editor page document for /__studio/config.json. */
JsonDoc *config_page_document(const char *base_root, char *err, size_t err_sz);

/* Open the folder containing state/config.json in the system file manager. */
int config_open_folder(const char *base_root, char *dir_out, size_t dir_sz,
                       char *err, size_t err_sz);

#endif
