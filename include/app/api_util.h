#ifndef API_UTIL_H
#define API_UTIL_H

#include "http/parser.h"
#include "http/response.h"
#include "util/json.h"
#include "util/slug.h"

#define API_EVENT "practice"

void api_send_json(http_response_t *res, http_status_t status, const json_t *j);
void api_error(http_response_t *res, http_status_t status, const char *message);
long api_query_long(const http_request_t *req, const char *key, long def, long lo, long hi);
void api_trim(char *s);
int  api_route_slug(const char *path, const char *prefix, const char *suffix, char out[SLUG_MAX + 1]);

#endif