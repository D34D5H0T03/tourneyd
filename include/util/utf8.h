#ifndef UTF8_H
#define UTF8_H
#include <stddef.h>
int utf8_valid(const char *s, size_t n);      /* 1 = valid, 0 = not */
#endif