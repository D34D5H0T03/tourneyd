#include "concurrency/concurrency.h"
#include "net/listener.h"
#include "util/clock.h"
#include "util/log.h"
#include "util/shutdown.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <string.h>

#define CHILD_MEM_LIMIT_MB 256 // address space: ample for 8 MB static files plus buffers
#define CHILD_CPU_LIMIT_SEC 60 // total CPU seconds per connection; legitimate use is tiny
#define CHILD_MAX_FDS 64       // client socket, a file, logs: a leak is capped here

#define ACCEPT_POLL_MS 500
#define ACCEPT_BACKOFF_US 100000
#define DRAIN_POLL_US 50000
#define KILL_WAIT_MS 1000

static volatile sig_atomic_t active_children;
static pid_t *volatile child_pids; // one slot per allowed worker
static volatile int n_slots;

// crash tracking globals
static volatile sig_atomic_t crashes, last_crash_sig, last_crash_pid;
static int logged_crashes;

// resource limiting
static void set_limit(int resource, rlim_t soft, rlim_t hard)
{
    struct rlimit rl = {.rlim_cur = soft, .rlim_max = hard};
    if (setrlimit(resource, &rl) < 0)
        log_error("setrlimit(%d): %m", resource);
}

static void limit_child_resources(void)
{
#if !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
    // ASan reserves a huge virtual range, so an address-space cap would stop it from starting
    set_limit(RLIMIT_AS, (rlim_t)CHILD_MEM_LIMIT_MB << 20, (rlim_t)CHILD_MEM_LIMIT_MB << 20);
#endif
    set_limit(RLIMIT_CPU, CHILD_CPU_LIMIT_SEC, CHILD_CPU_LIMIT_SEC + 5); // SIGXCPU, then SIGKILL
    set_limit(RLIMIT_NOFILE, CHILD_MAX_FDS, CHILD_MAX_FDS);
}

static void sigchld_handler(int sig)
{
    (void)sig;
    int saved = errno;
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (WIFSIGNALED(status))
        {
            last_crash_pid = pid;
            last_crash_sig = WTERMSIG(status);
            crashes++;
        }

        active_children--;
        for (int i = 0; i < n_slots; i++)
            if (child_pids[i] == pid)
            {
                child_pids[i] = 0;
                break;
            }
    }
    errno = saved;
}

static int install_sigchld(void)
{
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    return sigaction(SIGCHLD, &sa, NULL);
}

static void track_child(pid_t pid) // registers children pid
{                                  // called with signals blocked
    for (int i = 0; i < n_slots; i++)
        if (child_pids[i] == 0)
        {
            child_pids[i] = pid;
            return;
        }
}

static void signal_children(int sig)
{ // kills children via SIGTRM
    for (int i = 0; i < n_slots; i++)
    {
        pid_t p = child_pids[i];
        if (p > 0)
            kill(p, sig);
    }
}

static void wait_children(long long ms)
{
    long long end = clock_ms() + ms;
    while (active_children > 0 && clock_ms() < end)
        usleep(DRAIN_POLL_US);
}

static void drain_children(int grace_sec)
{ // sends terminate signal to each children, if not all children has been killed after the wait time, it sends SIGKILL to forcibly kill all children
    log_info("shutdown: draining %d connection(s), grace %ds", (int)active_children, grace_sec);
    signal_children(SIGTERM);
    wait_children((long long)grace_sec * 1000);
    if (active_children > 0)
    {
        log_info("shutdown: killing %d straggler(s)", (int)active_children);
        signal_children(SIGKILL);
        wait_children(KILL_WAIT_MS);
    }
}

int concurrency_run(int listen_fd, conn_handler_fn handler, const void *ctx,
                    const concurrency_opts_t *opts)
{
    // no or malicious config guard
    if (!opts || opts->max_workers < 1)
        return -1;

    // initialize the child_pid lookup table
    child_pids = calloc((size_t)opts->max_workers, sizeof *child_pids);
    if (!child_pids)
        return -1;
    n_slots = opts->max_workers;
    if (install_sigchld() < 0)
    {
        free(child_pids);
        child_pids = NULL;
        n_slots = 0;
        return -1;
    }

    sigset_t block, old; // block all three so the checks below are race-free
    sigemptyset(&block);
    sigaddset(&block, SIGCHLD);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);

    int rc = 0;
    while (!shutdown_requested())
    {
        // crash reporting
        if (crashes != logged_crashes) {
            logged_crashes = crashes;
            if (!shutdown_requested()) {
                log_error("child %d killed by signal %d (%s), %d crash(es) so far",
                          (int)last_crash_pid, (int)last_crash_sig,
                          strsignal(last_crash_sig), (int)crashes);
            }
        }
        // cap: wait for a child to exit; any signal wakes sigsuspend, so shutdown is noticed
        sigprocmask(SIG_BLOCK, &block, &old);
        while (active_children >= opts->max_workers && !shutdown_requested())
            sigsuspend(&old);
        sigprocmask(SIG_SETMASK, &old, NULL);
        if (shutdown_requested())
            break;

        // waits 500ms for a client, if no client arrives it checks flag for a requested shutdown
        int ready = listener_wait(listen_fd, ACCEPT_POLL_MS);
        if (ready == 0)
            continue; // timeout or signal: re-check the flag
        if (ready < 0)
        {
            log_error("poll: %m");
            rc = -1;
            break;
        }

        // client arrives, accept
        conn_t client;
        if (conn_accept(listen_fd, &client) < 0)
        {
            if (errno == EBADF || errno == EINVAL || errno == ENOTSOCK)
            {
                log_error("accept: %m");
                rc = -1;
                break; // listener is gone
            }
            if (errno != ECONNABORTED)
            { // EMFILE, ENOBUFS...: back off
                log_error("accept: %m");
                usleep(ACCEPT_BACKOFF_US);
            }
            continue;
        }

        // safe forking (blocks signals then unblocks it after forking)
        sigprocmask(SIG_BLOCK, &block, &old);
        pid_t pid = fork();
        if (pid > 0)
        { // parent process
            active_children++;
            track_child(pid);
        }
        if (pid == 0)                             // child process
            sigprocmask(SIG_SETMASK, &old, NULL); // child undoes the inherited mask
        else
            sigprocmask(SIG_SETMASK, &old, NULL);

        if (pid < 0)
        { // fork failed
            log_error("fork: %m");
            conn_close(&client);
            continue;
        }
        if (pid == 0)
        {
            signal(SIGCHLD, SIG_DFL);
            close(listen_fd);      // child doesn't need listener

            limit_child_resources(); // limits on resources
            handler(&client, ctx); // child handles the client and then exits
            conn_close(&client);
            _exit(0);
        }
        conn_close(&client); // parent's copy
    }

    drain_children(opts->shutdown_grace_sec);

    signal(SIGCHLD, SIG_DFL); // stop the handler before freeing its table
    n_slots = 0;
    free(child_pids);
    child_pids = NULL;
    return rc;
}

void concurrency_shutdown(void)
{
    shutdown_request();
}