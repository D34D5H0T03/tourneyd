#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "../net/conn.h"

// Define a function pointer type for handlers (like echo_handle)
typedef void (*conn_handler_fn)(conn_t *client);

// Takes control of the accept loop and decides how to run the handler
int concurrency_run(int listen_fd, conn_handler_fn handler);

// Gracefully stops the accept loop
void concurrency_shutdown(void);

#endif // CONCURRENCY_H