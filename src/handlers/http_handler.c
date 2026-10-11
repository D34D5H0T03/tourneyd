#include "handlers/http_handler.h"
#include "http/parser.h"
#include "util/log.h"
#include <stdio.h>
#include <string.h>

// Debug helper to print the parsed method
static const char* method_to_str(http_method_t m) {
    switch(m) {
        case METHOD_GET: return "GET";
        case METHOD_POST: return "POST";
        case METHOD_HEAD: return "HEAD";
        case METHOD_PUT: return "PUT";
        case METHOD_DELETE: return "DELETE";
        case METHOD_PATCH: return "PATCH";
        case METHOD_OPTIONS: return "OPTIONS";
        default: return "UNKNOWN";
    }
}

