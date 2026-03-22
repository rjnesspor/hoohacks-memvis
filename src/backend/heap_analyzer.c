#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <execinfo.h>
#include <cJSON.h>
#include <pthread.h>
#include "now.h"
#include "stack.h"
#include "logger.h"
#include "wrap_state.h"
#include "real_heap_functions.h"

#define BT_BUF_SIZE 100

__attribute__((no_instrument_function))
void* malloc(size_t size) {
    if (!real_malloc) return NULL;

    if (!tracing_enabled || in_wrapper) {
        return real_malloc(size);
    }

    in_wrapper = 1;
    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    void* ptr = real_malloc(size);

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);
    
    log_heap_entry(ptr, NULL, size, get_function_id(), bench_now_ns(), strings, nptrs, HEAP_MALLOC);
    //log_event("malloc", ptr, NULL, strings, nptrs, size, now_us);
    real_free(strings);

    in_wrapper = 0;

    return ptr;
}

__attribute__((no_instrument_function))
void* calloc(size_t num, size_t size) {
    if (!real_calloc) return NULL;

    if (!tracing_enabled || in_wrapper) {
        return real_calloc(num, size);
    }


    in_wrapper = 1;
    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    void* ptr = real_calloc(num, size);

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    log_heap_entry(ptr, NULL, size, get_function_id(), bench_now_ns(), strings, nptrs, HEAP_CALLOC);
    //log_event("calloc", ptr, NULL, strings, nptrs, num * size, now_us);

    real_free(strings);
    in_wrapper = 0;

    return ptr;
}

__attribute__((no_instrument_function))
void* realloc(void* ptr, size_t new_size) {
    if (!real_realloc) return NULL;
    
    if (!tracing_enabled || in_wrapper) {
        return real_realloc(ptr, new_size);
    }

    in_wrapper = 1;
    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    void* new_ptr = real_realloc(ptr, new_size);

    log_heap_entry(new_ptr, ptr, new_size, get_function_id(), bench_now_ns(), strings, nptrs, HEAP_REALLOC);
    //log_event("realloc", new_ptr, ptr, strings, nptrs, new_size, now_us);

    real_free(strings);

    in_wrapper = 0;

    return new_ptr;
}

__attribute__((no_instrument_function))
void free(void* ptr) {
    if (!real_free) return;

    if (!tracing_enabled || in_wrapper) {
        return real_free(ptr);
    }

    in_wrapper = 1;

    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    log_heap_entry(ptr, NULL, 0, get_function_id(), bench_now_ns(), strings, nptrs, HEAP_FREE);
    //log_event("free", ptr, NULL, strings, nptrs, 0, now_us);
    real_free(ptr);
    real_free(strings);

    in_wrapper = 0;
}