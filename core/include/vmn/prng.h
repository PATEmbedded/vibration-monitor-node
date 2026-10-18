#ifndef VMN_PRNG_H
#define VMN_PRNG_H

#include <stdint.h>

/*
 * xorshift32 random numbers. tools/vmnlib.py implements the same generator, so one seed
 * produces the same sequence on the PC, in CI and on the target.
 */

/** Returns a valid state for a seed; xorshift32 must never hold zero. */
static inline uint32_t vmn_prng_seed(uint32_t seed)
{
    return seed != 0u ? seed : 0x9E3779B9u;
}

static inline uint32_t vmn_prng_next(uint32_t *state)
{
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

/** Uniform value in [0, 1) with 24-bit resolution, so it is exact in a float. */
static inline float vmn_prng_uniform(uint32_t *state)
{
    return (float)(vmn_prng_next(state) >> 8) * (1.0f / 16777216.0f);
}

/** Approximately standard-normal value: the sum of 12 uniform values minus 6. */
static inline float vmn_prng_gauss(uint32_t *state)
{
    float sum = 0.0f;
    for (int i = 0; i < 12; ++i) {
        sum += vmn_prng_uniform(state);
    }
    return sum - 6.0f;
}

#endif
