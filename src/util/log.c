#include "util/log.h"
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#define BUFFER_SIZE 1024

// helper for writing the logs
static void log_write(const char *level, const char *fmt, va_list args){
    char buffer[BUFFER_SIZE];

    // get current time
    time_t now = time(NULL);
    struct tm tm_info;
    
    // gmtime_r is thread and fork safe
    gmtime_r(&now, &tm_info);

    // write the timestamp into the buffer
    int offset = strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S] ", &tm_info);

    // log level (INFO/ERROR)
    offset += snprintf(buffer + offset, sizeof(buffer) - offset, "[%s] ", level);

    // append the user's formatted string
    int len = vsnprintf(buffer + offset, sizeof(buffer) - offset, fmt, args);
    if(len > 0){
        offset += len;
    }

    // leaving exactly 2 bytes for \n and \0
    if(offset >= (int)sizeof(buffer)){
        offset = sizeof(buffer) - 2;
    }

    buffer[offset++] = '\n';
    buffer[offset] = '\0';

    // write to file descriptor 2 (stderr), cast return value to void to suppress compiler warnings
    (void)write(STDERR_FILENO, buffer, offset);
}

void log_info(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);
    log_write("INFO", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...){
    va_list args;
    va_start(args, fmt);
    log_write("ERROR", fmt, args);
    va_end(args);
}