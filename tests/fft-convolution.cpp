#include <fftw3.h>
#include <stdint.h>
#include <stddef.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

// ------------------------------------------------------------
// Configuration
// ------------------------------------------------------------
const size_t N_FFT   = 8192;
const size_t N_FIR   = 933;
const size_t L_BLOCK = N_FFT - N_FIR + 1;
const size_t SIG_LEN = 12'000'000;

// ------------------------------------------------------------
// Global buffers
// ------------------------------------------------------------
fftw_complex *signal_in      = nullptr;
fftw_complex *signal_out     = nullptr;
fftw_complex *filter_in      = nullptr;

fftw_complex *signal_frames  = nullptr;
fftw_complex *signal_spectra = nullptr;
fftw_complex *filter_spectra = nullptr;

// ------------------------------------------------------------
// Small utility helpers
// ------------------------------------------------------------
static inline void complex_set(fftw_complex &x, double re, double im) {
    x[0] = re;
    x[1] = im;
}

static inline void complex_zero(fftw_complex &x) {
    x[0] = 0.0;
    x[1] = 0.0;
}

static inline void complex_mul_inplace(fftw_complex &a, const fftw_complex &b) {
    double re = a[0] * b[0] - a[1] * b[1];
    double im = a[0] * b[1] + a[1] * b[0];
    a[0] = re;
    a[1] = im;
}

static inline void complex_scale_inplace(fftw_complex &a, double s) {
    a[0] *= s;
    a[1] *= s;
}

// ------------------------------------------------------------
// Intentionally split-up helpers for trace depth
// ------------------------------------------------------------
double *allocate_scratch_real(size_t n) {
    return (double*)malloc(n * sizeof(double));
}

void free_scratch_real(double *p) {
    free(p);
}

size_t *allocate_scratch_indices(size_t n) {
    return (size_t*)malloc(n * sizeof(size_t));
}

void free_scratch_indices(size_t *p) {
    free(p);
}

void touch_scratch_memory(double *scratch, size_t *indices, size_t n) {
    for (size_t i = 0; i < n; i++) {
        scratch[i] = (double)(i % 17) * 0.125;
        indices[i] = i;
    }
}

void initialize_signal_sample(fftw_complex &x, size_t i) {
    // A not-too-boring synthetic complex signal
    double t = (double)i;
    double re = std::sin(2.0 * M_PI * 0.0007 * t) + 0.35 * std::cos(2.0 * M_PI * 0.0031 * t);
    double im = 0.25 * std::sin(2.0 * M_PI * 0.0013 * t);
    complex_set(x, re, im);
}

void initialize_filter_sample(fftw_complex &x, size_t i) {
    // A simple decaying complex FIR
    double env = std::exp(-0.008 * (double)i);
    double phase = 2.0 * M_PI * 0.01 * (double)i;
    double re = env * std::cos(phase);
    double im = env * std::sin(phase);
    complex_set(x, re, im);
}

void fill_signal() {
    for (size_t i = 0; i < SIG_LEN; i++) {
        initialize_signal_sample(signal_in[i], i);
    }
}

void fill_filter() {
    for (size_t i = 0; i < N_FIR; i++) {
        initialize_filter_sample(filter_in[i], i);
    }
}

void zero_complex_buffer(fftw_complex *buf, size_t n) {
    for (size_t i = 0; i < n; i++) {
        complex_zero(buf[i]);
    }
}

void clear_output_buffer() {
    zero_complex_buffer(signal_out, SIG_LEN + N_FIR - 1);
}

void copy_filter_to_frame(fftw_complex *frame) {
    zero_complex_buffer(frame, N_FFT);
    for (size_t i = 0; i < N_FIR; i++) {
        frame[i][0] = filter_in[i][0];
        frame[i][1] = filter_in[i][1];
    }
}

void preprocess_filter(fftw_plan forward_plan) {
    copy_filter_to_frame(signal_frames);
    fftw_execute(forward_plan);

    for (size_t i = 0; i < N_FFT; i++) {
        filter_spectra[i][0] = signal_spectra[i][0];
        filter_spectra[i][1] = signal_spectra[i][1];
    }
}

void zero_pad_signal_block_into_frame(size_t offset) {
    zero_complex_buffer(signal_frames, N_FFT);

    size_t remaining = (offset < SIG_LEN) ? (SIG_LEN - offset) : 0;
    size_t copy_count = (remaining < L_BLOCK) ? remaining : L_BLOCK;

    for (size_t i = 0; i < copy_count; i++) {
        signal_frames[i][0] = signal_in[offset + i][0];
        signal_frames[i][1] = signal_in[offset + i][1];
    }
}

void multiply_spectra_with_filter() {
    for (size_t i = 0; i < N_FFT; i++) {
        complex_mul_inplace(signal_spectra[i], filter_spectra[i]);
    }
}

void normalize_ifft_output() {
    const double scale = 1.0 / (double)N_FFT;
    for (size_t i = 0; i < N_FFT; i++) {
        complex_scale_inplace(signal_frames[i], scale);
    }
}

void overlap_add_into_output(size_t offset) {
    size_t out_len = SIG_LEN + N_FIR - 1;
    for (size_t i = 0; i < N_FFT; i++) {
        size_t out_idx = offset + i;
        if (out_idx >= out_len) break;

        signal_out[out_idx][0] += signal_frames[i][0];
        signal_out[out_idx][1] += signal_frames[i][1];
    }
}

void do_extra_demo_allocations(size_t offset) {
    // Not needed for convolution; intentionally here to create more heap activity.
    const size_t scratch_n = 64 + (offset / L_BLOCK) % 64;

    double *scratch = allocate_scratch_real(scratch_n);
    size_t *indices = allocate_scratch_indices(scratch_n);

    if (scratch && indices) {
        touch_scratch_memory(scratch, indices, scratch_n);
    }

    free_scratch_real(scratch);
    free_scratch_indices(indices);
}

void convolve_one_block(size_t offset, fftw_plan forward_plan, fftw_plan inverse_plan) {
    do_extra_demo_allocations(offset);
    zero_pad_signal_block_into_frame(offset);
    fftw_execute(forward_plan);
    multiply_spectra_with_filter();
    fftw_execute(inverse_plan);
    normalize_ifft_output();
    overlap_add_into_output(offset);
}

size_t number_of_blocks() {
    return (SIG_LEN + L_BLOCK - 1) / L_BLOCK;
}

void convolve_all_blocks(fftw_plan forward_plan, fftw_plan inverse_plan) {
    size_t blocks = number_of_blocks();
    for (size_t b = 0; b < blocks; b++) {
        size_t offset = b * L_BLOCK;
        convolve_one_block(offset, forward_plan, inverse_plan);

        // Occasional progress so the program visibly does work.
        if ((b % 200) == 0) {
            std::printf("Processed block %zu / %zu\n", b, blocks);
        }
    }
}

double compute_energy_of_prefix(size_t n) {
    double acc = 0.0;
    size_t out_len = SIG_LEN + N_FIR - 1;
    if (n > out_len) n = out_len;

    for (size_t i = 0; i < n; i++) {
        acc += signal_out[i][0] * signal_out[i][0] + signal_out[i][1] * signal_out[i][1];
    }
    return acc;
}

void print_some_output_samples() {
    std::printf("\nFirst 8 output samples:\n");
    for (size_t i = 0; i < 8; i++) {
        std::printf("y[%zu] = %.6f + %.6fi\n", i, signal_out[i][0], signal_out[i][1]);
    }
}

void print_summary() {
    size_t out_len = SIG_LEN + N_FIR - 1;
    double energy = compute_energy_of_prefix(10000);

    std::printf("\nSummary\n");
    std::printf("  N_FFT   = %zu\n", N_FFT);
    std::printf("  N_FIR   = %zu\n", N_FIR);
    std::printf("  L_BLOCK = %zu\n", L_BLOCK);
    std::printf("  SIG_LEN = %zu\n", SIG_LEN);
    std::printf("  OUT_LEN = %zu\n", out_len);
    std::printf("  Blocks  = %zu\n", number_of_blocks());
    std::printf("  Prefix energy(10000) = %.6f\n", energy);

    print_some_output_samples();
}

// ------------------------------------------------------------
// Allocation / cleanup
// ------------------------------------------------------------
void allocate_main_buffers() {
    signal_in      = fftw_alloc_complex(SIG_LEN);
    signal_out     = fftw_alloc_complex(SIG_LEN + N_FIR - 1);
    filter_in      = fftw_alloc_complex(N_FIR);

    signal_frames  = fftw_alloc_complex(N_FFT);
    signal_spectra = fftw_alloc_complex(N_FFT);
    filter_spectra = fftw_alloc_complex(N_FFT);

    if (!signal_in || !signal_out || !filter_in ||
        !signal_frames || !signal_spectra || !filter_spectra) {
        std::fprintf(stderr, "Allocation failure\n");
        std::exit(1);
    }
}

void free_main_buffers() {
    if (signal_in)      fftw_free(signal_in);
    if (signal_out)     fftw_free(signal_out);
    if (filter_in)      fftw_free(filter_in);
    if (signal_frames)  fftw_free(signal_frames);
    if (signal_spectra) fftw_free(signal_spectra);
    if (filter_spectra) fftw_free(filter_spectra);
}

int main() {
    allocate_main_buffers();

    fill_signal();
    fill_filter();
    clear_output_buffer();

    fftw_plan forward = fftw_plan_dft_1d(
        (int)N_FFT,
        signal_frames,
        signal_spectra,
        FFTW_FORWARD,
        FFTW_ESTIMATE
    );

    fftw_plan inverse = fftw_plan_dft_1d(
        (int)N_FFT,
        signal_spectra,
        signal_frames,
        FFTW_BACKWARD,
        FFTW_ESTIMATE
    );

    if (!forward || !inverse) {
        std::fprintf(stderr, "Failed to create FFTW plans\n");
        free_main_buffers();
        return 1;
    }

    preprocess_filter(forward);
    convolve_all_blocks(forward, inverse);
    print_summary();

    fftw_destroy_plan(forward);
    fftw_destroy_plan(inverse);
    free_main_buffers();
    return 0;
}