#ifndef STUDIO_GIT_SCM_H
#define STUDIO_GIT_SCM_H

#include "json.h"

#include <stddef.h>

/* Git version control operations for the active project repository.
 *
 * Return codes:
 *   0  success (*out is set)
 *   1  validation / bad request (err is set)
 *   2  not found (err is set)
 *  -1  internal / git command failure (err is set)
 */

int git_scm_status(const char *repo_root, JsonDoc **out, char *err, size_t err_sz);

int git_scm_stage(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz);

int git_scm_unstage(const char *repo_root, const JsonValue *body, JsonDoc **out,
                    char *err, size_t err_sz);

int git_scm_discard(const char *repo_root, const JsonValue *body, JsonDoc **out,
                    char *err, size_t err_sz);

int git_scm_commit(const char *repo_root, const JsonValue *body, JsonDoc **out,
                   char *err, size_t err_sz);

int git_scm_diff(const char *repo_root, const char *path, int staged, const char *commit,
                 JsonDoc **out, char *err, size_t err_sz);

#endif
