#include "router/router.h"

#include <stdio.h>
#include <string.h>

#define MAX_ROUTES 64
#define MAX_ROUTE_PATH 128

typedef struct {
    http_method_t method;
    char path[MAX_ROUTE_PATH];
    size_t path_len;
    int is_prefix;
    route_handler_fn handler;
} route_t;


// filled at startup, read only from then. makes it safe under fork threads and epoll
static route_t routes[MAX_ROUTES];
static int route_count;
static route_handler_fn fallback;

void router_init(void){
    memset(routes, 0, sizeof(routes));
    route_count = 0;
    fallback = NULL;
}

static int add_route(http_method_t method, const char *path,
     route_handler_fn handler, int is_prefix){
    
    // guard clauses
    if(!path || !handler || path[0] == '\0' || method == METHOD_UNKNOWN){
        return -1; 
    }

    size_t len = strlen(path);
    if(len >= MAX_ROUTE_PATH){
        return -1;
    }
    if(is_prefix && path[len - 1] != '/'){
        return -1;
    }
    if(route_count > MAX_ROUTES) {
        return -1;
    }

    route_t *r = &routes[route_count++];
    r->method = method;
    memcpy(r->path, path, len + 1);
    r->path_len = len;
    r->is_prefix = is_prefix;
    r->handler = handler;

    return 0; //successfully added path
}

// adds an exact route
int router_add(http_method_t method, const char *path, route_handler_fn handler){
    return add_route(method, path, handler, 0);
}
// adds a wildcard prefix
void router_add_prefix(http_method_t method, const char *prefix, route_handler_fn handler){
    return add_route(method, prefix, handler, 1);
}

// sets the fallback handler
void router_set_fallback(route_handler_fn handler){
    fallback = handler;
}

static int path_matches(const route_t *route, const char *path){
    if(route->is_prefix){
        return strncmp(path, route->path, route->path_len) == 0; // selective compare 
    }
    return strcmp(path, route->path);
}


void router_dispatch(const http_request_t *req, http_response_t *res){
    // HEAD is routed exactly like GET
    http_method_t method = (req->method == METHOD_HEAD) ? METHOD_GET : req->method;

    const route_t *best = NULL;
    size_t best_score = 0;
    unsigned allowed = 0;
    int path_matched = 0;
}



