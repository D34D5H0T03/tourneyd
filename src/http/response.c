#include "http/response.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define SERVER_NAME "tourneyd"

// Internal buffer allocations
#define ERROR_PAGE_BUFFER_SIZE 256
#define DATE_STR_BUFFER_SIZE 64
#define HEAD_BASE_CAPACITY 512

// Space required for ": " and "\r\n" when formatting custom headers
#define HEADER_FORMAT_OVERHEAD 4

// helpers
// -1 on invalid, 0 on valid
static int header_is_valid(const char *name, const char *value)
{
    if (!name || !value || name[0] == '\0')
    {
        return -1; // empty header
    }
    if (strpbrk(name, "\r\n: \t"))
    {
        return -1; // header naming protocol check
    }
    if (strpbrk(value, "\r\n"))
    {
        return -1; // CRLF injection defense
    }

    // size checks for buffer overflow defense
    if (strlen(name) >= MAX_HEADER_NAME)
    {
        return -1;
    }
    if (strlen(value) >= MAX_HEADER_VALUE)
    {
        return -1;
    }

    return 0; // header valid
}

// replaces header if it exists, otherwise append it
static int set_header(http_response_t *res, const char *name, const char *value)
{
    if (header_is_valid(name, value) < 0)
    {
        return -1; // invalid header
    }

    // replace header
    for (int i = 0; i < res->header_count; i++)
    {
        if (strcasecmp(res->headers[i].name, name) == 0)
        {
            snprintf(res->headers[i].value, sizeof(res->headers[i].value), "%s", value);
            return 0;
        }
    }

    return http_response_add_header(res, name, value);
}

// appends formatted text to buffer 1 on success, 0 if too large
static int append(char *buffer, size_t cap, size_t *len, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    int n = vsnprintf(buffer + *len, cap - *len, fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n >= cap - *len)
    {
        return 0; // truncated data
    }
    *len += (size_t)n; // update data size;
    return 1;
}

// Headers that serializers writes. handlers cannot override them or it will break HTTP framing
// returns true(1) if reserved and false(0) if not
static int is_reserved(const char *name)
{
    // declares that both the strings and the array is immutable
    static const char *const reserved[] = {
        "Content-Length",
        "Connection",
        "Date",
        "Server"};

    for (size_t i = 0; i < sizeof(reserved) / sizeof(reserved[0]); i++)
    {
        if (strcasecmp(name, reserved[i]) == 0)
            return 1;
    }
    return 0;
}

// Checks if a header is already present (case-insensitive)
static int has_header(const http_response_t *res, const char *name)
{
    for (int i = 0; i < res->header_count; i++)
    {
        if (strcasecmp(res->headers[i].name, name) == 0)
            return 1;
    }
    return 0;
}

// Only adds the header if the handler hasn't explicitly set it
static void add_default(http_response_t *res, const char *name, const char *value)
{
    if (!has_header(res, name))
    {
        http_response_add_header(res, name, value);
    }
}

// public API

// initialize the response struct as empty
void http_response_init(http_response_t *res)
{
    memset(res, 0, sizeof(*res));
    res->status = HTTP_STATUS_OK;
}

// free the body of response
void http_response_free(http_response_t *res)
{
    free(res->body);
    res->body = NULL;
    res->body_len = 0;
}

int http_response_add_header(http_response_t *res, const char *name, const char *value)
{
    // guard clauses
    if (header_is_valid(name, value) < 0)
    {
        return -1;
    }
    if (res->header_count >= MAX_RESP_HEADERS)
    {
        return -1;
    }

    // adding a header
    http_header_t *h = &res->headers[res->header_count++];
    snprintf(h->name, sizeof(h->name), "%s", name);
    snprintf(h->value, sizeof(h->value), "%s", value);

    return 0; // sucessfully added header
}

int http_response_set_body(http_response_t *res, const void *data, size_t len, const char *content_type)
{
    char *copy = NULL;

    if (len > 0)
    {
        copy = malloc(len); // allocate memory as given by length of data
        if (!copy)
            return -1; // malloc failed
        memcpy(copy, data, len);
    }

    if (content_type && set_header(res, "Content-Type", content_type) < 0)
    {
        free(copy);
        return -1; // header setting error
    }

    free(res->body); // if previously freed it calls free(NULL) which is harmless
    res->body = copy;
    res->body_len = len;
    return 0;
}

void http_response_set_error(http_response_t *res, http_status_t code)
{
    // error page
    char page[ERROR_PAGE_BUFFER_SIZE];

    int n = snprintf(page, sizeof(page),
                     "<!DOCTYPE html><html><body><h1>%d %s</h1></body></html>\n",
                     (int)code, http_status_reason(code));

    if (n < 0 || (size_t)n >= sizeof(page))
    {
        n = 0;
    }

    http_response_free(res);
    res->status = code;
    http_response_set_body(res, page, (size_t)n, "text/html; charset=utf-8");
}

char *http_response_serialize_head(const http_response_t *res, int keep_alive, size_t *out_len)
{

    // worst case allocation: status line + 4 auto headers + all custom headers
    size_t cap = HEAD_BASE_CAPACITY + (size_t)res->header_count *
                                          (MAX_HEADER_NAME + MAX_HEADER_VALUE + HEADER_FORMAT_OVERHEAD);

    char *buffer = malloc(cap);

    if (!buffer)
        return NULL; // memory allocation failed

    size_t len = 0;

    char date[DATE_STR_BUFFER_SIZE];

    time_t now = time(NULL);
    struct tm tm; // thread safe time info
    gmtime_r(&now, &tm);

    // RfC 1123 time convention
    strftime(date, sizeof(date), "%a, %d %b %Y %H:%M:%S GMT", &tm);

    // appending the request
    int ok =
        append(buffer, cap, &len, "HTTP/1.1 %d %s\r\n", (int)res->status,
               http_status_reason(res->status)) &&
        append(buffer, cap, &len, "Date: %s\r\n", date) &&
        append(buffer, cap, &len, "Server: " SERVER_NAME "\r\n") &&
        append(buffer, cap, &len, "Content-Length: %zu\r\n", res->body_len) &&
        append(buffer, cap, &len, "Connection: %s\r\n",
               keep_alive ? "keep-alive" : "close");

    for (int i = 0; ok && i < res->header_count; i++)
    {
        if (is_reserved(res->headers[i].name))
            continue;
        ok = append(buffer, cap, &len, "%s: %s\r\n",
                    res->headers[i].name, res->headers[i].value);
    }

    ok = ok && append(buffer, cap, &len, "\r\n"); // blank line separates head from body

    if (!ok)
    {
        free(buffer);
        return NULL;
    }

    *out_len = len;
    return buffer;
}

// Injects baseline security headers for every response
void http_response_add_security_headers(http_response_t *res)
{
    add_default(res, "X-Content-Type-Options", "nosniff");
    add_default(res, "X-Frame-Options", "DENY");
    add_default(res, "Referrer-Policy", "no-referrer");
    add_default(res, "Permissions-Policy", "camera=(), microphone=(), geolocation=()");
    add_default(res, "Content-Security-Policy",
                "default-src 'self'; img-src 'self' data:; base-uri 'none'; "
                "form-action 'self'; frame-ancestors 'none'");
}