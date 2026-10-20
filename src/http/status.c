#include "http/status.h"

const char* http_status_reason(http_status_t status) {
    switch (status) {
        case HTTP_STATUS_OK: return "OK"; // 200
        case HTTP_STATUS_NO_CONTENT: return "No Content"; // 204
        case HTTP_STATUS_NOT_MODIFIED: return "Not Modified"; // 304
        case HTTP_STATUS_BAD_REQUEST: return "Bad Request"; // 400
        case HTTP_STATUS_NOT_FOUND: return "Not Found"; // 404
        case HTTP_STATUS_METHOD_NOT_ALLOWED: return "Method Not Allowed"; // 405
        case HTTP_STATUS_REQUEST_TIMEOUT: return "Request Timeout"; // 408
        case HTTP_STATUS_LENGTH_REQUIRED: return "Length Required"; // 411
        case HTTP_STATUS_PAYLOAD_TOO_LARGE: return "Payload Too Large"; // 413
        case HTTP_STATUS_URI_TOO_LONG: return "URI Too Long"; // 414
        case HTTP_STATUS_TOO_MANY_REQUESTS: return "Too Many Requests"; //429
        case HTTP_STATUS_HEADER_TOO_LARGE: return "Request Header Fields Too Large"; // 431
        case HTTP_STATUS_INTERNAL_SERVER_ERROR: return "Internal Server Error"; // 500
        case HTTP_STATUS_NOT_IMPLEMENTED: return "Not Implemented"; // 501
        case HTTP_STATUS_SERVICE_UNAVAILABLE: return "Service Unavailable"; // 503
        case HTTP_STATUS_VERSION_NOT_SUPPORTED: return "HTTP Version Not Supported"; // 505
        default: return "Unknown Status";
    }
}