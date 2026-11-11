#ifndef STUDIO_API_H
#define STUDIO_API_H

#include "http.h"

/* Handle /__studio/api/... and /api/... . Returns 0 when the request was
 * answered, or -1 when the path is not an API route. */
int api_handle_request(const HttpRequest *req, HttpResponse *res, const char *repo_root);

#endif
