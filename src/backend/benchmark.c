#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdatomic.h>
#include "benchmark.h"
#include "hash.h"


#ifndef _WIN32
  #include <time.h>
#else
  #include <windows.h>
#endif
/*TODO: redo init and free benchmark, break when see a span that is still open.*/



/* ======================= Structs ============================ */


    struct benchmark_event {
    uint16_t sample_size;
    uint16_t num_samples;
    double  *sample_times;
    char    *event_name;
    char    *key;
    enum benchmark_resolution   res;
    enum benchmark_detail_level importance;
    double total_time;
    double min;
    double max;
    };



    /* timeline types */
    typedef struct bench_lane {
        uint16_t id;
        enum bench_backend backend;
        uint16_t device_id;
        uint16_t queue_id;
        char    *name;
    } bench_lane;

    typedef struct bench_span {
        uint64_t start_ns, end_ns;
        uint16_t lane_id;
        uint16_t parent; /* UINT16_MAX if none */
        char    *name;
        char    *key;    /* optional: roll-up to events */
        enum benchmark_detail_level detail;
    } bench_span;


    typedef struct bench_instant {
        uint64_t ts_ns;
        uint16_t lane_id;
        char    *name;
        uint8_t  detail;
    } bench_instant;


    typedef struct bench_counter {
        uint64_t ts_ns;
        uint16_t lane_id;
        char    *name;
        double   value;
    } bench_counter;


    typedef struct bench_clock_offset {
        int      backend;       /* enum bench_backend */
        uint16_t device_id;
        int64_t  device_to_host_ns; /* host_ns = device_ns + offset */
    } bench_clock_offset;

    /* ======= Segmented single-producer queues ======= */

    #define BENCH_SEG_SIZE_DEFAULT 2048

    typedef struct bench_seg_header {
        _Atomic(struct bench_seg_header*) next; /* published by writer via release-store */
        uint32_t                 cap;  /* items capacity in this segment */
        _Atomic uint32_t        tail; /* items produced in this segment */
        _Atomic uint32_t         open_spans;
        /* items[] follow */
    } bench_seg_header;


    /* specific to bench spans since they need to be called twice: begin & end */
    struct bench_span_handle{
        bench_seg_header *seg;
        uint32_t idx;
    };

    /* Per-thread queue of T using linked segments; single producer (owning thread). */
    typedef struct {
        bench_seg_header *head;     /* first segment (for reader) */
        bench_seg_header *tail;     /* current write segment (writer only) */
        uint32_t          seg_cap;  /* default capacity */
    } bench_segq_T; /* used via macros below */



    /* ======= Per-thread writer state ======= */
    typedef struct bench_segq_instants { bench_segq_T q; } bench_segq_instants;
    typedef struct bench_segq_spans    { bench_segq_T q; } bench_segq_spans;
    typedef struct bench_segq_counters { bench_segq_T q; } bench_segq_counters;

    /* Owning-thread writer. One thread may write to many benchmarks: we bind dynamically. */
    struct bench_thread {
        uint64_t tid;                /* opaque thread id (hashed pthread_t or gettid) */
        _Atomic uint64_t heartbeat_ns;

        bench_segq_spans    spans;
        bench_segq_instants instants;
        bench_segq_counters counters;

        /* Per-thread aggregates (fast counters) can be added later if you want. */
    };


    /* Aggregator’s per-thread cursors to avoid duplicates */
    typedef struct bench_consume_cursor {
    bench_seg_header *spans_seg;     uint32_t spans_idx;
    bench_seg_header *inst_seg;      uint32_t inst_idx;
    bench_seg_header *ctrs_seg;      uint32_t ctrs_idx;
    } bench_consume_cursor;


    /* ======= Benchmark top-level ======= */

    struct benchmark {
        /* events & printing (cold) */
        char *device_name;
        char *test_name;
        uint16_t        num_events, events_size;
        benchmark_event *events;
        enum benchmark_detail_level print_detail_level;

        hash_index idx;   /* your event registry (sharded inside .c) */

        /* thread registry (cold) */
        pthread_mutex_t threads_mu;
        pthread_mutex_t rollup_mu;

        struct bench_thread_node {
            bench_thread *thr;
            bench_consume_cursor cursor;
            struct bench_thread_node *next;
        } *threads; /* intrusive list; mutations guarded by threads_mu */

        bench_lane    *lanes;    uint32_t lanes_sz, lanes_cap;
        /* timeline roll-up buffers (optional; can be produced on export instead) */
        bench_span    *spans;    uint32_t spans_sz, spans_cap;
        bench_instant *instants; uint32_t inst_sz, inst_cap;
        bench_counter *counters; uint32_t ctrs_sz, ctrs_cap;

        /* clocks */
        bench_clock_offset *clocks; uint32_t clocks_sz, clocks_cap;
    };

/* ======================= Local utils ======================= */
    static const char* resolutions[] = {"seconds", "milliseconds", "nanoseconds"};

    /* Converts a nanosecond timestamp to any resolution defined in benchmark_resolution */
    static inline double ns_to_res(uint64_t dt_ns, enum benchmark_resolution r) {
        switch (r) {
            case BENCHMARK_SEC: return (double)dt_ns / 1e9;
            case BENCHMARK_MS:  return (double)dt_ns / 1e6;
            case BENCHMARK_NS:  return (double)dt_ns;
            default:            return (double)dt_ns;
        }
    }

    /* ensures a buffer has capacity for a needed number of elements or realllocates, doubling buffer size */
    static void ensure_cap_void(void **buf, uint32_t elem_size, uint32_t *cap, uint32_t need) {
        if (need <= *cap) return;

        uint32_t new_cap = *cap ? (*cap << 1) : 8u;
        if (new_cap < need) new_cap = need;

        void *nb = realloc(*buf, (size_t)new_cap * elem_size);
        if (!nb) { perror("realloc"); exit(EXIT_FAILURE); }

        memset((char*)nb + (*cap * elem_size), 0,
            (size_t)(new_cap - *cap) * elem_size);

        *buf = nb;
        *cap = new_cap;
    }

    /* converts timespec struct to ns timestamp */
    static uint64_t to_ns(struct timespec ts) { return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }

    /* copies string into a returned char* */
    static char *dup_cstr(const char *s) {
        size_t n = strlen(s ? s : "") + 1;
        char *p = (char*)malloc(n);
        if (!p) { perror("malloc"); exit(EXIT_FAILURE); }
        memcpy(p, s ? s : "", n);
        return p;
    }


    static uint64_t bench_get_tid(void){
        /* On POSIX, pthread_t is opaque; hash it. If Linux, syscall(SYS_gettid) is fine. */
        pthread_t self = pthread_self();
        /* FNV-1a hash of pthread_t bytes */
        uint64_t h = 1469598103934665603ull;
        const unsigned char *p = (const unsigned char *)&self;
        for (size_t i=0;i<sizeof(self);++i){ h ^= p[i]; h *= 1099511628211ull; }
        return h;
    }


/* ======= Segmented Queue Internals (type-agnostic) ======= */

    static bench_seg_header* bench_new_segment(uint32_t cap, size_t item_size){
        size_t bytes = sizeof(bench_seg_header) + (size_t)cap * item_size;
        bench_seg_header *seg = (bench_seg_header*)aligned_alloc(64, ((bytes+63)/64)*64);
        if(!seg) return NULL;
        seg->next = NULL;
        seg->cap  = cap;
        atomic_store_explicit(&seg->tail, 0, memory_order_relaxed);
        atomic_store_explicit(&seg->open_spans, 0, memory_order_relaxed);
        return seg;
    }

    static void bench_segq_init(bench_segq_T *q, uint32_t seg_cap, size_t item_size){
        if (seg_cap == 0) seg_cap = BENCH_SEG_SIZE_DEFAULT;
        bench_seg_header *s = bench_new_segment(seg_cap, item_size);
        q->head = q->tail = s;
        q->seg_cap = seg_cap;
    }

    /* T* bench_segq_emplace(q, item_size) : returns pointer to uninitialized slot; caller writes fields then publishes by incrementing tail */
    static void* bench_segq_emplace(bench_segq_T *q, size_t item_size){
        bench_seg_header *t = q->tail;
        uint32_t tail = atomic_load_explicit(&t->tail, memory_order_relaxed);
        if (tail == t->cap){
            /* allocate new seg and link */
            bench_seg_header *n = bench_new_segment(q->seg_cap, item_size);
            atomic_store_explicit(&t->next, n, memory_order_release);
             /* writer link; not visible to reader until we increment n->tail later */
            /* publish link with release? The reader only follows when its current seg is fully consumed (tail==cap),
            and then it reads next pointer that we set here. No shared counter, so store is sufficient. */
            q->tail = n;
            t = n; tail = 0;
        }
        /* return pointer to slot */
        unsigned char *base = (unsigned char*)(t + 1);
        return base + (size_t)tail * item_size;
    }

    static void bench_segq_publish(bench_segq_T *q, bench_seg_header *seg){
        /* publish one item in seg by tail++ with release */
        atomic_fetch_add_explicit(&seg->tail, 1, memory_order_release);
    }

    /* reader: consume items from seg/index, calling fn(item_ptr). Updates seg/index in place. */
    typedef void (*bench_consume_fn)(void *item, void *ctx);
    static void bench_segq_consume(bench_seg_header **seg_io, uint32_t *idx_io,
                                size_t item_size, bench_consume_fn fn, void *ctx){
        bench_seg_header *seg = *seg_io;
        uint32_t idx = *idx_io;
 
        while (seg){
            uint32_t tail = atomic_load_explicit(&seg->tail, memory_order_acquire);
            uint32_t open = atomic_load_explicit(&seg->open_spans, memory_order_relaxed);
            if (open) break;
            while (idx < tail){
                unsigned char *base = (unsigned char*)(seg + 1);
                void *item = base + (size_t)idx * item_size;
                fn(item, ctx);
                idx++;
            }
            if (tail < seg->cap){ /* writer hasn’t filled this segment yet */
                break;
            }
            /* move to next segment */
            bench_seg_header *next = atomic_load_explicit(&seg->next, memory_order_acquire); /* linked by writer earlier */
            if (!next) break;
            /* this fully consumed segment could be freed here if you want GC on reader side */

            free(seg);
            seg = next;
            idx = 0;
        }

        *seg_io = seg;
        *idx_io = idx;
    }

/* ======= Per-Thread Registry (dynamic TLS binding per benchmark) ======= */

    typedef struct tls_binding {
        benchmark   *b;
        bench_thread *t;
        struct tls_binding *next;
    } tls_binding;

    static _Thread_local tls_binding *g_tls_bindings = NULL;

    static bench_thread* bench_thread_create(void){
        bench_thread *t = (bench_thread*)calloc(1, sizeof(*t));
        t->tid = bench_get_tid();
        atomic_store_explicit(&t->heartbeat_ns, bench_now_ns(), memory_order_relaxed);
        /* init seg queues with their item sizes */
        bench_segq_init(&t->spans.q,    BENCH_SEG_SIZE_DEFAULT, sizeof(bench_span));
        bench_segq_init(&t->instants.q, BENCH_SEG_SIZE_DEFAULT, sizeof(bench_instant));
        bench_segq_init(&t->counters.q, BENCH_SEG_SIZE_DEFAULT, sizeof(bench_counter));
        return t;
    }

    static void bench_register_thread_locked(benchmark *b, bench_thread *t){
        struct bench_thread_node *node = (struct bench_thread_node*)calloc(1, sizeof(*node));
        node->thr = t;
        node->cursor.spans_seg = t->spans.q.head;  node->cursor.spans_idx = 0;
        node->cursor.inst_seg  = t->instants.q.head; node->cursor.inst_idx = 0;
        node->cursor.ctrs_seg  = t->counters.q.head; node->cursor.ctrs_idx = 0;
        node->next = b->threads;
        b->threads = node;
    }

    bench_thread *benchmark_get_thread(benchmark *b){
        if (!b) return NULL;
        /* fast TLS lookup */
        for (tls_binding *it = g_tls_bindings; it; it = it->next){
            if (it->b == b) return it->t;
        }
        /* miss: create and register */
        bench_thread *t = bench_thread_create();
        pthread_mutex_lock(&b->threads_mu);
        bench_register_thread_locked(b, t);
        pthread_mutex_unlock(&b->threads_mu);
        /* add TLS binding */
        tls_binding *bnd = (tls_binding*)malloc(sizeof(*bnd));
        bnd->b = b; bnd->t = t; bnd->next = g_tls_bindings; g_tls_bindings = bnd;
        return t;
    }

/* ======================= Core time ======================= */

    /* returns current time stamp in nanoseconds */
    uint64_t bench_now_ns(void) {
    #ifndef _WIN32
        struct timespec ts;
    #ifdef CLOCK_MONOTONIC_RAW
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    #else
        clock_gettime(CLOCK_MONOTONIC, &ts);
    #endif
        return to_ns(ts);
    #else
        LARGE_INTEGER fq, cn;
        QueryPerformanceFrequency(&fq);
        QueryPerformanceCounter(&cn);
        return (uint64_t)((__int128)cn.QuadPart * 1000000000ull / fq.QuadPart);
    #endif
    }

/* ======================= Lifecycle ======================= */
    /* ensures that a benchmark has enough capacity for events given a need*/
    static void ensure_events_capacity(benchmark *b, uint16_t need) {
        if (need <= b->events_size) return;
        uint16_t new_cap = b->events_size ? (uint16_t)(b->events_size * 2) : 4;
        if (new_cap < need) new_cap = need;
        benchmark_event *nb = (benchmark_event*)realloc(b->events, new_cap * sizeof(*nb));
        if (!nb) { perror("realloc"); exit(EXIT_FAILURE); }
        b->events = nb;
        b->events_size = new_cap;
    }

    /* creates new benchmark: Not particularly thread safe */
    benchmark *benchmark_init(const char *device_name, const char *test_name) {
        benchmark *b = calloc(1, sizeof(*b));
        if (!b) return NULL;


        b->device_name = dup_cstr(device_name);
        b->test_name   = dup_cstr(test_name);
        b->num_events  = 0;
        b->events_size = 0;
        b->events      = NULL;
        b->print_detail_level = BENCHMARK_DETAILED;
        b->threads = NULL;

        pthread_mutex_init(&b->threads_mu, NULL);
        pthread_mutex_init(&b->rollup_mu, NULL);

        bench_hash_init(&b->idx, 8);

        b->lanes = NULL; b->lanes_sz = b->lanes_cap = 0;
        b->spans = NULL; b->spans_sz = b->spans_cap = 0;
        b->instants = NULL; b->inst_sz = b->inst_cap = 0;
        b->counters = NULL; b->ctrs_sz = b->ctrs_cap = 0;
        b->clocks = NULL; b->clocks_sz = b->clocks_cap = 0;
        return b;
    }

    /* frees benchmark: Not particularly thread safe */
    void benchmark_free(benchmark *b) {
        if (!b) return;
        pthread_mutex_destroy(&b->threads_mu);
        pthread_mutex_destroy(&b->rollup_mu);
        if(b->device_name)
            free(b->device_name);
        if(b->test_name)
            free(b->test_name);

        struct bench_thread_node *n = b->threads;
        while(n) {
            bench_seg_header *seg, *next;
            seg = n->thr->spans.q.head; while (seg) {next = seg->next; free(seg); seg = next;}
            seg = n->thr->instants.q.head; while (seg) {next = seg->next; free(seg); seg = next;}
            seg = n->thr->counters.q.head; while (seg) {next = seg->next; free(seg); seg = next;}
            free(n->thr);
            struct bench_thread_node *tmp = n; n = n->next; free(tmp);
        }

        for (uint16_t i = 0; i < b->num_events; ++i) {
            benchmark_event *e = &b->events[i];
            free(e->event_name);
            free(e->key);
            free(e->sample_times);
        }
        free(b->events);
        bench_hash_free(&b->idx);

        for (uint32_t i = 0; i < b->lanes_sz; ++i) free(b->lanes[i].name);
        for (uint32_t i = 0; i < b->spans_sz; ++i) { free(b->spans[i].name); if (b->spans[i].key) free(b->spans[i].key); }
        for (uint32_t i = 0; i < b->inst_sz; ++i) free(b->instants[i].name);
        for (uint32_t i = 0; i < b->ctrs_sz; ++i) free(b->counters[i].name);
        free(b->lanes); free(b->spans); free(b->instants); free(b->counters);
        free(b->clocks);
        free(b);
    }

/* ======================= Events API ======================= */

    /* returns an event from benchmark with same key as parameter key or NULL if not found */
    benchmark_event* benchmark_get_event(benchmark *b, const char *key) {
        if (!b) return NULL;
        uint16_t idx;
        if (!key) return NULL;
        if (!bench_hash_get(&b->idx, key, &idx)) return NULL;
        return &b->events[idx];
    }

    /* adds a new event with a given importance. No duplicate keys 
    Lock is aquired in this function as it is a function called by user */
    benchmark_event* benchmark_add_event(benchmark *b,
                                        const char *event_name,
                                        const char *key,
                                        enum benchmark_resolution res,
                                        enum benchmark_detail_level importance)
    {
        if (!b) return NULL;
        if (b->num_events == UINT16_MAX) { fprintf(stderr, "benchmark full, cannot add another event\n"); exit(EXIT_FAILURE); }
        pthread_mutex_lock(&b->threads_mu); 
        if (key && benchmark_get_event(b, key)) { fprintf(stderr, "benchmark_add_event: duplicate key '%s'\n", key); exit(EXIT_FAILURE); }

        ensure_events_capacity(b, (uint16_t)(b->num_events + 1));
        benchmark_event *e = &b->events[b->num_events];

        e->event_name  = dup_cstr(event_name);
        e->key         = dup_cstr(key);
        e->res         = res;
        e->num_samples = 0;
        e->sample_size = 4;
        e->importance  = importance;
        e->sample_times = (double*)malloc(sizeof(double) * e->sample_size);
        if (!e->sample_times) { perror("malloc"); exit(EXIT_FAILURE); }
        e->total_time  = 0.0;
        e->min = INFINITY; e->max = -INFINITY;

        uint16_t idx = b->num_events++;
        if (key) bench_hash_put(&b->idx, e->key, idx);
        pthread_mutex_unlock(&b->threads_mu);
        return e;
    }

    /* Lock needs to be held upon entering this function */
    void benchmark_add_runtime(benchmark_event *e, double time) {
        if (!e || time < 0.0) return;
        if (e->num_samples == e->sample_size) {
            uint16_t new_cap = (uint16_t)(e->sample_size * 2u);
            double *nt = (double*)realloc(e->sample_times, new_cap * sizeof(double));
            if (!nt) { perror("realloc"); exit(EXIT_FAILURE); }
            e->sample_times = nt;
            e->sample_size = new_cap;
        }
        e->sample_times[e->num_samples++] = time;
        e->total_time += time;
        if (time < e->min) e->min = time;
        if (time > e->max) e->max = time;
    }

    int benchmark_add_runtime_by_key(benchmark *b, const char *key, double time) {
        benchmark_event *e = benchmark_get_event(b, key);
        if (!e) return -1;
        benchmark_add_runtime(e, time);
        return 0;
    }

    void benchmark_set_detail_level(benchmark *b, enum benchmark_detail_level detail){b->print_detail_level = detail;}

/* ======================= Aggregation ====================== */

    /* union in case I want to reuse this later */
    typedef union {
        benchmark *b;
    } merge_ctx;

    /* Ensure capacity helpers for roll-up buffers */
    static void ensure_spans(benchmark *b, uint32_t add){
        if (b->spans_sz + add > b->spans_cap){
            uint32_t need = b->spans_sz + add;
            uint32_t cap = b->spans_cap ? b->spans_cap : 1024;
            while (cap < need) cap *= 2;
            b->spans = (bench_span*)realloc(b->spans, cap * sizeof(*b->spans));
            b->spans_cap = cap;
        }
    }

    static void ensure_instants(benchmark *b, uint32_t add){
        if (b->inst_sz + add > b->inst_cap){
            uint32_t need = b->inst_sz + add;
            uint32_t cap = b->inst_cap ? b->inst_cap : 1024;
            while (cap < need) cap *= 2;
            b->instants = (bench_instant*)realloc(b->instants, cap * sizeof(*b->instants));
            b->inst_cap = cap;
        }
    }
    static void ensure_counters(benchmark *b, uint32_t add){
        if (b->ctrs_sz + add > b->ctrs_cap){
            uint32_t need = b->ctrs_sz + add;
            uint32_t cap = b->ctrs_cap ? b->ctrs_cap : 1024;
            while (cap < need) cap *= 2;
            b->counters = (bench_counter*)realloc(b->counters, cap * sizeof(*b->counters));
            b->ctrs_cap = cap;
        }
    }

    /* First pass: count available items to reserve space; second pass: copy & advance cursors.
    For simplicity here, we do single-pass per thread with incremental ensure_* calls. */

    static void merge_span(void *item, void *ctx_){
        merge_ctx *ctx = (merge_ctx*)ctx_;
        benchmark *b = ctx->b;
        ensure_spans(b, 1);
        bench_span *bs = (bench_span*)item;
        benchmark_event *e = benchmark_get_event(b, bs->key);
        if (e) benchmark_add_runtime(e, ns_to_res(bs->end_ns - bs->start_ns, e->res));
        b->spans[b->spans_sz++] = *bs;
    }

    static void merge_instant(void *item, void *ctx_){
        merge_ctx *ctx = (merge_ctx*)ctx_;
        benchmark *b = ctx->b;
        ensure_instants(b, 1);
        b->instants[b->inst_sz++] = *(bench_instant*)item;
    }
    static void merge_counter(void *item, void *ctx_){
        merge_ctx *ctx = (merge_ctx*)ctx_;
        benchmark *b = ctx->b;
        ensure_counters(b, 1);
        b->counters[b->ctrs_sz++] = *(bench_counter*)item;
    }

    void static benchmark_aggregate_locked(benchmark *b){
        merge_ctx ctx = { .b = b };
        pthread_mutex_lock(&b->threads_mu);
        

        for (struct bench_thread_node *n = b->threads; n; n = n->next){
            /* consume from each queue, starting at cursor.{seg,idx}, and update cursor inplace */
            bench_segq_consume(&n->cursor.spans_seg, &n->cursor.spans_idx,
                            sizeof(bench_span), merge_span, &ctx);
            bench_segq_consume(&n->cursor.inst_seg, &n->cursor.inst_idx,
                            sizeof(bench_instant), merge_instant, &ctx);
            bench_segq_consume(&n->cursor.ctrs_seg, &n->cursor.ctrs_idx,
                            sizeof(bench_counter), merge_counter, &ctx);
        }
        pthread_mutex_unlock(&b->threads_mu);
    }

    void benchmark_aggregate(benchmark *b) {
        pthread_mutex_lock(&b->rollup_mu);
        benchmark_aggregate_locked(b);
        pthread_mutex_unlock(&b->rollup_mu);
    }

/* ======================= Printing ======================= */

    static double avg_runtime(const benchmark_event *e) {
        return (e->num_samples == 0) ? 0.0 : e->total_time / e->num_samples;
    }

    static double compute_stddev(const benchmark_event *e) {
        if (e->num_samples <= 1) return 0.0;
        double mean = avg_runtime(e);
        double sum_sq_diff = 0.0;
        for (int i = 0; i < e->num_samples; ++i) {
            double diff = e->sample_times[i] - mean;
            sum_sq_diff += diff * diff;
        }
        return sqrt(sum_sq_diff / (e->num_samples - 1));
    }

    void benchmark_print_stats_detail(benchmark *b, FILE* stream, enum benchmark_detail_level detail){
        if (!b) return;
        pthread_mutex_lock(&b->rollup_mu);
        benchmark_aggregate_locked(b);

        fprintf(stream, "number of spans %u\n", b->spans_sz);

        fprintf(stream, "Benchmark %s running on device %s\n\n", b->test_name, b->device_name);
        for (int i = 0; i < b->num_events; i++) {
            if (b->events[i].importance > detail) continue; /* print events at or above requested detail */
            double stddev = compute_stddev(b->events + i);
            fprintf(stream, "Event %d: %s\n", i, b->events[i].event_name);
            fprintf(stream, "  Runs      : %d\n", b->events[i].num_samples);
            fprintf(stream, "  Avg time  : %.6f %s\n", avg_runtime(b->events + i), resolutions[b->events[i].res]);
            fprintf(stream, "  Min time  : %.6f %s\n", b->events[i].min, resolutions[b->events[i].res]);
            fprintf(stream, "  Max time  : %.6f %s\n", b->events[i].max, resolutions[b->events[i].res]);
            fprintf(stream, "  Std Dev   : %.6f %s\n\n", stddev, resolutions[b->events[i].res]);
        }
        pthread_mutex_unlock(&b->rollup_mu);
    }

    void benchmark_print_stats(benchmark *b, FILE* stream) { benchmark_print_stats_detail(b, stream, b->print_detail_level); }

/* ======================= Timeline ======================= */

    uint16_t bench_lane_register(benchmark *b, const char *name,
                                enum bench_backend be, uint16_t device_id, uint16_t queue_id)
    {
        if (!b) return UINT16_MAX;
        ensure_cap_void((void**)&b->lanes, sizeof(*b->lanes), &b->lanes_cap, b->lanes_sz + 1);
        uint16_t id = (uint16_t)b->lanes_sz;
        bench_lane *L = &b->lanes[b->lanes_sz++];
        L->id = id;
        L->backend = be;
        L->device_id = device_id;
        L->queue_id = queue_id;
        L->name = dup_cstr(name);
        return id;
    }

    bench_span_handle *bench_span_begin(benchmark *b, uint16_t lane_id,
                            const char *name, const char *key, uint16_t parent_idx,
                            uint64_t start_host_ns)
    {
        if (!b) return NULL;
        bench_thread *t = benchmark_get_thread(b);

        bench_span *s = (bench_span*)bench_segq_emplace(&t->spans.q, sizeof(bench_span));
        

        s->start_ns = start_host_ns; s->end_ns = start_host_ns;
        s->lane_id = lane_id; s->parent = parent_idx;
        s->name = dup_cstr(name);
        s->key  = key ? dup_cstr(key) : NULL;
        s->detail = BENCHMARK_DETAILED;

        bench_seg_header *seg = t->spans.q.tail;
        uint32_t idx = atomic_load_explicit(&seg->tail, memory_order_relaxed);

        atomic_fetch_add_explicit(&seg->open_spans, 1, memory_order_relaxed);
        bench_segq_publish(&t->spans.q, seg);
            
        bench_span_handle *ret = malloc(sizeof(*ret));
        ret->idx = idx; ret->seg = seg;
        return ret;
    }

    void bench_rollup_span_to_event(benchmark *b, uint16_t span_idx) {
        if (!b) return;
        bench_span *S = &b->spans[span_idx];
        if (!S->key || S->end_ns < S->start_ns) return;
        uint64_t dt_ns = S->end_ns - S->start_ns;
        benchmark_event *e = benchmark_get_event(b, S->key);
        if (e) benchmark_add_runtime(e, ns_to_res(dt_ns, e->res));
    }

    void bench_span_end(bench_span_handle *h, uint64_t end_host_ns) {
        if (!h || !h->seg) return;

        uint32_t tail = atomic_load_explicit(&h->seg->tail, memory_order_acquire);
        if (h->idx >= tail) return; // malformed handle or not yet published

        bench_span *base = (bench_span*)(h->seg + 1);
        bench_span *s = &base[h->idx];
        s->end_ns = end_host_ns;
        
        atomic_fetch_sub_explicit(&h->seg->open_spans, 1, memory_order_release);
        free(h);
    }

    void bench_mark(benchmark *b, uint16_t lane_id, const char *name, uint64_t host_ns, enum benchmark_resolution res) {
        if (!b) return;
        bench_thread *t = benchmark_get_thread(b);
        bench_seg_header *seg = t->instants.q.tail;
        bench_instant *I = (bench_instant*) bench_segq_emplace(&t->instants.q, sizeof(bench_instant));
        I->ts_ns = host_ns; I->lane_id = lane_id; I->name = dup_cstr(name); I->detail = res;
        bench_segq_publish(&t->instants.q, seg);
    }

    void bench_counter_point(benchmark *b, uint16_t lane_id, const char *name, uint64_t host_ns, double value) {
        if (!b) return;
        bench_thread *t = benchmark_get_thread(b);
        bench_seg_header *seg = t->counters.q.tail;
        bench_counter *C = (bench_counter*) bench_segq_emplace(&t->counters.q, sizeof(bench_counter));
        C->ts_ns = host_ns; C->lane_id = lane_id; C->name = dup_cstr(name); C->value = value;
        bench_segq_publish(&t->counters.q, seg);
    }

/* ======================= Clocks ======================= */

    /* This function is not thread safe as clocks should only be set once anyway */
    void bench_clock_set_offset(benchmark *b, int backend, uint16_t device_id, int64_t device_to_host_ns) {
        if (!b) return;
        for (uint32_t i = 0; i < b->clocks_sz; ++i) {
            if (b->clocks[i].backend == backend && b->clocks[i].device_id == device_id) {
                b->clocks[i].device_to_host_ns = device_to_host_ns;
                return;
            }
        }
        ensure_cap_void((void**)&b->clocks, sizeof(*b->clocks), &b->clocks_cap, b->clocks_sz + 1);
        b->clocks[b->clocks_sz++] = (bench_clock_offset){ backend, device_id, device_to_host_ns };
    }

    /* Not thread safe undefined behavior if called while device clock is being set */
    int bench_clock_get_offset(const benchmark *b, int backend, uint16_t device_id, int64_t *out) {
        if (!b) return -1;
        for (uint32_t i = 0; i < b->clocks_sz; ++i) {
            if (b->clocks[i].backend == backend && b->clocks[i].device_id == device_id) {
                if (out) *out = b->clocks[i].device_to_host_ns;
                return 1;
            }
        }
        return 0;
    }

/* ======================= Utilization ======================= */
    static int cmp_span_by_start_then_end(const void *a, const void *b) {
        const bench_span *A = (const bench_span*)a, *B = (const bench_span*)b;
        if (A->start_ns < B->start_ns) return -1;
        if (A->start_ns > B->start_ns) return 1;
        if (A->end_ns   < B->end_ns)   return -1;
        if (A->end_ns   > B->end_ns)   return 1;
        return 0;
    }

    double benchmark_utilization(const benchmark *b, const uint16_t *lane_ids, size_t nlanes, uint64_t *active_ns_out)
    {
        if (!b) return 0;
        uint32_t n = 0;
        for (uint32_t i = 0; i < b->spans_sz; ++i) {
            for (size_t j = 0; j < nlanes; ++j) if (b->spans[i].lane_id == lane_ids[j]) { n++; break; }
        }
        if (n == 0) { if (active_ns_out) *active_ns_out = 0; return 0.0; }

        bench_span *tmp = (bench_span*)malloc(n * sizeof(*tmp));
        if (!tmp) { perror("malloc"); exit(EXIT_FAILURE); }
        uint32_t k = 0;
        uint64_t min_ts = UINT64_MAX, max_ts = 0;
        for (uint32_t i = 0; i < b->spans_sz; ++i) {
            for (size_t j = 0; j < nlanes; ++j) if (b->spans[i].lane_id == lane_ids[j]) {
                tmp[k++] = b->spans[i];
                if (b->spans[i].start_ns < min_ts) min_ts = b->spans[i].start_ns;
                if (b->spans[i].end_ns   > max_ts) max_ts = b->spans[i].end_ns;
                break;
            }
        }
        qsort(tmp, n, sizeof(*tmp), cmp_span_by_start_then_end);

        uint64_t union_ns = 0;
        uint64_t cur_s = tmp[0].start_ns, cur_e = tmp[0].end_ns;
        for (uint32_t i = 1; i < n; ++i) {
            if (tmp[i].start_ns <= cur_e) { if (tmp[i].end_ns > cur_e) cur_e = tmp[i].end_ns; }
            else { union_ns += (cur_e - cur_s); cur_s = tmp[i].start_ns; cur_e = tmp[i].end_ns; }
        }
        union_ns += (cur_e - cur_s);
        free(tmp);

        uint64_t span_total = (max_ts > min_ts) ? (max_ts - min_ts) : 1;
        if (active_ns_out) *active_ns_out = union_ns;
        return (double)union_ns / (double)span_total;
    }

/* ======================= Exporters ======================= */
    int benchmark_export_csv(benchmark *b, const char *spans_csv_path, const char *lanes_csv_path)
    {
        if (!b) return -1;
        pthread_mutex_lock(&b->rollup_mu);
        benchmark_aggregate_locked(b);
        FILE *fs = fopen(spans_csv_path, "w"); if (!fs) return -1;
        fprintf(fs, "lane_id,lane_label,name,start_ns,end_ns,duration_ns,parent\n");
        for (uint32_t i = 0; i < b->spans_sz; ++i) {
            const bench_span *S = &b->spans[i];
            const char *lname = (S->lane_id < b->lanes_sz) ? b->lanes[S->lane_id].name : "";
            fprintf(fs, "%u,%s,%s,%llu,%llu,%llu,%u\n",
                    (unsigned)S->lane_id, lname, S->name,
                    (unsigned long long)S->start_ns,
                    (unsigned long long)S->end_ns,
                    (unsigned long long)(S->end_ns - S->start_ns),
                    (unsigned)S->parent);
        }
        fclose(fs);

        FILE *fl = fopen(lanes_csv_path, "w"); if (!fl) return -1;
        fprintf(fl, "lane_id,label,backend,device,queue\n");
        for (uint32_t i = 0; i < b->lanes_sz; ++i) {
            const bench_lane *L = &b->lanes[i];
            fprintf(fl, "%u,%s,%u,%u,%u\n",
                    (unsigned)L->id, L->name, L->backend, L->device_id, L->queue_id);
        }
        fclose(fl);
        pthread_mutex_unlock(&b->rollup_mu);
        return 0;
    }

    int benchmark_export_chrome_trace(benchmark *b, const char *json_path) {
        if (!b) return -1;
        pthread_mutex_lock(&b->rollup_mu);
        benchmark_aggregate_locked(b);
        FILE *f = fopen(json_path, "w"); if (!f) return -1;
        fputs("{\"traceEvents\":[\n", f);
        int first = 1;
        for (uint32_t i = 0; i < b->spans_sz; ++i) {
            const bench_span *S = &b->spans[i];
            double ts_us = (double)S->start_ns / 1000.0;
            double dur_us = (double)(S->end_ns - S->start_ns) / 1000.0;
            const bench_lane *L = (S->lane_id < b->lanes_sz) ? &b->lanes[S->lane_id] : NULL;
            int pid = L ? L->device_id : 0;
            int tid = L ? L->queue_id  : 0;
            if (!first) fputs(",\n", f); first = 0;
            fprintf(f, "{\"name\":\"%s\",\"ph\":\"X\",\"ts\":%.3f,\"dur\":%.3f,\"pid\":%d,\"tid\":%d}",
                    S->name, ts_us, dur_us, pid, tid);
        }
        fputs("\n]}\n", f);
        fclose(f);
        pthread_mutex_unlock(&b->rollup_mu);
        return 0;
    }