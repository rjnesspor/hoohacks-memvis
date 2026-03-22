#ifndef LOGGER_H
#define LOGGER_H

#ifdef __cplusplus
extern "C" {
#endif

enum function_event {
    function_enter,
    function_exit
};

enum heap_event {
    heap_malloc,
    heap_calloc,
    heap_realloc,
    heap_free
};

void init_logger(const char* fn);

void log_heap_entry();

void log_stack_entry();

void log_function_entry();


#ifdef __cplusplus
}
#endif



#endif // LOGGER_H