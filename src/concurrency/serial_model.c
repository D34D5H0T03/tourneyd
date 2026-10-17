#include "concurrency/concurrency.h"
#include "util/shutdown.h"

int concurrency_run(int listen_fd, conn_handler_fn handler, const void *ctx, const concurrency_opts_t *opts)
{
    if (!opts)
        return -1; // guard against NULL config

    while (!shutdown_requested())
    {
        int ready = listener_wait(listen_fd, 500);
        if (ready == 0)
            continue;
        if (ready < 0)
            return -1;
        conn_t client;
        if (conn_accept(listen_fd, &client) < 0)
        {
            if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK)
                return -1;
            usleep(100000);
            continue;
        }
        handler(&client, ctx);
        conn_close(&client);
    }
    return 0;
}

void concurrency_shutdown(void)
{
    shutdown_request();
}