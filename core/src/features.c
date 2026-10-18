#include "vmn/features.h"

#include <math.h>
#include <stdbool.h>

#include "vmn/fft.h"

#define VMN_TWO_PI 6.28318530717958647692f

const float vmn_band_edges_hz[VMN_BAND_COUNT + 1] = {0.0f,   50.0f,   100.0f,  200.0f, 400.0f,
                                                      800.0f, 1600.0f, 3200.0f, 5120.0f};

static int band_of(float f)
{
    for (int b = 0; b < VMN_BAND_COUNT; ++b) {
        const bool last = b == VMN_BAND_COUNT - 1;
        if (f >= vmn_band_edges_hz[b] &&
            (f < vmn_band_edges_hz[b + 1] || (last && f <= vmn_band_edges_hz[b + 1]))) {
            return b;
        }
    }
    return -1;
}

int vmn_features_compute(const float *x, size_t n, float sample_rate_hz, float shaft_hz,
                         float *work_re, float *work_im, vmn_features_t *out)
{
    if (x == NULL || work_re == NULL || work_im == NULL || out == NULL || n < 16u ||
        sample_rate_hz <= 0.0f) {
        return -1;
    }

    /* Time domain, on the signal with its mean removed. */
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += x[i];
    }
    const float mean = sum / (float)n;

    float s2 = 0.0f;
    float s4 = 0.0f;
    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const float c = x[i] - mean;
        const float c2 = c * c;
        s2 += c2;
        s4 += c2 * c2;
        const float a = fabsf(c);
        if (a > peak) {
            peak = a;
        }
        /* Periodic Hann window for the spectrum. */
        work_re[i] = c * (0.5f - 0.5f * cosf(VMN_TWO_PI * (float)i / (float)n));
        work_im[i] = 0.0f;
    }
    const float var = s2 / (float)n;
    out->rms = sqrtf(var);
    out->peak = peak;
    out->crest = out->rms > 0.0f ? peak / out->rms : 0.0f;
    out->kurtosis = var > 0.0f ? (s4 / (float)n) / (var * var) : 0.0f;

    /* Amplitude spectrum: 2 |X[k]| / sum(window), and the sum of a periodic Hann window is n/2. */
    if (vmn_fft(work_re, work_im, n) != 0) {
        return -1;
    }
    const size_t half = n / 2u;
    const float scale = 4.0f / (float)n;
    for (size_t k = 0; k <= half; ++k) {
        work_re[k] = scale * sqrtf(work_re[k] * work_re[k] + work_im[k] * work_im[k]);
    }
    const float bin_hz = sample_rate_hz / (float)n;

    /* Harmonics: the largest amplitude within one bin of h times the shaft frequency. */
    for (int h = 0; h < 3; ++h) {
        const float target = (float)(h + 1) * shaft_hz / bin_hz;
        const long k0 = (long)floorf(target + 0.5f);
        float best = 0.0f;
        for (long k = k0 - 1; k <= k0 + 1; ++k) {
            if (k >= 1 && k <= (long)half - 1 && work_re[k] > best) {
                best = work_re[k];
            }
        }
        out->harmonic[h] = best;
    }

    /* Energy per band. */
    float total = 0.0f;
    for (int b = 0; b < VMN_BAND_COUNT; ++b) {
        out->band_energy[b] = 0.0f;
    }
    for (size_t k = 0; k <= half; ++k) {
        const int b = band_of((float)k * bin_hz);
        if (b >= 0) {
            const float e = work_re[k] * work_re[k];
            out->band_energy[b] += e;
            total += e;
        }
    }

    /* Classifier input: levels in log10, shape features relative to the overall level. */
    const float log_rms = log10f(out->rms + 1e-9f);
    const float log_total = log10f(total + 1e-12f);
    out->values[0] = log_rms;
    out->values[1] = log10f(out->peak + 1e-9f);
    out->values[2] = out->crest;
    out->values[3] = log10f(out->kurtosis + 1e-9f);
    for (int h = 0; h < 3; ++h) {
        out->values[4 + h] = log10f(out->harmonic[h] + 1e-9f) - log_rms;
    }
    for (int b = 0; b < VMN_BAND_COUNT; ++b) {
        out->values[7 + b] = log10f(out->band_energy[b] + 1e-12f) - log_total;
    }
    return 0;
}
