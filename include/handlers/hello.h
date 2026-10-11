#ifndef HELLO_H
#define HELLO_H

#include "http/parser.h"
#include "http/response.h"

// Demo exact match handler (GET /)
void hello_handler(const http_request_t *req, http_response_t *res);

// Demo uppercase echo handler (POST /upper)
void upper_handler(const http_request_t *req, http_response_t *res);

#endif // HELLO_H