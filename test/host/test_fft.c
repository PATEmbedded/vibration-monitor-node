#include <math.h>

#include "unity.h"
#include "vmn/fft.h"
#include "vmn/prng.h"

void setUp(void)
{
    vmn_fft_init();
}

void tearDown(void) {}

static void test_rejects_bad_lengths(void)
{
    float re[4] = {0};
    float im[4] = {0};
    TEST_ASSERT_EQUAL_INT(-1, vmn_fft(re, im, 0));
    TEST_ASSERT_EQUAL_INT(-1, vmn_fft(re, im, 3));
    TEST_ASSERT_EQUAL_INT(-1, vmn_fft(re, im, 2048));
    TEST_ASSERT_EQUAL_INT(-1, vmn_fft(NULL, im, 4));
}

static void test_impulse_has_flat_spectrum(void)
{
    float re[16] = {1.0f};
    float im[16] = {0};
    TEST_ASSERT_EQUAL_INT(0, vmn_fft(re, im, 16));
    for (int k = 0; k < 16; ++k) {
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, re[k]);
        TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, im[k]);
    }
}

static void test_matches_direct_dft(void)
{
    enum { N = 64 };
    float re[N];
    float im[N];
    double in_re[N];
    double in_im[N];
    uint32_t rng = vmn_prng_seed(42);
    for (int i = 0; i < N; ++i) {
        re[i] = vmn_prng_uniform(&rng) - 0.5f;
        im[i] = vmn_prng_uniform(&rng) - 0.5f;
        in_re[i] = (double)re[i];
        in_im[i] = (double)im[i];
    }
    TEST_ASSERT_EQUAL_INT(0, vmn_fft(re, im, N));
    for (int k = 0; k < N; ++k) {
        double sr = 0.0;
        double si = 0.0;
        for (int n = 0; n < N; ++n) {
            const double a = -2.0 * 3.14159265358979323846 * k * n / N;
            sr += in_re[n] * cos(a) - in_im[n] * sin(a);
            si += in_re[n] * sin(a) + in_im[n] * cos(a);
        }
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)sr, re[k]);
        TEST_ASSERT_FLOAT_WITHIN(1e-4f, (float)si, im[k]);
    }
}

static void test_cosine_lands_in_one_bin(void)
{
    enum { N = 1024, BIN = 37 };
    static float re[N];
    static float im[N];
    for (int i = 0; i < N; ++i) {
        re[i] = (float)cos(2.0 * 3.14159265358979323846 * BIN * i / N);
        im[i] = 0.0f;
    }
    TEST_ASSERT_EQUAL_INT(0, vmn_fft(re, im, N));
    for (int k = 0; k <= N / 2; ++k) {
        const float mag = sqrtf(re[k] * re[k] + im[k] * im[k]);
        TEST_ASSERT_FLOAT_WITHIN(0.05f, k == BIN ? N / 2.0f : 0.0f, mag);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_rejects_bad_lengths);
    RUN_TEST(test_impulse_has_flat_spectrum);
    RUN_TEST(test_matches_direct_dft);
    RUN_TEST(test_cosine_lands_in_one_bin);
    return UNITY_END();
}
