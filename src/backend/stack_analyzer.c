#include "stack.h"
#include "logger.h"
#include "now.h"
#include <stdatomic.h>


#define MAX_STACK_DEPTH 256

static struct active_event call_stack[MAX_STACK_DEPTH];
static size_t call_depth = 0;
static uint64_t ID = 0;
uint8_t* stack_top;


__attribute__((no_instrument_function))
void push_stack(void* fn) {
    if (call_depth >= MAX_STACK_DEPTH) return;
    uint64_t id = ID++;
    struct active_event event = {
        .fn = fn,
        .id = id,
        .stack_size = stack_top - get_sp(),
        .start_ns = bench_now_ns(),
    };

    call_stack[call_depth++] = event;

    log_stack_entry(fn, event.start_ns, event.stack_size, id, FUNCTION_ENTER);
}



__attribute__((no_instrument_function))
void pop_stack(void* fn) {
    if (call_depth == 0) return;

    struct active_event event = call_stack[--call_depth];
    uint64_t end = bench_now_ns();

    log_stack_entry(event.fn, end, stack_top - get_sp(), event.id, FUNCTION_EXIT);

    if (call_depth == 0) {
        // executable is likely to exit, flush buffers
        flush_logger();
    }
}

__attribute__((no_instrument_function))
uint64_t get_function_id(void) {
    return call_depth ? call_stack[call_depth - 1].id : 0;
}

__attribute__((no_instrument_function))
struct active_event get_active_event(void) {
    if (call_depth) return call_stack[call_depth - 1];
    struct active_event empty = {0};
    return empty;
}