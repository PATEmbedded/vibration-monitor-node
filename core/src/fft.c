#include "vmn/fft.h"

#include <math.h>
#include <stdbool.h>

#include "vmn/config.h"

static float s_cos[VMN_FFT_MAX_LEN / 2u];
static float s_sin[VMN_FFT_MAX_LEN / 2u];
static bool s_ready;

void vmn_fft_init(void)
{
    if (s_ready) {
        return;
    }
    /* Double precision once at start-up keeps the table exact to float resolution. */
    const double step = -2.0 * 3.14159265358979323846 / (double)VMN_FFT_MAX_LEN;
    for (size_t k = 0; k < VMN_FFT_MAX_LEN / 2u; ++k) {
        s_cos[k] = (float)cos(step * (double)k);
        s_sin[k] = (float)sin(step * (double)k);
    }
    s_ready = true;
}

int vmn_fft(float *re, float *im, size_t n)
{
    if (re == NULL || im == NULL || n < 2u || n > VMN_FFT_MAX_LEN || (n & (n - 1u)) != 0u) {
        return -1;
    }
    vmn_fft_init();

    /* Bit-reversal permutation. */
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; (j & bit) != 0u; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            const float tr = re[i];
            const float ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }

    /* Butterflies; the table holds the twiddles of the longest FFT, so a shorter one strides it. */
    for (size_t len = 2; len <= n; len <<= 1) {
        const size_t half = len >> 1;
        const size_t stride = VMN_FFT_MAX_LEN / len;
        for (size_t base = 0; base < n; base += len) {
            for (size_t k = 0; k < half; ++k) {
                const float wr = s_cos[k * stride];
                const float wi = s_sin[k * stride];
                const size_t a = base + k;
                const size_t b = a + half;
                const float tr = re[b] * wr - im[b] * wi;
                const float ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
            }
        }
    }
    return 0;
}
