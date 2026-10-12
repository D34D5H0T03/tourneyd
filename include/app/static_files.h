#ifndef STATIC_FILES_H
#define STATIC_FILES_H

#include "http/parser.h"
#include "http/response.h"

int static_files_init(const char *doc_root);
void static_files_handler(const http_request_t *req, http_response_t *res);

#endif