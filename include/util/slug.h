#ifndef SLUG_H
#define SLUG_H
#include <stddef.h>
#define SLUG_MAX 64
/* 1..SLUG_MAX characters from a-z, 0-9 and '-': the same rule as the schema's CHECK constraints. */
int slug_valid(const char *s, size_t n);
#endif