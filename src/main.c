#include <stdio.h>
#include <unistd.h>

#include "../include/net/listener.h"

// debugging and testing code
int main(void) {
    const char *port = "8080";
    int backlog = 128; 

    printf("Starting tourneyd on port %s...\n", port);
    
    int listen_fd = listener_create(port, backlog);
    if (listen_fd < 0) {
        fprintf(stderr, "Fatal: failed to initialize listener.\n");
        return 1;
    }

    printf("Successfully listening on port %s (fd: %d)\n", port, listen_fd);
    printf("Run 'ss -tlnp | grep 8080' in another terminal to verify.\n");
    printf("Press Ctrl+C to exit.\n");

    // infinite sleep so the process stays alive to hold the socket open
    while (1) {
        sleep(10);
    }

    close(listen_fd);
    return 0;
}