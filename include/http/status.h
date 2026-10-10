#ifndef STATUS_H
#define STATUS_H

typedef enum {
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_PAYLOAD_TOO_LARGE = 413,
    HTTP_STATUS_URI_TOO_LONG = 414,
    HTTP_STATUS_HEADER_TOO_LARGE = 431,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_VERSION_NOT_SUPPORTED = 505
} http_status_t;

// Helper function to get the human-readable string for the HTTP response line
// e.g., http_status_reason(HTTP_STATUS_BAD_REQUEST) returns "Bad Request"
const char* http_status_reason(http_status_t status);

#endif // STATUS_H