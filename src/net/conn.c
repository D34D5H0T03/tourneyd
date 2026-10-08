#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>

#include "../../include/net/conn.h"

int conn_accept(int listen_fd, conn_t* out){
    out->peer_len = sizeof(out->peer); // setting capacity limit to 128 bytes

    while(1){
        // the pointer cast is to prevent type mismatch error
        out->fd = accept(listen_fd, (struct sockaddr *)&out->peer, &out->peer_len);

        if(out->fd >= 0){
            return 0; //success
        }

        if(errno == EINTR){
            continue; //kernel can inturrupt the accept() with SIGCHLD or SIGINT
        }

        return -1; // Fatal Error (e.g Network error)
    }
}

// reads up to n bytes form the TCP reliable pipe socket fd.
ssize_t conn_read(conn_t *connected_client, void* buffer, size_t n){
    while(1){
        // ssize_t is signed size type, so can send back -1
        ssize_t bytes_read = read(connected_client->fd, buffer, n); // reads bytes and sends back bytes read

        if(bytes_read >= 0){
            return bytes_read;
        }

        if(errno == EINTR){
            continue; //signal inturrption handling
        }

        return -1; // Network error
    }
}


int conn_write_all(conn_t* connected_client, const void* buffer, size_t n){
    size_t remaining = n;

    const char* p = buffer; // pointer to the buffer

    while(remaining > 0){
        ssize_t written = write(connected_client->fd, p, remaining);

        if(written <= 0){ //error
            if(errno == EINTR) continue;
            return -1;
        }

        p += written; // translates the pointer to the number of bytes written
        remaining -= written;
    }

    return 0; //success
}

// close the TCP secure socket.
void conn_close(conn_t* connected_client){
    if(connected_client->fd >= 0){
        close(connected_client->fd);
        connected_client->fd = -1;
    }
}

void conn_peer_str(const conn_t *connected_client, char *buffer, size_t n) {
    // getnameinfo converts the raw binary address into a printable string
    // NI_NUMERICHOST prevents it from doing a slow DNS lookup on the IP
    // NI_NUMERICSERV prevents it from looking up the port name (e.g., returns "80" instead of "http")
    if (getnameinfo((struct sockaddr *)&connected_client->peer, connected_client->peer_len, buffer, n,
                     NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
        strncpy(buffer, "unknown", n); // did not get the address.
    }
}