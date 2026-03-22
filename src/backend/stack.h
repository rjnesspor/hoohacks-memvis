#ifndef STACK_H
#define STACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <pthread.h>


uint64_t get_function_id(void);

extern uint8_t* stack_top;


#ifdef __cplusplus
}
#endif


#endif