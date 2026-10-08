#ifndef CONN_H_INCLUDED
#define CONN_H_INCLUDED

#include <sys/socket.h>
#include <sys/types.h>

// empty container for client information to hand to kernel
typedef struct {
    int fd; //file descriptor of a socket. TCP reliable pipe for data.
    struct sockaddr_storage peer; // for IPv4 and IPv6 handling. Is 128 bytes IPv4 = 16 bytes and IPv6 = 28 bytes. so buffer overflow protection.
    socklen_t peer_len; //size of address for read and write syscalls
} conn_t; 

// blocks out untill client connects and puts data in the out field. returns 0 on success and -1 on failure
int conn_accept(int listen_fd, conn_t* out);

// reads up to n bytes into the buffer and reutrns bytes read (Remember TCP data transfer)
// returns 0 if disconnected, -1 on failure
ssize_t conn_read(conn_t *connected_client, void* buffer, size_t n);

// writes exactly n bytes, inturruptions are handled within
int conn_write_all(conn_t *connected_client, const void* buffer, size_t n);

// safely closes connection
void conn_close(conn_t *connected_client);

// For utility
// gets client ip address and turns it into readable string 
void conn_peer_str(const conn_t *connected_client, char* buffer, size_t n);

#endif