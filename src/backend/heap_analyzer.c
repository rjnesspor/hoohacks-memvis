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

#define OUTPUT_FILE "output.json"
#define BT_BUF_SIZE 100

static int in_wrapper = 0;

int tracer_busy = 0;

void log_event(char* event_type, void* ptr, void* old_ptr, char** bt, int bt_count, size_t size, double timestamp);

void* (*real_malloc)(size_t) = NULL;
void* (*real_calloc)(size_t, size_t) = NULL;
void* (*real_realloc)(void*, size_t) = NULL;
void (*real_free)(void*) = NULL;

__attribute__((constructor))
static void init_wrappers() {

    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_free = dlsym(RTLD_NEXT, "free");


    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    size_t stack_size;
    uint8_t *stack_addr;
    pthread_attr_getstack(&attr, &stack_addr, &stack_size);
    stack_top = stack_addr + stack_size;



    fprintf(stderr, "wrap.so constructor ran.\nreal_malloc=%p\nreal_calloc=%p\nreal_realloc=%p\nreal_free=%p\n",
        (void*)real_malloc, (void*)real_calloc, (void*)real_realloc, (void*)real_free);
}

void log_event(char* event_type, void* ptr, void* old_ptr, char** bt, int bt_count, size_t size, double timestamp) {
    if (tracer_busy) return;
    cJSON* root = NULL;
    FILE* fp = NULL;

    fp = fopen(OUTPUT_FILE, "a");
    if (!fp) return;

    cJSON* obj = cJSON_CreateObject();
    
    cJSON_AddStringToObject(obj, "event", event_type);

    char addr_buf[32];
    snprintf(addr_buf, sizeof(addr_buf), "%p", ptr);
    cJSON_AddStringToObject(obj, "ptr", addr_buf);

    if (old_ptr) {
        char oldptr_buf[32];
        snprintf(oldptr_buf, sizeof(oldptr_buf), "%p", old_ptr);
        cJSON_AddStringToObject(obj, "old_ptr", oldptr_buf);
    }

    cJSON_AddNumberToObject(obj, "size", size);

    cJSON* stack = cJSON_CreateArray();
    for (int i = 0; i < bt_count; i++) {
        cJSON_AddItemToArray(stack, cJSON_CreateString(bt[i]));
    }
    cJSON_AddItemToObject(obj, "stack", stack);

    cJSON_AddNumberToObject(obj, "id", get_function_id());

    cJSON_AddNumberToObject(obj, "ts", timestamp);

    char* output = cJSON_PrintUnformatted(obj);

    fputs(output, fp);
    fputc('\n', fp);
    fclose(fp);

    cJSON_free(output);
    cJSON_Delete(obj);
}

void* malloc(size_t size) {
    double now_us = bench_now_ns() / 1000;

    if (in_wrapper) {
        return real_malloc(size);
    }

    in_wrapper = 1;
    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    void* ptr = real_malloc(size);
    log_event("malloc", ptr, NULL, strings, nptrs, size, now_us);
    real_free(strings);

    in_wrapper = 0;

    return ptr;
}

void* calloc(size_t num, size_t size) {
    double now_us = bench_now_ns() / 1000;

    if (in_wrapper) {
        return real_calloc(num, size);
    }

    in_wrapper = 1;
    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    void* ptr = real_calloc(num, size);
    log_event("calloc", ptr, NULL, strings, nptrs, num * size, now_us);

    real_free(strings);
    in_wrapper = 0;

    return ptr;
}

void* realloc(void* ptr, size_t new_size) {
    double now_us = bench_now_ns() / 1000;

    if (in_wrapper) {
        return real_realloc(ptr, new_size);
    }

    in_wrapper = 1;
    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    void* new_ptr = real_realloc(ptr, new_size);
    log_event("realloc", new_ptr, ptr, strings, nptrs, new_size, now_us);

    real_free(strings);

    in_wrapper = 0;

    return new_ptr;
}

void free(void* ptr) {
    double now_us = bench_now_ns() / 1000;

    if (in_wrapper) {
        return real_free(ptr);
    }

    in_wrapper = 1;

    int nptrs;
    void* buffer[BT_BUF_SIZE];
    char** strings;

    nptrs = backtrace(buffer, BT_BUF_SIZE);
    strings = backtrace_symbols(buffer, nptrs);

    log_event("free", ptr, NULL, strings, nptrs, 0, now_us);
    real_free(ptr);
    real_free(strings);

    in_wrapper = 0;
}