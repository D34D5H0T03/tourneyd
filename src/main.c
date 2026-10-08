#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "../include/net/listener.h"
#include "../include/concurrency/concurrency.h"
#include "../include/handlers/echo.h"

int main(void) {
    // Ignore SIGPIPE. If we try to write to a client who suddenly disconnected, 
    // the OS sends SIGPIPE. The default behavior is to kill the server. 
    // Ignoring it makes write() return -1 instead, which we handle gracefully.
    signal(SIGPIPE, SIG_IGN);

    const char *port = "8080";
    int backlog = 128; 

    printf("Starting tourneyd on port %s...\n", port);
    
    int listen_fd = listener_create(port, backlog);
    if (listen_fd < 0) {
        fprintf(stderr, "Fatal: failed to initialize listener.\n");
        return 1;
    }

    printf("Successfully listening on port %s\n", port);

    // passing the file descriptor and the echo handler to the fucntion pointer
    concurrency_run(listen_fd, echo_handle);
    close(listen_fd);
    return 0;
}