#define _GNU_SOURCE
#include "real_heap_functions.h"
#include "logger.h"
#include "stack.h"
#include "trace.h"
#include <stdio.h>
#include <pthread.h>
#include "wrap_state.h"
#include <dlfcn.h>


void* (*real_malloc)(size_t) = NULL;
void* (*real_calloc)(size_t, size_t) = NULL;
void* (*real_realloc)(void*, size_t) = NULL;
void (*real_free)(void*) = NULL;

int tracing_enabled = 0;
__attribute__((tls_model("initial-exec"))) __thread int in_wrapper = 0;

__attribute__((constructor))
static void init_wrappers() {

    in_wrapper = 1;

    real_malloc = dlsym(RTLD_NEXT, "malloc");
    real_calloc = dlsym(RTLD_NEXT, "calloc");
    real_realloc = dlsym(RTLD_NEXT, "realloc");
    real_free = dlsym(RTLD_NEXT, "free");

    stack_top = get_sp();

    //init_logger("logfile.json");

    in_wrapper = 0;
    tracing_enabled = 1;
}


__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *this_fn, void *call_site) {
    (void) call_site;
    push_stack(this_fn);
}


__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *this_fn, void *call_site) {
    (void)call_site;
    func_exit(this_fn);
    pop_stack(this_fn);
}