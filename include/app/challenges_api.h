#ifndef CHALLENGES_API_H
#define CHALLENGES_API_H

#include "http/parser.h"
#include "http/response.h"

/*
 * GET /api/challenges
 * Returns a JSON array of all published challenges for the current event.
 * Excludes descriptions to save bandwidth on the listing page.
 */
void challenge_list_handler(const http_request_t *req, http_response_t *res);

/*
 * GET /api/challenges/<slug>
 * Returns a single JSON object containing the full challenge details, 
 * including the Markdown description.
 */
void challenge_get_handler(const http_request_t *req, http_response_t *res);

/*
 * POST /api/challenges/<slug>/submit
 * Expects the flag as plain text in the HTTP request body.
 * Returns a 200 OK with {"correct": true/false}, or a 429 if rate-limited.
 */
void challenge_submit_handler(const http_request_t *req, http_response_t *res);

#endif