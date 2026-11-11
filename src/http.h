#ifndef STUDIO_HTTP_H
#define STUDIO_HTTP_H

#include <stddef.h>

#define HTTP_MAX_BODY (4u * 1024u * 1024u)

#define HTTP_PARSE_OK 0
#define HTTP_PARSE_BAD (-1)
#define HTTP_PARSE_TOO_LARGE (-2)
#define HTTP_PARSE_NO_LENGTH (-3)

typedef struct {
    char method[16];
    char path[1024];       /* URL-decoded path without query string */
    char raw_uri[2048];    /* Raw path + query string */
    char query[1024];      /* Raw query string without leading '?' */
    char content_type[128];
    char *body;
    size_t body_len;
    /* Forwarded to the php -S child by the proxy. */
    char cookie[4096];
    char user_agent[512];
} HttpRequest;

typedef struct {
    int status;
    char content_type[128];
    char location[1024];
    char extra_headers[512]; /* Complete "Name: value\r\n" lines. */
    char *body;
    size_t body_len;
    size_t body_cap;
} HttpResponse;

void http_response_init(HttpResponse *res);
void http_response_free(HttpResponse *res);
int http_response_set_body(HttpResponse *res, const void *data, size_t len);
int http_response_set_json(HttpResponse *res, int status, const char *json_str);
int http_response_set_error(HttpResponse *res, int status, const char *message);

/* Returns 1 when key is present, 0 otherwise. '+' in values becomes space. */
int http_query_param(const char *query, const char *key, char *out_val, size_t out_sz);

/* Decodes %HH into dst. Does not treat '+' as space. Returns 0 on success. */
int http_url_decode(const char *src, char *dst, size_t dst_sz);

void http_request_init(HttpRequest *req);
void http_request_free(HttpRequest *req);

/* Parse one complete HTTP/1.0 or HTTP/1.1 request (headers and body). */
int http_parse_request_buffer(const char *data, size_t len, HttpRequest *req);

/* Read a regular file. *out is malloc'd and NUL-terminated. Returns 0 on success. */
int http_read_file(const char *path, char **out, size_t *out_len, size_t max_bytes);

/* Returns 0 when a TCP connection to host:port succeeds within timeout_ms. */
int http_try_connect(const char *host, int port, int timeout_ms);

typedef int (*HttpHandler)(const HttpRequest *req, HttpResponse *res, void *userdata);
typedef void (*HttpTick)(void *userdata);

typedef struct HttpServer HttpServer;

HttpServer *http_server_new(void);
void http_server_free(HttpServer *srv);
int http_server_listen(HttpServer *srv, const char *bind_host, int port);
int http_server_port(const HttpServer *srv);
int http_server_serve(HttpServer *srv, HttpHandler handler, void *handler_ud,
                      HttpTick tick, void *tick_ud);
void http_server_request_stop(void);

#endif
