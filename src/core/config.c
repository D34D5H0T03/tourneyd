#include "core/config.h"
#include <stddef.h>

void config_defaults(config_t *cfg) {
    cfg->bind_addr = "127.0.0.1"; // bind to localhost for cloudflare
    cfg->port = "8080"; // default port is set to 8080
    cfg->backlog = 128;
    cfg->doc_root = "./public";

    cfg->max_children = 64; // limits active fork() child process spawning
    cfg->idle_timeout_sec = 10; // max wait time for keep-alive connections
    cfg->request_timeout_sec = 15; // max time to recieve a full request
    cfg->max_requests_per_conn = 100; // max pipelined requests before force closed

    cfg->max_body_size = 1024 * 1024; // 1MB cap
    cfg->max_header_bytes = 16 * 1024; // 16KB cap

    cfg->shutdown_grace_sec = 10; // SIGTERM drain time (time for server process to terminate)
    cfg->trust_proxy = 0; // 0 = off, 1 = on
}

int config_parse_args(config_t *cfg, int argc, char **argv) {
    (void)cfg; (void)argc; (void)argv;
    // parse flags like -p 8080 or -d /var/www here later
    return 0; 
}