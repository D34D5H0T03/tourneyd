#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <poll.h>

#include "net/listener.h"
#include "util/log.h"

int listener_create(const char *bind_addr, const char *port, int backlog)
{
    struct addrinfo hints; // Hints for addrinfo to specify waht connection I want
    struct addrinfo *res;  // Pointer to the head of the returned linked list
    struct addrinfo *p;    // pointer to iterate through res

    int listener_fd = -1; // default to -1
    int saved_errno = 0;
    int yes = 1; // SO_REUSEADDR allows restart while old connections are in TIME_WAIT

    // preparing hints
    memset(&hints, 0, sizeof(hints)); // zero initialization
    hints.ai_family = AF_UNSPEC;      // Address family unspecified INET or INET6 for IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP

    // Numerichost to avoid dns resolution
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST; // tells that the result would be used for bind() rather that connect()

    // getting the linked list of available addresses for the port
    int status = getaddrinfo(bind_addr, port, &hints, &res);
    if (status != 0)
    {
        log_error("getaddrinfo error: %s", gai_strerror(status));
        return listener_fd; // -1 failure
    }

    // looping through the linked list to find the first connectable address
    for (p = res; p != NULL; p = p->ai_next)
    {

        // try to create a socket
        listener_fd = socket(p->ai_family, p->ai_socktype | SOCK_CLOEXEC, p->ai_protocol);
        if (listener_fd < 0)
        {
            continue; // failed to create, onto the next one
        }

        // settign socket option so port can be resued after ctr+c duuring debugging
        // SO_REUSEADDR allows restart while old connections are in TIME_WAIT
        if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) < 0 ||
            bind(listener_fd, p->ai_addr, p->ai_addrlen) < 0)
        { // try to bind the socket to the port

            saved_errno = errno;
            close(listener_fd);
            listener_fd = -1;
            continue; // try the next address in the getaddrinfo list
        }

        // if we reach this then we found the first address and bind worked
        break;
    }

    freeaddrinfo(res); // free the addrinfo struct

    if (listener_fd < 0)
    {
        log_error("cannot bind %s:%s: %s", bind_addr, port, strerror(saved_errno));
        return -1;
    }

    if (listen(listener_fd, backlog) < 0)
    {
        log_error("listen failed: %s", strerror(errno));
        close(listener_fd);
        return -1; // listen failure
    }

    return listener_fd; // return the file descriptor
}

int listener_wait(int fd, int timeout_ms)
{
    struct pollfd pfd = {.fd = fd, .events = POLLIN};

    int r = poll(&pfd, 1, timeout_ms);

    // If interrupted by our custom SIGTERM/SIGINT handler, treat it as a timeout (0) so the loop checks the shutdown flag
    if (r < 0)
        return errno == EINTR ? 0 : -1;

    // Returns 1 if a client is ready to be accept()'d, 0 if it timed out
    return r > 0 ? 1 : 0;
}