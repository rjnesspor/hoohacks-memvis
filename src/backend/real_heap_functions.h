#ifndef REAL_HEAP_FUNCTIONS_H
#define REAL_HEAP_FUNCTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern void* (*real_malloc)(size_t);
extern void* (*real_calloc)(size_t, size_t);
extern void* (*real_realloc)(void*, size_t);
extern void (*real_free)(void*);


#ifdef __cplusplus
}
#endif

#endif //REAL_HEAP_FUNCTIONS_H