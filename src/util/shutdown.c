#include "util/shutdown.h"
#include <signal.h>

static volatile sig_atomic_t flag;

void shutdown_request(void){
    flag = 1;
}

int shutdown_requested(void){
    return flag;
}