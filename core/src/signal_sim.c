#include "vmn/signal_sim.h"

#include <math.h>

#include "vmn/prng.h"

#define VMN_TWO_PI 6.28318530717958647692f

void vmn_sim_default_params(vmn_condition_t condition, float shaft_hz, vmn_sim_params_t *params)
{
    params->shaft_hz = shaft_hz;
    params->harmonic_amp[0] = 0.10f;
    params->harmonic_amp[1] = 0.03f;
    params->harmonic_amp[2] = 0.01f;
    params->noise_rms = 0.03f;
    params->impulse_amp = 0.0f;
    params->impulse_ratio = 3.58f;
    params->resonance_hz = 2800.0f;
    params->decay_s = 0.002f;

    switch (condition) {
    case VMN_CONDITION_IMBALANCE:
        params->harmonic_amp[0] = 0.80f;
        params->harmonic_amp[1] = 0.05f;
        params->harmonic_amp[2] = 0.02f;
        break;
    case VMN_CONDITION_MISALIGNMENT:
        params->harmonic_amp[0] = 0.20f;
        params->harmonic_amp[1] = 0.50f;
        params->harmonic_amp[2] = 0.25f;
        break;
    case VMN_CONDITION_BEARING_FAULT:
        params->impulse_amp = 1.0f;
        break;
    case VMN_CONDITION_HEALTHY:
    case VMN_CONDITION_COUNT:
    default:
        break;
    }
}

void vmn_sim_generate(const vmn_sim_params_t *params, uint32_t *rng, float *out, size_t n,
                      float sample_rate_hz)
{
    /* The order of the random draws is part of the model: tools/vmnlib.py draws in the same order. */
    float phase[3];
    for (int h = 0; h < 3; ++h) {
        phase[h] = VMN_TWO_PI * vmn_prng_uniform(rng);
    }
    const float impulse_rate = params->impulse_ratio * params->shaft_hz;
    const float offset_draw = vmn_prng_uniform(rng);

    for (size_t i = 0; i < n; ++i) {
        const float t = (float)i / sample_rate_hz;
        float x = 0.0f;
        for (int h = 0; h < 3; ++h) {
            x += params->harmonic_amp[h] *
                 sinf(VMN_TWO_PI * (float)(h + 1) * params->shaft_hz * t + phase[h]);
        }
        out[i] = x + params->noise_rms * vmn_prng_gauss(rng);
    }

    if (params->impulse_amp <= 0.0f || impulse_rate <= 0.0f || params->decay_s <= 0.0f) {
        return;
    }
    const float duration = (float)n / sample_rate_hz;
    const float period = 1.0f / impulse_rate;
    const float first = offset_draw * period;
    const size_t tail = (size_t)(5.0f * params->decay_s * sample_rate_hz) + 1u;

    for (unsigned k = 0;; ++k) {
        const float t_k = first + (float)k * period;
        if (t_k >= duration) {
            break;
        }
        const size_t start = (size_t)ceilf(t_k * sample_rate_hz);
        for (size_t j = start; j < n && j <= start + tail; ++j) {
            const float dt = (float)j / sample_rate_hz - t_k;
            out[j] += params->impulse_amp * expf(-dt / params->decay_s) *
                      sinf(VMN_TWO_PI * params->resonance_hz * dt);
        }
    }
}
