#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/time.h> // for timeouts
#include <sys/socket.h>

#include "net/conn.h"
#include "util/shutdown.h"
#include "util/clock.h"

#define MS_PER_SEC   1000
#define US_PER_MS    1000

int conn_accept(int listen_fd, conn_t *out){
    out->peer_len = sizeof(out->peer); // setting capacity limit to 128 bytes
    out->deadline_ms = 0; // setting default deadline to 0

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

void conn_set_deadline(conn_t *connected_client, int seconds){
    connected_client->deadline_ms = clock_ms() + (long long)seconds * MS_PER_SEC; 
}

static int apply_timeout(conn_t *connected_client, int opt){
    if(connected_client->deadline_ms == 0){
        return 0;
    }

    long long left = connected_client->deadline_ms - clock_ms();
    if(left <= 0){
        return CONN_TIMEOUT;
    }

    // Explicitly divides ms by 1000 for seconds, and multiplies remaining ms by 1000 for microseconds
    struct timeval tv = { 
        .tv_sec = left / MS_PER_SEC, 
        .tv_usec = (left % MS_PER_SEC) * US_PER_MS 
    };

    return setsockopt(connected_client->fd, SOL_SOCKET, opt, &tv, sizeof(tv));
}

// reads up to n bytes form the TCP reliable pipe socket fd.
ssize_t conn_read(conn_t *connected_client, void *buffer, size_t n){
    while(1){

        // apply the exact reamining time before reading
        int t = apply_timeout(connected_client, SO_RCVTIMEO);
        if (t < 0) return t;
        // ssize_t is signed size type, so can send back -1
        ssize_t bytes_read = read(connected_client->fd, buffer, n); // reads bytes and sends back bytes read

        if(bytes_read >= 0){
            return bytes_read;
        }

        if(errno == EINTR){
            if(shutdown_requested()){
                return -1; // shutdown
            }
            continue; //signal inturrption handling
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK){
            return CONN_TIMEOUT; // disctinct code for timeout
        }

        return -1; // Network error
    }
}

static int send_all(conn_t *connected_client, const void *buffer, size_t n, int flags){
    size_t remaining = n;

    const char *p = buffer; // pointer to the buffer

    while(remaining > 0){
        // Apply the exact remaining time before writing
        if (apply_timeout(connected_client, SO_SNDTIMEO) < 0) return -1;

        ssize_t written = send(connected_client->fd, p, remaining, flags);

        if(written < 0){ //error
            if(errno == EINTR) continue;
            return -1;
        }

        if(written == 0){
            return -1;
        }

        p += written; // translates the pointer to the number of bytes written
        remaining -= written;
    }

    return 0; //success
}
int conn_write_all(conn_t *connected_client, const void *buffer, size_t n){
    return send_all(connected_client, buffer, n, MSG_NOSIGNAL);
}

int conn_write_more(conn_t *connected_client, const void *buffer, size_t n){
    return send_all(connected_client, buffer, n, MSG_NOSIGNAL | MSG_MORE);
}

// close the TCP secure socket.
void conn_close(conn_t *connected_client){
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