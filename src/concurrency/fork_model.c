#include "../../include/concurrency/concurrency.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

// reaps zombie child processes without blocking parent
static void sigchld_handler(int sig){
    (void)sig;

    // saving errno because waitpid might change it
    int saved_errno = errno;

    // WNOHANG ensures no block untill child fully exits
    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}

static int install_sigchld(void){
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;

    sigemptyset(&sa.sa_mask);

    // SA_RESTART automatically restarts inturrpted syscalls
    // SA_NOCLDSTOP ensures no signal when children just pause or resume
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    return sigaction(SIGCHLD, &sa, NULL);
}

int concurrency_run(int listen_fd, conn_handler_fn handler){
    // handle signals with sigaction
    if(install_sigchld() < 0){
        return -1; //setup failed
    }

    while(1){
        // setup so connection is established with clients that completed the TCP handshake
        conn_t client;
        if(conn_accept(listen_fd, &client) < 0){
            continue;
        }

        pid_t pid = fork();

        if (pid < 0){
            // fork failed so drop client
            conn_close(&client);
            continue;
        }

        if(pid == 0){
            // inside the child process
            close(listen_fd); //child process doesn't need listener it only handles client
            handler(&client); //handle client requests
            conn_close(&client); //close the connection and exit
            _exit(0);
        }

        // parent process
        conn_close(&client); // parent doesn't handle client
    }
    return 0; //unreachable because currently while(1). only stop with SIGINT
}

void concurrency_shutdown(void){
    // later during server hardening phase
}