#include "concurrency/concurrency.h"

int concurrency_run(int listen_fd, conn_handler_fn handler, void *ctx, const concurrency_opts_t *opts){
    (void)opts; // only for fork model
    while(1){
        conn_t client;

        if(conn_accept(listen_fd, &client) < 0){
            continue; // connection not accepted.
        }

        // in serial model (no concurrency) the loop blocks untill client is handled
        handler(&client, ctx);

        conn_close(&client);
    }

    return 0;
}

void concurrency_shutdown(void){
    // leave for hardening phase
}