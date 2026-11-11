#ifndef STUDIO_TERMINAL_H
#define STUDIO_TERMINAL_H

#include "json.h"

#include <stddef.h>

/* Integrated terminal command execution and environment info.
 *
 * Return codes:
 *   0  success (*out is set)
 *   1  validation / bad request (err is set)
 *  -1  internal / execution failure (err is set)
 */

int terminal_info(const char *repo_root, const char *req_cwd, JsonDoc **out,
                  char *err, size_t err_sz);

int terminal_exec(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz);

int terminal_start(const char *repo_root, const JsonValue *body, JsonDoc **out,
                   char *err, size_t err_sz);

int terminal_poll(int job_id, size_t offset, JsonDoc **out,
                  char *err, size_t err_sz);

int terminal_stop(int job_id, JsonDoc **out,
                  char *err, size_t err_sz);

#endif
