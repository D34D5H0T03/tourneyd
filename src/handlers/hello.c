#include "handlers/hello.h"
#include "http/status.h"
#include <ctype.h>
#include <stdlib.h>

void hello_handler(const http_request_t *req, http_response_t *res) {
    (void)req; // explicitly mark unused to prevent compiler warnings
    http_response_set_body(res, "Hello from the router!\n", 23, "text/plain; charset=utf-8");
}

void upper_handler(const http_request_t *req, http_response_t *res) {
    size_t n = req->body_received;
    
    // Allocate buffer for uppercase conversion (min 1 byte to avoid malloc(0))
    char *tmp = malloc(n ? n : 1);
    if (!tmp) { 
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR); 
        return; 
    }
    
    // Convert request body to uppercase
    for (size_t i = 0; i < n; i++) {
        tmp[i] = (char)toupper((unsigned char)req->body[i]);
    }
    
    http_response_set_body(res, tmp, n, "text/plain; charset=utf-8");
    free(tmp);
}