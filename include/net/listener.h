#ifndef LISTENER_H
#define LISTENER_H


// This handles creation of the welcome socket for TCP connections
// And passes the listening/welcome file descriptor on success and -1 on failure.

int listener_create(const char *port, int backlog);


#endif