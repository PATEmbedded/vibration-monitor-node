#ifndef VMN_FEATURES_H
#define VMN_FEATURES_H

#include <stddef.h>

#define VMN_BAND_COUNT 8
#define VMN_FEATURE_COUNT 15

/** Band edges in Hz; the last band also includes its upper edge. */
extern const float vmn_band_edges_hz[VMN_BAND_COUNT + 1];

typedef struct {
    float rms;                           /**< after removing the mean */
    float peak;                          /**< largest absolute deviation from the mean */
    float crest;                         /**< peak / rms */
    float kurtosis;                      /**< 3 for Gaussian noise, 1.5 for a sine, high for impulses */
    float harmonic[3];                   /**< amplitude at 1x, 2x and 3x the shaft frequency */
    float band_energy[VMN_BAND_COUNT];   /**< sum of squared amplitudes per band */
    float values[VMN_FEATURE_COUNT];     /**< the classifier's input, see README */
} vmn_features_t;

/**
 * Computes the features of one frame.
 *
 * x holds n samples (n a power of two, at least 16). work_re and work_im are scratch buffers of
 * n floats each. Returns 0, or -1 for a bad argument.
 */
int vmn_features_compute(const float *x, size_t n, float sample_rate_hz, float shaft_hz,
                         float *work_re, float *work_im, vmn_features_t *out);

#endif
