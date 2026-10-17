#include "util/clock.h"
#include <time.h>

// Conversion factors for time units 
#define MS_PER_SEC 1000LL
#define US_PER_SEC 1000000LL
#define NS_PER_MS  1000000LL
#define NS_PER_US  1000LL

long long clock_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * MS_PER_SEC + ts.tv_nsec / NS_PER_MS;
}

long long clock_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * US_PER_SEC + ts.tv_nsec / NS_PER_US;
}
