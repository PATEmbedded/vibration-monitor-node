#include <string.h>

#include "unity.h"
#include "vmn/classifier.h"
#include "vmn/config.h"
#include "vmn/fft.h"
#include "vmn/prng.h"
#include "vmn/signal_sim.h"

static float s_x[VMN_FRAME_LEN];
static float s_re[VMN_FRAME_LEN];
static float s_im[VMN_FRAME_LEN];

void setUp(void)
{
    vmn_fft_init();
}

void tearDown(void) {}

static vmn_prediction_t classify_condition(vmn_condition_t condition, float shaft_hz, uint32_t seed)
{
    vmn_sim_params_t params;
    vmn_sim_default_params(condition, shaft_hz, &params);
    uint32_t rng = vmn_prng_seed(seed);
    vmn_sim_generate(&params, &rng, s_x, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ);
    vmn_features_t f;
    TEST_ASSERT_EQUAL_INT(0, vmn_features_compute(s_x, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, shaft_hz, s_re, s_im, &f));
    vmn_prediction_t pred;
    vmn_classify(f.values, &pred);
    return pred;
}

static void test_every_condition_is_recognised(void)
{
    const float speeds[] = {15.0f, 25.0f, 45.0f};
    for (int c = 0; c < VMN_CONDITION_COUNT; ++c) {
        for (unsigned s = 0; s < sizeof speeds / sizeof speeds[0]; ++s) {
            for (uint32_t seed = 1; seed <= 5; ++seed) {
                const vmn_prediction_t pred = classify_condition((vmn_condition_t)c, speeds[s], seed * 7919u);
                TEST_ASSERT_EQUAL_INT_MESSAGE(c, pred.cls, vmn_class_name(c));
                TEST_ASSERT_TRUE(pred.confidence > 0.5f && pred.confidence <= 1.0f);
            }
        }
    }
}

static void test_quantisation_saturates(void)
{
    float big[VMN_FEATURE_COUNT];
    int8_t q[VMN_FEATURE_COUNT];
    for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
        big[i] = (i % 2) ? 1e6f : -1e6f;
    }
    vmn_classifier_quantize_input(big, q);
    for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
        TEST_ASSERT_EQUAL_INT8((i % 2) ? 127 : -127, q[i]);
    }
}

static void test_logits_are_deterministic(void)
{
    int8_t q[VMN_FEATURE_COUNT];
    for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
        q[i] = (int8_t)(i * 9 - 60);
    }
    int32_t a[VMN_CLASS_COUNT];
    int32_t b[VMN_CLASS_COUNT];
    vmn_classifier_logits(q, a);
    vmn_classifier_logits(q, b);
    TEST_ASSERT_EQUAL_INT32_ARRAY(a, b, VMN_CLASS_COUNT);
}

static void test_class_names(void)
{
    TEST_ASSERT_EQUAL_STRING("healthy", vmn_class_name(0));
    TEST_ASSERT_EQUAL_STRING("bearing fault", vmn_class_name(3));
    TEST_ASSERT_EQUAL_STRING("unknown", vmn_class_name(4));
    TEST_ASSERT_EQUAL_STRING("unknown", vmn_class_name(-1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_every_condition_is_recognised);
    RUN_TEST(test_quantisation_saturates);
    RUN_TEST(test_logits_are_deterministic);
    RUN_TEST(test_class_names);
    return UNITY_END();
}
