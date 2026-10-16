#ifndef CONFIG_H
#define CONFIG_H

#include <stdio.h>

typedef struct {
    const char *bind_addr; // IP address to bind to
    const char *port; // Port to bind to
    int backlog; // maximum TCP handshake completion
    const char *doc_root; // public directory for frontend

    int max_children; // limiting multi-processing
    int idle_timeout_sec;
    int request_timeout_sec;
    int max_requests_per_conn;

    size_t max_body_size; //max body size for HTTP responses and requests
    size_t max_header_bytes; // http headers

    int shutdown_grace_sec; 
    int trust_proxy;
} config_t;

void config_defaults(config_t *cfg);
int config_parse_args(config_t *cfg, int argc, char **argv);

#endif // CONFIG_H