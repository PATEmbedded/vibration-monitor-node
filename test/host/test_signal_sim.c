#include <math.h>
#include <string.h>

#include "unity.h"
#include "vmn/config.h"
#include "vmn/prng.h"
#include "vmn/signal_sim.h"

static float s_a[VMN_FRAME_LEN];
static float s_b[VMN_FRAME_LEN];

void setUp(void) {}
void tearDown(void) {}

static void generate(vmn_condition_t condition, uint32_t seed, float *out)
{
    vmn_sim_params_t params;
    vmn_sim_default_params(condition, 25.0f, &params);
    uint32_t rng = vmn_prng_seed(seed);
    vmn_sim_generate(&params, &rng, out, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ);
}

static float rms(const float *x)
{
    float s = 0.0f;
    for (unsigned i = 0; i < VMN_FRAME_LEN; ++i) {
        s += x[i] * x[i];
    }
    return sqrtf(s / (float)VMN_FRAME_LEN);
}

static void test_same_seed_same_frame(void)
{
    generate(VMN_CONDITION_BEARING_FAULT, 1234, s_a);
    generate(VMN_CONDITION_BEARING_FAULT, 1234, s_b);
    TEST_ASSERT_EQUAL_MEMORY(s_a, s_b, sizeof s_a);
    generate(VMN_CONDITION_BEARING_FAULT, 1235, s_b);
    TEST_ASSERT_TRUE(memcmp(s_a, s_b, sizeof s_a) != 0);
}

static void test_zero_seed_is_usable(void)
{
    uint32_t state = vmn_prng_seed(0);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, state);
    TEST_ASSERT_NOT_EQUAL_UINT32(0, vmn_prng_next(&state));
}

static void test_healthy_level_matches_the_parameters(void)
{
    /* sqrt(0.10^2/2 + 0.03^2/2 + 0.01^2/2 + 0.03^2) = 0.080 */
    generate(VMN_CONDITION_HEALTHY, 99, s_a);
    TEST_ASSERT_FLOAT_WITHIN(0.012f, 0.080f, rms(s_a));
}

static void test_faults_raise_the_level(void)
{
    generate(VMN_CONDITION_HEALTHY, 5, s_a);
    const float healthy = rms(s_a);
    for (int c = VMN_CONDITION_IMBALANCE; c < VMN_CONDITION_COUNT; ++c) {
        generate((vmn_condition_t)c, 5, s_b);
        TEST_ASSERT_TRUE(rms(s_b) > 1.5f * healthy);
    }
}

static void test_impulses_need_a_positive_rate(void)
{
    vmn_sim_params_t params;
    vmn_sim_default_params(VMN_CONDITION_BEARING_FAULT, 25.0f, &params);
    params.impulse_ratio = 0.0f;
    uint32_t rng = vmn_prng_seed(3);
    vmn_sim_generate(&params, &rng, s_a, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ);
    TEST_ASSERT_TRUE(rms(s_a) < 0.2f);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_same_seed_same_frame);
    RUN_TEST(test_zero_seed_is_usable);
    RUN_TEST(test_healthy_level_matches_the_parameters);
    RUN_TEST(test_faults_raise_the_level);
    RUN_TEST(test_impulses_need_a_positive_rate);
    return UNITY_END();
}
