#ifndef LOG_H
#define LOG_H

typedef enum
{ // logging state machine 
    LOG_DEBUG,
    LOG_INFO,
    LOG_ERROR
} log_level_t;

// Tells the compiler to check the format string against the variadic arguments
#define LOG_PRINTF(a, b) __attribute__((format(printf, a, b)))

void log_set_level(log_level_t level);                  // call once at startup, before fork
void log_debug(const char *fmt, ...) LOG_PRINTF(1, 2);  // stderr, hidden unless level is DEBUG
void log_info(const char *fmt, ...) LOG_PRINTF(1, 2);   // stderr */
void log_error(const char *fmt, ...) LOG_PRINTF(1, 2);  // stderr, always shown
void log_access(const char *fmt, ...) LOG_PRINTF(1, 2); // stdout, one line per request

#endif