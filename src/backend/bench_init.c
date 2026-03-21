#include <bench_init.h>

benchmark * backtrace_track =  NULL;
uint16_t cpu_lane = 0;


void init_benchmark(void) {
    if (backtrace_track != NULL) return;
    backtrace_track = benchmark_init("CPU", "Executable Function Call Trace")
    cpu_lane = bench_lane_register(backtrace_track, "CPU Thread 0", BENCH_BACKEND_HOST, 0, 0);
}