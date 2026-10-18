#ifndef VMN_FFT_H
#define VMN_FFT_H

#include <stddef.h>

/** Builds the twiddle table. Call once at start-up, before any task uses vmn_fft. */
void vmn_fft_init(void);

/**
 * In-place radix-2 complex FFT without normalisation:
 * X[k] = sum over n of x[n] * exp(-2*pi*i*k*n/N).
 * n must be a power of two between 2 and VMN_FFT_MAX_LEN. Returns 0, or -1 for a bad length.
 */
int vmn_fft(float *re, float *im, size_t n);

#endif
