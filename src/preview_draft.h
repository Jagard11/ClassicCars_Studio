#ifndef STUDIO_PREVIEW_DRAFT_H
#define STUDIO_PREVIEW_DRAFT_H

#include "json.h"

/* Unsaved preview overlay. The open page in sales/, articles/, awards/,
 * and projects/ is never written. Drafts live in memory and in
 * studio/state/draft.json so the PHP preview process can read them.
 *
 * revision is a client counter. An older revision is ignored so a slow
 * stage cannot overwrite a newer edit or a save. Returns 0, or -1. */
int preview_draft_stage(const char *repo_root, const char *path,
                        const char *content, int has_content,
                        const JsonValue *images, int has_images,
                        long long revision);

/* Drop one staged draft, or every draft when path is NULL or empty. */
int preview_draft_clear(const char *repo_root, const char *path, long long revision);

/* Malloc'd staged source, or NULL when this path has no staged content. */
char *preview_draft_content(const char *repo_root, const char *path);

#endif
