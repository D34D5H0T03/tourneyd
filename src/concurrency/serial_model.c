#include "concurrency/concurrency.h"

int concurrency_run(int listen_fd, conn_handler_fn handler, const void *ctx, const concurrency_opts_t *opts){
    if(!opts) return -1; // guard against NULL config

    while(1){
        conn_t client;

        if(conn_accept(listen_fd, &client) < 0){
            if (concurrency_handle_accept_err() < 0) {
                return -1; // Abort if listener died
            }
            continue; 
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