#include "core/server.h"
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

int server_init(const config_t *cfg) {
    signal(SIGPIPE, SIG_IGN);

    // Initialize the doc root directory path
    if (static_files_init(cfg->doc_root) < 0) {
        log_error("Fatal: failed to initialize static document root: %s", cfg->doc_root);
        return -1;
    }

    router_init();
    app_register_routes();
    // static_files_init(cfg->doc_root); <-- Added in Phase A4

    log_info("Starting tourneyd on port %s...", cfg->port);
    server_fd = listener_create(cfg->port, cfg->backlog);
    
    if (server_fd < 0) {
        log_error("Fatal: failed to initialize listener.");
        return -1;
    }
    
    log_info("Successfully listening on port %s", cfg->port);
    return 0;
}

int server_run(void) {
    if (server_fd < 0) return -1;
    return concurrency_run(server_fd, http_connection_handle);
}

void server_shutdown(void) {
    if (server_fd >= 0) {
        close(server_fd);
        server_fd = -1;
    }
    concurrency_shutdown();
}