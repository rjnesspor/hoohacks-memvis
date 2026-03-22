#include "logger.h"
#include "real_heap_functions.h"
#include "tracer_state.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>

cJSON *root = NULL;

__attribute__((no_instrument_function))
static cJSON* json_from_file(const char*);

__attribute__((no_instrument_function))
static void flush_buffers(bool trace, bool stack, bool heap);

#define BACKTRACE_MAX_STRING 256
#define MAX_BUFFERED_TRACE 64
#define MAX_BUFFERED_STACK 64
#define MAX_BUFFERED_HEAP 16

struct finished_trace_event {
    void* fn;
    uint64_t start_ns;
    uint64_t end_ns;
    uint64_t id;
};

struct finished_stack_memory {
    void*fn;
    uint64_t ts_ns;
    uint64_t id;
    size_t size;
    function_event;
};

struct finished_heap_memory {
    uint8_t* ptr;
    size_t size;
    uint64_t id;
    uint64_t ts_ns;
    char bt[BACKTRACE_MAX_STRING];
};

static struct finished_trace_event trace_buffer[MAX_BUFFERED_TRACE];
static struct finished_stack_memory stack_buffer[MAX_BUFFERED_STACK];
static struct finished_heap_memory heap_buffer[MAX_BUFFERED_HEAP];

size_t trace_count = 0, stack_count =  0, heap_count = 0;

__attribute__((no_instrument_function))
void init_logger(const char* fn) {
    root = cJSON_CreateObject();
    cJSON_AddArrayToObject(root, "traceEvents");
    cJSON_AddArrayToObject(root, "stackMemory");
    cJSON_AddArrayToObject(root, "heapMemory");
}

__attribute__((no_instrument_function))
void flush_logger(void){
    flush_buffers(true, true, true);
}

__attribute__((no_instrument_function))
void log_heap_entry(void* fn, uint64_t start, uint64_t end, uint64_t id, ) {
    if (!root) return;

    if (trace_count >= MAX_BUFFERED_TRACE) {
        flush_buffers(true, false, false);
        trace_count = 0;
    }

    struct finished_trace_event trace = (
        fn,
        start,
        end,
        id
    );

    trace_buffer[trace_count++] = trace;
}


__attribute__((no_instrument_function))
void log_stack_entry(void* fn, uint64_t timestamp, size_t size, enum function_event event) {
    if (!root) return;

    if (stack_count >= MAX_BUFFERED_STACK) {
        flush_buffers(false, true, true);
        stack_count = 0;
    }

    struct finished_stack_memory stack = (
        fn,
        timestamp,
        size,
        event
    );

    stack_buffer[stack_count++] = stack;
}

__attribute__((no_instrument_function))
void log_function_entry(void* ptr, size_t size, uint64_t id, uint64_t timestamp, const char* backtrace) {
    if (!root) return;

    if (heap_count >= MAX_BUFFERED_HEAP) {
        flush_buffers(false, false, true);
        heap_count = 0;
    }

    struct finished_heap_memory heap = (
        (uint8_t*)ptr,
        size,
        id,
        timestamp,
    );

    snprintf(&heap.bt, BACKTRACE_MAX_STRING, "%s", backtrace);

    heap_buffer[heap_count++] = heap;
}

__attribute__((no_instrument_function))
static cJSON * json_from_file(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    fseek (fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    char *buf = (char*)( real_malloc(len + 1));
    if (!buf) {
        fclose(fp);
        return NULL;
    }


    fread(buf, 1, len, fp);
    fclose(fp);
    (buf)[len] = '\0';

    cJSON *root = cJSON_Parse(buf);

    real_free(buf);

    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

__attribute__((no_instrument_function))
static void flush_buffers(bool trace, bool stack, bool heap) {
    tracer_busy = 1;
    struct cJSON *root = NULL;
    struct stat buffer;
    pid_t tid = gettid();  
    bool logfile_exists = (stat (JSON_FILENAME, &buffer) == 0); 


    if (!logfile_initialized || !logfile_exists)
        root = initialize_file();
    else {
        root = json_from_file(JSON_FILENAME);
    }
    if (!root) return;
}