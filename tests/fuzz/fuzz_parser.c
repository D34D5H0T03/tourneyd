#include "http/parser.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #c, __FILE__, __LINE__); abort(); } } while (0)

static const http_limits_t LIMITS = { 64 * 1024, 16 * 1024 };

static size_t parse_whole(http_request_t *r, const uint8_t *d, size_t n) {
    size_t used = http_parse(r, (const char *)d, n);
    CHECK(used <= n);
    return used;
}

/* The same bytes in small pieces, the way http_connection.c feeds the parser. */
static size_t parse_pieces(http_request_t *r, const uint8_t *d, size_t n) {
    char *pending = malloc(n);
    size_t have = 0, fed = 0, total = 0, i = 0;
    while (fed < n && r->state != PARSE_DONE && r->state != PARSE_ERROR) {
        size_t step = 1 + d[i++ % n] % 11;           /* chunk sizes come from the input itself */
        if (step > n - fed) step = n - fed;
        memcpy(pending + have, d + fed, step);
        have += step; fed += step;
        size_t used = http_parse(r, pending, have);
        CHECK(used <= have);
        memmove(pending, pending + used, have - used);
        have -= used; total += used;
    }
    free(pending);
    return total;
}

static void no_control_chars(const char *s, int allow_tab) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        CHECK(!(c < 0x20 && !(allow_tab && c == '\t')) && c != 0x7f);
    }
}

static void invariants(const http_request_t *r) {
    CHECK(strlen(r->path) < MAX_PATH_LENGTH);
    CHECK(strlen(r->query) < MAX_QUERY_LENGTH);
    CHECK(strlen(r->version) < MAX_VERSION_LENGTH);
    CHECK(r->header_count >= 0 && r->header_count <= MAX_HEADERS);
    CHECK(r->body_received <= r->content_length);
    CHECK(r->content_length <= LIMITS.max_body_size);
    no_control_chars(r->path, 0);
    no_control_chars(r->query, 0);
    for (int i = 0; i < r->header_count; i++) {
        CHECK(strlen(r->headers[i].name) < MAX_HEADER_NAME && r->headers[i].name[0] != '\0');
        CHECK(strlen(r->headers[i].value) < MAX_HEADER_VALUE);
        no_control_chars(r->headers[i].name, 0);
        no_control_chars(r->headers[i].value, 1);
    }
    if (r->state == PARSE_DONE) {
        CHECK(r->error_status == 0);
        CHECK(r->path[0] == '/' && r->method != METHOD_UNKNOWN);
        CHECK(r->body_received == r->content_length);
        if (r->content_length) CHECK(r->body && r->body[r->content_length] == '\0');
    }
    if (r->state == PARSE_ERROR) {
        switch (r->error_status) {
        case HTTP_STATUS_BAD_REQUEST:   case HTTP_STATUS_PAYLOAD_TOO_LARGE:
        case HTTP_STATUS_URI_TOO_LONG:  case HTTP_STATUS_HEADER_TOO_LARGE:
        case HTTP_STATUS_INTERNAL_SERVER_ERROR: case HTTP_STATUS_NOT_IMPLEMENTED:
        case HTTP_STATUS_VERSION_NOT_SUPPORTED: break;
        default: CHECK(!"unexpected error status");
        }
    }
}

static void same(const http_request_t *a, const http_request_t *b) {
    CHECK(a->state == b->state && a->error_status == b->error_status);
    CHECK(a->method == b->method);
    CHECK(strcmp(a->path, b->path) == 0 && strcmp(a->query, b->query) == 0);
    CHECK(strcmp(a->version, b->version) == 0);
    CHECK(a->header_count == b->header_count);
    for (int i = 0; i < a->header_count; i++) {
        CHECK(strcmp(a->headers[i].name,  b->headers[i].name)  == 0);
        CHECK(strcmp(a->headers[i].value, b->headers[i].value) == 0);
    }
    CHECK(a->content_length == b->content_length && a->body_received == b->body_received);
    if (a->body_received) CHECK(memcmp(a->body, b->body, a->body_received) == 0);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size == 0 || size > 32 * 1024) return 0;
    http_request_t *a = calloc(1, sizeof *a), *b = calloc(1, sizeof *b);
    CHECK(a && b);
    http_request_init(a, &LIMITS);
    http_request_init(b, &LIMITS);
    size_t ua = parse_whole(a, data, size);
    size_t ub = parse_pieces(b, data, size);
    CHECK(ua == ub);
    invariants(a);
    same(a, b);
    http_request_free(a); http_request_free(b);
    free(a); free(b);
    return 0;
}