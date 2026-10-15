#include "concurrency/concurrency.h"

int concurrency_run(int listen_fd, int max_children, conn_handler_fn handler){
    (void)max_children; // only for fork model
    while(1){
        conn_t client;

        if(conn_accept(listen_fd, &client) < 0){
            continue; // connection not accepted.
        }

        // in serial model (no concurrency) the loop blocks untill client is handled
        handler(&client);

        conn_close(&client);
    }

    return 0;
}

void concurrency_shutdown(void){
    // leave for hardening phase
}