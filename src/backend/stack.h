#ifndef STACK_H
#define STACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <pthread.h>


struct active_event {
    void* fn;
    uint64_t start_ns;
    uint64_t id;
    size_t stack_size;
};


extern uint8_t* stack_top;

__attribute__((no_instrument_function))
uint64_t get_function_id(void);

__attribute__((no_instrument_function))
struct active_event get_active_event(void);


__attribute__((no_instrument_function))
void push_stack(void* fn);

__attribute__((no_instrument_function))
void pop_stack(void* fn);

__attribute__((no_instrument_function))
static inline uint8_t* get_sp(void) {
    void *sp;
    asm volatile("mov %%rsp, %0" : "=r"(sp));
    return (uint8_t*)(sp);
}


#ifdef __cplusplus
}
#endif


#endif