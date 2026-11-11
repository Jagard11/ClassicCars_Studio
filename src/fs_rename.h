#ifndef STUDIO_FS_RENAME_H
#define STUDIO_FS_RENAME_H

#include "json.h"

#include <stddef.h>

/* File and folder rename operations inside website content sections.
 *
 * Return codes:
 *   0  success (*out is set)
 *   1  validation / bad request (err is set) -> HTTP 400
 *   2  not found (err is set)                -> HTTP 404
 *   3  conflict / already exists (err is set)-> HTTP 409
 *  -1  internal / IO error (err is set)      -> HTTP 500
 */
int fs_rename_item(const char *repo_root, const JsonValue *body, JsonDoc **out,
                   char *err, size_t err_sz);

#endif
