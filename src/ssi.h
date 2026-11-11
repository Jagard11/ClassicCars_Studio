#ifndef STUDIO_SSI_H
#define STUDIO_SSI_H

#include <stddef.h>

/*
 * Expand <!--#include virtual="..." --> directives up to depth 8.
 * top_level_page_uri is forwarded to PHP includes as X-Preview-Document-Uri
 * (the HTML page that contained the include, not the include itself).
 * *out is malloc'd on success. Missing includes become HTML comments.
 * Returns 0 on success, -1 if memory or the output limit is exceeded.
 */
int ssi_splice(const char *input, size_t input_len,
               const char *repo_root,
               const char *top_level_page_uri,
               char **out, size_t *out_len);

#endif
