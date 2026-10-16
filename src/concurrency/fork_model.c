#include "concurrency/concurrency.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define LIFETIME_CAP 30

static volatile sig_atomic_t active_children = 0;

// reaps zombie child processes without blocking parent
static void sigchld_handler(int sig){
    (void)sig;

    // saving errno because waitpid might change it
    int saved_errno = errno;

    // WNOHANG ensures no block untill child fully exits
    while(waitpid(-1, NULL, WNOHANG) > 0){
        active_children--;
    }

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

int concurrency_run(int listen_fd, conn_handler_fn handler, void *ctx, const concurrency_opts_t *opts){
    // handle signals with sigaction
    if(install_sigchld() < 0){
        return -1; //setup failed
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);

    while(1){

        // process cap: safely block SIGCHLD, check cap and suspend if full
        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        while(active_children >= opts->max_workers){
            sigsuspend(&oldmask);
        }
        sigprocmask(SIG_SETMASK, &oldmask, NULL); // unblock
        // setup so connection is established with clients that completed the TCP handshake
        conn_t client;
        if(conn_accept(listen_fd, &client) < 0){
            continue;
        }

        // safe fork(), block SIGCHLD so children don't exit before increment
        sigprocmask(SIG_BLOCK, &mask, &oldmask);
        pid_t pid = fork();
        if(pid > 0){
            active_children++;
        }
        sigprocmask(SIG_SETMASK, &oldmask, NULL); // unblock

        if (pid < 0){
            // fork failed so drop client
            conn_close(&client);
            continue;
        }

        if(pid == 0){
            // inside the child process
            alarm(LIFETIME_CAP); // for slowloris
            close(listen_fd); //child process doesn't need listener it only handles client
            handler(&client, ctx); //handle client requests
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