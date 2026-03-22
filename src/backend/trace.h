#ifndef TRACE_H
#define TRACE_H

__attribute__((no_instrument_function))
void func_exit(void* fn);

#endif // TRACE_H