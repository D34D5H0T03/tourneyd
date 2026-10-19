#ifndef ROUTER_H
#define ROUTER_H

#include "http/parser.h"
#include "http/response.h"

// concurrency routing flags for epoll
enum {
    ROUTE_DEFAULT = 0,
    ROUTE_BLOCKING = 1
};

// A handler fills the response.
typedef void (*route_handler_fn)(const http_request_t *req, http_response_t *res);

// Clears the route table. Call once at startup, before concurrency_run(). 
void router_init(void);

// Exact-match route. Returns 0, or -1 if the table is full or the path is invalid. 
int router_add(http_method_t method, const char *path, route_handler_fn handler, unsigned flags);

// Prefix route, e.g. "/api/". The prefix must start and end with '/'. Returns 0 / -1. 
int router_add_prefix(http_method_t method, const char *prefix, route_handler_fn handler, unsigned flags);

// Handler for GET/HEAD requests that match no route (the static file handler later). 
void router_set_fallback(route_handler_fn handler);

// Finds the handler and calls it, or fills in 404/405. Always leaves res valid. 
void router_dispatch(const http_request_t *req, http_response_t *res);

#endif