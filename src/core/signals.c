#include "core/signals.h"
#include "util/shutdown.h"
#include <signal.h>
#include <string.h>

static void on_signal(int sig)
{
    (void)sig;
    shutdown_request();
}

int signals_install(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);

    // NO SA_RESTART, blocking system calls must fail and return EINTR
    sa.sa_flags = 0;

    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0)
        return -1;

    // Ignore SIGPIPE to prevent crashes if a client abruptly disconnects during a write
    sa.sa_handler = SIG_IGN;
    return sigaction(SIGPIPE, &sa, NULL);
}