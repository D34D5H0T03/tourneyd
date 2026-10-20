#include "util/json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define JSON_INITIAL_CAP 1024
#define ASCII_MIN_PRINTABLE 0x20 // Space character (Start of printable ASCII)
#define ASCII_DEL 0x7F           // Delete character (End of standard printable ASCII)
#define JSON_UNICODE_FORMAT "\\u%04x"


void json_init(json_t *j)
{
    memset(j, 0, sizeof(*j));
    j->buf = malloc(JSON_INITIAL_CAP);
    if (!j->buf)
    {
        j->err = 1;
        return;
    }
    j->cap = JSON_INITIAL_CAP;
    j->buf[0] = '\0';
}

void json_free(json_t *j)
{
    free(j->buf);
    j->buf = NULL;
}

static void json_append(json_t *j, const char *data, size_t dlen)
{
    if (j->err)
        return;
    // capacity check
    if (j->len + dlen + 1 > j->cap)
    {
        /* if there isn't enough capacity, 
        reallocate new memory untill we reach the point
        where the incoming data can be appended*/
        size_t new_cap = j->cap * 2;
        while (j->len + dlen + 1 > new_cap)
            new_cap *= 2;
        char *new_buf = realloc(j->buf, new_cap);
        // realloc fail (out of memory)
        if (!new_buf)
        {
            j->err = 1;
            return;
        }
        j->buf = new_buf;
        j->cap = new_cap;
    }
    // appending the data
    memcpy(j->buf + j->len, data, dlen);
    j->len += dlen;
    j->buf[j->len] = '\0';
}

static void check_comma(json_t *j)
{
    if (j->err || j->depth < 0 || j->depth >= 32)
        return;
    if (j->needs_comma[j->depth])
        json_append(j, ",", 1);
    j->needs_comma[j->depth] = 1;
}

// object start with {
void json_obj_begin(json_t *j)
{
    check_comma(j);
    json_append(j, "{", 1);
    j->depth++;
}

// object end with }
void json_obj_end(json_t *j)
{
    j->depth--;
    json_append(j, "}", 1);
}

// array begin with [
void json_arr_begin(json_t *j)
{
    check_comma(j);
    json_append(j, "[", 1);
    j->depth++;
}

// array end with ]
void json_arr_end(json_t *j)
{
    j->depth--;
    json_append(j, "]", 1);
}

// the key part of the json key value pair
void json_key(json_t *j, const char *key)
{
    if (j->err)
        return;
    check_comma(j);
    j->needs_comma[j->depth] = 0; // The value that follows will provide its own output, no comma between key and value
    json_append(j, "\"", 1);
    json_append(j, key, strlen(key));
    json_append(j, "\":", 2);
}

// the values
void json_str(json_t *j, const char *val)
{
    if (j->err)
        return;
    check_comma(j);
    if (!val)
    {
        json_append(j, "null", 4);
        return;
    }

    // quotation
    json_append(j, "\"", 1);
    while (*val)
    {
        unsigned char c = (unsigned char)*val++;
        if (c == '"') // "
            json_append(j, "\\\"", 2);
        else if (c == '\\') /* \ */
            json_append(j, "\\\\", 2);
        else if (c == '\n')
            json_append(j, "\\n", 2);
        else if (c == '\r')
            json_append(j, "\\r", 2);
        else if (c == '\t')
            json_append(j, "\\t", 2);
        else if (c < ASCII_MIN_PRINTABLE || c == ASCII_DEL)
        {
            char hex[7];
            snprintf(hex, sizeof(hex), JSON_UNICODE_FORMAT, c);
            json_append(j, hex, 6);
        }
        else
        {
            json_append(j, (const char *)&c, 1);
        }
    }
    json_append(j, "\"", 1);
}

void json_int(json_t *j, long val)
{
    // casting the long value into a string buffer, max 32 digits
    char num[32];
    snprintf(num, sizeof(num), "%ld", val);
    check_comma(j);
    json_append(j, num, strlen(num));
}

void json_bool(json_t *j, int val)
{
    check_comma(j);
    if (val)
        json_append(j, "true", 4);
    else
        json_append(j, "false", 5);
}

void json_null(json_t *j)
{
    check_comma(j);
    json_append(j, "null", 4);
}