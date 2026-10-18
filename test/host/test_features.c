#include <math.h>

#include "unity.h"
#include "vmn/config.h"
#include "vmn/features.h"
#include "vmn/fft.h"

static float s_x[VMN_FRAME_LEN];
static float s_re[VMN_FRAME_LEN];
static float s_im[VMN_FRAME_LEN];

void setUp(void)
{
    vmn_fft_init();
}

void tearDown(void) {}

static void fill_sine(float amplitude, float hz)
{
    for (unsigned i = 0; i < VMN_FRAME_LEN; ++i) {
        s_x[i] = amplitude * sinf(6.28318530717958647692f * hz * (float)i / VMN_SAMPLE_RATE_HZ);
    }
}

static void test_sine_statistics(void)
{
    vmn_features_t f;
    fill_sine(0.5f, 40.0f); /* exactly on bin 4 */
    TEST_ASSERT_EQUAL_INT(0, vmn_features_compute(s_x, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, 40.0f, s_re, s_im, &f));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f / sqrtf(2.0f), f.rms);
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.5f, f.peak);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, sqrtf(2.0f), f.crest);
    TEST_ASSERT_FLOAT_WITHIN(1e-2f, 1.5f, f.kurtosis);
    TEST_ASSERT_FLOAT_WITHIN(5e-3f, 0.5f, f.harmonic[0]);
    TEST_ASSERT_TRUE(f.harmonic[1] < 0.01f);
}

static void test_energy_sits_in_the_right_band(void)
{
    vmn_features_t f;
    fill_sine(1.0f, 1000.0f); /* band 800-1600 Hz, index 5 */
    TEST_ASSERT_EQUAL_INT(0, vmn_features_compute(s_x, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, 25.0f, s_re, s_im, &f));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, f.values[7 + 5]); /* log10 of nearly all the energy */
    TEST_ASSERT_TRUE(f.values[7 + 0] < -6.0f);
}

static void test_off_bin_harmonic_is_found(void)
{
    vmn_features_t f;
    fill_sine(1.0f, 25.0f); /* half-way between bins 2 and 3 */
    TEST_ASSERT_EQUAL_INT(0, vmn_features_compute(s_x, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, 25.0f, s_re, s_im, &f));
    /* The Hann window loses at most 1.42 dB half-way between bins. */
    TEST_ASSERT_TRUE(f.harmonic[0] > 0.84f);
    TEST_ASSERT_TRUE(f.harmonic[0] <= 1.0f);
}

static void test_rejects_bad_arguments(void)
{
    vmn_features_t f;
    TEST_ASSERT_EQUAL_INT(-1, vmn_features_compute(NULL, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, 25.0f, s_re, s_im, &f));
    TEST_ASSERT_EQUAL_INT(-1, vmn_features_compute(s_x, 8, VMN_SAMPLE_RATE_HZ, 25.0f, s_re, s_im, &f));
    TEST_ASSERT_EQUAL_INT(-1, vmn_features_compute(s_x, 1000, VMN_SAMPLE_RATE_HZ, 25.0f, s_re, s_im, &f));
}

static void test_silence_gives_finite_values(void)
{
    vmn_features_t f;
    for (unsigned i = 0; i < VMN_FRAME_LEN; ++i) {
        s_x[i] = 0.25f;
    }
    TEST_ASSERT_EQUAL_INT(0, vmn_features_compute(s_x, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, 25.0f, s_re, s_im, &f));
    for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
        TEST_ASSERT_TRUE(isfinite(f.values[i]));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_sine_statistics);
    RUN_TEST(test_energy_sits_in_the_right_band);
    RUN_TEST(test_off_bin_harmonic_is_found);
    RUN_TEST(test_rejects_bad_arguments);
    RUN_TEST(test_silence_gives_finite_values);
    return UNITY_END();
}
