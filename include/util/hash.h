#ifndef HASH_H
#define HASH_H

#include <stdint.h>

#define FLAG_SALT_LEN 16
#define FLAG_HASH_LEN 32

int flag_hash_with_salt(const char *flag, const uint8_t salt[FLAG_SALT_LEN], uint8_t out[FLAG_HASH_LEN]);

/* random salt */
int flag_hash_new(const char *flag, uint8_t salt[FLAG_SALT_LEN], uint8_t out[FLAG_HASH_LEN]);

 /* constant time */
int flag_hash_equal(const uint8_t a[FLAG_HASH_LEN], const uint8_t b[FLAG_HASH_LEN]);         

#endif
