#define _GNU_SOURCE
#include "logger.h"
#include "real_heap_functions.h"
#include "wrap_state.h"
#include <cJSON.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>

cJSON *root = NULL;

__attribute__((no_instrument_function))
static cJSON* json_from_file(const char*);

__attribute__((no_instrument_function))
static void flush_buffers(bool trace, bool stack, bool heap);


#define BACKTRACE_MAX_STRING 256
#define BACKTRACE_MAX_FRAMES 10

#define JSON_MAX_FILENAME 256
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
    enum function_event event;
};

struct finished_heap_memory {
    uint8_t* ptr;
    uint8_t* old_ptr;
    size_t size;
    uint64_t id;
    uint64_t ts_ns;
    enum heap_event event;
    char bt[BACKTRACE_MAX_FRAMES][BACKTRACE_MAX_STRING];
    int bt_count;
};

static struct finished_trace_event trace_buffer[MAX_BUFFERED_TRACE];
static struct finished_stack_memory stack_buffer[MAX_BUFFERED_STACK];
static struct finished_heap_memory heap_buffer[MAX_BUFFERED_HEAP];

size_t trace_count = 0, stack_count =  0, heap_count = 0;

static char json_file[JSON_MAX_FILENAME];

bool initialized = false;


__attribute__((no_instrument_function))
cJSON* init_logger(const char* fn) {
    root = cJSON_CreateObject();
    cJSON_AddArrayToObject(root, "traceEvents");
    cJSON_AddArrayToObject(root, "stackMemory");
    cJSON_AddArrayToObject(root, "heapMemory");
    snprintf(json_file, JSON_MAX_FILENAME, "%s", fn);
    initialized = true;
    return root;
}

__attribute__((no_instrument_function))
void flush_logger(void){
    flush_buffers(true, true, true);
}

__attribute__((no_instrument_function))
void log_function_entry(void* fn, uint64_t start, uint64_t end, uint64_t id) {


    if (trace_count >= MAX_BUFFERED_TRACE) {
        flush_buffers(true, false, false);
        trace_count = 0;
    }

    struct finished_trace_event trace = {
        .fn = fn,
        .start_ns = start,
        .end_ns = end,
        .id = id
    };

    trace_buffer[trace_count++] = trace;
}


__attribute__((no_instrument_function))
void log_stack_entry(void* fn, uint64_t timestamp, size_t size, uint64_t id, enum function_event event) {


    if (stack_count >= MAX_BUFFERED_STACK) {
        flush_buffers(false, true, true);
        stack_count = 0;
    }

    struct finished_stack_memory stack = {
        .fn = fn,
        .ts_ns = timestamp,
        .size = size,
        .id = id,
        .event = event
    };

    stack_buffer[stack_count++] = stack;
}

__attribute__((no_instrument_function))
void log_heap_entry(void* ptr, void* old_ptr, size_t size, uint64_t id, uint64_t timestamp, char** backtrace, int bt_count, enum heap_event event) {


    if (heap_count >= MAX_BUFFERED_HEAP) {
        flush_buffers(false, false, true);
        heap_count = 0;
    }

    struct finished_heap_memory heap = {
        .ptr = (uint8_t*)ptr,
        .old_ptr = (uint8_t*)old_ptr,
        .size = size,
        .id = id,
        .ts_ns = timestamp,
        .event = event,
        .bt_count = (bt_count > BACKTRACE_MAX_FRAMES) ? BACKTRACE_MAX_FRAMES : bt_count
    };

    for (int i = 0; i < heap.bt_count; i++) {
        snprintf(heap.bt[i], BACKTRACE_MAX_STRING, "%s", backtrace[i]);
    }

    heap_buffer[heap_count++] = heap;
}

__attribute__((no_instrument_function))
static cJSON * json_from_file(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        return NULL;
    }

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
    if (!trace && !stack && !heap) return;
    
    in_wrapper = 1;

    struct cJSON *root = NULL;
    struct stat buffer;
    pid_t tid = gettid();  
    bool logfile_exists = (stat (json_file, &buffer) == 0); 


    if (!logfile_exists || !initialized)
        root = init_logger("logfile.json");
    else 
        root = json_from_file(json_file);


    if (trace) {
        cJSON *trace_events = cJSON_GetObjectItem(root, "traceEvents");
        if (trace_events && cJSON_IsArray(trace_events)) {
            
            for (int i = 0; i < trace_count; i++) {
                const struct finished_trace_event function = trace_buffer[i];
                cJSON* event = cJSON_CreateObject();
                Dl_info info;
                const char* name = "<unknown>";
                if (dladdr(trace_buffer[i].fn, &info) && info.dli_sname) {
                    name = info.dli_sname;
                }

                cJSON_AddStringToObject(event, "name", name);
                cJSON_AddStringToObject(event, "cat", "Function Trace");
                cJSON_AddStringToObject(event, "ph", "X");
                cJSON_AddNumberToObject(event, "ts", (double)(function.start_ns) / (double)1000);
                cJSON_AddNumberToObject(event, "dur", ((double)function.end_ns - (double)function.start_ns) / (double)1000);
                cJSON_AddNumberToObject(event, "pid", (double)0);
                cJSON_AddNumberToObject(event, "tid", (double)tid);
                cJSON_AddNumberToObject(event, "id", (double)function.id);

                cJSON_AddItemToArray(trace_events, event);
            }
        }
    }

    if (stack) {
        cJSON *stack_memory = cJSON_GetObjectItem(root, "stackMemory");
        if (stack_memory && cJSON_IsArray(stack_memory)) {

            for (int i = 0; i < stack_count; i++) {
                const struct finished_stack_memory ev = stack_buffer[i];

                cJSON *event = cJSON_CreateObject();

                Dl_info info;
                const char* name = "<unknown>";
                if (dladdr(ev.fn, &info) && info.dli_sname) {
                    name = info.dli_sname;
                }

                const char* event_type =
                    (ev.event == FUNCTION_ENTER) ? "function enter" : "function exit";

                cJSON_AddStringToObject(event, "event", event_type);
                cJSON_AddStringToObject(event, "name", name);
                cJSON_AddNumberToObject(event, "ts", (double)ev.ts_ns / 1000.0);
                cJSON_AddNumberToObject(event, "size", (double)ev.size);

                cJSON_AddItemToArray(stack_memory, event);
            }
        }
    }

    if (heap) {
        cJSON *heap_memory = cJSON_GetObjectItem(root, "heapMemory");
        if (heap_memory && cJSON_IsArray(heap_memory)) {

            for (int i = 0; i < heap_count; i++) {
                const struct finished_heap_memory ev = heap_buffer[i];

                cJSON *event = cJSON_CreateObject();

                // event type
                const char *event_str;
                switch (ev.event) {
                    case HEAP_MALLOC:  event_str = "malloc"; break;
                    case HEAP_CALLOC:  event_str = "calloc"; break;
                    case HEAP_REALLOC: event_str = "realloc"; break;
                    case HEAP_FREE:    event_str = "free"; break;
                    default:           event_str = "<unknown>";
                }

                cJSON_AddStringToObject(event, "event", event_str);

                // ptr
                char ptr_str[32];
                snprintf(ptr_str, sizeof(ptr_str), "%p", ev.ptr);
                cJSON_AddStringToObject(event, "ptr", ptr_str);

                // old_ptr (only for realloc)
                if (ev.old_ptr) {
                    char old_ptr_str[32];
                    snprintf(old_ptr_str, sizeof(old_ptr_str), "%p", ev.old_ptr);
                    cJSON_AddStringToObject(event, "old_ptr", old_ptr_str);
                }

                cJSON_AddNumberToObject(event, "size", (double)ev.size);
                cJSON_AddNumberToObject(event, "id", (double)ev.id);
                cJSON_AddNumberToObject(event, "ts", (double)ev.ts_ns);

                // stack array
                cJSON *stack_arr = cJSON_CreateArray();
                for (int j = 0; j < ev.bt_count; j++) {
                    cJSON_AddItemToArray(stack_arr, cJSON_CreateString(ev.bt[j]));
                }
                cJSON_AddItemToObject(event, "stack", stack_arr);

                cJSON_AddItemToArray(heap_memory, event);
            }
        }
    }

    char *out = cJSON_Print(root);
    if (out) {
        FILE *fp = fopen(json_file, "w");
        if (fp) {
            fputs(out, fp);
            fclose(fp);
        }
        cJSON_free(out);
    }

    cJSON_Delete(root);
    root = NULL;
    in_wrapper = 0;
}