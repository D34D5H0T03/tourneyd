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
        return;
    }

    char decoded[MAX_PATH_LENGTH];
    if (http_url_decode(req->path, decoded, sizeof(decoded)) < 0){
        http_response_set_error(res, HTTP_STATUS_BAD_REQUEST);
        return;
    }

     // Hide dotfiles (.git, .env) and any ".." segment before touching the disk. 
     // further directory traversal squash defense
    if (strstr(decoded, "/.")) {
         http_response_set_error(res, HTTP_STATUS_NOT_FOUND);
         return;
    }

    // directory index rule. automatically serves index.html if user requests a directory
    size_t dlen = strlen(decoded);
    char want[PATH_MAX];
    int n = snprintf(want, sizeof(want), "%s%s%s", root, decoded,
                     (dlen > 0 && decoded[dlen - 1] == '/') ? "index.html" : "");

    if (n < 0 || (size_t)n >= sizeof want){
        http_response_set_error(res, HTTP_STATUS_NOT_FOUND);
        return;
    }

    char real[PATH_MAX];
    if (!realpath(want, real) || !is_inside_root(real)) {   // the traversal defense 
        http_response_set_error(res, HTTP_STATUS_NOT_FOUND); 
        return;
    }

    // opens the file descriptor in read only and close on execute
    int fd = open(real, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        http_response_set_error(res, HTTP_STATUS_NOT_FOUND);
        return; 
    }

    // checks if it is a regular file(.html / .css / .txt) or not
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) {        
        close(fd);
        http_response_set_error(res, HTTP_STATUS_NOT_FOUND);
        return;
    }

    // checks size of file
    if ((size_t)st.st_size > STATIC_MAX_FILE) {
        close(fd);
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }

    size_t size = (size_t)st.st_size, got = 0;
    char *buf = malloc(size ? size : 1); // if size is 0, malloc(1)
    if (!buf) { // out of memory
        close(fd);
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return; 
    }

    // reads even if the operation is interrupted
    while(got < size) {
        ssize_t r = read(fd, buf + got, size - got);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        got += (size_t)r;
    }

    close(fd);

    // corruption check to see if the bytes read is equal to the bytes at check
    if(got != size) {                                        
        free(buf);
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }

    // handoff to response builder
    if(http_response_set_body(res, buf, got, http_mime_from_path(real)) < 0){
        http_response_set_error(res, HTTP_STATUS_INTERNAL_SERVER_ERROR);
    }
    else{
        http_response_add_header(res, "X-Content-Type-Options", "nosniff");
    }
    free(buf);
}