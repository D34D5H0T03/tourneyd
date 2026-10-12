#include "app/static_files.h"
#include "http/mime.h"
#include "http/status.h"
#include "http/url.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define STATIC_MAX_FILE (8u * 1024 * 1024)

static char root[PATH_MAX];
static size_t root_len;

int static_files_init(const char *doc_root){
    if(!realpath(doc_root, root)) return -1;
    root_len = strlen(root);
    return 0;
}

// directory traversal defence, check if absolute path starts with public/ (the doc_root)
static int is_inside_root(const char *real){
    return strncmp(real, root, root_len) == 0 &&
            (real[root_len] == '/' || real[root_len] == '\0');
}

void static_files_handler(const http_request_t *req, http_response_t *res){
    if(req->method != METHOD_GET && req->method != METHOD_HEAD){
        http_response_set_error(res, HTTP_STATUS_METHOD_NOT_ALLOWED);
        http_response_add_header(res, "Allow", "GET, HEAD");
    }
}