#ifndef LISTENER_H
#define LISTENER_H


// This handles creation of the welcome socket for TCP connections
// And passes the listening/welcome file descriptor on success and -1 on failure.

int listener_create(const char *bind_address, const char *port, int backlog);

/* 
 * Safely waits for incoming connections without permanently blocking the thread.
 * Prevents a fatal race condition during graceful shutdown: if a SIGTERM arrives 
 * right before a standard accept(), the thread would go to sleep forever waiting 
 * for a client that might never come. poll() ensures the server wakes up every 
 * few milliseconds to re-check the shutdown_requested() flag.
 */
int listener_wait(int fd, int timeout_ms);

#endif