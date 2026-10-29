#pragma once
#include <stdint.h>

typedef void (*ds_task_fn)(void* arg, int worker);

void ds_parallel(int n, ds_task_fn fn, void* arg);
int64_t ds_atomic_add(volatile int64_t* counter, int64_t v);
int64_t ds_atomic_get(volatile int64_t* counter);
int ds_cpu_count(void);
double ds_time(void);
void ds_sleep_ms(int ms);
