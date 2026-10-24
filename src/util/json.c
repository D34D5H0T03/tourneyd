#include "util/json.h"
#include "util/utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Configuration & Constants ---
#define JSON_INIT_CAP 256
#define ASCII_MIN_PRINTABLE 0x20
#define ASCII_DEL 0x7F
#define ASCII_THRESHOLD 0x80

// Unicode Replacement Character (U+FFFD) encoded in UTF-8
#define JSON_REPLACEMENT_CHAR "\xEF\xBF\xBD"
#define JSON_REPLACEMENT_LEN 3

// Formatting constants
#define HEX_SHIFT 4
#define HEX_MASK 0x0F
#define UNICODE_ESCAPE_LEN 6
#define JSON_TRUE_LEN 4
#define JSON_FALSE_LEN 5
#define JSON_NULL_LEN 4

// Core memory appending function. Grows the buffer dynamically.
static void put(json_t *j, const char *s, size_t n)
{
    if (j->err)
        return;

    // If the new data exceeds capacity (accounting for NUL byte)
    if (j->len + n + 1 > j->cap)
    {
        size_t cap = j->cap ? j->cap : JSON_INIT_CAP;
        while (cap < j->len + n + 1)
            cap *= 2;

        char *p = realloc(j->buf, cap);
        if (!p)
        {
            j->err = 1; // Mark the entire JSON object as failed on OOM
            return;
        }
        j->buf = p;
        j->cap = cap;
    }

    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

// Single character append helper
static void put_c(json_t *j, char c)
{
    put(j, &c, 1);
}

/*
 * The State Machine: Runs before every key and every value.
 * Determines if a comma is needed based on the current depth and position.
 */
static void sep(json_t *j)
{
    // If just printed a key, the value MUST follow immediately (no comma).
    if (j->after_key)
    {
        j->after_key = 0;
        return;
    }

    if (j->depth > 0)
    {
        // If this is the very first element at this depth, mark it false and skip the comma.
        if (j->first[j->depth - 1])
        {
            j->first[j->depth - 1] = 0;
        }
        else
        {
            // Otherwise, we are adding to an existing list/object, so prepend a comma.
            put_c(j, ',');
        }
    }
}

// Helper to open an object '{' or array '['
static void open_(json_t *j, char c)
{
    sep(j);
    if (j->depth >= JSON_MAX_DEPTH)
    {
        j->err = 1;
        return;
    }
    put_c(j, c);
    j->first[j->depth++] = 1; // Next item added at this new depth will be the first
}

// Helper to close an object '}' or array ']'
static void close_(json_t *j, char c)
{
    if (j->depth == 0)
    {
        j->err = 1;
        return;
    }
    j->depth--;
    put_c(j, c);
}

// zero initialisation
void json_init(json_t *j){ 
    memset(j, 0, sizeof *j); 
}

// memory cleanup
void json_free(json_t *j)
{
    free(j->buf);
    memset(j, 0, sizeof *j);
}

// arr and object boundary helpers
void json_obj_begin(json_t *j){ 
    open_(j, '{'); 
}
void json_obj_end(json_t *j){ 
    close_(j, '}'); 
}
void json_arr_begin(json_t *j){ 
    open_(j, '['); 
}

void json_arr_end(json_t *j){ 
    close_(j, ']'); 
}

// The string writer. Defends against XSS and broken binary payloads.
static void write_string(json_t *j, const char *s, size_t n)
{
    static const char HEX[] = "0123456789abcdef";
    put_c(j, '"'); // string start

    for (size_t i = 0; i < n;)
    {
        unsigned char c = (unsigned char)s[i];

        // UTF-8 Validation Layer: Handle all non-ASCII bytes
        if (c >= ASCII_THRESHOLD)
        {
            size_t k = utf8_seq_len(s + i, n - i);
            if (k == 0)
            {
                // Invalid sequence detected. Strip it and inject the standard replacement character.
                put(j, JSON_REPLACEMENT_CHAR, JSON_REPLACEMENT_LEN);
                i++;
            }
            else
            {
                // Valid multi-byte character. Copy it directly.
                put(j, s + i, k);
                i += k;
            }
            continue;
        }

        // ASCII Escaping Layer
        switch (c)
        {
        case '"':
            put(j, "\\\"", 2);
            break;
        case '\\':
            put(j, "\\\\", 2);
            break;
        case '\n':
            put(j, "\\n", 2);
            break;
        case '\r':
            put(j, "\\r", 2);
            break;
        case '\t':
            put(j, "\\t", 2);
            break;
        default:
            // Catch unprintable control characters (e.g., \x01) and escape them as \u00XX
            if (c < ASCII_MIN_PRINTABLE || c == ASCII_DEL)
            {
                char e[UNICODE_ESCAPE_LEN] = {
                    '\\', 'u', '0', '0',
                    HEX[c >> HEX_SHIFT],
                    HEX[c & HEX_MASK]};
                put(j, e, UNICODE_ESCAPE_LEN);
            }
            else
            {
                put_c(j, (char)c);
            }
        }
        i++;
    }
    put_c(j, '"'); // string end
}

void json_key(json_t *j, const char *key)
{
    sep(j);
    write_string(j, key, strlen(key));
    put_c(j, ':');
    j->after_key = 1; // Set flag so the upcoming value doesn't print a comma
}

void json_str(json_t *j, const char *s)
{
    sep(j);
    write_string(j, s ? s : "", s ? strlen(s) : 0);
}

void json_str_n(json_t *j, const char *s, size_t n)
{
    sep(j);
    write_string(j, s, n);
}

void json_int(json_t *j, long long v)
{
    sep(j);
    char t[32];
    int n = snprintf(t, sizeof t, "%lld", v);
    put(j, t, (size_t)n);
}

void json_bool(json_t *j, int v)
{
    sep(j);
    put(j, v ? "true" : "false", v ? JSON_TRUE_LEN : JSON_FALSE_LEN);
}

void json_null(json_t *j)
{
    sep(j);
    put(j, "null", JSON_NULL_LEN);
}