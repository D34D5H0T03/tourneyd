#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include "../include/net/listener.h"
#include "../include/net/conn.h"
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

    // The Accept Loop
    while (1) {
        conn_t client;
        
        if (conn_accept(listen_fd, &client) < 0) {
            continue; // Skip to the next client on EINTR or accept error
        }
        
        char ip_str[64];
        conn_peer_str(&client, ip_str, sizeof(ip_str));
        printf("Accepted connection from %s\n", ip_str);

        // This function will block the loop until the client disconnects
        echo_handle(&client);

        printf("Client %s disconnected.\n", ip_str);
        
        // Clean up the file descriptor so we don't leak memory
        conn_close(&client);
    }

    close(listen_fd);
    return 0;
}