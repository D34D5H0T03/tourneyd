#include "util/log.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

/* One log line is one write(). Writes up to PIPE_BUF (4096) bytes are atomic on pipes, and
   children share the file offset, so lines from different processes never interleave. */
#define LOG_LINE_MAX 2048

static log_level_t min_level = LOG_INFO;     /* written once before fork, read-only after */

void log_set_level(log_level_t level) { min_level = level; }

static void emit(int fd, const char *buf, size_t len) { // make sure write() syscall writes everything
    while (len > 0) {
        ssize_t w = write(fd, buf, len);
        if (w < 0) { 
            // EINTR retry prevents lost logs if a signal interrupts the write
            if (errno == EINTR) continue; 
            return; 
        }
        buf += w;
        len -= (size_t)w;
    }
}

static void log_write(int fd, const char *tag, const char *fmt, va_list ap) { 
    int saved_errno = errno;                // saves the errno if crashed
    char buf[LOG_LINE_MAX];

    // timestamps
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    
    // ISO 8601 Timestamp
    size_t off = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    
    // Inject Process ID
    int p = snprintf(buf + off, sizeof(buf) - off, " %s pid=%ld ", tag, (long)getpid());
    if (p > 0) off += (size_t)p;
    
    // Overflow safety clamp
    if (off >= sizeof(buf) - 1) off = sizeof(buf) - 2;   /* keeps the math safe */

    size_t room = sizeof(buf) - off - 1;      /* reserve one byte for the newline */
    int n = vsnprintf(buf + off, room + 1, fmt, ap);
    size_t len = n < 0 ? 0 : ((size_t)n > room ? room : (size_t)n);

    // Sanitize output: scrub control characters to prevent terminal corruption
    for (size_t i = 0; i < len; i++) {       
        unsigned char ch = (unsigned char)buf[off + i];
        if (ch < 0x20 || ch == 0x7f) buf[off + i] = '?';
    }
    
    off += len;
    

    buf[off++] = '\n';                     

    emit(fd, buf, off);
    errno = saved_errno; // Restore original errno before returning
}

void log_debug(const char *fmt, ...) {
    if (min_level > LOG_DEBUG) return;
    va_list ap; va_start(ap, fmt); log_write(STDERR_FILENO, "[DEBUG]", fmt, ap); va_end(ap);
}
void log_info(const char *fmt, ...) {
    if (min_level > LOG_INFO) return;
    va_list ap; va_start(ap, fmt); log_write(STDERR_FILENO, "[INFO]", fmt, ap); va_end(ap);
}
void log_error(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); log_write(STDERR_FILENO, "[ERROR]", fmt, ap); va_end(ap);
}
void log_access(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); log_write(STDOUT_FILENO, "[ACCESS]", fmt, ap); va_end(ap);
}