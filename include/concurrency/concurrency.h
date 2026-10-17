#ifndef CONCURRENCY_H
#define CONCURRENCY_H

#include "net/conn.h"
#include "core/config.h"

#include <errno.h>
#include <unistd.h>

#define ACCEPT_ERR_BACKOFF 100000

typedef struct {
    int max_workers;
    int shutdown_grace_sec;
} concurrency_opts_t;

/* Helper to prevent CPU spinning on accept() errors */
static inline int concurrency_handle_accept_err(void) {
    if (errno == EMFILE || errno == ENFILE) {
        usleep(ACCEPT_ERR_BACKOFF); // 100ms backoff if out of file descriptors
        return 0;       // continue loop
    }
    if (errno == EBADF || errno == EINVAL) {
        return -1;      // fatal error, listener is gone
    }
    return 0;           // ignore other transient errors and continue
}

// Define a function pointer type for handlers (like echo_handle)
typedef void (*conn_handler_fn)(conn_t *client, const void *ctx);

// Takes control of the accept loop and decides how to run the handler
int concurrency_run(int listen_fd, conn_handler_fn handler, const void *ctx, const concurrency_opts_t *opts);

// Gracefully stops the accept loop
void concurrency_shutdown(void);

#endif // CONCURRENCY_H