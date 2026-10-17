#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "net/conn.h"

typedef struct {
    int max_workers;
    int shutdown_grace_sec;
} concurrency_opts_t;

// Define a function pointer type for handlers (like echo_handle)
typedef void (*conn_handler_fn)(conn_t *client, const void *ctx);

// Takes control of the accept loop and decides how to run the handler
int concurrency_run(int listen_fd, conn_handler_fn handler, void *ctx, const concurrency_opts_t *opts);

// Gracefully stops the accept loop
void concurrency_shutdown(void);

#endif // CONCURRENCY_H