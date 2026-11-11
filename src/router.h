#ifndef STUDIO_ROUTER_H
#define STUDIO_ROUTER_H

#include "http.h"

/* Fill res for one request. repo_root is the ClassicCars repository root. */
int router_handle(const HttpRequest *req, HttpResponse *res, const char *repo_root);

#endif
