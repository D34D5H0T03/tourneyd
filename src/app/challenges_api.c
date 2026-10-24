#include "app/challenges_api.h"
#include "app/api_util.h"
#include "db/db.h"
#include "db/models/challenge_model.h"
#include <stdlib.h>
#include <string.h>

// Route Matching Constants
#define ROUTE_PREFIX_CHALLENGES "/api/challenges/"
#define ROUTE_SUFFIX_SUBMIT "/submit"

// JSON Key Constants ---
#define JSON_KEY_CHALLENGES "challenges"
#define JSON_KEY_SLUG "slug"
#define JSON_KEY_TITLE "title"
#define JSON_KEY_CATEGORY "category"
#define JSON_KEY_POINTS "points"
#define JSON_KEY_DESC "description"
#define JSON_KEY_CORRECT "correct"

void challenge_list_handler(const http_request_t *req, http_response_t *res)
{
    (void)req; // Suppress unused parameter warning

    // Allocate the list array on the heap (around 60 KB).
    // Putting this on the stack could cause a stack overflow in a threaded environment.
    challenge_summary_t *rows = malloc(sizeof(*rows) * CHALLENGE_LIST_MAX);
    if (!rows)
    {
        api_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR, "out of memory");
        return;
    }

    int n = 0;
    // API_EVENT is the hardcoded "practice" macro from api_util.h.
    // The '0' flag tells the model NOT to include unpublished/hidden challenges.
    if (challenge_list(API_EVENT, 0, rows, CHALLENGE_LIST_MAX, &n) != DB_OK)
    {
        free(rows);
        api_error(res, HTTP_STATUS_SERVICE_UNAVAILABLE, "database unavailable");
        return;
    }

    // Build the JSON response: {"challenges": [{"slug": "...", ...}, ...]}
    json_t j;
    json_init(&j);
    json_obj_begin(&j);
    json_key(&j, JSON_KEY_CHALLENGES);
    json_arr_begin(&j);

    for (int i = 0; i < n; i++)
    {
        json_obj_begin(&j);
        json_key(&j, JSON_KEY_SLUG);
        json_str(&j, rows[i].slug);
        json_key(&j, JSON_KEY_TITLE);
        json_str(&j, rows[i].title);
        json_key(&j, JSON_KEY_CATEGORY);
        json_str(&j, rows[i].category);
        json_key(&j, JSON_KEY_POINTS);
        json_int(&j, rows[i].points);
        json_obj_end(&j);
    }

    json_arr_end(&j);
    json_obj_end(&j);
    free(rows);

    // api_send_json automatically applies the Cache-Control: no-store header
    api_send_json(res, HTTP_STATUS_OK, &j);
    json_free(&j);
}

void challenge_get_handler(const http_request_t *req, http_response_t *res)
{
    char slug[SLUG_MAX + 1];

    // Extracts <slug> from "/api/challenges/<slug>".
    // If the path has a suffix or invalid characters, it returns -1 (404 Not Found).
    if (api_route_slug(req->path, ROUTE_PREFIX_CHALLENGES, NULL, slug) < 0)
    {
        api_error(res, HTTP_STATUS_NOT_FOUND, "no such challenge");
        return;
    }

    challenge_t c;
    switch (challenge_get(API_EVENT, slug, &c))
    {
    case DB_OK:
        break;
    case DB_NOT_FOUND:
        api_error(res, HTTP_STATUS_NOT_FOUND, "no such challenge");
        return;
    default:
        api_error(res, HTTP_STATUS_SERVICE_UNAVAILABLE, "database unavailable");
        return;
    }

    // Serialize the detailed object, including the Markdown description
    json_t j;
    json_init(&j);
    json_obj_begin(&j);
    json_key(&j, JSON_KEY_SLUG);
    json_str(&j, c.s.slug);
    json_key(&j, JSON_KEY_TITLE);
    json_str(&j, c.s.title);
    json_key(&j, JSON_KEY_CATEGORY);
    json_str(&j, c.s.category);
    json_key(&j, JSON_KEY_POINTS);
    json_int(&j, c.s.points);
    json_key(&j, JSON_KEY_DESC);
    json_str(&j, c.description);
    json_obj_end(&j);

    challenge_free(&c); // Free the dynamically allocated Markdown description

    api_send_json(res, HTTP_STATUS_OK, &j);
    json_free(&j);
}

void challenge_submit_handler(const http_request_t *req, http_response_t *res)
{
    char slug[SLUG_MAX + 1];

    // Extracts <slug> from "/api/challenges/<slug>/submit".
    if (api_route_slug(req->path, ROUTE_PREFIX_CHALLENGES, ROUTE_SUFFIX_SUBMIT, slug) < 0)
    {
        api_error(res, HTTP_STATUS_NOT_FOUND, "no such challenge");
        return;
    }

    size_t n = req->body_received;

    // Security check: Reject empty flags, flags over 256 bytes, or flags containing binary NUL bytes
    if (n == 0 || n > CHALLENGE_FLAG_MAX || memchr(req->body, '\0', n))
    {
        api_error(res, HTTP_STATUS_BAD_REQUEST, "send the flag as the plain-text request body (1-256 bytes)");
        return;
    }

    char flag[CHALLENGE_FLAG_MAX + 1];
    memcpy(flag, req->body, n);
    flag[n] = '\0';

    // Command-line tools like 'echo' often append trailing newlines. Trim them in-place.
    api_trim(flag);

    if (flag[0] == '\0')
    {
        api_error(res, HTTP_STATUS_BAD_REQUEST, "the flag is empty");
        return;
    }

    int correct = 0;
    switch (challenge_check_flag(API_EVENT, slug, flag, &correct))
    {
    case DB_OK:
        break;
    case DB_NOT_FOUND:
        api_error(res, HTTP_STATUS_NOT_FOUND, "no such challenge");
        return;
    case DB_RATE_LIMITED:
        // 429 Too Many Requests informs the frontend to disable the submit button
        api_error(res, HTTP_STATUS_TOO_MANY_REQUESTS, "too many attempts on this challenge, wait a minute");
        http_response_add_header(res, "Retry-After", "60");
        return;
    default:
        api_error(res, HTTP_STATUS_SERVICE_UNAVAILABLE, "database busy, try again");
        return;
    }

    // Note: A wrong flag is still a 200 OK request.
    // We send {"correct": false} rather than throwing a 4xx error.
    // This makes real server errors easier to spot in the logs.
    json_t j;
    json_init(&j);
    json_obj_begin(&j);
    json_key(&j, JSON_KEY_CORRECT);
    json_bool(&j, correct);
    json_obj_end(&j);

    api_send_json(res, HTTP_STATUS_OK, &j);
    json_free(&j);
}