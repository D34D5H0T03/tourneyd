#ifndef JSON_H
#define JSON_H
#include <stddef.h>

// Flat JSON structures rarely need deep nesting. 
// Keeping this small saves memory on the stack.
#define JSON_MAX_DEPTH 8

typedef struct {
    char  *buf;                          /* NUL-terminated, dynamically grows as needed */
    size_t len;                          /* Current length of the JSON string */
    size_t cap;                          /* Current allocated capacity of the buffer */
    int    err;                          /* Set to 1 on allocation failure or depth overflow. Check once at the end. */
    int    depth;                        /* Current nesting level of objects/arrays */
    int    after_key;                    /* Flag: 1 if a key was just written, meaning the value follows without a comma */
    unsigned char first[JSON_MAX_DEPTH]; /* Tracks if the next item at a specific depth is the first item (needs no comma) */
} json_t;

void json_init(json_t *j);
void json_free(json_t *j);

void json_obj_begin(json_t *j);   
void json_obj_end(json_t *j);
void json_arr_begin(json_t *j);   
void json_arr_end(json_t *j);

void json_key(json_t *j, const char *key);

// Writes a standard NUL-terminated string. NULL becomes "".
void json_str(json_t *j, const char *s);                    

// Writes a string with an explicit length. Crucial for massive Markdown files to avoid strlen() overhead.
void json_str_n(json_t *j, const char *s, size_t n);        

void json_int(json_t *j, long long v);
void json_bool(json_t *j, int v);
void json_null(json_t *j);

#endif