#ifndef STUDIO_CONFIG_H
#define STUDIO_CONFIG_H

#include "json.h"

#include <stddef.h>

#define CONFIG_PUBLIC_PATH "/__studio/config.json"

typedef struct StudioConfig {
    char brand_name[128];
    char site_origin[256];
    char site_host[128];
    char site_hosts[8][128];
    size_t site_hosts_count;
    char default_author[128];
    char default_website_root[4096];
    char google_measurement_id[64];
    char extreme_login[64];
    char extreme_tracker_host[128];
    char traffic_exclude_include[256];
} StudioConfig;

void config_init_defaults(StudioConfig *cfg);

/* Active in-memory configuration. Always returns a valid pointer. */
const StudioConfig *config_get(void);

/* Resolve state/config.json and its parent state/ directory. */
int config_resolve_paths(const char *base_root, char *file_out, size_t file_sz,
                         char *dir_out, size_t dir_sz);

/* Load state/config.json if present; otherwise apply clean defaults.
 * Returns 0 when loaded from disk, 1 when file is absent, -1 on error. */
int config_load(const char *base_root);

/* Ensure state/config.json exists on disk, creating it if absent. */
int config_ensure_file(const char *base_root, char *file_out, size_t file_sz);

/* Validate json_text as a JSON object, write state/config.json, and reload. */
int config_save_text(const char *base_root, const char *json_text, char *err, size_t err_sz);

/* Build a JSON_OBJECT representing the active configuration. */
JsonValue *config_to_json(void);

/* Build the Studio code-editor page document for /__studio/config.json. */
JsonDoc *config_page_document(const char *base_root, char *err, size_t err_sz);

/* Open the folder containing state/config.json in the system file manager. */
int config_open_folder(const char *base_root, char *dir_out, size_t dir_sz,
                       char *err, size_t err_sz);

#endif
