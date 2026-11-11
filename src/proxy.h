#ifndef STUDIO_PROXY_H
#define STUDIO_PROXY_H

#include "http.h"

/*
 * Forward a request to 127.0.0.1:<php port>. A mutex serializes calls because
 * php -S serves one request at a time.
 * preview_document_uri, when non-NULL and non-empty, is sent as
 * X-Preview-Document-Uri. res is always filled (502 if PHP is unreachable).
 * Returns 0.
 */
int proxy_to_php(const HttpRequest *req, HttpResponse *res,
                 const char *preview_document_uri);

/*
 * GET virtual_url (path and optional query) through the PHP server.
 * On success *body is malloc'd. Returns 0, or -1 if PHP cannot be reached.
 */
int proxy_php_get(const char *virtual_url,
                  const char *preview_document_uri,
                  int *status,
                  char *content_type, size_t content_type_sz,
                  char **body, size_t *body_len);

#endif
