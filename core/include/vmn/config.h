#ifndef VMN_CONFIG_H
#define VMN_CONFIG_H

/* Acquisition settings shared by the firmware, the host tools and the model. */

/** Samples per second of the vibration signal. */
#define VMN_SAMPLE_RATE_HZ 10240.0f

/** Samples per analysis frame; 0.1 s at the sample rate, 10 Hz per FFT bin. */
#define VMN_FRAME_LEN 1024u

/** Longest FFT the twiddle table supports. */
#define VMN_FFT_MAX_LEN 1024u

#endif
