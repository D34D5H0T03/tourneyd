#ifndef WRITEUPS_API_H
#define WRITEUPS_API_H

#include "http/parser.h"
#include "http/response.h"

/*
 * GET /api/writeups?limit=<N>&offset=<M>
 * Returns a paginated JSON array of published writeup summaries.
 * Allows frontend clients to implement "Older/Newer" pagination.
 */
void writeup_list_handler(const http_request_t *req, http_response_t *res);

/*
 * GET /api/writeups/<slug>
 * Returns a single JSON object containing the full writeup details,
 * including the Markdown body.
 */
void writeup_get_handler(const http_request_t *req, http_response_t *res);

#endif