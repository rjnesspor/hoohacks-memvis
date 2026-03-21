#ifndef BENCH_INIT_H
#define BENCH_INIT_H


#ifdef __cplusplus
extern "C" {
#endif

#include <benchmark.h>

extern benchmark *backtrace_track;
extern uint16_t cpu_lane = 0;

void init_benchmark(void);


#ifdef __cplusplus
}
#endif


#endif 