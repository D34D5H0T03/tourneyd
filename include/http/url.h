#ifndef URL_H
#define URL_H

#include <stddef.h>

// Decodes %XX in place/to buffer. Returns length on success, -1 on error.
int http_url_decode(const char *in, char *out, size_t cap);

#endif // URL_H