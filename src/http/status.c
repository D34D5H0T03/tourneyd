#include "http/status.h"

const char* http_status_reason(http_status_t status) {
    switch (status) {
        case HTTP_STATUS_OK: return "OK";
        case HTTP_STATUS_NO_CONTENT: return "No Content";
        case HTTP_STATUS_NOT_MODIFIED: return "Not Modified";
        case HTTP_STATUS_BAD_REQUEST: return "Bad Request";
        case HTTP_STATUS_NOT_FOUND: return "Not Found";
        case HTTP_STATUS_METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HTTP_STATUS_REQUEST_TIMEOUT: return "Request Timeout";
        case HTTP_STATUS_LENGTH_REQUIRED: return "Length Required";
        case HTTP_STATUS_PAYLOAD_TOO_LARGE: return "Payload Too Large";
        case HTTP_STATUS_URI_TOO_LONG: return "URI Too Long";
        case HTTP_STATUS_HEADER_TOO_LARGE: return "Request Header Fields Too Large";
        case HTTP_STATUS_INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HTTP_STATUS_NOT_IMPLEMENTED: return "Not Implemented";
        case HTTP_STATUS_SERVICE_UNAVAILABLE: return "Service Unavailable";
        case HTTP_STATUS_VERSION_NOT_SUPPORTED: return "HTTP Version Not Supported";
        default: return "Unknown Status";
    }
}