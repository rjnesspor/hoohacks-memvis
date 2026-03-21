#define _GNU_SOURCE
#include <atomic>
#include <cstdint>
#include <array>
#include <stack>
#include <dlfcn.h>
#include <cJSON.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>


struct active_event {
    void* fn;
    uint64_t start_ns;
};

struct finished_trace_event {
    void* fn;
    uint64_t start_ns;
    uint64_t end_ns;
};

static constexpr size_t MAX_STACK_DEPTH = 256;
static constexpr size_t TRACE_BUFFER_SIZE = 1;

thread_local active_event call_stack[MAX_STACK_DEPTH];
thread_local size_t call_depth = 0;

thread_local finished_trace_event trace_buffer[TRACE_BUFFER_SIZE];
thread_local size_t trace_count = 0;



static constexpr const char* JSON_FILENAME = "backtrace.json";
static void* (*real_malloc)(size_t) = nullptr;
static void (*real_free)(void*) = nullptr;


static std::atomic<bool> logfile_initialized = false;


__attribute__((no_instrument_function))
static uint64_t to_ns(struct timespec ts) { return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec; }



__attribute__((no_instrument_function))
static uint64_t bench_now_ns(void) {
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






__attribute__((no_instrument_function))
static cJSON *initialize_file(void) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddArrayToObject(root, "traceEvents");
    return root;
}

__attribute__((no_instrument_function))
static cJSON * json_from_file(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return nullptr;

    fseek (fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    if (!real_malloc) {
        real_malloc = reinterpret_cast<void* (*)(size_t)>(dlsym(RTLD_NEXT, "malloc"));
    }

    char *buf = static_cast<char*>(real_malloc(len + 1));
    if (!buf) {
        fclose(fp);
        return nullptr;
    }


    fread(buf, 1, len, fp);
    fclose(fp);
    (buf)[len] = '\0';

    cJSON *root = cJSON_Parse(buf);

    if (!real_free) {
        real_free = reinterpret_cast<void (*)(void*)>(dlsym(RTLD_NEXT, "free"));
    }
    real_free(buf);

    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return nullptr;
    }

    return root;
}

__attribute__((no_instrument_function))
static void add_buffered_events_to_json(void){
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

    cJSON *trace_events = cJSON_GetObjectItem(root, "traceEvents");
    if (!trace_events || !cJSON_IsArray(trace_events)) {
        cJSON_Delete(root);
        return; 
    }
    
    for (int i = 0; i < trace_count; i++) {
        const finished_trace_event& function = trace_buffer[i];
        cJSON* event = cJSON_CreateObject();

        Dl_info info{};
        const char* name = "<unknown>";
        if (dladdr(trace_buffer[i].fn, &info) && info.dli_sname) {
            name = info.dli_sname;
        }

        cJSON_AddStringToObject(event, "name", name);
        cJSON_AddStringToObject(event, "cat", "Function Trace");
        cJSON_AddStringToObject(event, "ph", "X");
        cJSON_AddNumberToObject(event, "ts", static_cast<double>(function.start_ns) / double(1000));
        cJSON_AddNumberToObject(event, "dur", (static_cast<double>(function.end_ns) - static_cast<double>(function.start_ns)) / double(1000));
        cJSON_AddNumberToObject(event, "pid", static_cast<double>(0));
        cJSON_AddNumberToObject(event, "tid", static_cast<double>(tid));

        cJSON_AddItemToArray(trace_events, event);
    }

    char* printed = cJSON_Print(root);
    if (printed) {
        FILE* fp = fopen(JSON_FILENAME, "w");
        if (fp) {
            fputs(printed, fp);
            fclose(fp);
            logfile_initialized = true;
        }
        cJSON_free(printed);
    }

    cJSON_Delete(root);
}

__attribute__((no_instrument_function))
static void record_enter(void *fn) {
    if(call_depth >= MAX_STACK_DEPTH) return; // no more space to hold :(
    call_stack[call_depth++] = active_event{fn, bench_now_ns()};
}

__attribute__((no_instrument_function))
static void func_exit(void) {
    if (call_depth == 0) return;

    active_event ev = call_stack[--call_depth];
    uint64_t end = bench_now_ns();

    if (trace_count == TRACE_BUFFER_SIZE) {
        add_buffered_events_to_json();
        trace_count = 0;
    }


    trace_buffer[trace_count++] = finished_trace_event{
        ev.fn,
        ev.start_ns,
        end
    };

    if (call_depth == 0) {
        // likely going to exit, flush buffers
        add_buffered_events_to_json();
        trace_count = 0;
    }
}

extern "C" {

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site) {
    (void) call_site;
    record_enter(this_fn);
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site) {
    (void)this_fn;
    (void)call_site;
    func_exit();
}

}