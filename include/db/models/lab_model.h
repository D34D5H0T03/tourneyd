#ifndef LAB_MODEL_H
#define LAB_MODEL_H

#include <stdint.h>

int lab_db_counter_next(int64_t *out_val);
int lab_db_sleep(int ms);
int lab_db_cpu(long n, int64_t *out_count);

#endif