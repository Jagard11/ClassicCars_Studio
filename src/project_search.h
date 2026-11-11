#ifndef STUDIO_PROJECT_SEARCH_H
#define STUDIO_PROJECT_SEARCH_H

#include "json.h"

#include <stddef.h>

/* Project-wide search and replace across editable website text files.
 * Supports literal, regular expression (with $1..$9 and $& capture groups),
 * and wildcard (* and ?) matching, plus HTML/comment/PHP scope filtering
 * and include/exclude glob patterns.
 *
 * Return codes:
 *   0  success (*out is set)
 *   1  validation / bad request (err is set)
 *   2  not found (err is set)
 *  -1  internal / IO error (err is set)
 */

int project_search_query(const char *repo_root, const JsonValue *params, JsonDoc **out,
                         char *err, size_t err_sz);

int project_search_replace(const char *repo_root, const JsonValue *params, JsonDoc **out,
                           char *err, size_t err_sz);

#endif
