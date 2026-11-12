#ifndef STUDIO_PROJECT_MACROS_H
#define STUDIO_PROJECT_MACROS_H

#include "json.h"
#include <stddef.h>

/*
 * Per-project macro definitions and right-panel tool settings.
 * Stored in <studio_home>/state/project-macros.json keyed by project_root,
 * keeping project git trees clean while giving each project its own macros.
 */

/* Resolve the path to <studio_home>/state/project-macros.json. */
int project_macros_resolve_path(const char *fallback_root, char *file_out, size_t file_sz);

/* Build the GET /__studio/api/macros response document for repo_root. */
JsonDoc *project_macros_get_document(const char *repo_root, char *err, size_t err_sz);

/*
 * Apply a POST /__studio/api/macros update for repo_root and return the
 * updated response document in *out_doc.
 * Returns 0 on success, 1 on bad request (400), -1 on internal error (500).
 */
int project_macros_apply_update(const char *repo_root, const JsonValue *body,
                                JsonDoc **out_doc, char *err, size_t err_sz);

#endif
