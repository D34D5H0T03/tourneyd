#include "../../include/handlers/http_handler.h"
#include "../../include/http/parser.h"
#include "../../include/util/log.h"
#include <stdio.h>
#include <string.h>

// Debug helper to print the parsed method
static const char* method_to_str(http_method_t m) {
    switch(m) {
        case METHOD_GET: return "GET";
        case METHOD_POST: return "POST";
        case METHOD_HEAD: return "HEAD";
        case METHOD_PUT: return "PUT";
        case METHOD_DELETE: return "DELETE";
        case METHOD_PATCH: return "PATCH";
        case METHOD_OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

void http_test_handle(conn_t *client) {
    char buffer[8192];
    size_t total_len = 0;
    
    http_request_t req;
    http_request_init(&req);

    while (1) {
        // Read into the available space at the end of our buffer
        ssize_t bytes_read = conn_read(client, buffer + total_len, sizeof(buffer) - total_len);
        
        if (bytes_read <= 0) {
            break; // Client disconnected or network error
        }
        
        total_len += bytes_read;
        
        // Feed the accumulated bytes to the incremental parser
        size_t consumed = http_parse(&req, buffer, total_len);
        
        // Shift unparsed bytes (e.g., incomplete headers or body) to the front of the buffer
        if (consumed > 0 && consumed < total_len) {
            memmove(buffer, buffer + consumed, total_len - consumed);
            total_len -= consumed;
        } else if (consumed == total_len) {
            total_len = 0;
        }

        // Check if the state machine reached a terminal state
        if (req.state == PARSE_DONE) {
             // log it
            log_info("Parsed request: %s %s %s (Headers: %d, Body: %zu bytes)", 
                     method_to_str(req.method), req.path, req.version, 
                     req.header_count, req.content_length);

            // Send a valid HTTP dummy response to satisfy the client
            const char *dummy_resp = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\nConnection: close\r\n\r\nParser Works!";
            conn_write_all(client, dummy_resp, strlen(dummy_resp));
            break;
            
        } else if (req.state == PARSE_ERROR) {
            log_error("Parse rejected: Status Code %d", req.error_status);
            
            // Send a generic error response back
            const char *err_resp = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            conn_write_all(client, err_resp, strlen(err_resp));
            break;
        }
    }

    // Critical: Free the dynamically allocated body to prevent memory leaks
    http_request_free(&req);
}