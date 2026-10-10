#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "../../include/net/listener.h"

int listener_create(const char *port, int backlog){
    struct addrinfo hints; // Hints for addrinfo to specify waht connection I want
    struct addrinfo *res; // Pointer to the head of the returned linked list
    struct addrinfo *p; // pointer to iterate through res
    
    int listener_fd = -1; // default to -1
    int yes = 1; //for SIGINT handling. setsocketopt() takes a void* so has to pass by reference
    

    // preparing hints
    memset(&hints, 0, sizeof(hints)); // zero initialization
    hints.ai_family = AF_UNSPEC; // Address family unspecified INET or INET6 for IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP
    hints.ai_flags = AI_PASSIVE; // tells that the result would be used for bind() rather that connect()

    // getting the linked list of available addresses for the port
    int status = getaddrinfo(NULL, port, &hints, &res);
    if(status != 0){
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return listener_fd; // -1 failure
    }

    // looping through the linked list to find the first connectable address
    for(p = res; p != NULL; p = p->ai_next){

        // try to create a socket
        listener_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(listener_fd < 0){
            continue; // failed to create, onto the next one
        }

        // settign socket option so port can be resued after ctr+c duuring debugging
        if(setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
            fprintf(stderr, "Set socket option failed.\n");
            close(listener_fd);
            freeaddrinfo(res);
            return -1;
        }

        // try to bind the socket to the port
        if(bind(listener_fd, p->ai_addr, p->ai_addrlen) < 0){
            close(listener_fd);
            continue; //try the next one
        }

        // if we reach this then we found the first address and bind worked
        break;
    }

    freeaddrinfo(res); // free the addrinfo struct

    if(listen(listener_fd, backlog) < 0){
        fprintf(stderr, "listen failed\n");
        close(listener_fd);
        return -1; // listen failure
    }

    return listener_fd; // return the file descriptor
}