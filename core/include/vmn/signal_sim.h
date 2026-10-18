#ifndef VMN_SIGNAL_SIM_H
#define VMN_SIGNAL_SIM_H

#include <stddef.h>
#include <stdint.h>

/*
 * Synthetic vibration of a rotating machine. It stands in for the accelerometer in emulation
 * and in tests, and the Python tools use the same model to make the training data.
 *
 *   x(t) = sum of the 1x, 2x and 3x shaft harmonics
 *        + Gaussian noise
 *        + for a bearing fault, impulses at a multiple of the shaft speed, each ringing a
 *          structural resonance that decays exponentially
 */

/** Machine conditions; the order matches the classifier's classes. */
typedef enum {
    VMN_CONDITION_HEALTHY = 0,
    VMN_CONDITION_IMBALANCE = 1,
    VMN_CONDITION_MISALIGNMENT = 2,
    VMN_CONDITION_BEARING_FAULT = 3,
    VMN_CONDITION_COUNT
} vmn_condition_t;

typedef struct {
    float shaft_hz;         /**< shaft rotation frequency */
    float harmonic_amp[3];  /**< amplitude of the 1x, 2x and 3x components */
    float noise_rms;        /**< standard deviation of the noise */
    float impulse_amp;      /**< peak of each bearing impulse; 0 for no impulses */
    float impulse_ratio;    /**< impulse rate as a multiple of the shaft frequency */
    float resonance_hz;     /**< frequency that each impulse rings at */
    float decay_s;          /**< time constant of the ringing */
} vmn_sim_params_t;

/** Fills typical parameters for a condition at the given shaft speed. */
void vmn_sim_default_params(vmn_condition_t condition, float shaft_hz, vmn_sim_params_t *params);

/**
 * Writes n samples to out. rng is the random state (see vmn_prng_seed); it advances, so
 * successive calls give successive, different frames.
 */
void vmn_sim_generate(const vmn_sim_params_t *params, uint32_t *rng, float *out, size_t n,
                      float sample_rate_hz);

#endif
