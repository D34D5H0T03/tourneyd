#ifndef PARSER_H_INCLUDED
#define PARSER_H_INCLUDED

#include <stddef.h>

// strict fields to prevent buffer overflows or Dos attacks
#define MAX_HEADERS        50
#define MAX_HEADER_NAME    64
#define MAX_HEADER_VALUE   512
#define MAX_PATH_LENGTH    1024
#define MAX_METHOD_LENGTH  16
#define MAX_VERSION_LENGTH 16
#define MAX_LINE_LENGTH    4096
#define MAX_BODY_SIZE      (1024 * 1024)

// the parser states for the state machine
typedef enum{
    PARSE_REQ_LINE,
    PARSE_HEADERS,
    PARSE_BODY,
    PARSE_DONE,
    PARSE_ERROR,
} parser_state_t;

// http methods
typedef enum {
    METHOD_UNKNOWN,
    METHOD_GET,
    METHOD_POST,
    METHOD_HEAD,
    METHOD_PUT,
    METHOD_DELETE,
    METHOD_PATCH,
} http_method_t;

typedef struct {
    char name[MAX_HEADER_NAME];
    char value[MAX_HEADER_VALUE];
} http_header_t;

// the http method object created via parsing the request
typedef struct {
    http_method_t method;
    char path[MAX_PATH_LENGTH];
    char version[MAX_VERSION_LENGTH];

    http_header_t headers[MAX_HEADERS];
    int header_count;

    size_t content_length;
    size_t body_received;
    char  *body;              // malloc, need freeing and handle dangling pointer

    parser_state_t state;
    int error_status;         // 400, 413, 414, 431, 501, 505 when state == PARSE_ERROR
} http_request_t;

// initializes the http object
void http_request_init(http_request_t *req);

// frees the memory of the http object (because the body uses malloc)
void http_request_free(http_request_t *req);

// the core parser. returns number of bytes read
size_t http_parse(http_request_t *req, const char *buffer, size_t buffer_len);

// requests header
const char *http_request_header(const http_request_t *req, const char *name);

#endif // PARSER_H