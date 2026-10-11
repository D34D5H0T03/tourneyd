#include "http/response.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_serialize_happy_path() {
    http_response_t res;
    http_response_init(&res);
    
    http_response_set_body(&res, "hello", 5, "text/plain");
    http_response_add_header(&res, "X-Custom", "TestValue");

    size_t head_len;
    char *head = http_response_serialize_head(&res, 0, &head_len);
    
    assert(head != NULL);
    assert(strstr(head, "HTTP/1.1 200 OK\r\n") == head); // Must start exactly with this
    assert(strstr(head, "Content-Length: 5\r\n") != NULL);
    assert(strstr(head, "Content-Type: text/plain\r\n") != NULL);
    assert(strstr(head, "X-Custom: TestValue\r\n") != NULL);
    
    free(head);
    http_response_free(&res);
    printf("[OK] test_serialize_happy_path\n");
}

static void test_crlf_injection() {
    http_response_t res;
    http_response_init(&res);
    
    // Attempt to inject a fake header via CRLF
    int rc = http_response_add_header(&res, "X-Forwarded", "value\r\nSet-Cookie: evil=1");
    assert(rc == -1);
    assert(res.header_count == 0);
    
    printf("[OK] test_crlf_injection\n");
}

static void test_reserved_header_override() {
    http_response_t res;
    http_response_init(&res);
    
    http_response_set_body(&res, "hello", 5, "text/plain");
    // Attempt to forge a bogus content length
    http_response_add_header(&res, "Content-Length", "999");

    size_t head_len;
    char *head = http_response_serialize_head(&res, 0, &head_len);
    
    assert(head != NULL);
    assert(strstr(head, "Content-Length: 5\r\n") != NULL);
    assert(strstr(head, "Content-Length: 999\r\n") == NULL); // Bogus header stripped
    
    free(head);
    http_response_free(&res);
    printf("[OK] test_reserved_header_override\n");
}

static void test_set_error() {
    http_response_t res;
    http_response_init(&res);
    
    http_response_set_error(&res, HTTP_STATUS_NOT_FOUND);
    
    assert(res.status == HTTP_STATUS_NOT_FOUND);
    assert(res.body_len > 0);
    assert(strstr(res.body, "404 Not Found") != NULL);
    
    http_response_free(&res);
    printf("[OK] test_set_error\n");
}

int main() {
    printf("Running Response Unit Tests...\n");
    test_serialize_happy_path();
    test_crlf_injection();
    test_reserved_header_override();
    test_set_error();
    printf("All response tests passed successfully.\n");
    return 0;
}