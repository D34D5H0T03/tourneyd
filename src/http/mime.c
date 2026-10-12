#include "http/mime.h"
#include <string.h>
#include <strings.h>
 
// Multipurpose Internet Mail Extensions
// It tells the browswer how to render
const char *http_mime_from_path(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    dot++; // skip the '.'
    
    if (!strcasecmp(dot, "html") || !strcasecmp(dot, "htm")) return "text/html; charset=utf-8";
    if (!strcasecmp(dot, "css")) return "text/css; charset=utf-8";
    if (!strcasecmp(dot, "js")) return "text/javascript; charset=utf-8";
    if (!strcasecmp(dot, "json")) return "application/json; charset=utf-8";
    if (!strcasecmp(dot, "txt") || !strcasecmp(dot, "md")) return "text/plain; charset=utf-8";
    if (!strcasecmp(dot, "png")) return "image/png";
    if (!strcasecmp(dot, "jpg") || !strcasecmp(dot, "jpeg")) return "image/jpeg";
    if (!strcasecmp(dot, "svg")) return "image/svg+xml";
    if (!strcasecmp(dot, "ico")) return "image/x-icon";
    if (!strcasecmp(dot, "woff2")) return "font/woff2";
    if (!strcasecmp(dot, "pdf")) return "application/pdf";
    
    return "application/octet-stream";
}