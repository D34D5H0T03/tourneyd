#ifndef LAB_MODEL_H
#define LAB_MODEL_H

#include <stdint.h>

// Atomically increments the lab counter and returns the new value.
int lab_db_counter_next(int64_t *out_val);

// Forces the database connection to sleep for a specified number of milliseconds (max 2000).
int lab_db_sleep(int ms);

// Executes a recursive Common Table Expression (CTE) to artificially burn CPU cycles inside the DB engine.
int lab_db_cpu(long n, int64_t *out_count);

#endif