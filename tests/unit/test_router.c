#include "../../include/router/router.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

// --- Mock Handlers for Testing ---
static void handler_a(const http_request_t *req, http_response_t *res) {
    (void)req;
    res->status = HTTP_STATUS_OK;
    http_response_add_header(res, "X-Handler", "A");
}

static void handler_b(const http_request_t *req, http_response_t *res) {
    (void)req;
    res->status = HTTP_STATUS_OK;
    http_response_add_header(res, "X-Handler", "B");
}

static void handler_fallback(const http_request_t *req, http_response_t *res) {
    (void)req;
    res->status = HTTP_STATUS_OK;
    http_response_add_header(res, "X-Handler", "Fallback");
}

// --- Helper Functions ---
// Simulates the orchestrator passing a request to the router
static void dispatch(http_method_t method, const char *path, http_response_t *res) {
    http_request_t req;
    http_request_init(&req);
    req.method = method;
    snprintf(req.path, sizeof(req.path), "%s", path);
    
    http_response_init(res);
    router_dispatch(&req, res);
}

// Quickly grabs a header value from the response
static const char *get_header(const http_response_t *res, const char *name) {
    for (int i = 0; i < res->header_count; i++) {
        if (strcmp(res->headers[i].name, name) == 0) return res->headers[i].value;
    }
    return NULL;
}

// --- Test Cases ---
static void test_exact_match() {
    router_init();
    router_add(METHOD_GET, "/a", handler_a);
    
    http_response_t res;
    dispatch(METHOD_GET, "/a", &res);
    
    assert(res.status == HTTP_STATUS_OK);
    assert(strcmp(get_header(&res, "X-Handler"), "A") == 0);
    printf("[OK] test_exact_match\n");
}

static void test_head_alias() {
    router_init();
    router_add(METHOD_GET, "/a", handler_a); // Register GET
    
    http_response_t res;
    dispatch(METHOD_HEAD, "/a", &res);       // Send HEAD
    
    // The router should alias HEAD to GET and fire handler_a
    assert(res.status == HTTP_STATUS_OK);
    assert(strcmp(get_header(&res, "X-Handler"), "A") == 0);
    printf("[OK] test_head_alias\n");
}

static void test_method_not_allowed() {
    router_init();
    router_add(METHOD_GET, "/a", handler_a);
    router_add(METHOD_POST, "/a", handler_b);
    
    http_response_t res;
    dispatch(METHOD_PUT, "/a", &res); // Send an unsupported method
    
    assert(res.status == HTTP_STATUS_METHOD_NOT_ALLOWED);
    assert(strcmp(get_header(&res, "Allow"), "GET, HEAD, POST") == 0);
    printf("[OK] test_method_not_allowed\n");
}

static void test_prefix_matching() {
    router_init();
    router_add_prefix(METHOD_GET, "/api/", handler_a);
    router_add_prefix(METHOD_GET, "/api/v2/", handler_b);
    
    http_response_t res;
    
    // 1. Longest prefix should win
    dispatch(METHOD_GET, "/api/v2/users", &res);
    assert(strcmp(get_header(&res, "X-Handler"), "B") == 0);
    
    // 2. Exact match should beat prefix
    router_add(METHOD_GET, "/api/users", handler_a); // exact
    dispatch(METHOD_GET, "/api/users", &res);
    assert(strcmp(get_header(&res, "X-Handler"), "A") == 0);
    
    // 3. Prefix shouldn't catch unrelated paths sharing the same letters
    dispatch(METHOD_GET, "/apix", &res);
    assert(res.status == HTTP_STATUS_NOT_FOUND);
    
    printf("[OK] test_prefix_matching\n");
}

static void test_fallback() {
    router_init();
    router_set_fallback(handler_fallback);
    
    http_response_t res;
    
    // 1. Unmatched GET should trigger fallback
    dispatch(METHOD_GET, "/nope", &res);
    assert(strcmp(get_header(&res, "X-Handler"), "Fallback") == 0);
    
    // 2. Unmatched POST should 404, not fallback
    dispatch(METHOD_POST, "/nope", &res);
    assert(res.status == HTTP_STATUS_NOT_FOUND);
    
    printf("[OK] test_fallback\n");
}

static void test_table_limits() {
    router_init();
    
    // 1. Prefix without trailing slash must be rejected
    assert(router_add_prefix(METHOD_GET, "/api", handler_a) == -1);
    
    // 2. Fill the table to exactly 64 routes
    for (int i = 0; i < 64; i++) {
        assert(router_add(METHOD_GET, "/x", handler_a) == 0);
    }
    
    // 3. The 65th route should be rejected
    assert(router_add(METHOD_GET, "/y", handler_a) == -1);
    
    printf("[OK] test_table_limits\n");
}

int main(void) {
    printf("Running Router Unit Tests...\n");
    test_exact_match();
    test_head_alias();
    test_method_not_allowed();
    test_prefix_matching();
    test_fallback();
    test_table_limits();
    printf("All router tests passed successfully.\n");
    return 0;
}