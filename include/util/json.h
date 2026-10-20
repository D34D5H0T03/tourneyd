#ifndef JSON_H
#define JSON_H

#include <stddef.h>

#define JSON_MAXIMUM_DEPTH 32

typedef struct {
    char *buf; // memory buffer
    size_t len; // current length
    size_t cap; // size of memory 
    int err; // error code
    int depth; // the depth 'level' in a nested json obejct
    int needs_comma[JSON_MAXIMUM_DEPTH]; // Tracks whether a comma is needed at the current nesting depth
} json_t;

void json_init(json_t *j);
void json_free(json_t *j);

void json_obj_begin(json_t *j);
void json_obj_end(json_t *j);
void json_arr_begin(json_t *j);
void json_arr_end(json_t *j);

void json_key(json_t *j, const char *key);
void json_str(json_t *j, const char *val);
void json_int(json_t *j, long val);
void json_bool(json_t *j, int val);
void json_null(json_t *j);

#endif