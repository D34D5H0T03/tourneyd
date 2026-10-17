#include "core/server.h"
#include "core/signals.h"
#include "net/listener.h"
#include "concurrency/concurrency.h"
#include "connections/http_connection.h"
#include "http/router.h"
#include "app/routes.h"
#include "util/log.h"
#include "app/static_files.h"


#include <signal.h>
#include <unistd.h>

static int server_fd = -1;
static const config_t *server_cfg = NULL;

static http_conn_opts_t http_opts;
static concurrency_opts_t conc_opts;

int server_init(const config_t *cfg) {
    server_cfg = cfg;

    if (signals_install() < 0) {
        log_error("Failed to install signal handlers");
        return -1;
    }

    // passing server config for http options
    http_opts.limits.max_body_size = cfg->max_body_size;
    http_opts.limits.max_header_bytes = cfg->max_header_bytes;
    http_opts.idle_timeout_sec = cfg->idle_timeout_sec;
    http_opts.request_timeout_sec = cfg->request_timeout_sec;
    http_opts.max_requests_per_conn = cfg->max_requests_per_conn;
    http_opts.trust_proxy = cfg->trust_proxy;

    // server config for concurrency
    conc_opts.max_workers = cfg->max_children;
    conc_opts.shutdown_grace_sec = cfg->shutdown_grace_sec;

    // Initialize the doc root directory path
    if (static_files_init(cfg->doc_root) < 0) {
        log_error("Fatal: failed to initialize static document root: %s", cfg->doc_root);
        return -1;
    }

    router_init();
    app_register_routes();
    // static_files_init(cfg->doc_root); <-- Added in Phase A4

    log_info("Starting tourneyd on port %s...", cfg->port);
    server_fd = listener_create(cfg->bind_addr, cfg->port, cfg->backlog);
    
    if (server_fd < 0) {
        log_error("Fatal: failed to initialize listener.");
        return -1;
    }
    
    log_info("Successfully listening on port %s", cfg->port);
    return 0;
}

int server_run(void) {
    if (server_fd < 0 || !server_cfg) return -1;
    return concurrency_run(server_fd, http_connection_handle, &http_opts, &conc_opts);
}

void server_shutdown(void) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    concurrency_shutdown();
}