#ifndef RESPONSE_H
#define RESPONSE_H

#include "http/parser.h" // Reuses http_header_t
#include "http/status.h"
#include <stddef.h>

#define MAX_RESP_HEADERS 16

typedef struct {
    http_status_t status;
    http_header_t headers[MAX_RESP_HEADERS];
    int           header_count;
    
    char         *body;        // Dynamically allocated body payload (with malloc)
    size_t        body_len;    // Exact size of the body payload
} http_response_t;

// Zeroes the struct and sets the default status to 200 OK
void http_response_init(http_response_t *res);

// Frees the dynamically allocated body and prevents memory leak and dangling pointers
void http_response_free(http_response_t *res);

// Appends a header. Rejects \r or \n in name/value to prevent CRLF injection.
int http_response_add_header(http_response_t *res, const char *name, const char *value);

// Copies data into a newly allocated body buffer and sets the Content-Type
int http_response_set_body(http_response_t *res, const void *data, size_t len, const char *content_type);

// Utility to quickly generate an error page (e.g., "<h1>404 Not Found</h1>")
void http_response_set_error(http_response_t *res, http_status_t code);

// Serializes the status line, required headers, and custom headers into a malloc'd buffer
char *http_response_serialize_head(const http_response_t *res, int keep_alive, size_t *out_len);

#endif // RESPONSE_H