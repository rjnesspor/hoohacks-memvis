#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>


extern int tracer_busy;

enum function_event {
    FUNCTION_ENTER,
    FUNCTION_EXIT
};

enum heap_event {
    HEAP_MALLOC,
    HEAP_CALLOC,
    HEAP_REALLOC,
    HEAP_FREE
};



__attribute__((no_instrument_function))
void log_function_entry(void* fn, uint64_t start, uint64_t end, uint64_t id);

__attribute__((no_instrument_function))
void log_stack_entry(void* fn, uint64_t timestamp, size_t size, uint64_t id, enum function_event event);

__attribute__((no_instrument_function))
void log_heap_entry(void* ptr, void* old_ptr, size_t size, uint64_t id, uint64_t timestamp, char** backtrace, int bt_count, enum heap_event event);

__attribute__((no_instrument_function))
void flush_logger(void);


#ifdef __cplusplus
}
#endif



#endif // LOGGER_H