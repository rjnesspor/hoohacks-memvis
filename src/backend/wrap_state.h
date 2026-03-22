#pragma once

extern int tracing_enabled;
extern __attribute__((tls_model("initial-exec"))) __thread int in_wrapper;
