#include "util/slug.h"
// same constraint as the database
int slug_valid(const char *s, size_t n) {
    if (n == 0 || n > SLUG_MAX) return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
    }
    return 1;
}