#include "router/router.h"

#include <stdio.h>
#include <string.h>

#define MAX_ROUTES 64
#define MAX_ROUTE_PATH 128
#define ALLOW_HEADER_BUFFER_SIZE 128

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
    if(!path || !handler || path[0] != '/' || method == METHOD_UNKNOWN){
        return -1; 
    }

    size_t len = strlen(path);
    if(len >= MAX_ROUTE_PATH){
        return -1;
    }
    if(is_prefix && path[len - 1] != '/'){
        return -1;
    }
    if(route_count >= MAX_ROUTES) {
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
int router_add_prefix(http_method_t method, const char *prefix, route_handler_fn handler){
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
    return strcmp(path, route->path) == 0;
}

static void build_allow(unsigned mask, char *out, size_t cap){
    out[0] = '\0';
    size_t len = 0;

    for(int m = METHOD_GET; m <= METHOD_OPTIONS; m++){
        if(!(mask & (1u << m))){
            continue; // bit checking with & and skipping over 0s
        }

        int n = snprintf(out + len, cap - len, "%s%s", len ? ", " : "",
                    http_method_to_str((http_method_t)m));
        if(n < 0 || (size_t)n >= cap - len){
            return; // fail
        }

        len += (size_t)n;
        if(m == METHOD_GET && !(mask & (1u << METHOD_HEAD))){
            n = snprintf(out + len, cap - len, ", HEAD");
            
            if(n < 0 || (size_t)n >= cap - len){
                return; // fail
            }
            len += (size_t)n;
        }
    }
}


void router_dispatch(const http_request_t *req, http_response_t *res){
    // HEAD is routed exactly like GET
    http_method_t method = (req->method == METHOD_HEAD) ? METHOD_GET : req->method;

    const route_t *best = NULL;
    size_t best_score = 0;
    unsigned allowed = 0; // unsigned int, 30 bit array of 0s for setting and checking
    int path_matched = 0;

    for(int i = 0; i < route_count; i++){
        const route_t *route = &routes[i];
        if(!path_matches(route, req->path)){
            continue;
        }

        path_matched = 1;

        allowed |= 1u << route->method; // set the flag on allowed

        if(route->method != method) continue;

        // exact match beats any prefix, among prefixes the longes path wins
        size_t score = route->is_prefix ? route->path_len : (size_t)-1; // wraps around to max value of unsigned integer(2^32)

        if(!best || score > best_score){
            best = route;
            best_score = score;
        }
    }

    if(best){
        best->handler(req, res);
        return;
    }

    if(path_matched){
        char allow[ALLOW_HEADER_BUFFER_SIZE];
        build_allow(allowed, allow, sizeof(allow));
        http_response_set_error(res, HTTP_STATUS_METHOD_NOT_ALLOWED);
        http_response_add_header(res, "Allow", allow);
        return;
    }

    if(fallback && method == METHOD_GET){
        fallback(req, res);
        return;
    }

    http_response_set_error(res, HTTP_STATUS_NOT_FOUND);
}



