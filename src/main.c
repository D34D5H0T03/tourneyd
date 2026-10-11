#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "net/listener.h"
#include "concurrency/concurrency.h"
#include "handlers/http_handler.h"
#include "util/log.h"
#include "router/router.h"
#include "handlers/hello.h"

// default server configurations
#define DEFAULT_PORT "8080"
#define DEFAULT_BACKLOG 128

int main(void) {
    // Ignore SIGPIPE. If we try to write to a client who suddenly disconnected, 
    // the OS sends SIGPIPE. The default behavior is to kill the server. 
    // Ignoring it makes write() return -1 instead, which we handle gracefully.
    signal(SIGPIPE, SIG_IGN);

    const char *port = DEFAULT_PORT;
    int backlog = DEFAULT_BACKLOG; 

    router_init();
    router_add(METHOD_GET, "/", hello_handler);
    router_add(METHOD_POST, "/upper", upper_handler);

    log_info("Starting tourneyd on port %s...", port);
    
    int listen_fd = listener_create(port, backlog);
    if (listen_fd < 0) {
        log_error("Fatal: failed to initialize listener.");
        return 1;
    }

    log_info("Successfully listening on port %s", port);

    // passing the file descriptor and the echo handler to the fucntion pointer
    concurrency_run(listen_fd, http_connection_handle);
    close(listen_fd);
    return 0;
}