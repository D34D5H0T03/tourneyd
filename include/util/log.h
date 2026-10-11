#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

// Formats a message and writes it atomically to standard error
void log_info(const char *fmt, ...);

// Formats an error message and writes it atomically to standard error
void log_error(const char *fmt, ...);

#endif // LOG_H