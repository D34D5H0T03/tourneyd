#include "app/writeups_api.h"
#include "app/api_util.h"
#include "db/db.h"
#include "db/models/writeup_model.h"
#include <stdlib.h>

// Route Matching Constants
#define ROUTE_PREFIX_WRITEUPS "/api/writeups/"

// Pagination Constants
#define DEFAULT_LIST_LIMIT 20
#define MIN_LIST_LIMIT 1
#define DEFAULT_LIST_OFFSET 0
#define MIN_LIST_OFFSET 0
#define MAX_LIST_OFFSET 100000

// JSON Key Constants
#define JSON_KEY_SLUG "slug"
#define JSON_KEY_TITLE "title"
#define JSON_KEY_AUTHOR "author"
#define JSON_KEY_CREATED_AT "created_at"
#define JSON_KEY_EVENT "event"
#define JSON_KEY_CHALLENGE "challenge"
#define JSON_KEY_SOURCE_URL "source_url"
#define JSON_KEY_BODY_MD "body_md"
#define JSON_KEY_TOTAL "total"
#define JSON_KEY_OFFSET "offset"
#define JSON_KEY_LIMIT "limit"
#define JSON_KEY_WRITEUPS "writeups"

/*
 * Helper: Writes a string value if it exists, otherwise writes JSON `null`.
 * This keeps the API response clean and predictable for the frontend.
 */
static void key_str_or_null(json_t *j, const char *key, const char *value)
{
    json_key(j, key);
    if (value && value[0] != '\0')
    {
        json_str(j, value);
    }
    else
    {
        json_null(j);
    }
}

/*
 * Helper: Serializes the summary fields shared by both the list and get endpoints.
 */
static void summary_fields(json_t *j, const writeup_summary_t *s)
{
    json_key(j, JSON_KEY_SLUG);
    json_str(j, s->slug);
    json_key(j, JSON_KEY_TITLE);
    json_str(j, s->title);
    json_key(j, JSON_KEY_AUTHOR);
    json_str(j, s->author);
    json_key(j, JSON_KEY_CREATED_AT);
    json_int(j, s->created_at);

    // Optional relational fields
    key_str_or_null(j, JSON_KEY_EVENT, s->event_slug);
    key_str_or_null(j, JSON_KEY_CHALLENGE, s->challenge_slug);
    key_str_or_null(j, JSON_KEY_SOURCE_URL, s->source_url);
}

void writeup_list_handler(const http_request_t *req, http_response_t *res)
{
    // Safely extract and clamp pagination parameters to prevent DB abuse
    long limit = api_query_long(req, "limit", DEFAULT_LIST_LIMIT, MIN_LIST_LIMIT, WRITEUP_LIST_MAX);
    long offset = api_query_long(req, "offset", DEFAULT_LIST_OFFSET, MIN_LIST_OFFSET, MAX_LIST_OFFSET);

    // Allocate the list array on the heap (~50 KB) to protect the stack
    writeup_summary_t *rows = malloc(sizeof(*rows) * WRITEUP_LIST_MAX);
    if (!rows)
    {
        api_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR, "out of memory");
        return;
    }

    int n = 0;
    int total = 0;

    // The '0' flag ensures we only fetch published writeups
    if (writeup_list((int)offset, 0, rows, (int)limit, &n, &total) != DB_OK)
    {
        free(rows);
        api_error(res, HTTP_STATUS_SERVICE_UNAVAILABLE, "database unavailable");
        return;
    }

    json_t j;
    json_init(&j);
    json_obj_begin(&j);

    // Inject pagination metadata so the frontend knows when to disable "Next/Prev" buttons
    json_key(&j, JSON_KEY_TOTAL);
    json_int(&j, total);
    json_key(&j, JSON_KEY_OFFSET);
    json_int(&j, offset);
    json_key(&j, JSON_KEY_LIMIT);
    json_int(&j, limit);

    json_key(&j, JSON_KEY_WRITEUPS);
    json_arr_begin(&j);
    for (int i = 0; i < n; i++)
    {
        json_obj_begin(&j);
        summary_fields(&j, &rows[i]);
        json_obj_end(&j);
    }
    json_arr_end(&j);

    json_obj_end(&j);
    free(rows);

    api_send_json(res, HTTP_STATUS_OK, &j);
    json_free(&j);
}

void writeup_get_handler(const http_request_t *req, http_response_t *res)
{
    char slug[SLUG_MAX + 1];

    // Extract <slug> from "/api/writeups/<slug>" safely
    if (api_route_slug(req->path, ROUTE_PREFIX_WRITEUPS, NULL, slug) < 0)
    {
        api_error(res, HTTP_STATUS_NOT_FOUND, "no such writeup");
        return;
    }

    writeup_t w;
    switch (writeup_get(slug, &w))
    {
    case DB_OK:
        break;
    case DB_NOT_FOUND:
        api_error(res, HTTP_STATUS_NOT_FOUND, "no such writeup");
        return;
    default:
        api_error(res, HTTP_STATUS_SERVICE_UNAVAILABLE, "database unavailable");
        return;
    }

    json_t j;
    json_init(&j);
    json_obj_begin(&j);

    // Populate the shared summary fields
    summary_fields(&j, &w.s);

    // Append the massive Markdown body
    json_key(&j, JSON_KEY_BODY_MD);
    // Use json_str_n (passing explicit length) to avoid a costly strlen() on potentially huge texts
    json_str_n(&j, w.body_md, w.body_len);

    json_obj_end(&j);

    // Free the dynamically allocated Markdown text to prevent memory leaks
    writeup_free(&w);

    api_send_json(res, HTTP_STATUS_OK, &j);
    json_free(&j);
}