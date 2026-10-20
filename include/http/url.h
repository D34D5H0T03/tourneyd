#ifndef URL_H
#define URL_H

#include <stddef.h>

// Decodes %XX in place/to buffer. Returns length on success, -1 on error.
int http_url_decode(const char *in, char *out, size_t cap);

/* Finds `key` in "a=1&b=two", percent-decodes the value ('+' means space here).
   Returns the value length, or -1 if absent or it does not fit. */
int http_query_get(const char *query, const char *key, char *out, size_t cap);

#endif // URL_H