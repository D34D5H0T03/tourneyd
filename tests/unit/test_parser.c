#include "http/parser.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

// Helper to mock feeding the parser
static void feed_parser(http_request_t *req, const char *data, size_t chunk_size) {
    size_t total_data_len = strlen(data);
    size_t data_offset = 0; 
    
    char buffer[8192];
    size_t buffer_len = 0;
    
    while (data_offset < total_data_len && req->state != PARSE_DONE && req->state != PARSE_ERROR) {
        // "Read" chunk_size bytes from the simulated network into our buffer
        size_t remaining_to_read = total_data_len - data_offset;
        size_t to_read = (remaining_to_read < chunk_size) ? remaining_to_read : chunk_size;
        
        memcpy(buffer + buffer_len, data + data_offset, to_read);
        buffer_len += to_read;
        data_offset += to_read;
        
        // Feed the growing buffer to the parser
        size_t consumed = http_parse(req, buffer, buffer_len);
        
        // Shift unparsed bytes to the front, exactly like http_handler.c
        if (consumed > 0 && consumed < buffer_len) {
            memmove(buffer, buffer + consumed, buffer_len - consumed);
            buffer_len -= consumed;
        } else if (consumed == buffer_len) {
            buffer_len = 0;
        }
    }
}

static void test_query_split() {
    http_request_t req;
    http_request_init(&req, NULL);
    
    const char *raw = "GET /api/users?role=admin&active=true HTTP/1.1\r\nHost: localhost\r\n\r\n";
    feed_parser(&req, raw, strlen(raw));
    
    assert(req.state == PARSE_DONE);
    assert(req.method == METHOD_GET);
    assert(strcmp(req.path, "/api/users") == 0);
    assert(strcmp(req.query, "role=admin&active=true") == 0);
    assert(strcmp(req.version, "HTTP/1.1") == 0);
    
    printf("[OK] test_query_split\n");
}

static void test_no_query() {
    http_request_t req;
    http_request_init(&req, NULL);
    
    const char *raw = "POST /login HTTP/1.0\r\nContent-Length: 0\r\n\r\n";
    feed_parser(&req, raw, strlen(raw));
    
    assert(req.state == PARSE_DONE);
    assert(strcmp(req.path, "/login") == 0);
    assert(req.query[0] == '\0');
    
    printf("[OK] test_no_query\n");
}

static void test_incremental_delivery() {
    http_request_t req;
    http_request_init(&req, NULL);
    
    // Simulate severe network latency by feeding the parser 1 single byte at a time
    const char *raw = "GET /incremental?test=1 HTTP/1.1\r\nHost: x\r\n\r\n";
    feed_parser(&req, raw, 1);
    
    assert(req.state == PARSE_DONE);
    assert(strcmp(req.path, "/incremental") == 0);
    assert(strcmp(req.query, "test=1") == 0);
    
    printf("[OK] test_incremental_delivery\n");
}

static void test_malformed_requests() {
    http_request_t req;
    
    // Test 1: Unsupported Version
    http_request_init(&req, NULL);
    const char *bad_version = "GET / HTTP/2.0\r\n\r\n";
    feed_parser(&req, bad_version, strlen(bad_version));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_VERSION_NOT_SUPPORTED);
    
    // Test 2: Missing Path Slash
    http_request_init(&req, NULL);
    const char *bad_path = "GET index.html HTTP/1.1\r\n\r\n";
    feed_parser(&req, bad_path, strlen(bad_path));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_BAD_REQUEST);

    printf("[OK] test_malformed_requests\n");
}

static void test_hardening() {
    http_request_t req;

    // 1. NUL in header name (Smuggling)
    http_request_init(&req, NULL);
    const char nul_name[] = "GET / HTTP/1.1\r\nHost: a\r\nContent-Length\0x: 5\r\n\r\n";

    // Bypass feed_parser to avoid strlen() truncating the input at the \0 byte
    http_parse(&req, nul_name, sizeof(nul_name) - 1); 

    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_BAD_REQUEST);

    // 2. DEL in value
    http_request_init(&req, NULL);
    const char *del_val = "GET / HTTP/1.1\r\nHost: a\r\nHeader: val\x7f\r\n\r\n";
    feed_parser(&req, del_val, strlen(del_val));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_BAD_REQUEST);

    // 3. Duplicate Host
    http_request_init(&req, NULL);
    const char *dup_host = "GET / HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n";
    feed_parser(&req, dup_host, strlen(dup_host));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_BAD_REQUEST);

    // 4. Two Content-Length, 5 and 7
    http_request_init(&req, NULL);
    const char *dup_cl = "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nContent-Length: 7\r\n\r\n";
    feed_parser(&req, dup_cl, strlen(dup_cl));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_BAD_REQUEST);

    // 5. Two Content-Length, both 5 (Accepted)
    http_request_init(&req, NULL);
    const char *dup_cl_same = "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\n";
    feed_parser(&req, dup_cl_same, strlen(dup_cl_same));
    assert(req.state == PARSE_BODY); 

    // 6. HTTP/1.1 without Host
    http_request_init(&req, NULL);
    const char *no_host_11 = "GET / HTTP/1.1\r\n\r\n";
    feed_parser(&req, no_host_11, strlen(no_host_11));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_BAD_REQUEST);

    // 7. HTTP/1.0 without Host (Accepted)
    http_request_init(&req, NULL);
    const char *no_host_10 = "GET / HTTP/1.0\r\n\r\n";
    feed_parser(&req, no_host_10, strlen(no_host_10));
    assert(req.state == PARSE_DONE);

    // 8. Size Clamping
    http_limits_t bad_limits = { .max_body_size = SIZE_MAX, .max_header_bytes = SIZE_MAX };
    http_request_init(&req, &bad_limits);
    const char *big_body = "POST / HTTP/1.1\r\nHost: a\r\nContent-Length: 2097152\r\n\r\n"; // 2MB
    feed_parser(&req, big_body, strlen(big_body));
    assert(req.state == PARSE_ERROR);
    assert(req.error_status == HTTP_STATUS_PAYLOAD_TOO_LARGE);

    printf("[OK] test_hardening\n");
}

int main() {
    printf("Running Parser Unit Tests...\n");
    test_query_split();
    test_no_query();
    test_incremental_delivery();
    test_malformed_requests();
    test_hardening();
    printf("All parser tests passed successfully.\n");
    return 0;
}