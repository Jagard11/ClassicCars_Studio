#ifndef STUDIO_FS_OPS_H
#define STUDIO_FS_OPS_H

#include "json.h"

#include <stddef.h>

/* Reversible file tree operations: duplicate, copy, move (cut/paste),
 * delete (to studio state/tree-trash), and undo/redo history.
 *
 * Return codes:
 *   0  success (*out is set)
 *   1  validation / bad request (err is set) -> HTTP 400
 *   2  not found (err is set)                -> HTTP 404
 *   3  conflict / already exists (err is set)-> HTTP 409
 *  -1  internal / IO error (err is set)      -> HTTP 500
 */

int fs_ops_duplicate(const char *repo_root, const JsonValue *body, JsonDoc **out,
                     char *err, size_t err_sz);

int fs_ops_copy(const char *repo_root, const JsonValue *body, JsonDoc **out,
                char *err, size_t err_sz);

int fs_ops_move(const char *repo_root, const JsonValue *body, JsonDoc **out,
                char *err, size_t err_sz);

int fs_ops_delete(const char *repo_root, const JsonValue *body, JsonDoc **out,
                  char *err, size_t err_sz);

int fs_ops_mkdir(const char *repo_root, const JsonValue *body, JsonDoc **out,
                 char *err, size_t err_sz);

int fs_ops_undo(const char *repo_root, JsonDoc **out, char *err, size_t err_sz);

int fs_ops_redo(const char *repo_root, JsonDoc **out, char *err, size_t err_sz);

int fs_ops_history(const char *repo_root, JsonDoc **out, char *err, size_t err_sz);

/* Record a completed rename operation in the tree undo history and attach
 * undo/redo status fields to the response JSON object. */
void fs_ops_record_rename(const char *repo_root, const char *kind,
                          const char *old_section, const char *new_section,
                          const char *old_slug, const char *new_slug,
                          const char *old_path, const char *new_path,
                          const char *old_name, const char *new_name,
                          JsonValue *response_root);

#endif
