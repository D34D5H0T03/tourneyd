#include "util/hash.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <string.h>

// this is for checking flags with the stored salt
int flag_hash_with_salt(const char *flag, const uint8_t salt[FLAG_SALT_LEN], uint8_t out[FLAG_HASH_LEN]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new(); // message digest context
    if (!ctx) return -1;
    unsigned int len = 0;
    
    // Compute: SHA256(salt || flag)
    int ok = EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) == 1 && // passing the algorithm
             EVP_DigestUpdate(ctx, salt, FLAG_SALT_LEN) == 1 && // passing the salt
             EVP_DigestUpdate(ctx, flag, strlen(flag)) == 1 && // passing the flag
             EVP_DigestFinal_ex(ctx, out, &len) == 1 && len == FLAG_HASH_LEN; // get the hased value
             
    EVP_MD_CTX_free(ctx); //cleanup
    return ok ? 0 : -1;
}

// this is for new challenges
int flag_hash_new(const char *flag, uint8_t salt[FLAG_SALT_LEN], uint8_t out[FLAG_HASH_LEN]) {
    if (RAND_bytes(salt, FLAG_SALT_LEN) != 1) return -1; // secure random bytes for salts
    return flag_hash_with_salt(flag, salt, out);
}

// this is for checking the submitted flag
int flag_hash_equal(const uint8_t a[FLAG_HASH_LEN], const uint8_t b[FLAG_HASH_LEN]) {
    // Constant-time comparison to prevent cryptographic timing attacks
    return CRYPTO_memcmp(a, b, FLAG_HASH_LEN) == 0;
}