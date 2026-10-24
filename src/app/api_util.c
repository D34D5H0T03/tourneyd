#include "app/api_util.h"
#include "http/url.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define LONG_BUFFER_SIZE 24

void api_send_json(http_response_t *res, http_status_t status, const json_t *j)
{
    if (j->err || !j->buf) // oom error
    {
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }
    // content-type header for json
    if (http_response_set_body(res, j->buf, j->len, "application/json; charset=utf-8") < 0)
    {
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }
    // no cache
    res->status = status;
    http_response_add_header(res, "Cache-Control", "no-store");
}

// sending error responses
void api_error(http_response_t *res, http_status_t status, const char *message)
{
    json_t j;
    json_init(&j);
    json_obj_begin(&j);
    json_key(&j, "error");
    json_str(&j, message);
    json_obj_end(&j);
    api_send_json(res, status, &j);
    json_free(&j);
}


long api_query_long(const http_request_t *req, const char *key, long def, long lo, long hi)
{
    char buf[LONG_BUFFER_SIZE]; // bytes necessary to store a 64 bit integer as a string
    if (http_query_get(req->query, key, buf, sizeof buf) < 0) // fail
        return def; // send default
    
    char *end;
    errno = 0;
    long v = strtol(buf, &end, 10); // get number from string
    if (errno || end == buf || *end != '\0')
        return def;
    return v < lo ? lo : (v > hi ? hi : v);
}

// trim responses of \n
void api_trim(char *s)
{
    size_t n = strlen(s), a = 0;
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
    while (s[a] && isspace((unsigned char)s[a]))
        a++;
    if (a)
        memmove(s, s + a, n - a + 1);
}

// extracts route slug
int api_route_slug(const char *path, const char *prefix, const char *suffix, char out[SLUG_MAX + 1])
{
    size_t plen = strlen(prefix);
    if (strncmp(path, prefix, plen) != 0)
        return -1;
    const char *s = path + plen, *slash = strchr(s, '/');
    size_t n = slash ? (size_t)(slash - s) : strlen(s);
    if (strcmp(slash ? slash : "", suffix ? suffix : "") != 0)
        return -1;
    if (!slug_valid(s, n))
        return -1;
    memcpy(out, s, n);
    out[n] = '\0';
    return 0;
}