#include "trace.h"
#include "logger.h"
#include "stack.h"
#include "now.h"

__attribute__((no_instrument_function))
void func_exit(void* fn) {
    struct active_event exiting_func = get_active_event();

    log_function_entry(exiting_func.fn, exiting_func.start_ns, bench_now_ns(), exiting_func.id);   
}