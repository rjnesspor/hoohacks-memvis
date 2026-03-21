#ifndef C_COMMON_BENCHMARK_H
#define C_COMMON_BENCHMARK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include "hash.h"




/* ======================= Types & enums ======================= */
enum benchmark_resolution { BENCHMARK_SEC, BENCHMARK_MS, BENCHMARK_NS };
enum benchmark_detail_level {
  BENCHMARK_SURFACE_LEVEL = 0,
  BENCHMARK_DETAILED      = 1, /* default */
  BENCHMARK_FINE_DETAILED = 2
};
enum bench_backend  {
    BENCH_BACKEND_HOST = 0,
    BENCH_BACKEND_OPENCL = 1,
    BENCH_BACKEND_CUDA   = 2,
    BENCH_BACKEND_HIP    = 3,
    BENCH_BACKEND_SYCL   = 4,
    BENCH_BACKEND_OPENMP = 5,
    BENCH_BACKEND_PTHREADS = 6,
    BENCH_BACKEND_OTHER  = 7
};


typedef struct benchmark benchmark;
typedef struct benchmark_event benchmark_event;
typedef struct bench_span_handle bench_span_handle;
typedef struct bench_thread bench_thread;

/* ======================= Core utilities ======================= */
uint64_t bench_now_ns(void);

/* ======================= Lifecycle ======================= */
benchmark *benchmark_init(const char* device_name, const char* test_name);
void      benchmark_free(benchmark *);

/* thread-binding (dynamic TLS mapping; safe if thread uses multiple benchmarks) */
bench_thread *benchmark_get_thread(benchmark *); /* fast path; registers if new */

/* ======================= Events API ======================= */
benchmark_event* benchmark_add_event(benchmark *b, const char* event_name, const char* key,
                                     enum benchmark_resolution res, enum benchmark_detail_level importance);
benchmark_event* benchmark_get_event(benchmark *b, const char *key);
void      benchmark_add_runtime(benchmark_event *e, double time);
int       benchmark_add_runtime_by_key(benchmark *b, const char *key, double time);
void      benchmark_set_detail_level(benchmark *b, enum benchmark_detail_level detail);

/* ======================= Printing ======================= */
void      benchmark_print_stats(benchmark *b, FILE* stream);
void      benchmark_print_stats_detail(benchmark *b, FILE* stream, enum benchmark_detail_level detail);

/* ======================= Timeline ======================= */
uint16_t  bench_lane_register(benchmark *b, const char *name,
                              enum bench_backend be, uint16_t device_id, uint16_t queue_id);

bench_span_handle *bench_span_begin(benchmark *b, uint16_t lane_id,
                           const char *name, const char *key, uint16_t parent_idx, uint64_t start_host_ns);
void      bench_span_end(bench_span_handle *span_idx, uint64_t end_host_ns);
void      bench_mark(benchmark *b, uint16_t lane_id, const char *name, uint64_t host_ns, enum benchmark_resolution res);
void      bench_counter_point(benchmark *b, uint16_t lane_id, const char *name, uint64_t host_ns, double value);

/* roll-up helper */
void      bench_rollup_span_to_event(benchmark *b, uint16_t span_idx);

/* ======================= Clocks ======================= */
void      bench_clock_set_offset(benchmark *b, int backend, uint16_t device_id, int64_t device_to_host_ns);
int       bench_clock_get_offset(const benchmark *b, int backend, uint16_t device_id, int64_t *out);

/* ======================= Utilization ======================= */
double    benchmark_utilization(const benchmark *b, const uint16_t *lane_ids, size_t nlanes, uint64_t *active_ns_out);

/* ======================= Exporters ======================= */
int       benchmark_export_csv(benchmark *b, const char *spans_csv_path, const char *lanes_csv_path);
int       benchmark_export_chrome_trace(benchmark *b, const char *json_path);

/* ======================= Scope helper ======================= */
#define BENCH_SCOPE(b,lane,name,key) \
  for (struct {uint16_t i; int active;} _bs_ = { bench_span_begin((b),(lane),(name),(key),UINT16_MAX, bench_now_ns()), 1 }; \
       _bs_.active; _bs_.active = 0, bench_span_end((b), _bs_.i, bench_now_ns()))

/* =================== Optional OpenCL helpers =================== */
#ifdef BENCHMARK_ENABLE_OPENCL
  #include <CL/cl.h>
  int bench_opencl_calibrate_offset(benchmark *b, uint16_t device_id, cl_command_queue q);
  int bench_span_from_cl_event(benchmark *b, uint16_t lane_id, const char *name, const char *key, cl_event ev);
#endif

#ifdef BENCHMARK_ENABLE_CUDA
  #include <cupti.h>
  int bench_initialize_cupti(struct benchmark *b, uint16_t lane,
                           const CUpti_ActivityKind *kinds, int kinds_sz);

  /* Flush and shutdown CUPTI. */
  int bench_shutdown_cupti(void); 
#endif

#ifdef __cplusplus
}
#endif


#endif /* C_COMMON_BENCHMARK_H */


